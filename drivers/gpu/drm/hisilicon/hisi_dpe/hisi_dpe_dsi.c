// SPDX-License-Identifier: GPL-2.0-only
// HiSilicon Hi3670 DPE DSI Host Controller driver
// Based on Android hisi_mipi_dsi_v410.c

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

#include "hisi_dpe_dsi_reg.h"

#define PHY_REF_CLK_RATE	19200000
#define DSI_MAX_PAYLOAD		64

/*
 * D-PHY tuning constants, taken verbatim from the Android
 * get_dsi_dphy_ctrl() in hisi_mipi_dsi_v410.c so the register values come
 * out bit-identical to the downstream driver.
 *
 * accuracy scales every D-PHY spec constant below into the same unit as ui
 * (which is itself accuracy-scaled), and unit_tx_byte_clk_hs = 8 * ui.
 */
#define DPHY_ACCURACY		10
#define DPHY_CLK_POST_ADJUST	215	/* hw,lcdkit-mipi-clk-post-adjust */
#define DPHY_DATA_HS_PREPARE_ADJUST 35	/* default when the DT leaves it 0 */
#define DPHY_LP11_SHORT		1	/* MIPI_SHORT_LP11 */
#define MAX_TX_ESC_CLK_HZ	10000000UL

struct hisi_dsi_phy_params {
	u32 clk_pre_delay, clk_post_delay;
	u32 clk_t_hs_prepare, clk_t_lpx, clk_t_hs_zero, clk_t_hs_trial;
	u32 data_pre_delay, data_post_delay;
	u32 data_t_hs_prepare, data_t_lpx, data_t_hs_zero, data_t_hs_trial;
	u32 clk_lane_lp2hs_time, clk_lane_hs2lp_time;
	u32 data_lane_lp2hs_time, data_lane_hs2lp_time;
	u32 phy_stop_wait_time;
	u32 rg_cp, rg_lpf_r, rg_pre_div, rg_div;
	u32 rg_0p8v, rg_2p5g, rg_320m, rg_band_sel, rg_vrefsel_vcm;
	u32 lane_byte_clk, clk_division;
};

struct hisi_dsi {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct mipi_dsi_host host;
	struct drm_display_mode mode;
	struct hisi_dsi_phy_params phy;
	struct device *dev;
	void __iomem *base;
	struct clk *pclk;
	u32 lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	/*
	 * D-PHY data rate in MHz, i.e. hw,lcdkit-mipi-dsi-bit-clk in the
	 * Android DT. Android derives every PHY timing from this value
	 * (lane_clock = 2 * dsi_bit_clk) rather than from the pixel clock,
	 * because this panel runs in burst mode where the link rate is set by
	 * the vendor-qualified burst configuration, not by pixel rate x bpp /
	 * lanes. Zero means "derive it from the mode".
	 */
	u32 dsi_bit_clk_mhz;
	struct reset_control *rst;
	bool enabled;
};

#define bridge_to_dsi(b) container_of(b, struct hisi_dsi, bridge)
#define host_to_dsi(h) container_of(h, struct hisi_dsi, host)

/*
 * Round-up division and decrement helpers, byte-for-byte from the Android
 * hisi_mipi_dsi_v410.c. Every intermediate value below is kept in the same
 * accuracy-scaled unit Android uses; only the final register fields are
 * divided down.
 */
static inline u32 dsi_round1(u32 x, u32 y)
{
	return x / y + (x % y > 0 ? 1 : 0);
}

static inline u32 dss_reduce(u32 x)
{
	return x > 0 ? x - 1 : 0;
}

static void dsi_calc_phy_params(unsigned long lane_clock_hz,
				struct hisi_dsi_phy_params *phy)
{
	unsigned long vco_div = 1;
	u32 n_pll, m_pll, ui, unit, accuracy;
	u32 clk_post, clk_pre, clk_t_hs_exit;
	u32 clk_t_hs_prepare_raw, clk_t_lpx_raw, clk_t_hs_zero_raw;
	u32 data_t_hs_prepare_raw, data_t_lpx_raw, data_t_hs_zero_raw;
	u32 data_t_hs_trial_raw;

	/*
	 * lane_clock is the D-PHY data rate per lane in Hz; the panel driver
	 * asks for 2 * dsi_bit_clk = 1124 MHz, quantised by the PLL below.
	 */
	if (lane_clock_hz >= 320000000UL && lane_clock_hz <= 2500000000UL) {
		phy->rg_band_sel = 0;
		vco_div = 1;
	} else if (lane_clock_hz >= 80000000UL) {
		phy->rg_band_sel = 1;
		vco_div = 4;
	} else {
		phy->rg_band_sel = 0;
		vco_div = 1;
	}

	n_pll = 2;
	m_pll = (u32)(lane_clock_hz * vco_div * n_pll / PHY_REF_CLK_RATE);
	lane_clock_hz = m_pll * (PHY_REF_CLK_RATE / n_pll) / vco_div;

	if (lane_clock_hz > 750000000UL)
		phy->rg_cp = 3;
	else
		phy->rg_cp = 1;

	phy->rg_pre_div = n_pll - 1;
	phy->rg_div = m_pll;
	phy->rg_0p8v = 0;
	phy->rg_2p5g = 1;
	phy->rg_320m = 0;
	phy->rg_lpf_r = 0;
	phy->rg_vrefsel_vcm = 0x5d;

	accuracy = DPHY_ACCURACY;
	ui = (u32)(10ULL * 1000000000ULL * accuracy / lane_clock_hz);
	unit = 8 * ui;

	clk_post = 600 * accuracy + 52 * ui + unit +
		   DPHY_CLK_POST_ADJUST * ui;
	clk_pre = 8 * ui + unit;
	clk_t_hs_exit = 1000 * accuracy + 100 * accuracy;

	/* clocked by TXBYTECLKHS */
	clk_t_hs_prepare_raw = 660 * accuracy;
	clk_t_lpx_raw = 2000 * accuracy + 10 * accuracy - clk_t_hs_prepare_raw;
	clk_t_hs_zero_raw = 3000 * accuracy + 3 * unit - clk_t_hs_prepare_raw;

	/* D-PHY: data_t_hs_trial >= max(n*8*UI, 60ns + n*4*UI), n = 1 */
	data_t_hs_trial_raw = max(600 * accuracy + 4 * ui, 8 * ui) +
			      8 * ui + 3 * unit;

	/* D-PHY: 40ns + 4*UI <= data_t_hs_prepare <= 85ns + 6*UI */
	if ((400 * accuracy + 4 * ui +
	     DPHY_DATA_HS_PREPARE_ADJUST * ui) <=
	    (850 * accuracy + 6 * ui - 8 * ui))
		data_t_hs_prepare_raw = 400 * accuracy + 4 * ui +
					DPHY_DATA_HS_PREPARE_ADJUST * ui;
	else
		data_t_hs_prepare_raw = 850 * accuracy + 6 * ui - 8 * ui;

	data_t_lpx_raw = 2000 * accuracy + 10 * accuracy - data_t_hs_prepare_raw;
	data_t_hs_zero_raw = 1450 * accuracy + 10 * ui + 3 * unit -
			     data_t_hs_prepare_raw;

	phy->clk_pre_delay = dsi_round1(0, unit);
	phy->clk_t_hs_prepare = dsi_round1(clk_t_hs_prepare_raw, unit);
	phy->clk_t_lpx = dsi_round1(clk_t_lpx_raw, unit);
	phy->clk_t_hs_zero = dsi_round1(clk_t_hs_zero_raw, unit);
	phy->clk_t_hs_trial = dsi_round1(600 * accuracy + 3 * unit, unit);

	phy->data_post_delay = dsi_round1(0, unit);
	phy->data_t_hs_prepare = dsi_round1(data_t_hs_prepare_raw, unit);
	phy->data_t_lpx = dsi_round1(data_t_lpx_raw, unit);
	phy->data_t_hs_zero = dsi_round1(data_t_hs_zero_raw, unit);
	phy->data_t_hs_trial = dsi_round1(data_t_hs_trial_raw, unit);

	phy->clk_post_delay = phy->data_t_hs_trial +
			      dsi_round1(clk_post, unit);

	/*
	 * hw,lcdkit-mipi-lp11_flag = <0x01> = MIPI_SHORT_LP11, which forces
	 * these three to zero; the long-LP11 alternative is kept for reference.
	 */
	if (DPHY_LP11_SHORT != DPHY_LP11_SHORT) {
		u32 clk_pre_rounded = dsi_round1(clk_pre, unit);

		phy->data_pre_delay = phy->clk_pre_delay + 2 + phy->clk_t_lpx +
				      phy->clk_t_hs_prepare + phy->clk_t_hs_zero +
				      8 + clk_pre_rounded;
	} else {
		phy->data_pre_delay = 0;
	}

	phy->clk_lane_lp2hs_time = phy->clk_pre_delay + phy->clk_t_lpx +
				   phy->clk_t_hs_prepare + phy->clk_t_hs_zero +
				   5 + 7;
	phy->clk_lane_hs2lp_time = phy->clk_t_hs_trial +
				   phy->clk_post_delay + 8 + 4;
	phy->data_lane_lp2hs_time = phy->data_t_lpx + phy->data_t_hs_prepare +
				    phy->data_t_hs_zero + 7;
	phy->data_lane_hs2lp_time = phy->data_t_hs_trial + 8 + 5;
	phy->phy_stop_wait_time = phy->clk_t_hs_trial +
				  dsi_round1(clk_t_hs_exit, unit) -
				  (phy->data_post_delay + 4 +
				   phy->data_t_hs_trial) + 3;

	phy->lane_byte_clk = (u32)(lane_clock_hz / 8);
	phy->clk_division = dsi_round1((phy->lane_byte_clk / 2) /
				       MAX_TX_ESC_CLK_HZ, 1);
}

static void dsi_set_mipi_phy(struct hisi_dsi *dsi)
{
	void __iomem *base = dsi->base;
	struct hisi_dsi_phy_params *phy = &dsi->phy;
	u32 i, val;

	writel(dsi->lanes - 1, base + DSI_PHY_IF_CFG);
	writel(phy->clk_division | (phy->clk_division << 8),
	       base + DSI_CLKMGR_CFG);
	/* 16~19bit: pclk_en, pclk_sel, dpipclk_en, dpipclk_sel */
	dsi_update_bits(base + DSI_CLKMGR_CFG, 16, 0xf, 0x5);

	writel(0, base + DSI_PHY_RSTZ);
	writel(0x00, base + DSI_PHY_TST_CTRL0);
	writel(0x01, base + DSI_PHY_TST_CTRL0);
	writel(0x00, base + DSI_PHY_TST_CTRL0);

	for (i = 0; i <= dsi->lanes + 1; i++)
		dsi_phy_tst_write(base,
				  DSI_PHY_TST_LANE_TRANSMISSION_PROPERTY +
				  (i << 5), 0x43);

	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_PRE_DELAY,
			  dss_reduce(phy->clk_pre_delay));
	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_POST_DELAY,
			  dss_reduce(phy->clk_post_delay));
	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_TLPX,
			  dss_reduce(phy->clk_t_lpx));
	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_PREPARE,
			  dss_reduce(phy->clk_t_hs_prepare));
	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_ZERO,
			  dss_reduce(phy->clk_t_hs_zero));
	dsi_phy_tst_write(base, DSI_PHY_TST_CLK_TRAIL,
			  dss_reduce(phy->clk_t_hs_trial));

	/*
	 * Android skips i == 2 (the address 0x60 is not a valid data lane on
	 * this D-PHY: lane0 = 0x60 is reserved, lane1 = 0x80, lane2 = 0xC0,
	 * lane3 = 0xE0).  The address must be computed AFTER the skip so the
	 * clock-lane block at 0xB0..0xB5 is not overwritten and lane 3's
	 * 0xD0..0xD5 is still written.
	 */
	for (i = 0; i <= dsi->lanes + 1; i++) {
		u32 ba;

		if (i == 2)
			i++;

		ba = 0x00010070 + (i << 5);

		dsi_phy_tst_write(base, ba + 0,
				  dss_reduce(phy->data_pre_delay));
		dsi_phy_tst_write(base, ba + 1,
				  dss_reduce(phy->data_post_delay));
		dsi_phy_tst_write(base, ba + 2, dss_reduce(phy->data_t_lpx));
		dsi_phy_tst_write(base, ba + 3,
				  dss_reduce(phy->data_t_hs_prepare));
		dsi_phy_tst_write(base, ba + 4,
				  dss_reduce(phy->data_t_hs_zero));
		dsi_phy_tst_write(base, ba + 5,
				  dss_reduce(phy->data_t_hs_trial));
	}

	/* D-PHY (not C-PHY) PLL configuration */
	dsi_phy_tst_write(base, 0x00010042, 0x21);
	dsi_phy_tst_write(base, 0x00010046,
			  phy->rg_cp + (phy->rg_lpf_r << 4));
	dsi_phy_tst_write(base, 0x00010048,
			  phy->rg_0p8v + (phy->rg_2p5g << 1) +
			  (phy->rg_320m << 2) + (phy->rg_band_sel << 3));
	dsi_phy_tst_write(base, 0x00010049, phy->rg_pre_div);
	dsi_phy_tst_write(base, 0x0001004A, phy->rg_div);
	dsi_phy_tst_write(base, 0x0001004F, 0xf0);
	dsi_phy_tst_write(base, 0x00010050, 0xc0);
	dsi_phy_tst_write(base, 0x00010051, 0x22);
	dsi_phy_tst_write(base, 0x00010053, phy->rg_vrefsel_vcm);
	/* enable BTA */
	dsi_phy_tst_write(base, 0x00010054, 0x03);
	/* PLL update control */
	dsi_phy_tst_write(base, 0x0001004B, 0x01);

	writel(DSI_PHY_ENABLECLK | DSI_PHY_UNRSTZ | DSI_PHY_UNSHUTDOWNZ,
	       base + DSI_PHY_RSTZ);
	usleep_range(1000, 1500);

	/* Wait for the PHY PLL lock (PHY_STATUS bit 0). */
	for (i = 100; i > 0; i--) {
		val = readl(base + DSI_PHY_STATUS);
		if (val & DSI_PHY_LOCK)
			break;
		udelay(1);
	}

	/*
	 * Wait for every data lane plus the clock lane to reach the stop state.
	 * The expected value is lane-count dependent, exactly as Android does it:
	 * 4 lanes -> BIT(4)|BIT(7)|BIT(9)|BIT(11), 3 -> BIT(4)|BIT(7)|BIT(9),
	 * 2 -> BIT(4)|BIT(7), otherwise BIT(4).
	 */
	if (dsi->lanes >= 4)
		val = BIT(4) | BIT(7) | BIT(9) | BIT(11);
	else if (dsi->lanes == 3)
		val = BIT(4) | BIT(7) | BIT(9);
	else if (dsi->lanes == 2)
		val = BIT(4) | BIT(7);
	else
		val = BIT(4);

	for (i = 100; i > 0; i--) {
		u32 st = readl(base + DSI_PHY_STATUS);

		if ((st & val) == val)
			break;
		udelay(1);
	}
}

static void dsi_set_video_mode(void __iomem *base,
			       struct drm_display_mode *mode,
			       struct hisi_dsi_phy_params *phy,
			       unsigned long flags)
{
	u32 hbp, hsw, vfp, vbp, vsw;
	u32 pixel_clk_hz = mode->clock * 1000;
	u32 hsa_time, hbp_time, hline_time;
	u32 vid_mode_type;

	writel(DSI_DPI_COLOR_RGB888, base + DSI_DPI_COLOR_CODING);
	/*
	 * hw,lcdkit-ldi-data-en-plr / vsync-plr / hsync-plr are all 0, so DE,
	 * VS and HS are active high and DPI_CFG_POL stays all-zero.
	 */
	writel(0, base + DSI_DPI_CFG_POL);

	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = min_t(u32, mode->vsync_end - mode->vsync_start, 15);

	hsa_time = (hsw * phy->lane_byte_clk) / pixel_clk_hz;
	hbp_time = (hbp * phy->lane_byte_clk) / pixel_clk_hz;
	hline_time = (mode->htotal * (u64)phy->lane_byte_clk) / pixel_clk_hz;

	writel(hsa_time, base + DSI_VID_HSA_TIME);
	writel(hbp_time, base + DSI_VID_HBP_TIME);
	writel(hline_time, base + DSI_VID_HLINE_TIME);
	writel(vsw, base + DSI_VID_VSA_LINES);
	writel(vbp, base + DSI_VID_VBP_LINES);
	writel(vfp, base + DSI_VID_VFP_LINES);
	writel(mode->vdisplay, base + DSI_VID_VACTIVE_LINES);
	writel(mode->hdisplay, base + DSI_VID_PKT_SIZE);

	/*
	 * hw,lcdkit-mipi-burst-mode = <0x02> is DSI_BURST_SYNC_PULSES_1
	 * (enum value 2). The panel sets MIPI_DSI_MODE_VIDEO_BURST |
	 * MIPI_DSI_MODE_VIDEO_SYNC_PULSE, which maps to value 2 below.
	 * Without SYNC_PULSE the mapping would yield value 3 (PULSES_2),
	 * which is wrong for this panel.
	 */
	if (flags & MIPI_DSI_MODE_VIDEO_BURST)
		vid_mode_type = (flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) ?
				DSI_BURST_SYNC_PULSES_1 :
				DSI_BURST_SYNC_PULSES_2;
	else
		vid_mode_type = (flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) ?
				DSI_NON_BURST_SYNC_PULSES :
				DSI_NON_BURST_SYNC_EVENTS;

	/* video mode: low power mode (lp11_flag != MIPI_DISABLE_LP11) */
	dsi_update_bits(base + DSI_VID_MODE_CFG, 8, DSI_MASK(6), 0x3f);
	/* video mode: send read cmd by lp mode */
	dsi_update_bits(base + DSI_VID_MODE_CFG, 15, DSI_MASK(1), 1);
	/* burst mode */
	dsi_update_bits(base + DSI_VID_MODE_CFG, 0, DSI_MASK(2),
			vid_mode_type);
	/* for dsi read, BTA enable */
	dsi_update_bits(base + DSI_PCKHDL_CFG, 2, DSI_MASK(1), 1);

	/* blank-display backlight fixup for video panels */
	writel(0x4 << 16, base + DSI_DPI_LP_CMD_TIM);
	writel(0x7FF, base + DSI_TO_CNT_CFG);

	/* PHY LP/HS transition timers and the read configuration register */
	writel(phy->clk_lane_lp2hs_time |
	       (phy->clk_lane_hs2lp_time << 16), base + DSI_PHY_TMR_LPCLK_CFG);
	writel(0x7FFF, base + DSI_PHY_TMR_RD_CFG);
	writel(phy->data_lane_lp2hs_time |
	       (phy->data_lane_hs2lp_time << 16), base + DSI_PHY_TMR_CFG);

	/* phy_stop_wait_time in PHY_IF_CFG bits 15:8 */
	dsi_update_bits(base + DSI_PHY_IF_CFG, 8, 0xff,
			phy->phy_stop_wait_time);

	writel(DSI_PHY_TXREQUESTCLKHS, base + DSI_LPCLK_CTRL);
	writel(DSI_VIDEO_MODE, base + DSI_MODE_CFG);
}

static void dsi_mipi_init(struct hisi_dsi *dsi)
{
	void __iomem *base = dsi->base;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	/*
	 * The D-PHY DDR bit clock per lane, matching Android's
	 * hw,lcdkit-mipi-dsi-bit-clk = <562> (MHz). When the DT does not carry
	 * an explicit bit clock, derive the nominal non-burst value from the
	 * pixel clock: the lane bit rate is pixel_clk * bpp / lanes, and
	 * dsi_calc_phy_params() is passed twice dsi_bit_clk as that bit rate
	 * (DDR: bit rate = 2 * clock), so dsi_bit_clk is half the bit rate.
	 */
	u32 dsi_bit_clk_mhz = dsi->dsi_bit_clk_mhz;

	if (!dsi_bit_clk_mhz) {
		dsi_bit_clk_mhz = (u64)dsi->mode.clock * bpp /
				  (dsi->lanes * 2000);
		dev_warn(dsi->dev,
			 "dsi-bit-clk-mhz missing, using %u MHz\n",
			 dsi_bit_clk_mhz);
	}

	dsi_calc_phy_params(dsi_bit_clk_mhz * 2000000UL, &dsi->phy);
	dsi_set_mipi_phy(dsi);
	dsi_set_video_mode(base, &dsi->mode, &dsi->phy, dsi->mode_flags);
	writel(DSI_POWERUP, base + DSI_PWR_UP);
}

static void hisi_dsi_bridge_enable(struct drm_bridge *bridge)
{
	struct hisi_dsi *dsi = bridge_to_dsi(bridge);
	int ret;

	if (dsi->enabled)
		return;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret)
		return;

	/* Release the DSI APB reset (ip_prst_dsi0) before touching registers */
	if (dsi->rst) {
		ret = reset_control_deassert(dsi->rst);
		if (ret) {
			clk_disable_unprepare(dsi->pclk);
			return;
		}
	}

	/*
	 * Android splits this into mipi_dsi_on_sub1/sub2.  sub1 runs
	 * mipi_init() (PHY + video timing) and drops into command mode so the
	 * panel init sequence can be sent over LP; sub2 switches back to video
	 * mode, enables EOTP TX and re-enables the HS clock only after the
	 * panel is on.  The mainline bridge callbacks have no post_enable
	 * hook, so we stay in video mode throughout and keep the HS clock
	 * running: the panel's DCS commands travel over LP regardless, and
	 * disabling the clock here would leave the stream dead during video.
	 */
	writel(0x02600008, dsi->base + DSI_MEM_CTRL);
	writel(0, dsi->base + DSI_DPI_VCID);
	writel(0x7FF, dsi->base + DSI_TO_CNT_CFG);
	dsi_mipi_init(dsi);

	/* cmd mode config: low power mode bits (harmless in video mode) */
	dsi_update_bits(dsi->base + DSI_CMD_MODE_CFG, 8, 0x7f, 0x7f);
	dsi_update_bits(dsi->base + DSI_CMD_MODE_CFG, 16, 0xf, 0xf);
	dsi_update_bits(dsi->base + DSI_CMD_MODE_CFG, 24, 1, 1);
	/* enable EOTP TX (hw,lcdkit-mipi-eotp-disable-flag = <0>) */
	dsi_update_bits(dsi->base + DSI_PCKHDL_CFG, 0, 1, 1);
	/* enable generate High Speed clock (phy_txrequestclkhs = 1) */
	dsi_update_bits(dsi->base + DSI_LPCLK_CTRL, 0, 1, 1);

	dsi->enabled = true;
}

static void hisi_dsi_bridge_post_disable(struct drm_bridge *bridge)
{
	/*
	 * Nothing to do here: hisi_dsi_bridge_disable() has already powered
	 * down the host (PWR_UP=0, LPCLK_CTRL=0, PHY_RSTZ=0).  The previous
	 * code wrote "enable EOTP / enable HS clock / switch to video mode"
	 * here, but post_disable runs AFTER disable, so those writes landed
	 * on an already-powered-down block and were pure noise.
	 */
}

static void hisi_dsi_bridge_disable(struct drm_bridge *bridge)
{
	struct hisi_dsi *dsi = bridge_to_dsi(bridge);

	if (!dsi->enabled)
		return;

	writel(0, dsi->base + DSI_PWR_UP);
	writel(0, dsi->base + DSI_LPCLK_CTRL);
	writel(0, dsi->base + DSI_PHY_RSTZ);
	if (dsi->rst)
		reset_control_assert(dsi->rst);
	clk_disable_unprepare(dsi->pclk);
	dsi->enabled = false;
}

static void hisi_dsi_bridge_mode_set(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode,
				     const struct drm_display_mode *adj_mode)
{
	struct hisi_dsi *dsi = bridge_to_dsi(bridge);

	drm_mode_copy(&dsi->mode, adj_mode);
}

static int hisi_dsi_bridge_attach(struct drm_bridge *bridge,
				  struct drm_encoder *encoder,
				  enum drm_bridge_attach_flags flags);

static const struct drm_bridge_funcs hisi_dsi_bridge_funcs = {
	.enable = hisi_dsi_bridge_enable,
	.post_disable = hisi_dsi_bridge_post_disable,
	.disable = hisi_dsi_bridge_disable,
	.mode_set = hisi_dsi_bridge_mode_set,
	.attach = hisi_dsi_bridge_attach,
};

/*
 * The bridge chain is encoder -> DPE -> DSI host -> panel. The DPE driver
 * resolves our bridge from its OF graph endpoint (port 0), so when it attaches
 * to us we have to chain on whatever this DT hangs off our own downstream
 * endpoint (port 1): either a panel, wrapped into a bridge, or a plain bridge.
 * Without this the panel is never attached and its prepare/enable never run.
 */
static int hisi_dsi_bridge_attach(struct drm_bridge *bridge,
				  struct drm_encoder *encoder,
				  enum drm_bridge_attach_flags flags)
{
	struct hisi_dsi *dsi = bridge_to_dsi(bridge);
	struct device *dev = dsi->dev;
	struct drm_panel *panel = NULL;
	struct drm_bridge *next;
	int ret;

	/*
	 * devm_drm_of_get_bridge() only resolves DRM bridges, so try it first
	 * and fall back to the panel-or-bridge lookup, wrapping a panel.
	 */
	next = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(next)) {
		ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0,
						  &panel, &next);
		if (ret)
			return ret;

		if (panel) {
			next = devm_drm_panel_bridge_add(dev, panel);
			if (IS_ERR(next))
				return PTR_ERR(next);
		}
	}

	dsi->next_bridge = next;

	return drm_bridge_attach(encoder, dsi->next_bridge, bridge, flags);
}

/*
 * Send one DSI message. Mirrors the two Android write paths,
 * mipi_dsi_lwrite() (long write, >2 payload bytes) and mipi_dsi_swrite()
 * (short write, 0-2 payload bytes), in hisi_mipi_dsi_host.c:
 *
 *   header = dtype[5:0] | vc[7:6] | wc[23:8]        (25 bits)
 *
 * where wc is the payload length for a long write, and the first two
 * payload bytes occupy data1[15:8] / data2[23:16] for a short write. The
 * payload words must be pushed into GEN_PLD_DATA BEFORE the header goes
 * into GEN_HDR, and the long-write payload starts at offset 0 -- the port
 * used to start it at offset 2, which silently dropped the first two bytes
 * of every register write (e.g. CF/CE page commands) and shifted the rest.
 *
 * GEN_VID_LP_CMD (bit 24) is what marks the transfer as a low-power
 * transmission while the DSI link is in video mode; Android ORs it into
 * every panel command because every lcdkit command list is tagged
 * dsi_lp_mode. Without it the panel ignores the writes.
 */
static int hisi_dsi_wait_cmd_room(void __iomem *base)
{
	int i;

	/*
	 * Wait until the command FIFO has room. CMD_PKT_STATUS bit 0 is
	 * gen_cmd_wempty (write fifo empty); Android's write path waits on
	 * bit 0 set together with PHY_STATUS bit 1 clear.
	 */
	for (i = 0; i < 10000; i++) {
		u32 pkg = readl(base + DSI_CMD_PKT_STATUS);
		u32 phy = readl(base + DSI_PHY_STATUS);

		if ((pkg & DSI_CMD_FIFO_EMPTY) &&
		    !(phy & DSI_PHY_STOPSTATE_CLK))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/*
 * Read path. mipi_dsi_dcs_read() in drm_mipi_dsi.c only builds the message
 * and hands it to us - the core never issues MAX_RETURN_PACKET_SIZE - so the
 * whole three-step sequence has to happen here, exactly as Android does it
 * in hisi_mipi_dsi_host.c: mipi_dsi_max_return_packet_size(), then
 * mipi_dsi_sread_request(), then mipi_dsi_read() once per 32-bit payload
 * word with gen_pld_r_empty (CMD_PKT_STATUS bit 4) polling in between.
 *
 * BTA is already armed in DSI_PCKHDL_CFG by hisi_dsi_init_phy_link() and
 * DSI_PHY_TMR_RD_CFG carries the read turnaround timing, so nothing extra
 * has to be programmed for a read to complete.
 */
static ssize_t hisi_dsi_host_read(struct hisi_dsi *dsi,
				  const struct mipi_dsi_msg *msg)
{
	void __iomem *base = dsi->base;
	const u8 *tx = msg->tx_buf;
	u8 *rx = msg->rx_buf;
	size_t i;
	int j;

	if (!msg->tx_buf || msg->tx_len != 1 || !rx || !msg->rx_len)
		return -EINVAL;

	if (hisi_dsi_wait_cmd_room(base))
		return -ETIMEDOUT;

	/* Step 1: tell the peripheral how many bytes we are willing to take. */
	writel(DSI_HDR_DTYPE(MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE) |
	       DSI_HDR_VC(msg->channel) | DSI_HDR_WC(msg->rx_len),
	       base + DSI_GEN_HDR);

	/* Step 2: the read request itself (DCS read with one parameter). */
	writel(DSI_HDR_DTYPE(msg->type) | DSI_HDR_VC(msg->channel) |
	       DSI_HDR_DATA1(tx[0]) | DSI_HDR_DATA2(0) | DSI_GEN_VID_LP_CMD,
	       base + DSI_GEN_HDR);

	/* Step 3: drain the response, one 32-bit payload word at a time. */
	for (i = 0; i < msg->rx_len; i += 4) {
		u32 word;
		size_t k;

		for (j = 0; j < 700; j++) {
			if (!(readl(base + DSI_CMD_PKT_STATUS) &
			      DSI_GEN_PLD_R_EMPTY))
				break;
			usleep_range(50, 100);
		}
		if (j == 700)
			return -ETIMEDOUT;

		word = readl(base + DSI_GEN_PLD_DATA);
		for (k = 0; k < 4 && i + k < msg->rx_len; k++)
			rx[i + k] = (word >> (k * 8)) & 0xff;
	}

	return msg->rx_len;
}

static ssize_t hisi_dsi_host_transfer(struct mipi_dsi_host *host,
				      const struct mipi_dsi_msg *msg)
{
	struct hisi_dsi *dsi = host_to_dsi(host);
	void __iomem *base = dsi->base;
	const u8 *tx = msg->tx_buf;
	u32 header;
	int i;

	if (!dsi->enabled)
		return -ENODEV;

	if (msg->tx_len < 1 || msg->tx_len > DSI_MAX_PAYLOAD)
		return -EINVAL;

	if (msg->rx_len)
		return hisi_dsi_host_read(dsi, msg);

	if (hisi_dsi_wait_cmd_room(base))
		return -ETIMEDOUT;

	header = DSI_HDR_DTYPE(msg->type) | DSI_HDR_VC(msg->channel);

	if (msg->tx_len > 2) {
		/* Long write: whole payload through GEN_PLD_DATA. */
		for (i = 0; i < msg->tx_len; i += 4) {
			u32 pl = 0;

			pl |= tx[i];
			if (i + 1 < msg->tx_len)
				pl |= tx[i + 1] << 8;
			if (i + 2 < msg->tx_len)
				pl |= tx[i + 2] << 16;
			if (i + 3 < msg->tx_len)
				pl |= tx[i + 3] << 24;
			writel(pl, base + DSI_GEN_PLD_DATA);
		}

		header |= DSI_HDR_WC(msg->tx_len);
	} else {
		/*
		 * Short write: the payload rides in the header itself, so
		 * nothing is pushed to GEN_PLD_DATA. dlen 0 sets both data
		 * fields to zero (DCS write with no parameter).
		 */
		if (msg->tx_len > 0)
			header |= DSI_HDR_DATA1(tx[0]);
		if (msg->tx_len > 1)
			header |= DSI_HDR_DATA2(tx[1]);
	}

	/* Mark the transfer as low-power; the panel lists are dsi_lp_mode. */
	header |= DSI_GEN_VID_LP_CMD;

	writel(header, base + DSI_GEN_HDR);

	/*
	 * Wait for the command engine to drain so a following transfer does
	 * not interleave with this one. DSI_CMD_FIFO_BUSY clears once the
	 * command has left the FIFO.
	 */
	for (i = 0; i < 10000; i++) {
		if (!(readl(base + DSI_CMD_PKT_STATUS) & DSI_CMD_FIFO_BUSY))
			break;
		udelay(1);
	}

	return msg->tx_len;
}

static int hisi_dsi_host_attach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *client)
{
	struct hisi_dsi *dsi = host_to_dsi(host);

	/*
	 * The bridge needs the peripheral's bus parameters to program the PHY
	 * and the DPI timing. They are set by the panel driver at its own
	 * probe time (client->lanes/format/mode_flags), so capture them here.
	 * Without this they stay 0 and dsi_mipi_init() divides by dsi->lanes.
	 */
	dsi->lanes = client->lanes;
	dsi->format = client->format;
	dsi->mode_flags = client->mode_flags;

	if (dsi->lanes < 1 || dsi->lanes > 4) {
		dev_err(host->dev, "invalid lane count %u from %s\n",
			dsi->lanes, dev_name(&client->dev));
		return -EINVAL;
	}

	return 0;
}

static const struct mipi_dsi_host_ops hisi_dsi_host_ops = {
	.attach = hisi_dsi_host_attach,
	.transfer = hisi_dsi_host_transfer,
};

static int hisi_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_dsi *dsi;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;
	dsi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsi->base))
		return PTR_ERR(dsi->base);

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk))
		return dev_err_probe(dev, PTR_ERR(dsi->pclk),
				     "failed to get pclk\n");

	/* ip_prst_dsi0 = PERRSTEN3 bit 28; upstream dw-mipi-dsi calls it "apb" */
	dsi->rst = devm_reset_control_get_optional_exclusive(dev, "apb");
	if (IS_ERR(dsi->rst))
		return dev_err_probe(dev, PTR_ERR(dsi->rst),
				     "failed to get apb reset\n");

	/*
	 * hw,lcdkit-mipi-dsi-bit-clk from the Android DT, expressed in MHz.
	 * Optional: without it the rate is derived from the display mode.
	 */
	of_property_read_u32(dev->of_node, "dsi-bit-clk-mhz",
			     &dsi->dsi_bit_clk_mhz);

	platform_set_drvdata(pdev, dsi);

	/* Register bridge */
	dsi->bridge.funcs = &hisi_dsi_bridge_funcs;
	dsi->bridge.of_node = dev->of_node;
	dsi->bridge.type = DRM_MODE_CONNECTOR_DSI;
	drm_bridge_add(&dsi->bridge);

	/* Register DSI host */
	dsi->host.dev = dev;
	dsi->host.ops = &hisi_dsi_host_ops;
	ret = mipi_dsi_host_register(&dsi->host);
	if (ret) {
		drm_bridge_remove(&dsi->bridge);
		return ret;
	}

	return 0;
}

static void hisi_dsi_remove(struct platform_device *pdev)
{
	struct hisi_dsi *dsi = platform_get_drvdata(pdev);

	mipi_dsi_host_unregister(&dsi->host);
	drm_bridge_remove(&dsi->bridge);
}

static const struct of_device_id hisi_dsi_of_match[] = {
	{ .compatible = "hisilicon,hi3670-dsi" },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, hisi_dsi_of_match);

static struct platform_driver hisi_dsi_driver = {
	.probe = hisi_dsi_probe,
	.remove = hisi_dsi_remove,
	.driver = {
		.name = "hisi-dsi",
		.of_match_table = hisi_dsi_of_match,
	},
};

module_platform_driver(hisi_dsi_driver);

MODULE_AUTHOR("HiSilicon Limited");
MODULE_DESCRIPTION("HiSilicon Hi3670 DSI Host Controller Driver");
MODULE_LICENSE("GPL v2");
