// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics TCM SPI touchscreen driver (Kirin 970 / COR-AL00)
 *
 * The Honor Play (Hi3670 / COR-AL00) drives a Synaptics Touch Controller
 * Module (TCM) on SPI3 (GPIO144 CLK, GPIO145 DI, GPIO146 DO) with GPIO147
 * as chip select.  The part is "ramless": after every reset it only runs
 * the F$35 microbootloader, so the host has to download the application
 * firmware into PMEM before any touch report can be read.  This driver
 * implements
 *
 *   - the TCM message transport (0xa5 marker, 4-byte header, 0x5a padding),
 *   - the F$35 host-download path (PDT probe, firmware image CRC check,
 *     WRITE_FW_TO_PMEM) plus the display/touch config download the
 *     firmware asks for afterwards,
 *   - the touch report parser, driven by the report layout the firmware
 *     hands out via CMD_GET_TOUCH_REPORT_CONFIG,
 *   - standard Linux input multi-touch reporting (type B).
 *
 * The vendor driver in drivers/devkit/tpkit/panel/synaptics_tcm/ is a
 * huawei_ts_kit sub-driver with its own command queue, character device,
 * factory test and gesture stack; none of that is needed for touch input
 * and none of it is portable, so only the on-the-wire protocol is reused.
 *
 * Firmware: the vendor's ts/corn_synaptics_tcm_corn69130.img (magic
 * 0x4818472b).  Point "syna,firmware-name" at it and install it in
 * /lib/firmware (or build it in with CONFIG_EXTRA_FIRMWARE).
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#define DRIVER_NAME			"synaptics_tcm_spi"

/* ---- TCM transport ---------------------------------------------------- */

#define TCM_MARKER			0xa5
#define TCM_PADDING			0x5a
#define TCM_HEADER_LEN			4
#define TCM_READ_LENGTH			256
#define TCM_MAX_PAYLOAD			(TCM_READ_LENGTH - TCM_HEADER_LEN - 1)
#define TCM_REPORT_CONFIG_LEN		128
#define TCM_IDENTIFY_LEN		24
#define TCM_APP_INFO_LEN		50
#define TCM_IDENTIFY_DRAIN_LEN		30

#define TCM_DEFAULT_SPI_HZ		10000000
#define TCM_RMI_READ_HZ			100000
#define TCM_RMI_WRITE_HZ		10000000

#define TCM_RESPONSE_TIMEOUT_MS		3000
#define TCM_RESPONSE_POLL_US		5000
#define TCM_READ_RETRY_US		5000
#define TCM_APP_STATUS_POLL_MS		100
#define TCM_APP_STATUS_POLL_TRIES	10

/* commands (vendor core.h: enum command) */
#define CMD_CONTINUE_WRITE		0x01
#define CMD_IDENTIFY			0x02
#define CMD_ENABLE_REPORT		0x05
#define CMD_DISABLE_REPORT		0x06
#define CMD_GET_APPLICATION_INFO	0x20
#define CMD_GET_TOUCH_REPORT_CONFIG	0x25
#define CMD_DOWNLOAD_CONFIG		0x30

/* status codes */
#define STATUS_IDLE			0x00
#define STATUS_OK			0x01
#define STATUS_BUSY			0x02
#define STATUS_CONTINUED_READ		0x03
#define STATUS_ERROR			0x0f

/* report codes */
#define REPORT_IDENTIFY			0x10
#define REPORT_TOUCH			0x11
#define REPORT_STATUS			0x1b

/* firmware identification modes */
#define MODE_APPLICATION		0x01

/* ---- F$35 host download ---------------------------------------------- */

#define PDT_START_ADDR			0x00e9
#define PDT_END_ADDR			0x00ee
#define UBL_FN_NUMBER			0x35

#define F35_CTRL3_OFFSET		18
#define F35_CTRL7_OFFSET		22
#define F35_WRITE_FW_TO_PMEM		4
#define F35_REQUESTING_FIRMWARE		11
#define F35_CHECKSUM_FAILURE		6

#define IMAGE_MAGIC			0x4818472b
#define AREA_MAGIC			0x7c05e516
#define AREA_HDR_LEN			36
#define HDL_TOUCH_CONFIG_TO_PMEM	1
#define HDL_DISPLAY_CONFIG_TO_PMEM	2
#define CONFIG_DOWNLOAD_RETRY		10
#define IMAGE_DOWNLOAD_RETRY		5
#define STATUS_REPORT_RETRY		3

/* ---- touch report layout (vendor touch.c: enum touch_report_code) ----- */

#define TOUCH_END			0x00
#define TOUCH_FOREACH_ACTIVE_OBJECT	0x01
#define TOUCH_FOREACH_OBJECT		0x02
#define TOUCH_FOREACH_END		0x03
#define TOUCH_PAD_TO_NEXT_BYTE		0x04
#define TOUCH_TIMESTAMP			0x05
#define TOUCH_OBJECT_N_INDEX		0x06
#define TOUCH_OBJECT_N_CLASSIFICATION	0x07
#define TOUCH_OBJECT_N_X_POSITION	0x08
#define TOUCH_OBJECT_N_Y_POSITION	0x09
#define TOUCH_OBJECT_N_Z		0x0a
#define TOUCH_OBJECT_N_X_WIDTH		0x0b
#define TOUCH_OBJECT_N_Y_WIDTH		0x0c
#define TOUCH_OBJECT_N_TX_POSITION	0x0d
#define TOUCH_OBJECT_N_RX_POSITION	0x0e
#define TOUCH_0D_BUTTONS_STATE		0x0f
#define TOUCH_GESTURE_DOUBLE_TAP	0x10
#define TOUCH_FRAME_RATE		0x11
#define TOUCH_POWER_IM			0x12
#define TOUCH_CID_IM			0x13
#define TOUCH_RAIL_IM			0x14
#define TOUCH_CID_VARIANCE_IM		0x15
#define TOUCH_NSM_FREQUENCY		0x16
#define TOUCH_NSM_STATE			0x17
#define TOUCH_NUM_OF_ACTIVE_OBJECTS	0x18
#define TOUCH_NUM_OF_CPU_CYCLES		0x19
#define TOUCH_TUNING_GAUSSIAN_WIDTHS	0x80
#define TOUCH_TUNING_SMALL_OBJECT_PARAMS 0x81
#define TOUCH_TUNING_0D_BUTTONS_VARIANCE 0x82
#define TOUCH_ROI_DATA			0xca
#define TOUCH_GRIP_DATA			0xcb
#define TOUCH_ESD_DETECT		0xcc

#define TOUCH_LIFT			0
#define TOUCH_FINGER			1
#define TOUCH_GLOVED_FINGER		2

#define TCM_MAX_OBJECTS			10

struct tcm_object {
	u8 status;
	u32 x;
	u32 y;
	u32 x_width;
	u32 y_width;
	u32 z;
	u32 grip;
};

struct tcm_image {
	const u8 *app;
	size_t app_len;
	const u8 *app_config;
	size_t app_config_len;
	const u8 *disp_config;
	size_t disp_config_len;
};

struct tcm_ts {
	struct spi_device *spi;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct mutex lock;		/* serialises IRQ handling with PM */

	u8 *rx;			/* SPI read scratch, TCM_READ_LENGTH bytes */
	u8 *tx_dummy;		/* 0xff fill for full-duplex reads */
	u8 *msg;		/* last received payload */

	u8 config[TCM_REPORT_CONFIG_LEN];
	u32 config_len;
	u32 report_len;
	u32 foreach_end;	/* config index after the last FOREACH_END */
	bool esd_frame;

	struct tcm_object obj[TCM_MAX_OBJECTS];
	bool prev_status[TCM_MAX_OBJECTS];

	u16 max_x;
	u16 max_y;
	u8 max_objects;
	u16 wr_chunk_size;		/* 0 = no chunking */
	u8 slot_count;			/* input_mt slots actually allocated */

	u32 reset_active_ms;
	u32 reset_delay_ms;
	u8 dt_max_objects;

	bool swapped_x_y;
	bool inverted_x;
	bool inverted_y;
	bool suspended;

	const char *fw_name;

	unsigned int irq_errors;
};

static int tcm_spi_xfer(struct tcm_ts *ts, const void *tx, void *rx,
			size_t len, u32 speed_hz)
{
	struct spi_transfer xfer = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = len,
		.bits_per_word = 8,
	};
	int ret;

	if (len > TCM_READ_LENGTH)
		return -EINVAL;

	if (ts->spi->max_speed_hz != speed_hz) {
		ts->spi->max_speed_hz = speed_hz;
		ret = spi_setup(ts->spi);
		if (ret)
			return ret;
	}

	return spi_sync_transfer(ts->spi, &xfer, 1);
}

static int tcm_write_raw(struct tcm_ts *ts, const void *data, size_t len)
{
	return tcm_spi_xfer(ts, data, NULL, len, TCM_DEFAULT_SPI_HZ);
}

static int tcm_read_raw(struct tcm_ts *ts, void *data, size_t len)
{
	if (len > TCM_READ_LENGTH)
		return -EINVAL;

	memset(ts->tx_dummy, 0xff, len);
	return tcm_spi_xfer(ts, ts->tx_dummy, data, len, TCM_DEFAULT_SPI_HZ);
}

/* ---- F$35 / RMI access ------------------------------------------------ */

static int tcm_rmi_read(struct tcm_ts *ts, u16 addr, void *data, size_t len)
{
	struct spi_transfer xfer[2] = {
		{ .tx_buf = ts->rx, .len = 2, .bits_per_word = 8, },
		{ .rx_buf = data, .len = len, .bits_per_word = 8, },
	};
	int ret;

	/* rx[0..1] hold the address and rx[2..] the dummy clocks */
	if (len > TCM_READ_LENGTH - 2)
		return -EINVAL;

	ts->rx[0] = (addr >> 8) | 0x80;	/* bit 7: read */
	ts->rx[1] = addr & 0xff;
	memset(ts->rx + 2, 0xff, len);
	xfer[1].tx_buf = ts->rx + 2;

	if (ts->spi->max_speed_hz != TCM_RMI_READ_HZ) {
		ts->spi->max_speed_hz = TCM_RMI_READ_HZ;
		ret = spi_setup(ts->spi);
		if (ret)
			return ret;
	}

	return spi_sync_transfer(ts->spi, xfer, 2);
}

static int tcm_rmi_write(struct tcm_ts *ts, u16 addr, const void *data,
			 size_t len)
{
	struct spi_transfer xfer;
	u8 *buf;
	int ret;

	if (len > TCM_MAX_PAYLOAD)
		return -EINVAL;

	buf = kmalloc(len + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = (addr >> 8) & ~0x80;
	buf[1] = addr & 0xff;
	memcpy(buf + 2, data, len);

	if (ts->spi->max_speed_hz != TCM_RMI_WRITE_HZ) {
		ts->spi->max_speed_hz = TCM_RMI_WRITE_HZ;
		ret = spi_setup(ts->spi);
		if (ret)
			goto out;
	}

	xfer.tx_buf = buf;
	xfer.len = len + 2;
	xfer.bits_per_word = 8;
	ret = spi_sync_transfer(ts->spi, &xfer, 1);
out:
	kfree(buf);
	return ret;
}

/* ---- TCM messages ----------------------------------------------------- */

static int tcm_write_cmd(struct tcm_ts *ts, u8 command, const void *payload,
			 u16 length)
{
	u32 remaining = length + 2;	/* length field counts itself */
	u32 chunk = 0;
	u8 *buf;
	int ret = 0;
	u32 idx, xfer_len, chunks;

	if (length && !payload)
		return -EINVAL;

	/* a chunk smaller than the 2-byte length header makes no sense */
	if (ts->wr_chunk_size >= 4)
		chunk = ts->wr_chunk_size - 1;

	chunks = chunk ? DIV_ROUND_UP(remaining, chunk) : 1;

	buf = kmalloc(remaining + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (idx = 0; idx < chunks; idx++) {
		xfer_len = chunk && remaining > chunk ? chunk : remaining;

		if (idx == 0) {
			buf[0] = command;
			put_unaligned_le16(length, buf + 1);
			if (xfer_len > 2)
				memcpy(buf + 3, payload, xfer_len - 2);
			ret = tcm_write_raw(ts, buf, xfer_len + 1);
		} else {
			buf[0] = CMD_CONTINUE_WRITE;
			memcpy(buf + 1, (const u8 *)payload + idx * chunk - 2,
			       xfer_len);
			ret = tcm_write_raw(ts, buf, xfer_len + 1);
		}
		if (ret)
			break;

		remaining -= xfer_len;
		if (chunks > 1)
			usleep_range(500, 1000);
	}

	kfree(buf);
	return ret;
}

/*
 * Read one TCM message.  The device pushes messages; there is no read
 * command.  Layout: [0xa5][code][len_lo][len_hi][payload...][0x5a].
 */
static int tcm_read_msg(struct tcm_ts *ts, u8 *code, u8 *payload, u32 *plen)
{
	int retry, ret;
	u32 len;

	for (retry = 0; retry < 2; retry++) {
		ret = tcm_read_raw(ts, ts->rx, TCM_READ_LENGTH);
		if (ret)
			return ret;

		if (ts->rx[0] != TCM_MARKER) {
			usleep_range(TCM_READ_RETRY_US,
				     TCM_READ_RETRY_US + 5000);
			continue;
		}

		len = get_unaligned_le16(ts->rx + 2);
		if (len > TCM_MAX_PAYLOAD)
			return -EIO;

		/*
		 * Empty/continuation responses carry no payload and are not
		 * padded; only real messages are terminated by 0x5a.
		 */
		*code = ts->rx[1];
		if (*code >= STATUS_BUSY) {
			if (ts->rx[TCM_HEADER_LEN + len] != TCM_PADDING)
				return -EIO;
		}

		*plen = len;
		memcpy(payload, ts->rx + TCM_HEADER_LEN, len);
		return 0;
	}

	return -EIO;
}

/*
 * Send a command and wait for its STATUS_OK response.  Report messages that
 * arrive in between are dropped here; once the report stream is enabled they
 * are consumed by the IRQ thread instead.
 */
static int tcm_cmd(struct tcm_ts *ts, u8 command, const void *payload,
		   u16 length, u8 *resp, u32 resp_cap, u32 *resp_len)
{
	unsigned long deadline;
	int ret;

	ret = tcm_write_cmd(ts, command, payload, length);
	if (ret)
		return ret;

	deadline = jiffies + msecs_to_jiffies(TCM_RESPONSE_TIMEOUT_MS);
	do {
		u32 plen = 0;
		u8 code = STATUS_IDLE;

		ret = tcm_read_msg(ts, &code, ts->msg, &plen);
		if (!ret) {
			if (code == STATUS_OK) {
				if (plen > resp_cap)
					return -EMSGSIZE;
				if (resp && resp_len) {
					memcpy(resp, ts->msg, plen);
					*resp_len = plen;
				}
				return 0;
			}
			if (code == STATUS_ERROR)
				return -EIO;
			/* BUSY/IDLE/continuation or a report: keep polling */
		}
		usleep_range(TCM_RESPONSE_POLL_US, TCM_RESPONSE_POLL_US + 5000);
	} while (time_before(jiffies, deadline));

	return -ETIMEDOUT;
}

/* ---- reset / power ---------------------------------------------------- */

/*
 * reset-gpios is declared GPIO_ACTIVE_LOW: logical 1 asserts reset (the
 * panel's TP_RST_N low), logical 0 releases it.  This matches the retail
 * DTS "synaptics,reset-on-state = <0>" (physical low) and its 20ms/200ms
 * timing.
 */
static void tcm_reset(struct tcm_ts *ts)
{
	if (!ts->reset_gpio)
		return;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	msleep(ts->reset_active_ms);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	msleep(ts->reset_delay_ms);
}

/*
 * The bootloader expects this exact pulse before a firmware download:
 * released 1ms, reset 300us, released 21ms (physical high/low/high).
 */
static void tcm_bootloader_pulse(struct tcm_ts *ts)
{
	if (!ts->reset_gpio)
		return;

	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	mdelay(1);
	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	udelay(300);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	msleep(21);
}

/* ---- firmware image handling ----------------------------------------- */

static int tcm_parse_image(struct device *dev, const struct firmware *fw,
			   struct tcm_image *img)
{
	const u8 *image = fw->data;
	u32 num_areas, i;

	memset(img, 0, sizeof(*img));

	if (fw->size < 8 || get_unaligned_le32(image) != IMAGE_MAGIC)
		return -EINVAL;

	num_areas = get_unaligned_le32(image + 4);
	if (num_areas > 16)
		return -EINVAL;
	if (num_areas > (fw->size - 8) / 4)
		return -EINVAL;

	for (i = 0; i < num_areas; i++) {
		const u8 *desc, *content;
		u32 addr, len, crc;
		bool known;

		addr = get_unaligned_le32(image + 8 + i * 4);
		if (addr > fw->size || fw->size - addr < AREA_HDR_LEN)
			return -EINVAL;

		desc = image + addr;
		if (get_unaligned_le32(desc) != AREA_MAGIC)
			continue;

		len = get_unaligned_le32(desc + 28);
		crc = get_unaligned_le32(desc + 32);
		content = desc + AREA_HDR_LEN;
		if (len > fw->size - addr - AREA_HDR_LEN)
			return -EINVAL;

		known = !strncmp(desc + 4, "F35_APP_CODE", 12) ||
			!strncmp(desc + 4, "APP_CONFIG", 10) ||
			!strncmp(desc + 4, "DISPLAY", 7);
		if (!known)
			continue;

		if ((crc32(~0, content, len) ^ ~0) != crc) {
			dev_err(dev, "bad CRC in firmware area %.16s\n",
				desc + 4);
			return -EINVAL;
		}

		if (!strncmp(desc + 4, "F35_APP_CODE", 12)) {
			img->app = content;
			img->app_len = len;
		} else if (!strncmp(desc + 4, "APP_CONFIG", 10)) {
			img->app_config = content;
			img->app_config_len = len;
		} else {
			img->disp_config = content;
			img->disp_config_len = len;
		}
	}

	if (!img->app || !img->app_len)
		return -EINVAL;

	return 0;
}

static int tcm_check_uboot(struct tcm_ts *ts, u16 *control_base, u16 *data_base)
{
	u8 pdt[4], query[3], fn = 0;
	int ret;

	ret = tcm_rmi_read(ts, PDT_END_ADDR, &fn, 1);
	if (ret)
		return ret;
	if (fn != UBL_FN_NUMBER)
		return -ENODEV;

	ret = tcm_rmi_read(ts, PDT_START_ADDR, pdt, sizeof(pdt));
	if (ret)
		return ret;

	*control_base = pdt[2];
	*data_base = pdt[3];

	ret = tcm_rmi_read(ts, pdt[0], query, sizeof(query));
	if (ret)
		return ret;

	/* has_query2 (query[0] bit7), has_ctrl7 + has_host_download (query[2]) */
	if (!(query[0] & BIT(7)) || !(query[2] & BIT(0)) ||
	    !(query[2] & BIT(1)))
		return -ENODEV;

	return 0;
}

static int tcm_download_config(struct tcm_ts *ts, u8 type, const u8 *data,
			       size_t len)
{
	u8 *payload;
	u16 saved_chunk = ts->wr_chunk_size;
	u32 resp_len = 0;
	int ret = -EINVAL, retry;

	if (!data || !len || len + 2 > 0xffff)
		return -EINVAL;

	payload = kmalloc(len + 2, GFP_KERNEL);
	if (!payload)
		return -ENOMEM;
	payload[0] = 1;			/* config format version */
	payload[1] = type;
	memcpy(payload + 2, data, len);

	/* the firmware expects 256-byte write chunks for config download */
	ts->wr_chunk_size = 256;

	for (retry = 0; retry < CONFIG_DOWNLOAD_RETRY; retry++) {
		ret = tcm_cmd(ts, CMD_DOWNLOAD_CONFIG, payload, len + 2,
			      NULL, 0, &resp_len);
		if (!ret)
			break;
	}

	ts->wr_chunk_size = saved_chunk;
	kfree(payload);
	return ret;
}

/* read the status report the firmware pushes after the download */
static int tcm_read_fw_status(struct tcm_ts *ts, u16 *flags)
{
	u8 status[10];
	int retry;

	*flags = 0;
	for (retry = 0; retry < STATUS_REPORT_RETRY; retry++) {
		memset(status, 0, sizeof(status));
		if (tcm_read_raw(ts, status, sizeof(status)))
			return -EIO;
		if (status[0] != TCM_MARKER)
			continue;
		if (status[1] != REPORT_STATUS && status[1] != STATUS_OK)
			continue;
		*flags = get_unaligned_le16(status + 4);
		return 0;
	}

	return -EIO;
}

static int tcm_download_firmware_once(struct tcm_ts *ts,
				      const struct tcm_image *img)
{
	u16 control_base, data_base;
	u8 data = 0, command, identify[TCM_IDENTIFY_DRAIN_LEN];
	int ret, i;

	ret = tcm_check_uboot(ts, &control_base, &data_base);
	if (ret)
		return ret;

	ret = tcm_rmi_read(ts, data_base, &data, 1);
	if (ret)
		return ret;
	/* error_code is the low 5 bits of F$35 data */
	if ((data & 0x1f) != F35_REQUESTING_FIRMWARE &&
	    (data & 0x1f) != F35_CHECKSUM_FAILURE)
		return -EIO;

	tcm_bootloader_pulse(ts);

	command = F35_WRITE_FW_TO_PMEM;
	ret = tcm_rmi_write(ts, control_base + F35_CTRL3_OFFSET, &command, 1);
	if (ret)
		return ret;

	ret = tcm_rmi_write(ts, control_base + F35_CTRL7_OFFSET, img->app,
			    img->app_len);
	if (ret)
		return ret;

	dev_info(&ts->spi->dev, "firmware downloaded (%zu bytes)\n",
		 img->app_len);

	/*
	 * The chip pushes one identify report (4 + 24 + 1 = 29 bytes) once
	 * the application starts; drain a full 30 bytes like the vendor so
	 * the stream stays aligned.
	 */
	msleep(20);
	ret = tcm_read_raw(ts, identify, sizeof(identify));
	if (ret)
		return ret;

	/* display/touch config, if the firmware asks for it */
	udelay(2000);
	for (i = 0; i < IMAGE_DOWNLOAD_RETRY; i++) {
		u16 flags = 0;

		ret = tcm_read_fw_status(ts, &flags);
		if (ret)
			return ret;

		if (!(flags & BIT(1)) && !(flags & BIT(2)))
			break;

		if (flags & BIT(1)) {
			ret = tcm_download_config(ts, HDL_DISPLAY_CONFIG_TO_PMEM,
						  img->disp_config,
						  img->disp_config_len);
			if (ret)
				return ret;
			continue;
		}

		ret = tcm_download_config(ts, HDL_TOUCH_CONFIG_TO_PMEM,
					  img->app_config,
					  img->app_config_len);
		if (ret)
			return ret;
	}

	return 0;
}

static int tcm_download_firmware(struct tcm_ts *ts)
{
	const struct firmware *fw = NULL;
	struct tcm_image img;
	int ret, attempt;

	ret = request_firmware(&fw, ts->fw_name, &ts->spi->dev);
	if (ret) {
		dev_err(&ts->spi->dev, "failed to load %s: %d\n",
			ts->fw_name, ret);
		return ret;
	}

	ret = tcm_parse_image(&ts->spi->dev, fw, &img);
	if (ret) {
		dev_err(&ts->spi->dev, "invalid firmware image\n");
		goto out;
	}

	for (attempt = 0; attempt < 3; attempt++) {
		ret = tcm_download_firmware_once(ts, &img);
		if (!ret)
			break;
		dev_warn(&ts->spi->dev,
			 "firmware download failed (%d), retry %d\n",
			 ret, attempt + 1);
		msleep(20);
	}

out:
	release_firmware(fw);
	return ret;
}

/* ---- application bring-up -------------------------------------------- */

static int tcm_identify(struct tcm_ts *ts)
{
	u8 id[TCM_IDENTIFY_LEN];
	u16 max_write_size;
	u32 len = 0;
	int ret;

	ret = tcm_cmd(ts, CMD_IDENTIFY, NULL, 0, id, sizeof(id), &len);
	if (ret)
		return ret;
	if (len < TCM_IDENTIFY_LEN)
		return -EIO;
	if (id[1] != MODE_APPLICATION)
		return -EIO;

	/*
	 * identification: version, mode, part_number[16], build_id[4],
	 * max_write_size[2]
	 */
	max_write_size = get_unaligned_le16(id + 22);
	ts->wr_chunk_size = max_write_size >= 4 ? max_write_size : 0;

	return 0;
}

static int tcm_get_app_info(struct tcm_ts *ts)
{
	u8 info[TCM_APP_INFO_LEN];
	u32 len = 0;
	int ret = -ETIMEDOUT, i;

	for (i = 0; i < TCM_APP_STATUS_POLL_TRIES; i++) {
		ret = tcm_cmd(ts, CMD_GET_APPLICATION_INFO, NULL, 0, info,
			      sizeof(info), &len);
		if (!ret && len >= 38 &&
		    get_unaligned_le16(info + 2) == STATUS_OK)
			break;
		ret = -EIO;
		msleep(TCM_APP_STATUS_POLL_MS);
	}
	if (ret)
		return ret;
	if (len < 38)
		return -EIO;

	ts->max_x = get_unaligned_le16(info + 32);
	ts->max_y = get_unaligned_le16(info + 34);
	ts->max_objects = min_t(u16, get_unaligned_le16(info + 36),
				TCM_MAX_OBJECTS);
	if (ts->dt_max_objects)
		ts->max_objects = min(ts->max_objects, ts->dt_max_objects);
	if (ts->slot_count)
		ts->max_objects = min(ts->max_objects, ts->slot_count);

	if (!ts->max_x || !ts->max_y || !ts->max_objects)
		return -EIO;

	if (ts->swapped_x_y) {
		u16 tmp = ts->max_x;

		ts->max_x = ts->max_y;
		ts->max_y = tmp;
	}

	return 0;
}

static int tcm_get_report_config(struct tcm_ts *ts)
{
	u8 cfg[TCM_REPORT_CONFIG_LEN];
	u32 len = 0;
	int ret;

	ret = tcm_cmd(ts, CMD_GET_TOUCH_REPORT_CONFIG, NULL, 0, cfg,
		      sizeof(cfg), &len);
	if (ret)
		return ret;

	ts->config_len = min_t(u32, len, TCM_REPORT_CONFIG_LEN);
	memcpy(ts->config, cfg, ts->config_len);
	ts->foreach_end = 0;

	return ts->config_len ? 0 : -EIO;
}

static int tcm_set_report(struct tcm_ts *ts, bool enable)
{
	u8 report = REPORT_TOUCH;

	return tcm_cmd(ts, enable ? CMD_ENABLE_REPORT : CMD_DISABLE_REPORT,
		       &report, 1, NULL, 0, NULL);
}

static int tcm_init_hw(struct tcm_ts *ts)
{
	int ret;

	tcm_reset(ts);

	ret = tcm_download_firmware(ts);
	if (ret)
		return ret;

	ret = tcm_identify(ts);
	if (ret)
		return ret;

	ret = tcm_get_app_info(ts);
	if (ret)
		return ret;

	ret = tcm_get_report_config(ts);
	if (ret)
		return ret;

	ret = tcm_set_report(ts, false);
	if (ret)
		return ret;

	memset(ts->prev_status, 0, sizeof(ts->prev_status));
	ts->irq_errors = 0;

	dev_info(&ts->spi->dev, "TCM ready, %ux%u, %u objects\n",
		 ts->max_x, ts->max_y, ts->max_objects);

	return 0;
}

/* ---- touch report parsing -------------------------------------------- */

/*
 * Extract a LSB-first packed bit field from the report payload, exactly like
 * the vendor's touch_get_report_data().
 */
static u32 tcm_field(const u8 *report, u32 report_len, u32 offset, u32 bits)
{
	u32 value = 0, remaining = bits, bit_off, byte_off, avail, take;
	u8 byte;

	if (!bits || bits > 32 || offset + bits > report_len * 8)
		return 0;

	bit_off = offset % 8;
	byte_off = offset / 8;

	while (remaining) {
		byte = report[byte_off] >> bit_off;
		avail = 8 - bit_off;
		take = min(avail, remaining);
		byte &= 0xff >> (8 - take);
		value |= (u32)byte << (bits - remaining);
		bit_off = 0;
		byte_off++;
		remaining -= take;
	}

	return value;
}

static bool tcm_known_field(u8 code)
{
	switch (code) {
	case TOUCH_TIMESTAMP:
	case TOUCH_0D_BUTTONS_STATE:
	case TOUCH_GESTURE_DOUBLE_TAP:
	case TOUCH_FRAME_RATE:
	case TOUCH_POWER_IM:
	case TOUCH_CID_IM:
	case TOUCH_RAIL_IM:
	case TOUCH_CID_VARIANCE_IM:
	case TOUCH_NSM_FREQUENCY:
	case TOUCH_NSM_STATE:
	case TOUCH_NUM_OF_CPU_CYCLES:
	case TOUCH_TUNING_GAUSSIAN_WIDTHS:
	case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
	case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
		return true;
	default:
		return false;
	}
}

/*
 * Walk the firmware-provided report layout and fill ts->obj[].  The layout
 * is a small byte-code program: foreach loops bracket per-object fields and
 * every field starts with its bit width.
 */
static void tcm_parse_report(struct tcm_ts *ts, const u8 *report, u32 len)
{
	const u8 *cfg = ts->config;
	u32 cfg_len = ts->config_len;
	u32 idx = 0, offset = 0, obj = 0, next = 0, bits, data;
	u32 active_objects = 0, objects = 0, budget;
	bool active_only = false, have_active_count = false;

	memset(ts->obj, 0, sizeof(ts->obj));
	ts->esd_frame = false;

	/* hard bound: a malformed layout must not spin here */
	budget = cfg_len * (TCM_MAX_OBJECTS + 2) + 64;

	while (idx < cfg_len) {
		u8 code = cfg[idx++];

		if (!budget--)
			return;

		switch (code) {
		case TOUCH_END:
			return;
		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;
		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;
		case TOUCH_FOREACH_END:
			ts->foreach_end = idx;
			if (active_only) {
				if (have_active_count) {
					if (++objects < active_objects &&
					    objects < ts->max_objects)
						idx = next;
				} else if (offset < len * 8) {
					idx = next;
					if (++objects >= TCM_MAX_OBJECTS)
						return;
				}
			} else if (++obj < ts->max_objects) {
				idx = next;
			}
			break;
		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ALIGN(offset, 8);
			break;
		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			if (idx >= cfg_len)
				return;
			bits = cfg[idx++];
			active_objects = tcm_field(report, len, offset, bits);
			have_active_count = true;
			offset += bits;
			if (active_objects > ts->max_objects)
				active_objects = ts->max_objects;
			if (!active_objects) {
				/*
				 * No finger: skip the per-object block.  The
				 * end index is learned from an earlier frame
				 * because the count sits inside the loop.
				 */
				if (ts->foreach_end)
					idx = ts->foreach_end;
				else
					return;
			}
			break;
		case TOUCH_ROI_DATA:
			if (idx + 2 > cfg_len)
				return;
			bits = cfg[idx] | (cfg[idx + 1] << 8);
			idx += 2;
			if ((offset & 7) || (bits & 7))
				return;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_INDEX:
			if (idx >= cfg_len)
				return;
			bits = cfg[idx++];
			obj = tcm_field(report, len, offset, bits);
			offset += bits;
			if (obj >= ts->max_objects)
				return;
			break;
		case TOUCH_ESD_DETECT:
			if (idx >= cfg_len)
				return;
			bits = cfg[idx++];
			data = tcm_field(report, len, offset, bits);
			offset += bits;
			/* high byte bit0 flags an ESD frame */
			if ((data >> 8) & BIT(0))
				ts->esd_frame = true;
			break;
		case TOUCH_OBJECT_N_CLASSIFICATION:
		case TOUCH_OBJECT_N_X_POSITION:
		case TOUCH_OBJECT_N_Y_POSITION:
		case TOUCH_OBJECT_N_Z:
		case TOUCH_OBJECT_N_X_WIDTH:
		case TOUCH_OBJECT_N_Y_WIDTH:
		case TOUCH_OBJECT_N_TX_POSITION:
		case TOUCH_OBJECT_N_RX_POSITION:
		case TOUCH_GRIP_DATA:
			if (idx >= cfg_len)
				return;
			bits = cfg[idx++];
			data = tcm_field(report, len, offset, bits);
			offset += bits;
			if (obj >= ts->max_objects)
				break;
			switch (code) {
			case TOUCH_OBJECT_N_CLASSIFICATION:
				ts->obj[obj].status = data;
				break;
			case TOUCH_OBJECT_N_X_POSITION:
				ts->obj[obj].x = data;
				break;
			case TOUCH_OBJECT_N_Y_POSITION:
				ts->obj[obj].y = data;
				break;
			case TOUCH_OBJECT_N_Z:
				ts->obj[obj].z = data;
				break;
			case TOUCH_OBJECT_N_X_WIDTH:
				ts->obj[obj].x_width = data;
				break;
			case TOUCH_OBJECT_N_Y_WIDTH:
				ts->obj[obj].y_width = data;
				break;
			case TOUCH_GRIP_DATA:
				ts->obj[obj].grip = data;
				break;
			default:
				break;
			}
			break;
		default:
			if (!tcm_known_field(code))
				return;	/* unknown opcode: stop, stay aligned */
			if (idx >= cfg_len)
				return;
			bits = cfg[idx++];
			offset += bits;
			break;
		}
	}
}

static void tcm_release_all(struct tcm_ts *ts)
{
	struct input_dev *input = ts->input;
	int i;

	for (i = 0; i < ts->max_objects; i++) {
		if (!ts->prev_status[i])
			continue;
		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
		ts->prev_status[i] = false;
	}

	input_mt_sync_frame(input);
	input_sync(input);
}

static void tcm_report_touch(struct tcm_ts *ts)
{
	struct input_dev *input = ts->input;
	int i;

	/* a zero length frame means "all fingers up" */
	if (!ts->report_len) {
		tcm_release_all(ts);
		return;
	}

	tcm_parse_report(ts, ts->msg, ts->report_len);

	if (ts->esd_frame) {
		tcm_release_all(ts);
		return;
	}

	for (i = 0; i < ts->max_objects; i++) {
		struct tcm_object *o = &ts->obj[i];
		bool active = o->status == TOUCH_FINGER ||
			      o->status == TOUCH_GLOVED_FINGER;
		int x = 0, y = 0;

		if (active) {
			x = min_t(u32, o->x, ts->max_x);
			y = min_t(u32, o->y, ts->max_y);
			if (ts->inverted_x)
				x = ts->max_x - x;
			if (ts->inverted_y)
				y = ts->max_y - y;
		}

		if (!active && !ts->prev_status[i])
			continue;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, active);

		if (!active) {
			ts->prev_status[i] = false;
			continue;
		}

		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_abs(input, ABS_MT_PRESSURE,
				 min_t(u32, o->z, 0xffff));
		input_report_abs(input, ABS_MT_TOUCH_MAJOR,
				 min_t(u32, o->x_width, 0xffff));
		input_report_abs(input, ABS_MT_TOUCH_MINOR,
				 min_t(u32, o->y_width, 0xffff));
		ts->prev_status[i] = true;
	}

	input_mt_sync_frame(input);
	input_sync(input);
}

static irqreturn_t tcm_irq(int irq, void *dev_id)
{
	struct tcm_ts *ts = dev_id;
	u8 code = 0;
	u32 len = 0;
	int ret;

	mutex_lock(&ts->lock);
	ret = tcm_read_msg(ts, &code, ts->msg, &len);
	if (ret) {
		if (++ts->irq_errors >= 3) {
			dev_err_ratelimited(&ts->spi->dev,
					    "TCM read keeps failing, masking IRQ\n");
			disable_irq_nosync(irq);
		}
	} else {
		ts->irq_errors = 0;
		if (code == REPORT_TOUCH) {
			ts->report_len = len;
			tcm_report_touch(ts);
		}
	}
	mutex_unlock(&ts->lock);

	return IRQ_HANDLED;
}

/* ---- probe / remove / PM --------------------------------------------- */

static int tcm_parse_dt(struct tcm_ts *ts)
{
	struct device_node *np = ts->spi->dev.of_node;
	struct device *dev = &ts->spi->dev;
	const char *fw_name;
	u32 val = 0;

	ts->reset_active_ms = 20;
	ts->reset_delay_ms = 200;
	ts->max_x = 1080;
	ts->max_y = 2340;
	ts->max_objects = TCM_MAX_OBJECTS;
	ts->dt_max_objects = 0;

	of_property_read_u32(np, "syna,reset-active-ms", &ts->reset_active_ms);
	of_property_read_u32(np, "syna,reset-delay-ms", &ts->reset_delay_ms);

	if (!of_property_read_u32(np, "touchscreen-size-x", &val) && val)
		ts->max_x = min_t(u32, val, 0xffff);
	val = 0;
	if (!of_property_read_u32(np, "touchscreen-size-y", &val) && val)
		ts->max_y = min_t(u32, val, 0xffff);
	val = 0;
	if (!of_property_read_u32(np, "syna,max-objects", &val) && val)
		ts->dt_max_objects = min_t(u32, val, TCM_MAX_OBJECTS);

	ts->swapped_x_y = of_property_read_bool(np, "touchscreen-swapped-x-y");
	ts->inverted_x = of_property_read_bool(np, "touchscreen-inverted-x");
	ts->inverted_y = of_property_read_bool(np, "touchscreen-inverted-y");

	if (of_property_read_string(np, "syna,firmware-name", &fw_name)) {
		dev_err(dev, "missing syna,firmware-name\n");
		return -EINVAL;
	}
	ts->fw_name = devm_kstrdup(dev, fw_name, GFP_KERNEL);
	if (!ts->fw_name)
		return -ENOMEM;

	return 0;
}

static int tcm_probe(struct spi_device *spi)
{
	struct tcm_ts *ts;
	int ret;

	ts = devm_kzalloc(&spi->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->spi = spi;
	mutex_init(&ts->lock);
	spi_set_drvdata(spi, ts);

	ts->rx = devm_kzalloc(&spi->dev, TCM_READ_LENGTH, GFP_KERNEL);
	ts->tx_dummy = devm_kzalloc(&spi->dev, TCM_READ_LENGTH, GFP_KERNEL);
	ts->msg = devm_kzalloc(&spi->dev, TCM_READ_LENGTH, GFP_KERNEL);
	if (!ts->rx || !ts->tx_dummy || !ts->msg)
		return -ENOMEM;

	ret = tcm_parse_dt(ts);
	if (ret)
		return ret;

	/* start with the chip held in reset */
	ts->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(ts->reset_gpio),
				     "failed to get reset gpio\n");

	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = TCM_DEFAULT_SPI_HZ;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	if (spi->irq <= 0)
		return dev_err_probe(&spi->dev, -EINVAL,
				     "no interrupt in device tree\n");

	ret = tcm_init_hw(ts);
	if (ret) {
		/*
		 * The controller is powered by the in-cell panel and the
		 * firmware may not be on the rootfs yet; retry later.
		 */
		dev_info(&spi->dev, "TCM not ready (%d), deferring\n", ret);
		return -EPROBE_DEFER;
	}

	ts->input = devm_input_allocate_device(&spi->dev);
	if (!ts->input)
		return -ENOMEM;

	ts->input->name = "Synaptics TCM TouchScreen";
	ts->input->phys = "input/ts";
	ts->input->id.bustype = BUS_SPI;
	__set_bit(INPUT_PROP_DIRECT, ts->input->propbit);

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 0xffff, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 0xffff, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 0xffff, 0, 0);

	ret = input_mt_init_slots(ts->input, ts->max_objects,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;
	ts->slot_count = ts->max_objects;

	ret = input_register_device(ts->input);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, tcm_irq,
					IRQF_ONESHOT, DRIVER_NAME, ts);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "failed to request irq\n");

	/*
	 * Only now start the report stream: the input device and its IRQ are
	 * in place, so the first frame cannot be lost in a chip FIFO.
	 */
	ret = tcm_set_report(ts, true);
	if (ret) {
		dev_err(&spi->dev, "failed to enable touch reporting: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static void tcm_remove(struct spi_device *spi)
{
	struct tcm_ts *ts = spi_get_drvdata(spi);

	if (!ts)
		return;

	if (!ts->suspended)
		disable_irq(ts->spi->irq);
	tcm_set_report(ts, false);
	gpiod_set_value_cansleep(ts->reset_gpio, 1);
}

static int tcm_suspend(struct device *dev)
{
	struct tcm_ts *ts = dev_get_drvdata(dev);

	if (!ts || ts->suspended)
		return 0;

	disable_irq(ts->spi->irq);
	ts->suspended = true;

	return 0;
}

static int tcm_resume(struct device *dev)
{
	struct tcm_ts *ts = dev_get_drvdata(dev);
	int ret;

	if (!ts || !ts->suspended)
		return 0;

	ret = tcm_init_hw(ts);
	if (ret) {
		dev_err(dev, "failed to restore TCM: %d\n", ret);
		return ret;
	}

	enable_irq(ts->spi->irq);
	ret = tcm_set_report(ts, true);
	if (ret) {
		dev_err(dev, "failed to re-enable reporting: %d\n", ret);
		return ret;
	}
	ts->suspended = false;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tcm_pm_ops, tcm_suspend, tcm_resume);

static const struct of_device_id tcm_of_match[] = {
	{ .compatible = "syna,tcm-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcm_of_match);

static const struct spi_device_id tcm_spi_id[] = {
	{ "tcm-spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, tcm_spi_id);

static struct spi_driver tcm_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = tcm_of_match,
		.pm = pm_sleep_ptr(&tcm_pm_ops),
	},
	.id_table = tcm_spi_id,
	.probe = tcm_probe,
	.remove = tcm_remove,
};
module_spi_driver(tcm_spi_driver);

MODULE_AUTHOR("Ported for Kirin 970 COR-AL00");
MODULE_DESCRIPTION("Synaptics TCM SPI touchscreen (touch-only port)");
MODULE_LICENSE("GPL");
