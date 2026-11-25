/*
 * IOM3 sensorhub client driver (Kirin 970 / COR-AL00, Hi3670)
 *
 * The Honor Play's motion and environment sensors (accelerometer, gyroscope,
 * magnetometer, ambient light, proximity) are physically wired to the **IOM3
 * MCU** - a small Cortex-M core that owns its own I2C bus - not to the
 * application processor.
 * They are reached from Linux through the AP <-> IOM3 inter-processor mailbox,
 * a proprietary packet protocol this driver speaks directly.
 *
 * Architecture (why a standalone driver, and why it is small):
 *   - The IOM3 boots its sensor firmware from ROM and is woken by a single
 *     mailbox "doorbell" (STARTUP_IOM3_CMD).  There is NO firmware download in
 *     this driver - the vendor's 4605-line hisp_remoteproc / 19853-line inputhub
 *     framework are off the boot path, exactly like the FTS flash path was.
 *   - AP <-> IOM3 traffic is a sequence of packets, each <= 32 bytes (8 mailbox
 *     words), framed as an 8-byte header + payload.  Larger payloads are chunked
 *     by the sender; there is no shared-memory ring on the sensor-report path
 *     (the shmem regions are only used for bulk load-monitor dumps), so this
 *     driver talks purely over the mailbox.
 *   - The AO mailbox silicon (HiIPCV230 @ 0xfff22000) is driven register-for-
 *     register here rather than through the mainline "hi3670-mbox" controller
 *     driver, which is registered for the separate LPM3 controller at
 *     0xe896b000.  The register sequence below is ported from the mainline
 *     hi3670_mbox_send_data() (identical register offsets) and the vendor
 *     hisi_mailbox RX path.  Keeping it local means no shared mailbox driver
 *     state is involved - the same rule every port in this tree follows.
 *
 * MODULE_LICENSE: GPL (ioremap / input core / mailbox regs all fine).
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#define DRIVER_NAME	"iom3_sensorhub"

/*
 * HiIPCV230 mailbox register map (per-channel base = ch * 0x40).
 * Verified against the real DTB (hisilicon,HiIPCV230 @ 0xfff22000) and the
 * mainline hi3670-mailbox.c definition - identical layout, only the compatible
 * string differs.
 */
#define MBOX_CH_STRIDE			0x40
#define MBOX_CH_REG(base, ch, off)	((base) + (ch) * MBOX_CH_STRIDE + (off))

#define MBOX_SRC_REG			0x00
#define MBOX_DST_REG			0x04
#define MBOX_DCLR_REG			0x08
#define MBOX_DSTAT_REG			0x0c
#define MBOX_MODE_REG			0x10
#define MBOX_IMASK_REG			0x14
#define MBOX_ICLR_REG			0x18
#define MBOX_SEND_REG			0x1c
#define MBOX_DATA_REG			0x20

#define MBOX_IPC_LOCK_REG		0xa00
#define MBOX_IPC_UNLOCK			0x1acce551

#define MBOX_AUTOMATIC_ACK		1
#define MBOX_STATE_IDLE			BIT(4)
#define MBOX_STATE_READY		BIT(5)
#define MBOX_STATE_ACK			BIT(7)
#define MBOX_MSG_WORDS			8	/* 8 x u32 = 32 bytes / frame */

/*
 * Sensorhub mailbox channels.  The vendor routes the sensorhub over AO rproc
 * ids 200 (AP -> IOM3) and 201 (IOM3 -> AP); those map to physical channels
 * (200 % 100) + AO_INDEX_BASE(200) = 0 and 1.  The real DTB ao-mailbox-0
 * (tx, src_bit=1) gives the AP source bit = 1 and the IOM3 destination bit
 * = 0; ao-mailbox-1 (rx, des_bit=1) is the AP's receive channel, whose irq
 * is cleared with the ACPU bit (GIC_2 = 1) - see MBOX_RX_ICLR_BIT.
 */
#define IOM3_TX_CH			0	/* AP -> IOM3 (ao-mailbox-0) */
#define IOM3_RX_CH			1	/* IOM3 -> AP (ao-mailbox-1) */
#define MBOX_SRC_AP			1	/* ACPU (A53) bit, ao-mailbox-0 src_bit */
#define MBOX_DST_IOM3			0	/* sensorhub/IOM7 bit, ao-mailbox-0 des_bit */
#define MBOX_RX_ICLR_BIT			1	/* ACPU bit, ao-mailbox-1 des_bit */

/*
 * Packet protocol (vendor protocol.h).  8-byte header + payload, <= 32 bytes
 * per mailbox frame.
 */
#define PKT_TAG_ACCEL			1
#define PKT_TAG_GYRO			2
#define PKT_TAG_MAG			3
#define PKT_TAG_ALS			4
#define PKT_TAG_PS			5
#define PKT_TAG_SYS			0x89	/* TAG_SYS (TAG_MODEM=0x80 +9) */

#define PKT_CMD_OPEN_REQ		0x01
#define PKT_CMD_CLOSE_REQ		0x03
#define PKT_CMD_INTERVAL_REQ		0x05
#define PKT_CMD_CONFIG_REQ		0x07
#define PKT_CMD_STATUSCHANGE_REQ	0x2b

#define PKT_RESP_NONE			0	/* NO_RESP */
#define PKT_RESP_REQ			1	/* RESP     */

#define PKT_TAG_END			0xff	/* chunk-continuation tag */

/* interval_param.mode (obj_report_mode_t) */
#define PKT_MODE_AUTO			0	/* AUTO_MODE */
#define PKT_MODE_FIFO			1	/* FIFO_MODE */
#define PKT_MODE_REALTIME		3	/* REALTIME_MODE */

/* pkt_common_data.data_flag bits */
#define PKT_DATA_FLAG_VALID_TIMESTAMP	BIT(0)

/* system status (sys_status_t): ST_POWERON=1, ST_MINSYSREADY=2, ST_MCUREADY=4 */
#define ST_MINSYSREADY			2
#define ST_MCUREADY			4

/* boot doorbell value the MCU waits for (STARTUP_IOM3_CMD) */
#define IOM3_STARTUP_CMD			0x00070001

#define PKT_HEADER_SIZE			8
#define MAX_FRAME_BYTES			32

/*
 * Largest packet we send (vendor MAX_PKT_LENGTH) and the largest we accept
 * back (MAX_PKT_LENGTH_AP).  RX reports are reassembled into an RX-sized
 * buffer before decoding.
 */
#define MAX_PKT_LENGTH			128
#define MAX_PKT_LENGTH_AP		2560

#pragma pack(push, 1)
struct pkt_header {
	u8	tag;
	u8	cmd;
	u8	resp;
	u8	partial_order;
	u16	tranid;
	u16	length;
};

struct pkt_sys_status {
	struct pkt_header hd;
	u16	status;
	u16	version;
};

/*
 * Per-sensor report interval (vendor interval_param_t).  Sent as the payload
 * of CMD_CMN_INTERVAL_REQ; length = sizeof(struct pkt_interval).
 */
struct pkt_interval {
	u32	period;		/* ms between reports			*/
	u32	batch_count;	/* samples per report (1 = streaming)	*/
	u8	mode;		/* 0 = AUTO_MODE, 1 = FIFO, 3 = REALTIME	*/
	u8	reserved[3];
};

/*
 * Chunk continuation header (vendor pkt_part_header_t).  When a packet does
 * not fit in one 32-byte frame, the first 32 bytes go as a normal pkt_header
 * and every following 30 bytes are prefixed with this 2-byte header whose tag
 * is TAG_END (0xff) and whose partial_order increments.  See iom3_mbox_send().
 */
struct pkt_part_header {
	u8	tag;		/* PKT_TAG_END */
	u8	partial_order;
};

/*
 * Sensor data report (vendor pkt_common_data_t + sensor_data_xyz).  The MCU
 * sends header + data_flag/cnt/len_element/sample_rate/timestamp, then cnt
 * samples of {x, y, z, accracy}.  A single 40-byte sample exceeds one frame,
 * so reports are chunked (Stage 2) and reassembled before decoding.
 */
struct pkt_common_data {
	struct pkt_header hd;
	u16	data_flag;
	u16	cnt;
	u16	len_element;
	u16	sample_rate;
	u64	timestamp;
};

struct sensor_data_xyz {
	s32	x;
	s32	y;
	s32	z;
	u32	accracy;
};
#pragma pack(pop)

/*
 * The sensors this client enables and exposes.  Each becomes its own
 * /dev/input/eventN device so generic tools (evtest/getevent) and any Android
 * sensor HAL see them independently.  accel/gyro/mag map to ABS_X/Y/Z; als is
 * a single-channel ambient-light value reported as ABS_MISC; ps is reported as
 * ABS_DISTANCE (0 = near, 1 = far).  tags are the vendor protocol.h values
 * (TAG_ACCEL=1 .. TAG_PS=5).
 */
enum iom3_sensor_id {
	IOM3_SENSOR_ACCEL = 0,
	IOM3_SENSOR_GYRO,
	IOM3_SENSOR_MAG,
	IOM3_SENSOR_ALS,
	IOM3_SENSOR_PS,
	IOM3_SENSOR_NUM
};

struct iom3_sensor {
	const char	*name;
	u8		tag;
	u32		period_ms;
	s32		abs_min;	/* ABS lower bound */
	s32		abs_max;	/* ABS upper bound */
	bool		axes;		/* true = X/Y/Z, false = single value */
	u16		abs_code;	/* single-value sensors only */
	bool		binary;		/* report 0/1 rather than raw value */
};

static const struct iom3_sensor iom3_sensors[IOM3_SENSOR_NUM] = {
	{ "iom3_accel", PKT_TAG_ACCEL, 100, -16000, 16000, true, 0, false },
	{ "iom3_gyro",  PKT_TAG_GYRO,  100, -16000, 16000, true, 0, false },
	{ "iom3_mag",   PKT_TAG_MAG,   100, -16000, 16000, true, 0, false },
	{ "iom3_als",   PKT_TAG_ALS,  1000,      0, 65535, false,
	  ABS_MISC, false },
	{ "iom3_prox",  PKT_TAG_PS,    250,      0,     1, false,
	  ABS_DISTANCE, true },
};

struct iom3_ts {
	struct platform_device *pdev;
	void __iomem *mbox_base;
	int tx_irq;
	int rx_irq;

	struct mutex lock;		/* serialises doorbell + cmd tx */
	struct completion ready;
	bool mcu_ready;

	/* boot handshake state */
	int last_status;

	struct input_dev *input[IOM3_SENSOR_NUM];	/* per-sensor devices */

	/*
	 * RX reassembly (vendor normalpack / link_package).  The RX ISR is
	 * single-threaded and the linker is only touched there, so a plain
	 * struct is enough (no extra lock).
	 */
	u8	rx_link[MAX_PKT_LENGTH_AP];	/* reassembly can exceed one TX pkt */
	int	rx_offset;
	int	rx_total;
	int	rx_partial;
};

static atomic_t iom3_tranid = ATOMIC_INIT(0);

/* ---- low-level mailbox helpers ------------------------------------------- */

static int iom3_mbox_unlock(struct iom3_ts *ts)
{
	unsigned int val, retry = 3;

	do {
		writel(MBOX_IPC_UNLOCK, ts->mbox_base + MBOX_IPC_LOCK_REG);
		val = readl(ts->mbox_base + MBOX_IPC_LOCK_REG);
		if (!val)
			break;
		udelay(10);
	} while (retry--);

	return val ? -ETIMEDOUT : 0;
}

static int iom3_mbox_acquire(struct iom3_ts *ts, int ch)
{
	void __iomem *base = MBOX_CH_REG(ts->mbox_base, ch, 0);
	unsigned int val, retry;

	for (retry = 10; retry; retry--) {
		if (readl(base + MBOX_MODE_REG) & MBOX_STATE_IDLE) {
			writel(BIT(MBOX_SRC_AP), base + MBOX_SRC_REG);
			val = readl(base + MBOX_SRC_REG);
			if (val & BIT(MBOX_SRC_AP))
				break;
		}
	}
	return retry ? 0 : -ETIMEDOUT;
}

static int iom3_mbox_wait_idle(struct iom3_ts *ts, int ch)
{
	void __iomem *base = MBOX_CH_REG(ts->mbox_base, ch, 0);
	unsigned long val;
	int ret;

	if (readl(base + MBOX_MODE_REG) & MBOX_STATE_READY)
		return 0;

	ret = readx_poll_timeout(readl, base + MBOX_MODE_REG,
				 val, (val & MBOX_STATE_ACK), 1000, 300000);
	if (ret) {
		dev_err(&ts->pdev->dev, "channel %d: timeout waiting ack\n", ch);
		return ret;
	}

	/* clear ack state, mailbox returns to ready */
	writel(BIT(MBOX_SRC_AP), base + MBOX_ICLR_REG);
	return 0;
}

/*
 * Send one frame (<= 32 bytes) to the IOM3.  Sequence ported from the mainline
 * hi3660_mbox_send_data() + vendor hisi_mdev_hw_send(): unlock, acquire, wait
 * idle, program destination interrupt + auto-ack, write up to 8 words, trigger.
 */
static int iom3_mbox_send(struct iom3_ts *ts, const void *buf, u32 len)
{
	void __iomem *base = MBOX_CH_REG(ts->mbox_base, IOM3_TX_CH, 0);
	const u32 *words = buf;
	unsigned int i, nwords;
	int ret;

	if (len == 0 || len > MAX_FRAME_BYTES || (len % 4))
		return -EINVAL;
	nwords = len / 4;

	ret = iom3_mbox_unlock(ts);
	if (ret)
		return ret;
	ret = iom3_mbox_acquire(ts, IOM3_TX_CH);
	if (ret)
		return ret;
	ret = iom3_mbox_wait_idle(ts, IOM3_TX_CH);
	if (ret)
		return ret;

	writel_relaxed((u32)~BIT(MBOX_DST_IOM3), base + MBOX_IMASK_REG);
	writel_relaxed(BIT(MBOX_DST_IOM3), base + MBOX_DST_REG);
	writel_relaxed(MBOX_AUTOMATIC_ACK, base + MBOX_MODE_REG);

	for (i = 0; i < nwords; i++)
		writel_relaxed(words[i], base + MBOX_DATA_REG + i * 4);

	writel(BIT(MBOX_SRC_AP), base + MBOX_SEND_REG);
	return 0;
}

/* Read one frame (<= 32 bytes) from the IOM3 and clear the interrupt/ack. */
static void iom3_mbox_read(struct iom3_ts *ts, void *buf, u32 len)
{
	void __iomem *base = MBOX_CH_REG(ts->mbox_base, IOM3_RX_CH, 0);
	u32 *words = buf;
	unsigned int i, nwords = len / 4;

	for (i = 0; i < nwords; i++)
		words[i] = readl(base + MBOX_DATA_REG + i * 4);

	/*
	 * acknowledge: clear the ACPU (GIC_2) receive bit that fired the IRQ.
	 * The vendor clears BIT(GIC_2) for an "ao" rx channel (hisi_mdev_clr_
	 * irq_and_ack), i.e. the destination/ACPU bit, NOT the IOM3 source bit.
	 */
	writel(BIT(MBOX_RX_ICLR_BIT), base + MBOX_ICLR_REG);
}

/* ---- packet helpers ------------------------------------------------------ */

static void iom3_pkt_hdr_fill(struct pkt_header *hd, u8 tag, u8 cmd,
			      u16 length, bool want_resp)
{
	hd->tag = tag;
	hd->cmd = cmd;
	hd->resp = want_resp ? PKT_RESP_REQ : PKT_RESP_NONE;
	hd->partial_order = 0;
	hd->tranid = (u16)atomic_inc_return(&iom3_tranid);
	hd->length = length;
}

static int iom3_send_pkt(struct iom3_ts *ts, const void *pkt, u32 len)
{
	int ret;

	mutex_lock(&ts->lock);
	ret = iom3_mbox_send(ts, pkt, len);
	mutex_unlock(&ts->lock);
	return ret;
}

/*
 * Reassemble one received frame into a complete packet, ported from the vendor
 * normalpack() / link_package.  Returns a pointer to a complete pkt_header
 * inside ts->rx_link (valid until the next call), or NULL while more frames of
 * a chunked packet are still expected.  Runs only in the RX ISR context.
 */
static struct pkt_header *iom3_rx_reassemble(struct iom3_ts *ts,
					     const void *frame, u32 len)
{
	const struct pkt_header *head = frame;
	const struct pkt_part_header *part = frame;

	if (len < PKT_HEADER_SIZE)
		return NULL;

	if (head->tag >= PKT_TAG_ACCEL && head->tag < PKT_TAG_END) {
		/* a real packet head: does the whole thing fit in this frame? */
		ts->rx_total = head->length + PKT_HEADER_SIZE;
		if (ts->rx_total <= len)
			return (struct pkt_header *)frame;	/* full packet */

		/* otherwise start a chunked reassembly */
		if (ts->rx_total > MAX_PKT_LENGTH_AP)
			goto error;
		ts->rx_partial = head->partial_order;
		memcpy(ts->rx_link, frame, len);
		ts->rx_offset = len;
		return NULL;
	} else if (head->tag == PKT_TAG_END) {
		/* chunk continuation: append payload after the 2-byte part header */
		int plen = len - sizeof(*part);

		if (part->partial_order != (u8)(ts->rx_partial + 1))
			goto error;
		if (ts->rx_offset + plen > MAX_PKT_LENGTH_AP)
			goto error;
		ts->rx_partial++;
		memcpy(ts->rx_link + ts->rx_offset,
		       (const u8 *)frame + sizeof(*part), plen);
		ts->rx_offset += plen;
		if (ts->rx_offset >= ts->rx_total)
			return (struct pkt_header *)ts->rx_link;	/* complete */
		return NULL;
	}

	return NULL;

error:
	dev_err_ratelimited(&ts->pdev->dev,
			    "rx reassembly error (tag 0x%02x len %u)\n",
			    head->tag, len);
	ts->rx_partial = -1;
	ts->rx_total = 0;
	ts->rx_offset = 0;
	return NULL;
}

/* ---- boot / handshake ---------------------------------------------------- */

static void iom3_handle_sys(struct iom3_ts *ts, const struct pkt_header *hd)
{
	const struct pkt_sys_status *st = (const struct pkt_sys_status *)hd;

	if (hd->cmd != PKT_CMD_STATUSCHANGE_REQ)
		return;

	ts->last_status = st->status;
	dev_info(&ts->pdev->dev, "IOM3 sys status change: %u\n", st->status);

	if (st->status == ST_MINSYSREADY || st->status == ST_MCUREADY)
		ts->mcu_ready = (st->status == ST_MCUREADY);
}

/*
 * Decode a completed sensor report (pkt_common_data_t + sensor_data_xyz[]) and
 * report it to the matching input device.  accel/gyro/mag map to ABS_X/Y/Z;
 * als is a single value on ABS_MISC and ps on ABS_DISTANCE.  Only the first
 * sample of a batch is surfaced per input_sync (batched samples would need
 * timestamping we don't track yet).
 */
static void iom3_handle_report(struct iom3_ts *ts, const struct pkt_header *hd)
{
	const struct pkt_common_data *cd = (const struct pkt_common_data *)hd;
	const struct sensor_data_xyz *s;
	struct input_dev *input;
	const struct iom3_sensor *desc = NULL;
	unsigned int stride;
	int val;
	int i;

	for (i = 0; i < IOM3_SENSOR_NUM; i++) {
		if (iom3_sensors[i].tag == hd->tag) {
			desc = &iom3_sensors[i];
			break;
		}
	}
	if (!desc)
		return;

	if (hd->length < sizeof(*cd) - PKT_HEADER_SIZE || cd->cnt < 1)
		return;

	/*
	 * len_element is the byte stride of one sample (vendor steps xyz[] as a
	 * raw byte offset, i * len_element, not as a struct array).  Fall back
	 * to sizeof(sensor_data_xyz) if the MCU left it zero.
	 */
	stride = cd->len_element ? cd->len_element : sizeof(struct sensor_data_xyz);
	if (stride < sizeof(struct sensor_data_xyz))
		return;
	if (sizeof(*cd) - PKT_HEADER_SIZE + stride > hd->length)
		return;
	s = (const struct sensor_data_xyz *)(cd + 1);

	input = ts->input[i];
	if (!input)
		return;

	if (desc->axes) {
		input_report_abs(input, ABS_X, s->x);
		input_report_abs(input, ABS_Y, s->y);
		input_report_abs(input, ABS_Z, s->z);
	} else {
		val = desc->binary ? !!s->x : s->x;
		input_report_abs(input, desc->abs_code, val);
	}
	input_sync(input);
}

/*
 * RX interrupt: the IOM3 has written a frame into channel 1.  Read it,
 * reassemble (chunked reports span several frames), then dispatch.  TAG_SYS
 * drives the boot handshake; sensor tags are logged here and mapped to input
 * in Stage 3.
 */
static irqreturn_t iom3_rx_isr(int irq, void *dev_id)
{
	struct iom3_ts *ts = dev_id;
	u8 frame[MAX_FRAME_BYTES];
	struct pkt_header *hd;

	iom3_mbox_read(ts, frame, MAX_FRAME_BYTES);

	hd = iom3_rx_reassemble(ts, frame, MAX_FRAME_BYTES);
	if (!hd)
		return IRQ_HANDLED;

	if (hd->tag == PKT_TAG_SYS)
		iom3_handle_sys(ts, hd);
	else if (hd->tag >= PKT_TAG_ACCEL && hd->tag < PKT_TAG_END)
		iom3_handle_report(ts, hd);

	/* handshake completion is polled via reinit_completion in probe */
	if (ts->last_status == ST_MCUREADY)
		complete(&ts->ready);

	return IRQ_HANDLED;
}

static int iom3_boot(struct iom3_ts *ts)
{
	u32 doorbell = cpu_to_le32(IOM3_STARTUP_CMD);
	int ret;

	/* Arm the handshake completion before ringing the doorbell. */
	reinit_completion(&ts->ready);
	ts->last_status = 0;
	ts->mcu_ready = false;

	ret = iom3_send_pkt(ts, &doorbell, sizeof(doorbell));
	if (ret) {
		dev_err(&ts->pdev->dev, "failed to send boot doorbell: %d\n", ret);
		return ret;
	}

	/* The MCU answers ST_MINSYSREADY then ST_MCUREADY over the RX channel. */
	ret = wait_for_completion_timeout(&ts->ready, msecs_to_jiffies(3000));
	if (!ret) {
		dev_err(&ts->pdev->dev, "IOM3 handshake timeout (last status %d)\n",
			ts->last_status);
		return -ETIMEDOUT;
	}

	dev_info(&ts->pdev->dev, "IOM3 MCU ready (status %d)\n", ts->last_status);
	return 0;
}

/* ---- sensor control ------------------------------------------------------ */

/*
 * Enable one sensor and set its report interval.  This is the vendor's
 * inputhub_sensor_enable() + inputhub_sensor_setdelay(): a bare
 * CMD_CMN_OPEN_REQ (length 0) followed by a CMD_CMN_INTERVAL_REQ carrying
 * interval_param_t.  Both fit in a single 32-byte frame.
 */
static int iom3_sensor_enable(struct iom3_ts *ts, u8 tag, u32 period_ms)
{
	struct pkt_header hd;
	struct {
		struct pkt_header hd;
		struct pkt_interval param;
	} __packed ip;
	int ret;

	iom3_pkt_hdr_fill(&hd, tag, PKT_CMD_OPEN_REQ, 0, false);
	ret = iom3_send_pkt(ts, &hd, sizeof(hd));
	if (ret)
		return ret;

	iom3_pkt_hdr_fill(&ip.hd, tag, PKT_CMD_INTERVAL_REQ,
			  sizeof(ip.param), false);
	ip.param.period = period_ms;
	ip.param.batch_count = 1;
	ip.param.mode = PKT_MODE_AUTO;
	return iom3_send_pkt(ts, &ip, sizeof(ip));
}

/* ---- input devices ------------------------------------------------------- */

/*
 * Allocate and register one input device per sensor.  Each becomes its own
 * /dev/input/eventN.  Registration failures are non-fatal: a sensor with no
 * input device is simply skipped in iom3_handle_report().
 */
static int iom3_input_setup(struct iom3_ts *ts)
{
	struct device *dev = &ts->pdev->dev;
	int i, ret;

	for (i = 0; i < IOM3_SENSOR_NUM; i++) {
		const struct iom3_sensor *s = &iom3_sensors[i];
		struct input_dev *input;

		input = devm_input_allocate_device(dev);
		if (!input) {
			dev_err(dev, "alloc input for %s failed\n", s->name);
			return -ENOMEM;
		}

		input->name = s->name;
		input->id.bustype = BUS_HOST;

		if (s->axes) {
			input_set_abs_params(input, ABS_X, s->abs_min, s->abs_max,
					     0, 0);
			input_set_abs_params(input, ABS_Y, s->abs_min, s->abs_max,
					     0, 0);
			input_set_abs_params(input, ABS_Z, s->abs_min, s->abs_max,
					     0, 0);
		} else {
			input_set_abs_params(input, s->abs_code, s->abs_min,
					     s->abs_max, 0, 0);
		}

		ret = input_register_device(input);
		if (ret) {
			dev_err(dev, "register input %s failed: %d\n", s->name,
				ret);
			return ret;
		}

		ts->input[i] = input;
	}

	return 0;
}

static int iom3_probe(struct platform_device *pdev)
{
	struct iom3_ts *ts;
	struct resource *res;
	int ret;
	int i;

	ts = devm_kzalloc(&pdev->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->pdev = pdev;
	platform_set_drvdata(pdev, ts);
	mutex_init(&ts->lock);
	init_completion(&ts->ready);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	ts->mbox_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!ts->mbox_base)
		return -ENOMEM;

	ts->rx_irq = platform_get_irq(pdev, 0);
	if (ts->rx_irq <= 0)
		return ts->rx_irq ? : -ENODEV;

	ret = iom3_input_setup(ts);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(&pdev->dev, ts->rx_irq, NULL,
					iom3_rx_isr, IRQF_ONESHOT,
					DRIVER_NAME, ts);
	if (ret) {
		dev_err(&pdev->dev, "request rx irq failed: %d\n", ret);
		return ret;
	}

	ret = iom3_boot(ts);
	if (ret)
		return ret;

	/*
	 * Enable the motion / environment sensors.  Periods mirror the vendor
	 * defaults (accel/gyro ~50-200 ms, mag ~100 ms, als ~1 s, ps ~250 ms).
	 * Failures are non-fatal: the transport is up and a missing sensor just
	 * logs nothing.
	 */
	for (i = 0; i < IOM3_SENSOR_NUM; i++)
		iom3_sensor_enable(ts, iom3_sensors[i].tag,
				   iom3_sensors[i].period_ms);

	dev_info(&pdev->dev, "IOM3 sensorhub transport up (ch %d tx / ch %d rx)\n",
		 IOM3_TX_CH, IOM3_RX_CH);
	return 0;
}

static void iom3_remove(struct platform_device *pdev)
{
	struct iom3_ts *ts = platform_get_drvdata(pdev);

	/* Best-effort: tell the MCU we're going away. */
	if (ts->mbox_base) {
		struct pkt_header close;

		iom3_pkt_hdr_fill(&close, PKT_TAG_SYS, PKT_CMD_CLOSE_REQ, 0, false);
		iom3_send_pkt(ts, &close, sizeof(close));
	}
}

static const struct of_device_id iom3_of_match[] = {
	{ .compatible = "hisilicon,iom3-sensorhub" },
	{ }
};
MODULE_DEVICE_TABLE(of, iom3_of_match);

static struct platform_driver iom3_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= iom3_of_match,
	},
	.probe	= iom3_probe,
	.remove = iom3_remove,
};
module_platform_driver(iom3_driver);

MODULE_AUTHOR("Ported for Kirin 970 COR-AL00");
MODULE_DESCRIPTION("IOM3 sensorhub IPC client");
MODULE_LICENSE("GPL");
