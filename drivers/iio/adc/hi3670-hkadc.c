// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 HKADC (housekeeping ADC) driver
 *
 * The HKADC block is owned by the LPM3 firmware.  The AP only asks LPM3 to
 * sample a channel over the HiIPCV230 mailbox and receives the result on the
 * matching receive channel:
 *
 *   AP -> LPM3: LPM3 mailbox 16, two 32-bit words {command, channel}
 *   LPM3 -> AP: LPM3 mailbox 0,  two 32-bit words {header, payload}
 *
 * Payload layout: bits 7:0 channel, bits 15:8 error, bits 31:16 sample.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hisi/hisi_adc.h>
#include <linux/iio/iio.h>
#include <linux/jiffies.h>
#include <linux/mailbox_client.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define HI3670_HKADC_NUM_CHANNELS	18
#define HI3670_HKADC_XOADC_CHANNEL	15

#define HI3670_HKADC_VREF_MV		1800
#define HI3670_HKADC_FULL_SCALE		4095
#define HI3670_HKADC_XO_FULL_SCALE	32767

#define HI3670_HKADC_TIMEOUT_MS		1500
#define HI3670_HKADC_TX_RETRIES		3

#define HI3670_HKADC_AP_ID		0x00
#define HI3670_HKADC_OBJ_ID		0x0b
#define HI3670_HKADC_CMD_TYPE0		0x02
#define HI3670_HKADC_CMD_ADC		0x0c
#define HI3670_HKADC_CMD_CURRENT	0x14

#define HI3670_HKADC_CMD(_type) \
	((HI3670_HKADC_AP_ID << 24) | (HI3670_HKADC_OBJ_ID << 16) | \
	 (HI3670_HKADC_CMD_TYPE0 << 8) | (_type))
#define HI3670_HKADC_RSP(_type) \
	((0x08 << 24) | (HI3670_HKADC_OBJ_ID << 16) | \
	 (HI3670_HKADC_CMD_TYPE0 << 8) | (_type))

#define HI3670_HKADC_MBOX_TX		"tx"
#define HI3670_HKADC_MBOX_RX		"rx"

struct hi3670_hkadc {
	struct mbox_client tx_client;
	struct mbox_client rx_client;
	struct mbox_chan *tx;
	struct mbox_chan *rx;
	/* Serializes request/response transactions on the mailbox pair. */
	struct mutex lock;
	struct completion done;
	u32 tx_msg[8];
	u32 channel;
	int value;
	int result;
};

static struct hi3670_hkadc *hi3670_hkadc_dev;
static DEFINE_MUTEX(hi3670_hkadc_dev_lock);

static int hi3670_hkadc_sample(struct hi3670_hkadc *adc, unsigned int channel,
			       u32 command)
{
	unsigned int retries;
	unsigned long timeout;
	int ret;

	lockdep_assert_held(&adc->lock);

	adc->channel = channel;
	adc->value = 0;
	adc->result = -ETIMEDOUT;
	reinit_completion(&adc->done);

	adc->tx_msg[0] = HI3670_HKADC_CMD(command);
	adc->tx_msg[1] = channel;

	for (retries = 0; retries < HI3670_HKADC_TX_RETRIES; retries++) {
		ret = mbox_send_message(adc->tx, adc->tx_msg);
		if (ret != -ENOMEM)
			break;
	}
	if (ret < 0)
		return ret;

	timeout = wait_for_completion_timeout(&adc->done,
					      msecs_to_jiffies(HI3670_HKADC_TIMEOUT_MS));
	if (!timeout)
		return -ETIMEDOUT;

	return adc->result;
}

static void hi3670_hkadc_rx_callback(struct mbox_client *cl, void *data)
{
	struct hi3670_hkadc *adc = container_of(cl, struct hi3670_hkadc,
						rx_client);
	u32 *msg = data;
	u32 header = msg[0];
	u32 payload = msg[1];
	u32 error, channel;

	if (header != HI3670_HKADC_RSP(HI3670_HKADC_CMD_ADC) &&
	    header != HI3670_HKADC_RSP(HI3670_HKADC_CMD_CURRENT))
		return;

	error = FIELD_GET(GENMASK(15, 8), payload);
	channel = FIELD_GET(GENMASK(7, 0), payload);
	if (!error && channel != adc->channel)
		return;

	if (error) {
		adc->result = -EIO;
	} else {
		adc->result = 0;
		adc->value = FIELD_GET(GENMASK(31, 16), payload);
	}

	complete(&adc->done);
}

/*
 * Transitional API for the vendor power, USB and LCD drivers that still call
 * hisi_adc_get_value()/hisi_adc_get_adc().  New code should use the IIO
 * channels directly; these wrappers keep the existing board drivers working
 * until they are converted.
 */
static int hi3670_hkadc_legacy_read(unsigned int channel, u32 command,
				    int *value)
{
	struct hi3670_hkadc *adc;
	int ret;

	if (channel >= HI3670_HKADC_NUM_CHANNELS)
		return -EINVAL;

	mutex_lock(&hi3670_hkadc_dev_lock);
	adc = hi3670_hkadc_dev;
	if (!adc) {
		ret = -ENODEV;
		goto out_unlock_dev;
	}

	mutex_lock(&adc->lock);
	ret = hi3670_hkadc_sample(adc, channel, command);
	if (!ret)
		*value = adc->value;
	mutex_unlock(&adc->lock);

out_unlock_dev:
	mutex_unlock(&hi3670_hkadc_dev_lock);
	return ret;
}

int adc_to_volt(int adc)
{
	if (adc < 0)
		return -1;

	return adc * HI3670_HKADC_VREF_MV / HI3670_HKADC_FULL_SCALE;
}
EXPORT_SYMBOL_GPL(adc_to_volt);

int hisi_adc_get_adc(int adc_channel)
{
	int value;
	int ret;

	if (adc_channel < 0)
		return -EINVAL;

	ret = hi3670_hkadc_legacy_read(adc_channel, HI3670_HKADC_CMD_ADC,
				       &value);
	return ret ? ret : value;
}
EXPORT_SYMBOL_GPL(hisi_adc_get_adc);

int hisi_adc_get_value(int adc_channel)
{
	int ret;

	ret = hisi_adc_get_adc(adc_channel);
	if (ret < 0)
		return ret;

	if (adc_channel == HI3670_HKADC_XOADC_CHANNEL)
		return ret * HI3670_HKADC_VREF_MV / HI3670_HKADC_XO_FULL_SCALE;

	return adc_to_volt(ret);
}
EXPORT_SYMBOL_GPL(hisi_adc_get_value);

int hisi_adc_get_current(int adc_channel)
{
	int value;
	int ret;

	if (adc_channel < 0)
		return -EINVAL;

	ret = hi3670_hkadc_legacy_read(adc_channel, HI3670_HKADC_CMD_CURRENT,
				       &value);
	return ret ? ret : value;
}
EXPORT_SYMBOL_GPL(hisi_adc_get_current);

static int hi3670_hkadc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct hi3670_hkadc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);
		ret = hi3670_hkadc_sample(adc, chan->channel, HI3670_HKADC_CMD_ADC);
		if (!ret)
			*val = adc->value;
		mutex_unlock(&adc->lock);
		return ret ? ret : IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = HI3670_HKADC_VREF_MV;
		*val2 = chan->channel == HI3670_HKADC_XOADC_CHANNEL ?
			HI3670_HKADC_XO_FULL_SCALE : HI3670_HKADC_FULL_SCALE;
		return IIO_VAL_FRACTIONAL;

	default:
		return -EINVAL;
	}
}

static const struct iio_info hi3670_hkadc_info = {
	.read_raw = hi3670_hkadc_read_raw,
};

#define HI3670_HKADC_CHANNEL(_idx) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
}

static const struct iio_chan_spec hi3670_hkadc_channels[] = {
	HI3670_HKADC_CHANNEL(0),
	HI3670_HKADC_CHANNEL(1),
	HI3670_HKADC_CHANNEL(2),
	HI3670_HKADC_CHANNEL(3),
	HI3670_HKADC_CHANNEL(4),
	HI3670_HKADC_CHANNEL(5),
	HI3670_HKADC_CHANNEL(6),
	HI3670_HKADC_CHANNEL(7),
	HI3670_HKADC_CHANNEL(8),
	HI3670_HKADC_CHANNEL(9),
	HI3670_HKADC_CHANNEL(10),
	HI3670_HKADC_CHANNEL(11),
	HI3670_HKADC_CHANNEL(12),
	HI3670_HKADC_CHANNEL(13),
	HI3670_HKADC_CHANNEL(14),
	HI3670_HKADC_CHANNEL(15),
	HI3670_HKADC_CHANNEL(16),
	HI3670_HKADC_CHANNEL(17),
};

static int hi3670_hkadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi3670_hkadc *adc;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->tx_client.dev = dev;
	adc->tx_client.tx_block = true;
	adc->tx_client.tx_tout = HI3670_HKADC_TIMEOUT_MS;
	adc->rx_client.dev = dev;
	adc->rx_client.rx_callback = hi3670_hkadc_rx_callback;

	mutex_init(&adc->lock);
	init_completion(&adc->done);

	adc->tx = mbox_request_channel_byname(&adc->tx_client,
					      HI3670_HKADC_MBOX_TX);
	if (IS_ERR(adc->tx))
		return dev_err_probe(dev, PTR_ERR(adc->tx),
				     "failed to get TX mailbox\n");

	adc->rx = mbox_request_channel_byname(&adc->rx_client,
					      HI3670_HKADC_MBOX_RX);
	if (IS_ERR(adc->rx)) {
		ret = dev_err_probe(dev, PTR_ERR(adc->rx),
				    "failed to get RX mailbox\n");
		goto err_free_tx;
	}

	indio_dev->name = "hi3670-hkadc";
	indio_dev->info = &hi3670_hkadc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = hi3670_hkadc_channels;
	indio_dev->num_channels = ARRAY_SIZE(hi3670_hkadc_channels);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_free_rx;

	mutex_lock(&hi3670_hkadc_dev_lock);
	hi3670_hkadc_dev = adc;
	mutex_unlock(&hi3670_hkadc_dev_lock);

	platform_set_drvdata(pdev, indio_dev);

	return 0;

err_free_rx:
	mbox_free_channel(adc->rx);
err_free_tx:
	mbox_free_channel(adc->tx);
	return ret;
}

static void hi3670_hkadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct hi3670_hkadc *adc = iio_priv(indio_dev);

	mutex_lock(&hi3670_hkadc_dev_lock);
	if (hi3670_hkadc_dev == adc)
		hi3670_hkadc_dev = NULL;
	mutex_unlock(&hi3670_hkadc_dev_lock);

	iio_device_unregister(indio_dev);
	mbox_free_channel(adc->rx);
	mbox_free_channel(adc->tx);
}

static const struct of_device_id hi3670_hkadc_of_match[] = {
	{ .compatible = "hisilicon,hi3670-hkadc" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3670_hkadc_of_match);

static struct platform_driver hi3670_hkadc_driver = {
	.probe = hi3670_hkadc_probe,
	.remove = hi3670_hkadc_remove,
	.driver = {
		.name = "hi3670-hkadc",
		.of_match_table = hi3670_hkadc_of_match,
	},
};
module_platform_driver(hi3670_hkadc_driver);

MODULE_DESCRIPTION("HiSilicon Hi3670 HKADC driver");
MODULE_LICENSE("GPL");
