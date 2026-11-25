/*
 * FocalTech FTS SPI touchscreen driver (Kirin 970 / COR-AL00)
 *
 * Port of the FocalTech touch path that the Honor Play (Hi3670 / COR-AL00)
 * panel needs, decoupled from the vendor's private "huawei,ts_kit" framework
 * and rewritten onto the standard SPI + input subsystems.
 *
 * The panel is a FocalTech FTS controller on SPI3 (pl022), in-cell
 * (is_in_cell = 1), 1080 x 2340, chip id 0x87 (ic_type 3 -> FT8719).
 * The panel firmware is already running when the LCD powers it on, so there
 * is NO firmware download in this driver - the vendor's 2879-line flash path
 * is upgrade/exception-only and is off the boot path entirely.
 *
 * The controller does not accept plain register reads/writes over SPI; it
 * speaks a small packet protocol (STATUS / COMMAND / DATA packages with a
 * Kermit CRC).  That transport is reimplemented here, ported from the
 * vendor's focaltech_spi.c with the DMA-mode branches removed (mainline
 * pl022 does not parse the vendor "pl022,com-mode" extension).
 *
 * MODULE_LICENSE: GPL (reuses spi_sync_transfer / input core, both fine).
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/unaligned.h>

#define DRIVER_NAME	"focaltech_fts"

/*
 * FTS SPI packet protocol (vendor focaltech_spi.c).
 */
#define FTS_SPI_STATUS_PACKAGE		0x05
#define FTS_SPI_COMMAND_PACKAGE		0xC0
#define FTS_SPI_DATA_PACKAGE		0x3F

#define FTS_CTRL_READ_WRITE_FLAG	7
#define FTS_CTRL_CMD_CRC_BIT		6
#define FTS_CTRL_DATA_CRC_BIT		5

#define FTS_STATUS_BUSY_BIT		7
#define FTS_SPI_STATUS_MASK		0x81

#define FTS_WRITE_CMD			0x00
#define FTS_READ_CMD			0x80

#define FTS_BUSY_QUERY_TIMEOUT		100
#define FTS_BUSY_QUERY_DELAY		150	/* us */
#define FTS_CS_HIGH_DELAY		30	/* us */
#define FTS_DELAY_AFTER_FIRST_BYTE	20	/* us */

#define FTS_CRC_POLYNOMIAL		0x8408

/* boot-mode read/write payload cap ((4k)-4) */
#define FTS_PACKAGE_SIZE_SPI		((4 * 1024) - 4)

/*
 * FTS register / report layout (vendor focaltech_core.c).
 */
#define FTS_REG_BOOT_CHIP_ID		0x90

#define FTS_MAX_TOUCH_POINTS		10
#define FTS_POINT_DATA_SIZE		6
#define FTS_MAX_POINT_ID			0x0F

#define FTS_EDGE_DATA_OFF		0x40
#define FTS_FP_EDGE_TOUCH_DATA_LEN	70
#define FTS_FP_EDGE_POINT_DATA_SIZE	7
#define FTS_ADDR_EDGE_WX			5
#define FTS_ADDR_EDGE_WY			6

/* bytes read per interrupt: header + 10 points (6B) + edge block (0x40..0x40+70) */
#define FTS_TOUCH_DATA_LEN		(3 + FTS_POINT_DATA_SIZE * FTS_MAX_TOUCH_POINTS)
#define FTS_REPORT_ALL_LEN		(FTS_TOUCH_DATA_LEN + FTS_EDGE_DATA_OFF + \
					 FTS_FP_EDGE_TOUCH_DATA_LEN)

/* reset / detect timing (vendor focaltech_core.c) */
#define FTS_RESET_FIRST_START_DELAY	240	/* ms, first start */
#define FTS_RESET_NORMAL_DELAY		120	/* ms, subsequent */
#define FTS_RESET_KEEP_LOW_TIME		1500	/* us */
#define FTS_DETECT_RETRY_TIMES		3

#define FTS_FW_STATE_ERROR		0xEF

/* worst-case transfer buffers */
#define FTS_SPI_BUF_LENGTH		256
#define FTS_MAX_COMMAND_LENGTH		16

/*
 * Report byte offsets.  The vendor reads the report as a "default read" (no
 * register address on the wire - the firmware's read pointer already sits at
 * 0x71).  It stores the received bytes at buf[1..] (buf[0] holds a vestigial
 * 0x71) and decodes with absolute buf[] indices (ADDR_POINT_NUM = 2, ...).
 * That means the vendor's buf[k] is the (k-1)-th *received* byte.
 *
 * This driver strips the DATA-package byte, so data[] holds the received
 * content directly: data[k] == received byte k == vendor buf[k+1].  The
 * macros below therefore use the vendor's absolute index minus one.  In
 * received space the layout is: [0]=status/type, [1]=point count, then 10
 * six-byte points, and the edge (width/height) block at offset 0x40.
 */
#define RB_STATUS(rb)			((rb)[0])
#define RB_POINT_NUM(rb)		((rb)[1] & 0x0F)
#define RB_FINGER_ID(rb, off)		((rb)[4 + (off)] >> 4)
#define RB_EVENT(rb, off)		((rb)[2 + (off)] >> 6)
#define RB_X(rb, off)			(((u16)((rb)[2 + (off)] & 0x0F) << 8) | \
						 (rb)[3 + (off)])
#define RB_Y(rb, off)			(((u16)((rb)[4 + (off)] & 0x0F) << 8) | \
						 (rb)[5 + (off)])
#define RB_PRESSURE(rb, off)		((rb)[6 + (off)])
#define RB_AREA(rb, off)		((rb)[7 + (off)] >> 4)
#define RB_EDGE(rb)			(&(rb)[FTS_EDGE_DATA_OFF])
#define RB_EDGE_WX(rb, id)		(RB_EDGE(rb)[(id) * FTS_FP_EDGE_POINT_DATA_SIZE + \
						     FTS_ADDR_EDGE_WX])
#define RB_EDGE_WY(rb, id)		(RB_EDGE(rb)[(id) * FTS_FP_EDGE_POINT_DATA_SIZE + \
						     FTS_ADDR_EDGE_WY])

struct fts_ts_data {
	struct spi_device *spi;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;

	/* default screen geometry, overridable from DT */
	unsigned int max_x;
	unsigned int max_y;

	/* serialises the multi-message packet transactions */
	struct mutex lock;
};

/*
 * Kermit CRC (reflected, poly 0x8408, init 0xFFFF), ported verbatim from the
 * vendor's crckermit().  Self-contained so the driver does not need to
 * select CRC_CCITT.
 */
static void fts_crc_kermit(u8 *data, u16 len, u16 *crc_out)
{
	u16 i, j, crc = 0xFFFF;

	if (!data || !crc_out)
		return;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if (crc & 0x01)
				crc = (crc >> 1) ^ FTS_CRC_POLYNOMIAL;
			else
				crc >>= 1;
		}
	}

	*crc_out = crc;
}

/*
 * Raw SPI transfer.  The FTS link needs the first byte isolated in its own
 * transfer with a delay after it (the controller latches the package type on
 * the leading byte); the remaining bytes follow in a second transfer.  For a
 * read both buffers alias the same memory (full duplex).
 */
static int fts_spi_write(struct spi_device *spi, u8 *buf, u32 len)
{
	struct spi_transfer xfer[2];

	if (len < 1)
		return -EINVAL;

	memset(xfer, 0, sizeof(xfer));
	xfer[0].tx_buf = buf;
	xfer[0].len = 1;
	xfer[0].bits_per_word = 8;
	xfer[0].delay.value = FTS_DELAY_AFTER_FIRST_BYTE;
	xfer[0].delay.unit = SPI_DELAY_UNIT_USECS;
	xfer[1].tx_buf = buf + 1;
	xfer[1].len = len - 1;

	return spi_sync_transfer(spi, xfer, 2);
}

static int fts_spi_read(struct spi_device *spi, u8 *buf, u32 len)
{
	struct spi_transfer xfer[2];

	if (len < 2)
		return -EINVAL;

	memset(xfer, 0, sizeof(xfer));
	xfer[0].tx_buf = buf;
	xfer[0].rx_buf = buf;
	xfer[0].len = 1;
	xfer[0].bits_per_word = 8;
	xfer[0].delay.value = FTS_DELAY_AFTER_FIRST_BYTE;
	xfer[0].delay.unit = SPI_DELAY_UNIT_USECS;
	xfer[1].tx_buf = buf + 1;
	xfer[1].rx_buf = buf + 1;
	xfer[1].len = len - 1;

	return spi_sync_transfer(spi, xfer, 2);
}

/* read the 1-byte SPI status register */
static int fts_read_status(struct spi_device *spi, u8 *status)
{
	u8 status_cmd[2] = { FTS_SPI_STATUS_PACKAGE, 0xFF };
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = status_cmd,
		.rx_buf = status_cmd,
		.len = 2,
	};
	int ret;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(spi, &msg);
	if (ret)
		return ret;

	*status = status_cmd[1];
	return 0;
}

/*
 * Wait until the SPI slave is idle for the requested mode.  After the panel
 * firmware is running the idle status is 0x01; in ROM/bootloader mode it is
 * 0x00.  We only ever run with firmware up, so we poll for 0x01.
 */
static int fts_wait_idle(struct fts_ts_data *ts)
{
	int ret, i;
	u8 status = 0xFF;

	for (i = 0; i < FTS_BUSY_QUERY_TIMEOUT; i++) {
		usleep_range(FTS_BUSY_QUERY_DELAY,
			     FTS_BUSY_QUERY_DELAY + 50);

		ret = fts_read_status(ts->spi, &status);
		if (ret >= 0) {
			status &= FTS_SPI_STATUS_MASK;
			if (status == 0x01)
				break;
		}
	}

	if (i >= FTS_BUSY_QUERY_TIMEOUT)
		return -EIO;

	udelay(FTS_CS_HIGH_DELAY);
	return 0;
}

/* send a COMMAND package: [0xC0][ctrl|len][cmd..][crc16?] */
static int fts_cmd_write(struct fts_ts_data *ts, u8 ctrl, u8 *cmd, u8 len)
{
	u8 buf[FTS_MAX_COMMAND_LENGTH];
	u16 crc;
	int pos = 0, i;

	if (len <= 0 || len >= FTS_MAX_COMMAND_LENGTH - 4)
		return -EINVAL;

	buf[pos++] = FTS_SPI_COMMAND_PACKAGE;
	buf[pos++] = ctrl | (len & 0x0F);
	for (i = 0; i < len; i++)
		buf[pos++] = cmd[i];

	if (ctrl & BIT(FTS_CTRL_CMD_CRC_BIT)) {
		fts_crc_kermit(buf, pos, &crc);
		buf[pos++] = crc & 0xFF;
		buf[pos++] = (crc >> 8) & 0xFF;
	}

	return fts_spi_write(ts->spi, buf, pos);
}

/*
 * Write a register + data block (firmware-running mode):
 * COMMAND package carrying [reg][len>>8][len&0xff], then a DATA package.
 */
static __maybe_unused int fts_fw_write(struct fts_ts_data *ts, u8 addr, u8 *data, u32 datalen)
{
	u8 cmd[3];
	int ret;

	if (datalen > FTS_PACKAGE_SIZE_SPI)
		return -EINVAL;

	ret = fts_wait_idle(ts);
	if (ret)
		return ret;

	cmd[0] = addr;
	cmd[1] = (datalen >> 8) & 0xFF;
	cmd[2] = datalen & 0xFF;
	ret = fts_cmd_write(ts, FTS_WRITE_CMD, cmd, 3);
	if (ret)
		return ret;

	if (data && datalen) {
		u8 *buf = kmalloc(datalen + 1, GFP_KERNEL);

		if (!buf)
			return -ENOMEM;
		buf[0] = FTS_SPI_DATA_PACKAGE;
		memcpy(buf + 1, data, datalen);

		ret = fts_wait_idle(ts);
		if (ret) {
			kfree(buf);
			return ret;
		}
		ret = fts_spi_write(ts->spi, buf, datalen + 1);
		kfree(buf);
	}

	return ret;
}

/*
 * Read a register block (firmware-running mode):
 * COMMAND package carrying [reg][len>>8][len&0xff], then a DATA read.
 */
static int fts_fw_read(struct fts_ts_data *ts, u8 addr, u8 *data, u32 datalen)
{
	u8 cmd[3];
	u8 *buf;
	int ret;

	if (datalen == 0 || datalen > FTS_PACKAGE_SIZE_SPI)
		return -EINVAL;

	buf = kmalloc(datalen + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = fts_wait_idle(ts);
	if (ret)
		goto out;

	cmd[0] = addr;
	cmd[1] = (datalen >> 8) & 0xFF;
	cmd[2] = datalen & 0xFF;
	ret = fts_cmd_write(ts, FTS_READ_CMD, cmd, 3);
	if (ret)
		goto out;

	ret = fts_wait_idle(ts);
	if (ret)
		goto out;

	memset(buf, 0xFF, datalen + 1);
	buf[0] = FTS_SPI_DATA_PACKAGE;
	ret = fts_spi_read(ts->spi, buf, datalen + 1);
	if (ret)
		goto out;

	memcpy(data, buf + 1, datalen);
out:
	kfree(buf);
	return ret;
}

/*
 * The touch report is a *default* read: no COMMAND package, no register
 * address - the firmware's read pointer already sits at the report base
 * (0x71).  We just send a DATA package and clock the report back.
 */
static int fts_read_touchdata(struct fts_ts_data *ts, u8 *data, u32 datalen)
{
	u8 *buf;
	int ret;

	buf = kmalloc(datalen + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(buf, 0xFF, datalen + 1);
	buf[0] = FTS_SPI_DATA_PACKAGE;
	ret = fts_spi_read(ts->spi, buf, datalen + 1);
	if (!ret)
		memcpy(data, buf + 1, datalen);
	kfree(buf);
	return ret;
}

static int fts_read_reg(struct fts_ts_data *ts, u8 addr, u8 *val)
{
	return fts_fw_read(ts, addr, val, 1);
}

/*
 * Hardware reset: assert reset low, release, wait for the controller to come
 * up.  First-start needs the longer 240 ms settle.
 */
static int fts_hw_reset(struct fts_ts_data *ts, bool first)
{
	if (!ts->reset_gpio)
		return 0;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	usleep_range(FTS_RESET_KEEP_LOW_TIME, FTS_RESET_KEEP_LOW_TIME + 100);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);

	msleep(first ? FTS_RESET_FIRST_START_DELAY : FTS_RESET_NORMAL_DELAY);
	return 0;
}

/* Read the boot chip id; accept anything but the bus/IC error sentinels. */
static int fts_detect_chip(struct fts_ts_data *ts)
{
	int ret, i;
	u8 id = 0;

	for (i = 0; i < FTS_DETECT_RETRY_TIMES; i++) {
		ret = fts_read_reg(ts, FTS_REG_BOOT_CHIP_ID, &id);
		if (!ret && id != 0x00 && id != 0xFF && id != FTS_FW_STATE_ERROR) {
			dev_info(&ts->spi->dev, "FTS touchscreen, chip id 0x%02x\n", id);
			return 0;
		}
		msleep(50);
	}

	dev_err(&ts->spi->dev, "chip detect failed, id=0x%02x ret=%d\n", id, ret);
	return -ENODEV;
}

static irqreturn_t fts_irq_handler(int irq, void *dev_id)
{
	struct fts_ts_data *ts = dev_id;
	struct input_dev *input = ts->input;
	u8 data[FTS_REPORT_ALL_LEN];
	u8 *rb;
	int ret, i, points;
	bool active = false;

	ret = fts_read_touchdata(ts, data, FTS_REPORT_ALL_LEN);
	if (ret) {
		dev_warn(&ts->spi->dev, "touch read failed: %d\n", ret);
		return IRQ_HANDLED;
	}

	rb = data;

	/*
	 * FTS_FW_STATE_ERROR is only meaningful when the panel is powered and
	 * running; a chip still coming up may transiently return it, so we just
	 * drop the frame rather than erroring.  Vendor checks buf[2..4]; those
	 * are received bytes 1..3 in this driver's window.
	 */
	if (rb[1] == FTS_FW_STATE_ERROR && rb[2] == FTS_FW_STATE_ERROR &&
	    rb[3] == FTS_FW_STATE_ERROR)
		return IRQ_HANDLED;

	/* idle marker (vendor buf[1] == 0x01) - not a report frame */
	if (rb[0] == 0x01)
		return IRQ_HANDLED;

	points = RB_POINT_NUM(rb);
	if (points > FTS_MAX_TOUCH_POINTS)
		points = FTS_MAX_TOUCH_POINTS;

	for (i = 0; i < points; i++) {
		int off = FTS_POINT_DATA_SIZE * i;
		int id = RB_FINGER_ID(rb, off);
		int x, y;

		if (id >= FTS_MAX_TOUCH_POINTS)
			break;

		x = RB_X(rb, off);
		y = RB_Y(rb, off);
		if (x > ts->max_x)
			x = ts->max_x;
		if (y > ts->max_y)
			y = ts->max_y;

		input_mt_slot(input, id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_abs(input, ABS_MT_PRESSURE, RB_PRESSURE(rb, off));
		input_report_abs(input, ABS_MT_TOUCH_MAJOR,
				 RB_EDGE_WX(rb, id));
		input_report_abs(input, ABS_MT_TOUCH_MINOR,
				 RB_EDGE_WY(rb, id));
		active = true;
	}

	if (!active) {
		/* all fingers up */
		for (i = 0; i < FTS_MAX_TOUCH_POINTS; i++) {
			input_mt_slot(input, i);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
		}
	}

	input_mt_sync_frame(input);
	input_sync(input);
	return IRQ_HANDLED;
}

static int fts_probe(struct spi_device *spi)
{
	struct fts_ts_data *ts;
	struct device_node *np = spi->dev.of_node;
	int ret;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_1;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	ts = devm_kzalloc(&spi->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->spi = spi;
	mutex_init(&ts->lock);
	spi_set_drvdata(spi, ts);

	/* geometry: DT override, else the COR-AL00 panel defaults */
	if (of_property_read_u32(np, "touchscreen-size-x", &ts->max_x))
		ts->max_x = 1080;
	if (of_property_read_u32(np, "touchscreen-size-y", &ts->max_y))
		ts->max_y = 2340;

	ts->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						 GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio)) {
		ret = PTR_ERR(ts->reset_gpio);
		dev_err(&spi->dev, "reset gpio failed: %d\n", ret);
		return ret;
	}

	mutex_lock(&ts->lock);
	fts_hw_reset(ts, true);
	ret = fts_detect_chip(ts);
	mutex_unlock(&ts->lock);
	if (ret) {
		/*
		 * In-cell panel: the LCD powers the touch controller.  If the
		 * display has not come up yet the chip-id read fails; defer so
		 * the kernel re-probes once hisi_dpe has powered the panel.
		 */
		dev_info(&spi->dev, "deferring probe (panel not powered yet?)\n");
		return -EPROBE_DEFER;
	}

	ts->input = devm_input_allocate_device(&spi->dev);
	if (!ts->input)
		return -ENOMEM;

	ts->input->name = "FocalTech FTS TouchScreen";
	ts->input->phys = "input/ts";
	ts->input->id.bustype = BUS_SPI;
	ts->input->id.vendor = 0x2804;	/* FocalTech */
	ts->input->id.product = 0x8719;
	ts->input->id.version = 0x0100;
	__set_bit(INPUT_PROP_DIRECT, ts->input->propbit);

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	ret = input_mt_init_slots(ts->input, FTS_MAX_TOUCH_POINTS,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;

	ret = input_register_device(ts->input);
	if (ret)
		return ret;

	if (spi->irq <= 0) {
		dev_err(&spi->dev, "no IRQ in device tree\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
					fts_irq_handler, IRQF_ONESHOT,
					DRIVER_NAME, ts);
	if (ret) {
		dev_err(&spi->dev, "request irq failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void fts_remove(struct spi_device *spi)
{
	struct fts_ts_data *ts = spi_get_drvdata(spi);

	if (ts->reset_gpio)
		gpiod_set_value_cansleep(ts->reset_gpio, 1);
}

static int fts_suspend(struct device *dev)
{
	struct fts_ts_data *ts = dev_get_drvdata(dev);

	/*
	 * The controller is powered by the in-cell panel, which may be turned
	 * off underneath us.  Mask the IRQ so the threaded handler does not
	 * keep polling a powered-down chip.
	 */
	disable_irq(ts->spi->irq);

	return 0;
}

static int fts_resume(struct device *dev)
{
	struct fts_ts_data *ts = dev_get_drvdata(dev);

	/*
	 * The panel may have lost power while suspended; give the controller
	 * a reset pulse so its firmware comes back up, then unmask the IRQ.
	 */
	mutex_lock(&ts->lock);
	fts_hw_reset(ts, false);
	mutex_unlock(&ts->lock);

	enable_irq(ts->spi->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(fts_pm_ops, fts_suspend, fts_resume);

static const struct of_device_id fts_of_match[] = {
	{ .compatible = "focaltech,fts" },
	{ }
};
MODULE_DEVICE_TABLE(of, fts_of_match);

static struct spi_driver fts_spi_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= fts_of_match,
		.pm		= &fts_pm_ops,
	},
	.probe	= fts_probe,
	.remove	= fts_remove,
};

module_spi_driver(fts_spi_driver);

MODULE_AUTHOR("Ported for Kirin 970 COR-AL00");
MODULE_DESCRIPTION("FocalTech FTS SPI touchscreen driver");
MODULE_LICENSE("GPL");
