// SPDX-License-Identifier: GPL-2.0-only
/*
 * BOE FT8719 1080x2340 MIPI DSI panel driver (Honor Play / COR-AL00 variant)
 * Init sequence from Android lcdkit cor_vn2_boe_ft8719 dtsi
 *
 * The compatible is "hisi,ft8719" rather than "boe,ft8719" for symmetry with
 * the TD4320: upstream already has drivers/gpu/drm/panel/panel-ebbg-ft8719.c
 * bound to "ebbg,ft8719", and keeping our sibling variant in the same
 * "hisi," namespace makes it obvious which driver carries the lcdkit
 * sequence from this device tree.
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <video/mipi_display.h>

/*
 * Power/reset delays, in milliseconds, exactly as the COR-AL00 ships them
 * (hw,lcdkit-delay-af-* and hw,lcdkit-lcd-rst-* in
 * cor_boe_ft8719_6p3_1080p_video.dtsi). Kept in the driver rather than
 * exposed as DT properties, the same way every other mainline panel driver
 * handles its power sequence.
 */
#define BOE_FT8719_PREPARE_IOVCC_MS	11	/* hw,lcdkit-delay-af-iovcc-on */
#define BOE_FT8719_PREPARE_VSP_MS	10	/* hw,lcdkit-delay-af-vsp-on */
#define BOE_FT8719_PREPARE_VSN_MS	12	/* hw,lcdkit-delay-af-vsn-on */
#define BOE_FT8719_RESET_HIGH1_MS	6	/* hw,lcdkit-lcd-rst-first-high */
#define BOE_FT8719_RESET_LOW_MS		1	/* hw,lcdkit-lcd-rst-low */
#define BOE_FT8719_RESET_HIGH2_MS	15	/* hw,lcdkit-lcd-rst-second-high */
#define BOE_FT8719_DISPLAY_OFF_MS	20	/* wait after 0x28 */
#define BOE_FT8719_SLEEP_IN_MS		50	/* wait after 0x10 */
#define BOE_FT8719_UNPREPARE_VSN_MS	5	/* hw,lcdkit-delay-af-vsn-off */
#define BOE_FT8719_UNPREPARE_VSP_MS	5	/* hw,lcdkit-delay-af-vsp-off */
#define BOE_FT8719_UNPREPARE_IOVCC_MS	10	/* hw,lcdkit-delay-af-iovcc-off */
#define BOE_FT8719_RESET_OFF_MS		5	/* hw,lcdkit-delay-af-rst-off */

/*
 * ESD policy, taken from Android's drivers/video/fbdev/hisi/dss/hisi_fb_esd.c
 * so the mainline driver reacts on the same schedule as the vendor one:
 * a check every ESD_CHECK_TIME_PERIOD ms, recovery after
 * HISI_ESD_CHECK_MAX_COUNT consecutive bad checks, and the checker disables
 * itself after hw,lcdkit-esd-recovery-max-count (default 10) recoveries.
 */
#define BOE_FT8719_ESD_PERIOD_MS	5000
#define BOE_FT8719_ESD_MAX_FAILURES	3
#define BOE_FT8719_ESD_MAX_RECOVER	10
#define BOE_FT8719_ESD_POWER_OFF_MS	100

/* One entry of Android's enum esd_judge_type (lcdkit_panel.h). */
enum boe_esd_judge {
	BOE_ESD_UNEQUAL,
	BOE_ESD_EQUAL,
	BOE_ESD_BIT_VALID,
};

/*
 * hw,lcdkit-panel-esd-reg-command / hw,lcdkit-panel-esd-value from
 * cor_boe_ft8719_6p3_1080p_video.dtsi: five registers, all ESD_UNEQUAL
 * (judge type 0 in the low byte of each esd-value word).
 */
static const struct {
	u8 reg;
	u8 judge;
	u8 expect;
} boe_ft8719_esd_regs[] = {
	{ 0x0a, BOE_ESD_UNEQUAL, 0x9c },
	{ 0x0b, BOE_ESD_UNEQUAL, 0x00 },
	{ 0x0d, BOE_ESD_UNEQUAL, 0x00 },
	{ 0x0e, BOE_ESD_UNEQUAL, 0x00 },
	{ 0xac, BOE_ESD_UNEQUAL, 0x20 },
};

struct boe_ft8719 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *iovcc;
	struct regulator *vsp;
	struct regulator *vsn;
	struct gpio_desc *reset_gpio;
	/*
	 * Serialises the four panel callbacks against the ESD worker. Taken
	 * by every __boe_ft8719_* helper and by the worker; it must never be
	 * held across cancel_delayed_work_sync().
	 */
	struct mutex lock;
	struct delayed_work esd_work;
	unsigned int esd_fail_count;
	unsigned int esd_recover_count;
	bool esd_disabled;
	bool prepared;
	bool enabled;
};

#define to_boe_ft8719(p) container_of(p, struct boe_ft8719, panel)

/*
 * Panel timing from the Android lcdkit node
 * (hw,lcdkit-h-*-porch / h-*-pulse-width, hw,lcdkit-panel-pxl-clk = <192> MHz):
 *
 *   hbp 16, hfp 170, hpw 6   -> htotal = 1080 + 16 + 170 + 6  = 1272
 *   vbp 54, vfp 112, vpw 9   -> vtotal = 2340 + 54 + 112 + 9   = 2515
 *
 * 1272 * 2515 * 60 = 191944 kHz, rounded up to 192 MHz.
 * Panel diagonal is 6.3", active area 67 x 145 mm.
 *
 * DRM's porches are hsync_start = hdisplay + hfp, hsync_end = hsync_start + hpw,
 * htotal = hsync_end + hbp, so hfp (170) lands before the sync pulse and hbp
 * (16) after it - not the other way round.
 */
static const struct drm_display_mode boe_ft8719_mode = {
	.clock = 192000,
	.hdisplay = 1080,
	.hsync_start = 1250,
	.hsync_end = 1256,
	.htotal = 1272,
	.vdisplay = 2340,
	.vsync_start = 2452,
	.vsync_end = 2461,
	.vtotal = 2515,
	.width_mm = 67,
	.height_mm = 145,
	.flags = 0,
	.name = "1080x2340@60",
};

/*
 * Panel init sequence decoded from the Android lcdkit dtsi
 * (cor_boe_ft8719_6p3_1080p_video.dtsi).
 */
static int boe_ft8719_init(struct mipi_dsi_device *dsi)
{
	int ret;

	/*
	 * Panel init sequence transcribed byte-for-byte from the Android
	 * lcdkit cor_vn2_boe_ft8719_6p3_1080p_video node
	 * (hw,lcdkit-panel-on-command), 57 commands in the exact order the panel
	 * expects. Each manufacturer register write is a generic long write
	 * (DTYPE 0x29); the brightness/CABC writes and the two DCS commands are
	 * DCS (DTYPE 0x15). All commands are sent in LP mode, which is why the
	 * low-power GEN_VID_LP_CMD bit must be set in the host transfer.
	 *
	 * The dtsi gives every command a wait of 10 with waittype 0 (i.e. 10 us)
	 * except command 53 (13 us) and command 56 (100 with waittype 1, i.e.
	 * 100 ms) which is honoured explicitly after the sleep-out below. The
	 * microsecond waits are already covered by the CMD_PKT_STATUS busy-wait
	 * in hisi_dsi_host_transfer(), so they are not repeated here.
	 */

#define BOE_LW(...) \
	mipi_dsi_generic_write(dsi, (u8[]){ __VA_ARGS__ }, \
			       sizeof((u8[]){ __VA_ARGS__ }))

#define BOE_DCS(...) \
	mipi_dsi_dcs_write_buffer(dsi, (u8[]){ __VA_ARGS__ }, \
			      sizeof((u8[]){ __VA_ARGS__ }))

	/* 1: unlock manufacturer command access (0x00, 0x00) */
	ret = BOE_LW(0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 2: 0xFF 0x87, 0x19, 0x01 */
	ret = BOE_LW(0xFF, 0x87, 0x19, 0x01);
	if (ret < 0)
		return ret;

	/* 3: select command page 0x80 */
	ret = BOE_LW(0x00, 0x80);
	if (ret < 0)
		return ret;

	/* 4: 0xFF 0x87, 0x19 */
	ret = BOE_LW(0xFF, 0x87, 0x19);
	if (ret < 0)
		return ret;

	/* 5: select command page 0xA1 */
	ret = BOE_LW(0x00, 0xA1);
	if (ret < 0)
		return ret;

	/* 6: 0xB3 0x04, 0x38, 0x09, 0x24 */
	ret = BOE_LW(0xB3, 0x04, 0x38, 0x09, 0x24);
	if (ret < 0)
		return ret;

	/* 7: select command page 0x80 */
	ret = BOE_LW(0x00, 0x80);
	if (ret < 0)
		return ret;

	/* 8: 0xC0 0x00, 0x79, 0x00, 0x6D, 0x00, 0x10 */
	ret = BOE_LW(0xC0, 0x00, 0x79, 0x00, 0x6D, 0x00, 0x10);
	if (ret < 0)
		return ret;

	/* 9: select command page 0x87 */
	ret = BOE_LW(0x00, 0x87);
	if (ret < 0)
		return ret;

	/* 10: 0xC0 0x01, 0x01 */
	ret = BOE_LW(0xC0, 0x01, 0x01);
	if (ret < 0)
		return ret;

	/* 11: select command page 0x8A */
	ret = BOE_LW(0x00, 0x8A);
	if (ret < 0)
		return ret;

	/* 12: 0xC0 0x1D, 0x08 */
	ret = BOE_LW(0xC0, 0x1D, 0x08);
	if (ret < 0)
		return ret;

	/* 13: select command page 0x90 */
	ret = BOE_LW(0x00, 0x90);
	if (ret < 0)
		return ret;

	/* 14: 0xC0 0x00, 0x79, 0x00, 0x6D, 0x00, 0x10 */
	ret = BOE_LW(0xC0, 0x00, 0x79, 0x00, 0x6D, 0x00, 0x10);
	if (ret < 0)
		return ret;

	/* 15: select command page 0xD7 */
	ret = BOE_LW(0x00, 0xD7);
	if (ret < 0)
		return ret;

	/* 16: 0xC0 0x00, 0x74, 0x00, 0x71, 0x00, 0x10 */
	ret = BOE_LW(0xC0, 0x00, 0x74, 0x00, 0x71, 0x00, 0x10);
	if (ret < 0)
		return ret;

	/* 17: select command page 0xA0 */
	ret = BOE_LW(0x00, 0xA0);
	if (ret < 0)
		return ret;

	/* 18: 0xC0 0x01, 0x05, 0x00, 0x3B, 0x00, 0x10 */
	ret = BOE_LW(0xC0, 0x01, 0x05, 0x00, 0x3B, 0x00, 0x10);
	if (ret < 0)
		return ret;

	/* 19: select command page 0xB0 */
	ret = BOE_LW(0x00, 0xB0);
	if (ret < 0)
		return ret;

	/* 20: 0xC0 0x00, 0x79, 0x02, 0x2A, 0x10 */
	ret = BOE_LW(0xC0, 0x00, 0x79, 0x02, 0x2A, 0x10);
	if (ret < 0)
		return ret;

	/* 21: select command page 0xC1 */
	ret = BOE_LW(0x00, 0xC1);
	if (ret < 0)
		return ret;

	/* 22: 0xC0 0x00, 0xAE, 0x00, 0x88, 0x00, 0x74, 0x00, 0xCC */
	ret = BOE_LW(0xC0, 0x00, 0xAE, 0x00, 0x88, 0x00, 0x74, 0x00, 0xCC);
	if (ret < 0)
		return ret;

	/* 23: select command page 0xA5 */
	ret = BOE_LW(0x00, 0xA5);
	if (ret < 0)
		return ret;

	/* 24: 0xC1 0x00, 0x56, 0x00, 0x02 */
	ret = BOE_LW(0xC1, 0x00, 0x56, 0x00, 0x02);
	if (ret < 0)
		return ret;

	/* 25: select command page 0x80 */
	ret = BOE_LW(0x00, 0x80);
	if (ret < 0)
		return ret;

	/*
	 * 26: 0xCE 0x01, 0x80, 0x01, 0x09, 0x00, 0xD8, 0x00, 0xD8, 0x00,
	 *     0x90, 0x00, 0x90, 0x0D, 0x0E, 0x09
	 */
	ret = BOE_LW(0xCE, 0x01, 0x80, 0x01, 0x09, 0x00, 0xD8, 0x00,
		     0xD8, 0x00, 0x90, 0x00, 0x90, 0x0D, 0x0E, 0x09);
	if (ret < 0)
		return ret;

	/* 27: select command page 0x90 */
	ret = BOE_LW(0x00, 0x90);
	if (ret < 0)
		return ret;

	/* 28: 0xCE 0x00, 0x82, 0x0D, 0x5C, 0x00, 0x82, 0x80, 0x09, 0x00, 0x04 */
	ret = BOE_LW(0xCE, 0x00, 0x82, 0x0D, 0x5C, 0x00, 0x82, 0x80, 0x09, 0x00, 0x04);
	if (ret < 0)
		return ret;

	/* 29: select command page 0xA0 */
	ret = BOE_LW(0x00, 0xA0);
	if (ret < 0)
		return ret;

	/* 30: 0xCE 0x00, 0x00, 0x00 */
	ret = BOE_LW(0xCE, 0x00, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 31: select command page 0xB0 */
	ret = BOE_LW(0x00, 0xB0);
	if (ret < 0)
		return ret;

	/* 32: 0xCE 0x11, 0x00, 0x00 */
	ret = BOE_LW(0xCE, 0x11, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 33: select command page 0xC0 */
	ret = BOE_LW(0x00, 0xC0);
	if (ret < 0)
		return ret;

	/* 34: 0xCE 0x00, 0x00, 0x00 */
	ret = BOE_LW(0xCE, 0x00, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 35: select command page 0xC0 */
	ret = BOE_LW(0x00, 0xC0);
	if (ret < 0)
		return ret;

	/* 36: 0xC5 0x51 */
	ret = BOE_LW(0xC5, 0x51);
	if (ret < 0)
		return ret;

	/* 37: select command page 0xD0 */
	ret = BOE_LW(0x00, 0xD0);
	if (ret < 0)
		return ret;

	/* 38: 0xCE 0x91, 0x00, 0x0A, 0x01, 0x01, 0x00, 0x5E, 0x01 */
	ret = BOE_LW(0xCE, 0x91, 0x00, 0x0A, 0x01, 0x01, 0x00, 0x5E, 0x01);
	if (ret < 0)
		return ret;

	/* 39: select command page 0xE0 */
	ret = BOE_LW(0x00, 0xE0);
	if (ret < 0)
		return ret;

	/* 40: 0xCE 0x88, 0x08, 0x02, 0x15, 0x02, 0x15, 0x02, 0x15, 0x00, 0x2C, 0x00, 0x62 */
	ret = BOE_LW(0xCE, 0x88, 0x08, 0x02, 0x15, 0x02, 0x15, 0x02, 0x15, 0x00, 0x2C, 0x00, 0x62);
	if (ret < 0)
		return ret;

	/* 41: select command page 0xF0 */
	ret = BOE_LW(0x00, 0xF0);
	if (ret < 0)
		return ret;

	/* 42: 0xCE 0x80, 0x17, 0x0B, 0x0F, 0x00, 0xB0, 0x01, 0x3D, 0x01, 0x63 */
	ret = BOE_LW(0xCE, 0x80, 0x17, 0x0B, 0x0F, 0x00, 0xB0, 0x01, 0x3D, 0x01, 0x63);
	if (ret < 0)
		return ret;

	/* 43: select command page 0xB0 */
	ret = BOE_LW(0x00, 0xB0);
	if (ret < 0)
		return ret;

	/* 44: 0xCF 0x00, 0x00, 0x6C, 0x70, 0x00, 0x05, 0x05, 0x35, 0x39 */
	ret = BOE_LW(0xCF, 0x00, 0x00, 0x6C, 0x70, 0x00, 0x05, 0x05, 0x35, 0x39);
	if (ret < 0)
		return ret;

	/* 45: select command page 0xC0 */
	ret = BOE_LW(0x00, 0xC0);
	if (ret < 0)
		return ret;

	/* 46: 0xCF 0x08, 0x08, 0xCA, 0xCE, 0x00, 0x00, 0x00, 0x08, 0x0C */
	ret = BOE_LW(0xCF, 0x08, 0x08, 0xCA, 0xCE, 0x00, 0x00, 0x00, 0x08, 0x0C);
	if (ret < 0)
		return ret;

	/* 47: select command page 0x80 */
	ret = BOE_LW(0x00, 0x80);
	if (ret < 0)
		return ret;

	/* 48: 0xB4 0x0A */
	ret = BOE_LW(0xB4, 0x0A);
	if (ret < 0)
		return ret;

	/* 49: select command page 0xB0 */
	ret = BOE_LW(0x00, 0xB0);
	if (ret < 0)
		return ret;

	/* 50: 0xF5 0x00 */
	ret = BOE_LW(0xF5, 0x00);
	if (ret < 0)
		return ret;

	/* 51: select command page 0x00 */
	ret = BOE_LW(0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 52: 0xFF 0x00, 0x00, 0x00 */
	ret = BOE_LW(0xFF, 0x00, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* 53: 0x51 0xFE */
	ret = BOE_DCS(0x51, 0xFE);
	if (ret < 0)
		return ret;

	/* 54: 0x53 0x24 */
	ret = BOE_DCS(0x53, 0x24);
	if (ret < 0)
		return ret;

	/* 55: 0x55 0x00 */
	ret = BOE_DCS(0x55, 0x00);
	if (ret < 0)
		return ret;

	/* 56: DCS sleep out (0x11), the dtsi then waits 100 ms */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	msleep(100);

	/* 57: DCS display on (0x29) */
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;

#undef BOE_LW
#undef BOE_DCS

	return 0;
}

static int __boe_ft8719_prepare(struct boe_ft8719 *priv)
{
	int ret;

	if (priv->prepared)
		return 0;

	/*
	 * Hold the panel in reset while the rails come up, so that the reset
	 * pulse below defines the whole startup edge. GPIO_ACTIVE_LOW means
	 * logical 1 = physical LOW = in reset, logical 0 = physical HIGH =
	 * out of reset.
	 */
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);

	if (priv->iovcc) {
		ret = regulator_enable(priv->iovcc);
		if (ret) {
			DRM_ERROR("failed to enable iovcc: %d\n", ret);
			return ret;
		}
		msleep(BOE_FT8719_PREPARE_IOVCC_MS);
	}

	if (priv->vsp) {
		ret = regulator_enable(priv->vsp);
		if (ret) {
			DRM_ERROR("failed to enable vsp: %d\n", ret);
			goto err_iovcc;
		}
		msleep(BOE_FT8719_PREPARE_VSP_MS);
	}

	if (priv->vsn) {
		ret = regulator_enable(priv->vsn);
		if (ret) {
			DRM_ERROR("failed to enable vsn: %d\n", ret);
			goto err_vsp;
		}
		msleep(BOE_FT8719_PREPARE_VSN_MS);
	}

	/*
	 * Hardware reset pulse: first-high / low / second-high, 6/1/15 ms for
	 * this glass. With GPIO_ACTIVE_LOW, logical 0 = physical HIGH (out of
	 * reset) and logical 1 = physical LOW (in reset).
	 */
	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(BOE_FT8719_RESET_HIGH1_MS);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		msleep(BOE_FT8719_RESET_LOW_MS);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(BOE_FT8719_RESET_HIGH2_MS);
	}

	priv->prepared = true;
	return 0;

err_vsp:
	if (priv->vsp)
		regulator_disable(priv->vsp);
err_iovcc:
	if (priv->iovcc)
		regulator_disable(priv->iovcc);
	return ret;
}

static int __boe_ft8719_unprepare(struct boe_ft8719 *priv)
{
	if (!priv->prepared)
		return 0;

	/* Hold the panel in reset while the rails come down. */
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	/* hw,lcdkit-delay-af-rst-off: settle in reset before cutting rails */
	msleep(BOE_FT8719_RESET_OFF_MS);

	if (priv->vsn)
		regulator_disable(priv->vsn);
	msleep(BOE_FT8719_UNPREPARE_VSN_MS);

	if (priv->vsp)
		regulator_disable(priv->vsp);
	msleep(BOE_FT8719_UNPREPARE_VSP_MS);

	if (priv->iovcc)
		regulator_disable(priv->iovcc);
	msleep(BOE_FT8719_UNPREPARE_IOVCC_MS);

	priv->prepared = false;
	return 0;
}

static int __boe_ft8719_enable(struct boe_ft8719 *priv)
{
	int ret;

	if (priv->enabled)
		return 0;

	ret = boe_ft8719_init(priv->dsi);
	if (ret)
		return ret;

	priv->enabled = true;
	return 0;
}

static int __boe_ft8719_disable(struct boe_ft8719 *priv)
{
	struct mipi_dsi_device *dsi = priv->dsi;
	int ret;

	if (!priv->enabled)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		DRM_WARN("failed to set display off: %d\n", ret);
	msleep(BOE_FT8719_DISPLAY_OFF_MS);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		DRM_WARN("failed to enter sleep mode: %d\n", ret);
	msleep(BOE_FT8719_SLEEP_IN_MS);

	priv->enabled = false;
	return 0;
}

/*
 * Read the panel's status registers and return true as soon as one of them
 * disagrees with the vendor expectation, mirroring lcd_kit_judge_esd().
 * A failed read counts as a failure too: Android's hisifb_esd_timer_func()
 * treats a non-zero return from hisifb_ctrl_esd() the same way.
 */
static bool boe_ft8719_esd_failed(struct boe_ft8719 *priv)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(boe_ft8719_esd_regs); i++) {
		const u8 reg = boe_ft8719_esd_regs[i].reg;
		const u8 judge = boe_ft8719_esd_regs[i].judge;
		const u8 expect = boe_ft8719_esd_regs[i].expect;
		bool bad;
		u8 val;
		ssize_t ret;

		ret = mipi_dsi_dcs_read(priv->dsi, reg, &val, 1);
		if (ret < 1) {
			DRM_WARN("ESD: read of reg 0x%02x failed: %zd\n",
				 reg, ret);
			return true;
		}

		switch (judge) {
		case BOE_ESD_UNEQUAL:
			bad = val != expect;
			break;
		case BOE_ESD_EQUAL:
			bad = val == expect;
			break;
		case BOE_ESD_BIT_VALID:
			bad = val & expect;
			break;
		default:
			bad = false;
			break;
		}

		if (bad) {
			DRM_WARN("ESD: reg 0x%02x = 0x%02x, expected 0x%02x\n",
				 reg, val, expect);
			return true;
		}
	}

	return false;
}

/*
 * Runs with priv->lock held, so it can safely call the __ helpers and be sure
 * DRM is not in the middle of its own sequence.
 */
static void boe_ft8719_esd_recover(struct boe_ft8719 *priv)
{
	int ret;

	__boe_ft8719_disable(priv);
	__boe_ft8719_unprepare(priv);
	msleep(BOE_FT8719_ESD_POWER_OFF_MS);

	ret = __boe_ft8719_prepare(priv);
	if (ret) {
		DRM_ERROR("ESD: panel re-init failed: %d\n", ret);
		priv->esd_disabled = true;
		return;
	}

	ret = __boe_ft8719_enable(priv);
	if (ret) {
		DRM_ERROR("ESD: panel re-init failed: %d\n", ret);
		priv->esd_disabled = true;
	}
}

static void boe_ft8719_esd_work(struct work_struct *work)
{
	struct boe_ft8719 *priv = container_of(to_delayed_work(work),
						struct boe_ft8719, esd_work);
	unsigned int period;

	mutex_lock(&priv->lock);

	if (priv->prepared && priv->enabled && !priv->esd_disabled) {
		if (boe_ft8719_esd_failed(priv)) {
			if (++priv->esd_fail_count >=
			    BOE_FT8719_ESD_MAX_FAILURES) {
				priv->esd_fail_count = 0;
				priv->esd_recover_count++;

				if (priv->esd_recover_count >=
				    BOE_FT8719_ESD_MAX_RECOVER) {
					DRM_ERROR("ESD: %u recoveries, disabling the checker\n",
						  priv->esd_recover_count);
					priv->esd_disabled = true;
				} else {
					DRM_WARN("ESD: recovering the panel (attempt %u)\n",
						 priv->esd_recover_count);
					boe_ft8719_esd_recover(priv);
				}
			}
		} else {
			priv->esd_fail_count = 0;
		}
	}

	period = priv->esd_disabled ? 0 : BOE_FT8719_ESD_PERIOD_MS;
	mutex_unlock(&priv->lock);

	if (period)
		schedule_delayed_work(&priv->esd_work,
				      msecs_to_jiffies(period));
}

static int boe_ft8719_prepare(struct drm_panel *panel)
{
	struct boe_ft8719 *priv = to_boe_ft8719(panel);
	int ret;

	mutex_lock(&priv->lock);
	ret = __boe_ft8719_prepare(priv);
	if (!ret)
		schedule_delayed_work(&priv->esd_work,
				      msecs_to_jiffies(BOE_FT8719_ESD_PERIOD_MS));
	mutex_unlock(&priv->lock);

	return ret;
}

static int boe_ft8719_unprepare(struct drm_panel *panel)
{
	struct boe_ft8719 *priv = to_boe_ft8719(panel);

	/*
	 * Must run without priv->lock: the ESD worker takes it, so waiting
	 * for the worker here while holding it would deadlock. The worker
	 * re-checks prepared under the same lock before re-arming itself, so
	 * cancelling after it has cleared the flag is enough.
	 */
	cancel_delayed_work_sync(&priv->esd_work);

	mutex_lock(&priv->lock);
	__boe_ft8719_unprepare(priv);
	mutex_unlock(&priv->lock);

	return 0;
}

static int boe_ft8719_enable(struct drm_panel *panel)
{
	struct boe_ft8719 *priv = to_boe_ft8719(panel);
	int ret;

	mutex_lock(&priv->lock);
	ret = __boe_ft8719_enable(priv);
	mutex_unlock(&priv->lock);

	return ret;
}

static int boe_ft8719_disable(struct drm_panel *panel)
{
	struct boe_ft8719 *priv = to_boe_ft8719(panel);

	mutex_lock(&priv->lock);
	__boe_ft8719_disable(priv);
	mutex_unlock(&priv->lock);

	return 0;
}

static int boe_ft8719_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &boe_ft8719_mode);
	if (!mode)
		return 0;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = boe_ft8719_mode.width_mm;
	connector->display_info.height_mm = boe_ft8719_mode.height_mm;
	connector->display_info.bpc = 8;
	/*
	 * hw,lcdkit-ldi-data-en-plr = <0x00> and hw,lcdkit-ldi-pixel-clk-plr =
	 * <0x00>: the LDI drives DE active high and samples pixel data on the
	 * rising edge of PIXCLK, so no DRM bus flags are needed.
	 */

	return 1;
}

static const struct drm_panel_funcs boe_ft8719_panel_funcs = {
	.prepare = boe_ft8719_prepare,
	.unprepare = boe_ft8719_unprepare,
	.enable = boe_ft8719_enable,
	.disable = boe_ft8719_disable,
	.get_modes = boe_ft8719_get_modes,
};

static int boe_ft8719_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_ft8719 *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct boe_ft8719, panel,
				    &boe_ft8719_panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return dev_err_probe(dev, PTR_ERR(priv),
				     "failed to allocate panel\n");

	/*
	 * hw,lcdkit-mipi-lane-nums = <0x03>: three data lanes (plus the clock
	 * lane), not four. hw,lcdkit-mipi-burst-mode = <0x02> is
	 * DSI_BURST_SYNC_PULSES_1 (enum value 2). SYNC_PULSE is set so the
	 * bridge's flag-to-VID_MODE_CFG mapping yields value 2, not 3.
	 * hw,lcdkit-ldi-data-en-plr and hw,lcdkit-ldi-pixel-clk-plr are both 0,
	 * so hsync/vsync/DE/pixel clock are all active high and the connector
	 * carries no DE_LOW or PIXDATA_NEGEDGE flag.
	 */
	priv->dsi = dsi;
	mutex_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->esd_work, boe_ft8719_esd_work);
	dsi->lanes = 3;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	/*
	 * GPIOD_OUT_HIGH = logical 1 = physical LOW with GPIO_ACTIVE_LOW, so
	 * the line boots held in reset until prepare() runs.
	 */
	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "failed to get reset gpio\n");

	priv->iovcc = devm_regulator_get_optional(dev, "iovcc");
	if (IS_ERR(priv->iovcc)) {
		if (PTR_ERR(priv->iovcc) == -ENODEV)
			priv->iovcc = NULL;
		else
			return dev_err_probe(dev, PTR_ERR(priv->iovcc),
					     "failed to get iovcc\n");
	}

	priv->vsp = devm_regulator_get_optional(dev, "vsp");
	if (IS_ERR(priv->vsp)) {
		if (PTR_ERR(priv->vsp) == -ENODEV)
			priv->vsp = NULL;
		else
			return dev_err_probe(dev, PTR_ERR(priv->vsp),
					     "failed to get vsp\n");
	}

	priv->vsn = devm_regulator_get_optional(dev, "vsn");
	if (IS_ERR(priv->vsn)) {
		if (PTR_ERR(priv->vsn) == -ENODEV)
			priv->vsn = NULL;
		else
			return dev_err_probe(dev, PTR_ERR(priv->vsn),
					     "failed to get vsn\n");
	}

	/*
	 * Pick up the backlight the panel node's "backlight" phandle points
	 * at.  -ENODEV means the board does not define one, which is fine:
	 * the panel still probes, it just has no brightness control.
	 */
	ret = drm_panel_of_backlight(&priv->panel);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	drm_panel_add(&priv->panel);

	mipi_dsi_set_drvdata(dsi, priv);

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		DRM_ERROR("failed to attach DSI\n");
		drm_panel_remove(&priv->panel);
		return ret;
	}

	DRM_INFO("BOE FT8719 1080x2340 panel probed\n");
	return 0;
}

static void boe_ft8719_remove(struct mipi_dsi_device *dsi)
{
	struct boe_ft8719 *priv = mipi_dsi_get_drvdata(dsi);

	cancel_delayed_work_sync(&priv->esd_work);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&priv->panel);
}

static const struct of_device_id hisi_ft8719_of_match[] = {
	{ .compatible = "hisi,ft8719" },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, hisi_ft8719_of_match);

static struct mipi_dsi_driver hisi_ft8719_driver = {
	.probe = boe_ft8719_probe,
	.remove = boe_ft8719_remove,
	.driver = {
		.name = "hisi-ft8719",
		.of_match_table = hisi_ft8719_of_match,
	},
};
module_mipi_dsi_driver(hisi_ft8719_driver);

MODULE_AUTHOR("HiSilicon Limited");
MODULE_DESCRIPTION("BOE FT8719 1080x2340 MIPI DSI panel driver (Honor Play variant)");
MODULE_LICENSE("GPL v2");
