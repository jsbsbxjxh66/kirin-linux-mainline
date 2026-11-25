// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 (Kirin 970) mailbox controller.
 *
 * The HiIPCV230 block at 0xe896b000 has 32 channels.  Each channel is 0x40
 * bytes wide and carries one 8-word message.  The channel descriptor in DT
 * supplies the AP source bit and the remote destination bit, matching the
 * per-channel "src_bit"/"des_bit" values used by the firmware.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mailbox_controller.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define HI3670_MBOX_CHAN_MAX		32
#define HI3670_MBOX_CHAN_STRIDE		0x40
#define HI3670_MBOX_MSG_LEN		8
#define HI3670_MBOX_ACK_TIMEOUT_US	300000
#define HI3670_MBOX_FAST_ACK_US		1000

#define HI3670_MBOX_SRC_REG		0x00
#define HI3670_MBOX_DST_REG		0x04
#define HI3670_MBOX_DCLR_REG		0x08
#define HI3670_MBOX_DSTAT_REG		0x0c
#define HI3670_MBOX_MODE_REG		0x10
#define HI3670_MBOX_IMASK_REG		0x14
#define HI3670_MBOX_ICLR_REG		0x18
#define HI3670_MBOX_SEND_REG		0x1c
#define HI3670_MBOX_DATA_REG		0x20

#define HI3670_MBOX_IPC_LOCK_REG	0xa00
#define HI3670_MBOX_IPC_UNLOCK		0x1acce551

#define HI3670_MBOX_AUTOMATIC_ACK	1

#define HI3670_MBOX_STATE_IDLE		BIT(4)
#define HI3670_MBOX_STATE_READY		BIT(5)
#define HI3670_MBOX_STATE_ACK		BIT(7)

struct hi3670_mbox;

struct hi3670_mbox_chan {
	u32 src_bit;
	u32 dst_bit;
	unsigned int ch;
	int irq;
	bool automatic_ack;
	bool sync;
	bool irq_requested;
	bool configured;
	bool ack_pending;
	struct hi3670_mbox *mbox;
	struct mbox_chan *chan;
	struct tasklet_struct ack_tasklet;
	struct work_struct ack_work;
	u32 ack_buf[HI3670_MBOX_MSG_LEN];
};

struct hi3670_mbox {
	struct device *dev;
	void __iomem *base;
	u32 unlock_key;
	struct mbox_chan chan[HI3670_MBOX_CHAN_MAX];
	struct hi3670_mbox_chan mchan[HI3670_MBOX_CHAN_MAX];
	struct mbox_controller controller;
};

static inline void __iomem *hi3670_mbox_base(struct hi3670_mbox *mbox,
					     unsigned int ch)
{
	return mbox->base + ch * HI3670_MBOX_CHAN_STRIDE;
}

static int hi3670_mbox_unlock(struct hi3670_mbox *mbox)
{
	unsigned int value, retry = 3;

	do {
		writel(mbox->unlock_key, mbox->base + HI3670_MBOX_IPC_LOCK_REG);

		value = readl(mbox->base + HI3670_MBOX_IPC_LOCK_REG);
		if (!value)
			return 0;

		udelay(10);
	} while (--retry);

	dev_err(mbox->dev, "failed to unlock mailbox\n");

	return -ETIMEDOUT;
}

static int hi3670_mbox_acquire_channel(struct mbox_chan *chan)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];
	void __iomem *base = hi3670_mbox_base(mbox, ch);
	unsigned int value, retry;

	for (retry = 10; retry; retry--) {
		if (readl(base + HI3670_MBOX_MODE_REG) & HI3670_MBOX_STATE_IDLE) {
			writel(BIT(mchan->src_bit), base + HI3670_MBOX_SRC_REG);

			value = readl(base + HI3670_MBOX_SRC_REG);
			if (value & BIT(mchan->src_bit))
				return 0;
		}
		udelay(10);
	}

	dev_err(mbox->dev, "channel %u acquire timeout\n", ch);

	return -ETIMEDOUT;
}

/*
 * The mailbox core calls send_data() with IRQs disabled (msg_submit() holds
 * chan->lock in a spinlock_irqsave section).  Only poll for the ACK briefly
 * there; if the firmware has not replied yet, hand the wait over to a
 * workqueue so a lost ACK cannot stall the CPU with interrupts off for the
 * full 300 ms tolerance the vendor stack allows.
 */
static void hi3670_mbox_reap_ack(struct hi3670_mbox_chan *mchan)
{
	void __iomem *base = hi3670_mbox_base(mchan->mbox, mchan->ch);
	unsigned int i;

	for (i = 0; i < HI3670_MBOX_MSG_LEN; i++)
		mchan->ack_buf[i] = readl(base + HI3670_MBOX_DATA_REG + i * 4);

	for (i = 0; i < HI3670_MBOX_MSG_LEN; i++)
		writel_relaxed(0, base + HI3670_MBOX_DATA_REG + i * 4);

	writel(BIT(mchan->src_bit), base + HI3670_MBOX_ICLR_REG);
}

static void hi3670_mbox_ack_work(struct work_struct *work)
{
	struct hi3670_mbox_chan *mchan =
		container_of(work, struct hi3670_mbox_chan, ack_work);
	void __iomem *base = hi3670_mbox_base(mchan->mbox, mchan->ch);
	unsigned long value;
	int ret;

	if (!mchan->ack_pending)
		return;

	ret = readx_poll_timeout(readl, base + HI3670_MBOX_MODE_REG,
				 value, value & HI3670_MBOX_STATE_ACK,
				 1000, HI3670_MBOX_ACK_TIMEOUT_US);
	if (ret) {
		dev_err(mchan->mbox->dev,
			"channel %u sync ack timeout\n", mchan->ch);
		writel(BIT(mchan->src_bit), base + HI3670_MBOX_ICLR_REG);
		mchan->ack_pending = false;
		return;
	}

	mchan->ack_pending = false;
	hi3670_mbox_reap_ack(mchan);
	mbox_chan_received_data(mchan->chan, mchan->ack_buf);
}

static void hi3670_mbox_ack_tasklet(struct tasklet_struct *t)
{
	struct hi3670_mbox_chan *mchan =
		container_of(t, struct hi3670_mbox_chan, ack_tasklet);

	mbox_chan_received_data(mchan->chan, mchan->ack_buf);
}

static irqreturn_t hi3670_mbox_irq(int irq, void *data)
{
	struct mbox_chan *chan = data;
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];
	void __iomem *base = hi3670_mbox_base(mbox, ch);
	u32 buf[HI3670_MBOX_MSG_LEN];
	unsigned int i;

	for (i = 0; i < HI3670_MBOX_MSG_LEN; i++)
		buf[i] = readl(base + HI3670_MBOX_DATA_REG + i * 4);

	for (i = 0; i < HI3670_MBOX_MSG_LEN; i++)
		writel_relaxed(0, base + HI3670_MBOX_DATA_REG + i * 4);

	writel(BIT(mchan->dst_bit), base + HI3670_MBOX_ICLR_REG);

	if (!mchan->automatic_ack)
		writel(BIT(mchan->src_bit), base + HI3670_MBOX_SEND_REG);

	mbox_chan_received_data(chan, buf);

	return IRQ_HANDLED;
}

static int hi3670_mbox_startup(struct mbox_chan *chan)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];
	int ret;

	ret = hi3670_mbox_unlock(mbox);
	if (ret)
		return ret;

	ret = hi3670_mbox_acquire_channel(chan);
	if (ret || !mchan->irq)
		return ret;

	ret = request_threaded_irq(mchan->irq, NULL, hi3670_mbox_irq,
				   IRQF_ONESHOT, dev_name(mbox->dev), chan);
	if (ret)
		return ret;

	mchan->irq_requested = true;

	return 0;
}

static void hi3670_mbox_shutdown(struct mbox_chan *chan)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];

	cancel_work_sync(&mchan->ack_work);
	tasklet_kill(&mchan->ack_tasklet);

	if (mchan->irq_requested) {
		free_irq(mchan->irq, chan);
		mchan->irq_requested = false;
	}
}

static int hi3670_mbox_check_state(struct mbox_chan *chan)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];
	void __iomem *base = hi3670_mbox_base(mbox, ch);
	unsigned long value;
	int ret;

	value = readl(base + HI3670_MBOX_MODE_REG);
	if (value & HI3670_MBOX_STATE_READY)
		return 0;

	if (value & HI3670_MBOX_STATE_IDLE)
		return hi3670_mbox_acquire_channel(chan);

	ret = readx_poll_timeout_atomic(readl, base + HI3670_MBOX_MODE_REG,
					value, value & HI3670_MBOX_STATE_ACK,
					10, HI3670_MBOX_FAST_ACK_US);
	if (ret) {
		dev_err(mbox->dev, "channel %u ack timeout\n", ch);
		return ret;
	}

	writel(BIT(mchan->src_bit), base + HI3670_MBOX_ICLR_REG);

	return 0;
}

static int hi3670_mbox_send_data(struct mbox_chan *chan, void *msg)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	unsigned int ch = (unsigned long)chan->con_priv;
	struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];
	void __iomem *base = hi3670_mbox_base(mbox, ch);
	u32 *buf = msg;
	unsigned long value;
	unsigned int i;
	int ret;

	ret = hi3670_mbox_check_state(chan);
	if (ret)
		return ret;

	if (mchan->sync)
		writel_relaxed(~(BIT(mchan->src_bit) | BIT(mchan->dst_bit)),
			       base + HI3670_MBOX_IMASK_REG);
	else
		writel_relaxed(~BIT(mchan->dst_bit),
			       base + HI3670_MBOX_IMASK_REG);

	writel_relaxed(BIT(mchan->dst_bit), base + HI3670_MBOX_DST_REG);
	writel_relaxed(mchan->automatic_ack ? HI3670_MBOX_AUTOMATIC_ACK : 0,
		       base + HI3670_MBOX_MODE_REG);

	for (i = 0; i < HI3670_MBOX_MSG_LEN; i++)
		writel_relaxed(buf[i], base + HI3670_MBOX_DATA_REG + i * 4);

	writel(BIT(mchan->src_bit), base + HI3670_MBOX_SEND_REG);

	/*
	 * Do NOT call mbox_chan_txdone() here: msg_submit() invokes this
	 * callback with chan->lock held, and the TX completion path takes the
	 * same lock (spinlock recursion).  The core polls last_tx_done()
	 * instead - the message is already handed to the hardware by the time
	 * send_data() returns, so TX is complete by definition.
	 */
	if (!mchan->sync)
		return 0;

	mchan->ack_pending = true;

	ret = readx_poll_timeout_atomic(readl, base + HI3670_MBOX_MODE_REG,
					value, value & HI3670_MBOX_STATE_ACK,
					10, HI3670_MBOX_FAST_ACK_US);
	if (ret) {
		/*
		 * The message is already queued in the mailbox core.  Do not
		 * return an error here, otherwise the core would retry it
		 * behind the client's back; let the client's own timeout
		 * report the failure instead.  The ACK, if it ever arrives,
		 * is reaped from process context.
		 */
		schedule_work(&mchan->ack_work);
		return 0;
	}

	mchan->ack_pending = false;
	hi3670_mbox_reap_ack(mchan);
	tasklet_schedule(&mchan->ack_tasklet);

	return 0;
}

static bool hi3670_mbox_last_tx_done(struct mbox_chan *chan)
{
	/*
	 * send_data() writes the message into the channel registers and
	 * raises SEND before returning, so the transfer has been handed to
	 * the hardware and the core is free to release the message.
	 */
	return true;
}

static const struct mbox_chan_ops hi3670_mbox_ops = {
	.startup	= hi3670_mbox_startup,
	.shutdown	= hi3670_mbox_shutdown,
	.send_data	= hi3670_mbox_send_data,
	.last_tx_done	= hi3670_mbox_last_tx_done,
};

static struct mbox_chan *hi3670_mbox_xlate(struct mbox_controller *controller,
					   const struct of_phandle_args *spec)
{
	struct hi3670_mbox *mbox = dev_get_drvdata(controller->dev);
	unsigned int ch = spec->args[0];

	if (ch >= HI3670_MBOX_CHAN_MAX || !mbox->mchan[ch].configured)
		return ERR_PTR(-EINVAL);

	return &mbox->chan[ch];
}

static int hi3670_mbox_parse_channels(struct hi3670_mbox *mbox)
{
	struct device_node *np = mbox->dev->of_node, *child;

	for_each_child_of_node(np, child) {
		struct hi3670_mbox_chan *mchan;
		u32 index, src_bit, dst_bit;

		if (of_property_read_u32(child, "hisilicon,index", &index))
			continue;

		if (index >= HI3670_MBOX_CHAN_MAX) {
			dev_warn(mbox->dev, "%pOF: invalid channel %u\n",
				 child, index);
			continue;
		}

		if (of_property_read_u32(child, "hisilicon,src-bit", &src_bit) ||
		    of_property_read_u32(child, "hisilicon,dst-bit", &dst_bit)) {
			dev_warn(mbox->dev, "%pOF: missing src/dst bit\n",
				 child);
			continue;
		}

		mchan = &mbox->mchan[index];
		mchan->src_bit = src_bit;
		mchan->dst_bit = dst_bit;
		mchan->automatic_ack =
			of_property_read_bool(child, "hisilicon,automatic-ack");
		mchan->sync = of_property_read_bool(child, "hisilicon,sync");
		mchan->irq = of_irq_get(child, 0);
		if (mchan->irq == -EPROBE_DEFER) {
			of_node_put(child);
			return -EPROBE_DEFER;
		}
		if (mchan->irq < 0)
			mchan->irq = 0;
		mchan->configured = true;
	}

	return 0;
}

static int hi3670_mbox_probe(struct platform_device *pdev)
{
	struct hi3670_mbox *mbox;
	unsigned int ch;
	int ret;

	mbox = devm_kzalloc(&pdev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->dev = &pdev->dev;
	mbox->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->base))
		return PTR_ERR(mbox->base);

	if (of_property_read_u32(pdev->dev.of_node, "hisilicon,unlock-key",
				 &mbox->unlock_key))
		mbox->unlock_key = HI3670_MBOX_IPC_UNLOCK;

	ret = hi3670_mbox_parse_channels(mbox);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);

	for (ch = 0; ch < HI3670_MBOX_CHAN_MAX; ch++) {
		mbox->chan[ch].con_priv = (void *)(unsigned long)ch;
		mbox->mchan[ch].ch = ch;
		mbox->mchan[ch].mbox = mbox;
		mbox->mchan[ch].chan = &mbox->chan[ch];
		tasklet_setup(&mbox->mchan[ch].ack_tasklet,
			      hi3670_mbox_ack_tasklet);
		INIT_WORK(&mbox->mchan[ch].ack_work, hi3670_mbox_ack_work);
	}

	mbox->controller.dev = &pdev->dev;
	mbox->controller.chans = mbox->chan;
	mbox->controller.num_chans = HI3670_MBOX_CHAN_MAX;
	mbox->controller.ops = &hi3670_mbox_ops;
	mbox->controller.of_xlate = hi3670_mbox_xlate;
	/*
	 * TX completes as soon as the message has been written to the channel
	 * registers, but that happens inside ->send_data() with chan->lock
	 * held, so it cannot be reported from there.  Use the core's
	 * polling completion instead: the message is handed to the hardware
	 * synchronously and the poll hrtimer ticks the core right after
	 * submission.  This keeps tx_block clients such as hkadc from
	 * blocking for the full timeout without any client intervention.
	 */
	mbox->controller.txdone_poll = true;
	mbox->controller.txpoll_period = 1;

	ret = devm_mbox_controller_register(&pdev->dev, &mbox->controller);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register mailbox\n");

	dev_info(&pdev->dev, "Hi3670 mailbox enabled\n");

	return 0;
}

static void hi3670_mbox_remove(struct platform_device *pdev)
{
	struct hi3670_mbox *mbox = platform_get_drvdata(pdev);
	unsigned int ch;

	for (ch = 0; ch < HI3670_MBOX_CHAN_MAX; ch++) {
		struct hi3670_mbox_chan *mchan = &mbox->mchan[ch];

		cancel_work_sync(&mchan->ack_work);
		tasklet_kill(&mchan->ack_tasklet);
		if (mchan->irq_requested) {
			free_irq(mchan->irq, &mbox->chan[ch]);
			mchan->irq_requested = false;
		}
	}
}

static const struct of_device_id hi3670_mbox_of_match[] = {
	{ .compatible = "hisilicon,hi3670-mbox" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3670_mbox_of_match);

static struct platform_driver hi3670_mbox_driver = {
	.probe = hi3670_mbox_probe,
	.remove = hi3670_mbox_remove,
	.driver = {
		.name = "hi3670-mbox",
		.of_match_table = hi3670_mbox_of_match,
	},
};
module_platform_driver(hi3670_mbox_driver);

MODULE_DESCRIPTION("HiSilicon Hi3670 mailbox controller");
MODULE_LICENSE("GPL");
