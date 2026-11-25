// SPDX-License-Identifier: GPL-2.0
/*
 * Bluetooth support for the HiSilicon Hi1102 combo chip (UART transport).
 *
 * The Hi1102 (Kirin 970 / Honor Play, COR-AL00) exposes Bluetooth over a UART
 * that is shared with GNSS.  On the wire each transfer is wrapped in a private
 * "0x7e" power-save frame; the payload of a BT frame is a standard H:4 packet.
 * This driver owns that UART, decodes the framing and presents a normal hciN
 * device to the HCI core (BlueZ / hciconfig), and re-exposes the de-framed
 * GNSS stream at /dev/gnss.
 *
 * It is self-contained: it does not use the vendor BFG stack, and reuses the
 * mainline H:4 reassembly (h4_recv_buf) and HCI core.  The vendor BFG path is
 * inert on this tree (it registers its line discipline at N_HW_BFG=35, which is
 * rejected because it exceeds NR_LDISCS), so this driver takes over the UART.
 *
 * Frame layout (no CRC):
 *   [0x7e][func_type][len_lo][len_hi][payload ...][0x7e]
 *   len = total length including the 4-byte head and 1-byte end, little-endian.
 *   func_type: SYS_MSG=0x00, BT_MSG=0x01, GNSS_First/Common/Last=0x02/03/04.
 *
 * Pattern sources: tty ownership + custom line discipline from
 * drivers/accessibility/speakup/spk_ttyio.c; H:4 + HCI-core attach from
 * drivers/bluetooth/btmtkuart.c; miscdevice /dev/gnss from drivers/gnss/core.c.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/export.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"

#define HI1102_DRIVER_VERSION	"1.0"

/*
 * Line-discipline number.  The vendor used N_HW_BFG=35, which mainline rejects
 * (tty_register_ldisc requires num < NR_LDISCS == 31).  Any free slot works;
 * N_DEVELOPMENT (29) is reserved by mainline for out-of-tree use.  Verified no
 * in-tree driver registers a line discipline at 29.
 */
#define N_HI1102	29

/* 0x7e framing constants (mirror the vendor hw_bfg_ps.h). */
#define HI1102_FRAME_START		0x7e
#define HI1102_FRAME_END		0x7e
#define HI1102_PACKET_HEAD_LEN		4	/* start + type + u16 len */
#define HI1102_PACKET_END_LEN		1
#define HI1102_PUBLIC_BUF_MAX		(8 * 1024)

/* func_type tags we care about. */
#define HI1102_MSG_SYS			0x00
#define HI1102_MSG_BT			0x01
#define HI1102_MSG_GNSS_FIRST		0x02
#define HI1102_MSG_GNSS_COMMON		0x03
#define HI1102_MSG_GNSS_LAST		0x04

/* SYS_MSG opcodes. */
#define HI1102_SYS_CFG_OPEN_BT		0
#define HI1102_SYS_CFG_CLOSE_BT		1
#define HI1102_SYS_CFG_OPEN_GNSS	2
#define HI1102_SYS_CFG_CLOSE_GNSS	3
#define HI1102_SYS_INF_BT_INIT		0x01
#define HI1102_SYS_INF_GNSS_INIT	0x02
#define HI1102_SYS_TOTAL_PACKET_LENTH	0x06

/* Per-subsys RX frame caps (vendor hw_bfg_ps.h:29-31). */
#define HI1102_BT_RX_MAX_FRAME		1500
#define HI1102_GNSS_RX_MAX_FRAME	10000

#define HI1102_WAIT_OPEN_TIME_MS	3000
#define HI1102_OPER_BAUDRATE		5000000

/* Default UART device node (alias serial4=&uart4 -> /dev/ttyAMA4). */
#define HI1102_DEFAULT_TTY		"ttyAMA4"

/* GNSS passthrough FIFO size. */
#define HI1102_GNSS_FIFO_SIZE		(64 * 1024)

struct hi1102_dev {
	struct device *dev;

	/* tty + line discipline */
	struct tty_struct *tty;
	struct platform_device *pdev;

	/* HCI side (hand-built hci_uart, btmtkuart pattern) */
	struct hci_dev *hdev;
	struct hci_uart hu;
	struct sk_buff *rx_skb;

	/* TX path */
	struct sk_buff_head txq;
	struct work_struct tx_work;

	/* power */
	struct regulator *wlan_en;
	bool powered;

	/* BT open handshake */
	struct completion bt_opened;
	bool bt_open_pending;

	/* Deferred bring-up: power + BT handshake + HCI register.  The BT
	 * handshake must NOT run inside the ldisc .open() callback, because
	 * tty_set_ldisc() holds tty->ldisc_sem for write while it invokes
	 * .open(), and the RX path takes ldisc_sem via ldsem_down_read_trylock()
	 * — so a blocking wait for the SYS_INF reply inside .open() could never
	 * be satisfied.  Run it from process context after the lock is released.
	 */
	struct work_struct bringup_work;

	/* GNSS open handshake */
	struct completion gnss_opened;
	bool gnss_open_pending;

	/* GNSS passthrough device */
	struct miscdevice gnss_misc_dev;
	DECLARE_KFIFO(gnss_fifo, u8, HI1102_GNSS_FIFO_SIZE);
	wait_queue_head_t gnss_read_queue;
	struct mutex gnss_read_lock;	/* serialises /dev/gnss readers */
	spinlock_t gnss_fifo_lock;	/* guards kfifo vs RX producer */
	bool gnss_misc_registered;
	bool gnss_disconnected;

	/* GNSS separated-frame reassembly (First/Common/Last). */
	u8 *gnss_rx_buf;
	u16 gnss_rx_len;

	/* 0x7e RX reassembly accumulator. */
	u8 rx_frame[HI1102_PUBLIC_BUF_MAX];
	u16 rx_len;			/* bytes accumulated into rx_frame */
	u16 rx_expect;			/* total frame length once head is parsed */

	bool ldisc_registered;
	struct mutex io_lock;		/* serialises open/close/power + fifo */
};

/* GNSS reassembly sequence states (mirror vendor RX_SEQ_*). */
#define HI1102_RX_SEQ_NULL	0
#define HI1102_RX_SEQ_START	1
#define HI1102_RX_SEQ_INT	2
#define HI1102_RX_SEQ_LAST	3

/* Our own H:4 receive table (btmtkuart's mtk_recv_pkts pattern). */
static const struct h4_recv_pkt hi1102_recv_pkts[] = {
	{ H4_RECV_ACL,    .recv = hci_recv_frame },
	{ H4_RECV_SCO,    .recv = hci_recv_frame },
	{ H4_RECV_EVENT,  .recv = hci_recv_frame },
	{ H4_RECV_ISO,    .recv = hci_recv_frame },
};

static struct hi1102_dev *hi1102_singleton;

/* ------------------------------------------------------------------------- */
/* GPS 32 kHz reference clock.                                               */
/*                                                                           */
/* The clock itself is owned by the vendor refclk driver                    */
/* (drivers/hisi/connectivity/hi11xx/.../platform/pm/gps_refclk_src_3.c).     */
/* That tree is mirrored from the Android kernel but is gated by              */
/* CONNECTIVITY_HI110X default n, so it is not built yet.  Provide a weak     */
/* no-op definition so this UART shim links on its own; once the             */
/* connectivity tree is ported its strong definition takes over at link      */
/* time without any change here.                                            */
/* ------------------------------------------------------------------------- */
enum hi1102_gps_modem_id { HI1102_GPS_MODEM_0 = 0 };
enum hi1102_gps_rat_mode { HI1102_GPS_RAT_MODE_0 = 0 };

int set_gps_ref_clk_enable_hi110x(bool enable,
				 enum hi1102_gps_modem_id modem_id,
				 enum hi1102_gps_rat_mode rat_mode);

int __weak set_gps_ref_clk_enable_hi110x(bool enable,
					 enum hi1102_gps_modem_id modem_id,
					 enum hi1102_gps_rat_mode rat_mode)
{
	(void)enable;
	(void)modem_id;
	(void)rat_mode;

	return 0;
}

static void hi1102_gps_refclk_set(bool enable)
{
	set_gps_ref_clk_enable_hi110x(enable, HI1102_GPS_MODEM_0,
				      HI1102_GPS_RAT_MODE_0);
}

/* ------------------------------------------------------------------------- */
/* Power (shared wlan_en regulator).                                         */
/* ------------------------------------------------------------------------- */
static int hi1102_power_on(struct hi1102_dev *h)
{
	int ret;

	if (h->powered)
		return 0;

	/* The vendor toggles the line low -> 10ms -> high -> 30ms.  With a fixed
	 * regulator "enable-active-high" this is achieved by a disable/enable
	 * cycle bracketed by delays.
	 */
	if (h->wlan_en) {
		ret = regulator_disable(h->wlan_en);
		if (ret)
			dev_warn(h->dev, "wlan_en disable: %d\n", ret);
		msleep(10);

		ret = regulator_enable(h->wlan_en);
		if (ret) {
			dev_err(h->dev, "wlan_en enable failed: %d\n", ret);
			return ret;
		}
		msleep(30);
	}

	h->powered = true;
	return 0;
}

static void hi1102_power_off(struct hi1102_dev *h)
{
	if (!h->powered)
		return;

	hi1102_gps_refclk_set(false);

	if (h->wlan_en)
		regulator_disable(h->wlan_en);

	h->powered = false;
}

/* ------------------------------------------------------------------------- */
/* 0x7e framing: TX.                                                         */
/* ------------------------------------------------------------------------- */

/* Write raw bytes to the tty. Returns bytes written or a negative error. */
static int hi1102_tty_write(struct hi1102_dev *h, const u8 *buf, size_t len)
{
	struct tty_struct *tty = h->tty;

	if (!tty || !tty->ops || !tty->ops->write)
		return -ENODEV;

	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	return tty->ops->write(tty, buf, len);
}

/* Wrap payload in a 0x7e frame and send it.  Caller must hold io_lock. */
static int __hi1102_send_frame(struct hi1102_dev *h, u8 type,
			       const u8 *payload, u16 payload_len)
{
	u8 head[HI1102_PACKET_HEAD_LEN];
	u8 end = HI1102_FRAME_END;
	u16 total;
	int ret;

	total = HI1102_PACKET_HEAD_LEN + payload_len + HI1102_PACKET_END_LEN;
	if (total > HI1102_PUBLIC_BUF_MAX)
		return -EINVAL;

	head[0] = HI1102_FRAME_START;
	head[1] = type;
	head[2] = total & 0xff;
	head[3] = (total >> 8) & 0xff;

	ret = hi1102_tty_write(h, head, sizeof(head));
	if (ret < 0)
		goto out;
	if (payload_len) {
		ret = hi1102_tty_write(h, payload, payload_len);
		if (ret < 0)
			goto out;
	}
	ret = hi1102_tty_write(h, &end, 1);
out:
	return ret < 0 ? ret : 0;
}

static int hi1102_send_frame(struct hi1102_dev *h, u8 type,
			     const u8 *payload, u16 payload_len)
{
	int ret;

	mutex_lock(&h->io_lock);
	ret = __hi1102_send_frame(h, type, payload, payload_len);
	mutex_unlock(&h->io_lock);

	return ret;
}

/* Send a 1-byte SYS_MSG opcode (BT/GNSS open/close). */
static int hi1102_send_sys(struct hi1102_dev *h, u8 opcode)
{
	u8 payload = opcode;

	/* SYS_MSG total length is fixed at 6 (head 4 + opcode 1 + end 1). */
	return hi1102_send_frame(h, HI1102_MSG_SYS, &payload,
				 HI1102_SYS_TOTAL_PACKET_LENTH
					- HI1102_PACKET_HEAD_LEN
					- HI1102_PACKET_END_LEN);
}

/* Same as hi1102_send_sys() but the caller already holds io_lock. */
static int __hi1102_send_sys(struct hi1102_dev *h, u8 opcode)
{
	u8 payload = opcode;

	return __hi1102_send_frame(h, HI1102_MSG_SYS, &payload,
				   HI1102_SYS_TOTAL_PACKET_LENTH
					- HI1102_PACKET_HEAD_LEN
					- HI1102_PACKET_END_LEN);
}

/* ------------------------------------------------------------------------- */
/* 0x7e framing: RX reassembly + dispatch.                                    */
/* ------------------------------------------------------------------------- */

static void hi1102_deliver_bt(struct hi1102_dev *h, const u8 *payload,
			      u16 len)
{
	/* payload is a complete H:4 packet; feed it to mainline H:4 reassembly. */
	if (!h->hdev)
		return;

	h->rx_skb = h4_recv_buf(&h->hu, h->rx_skb, payload, len,
				hi1102_recv_pkts,
				ARRAY_SIZE(hi1102_recv_pkts));
	if (IS_ERR(h->rx_skb)) {
		bt_dev_err(h->hdev, "H:4 reassembly failed (%ld)",
			   PTR_ERR(h->rx_skb));
		h->rx_skb = NULL;
		return;
	}

	h->hdev->stat.byte_rx += len;
}

static void hi1102_gnss_flush(struct hi1102_dev *h)
{
	unsigned long flags;

	if (!h->gnss_rx_len)
		return;

	spin_lock_irqsave(&h->gnss_fifo_lock, flags);
	if (kfifo_avail(&h->gnss_fifo) >= h->gnss_rx_len)
		kfifo_in(&h->gnss_fifo, h->gnss_rx_buf, h->gnss_rx_len);
	else
		dev_warn(h->dev, "gnss fifo full, dropped %u bytes\n",
			 h->gnss_rx_len);
	spin_unlock_irqrestore(&h->gnss_fifo_lock, flags);

	wake_up_interruptible(&h->gnss_read_queue);

	h->gnss_rx_len = 0;
}

static void hi1102_deliver_gnss(struct hi1102_dev *h, u8 type,
				const u8 *payload, u16 len)
{
	/* GNSS arrives as First/Common/Last segments that must be concatenated
	 * into one logical stream, mirroring ps_recv_sepreated_data().
	 */
	u16 avail;

	if (len > HI1102_GNSS_RX_MAX_FRAME)
		return;

	avail = HI1102_GNSS_RX_MAX_FRAME - h->gnss_rx_len;
	if (len > avail) {
		dev_warn(h->dev, "gnss reassembly overflow, reset\n");
		h->gnss_rx_len = 0;
		return;
	}

	memcpy(h->gnss_rx_buf + h->gnss_rx_len, payload, len);
	h->gnss_rx_len += len;

	if (type == HI1102_MSG_GNSS_LAST)
		hi1102_gnss_flush(h);
}

static void hi1102_deliver_sys(struct hi1102_dev *h, const u8 *payload,
			       u16 len)
{
	u8 opcode;

	if (len < 1)
		return;
	opcode = payload[0];

	switch (opcode) {
	case HI1102_SYS_INF_BT_INIT:
		if (h->bt_open_pending) {
			h->bt_open_pending = false;
			complete(&h->bt_opened);
		}
		break;
	case HI1102_SYS_INF_GNSS_INIT:
		if (h->gnss_open_pending) {
			h->gnss_open_pending = false;
			complete(&h->gnss_opened);
		}
		break;
	default:
		break;
	}
}

static void hi1102_parse_frame(struct hi1102_dev *h, const u8 *frame,
			       u16 total_len)
{
	u8 type;
	u16 payload_len;
	const u8 *payload;

	type = frame[1];
	payload = frame + HI1102_PACKET_HEAD_LEN;
	payload_len = total_len - HI1102_PACKET_HEAD_LEN
			- HI1102_PACKET_END_LEN;

	switch (type) {
	case HI1102_MSG_SYS:
		hi1102_deliver_sys(h, payload, payload_len);
		break;
	case HI1102_MSG_BT:
		hi1102_deliver_bt(h, payload, payload_len);
		break;
	case HI1102_MSG_GNSS_FIRST:
	case HI1102_MSG_GNSS_COMMON:
	case HI1102_MSG_GNSS_LAST:
		hi1102_deliver_gnss(h, type, payload, payload_len);
		break;
	default:
		dev_dbg(h->dev, "ignore frame type 0x%02x\n", type);
		break;
	}
}

/* Line-discipline RX entry: reassemble 0x7e frames from the byte stream. */
static void hi1102_receive_buf(struct hi1102_dev *h, const u8 *cp, size_t count)
{
	const u8 *p = cp;
	size_t left = count;

	/* The accumulator lives in the device struct (not function statics) so
	 * the parser is safe if receive_buf2 is ever reentered on another CPU.
	 */
	while (left > 0) {
		size_t take;

		if (h->rx_len < HI1102_PACKET_HEAD_LEN) {
			/* Accumulate head, hunting for the 0x7e start byte first. */
			if (h->rx_len == 0) {
				while (left > 0 && *p != HI1102_FRAME_START) {
					p++;
					left--;
				}
				if (left == 0)
					break;
			}
			take = min_t(size_t, HI1102_PACKET_HEAD_LEN - h->rx_len,
				     left);
			memcpy(h->rx_frame + h->rx_len, p, take);
			h->rx_len += take;
			p += take;
			left -= take;
			if (h->rx_len < HI1102_PACKET_HEAD_LEN)
				break;

			/* Head complete: validate + parse total length. */
			if (h->rx_frame[0] != HI1102_FRAME_START) {
				h->rx_len = 0;	/* resync from next byte */
				continue;
			}
			h->rx_expect = h->rx_frame[2] | (h->rx_frame[3] << 8);
			if (h->rx_expect < HI1102_PACKET_HEAD_LEN + 1 ||
			    h->rx_expect > HI1102_PUBLIC_BUF_MAX) {
				h->rx_len = 0;	/* bad length: resync */
				continue;
			}
			continue;
		}

		/* Body: (rx_expect - head) bytes including the end byte. */
		take = min_t(size_t, h->rx_expect - h->rx_len, left);
		memcpy(h->rx_frame + h->rx_len, p, take);
		h->rx_len += take;
		p += take;
		left -= take;

		if (h->rx_len >= h->rx_expect) {
			hi1102_parse_frame(h, h->rx_frame, h->rx_expect);
			h->rx_len = 0;
		}
	}
}

/* ------------------------------------------------------------------------- */
/* BT TX path (HCI core -> 0x7e -> tty).                                      */
/* ------------------------------------------------------------------------- */

static void hi1102_tx_work(struct work_struct *work)
{
	struct hi1102_dev *h = container_of(work, struct hi1102_dev, tx_work);

	while (true) {
		struct sk_buff *skb = skb_dequeue(&h->txq);
		int len;

		if (!skb)
			break;

		/* The HCI core hands us a frame without the H:4 type byte; the
		 * transport owns prepending it (hci_h4 / btmtkuart both do this).
		 */
		memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

		len = hi1102_send_frame(h, HI1102_MSG_BT, skb->data, skb->len);
		if (len < 0) {
			bt_dev_err(h->hdev, "tx write failed (%d)\n", len);
			kfree_skb(skb);
			continue;
		}

		switch (hci_skb_pkt_type(skb)) {
		case HCI_COMMAND_PKT:
			h->hdev->stat.cmd_tx++;
			break;
		case HCI_ACLDATA_PKT:
			h->hdev->stat.acl_tx++;
			break;
		case HCI_SCODATA_PKT:
			h->hdev->stat.sco_tx++;
			break;
		default:
			break;
		}
		h->hdev->stat.byte_tx += skb->len;

		kfree_skb(skb);
	}
}

static int hi1102_hci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hi1102_dev *h = hci_get_drvdata(hdev);

	skb_queue_tail(&h->txq, skb);
	schedule_work(&h->tx_work);

	return 0;
}

static int hi1102_hci_open(struct hci_dev *hdev)
{
	/* Transport is brought up when the ldisc attaches; nothing to do here. */
	return 0;
}

static int hi1102_hci_close(struct hci_dev *hdev)
{
	struct hi1102_dev *h = hci_get_drvdata(hdev);

	skb_queue_purge(&h->txq);
	flush_work(&h->tx_work);
	kfree_skb(h->rx_skb);
	h->rx_skb = NULL;

	return 0;
}

/* ------------------------------------------------------------------------- */
/* HCI device lifecycle.                                                     */
/* ------------------------------------------------------------------------- */

static int hi1102_hci_register(struct hi1102_dev *h)
{
	struct hci_dev *hdev;
	int ret;

	if (h->hdev)
		return 0;

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	h->hdev = hdev;
	h->hu.tty = h->tty;
	h->hu.hdev = hdev;

	hci_set_drvdata(hdev, h);
	SET_HCIDEV_DEV(hdev, h->dev);

	hdev->bus = HCI_UART;
	hdev->open = hi1102_hci_open;
	hdev->close = hi1102_hci_close;
	hdev->send = hi1102_hci_send_frame;
	hdev->flush = hi1102_hci_close;

	ret = hci_register_dev(hdev);
	if (ret < 0) {
		bt_dev_err(hdev, "Can't register HCI device (%d)\n", ret);
		hci_free_dev(hdev);
		h->hdev = NULL;
		return ret;
	}

	return 0;
}

static void hi1102_hci_unregister(struct hi1102_dev *h)
{
	if (!h->hdev)
		return;

	hci_unregister_dev(h->hdev);
	hci_free_dev(h->hdev);
	h->hdev = NULL;
}

/* ------------------------------------------------------------------------- */
/* Line discipline.                                                          */
/* ------------------------------------------------------------------------- */

static int hi1102_ldisc_open(struct tty_struct *tty)
{
	struct hi1102_dev *h = hi1102_singleton;

	if (!h)
		return -ENODEV;
	if (tty != h->tty)
		return -ENODEV;
	if (!tty->ops->write)
		return -EOPNOTSUPP;

	/* Defer the power-on + handshake to process context.  tty_set_ldisc()
	 * holds tty->ldisc_sem for write while calling us, and the RX path
	 * acquires that sem via ldsem_down_read_trylock(); a blocking wait for
	 * the SYS_INF reply here could never complete.  Returning immediately
	 * lets the lock drop so receive_buf2 can deliver the reply.
	 */
	schedule_work(&h->bringup_work);
	return 0;
}

static void hi1102_bringup_work(struct work_struct *work)
{
	struct hi1102_dev *h = container_of(work, struct hi1102_dev,
					    bringup_work);
	int ret;
	long t;

	if (!h->tty)
		return;

	mutex_lock(&h->io_lock);

	/* Power the chip up before the handshake. */
	ret = hi1102_power_on(h);
	if (ret) {
		mutex_unlock(&h->io_lock);
		return;
	}

	/* BT open handshake: send SYS_CFG_OPEN_BT, await SYS_INF_BT_INIT. */
	reinit_completion(&h->bt_opened);
	h->bt_open_pending = true;
	ret = __hi1102_send_sys(h, HI1102_SYS_CFG_OPEN_BT);
	if (ret) {
		h->bt_open_pending = false;
		hi1102_power_off(h);
		mutex_unlock(&h->io_lock);
		dev_err(h->dev, "BT open send failed (%d)\n", ret);
		return;
	}
	mutex_unlock(&h->io_lock);

	t = wait_for_completion_timeout(&h->bt_opened,
					msecs_to_jiffies(HI1102_WAIT_OPEN_TIME_MS));
	if (t == 0) {
		dev_err(h->dev, "BT open handshake timed out\n");
		h->bt_open_pending = false;
		mutex_lock(&h->io_lock);
		hi1102_power_off(h);
		mutex_unlock(&h->io_lock);
		return;
	}

	/* Register the HCI device now that BT is up. */
	ret = hi1102_hci_register(h);
	if (ret) {
		mutex_lock(&h->io_lock);
		__hi1102_send_sys(h, HI1102_SYS_CFG_CLOSE_BT);
		hi1102_power_off(h);
		mutex_unlock(&h->io_lock);
	}
}

static void hi1102_ldisc_close(struct tty_struct *tty)
{
	struct hi1102_dev *h = hi1102_singleton;

	if (!h || tty != h->tty)
		return;

	/* Cancel any pending/racing bring-up before tearing down. */
	cancel_work_sync(&h->bringup_work);

	hi1102_hci_unregister(h);

	mutex_lock(&h->io_lock);
	if (h->bt_open_pending) {
		h->bt_open_pending = false;
		complete(&h->bt_opened);
	}
	__hi1102_send_sys(h, HI1102_SYS_CFG_CLOSE_BT);
	hi1102_power_off(h);
	mutex_unlock(&h->io_lock);
}

static size_t hi1102_ldisc_receive_buf2(struct tty_struct *tty, const u8 *cp,
					const u8 *fp, size_t count)
{
	struct hi1102_dev *h = hi1102_singleton;

	if (!h || tty != h->tty)
		return count;

	hi1102_receive_buf(h, cp, count);

	return count;
}

static void hi1102_ldisc_write_wakeup(struct tty_struct *tty)
{
	struct hi1102_dev *h = hi1102_singleton;

	if (h && tty == h->tty)
		schedule_work(&h->tx_work);
}

static struct tty_ldisc_ops hi1102_ldisc_ops = {
	.owner		= THIS_MODULE,
	.num		= N_HI1102,
	.name		= "hi1102_ldisc",
	.open		= hi1102_ldisc_open,
	.close		= hi1102_ldisc_close,
	.receive_buf2	= hi1102_ldisc_receive_buf2,
	.write_wakeup	= hi1102_ldisc_write_wakeup,
};

/* ------------------------------------------------------------------------- */
/* tty ownership (speakup spk_ttyio_initialise_ldisc pattern).               */
/* ------------------------------------------------------------------------- */

static int hi1102_open_tty(struct hi1102_dev *h, const char *name)
{
	struct tty_struct *tty;
	struct ktermios tmp_termios;
	dev_t dev;
	int ret;

	ret = tty_dev_name_to_number(name, &dev);
	if (ret) {
		dev_err(h->dev, "cannot resolve tty name %s (%d)\n", name, ret);
		return ret;
	}

	tty = tty_kopen_exclusive(dev);
	if (IS_ERR(tty))
		return PTR_ERR(tty);

	if (tty->ops->open)
		ret = tty->ops->open(tty, NULL);
	else
		ret = -ENODEV;
	if (ret) {
		tty_unlock(tty);
		return ret;
	}

	clear_bit(TTY_HUPPED, &tty->flags);

	/* Set operating baud rate + hardware flow control. */
	down_read(&tty->termios_rwsem);
	tmp_termios = tty->termios;
	up_read(&tty->termios_rwsem);

	tty_termios_encode_baud_rate(&tmp_termios, HI1102_OPER_BAUDRATE,
				     HI1102_OPER_BAUDRATE);
	tmp_termios.c_cflag |= CRTSCTS;
	tty_set_termios(tty, &tmp_termios);

	tty_unlock(tty);

	h->tty = tty;

	ret = tty_set_ldisc(tty, N_HI1102);
	if (ret) {
		dev_err(h->dev, "failed to set N_HI1102 ldisc (%d)\n", ret);
		tty_lock(tty);
		if (tty->ops->close)
			tty->ops->close(tty, NULL);
		tty_unlock(tty);
		tty_kclose(tty);
		h->tty = NULL;
		return ret;
	}

	return 0;
}

static void hi1102_close_tty(struct hi1102_dev *h)
{
	struct tty_struct *tty = h->tty;

	if (!tty)
		return;

	/* Match speakup's release: close, flush the ldisc, then kclose (which
	 * runs tty_ldisc_release() to detach our discipline).  No manual
	 * set_ldisc(NULL) — tty_kclose handles detaching.
	 */
	tty_lock(tty);
	if (tty->ops->close)
		tty->ops->close(tty, NULL);
	tty_ldisc_flush(tty);
	tty_unlock(tty);

	tty_kclose(tty);
	h->tty = NULL;
}

/* ------------------------------------------------------------------------- */
/* /dev/gnss miscdevice.                                                     */
/* ------------------------------------------------------------------------- */

static int hi1102_gnss_open(struct inode *inode, struct file *file)
{
	struct hi1102_dev *h = hi1102_singleton;
	long t;
	int ret;

	if (!h)
		return -ENODEV;

	mutex_lock(&h->gnss_read_lock);
	h->gnss_disconnected = false;
	mutex_unlock(&h->gnss_read_lock);

	/* Assert the 32 kHz reference clock before the handshake. */
	hi1102_gps_refclk_set(true);

	/* GNSS open handshake. */
	reinit_completion(&h->gnss_opened);
	h->gnss_open_pending = true;
	ret = hi1102_send_sys(h, HI1102_SYS_CFG_OPEN_GNSS);
	if (ret) {
		h->gnss_open_pending = false;
		hi1102_gps_refclk_set(false);
		return ret;
	}

	t = wait_for_completion_timeout(&h->gnss_opened,
					msecs_to_jiffies(HI1102_WAIT_OPEN_TIME_MS));
	if (t == 0) {
		dev_err(h->dev, "GNSS open handshake timed out\n");
		h->gnss_open_pending = false;
		hi1102_gps_refclk_set(false);
		return -ETIMEDOUT;
	}

	file->private_data = h;
	return 0;
}

static int hi1102_gnss_release(struct inode *inode, struct file *file)
{
	struct hi1102_dev *h = file->private_data;

	if (!h)
		return 0;

	h->gnss_open_pending = false;
	complete(&h->gnss_opened);

	/* Wake any blocked readers so they observe EOF / -EAGAIN. */
	mutex_lock(&h->gnss_read_lock);
	h->gnss_disconnected = true;
	mutex_unlock(&h->gnss_read_lock);
	wake_up_interruptible(&h->gnss_read_queue);

	hi1102_send_sys(h, HI1102_SYS_CFG_CLOSE_GNSS);
	hi1102_gps_refclk_set(false);

	return 0;
}

static ssize_t hi1102_gnss_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hi1102_dev *h = file->private_data;
	unsigned int copied;
	int ret;

	if (!h)
		return -ENODEV;

	/* Mirror drivers/gnss/core.c: a mutex serialises concurrent readers so
	 * only one thread drains the fifo at a time (kfifo is otherwise
	 * lockless between the single producer and the reader).
	 */
	mutex_lock(&h->gnss_read_lock);
	while (kfifo_is_empty(&h->gnss_fifo)) {
		if (h->gnss_disconnected) {
			mutex_unlock(&h->gnss_read_lock);
			return 0;
		}

		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&h->gnss_read_lock);
			return -EAGAIN;
		}

		mutex_unlock(&h->gnss_read_lock);
		ret = wait_event_interruptible(h->gnss_read_queue,
				h->gnss_disconnected ||
				!kfifo_is_empty(&h->gnss_fifo));
		if (ret)
			return -ERESTARTSYS;
		mutex_lock(&h->gnss_read_lock);
	}

	ret = kfifo_to_user(&h->gnss_fifo, buf, count, &copied);
	if (ret == 0)
		ret = copied;

	mutex_unlock(&h->gnss_read_lock);

	return ret;
}

static ssize_t hi1102_gnss_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct hi1102_dev *h = file->private_data;
	u8 *tmp;
	int ret;

	if (!h)
		return -ENODEV;
	if (count == 0)
		return 0;
	if (count > HI1102_GNSS_RX_MAX_FRAME)
		count = HI1102_GNSS_RX_MAX_FRAME;

	tmp = memdup_user(buf, count);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	ret = hi1102_send_frame(h, HI1102_MSG_GNSS_FIRST, tmp, count);
	kfree(tmp);
	if (ret < 0)
		return ret;

	return count;
}

static __poll_t hi1102_gnss_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct hi1102_dev *h = file->private_data;
	__poll_t mask = 0;

	if (!h)
		return EPOLLHUP;

	poll_wait(file, &h->gnss_read_queue, wait);

	if (!kfifo_is_empty(&h->gnss_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static const struct file_operations hi1102_gnss_fops = {
	.owner		= THIS_MODULE,
	.open		= hi1102_gnss_open,
	.release	= hi1102_gnss_release,
	.read		= hi1102_gnss_read,
	.write		= hi1102_gnss_write,
	.poll		= hi1102_gnss_poll,
};

/* ------------------------------------------------------------------------- */
/* Platform driver (DT-discoverable, owns the shared supply + tty name).     */
/* ------------------------------------------------------------------------- */

static const char *hi1102_get_tty_name(struct hi1102_dev *h)
{
	const char *name = NULL;

	if (h->dev && h->dev->of_node)
		of_property_read_string(h->dev->of_node, "uart-port", &name);

	return name ? name : HI1102_DEFAULT_TTY;
}

static int hi1102_probe(struct platform_device *pdev)
{
	struct hi1102_dev *h;
	int ret;

	h = devm_kzalloc(&pdev->dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	h->dev = &pdev->dev;
	h->powered = false;

	mutex_init(&h->io_lock);
	spin_lock_init(&h->gnss_fifo_lock);
	init_completion(&h->bt_opened);
	init_completion(&h->gnss_opened);
	skb_queue_head_init(&h->txq);
	INIT_WORK(&h->tx_work, hi1102_tx_work);
	INIT_WORK(&h->bringup_work, hi1102_bringup_work);
	init_waitqueue_head(&h->gnss_read_queue);
	mutex_init(&h->gnss_read_lock);

	h->gnss_rx_buf = devm_kzalloc(&pdev->dev, HI1102_GNSS_RX_MAX_FRAME,
				      GFP_KERNEL);
	if (!h->gnss_rx_buf)
		return -ENOMEM;
	INIT_KFIFO(h->gnss_fifo);

	/* Shared power supply (refcounted with WiFi through the regulator). */
	h->wlan_en = devm_regulator_get(&pdev->dev, "wlan-en");
	if (IS_ERR(h->wlan_en)) {
		ret = PTR_ERR(h->wlan_en);
		dev_err(&pdev->dev, "failed to get wlan-en supply: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, h);
	hi1102_singleton = h;

	/* Register the line discipline before opening the tty. */
	ret = tty_register_ldisc(&hi1102_ldisc_ops);
	if (ret) {
		dev_err(&pdev->dev, "cannot register ldisc (%d)\n", ret);
		hi1102_singleton = NULL;
		return ret;
	}
	h->ldisc_registered = true;

	/* /dev/gnss passthrough. */
	h->gnss_misc_dev.minor = MISC_DYNAMIC_MINOR;
	h->gnss_misc_dev.name = "gnss";
	h->gnss_misc_dev.fops = &hi1102_gnss_fops;
	h->gnss_misc_dev.parent = &pdev->dev;
	ret = misc_register(&h->gnss_misc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot register /dev/gnss (%d)\n", ret);
		goto err_unreg_ldisc;
	}
	h->gnss_misc_registered = true;

	/* Own the UART + bring up the transport. */
	ret = hi1102_open_tty(h, hi1102_get_tty_name(h));
	if (ret) {
		dev_err(&pdev->dev, "cannot open tty (%d)\n", ret);
		goto err_unreg_misc;
	}

	dev_info(&pdev->dev, "Hi1102 BT/GPS bridge v%s on %s\n",
		 HI1102_DRIVER_VERSION, hi1102_get_tty_name(h));

	return 0;

err_unreg_misc:
	misc_deregister(&h->gnss_misc_dev);
	h->gnss_misc_registered = false;
err_unreg_ldisc:
	if (h->ldisc_registered) {
		tty_unregister_ldisc(&hi1102_ldisc_ops);
		h->ldisc_registered = false;
	}
	hi1102_singleton = NULL;
	return ret;
}

static void hi1102_remove(struct platform_device *pdev)
{
	struct hi1102_dev *h = platform_get_drvdata(pdev);

	if (!h)
		return;

	/* Stop any in-flight bring-up before the tty goes away. */
	cancel_work_sync(&h->bringup_work);

	hi1102_close_tty(h);

	if (h->gnss_misc_registered)
		misc_deregister(&h->gnss_misc_dev);

	if (h->ldisc_registered) {
		tty_unregister_ldisc(&hi1102_ldisc_ops);
		h->ldisc_registered = false;
	}

	flush_work(&h->tx_work);
	skb_queue_purge(&h->txq);
	kfree_skb(h->rx_skb);
	h->rx_skb = NULL;

	hi1102_singleton = NULL;
}

static const struct of_device_id hi1102_of_match[] = {
	{ .compatible = "hisilicon,hi1102-bt" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi1102_of_match);

static struct platform_driver hi1102_driver = {
	.probe	= hi1102_probe,
	.remove	= hi1102_remove,
	.driver	= {
		.name	= "hci_hi1102",
		.of_match_table = hi1102_of_match,
	},
};

static int __init hi1102_init(void)
{
	return platform_driver_register(&hi1102_driver);
}

static void __exit hi1102_exit(void)
{
	platform_driver_unregister(&hi1102_driver);
}

module_init(hi1102_init);
module_exit(hi1102_exit);

MODULE_AUTHOR("Kirin970 mainline port");
MODULE_DESCRIPTION("Bluetooth support for HiSilicon Hi1102 (UART)");
MODULE_VERSION(HI1102_DRIVER_VERSION);
MODULE_LICENSE("GPL");
