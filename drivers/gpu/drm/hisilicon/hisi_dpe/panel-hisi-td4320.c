// SPDX-License-Identifier: GPL-2.0-only
/*
 * BOE TD4320 1080x2340 MIPI DSI panel driver (Honor Play / COR-AL00)
 * Init sequence from Android lcdkit cor_vn2_boe_td4320_6p3_1080p_video dtsi
 *
 * The compatible is "hisi,td4320", not "boe,td4320": the latter is already
 * taken upstream by drivers/gpu/drm/panel/panel-boe-td4320.c, which targets
 * the Xiaomi Redmi Note 7 and configures the same glass with 4 data lanes,
 * MIPI_DSI_MODE_VIDEO_BURST and its own init sequence. The COR-AL00 runs
 * this panel with 3 data lanes (hw,lcdkit-mipi-lane-nums = <0x03>), sync
 * pulses and the lcdkit sequence below, so the two must not be allowed to
 * bind to the same node.
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
 * cor_boe_td4320_6p3_1080p_video.dtsi). Kept in the driver rather than
 * exposed as DT properties, the same way every other mainline panel driver
 * handles its power sequence.
 */
#define BOE_TD4320_PREPARE_IOVCC_MS	4	/* hw,lcdkit-delay-af-iovcc-on */
#define BOE_TD4320_PREPARE_VSP_MS	2	/* hw,lcdkit-delay-af-vsp-on */
#define BOE_TD4320_PREPARE_VSN_MS	2	/* hw,lcdkit-delay-af-vsn-on */
#define BOE_TD4320_RESET_HIGH1_MS	0	/* hw,lcdkit-lcd-rst-first-high */
#define BOE_TD4320_RESET_LOW_MS		0	/* hw,lcdkit-lcd-rst-low */
#define BOE_TD4320_RESET_HIGH2_MS	22	/* hw,lcdkit-lcd-rst-second-high */
#define BOE_TD4320_DISPLAY_OFF_MS	20	/* wait after 0x28 */
#define BOE_TD4320_SLEEP_IN_MS		50	/* wait after 0x10 */
#define BOE_TD4320_UNPREPARE_VSN_MS	5	/* hw,lcdkit-delay-af-vsn-off */
#define BOE_TD4320_UNPREPARE_VSP_MS	5	/* hw,lcdkit-delay-af-vsp-off */
#define BOE_TD4320_UNPREPARE_IOVCC_MS	10	/* hw,lcdkit-delay-af-iovcc-off */
#define BOE_TD4320_RESET_OFF_MS		5	/* hw,lcdkit-delay-af-rst-off */

/*
 * ESD policy, taken from Android's drivers/video/fbdev/hisi/dss/hisi_fb_esd.c
 * so the mainline driver reacts on the same schedule as the vendor one:
 *
 *   ESD_CHECK_TIME_PERIOD     5000 ms between checks
 *   HISI_ESD_CHECK_MAX_COUNT  3 consecutive bad checks before recovering
 *   hw,lcdkit-esd-recovery-max-count (default 10) recoveries before the
 *                             checker gives up and disables itself
 *   HISI_ESD_POWER_OFF_TIME   100 ms between panel off and panel on
 */
#define BOE_TD4320_ESD_PERIOD_MS	5000
#define BOE_TD4320_ESD_MAX_FAILURES	3
#define BOE_TD4320_ESD_MAX_RECOVER	10
#define BOE_TD4320_ESD_POWER_OFF_MS	100

/* One entry of Android's enum esd_judge_type (lcdkit_panel.h). */
enum boe_esd_judge {
	BOE_ESD_UNEQUAL,
	BOE_ESD_EQUAL,
	BOE_ESD_BIT_VALID,
};

/*
 * hw,lcdkit-panel-esd-reg-command / hw,lcdkit-panel-esd-value from
 * cor_boe_td4320_6p3_1080p_video.dtsi. Each esd-value word packs the judge
 * type into bits 15:8 and the expected byte into bits 7:0, which is what
 * lcd_kit_judge_esd() unpacks.
 */
static const struct {
	u8 reg;
	u8 judge;
	u8 expect;
} boe_td4320_esd_regs[] = {
	{ 0x0a, BOE_ESD_UNEQUAL, 0x1c },
	{ 0x0e, BOE_ESD_UNEQUAL, 0x00 },
	{ 0xda, BOE_ESD_EQUAL,   0x00 },
};

struct boe_td4320 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *iovcc;
	struct regulator *vsp;
	struct regulator *vsn;
	struct gpio_desc *reset_gpio;
	/*
	 * Serialises the four panel callbacks against the ESD worker, which
	 * runs the whole off/on recovery as one uninterruptible step. The
	 * worker takes it; so does every __boe_td4320_* helper. It must never
	 * be held across cancel_delayed_work_sync().
	 */
	struct mutex lock;
	struct delayed_work esd_work;
	unsigned int esd_fail_count;
	unsigned int esd_recover_count;
	bool esd_disabled;
	bool prepared;
	bool enabled;
};

#define to_boe_td4320(p) container_of(p, struct boe_td4320, panel)

/*
 * Panel timing from the Android lcdkit node
 * (hw,lcdkit-h-*-porch / h-*-pulse-width, hw,lcdkit-panel-pxl-clk = <192> MHz):
 *
 *   hbp 36, hfp 196, hpw 16  -> htotal = 1080 + 36 + 196 + 16 = 1328
 *   vbp 60, vfp 4,  vpw 4   -> vtotal = 2340 + 60 + 4   + 4  = 2408
 *
 * 1328 * 2408 = 3197824 dots/frame, so the dtsi's hw,lcdkit-panel-pxl-clk =
 * <192> MHz gives 192000 / 3197.824 = 60.04 Hz - the nominal 60 Hz refresh
 * rate. Panel diagonal is 6.3", active area 67 x 145 mm (the same glass as
 * the FT8719 variant).
 *
 * Note the TD4320's frame total is 1328 x 2408 = 3197824 dots, versus the
 * FT8719's 1272 x 2515 = 3199180 - a 0.04 % difference, which is why the
 * same 192 MHz pixel clock serves both variants at 60 Hz.
 *
 * DRM's porches are hsync_start = hdisplay + hfp, hsync_end = hsync_start + hpw,
 * htotal = hsync_end + hbp, so hfp (196) lands before the sync pulse and hbp
 * (36) after it - not the other way round.
 */
static const struct drm_display_mode boe_td4320_mode = {
	.clock = 192000,
	.hdisplay = 1080,
	.hsync_start = 1276,
	.hsync_end = 1292,
	.htotal = 1328,
	.vdisplay = 2340,
	.vsync_start = 2344,
	.vsync_end = 2348,
	.vtotal = 2408,
	.width_mm = 67,
	.height_mm = 145,
	.flags = 0,
	.name = "1080x2340@60",
};

/*
 * Panel init sequence decoded from the Android lcdkit dtsi
 * (cor_vn2_boe_td4320_6p3_1080p_video.dtsi).
 *
 * The dtsi encodes each command as struct lcdkit_dsi_cmd_desc
 * (lcdkit_panel.h:270): dtype, last, vc, ack, wait, waittype, dlen - seven
 * words - followed by dlen payload words. That layout is what makes the byte
 * stream parse cleanly: the 82 words of hw,lcdkit-panel-on-command walk out
 * as exactly 8 commands with nothing left over.
 *
 * Unlike the FT8719 (57 register writes), this panel is pre-configured at the
 * factory: it needs only its runtime register page writes, an exit-sleep and a
 * display-on. All the generic long writes go out in LP mode, so the low-power
 * GEN_VID_LP_CMD bit must be set in the host transfer.
 */
static int boe_td4320_init(struct mipi_dsi_device *dsi)
{
	int ret;

#define TD_LW(...) \
	mipi_dsi_generic_write(dsi, (u8[]){ __VA_ARGS__ }, \
			       sizeof((u8[]){ __VA_ARGS__ }))

	/* 1: DCS sleep out (0x11); the dtsi then waits 80 ms */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	msleep(80);

	/* 2: select command page 0x04 */
	ret = TD_LW(0xB0, 0x04);
	if (ret < 0)
		return ret;

	/* 3: 0xB6 0x30, 0x6B, 0x00, 0x82, 0x05 */
	ret = TD_LW(0xB6, 0x30, 0x6B, 0x00, 0x82, 0x05);
	if (ret < 0)
		return ret;

	/* 4: 0xF0 0xE5, 0x03, 0x02 */
	ret = TD_LW(0xF0, 0xE5, 0x03, 0x02);
	if (ret < 0)
		return ret;

	/* 5: 0xF0 0xE4, 0x09, 0x02 */
	ret = TD_LW(0xF0, 0xE4, 0x09, 0x02);
	if (ret < 0)
		return ret;

	/* 6: 0xF0 0xE5, 0x03, 0x00 */
	ret = TD_LW(0xF0, 0xE5, 0x03, 0x00);
	if (ret < 0)
		return ret;

	/* 7: select command page 0x03 */
	ret = TD_LW(0xB0, 0x03);
	if (ret < 0)
		return ret;

	/* 8: DCS display on (0x29); the dtsi then waits 20 ms */
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;
	msleep(20);

#undef TD_LW

	return 0;
}

static int __boe_td4320_prepare(struct boe_td4320 *priv)
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
		msleep(BOE_TD4320_PREPARE_IOVCC_MS);
	}

	if (priv->vsp) {
		ret = regulator_enable(priv->vsp);
		if (ret) {
			DRM_ERROR("failed to enable vsp: %d\n", ret);
			goto err_vddio;
		}
		msleep(BOE_TD4320_PREPARE_VSP_MS);
	}

	if (priv->vsn) {
		ret = regulator_enable(priv->vsn);
		if (ret) {
			DRM_ERROR("failed to enable vsn: %d\n", ret);
			goto err_vsp;
		}
		msleep(BOE_TD4320_PREPARE_VSN_MS);
	}

	/*
	 * Hardware reset pulse: first-high / low / second-high, which for this
	 * glass are 0/0/22 ms - the line just has to be held out of reset for
	 * 22 ms after the rails are stable, with no negative pulse at all. With
	 * GPIO_ACTIVE_LOW the pin's *physical* high is logical 0, so the lcdkit
	 * high/low/high sequence becomes logical 0 / 1 / 0 and the line is
	 * left out of reset.
	 */
	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(BOE_TD4320_RESET_HIGH1_MS);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		msleep(BOE_TD4320_RESET_LOW_MS);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(BOE_TD4320_RESET_HIGH2_MS);
	}

	priv->prepared = true;
	return 0;

err_vsp:
	if (priv->vsp)
		regulator_disable(priv->vsp);
err_vddio:
	if (priv->iovcc)
		regulator_disable(priv->iovcc);
	return ret;
}

static int __boe_td4320_unprepare(struct boe_td4320 *priv)
{
	if (!priv->prepared)
		return 0;

	/* Hold the panel in reset while the rails come down. */
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	/* hw,lcdkit-delay-af-rst-off: settle in reset before cutting rails */
	msleep(BOE_TD4320_RESET_OFF_MS);

	if (priv->vsn)
		regulator_disable(priv->vsn);
	msleep(BOE_TD4320_UNPREPARE_VSN_MS);

	if (priv->vsp)
		regulator_disable(priv->vsp);
	msleep(BOE_TD4320_UNPREPARE_VSP_MS);

	if (priv->iovcc)
		regulator_disable(priv->iovcc);
	msleep(BOE_TD4320_UNPREPARE_IOVCC_MS);

	priv->prepared = false;
	return 0;
}

static int __boe_td4320_enable(struct boe_td4320 *priv)
{
	int ret;

	if (priv->enabled)
		return 0;

	ret = boe_td4320_init(priv->dsi);
	if (ret)
		return ret;

	priv->enabled = true;
	return 0;
}

static int __boe_td4320_disable(struct boe_td4320 *priv)
{
	struct mipi_dsi_device *dsi = priv->dsi;
	int ret;

	if (!priv->enabled)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		DRM_WARN("failed to set display off: %d\n", ret);
	msleep(BOE_TD4320_DISPLAY_OFF_MS);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		DRM_WARN("failed to enter sleep mode: %d\n", ret);
	msleep(BOE_TD4320_SLEEP_IN_MS);

	priv->enabled = false;
	return 0;
}

/*
 * Read the panel's status registers and return true as soon as one of them
 * disagrees with the vendor expectation, mirroring lcd_kit_judge_esd().
 * A failed read counts as a failure too: Android's hisifb_esd_timer_func()
 * treats a non-zero return from hisifb_ctrl_esd() the same way.
 */
static bool boe_td4320_esd_failed(struct boe_td4320 *priv)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(boe_td4320_esd_regs); i++) {
		const u8 reg = boe_td4320_esd_regs[i].reg;
		const u8 judge = boe_td4320_esd_regs[i].judge;
		const u8 expect = boe_td4320_esd_regs[i].expect;
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
static void boe_td4320_esd_recover(struct boe_td4320 *priv)
{
	int ret;

	__boe_td4320_disable(priv);
	__boe_td4320_unprepare(priv);
	msleep(BOE_TD4320_ESD_POWER_OFF_MS);

	ret = __boe_td4320_prepare(priv);
	if (ret) {
		DRM_ERROR("ESD: panel re-init failed: %d\n", ret);
		priv->esd_disabled = true;
		return;
	}

	ret = __boe_td4320_enable(priv);
	if (ret) {
		DRM_ERROR("ESD: panel re-init failed: %d\n", ret);
		priv->esd_disabled = true;
	}
}

static void boe_td4320_esd_work(struct work_struct *work)
{
	struct boe_td4320 *priv = container_of(to_delayed_work(work),
					       struct boe_td4320, esd_work);
	unsigned int period;

	mutex_lock(&priv->lock);

	if (priv->prepared && priv->enabled && !priv->esd_disabled) {
		if (boe_td4320_esd_failed(priv)) {
			if (++priv->esd_fail_count >=
			    BOE_TD4320_ESD_MAX_FAILURES) {
				priv->esd_fail_count = 0;
				priv->esd_recover_count++;

				if (priv->esd_recover_count >=
				    BOE_TD4320_ESD_MAX_RECOVER) {
					DRM_ERROR("ESD: %u recoveries, disabling the checker\n",
						  priv->esd_recover_count);
					priv->esd_disabled = true;
				} else {
					DRM_WARN("ESD: recovering the panel (attempt %u)\n",
						 priv->esd_recover_count);
					boe_td4320_esd_recover(priv);
				}
			}
		} else {
			priv->esd_fail_count = 0;
		}
	}

	period = priv->esd_disabled ? 0 : BOE_TD4320_ESD_PERIOD_MS;
	mutex_unlock(&priv->lock);

	if (period)
		schedule_delayed_work(&priv->esd_work,
				      msecs_to_jiffies(period));
}

static int boe_td4320_prepare(struct drm_panel *panel)
{
	struct boe_td4320 *priv = to_boe_td4320(panel);
	int ret;

	mutex_lock(&priv->lock);
	ret = __boe_td4320_prepare(priv);
	if (!ret)
		schedule_delayed_work(&priv->esd_work,
				      msecs_to_jiffies(BOE_TD4320_ESD_PERIOD_MS));
	mutex_unlock(&priv->lock);

	return ret;
}

static int boe_td4320_unprepare(struct drm_panel *panel)
{
	struct boe_td4320 *priv = to_boe_td4320(panel);

	/*
	 * Must run without priv->lock: the ESD worker takes it, so waiting
	 * for the worker here while holding it would deadlock. The worker
	 * re-checks prepared under the same lock before re-arming itself, so
	 * cancelling after it has cleared the flag is enough.
	 */
	cancel_delayed_work_sync(&priv->esd_work);

	mutex_lock(&priv->lock);
	__boe_td4320_unprepare(priv);
	mutex_unlock(&priv->lock);

	return 0;
}

static int boe_td4320_enable(struct drm_panel *panel)
{
	struct boe_td4320 *priv = to_boe_td4320(panel);
	int ret;

	mutex_lock(&priv->lock);
	ret = __boe_td4320_enable(priv);
	mutex_unlock(&priv->lock);

	return ret;
}

static int boe_td4320_disable(struct drm_panel *panel)
{
	struct boe_td4320 *priv = to_boe_td4320(panel);

	mutex_lock(&priv->lock);
	__boe_td4320_disable(priv);
	mutex_unlock(&priv->lock);

	return 0;
}

static int boe_td4320_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &boe_td4320_mode);
	if (!mode)
		return 0;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = boe_td4320_mode.width_mm;
	connector->display_info.height_mm = boe_td4320_mode.height_mm;
	connector->display_info.bpc = 8;
	/*
	 * hw,lcdkit-ldi-data-en-plr = <0x00> and hw,lcdkit-ldi-pixel-clk-plr =
	 * <0x00>: the LDI drives DE active high and samples pixel data on the
	 * rising edge of PIXCLK, so no DRM bus flags are needed.
	 */

	return 1;
}

static const struct drm_panel_funcs boe_td4320_panel_funcs = {
	.prepare = boe_td4320_prepare,
	.unprepare = boe_td4320_unprepare,
	.enable = boe_td4320_enable,
	.disable = boe_td4320_disable,
	.get_modes = boe_td4320_get_modes,
};

static int boe_td4320_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_td4320 *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct boe_td4320, panel,
				    &boe_td4320_panel_funcs,
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
	INIT_DELAYED_WORK(&priv->esd_work, boe_td4320_esd_work);
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
	 * at (see hi3670-cor-al00.dts).  Without this the panel lights up but
	 * the backlight device is never registered, so brightness stays
	 * wherever the bootloader left it.
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

	DRM_INFO("BOE TD4320 1080x2340 panel probed\n");
	return 0;
}

static void boe_td4320_remove(struct mipi_dsi_device *dsi)
{
	struct boe_td4320 *priv = mipi_dsi_get_drvdata(dsi);

	cancel_delayed_work_sync(&priv->esd_work);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&priv->panel);
}

static const struct of_device_id hisi_td4320_of_match[] = {
	{ .compatible = "hisi,td4320" },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, hisi_td4320_of_match);

static struct mipi_dsi_driver hisi_td4320_driver = {
	.probe = boe_td4320_probe,
	.remove = boe_td4320_remove,
	.driver = {
		.name = "hisi-td4320",
		.of_match_table = hisi_td4320_of_match,
	},
};
module_mipi_dsi_driver(hisi_td4320_driver);

MODULE_AUTHOR("HiSilicon Limited");
MODULE_DESCRIPTION("BOE TD4320 1080x2340 MIPI DSI panel driver (Honor Play)");
MODULE_LICENSE("GPL v2");
