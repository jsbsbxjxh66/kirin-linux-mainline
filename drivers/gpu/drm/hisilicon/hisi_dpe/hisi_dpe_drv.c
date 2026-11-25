// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 DPE (Display Processing Engine) DRM driver
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "hisi_dpe_drv.h"
#include "hisi_dpe_reg.h"

static const struct hisi_dpe_format dpe_formats[] = {
	{ DRM_FORMAT_RGB565,	DMA_PIXEL_FORMAT_RGB_565 },
	{ DRM_FORMAT_XRGB8888,	DMA_PIXEL_FORMAT_XRGB_8888 },
	{ DRM_FORMAT_ARGB8888,	DMA_PIXEL_FORMAT_ARGB_8888 },
	{ DRM_FORMAT_XBGR8888,	DMA_PIXEL_FORMAT_XRGB_8888 },
	{ DRM_FORMAT_ABGR8888,	DMA_PIXEL_FORMAT_ARGB_8888 },
	{ DRM_FORMAT_RGBA8888,	DMA_PIXEL_FORMAT_ARGB_8888 },
	{ DRM_FORMAT_BGRA8888,	DMA_PIXEL_FORMAT_ARGB_8888 },
	{ DRM_FORMAT_RGB888,	DMA_PIXEL_FORMAT_XRGB_8888 },
};

/*
 * DFC (display format converter) pixel formats, keyed by the same DRM fourcc
 * but with the channel-order swap bits that the DFC block wants. Android
 * reaches this through hisi_pixel_format_hal2dfc() + hisi_rb_swap() in
 * hisi_dss_rdfc_config(), which programs the DFC block on every layer.
 */
static const struct hisi_dpe_dfc_format dpe_dfc_formats[] = {
	{ DRM_FORMAT_RGB565,	DFC_PIXEL_FORMAT_RGB_565,	0 },
	{ DRM_FORMAT_XRGB8888,	DFC_PIXEL_FORMAT_XBGR_8888,	0 },
	{ DRM_FORMAT_ARGB8888,	DFC_PIXEL_FORMAT_ABGR_8888,	0 },
	{ DRM_FORMAT_XBGR8888,	DFC_PIXEL_FORMAT_XRGB_8888,	1 },
	{ DRM_FORMAT_ABGR8888,	DFC_PIXEL_FORMAT_ARGB_8888,	1 },
	{ DRM_FORMAT_RGBA8888,	DFC_PIXEL_FORMAT_ABGR_8888,	0 },
	{ DRM_FORMAT_BGRA8888,	DFC_PIXEL_FORMAT_ARGB_8888,	1 },
	{ DRM_FORMAT_RGB888,	DFC_PIXEL_FORMAT_XBGR_8888,	0 },
};

const struct hisi_dpe_dfc_format *
hisi_dpe_get_dfc_format(u32 drm_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpe_dfc_formats); i++) {
		if (dpe_dfc_formats[i].drm_format == drm_format)
			return &dpe_dfc_formats[i];
	}

	return NULL;
}

u32 hisi_dpe_get_hw_format(u32 drm_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpe_formats); i++) {
		if (dpe_formats[i].drm_format == drm_format)
			return dpe_formats[i].hw_format;
	}

	return DMA_PIXEL_FORMAT_XRGB_8888;
}

int hisi_dpe_power_up(struct hisi_dpe_device *hisi_dpe)
{
	struct device *dev = &hisi_dpe->pdev->dev;
	int ret;

	mutex_lock(&hisi_dpe->power_lock);

	if (READ_ONCE(hisi_dpe->power_on)) {
		ret = 0;
		goto out;
	}

	/*
	 * The real clock/reset work lives in hisi_dpe_runtime_resume(); all
	 * this does is take a usage reference so the RPM core brings the block
	 * up (and keeps it up until the matching hisi_dpe_power_down()).
	 * power_on is set inside the resume callback, so callers see a coherent
	 * view of it afterwards.
	 */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		goto out;
	}
	ret = 0;
out:
	mutex_unlock(&hisi_dpe->power_lock);
	return ret;
}

/*
 * Inverse of hisi_dpe_power_up(), called from hisi_dpe_crtc_disable() once
 * the LDI is off.  Takes power_lock so it cannot interleave with a concurrent
 * power-up from mode_set_nofb() of an overlapping atomic commit; the RPM
 * callbacks themselves must not take power_lock or this would deadlock.
 */
void hisi_dpe_power_down(struct hisi_dpe_device *hisi_dpe)
{
	struct device *dev = &hisi_dpe->pdev->dev;

	mutex_lock(&hisi_dpe->power_lock);

	if (!READ_ONCE(hisi_dpe->power_on)) {
		mutex_unlock(&hisi_dpe->power_lock);
		return;
	}

	pm_runtime_put_sync(dev);

	mutex_unlock(&hisi_dpe->power_lock);
}

/*
 * RPM callbacks: the only place that actually turns the DSS block on and off.
 * power_on is written here (and only here) with WRITE_ONCE so that
 * enable/disable_vblank, which run with vbl_lock held and IRQs off, can poll
 * it with READ_ONCE() without taking power_lock.
 *
 * Order on resume mirrors Android's power-up sequence: clk_set_rate() ->
 * clk_prepare_enable() -> deassert reset -> DPE-internal clock gating
 * (all inside hisi_dpe_clk_enable()).  300 MHz is
 * DEFAULT_DSS_CORE_CLK_RATE_L1, the vote Android's get_dss_vote_cmd() picks
 * for a 1080p primary panel on Kirin970; 554 MHz is the 4K/120Hz L3 rate and
 * would need a 0.80V vote.
 */
static int hisi_dpe_runtime_resume(struct device *dev)
{
	struct hisi_dpe_device *hisi_dpe = dev_get_drvdata(dev);
	int ret;

	ret = hisi_dpe_clk_set_rate(hisi_dpe, 300000000);
	if (ret)
		return ret;	/* nothing enabled yet, so nothing to disable */

	ret = hisi_dpe_clk_enable(hisi_dpe);
	if (ret)
		return ret;	/* clk_enable() already rolled back on failure */

	/*
	 * Program the interrupt masks now that the block is clocked: mask all
	 * LDI sources, then unmask only the GLB ITF0 aggregator bit (see the
	 * comment in hisi_dpe_platform_probe for the rationale).
	 */
	dpe_write(hisi_dpe->dss_base, DSS_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK,
		  0xFFFFFFFF);
	dpe_write(hisi_dpe->dss_base, GLB_CPU_PDP_INT_MSK,
		  ~(u32)BIT_ITF0_INTS);

	WRITE_ONCE(hisi_dpe->power_on, true);
	return 0;
}

static int hisi_dpe_runtime_suspend(struct device *dev)
{
	struct hisi_dpe_device *hisi_dpe = dev_get_drvdata(dev);

	WRITE_ONCE(hisi_dpe->power_on, false);
	hisi_dpe_clk_disable(hisi_dpe);
	return 0;
}

static int __maybe_unused hisi_dpe_suspend(struct device *dev)
{
	struct hisi_dpe_device *hisi_dpe = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&hisi_dpe->drm);
}

static int __maybe_unused hisi_dpe_resume(struct device *dev)
{
	struct hisi_dpe_device *hisi_dpe = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&hisi_dpe->drm);
}

static const struct dev_pm_ops hisi_dpe_pm_ops = {
	SET_RUNTIME_PM_OPS(hisi_dpe_runtime_suspend, hisi_dpe_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(hisi_dpe_suspend, hisi_dpe_resume)
};

static int hisi_dpe_encoder_atomic_check(struct drm_encoder *encoder,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs hisi_dpe_encoder_helper_funcs = {
	.atomic_check = hisi_dpe_encoder_atomic_check,
};

static void hisi_dpe_crtc_enable(struct drm_crtc *crtc,
				 struct drm_atomic_commit *state)
{
	struct hisi_dpe_device *hisi_dpe = to_hisi_dpe_crtc(crtc);

	hisi_dpe_ldi_enable(hisi_dpe);
	drm_crtc_vblank_on(crtc);
}

static void hisi_dpe_crtc_disable(struct drm_crtc *crtc,
				  struct drm_atomic_commit *state)
{
	struct hisi_dpe_device *hisi_dpe = to_hisi_dpe_crtc(crtc);

	drm_crtc_vblank_off(crtc);
	hisi_dpe_ldi_disable(hisi_dpe);
	/*
	 * Stop the read channel while the DSS is still clocked. commit_planes()
	 * runs after commit_modeset_disables(), so the plane's atomic_disable()
	 * would otherwise write into a powered-down block. It re-checks
	 * power_on and becomes a no-op on this path.
	 */
	hisi_dpe_plane_disable(hisi_dpe);
	/*
	 * vblank_off() above runs disable_vblank() first, so the LDI mask
	 * write still happens with power_on set; only then do the clocks go
	 * away.  mode_set_nofb() brings them back on the next commit that
	 * turns the CRTC on.
	 */
	hisi_dpe_power_down(hisi_dpe);
}

static bool hisi_dpe_crtc_mode_fixup(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	return true;
}

/*
 * DBUF QoS / flux-request thresholds for the 1080p primary panel on Kirin970.
 * The values are the hardcoded FB_ACCEL_KIRIN970 / PRIMARY_PANEL_IDX /
 * 1080p branch of Android's init_dbuf() (hisi_dpe_utils_kirin970.c).
 * The thresholds gate the data-buffer fill level to the pixel clock and
 * total line time so the AXI QoS stays in range during DFS.
 */
static void hisi_dpe_init_dbuf(struct hisi_dpe_device *hisi_dpe)
{
	void __iomem *base = hisi_dpe->dss_base + DSS_DBUF0_OFFSET;

	/* xres * yres = 1080 * 2340 */
	dpe_write(base, DBUF_FRM_SIZE, 1080 * 2340);
	/* DSS_WIDTH(1080): width field is hsize-1 in bits [14:0] */
	dpe_write(base, DBUF_FRM_HSIZE, 1079);
	dpe_write(base, DBUF_SRAM_VALID_NUM, 1);
	/* thd_rqos: out = 0x217f, in = 0x1c78 */
	dpe_write(base, DBUF_THD_RQOS, (0x217f << 16) | 0x1c78);
	/* thd_wqos: both 0 (Android leaves them 0 for 1080p) */
	dpe_write(base, DBUF_THD_WQOS, 0);
	/* thd_cg: out = 0x217f, in = 0x227f */
	dpe_write(base, DBUF_THD_CG, (0x217f << 16) | 0x227f);
	/* thd_other: thd_cg_hold = 0, thd_wr_wait = 0 */
	dpe_write(base, DBUF_THD_OTHER, 0);
	/* flux_req_befdfs: out = 0x1f0c, in = 0x1140 */
	dpe_write(base, DBUF_THD_FLUX_REQ_BEF, (0x1f0c << 16) | 0x1140);
	/* flux_req_aftdfs: out = 0xfcc, in = 0x7e6 */
	dpe_write(base, DBUF_THD_FLUX_REQ_AFT, (0xfcc << 16) | 0x7e6);
	/*
	 * thd_dfs_ok.  Android captures thd_dfs_ok = thd_flux_req_befdfs_in
	 * *before* the Kirin970 hardcoded-override block runs
	 * (hisi_dpe_utils_kirin970.c:1557 vs the override at :1567), so it
	 * keeps the computed value rather than 0x1f0c:
	 *   dfs_time  = 80, depth = DBUF0_DEPTH = 1408, pxl_clk = 192 MHz,
	 *   xres = 1080, hbp + hsync + hfp = 36 + 16 + 196 = 248
	 *   thd_cg_out = (80 * 192e6 * 1080) / ((248 + 1080) * 6 * 1e6) = 2081
	 *   sram_valid_num = 2081 / 1408 = 1, sram_max_mem_depth = 2 * 1408
	 *   thd_flux_req_befdfs_in = 2816 * 50 / 100 = 1408 = 0x580
	 */
	dpe_write(base, DBUF_THD_DFS_OK, 0x580);
	/* dfs_ok_mask = 0, thd_flux_req_sw_en = 1 */
	dpe_write(base, DBUF_FLUX_REQ_CTRL, 0x1);
	dpe_write(base, DBUF_DFS_LP_CTRL, 0x1);
	/* dfs_ram = 0xF00 (non-4K) */
	dpe_write(base, DBUF_DFS_RAM_MANAGE, 0xF00);
}

/*
 * PERI_CTRL29 (PCTRL @ 0xE8A09000) is the DPHY TX stop-state timeout: how
 * many APB clocks the controller waits for all four data lanes to enter
 * stop state before it proceeds with the video stream. Android programs it
 * from pctrl_dphytx_stopcnt_config() (hisi_mipi_dsi_v410.c:1213-1240):
 *
 *   video panel: (hbp + hfp + hsw + xres + 5) * pclk_pctrl / pxl_clk
 *
 * which algebraically collapses to (htotal + 5) * pclk_pctrl / pxl_clk, and
 * pclk_pctrl is the constant DEFAULT_PCLK_PCTRL_RATE = 80 MHz
 * (hisi_overlay_utils_kirin970.h:79) - Android never clk_set_rate()s it.
 * Writing 0 here would leave no time for the lanes to settle and the first
 * frames can be dropped. The dual-DSI variant uses PERI_CTRL32 too; this
 * panel is single-link.
 */
static void hisi_dpe_pctrl_stopcnt_config(struct hisi_dpe_device *hisi_dpe,
					  const struct drm_display_mode *mode)
{
	u64 numerator;
	u32 denominator;

	if (!mode->clock)
		return;

	numerator = (u64)(mode->htotal + 5) * PCLK_PCTRL_RATE_HZ;
	denominator = (u32)mode->clock * 1000;	/* mode->clock is kHz */

	dpe_write(hisi_dpe->pctrl_base, PERI_CTRL29,
		  (u32)div_u64(numerator, denominator));
}

static void hisi_dpe_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct hisi_dpe_device *hisi_dpe = to_hisi_dpe_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->mode;

	/*
	 * Power up before writing LDI timing: mode_set_nofb runs first in
	 * the atomic commit (before atomic_update on planes and before
	 * atomic_flush which sends the vblank event).  Without this the
	 * timing writes land while DSS is still in reset and are lost.
	 * Runs in process context (drm_atomic_helper_commit_modeset_disables)
	 * so clk_prepare_enable() sleeping is safe.  Bail out if the clocks
	 * could not be brought up: every register write below would go to a
	 * powered-down block, and hisi_dpe_crtc_enable() has a matching
	 * power_on guard in hisi_dpe_ldi_enable().
	 */
	if (hisi_dpe_power_up(hisi_dpe)) {
		DRM_ERROR("failed to power up DPE for mode_set\n");
		return;
	}

	hisi_dpe_init_dbuf(hisi_dpe);
	hisi_dpe_ldi_set_mode(hisi_dpe, mode);
	hisi_dpe_pctrl_stopcnt_config(hisi_dpe, mode);
}

static void hisi_dpe_crtc_atomic_begin(struct drm_crtc *crtc,
				       struct drm_atomic_commit *state)
{
}

static void hisi_dpe_crtc_atomic_flush(struct drm_crtc *crtc,
				       struct drm_atomic_commit *state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	/*
	 * Nothing to poke in hardware here: the DSS clocks are all enabled once
	 * by hisi_dpe_inner_clk_enable() at power-up and stay on, and there is
	 * no per-frame register Android touches (GLB_MODULE_CLK_EN is defined in
	 * soc_dss_interface.h but never written anywhere in the vendor tree).
	 * The frame is already on its way to the panel from the plane/DMA
	 * update, so all that is left is to complete the vblank event.
	 */
	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs hisi_dpe_crtc_helper_funcs = {
	.mode_fixup	= hisi_dpe_crtc_mode_fixup,
	.mode_set_nofb	= hisi_dpe_crtc_mode_set_nofb,
	.atomic_begin	= hisi_dpe_crtc_atomic_begin,
	.atomic_flush	= hisi_dpe_crtc_atomic_flush,
	.atomic_enable	= hisi_dpe_crtc_enable,
	.atomic_disable	= hisi_dpe_crtc_disable,
};

static int hisi_dpe_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct hisi_dpe_device *hisi_dpe = to_hisi_dpe_crtc(crtc);

	/*
	 * Do NOT call hisi_dpe_power_up() here: drm_vblank_get() holds
	 * vbl_lock (a spinlock) with IRQs off, and clk_prepare_enable()
	 * sleeps.  Power-up happens in hisi_dpe_crtc_mode_set_nofb() which
	 * runs in process context at the start of every atomic commit.
	 * READ_ONCE(): power_on is written under power_lock, but this path
	 * runs with vbl_lock held and IRQs off, so it must not take the mutex.
	 */
	if (!READ_ONCE(hisi_dpe->power_on))
		return -ENXIO;

	/*
	 * Unmask the LDI frame interrupt. The mask register
	 * LDI_CPU_ITF_INT_MSK (0x024C) is an *active-high* mask: a 1 bit blocks
	 * the corresponding LDI_CPU_ITF_INTS (0x0248) status bit and a 0 bit lets
	 * it through, so unmasking means clearing the bit. Android does exactly
	 * that in dpe_interrupt_unmask() - for a VIDEO panel it unmasks
	 * BIT_VSYNC | BIT_VACTIVE0_START | BIT_VACTIVE0_END | BIT_FRM_END, and
	 * dpe_interrupt_mask() masks everything by writing ~0.
	 *
	 * DRM's vblank is one event per frame, and on a VIDEO-mode panel the
	 * vsync edge is the frame boundary, so BIT_VSYNC (bit 4) is the bit to
	 * use - it is what Android's hisifb_vsync_isr_handler() hangs its
	 * userspace vsync off. BIT_FRM_END (bit 1) fires once per frame too but
	 * is the command-panel path (where the equivalent is BIT_LCD_TE0_PIN).
	 */
	dpe_update_bits(hisi_dpe->dss_base,
			DSS_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK,
			BIT_VSYNC, 0);

	return 0;
}

static void hisi_dpe_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct hisi_dpe_device *hisi_dpe = to_hisi_dpe_crtc(crtc);

	if (!READ_ONCE(hisi_dpe->power_on))
		return;

	dpe_update_bits(hisi_dpe->dss_base,
			DSS_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK,
			BIT_VSYNC, BIT_VSYNC);
}

static void hisi_dpe_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static const struct drm_crtc_funcs hisi_dpe_crtc_funcs = {
	.destroy	= hisi_dpe_crtc_destroy,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank	= hisi_dpe_crtc_enable_vblank,
	.disable_vblank	= hisi_dpe_crtc_disable_vblank,
};

static irqreturn_t hisi_dpe_irq_handler(int irq, void *data)
{
	struct hisi_dpe_device *hisi_dpe = data;
	struct drm_crtc *crtc = &hisi_dpe->crtc;
	u32 glb_status;
	u32 status;

	/* Register access requires the block powered; ignore stray IRQs. */
	if (!READ_ONCE(hisi_dpe->power_on))
		return IRQ_HANDLED;

	/*
	 * The PDP interrupt status lives in several registers (global PDP, LDI
	 * CPU interface, DPP, SMMU). Only the LDI CPU interface one carries the
	 * vsync bit, so read it once, write the same value back to clear what was
	 * latched, then test the value that was read.  Read the global PDP
	 * status too and clear it so a level-latched source is not re-read on
	 * the next edge.  Mirrors hisi_fb_isr.c, which reads all four and
	 * writes each back.
	 */
	glb_status = dpe_read(hisi_dpe->dss_base, GLB_CPU_PDP_INTS);
	dpe_write(hisi_dpe->dss_base, GLB_CPU_PDP_INTS, glb_status);
	status = dpe_read(hisi_dpe->dss_base,
			  DSS_LDI0_OFFSET + LDI_CPU_ITF_INTS);

	dpe_write(hisi_dpe->dss_base,
		  DSS_LDI0_OFFSET + LDI_CPU_ITF_INTS, status);

	if (status & BIT_VSYNC)
		drm_crtc_handle_vblank(crtc);

	/*
	 * The line is registered with IRQF_SHARED, so an edge that belongs to
	 * another owner must not be swallowed: claim it only when one of our
	 * two status registers actually held something.
	 */
	if (!(glb_status | status))
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static const struct drm_mode_config_funcs hisi_dpe_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int hisi_dpe_crtc_init(struct drm_device *drm,
			      struct hisi_dpe_device *hisi_dpe)
{
	struct device_node *port;
	int ret;

	/*
	 * The port node reference is owned by crtc.port, so it must not be
	 * released here: drm_crtc_cleanup() takes care of it during teardown.
	 */
	port = of_get_child_by_name(drm->dev->of_node, "port");
	if (!port) {
		DRM_ERROR("no port node found in %pOF\n",
			  drm->dev->of_node);
		return -EINVAL;
	}
	hisi_dpe->crtc.port = port;

	ret = drm_crtc_init_with_planes(drm, &hisi_dpe->crtc,
					&hisi_dpe->primary, NULL,
					&hisi_dpe_crtc_funcs, NULL);
	if (ret) {
		hisi_dpe->crtc.port = NULL;
		of_node_put(port);
		return ret;
	}

	drm_crtc_helper_add(&hisi_dpe->crtc, &hisi_dpe_crtc_helper_funcs);

	return 0;
}

DEFINE_DRM_GEM_DMA_FOPS(hisi_dpe_fops);

static const struct drm_driver hisi_dpe_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &hisi_dpe_fops,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.name			= "hisi-dpe",
	.desc			= "HiSilicon Hi3670 DPE DRM Driver",
	.major			= 1,
	.minor			= 0,
};

static int hisi_dpe_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_dpe_device *hisi_dpe;
	struct drm_device *drm;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	int ret;

	hisi_dpe = devm_drm_dev_alloc(dev, &hisi_dpe_driver,
				      struct hisi_dpe_device, drm);
	if (IS_ERR(hisi_dpe))
		return PTR_ERR(hisi_dpe);

	drm = &hisi_dpe->drm;
	hisi_dpe->pdev = pdev;
	platform_set_drvdata(pdev, hisi_dpe);
	mutex_init(&hisi_dpe->power_lock);

	hisi_dpe->dss_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hisi_dpe->dss_base))
		return PTR_ERR(hisi_dpe->dss_base);

	hisi_dpe->peri_crg_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(hisi_dpe->peri_crg_base))
		return PTR_ERR(hisi_dpe->peri_crg_base);

	hisi_dpe->sctrl_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(hisi_dpe->sctrl_base))
		return PTR_ERR(hisi_dpe->sctrl_base);

	hisi_dpe->pctrl_base = devm_platform_ioremap_resource(pdev, 3);
	if (IS_ERR(hisi_dpe->pctrl_base))
		return PTR_ERR(hisi_dpe->pctrl_base);

	hisi_dpe->noc_dss_base = devm_platform_ioremap_resource(pdev, 4);
	if (IS_ERR(hisi_dpe->noc_dss_base))
		return PTR_ERR(hisi_dpe->noc_dss_base);

	hisi_dpe->mmbuf_crg_base = devm_platform_ioremap_resource(pdev, 5);
	if (IS_ERR(hisi_dpe->mmbuf_crg_base))
		return PTR_ERR(hisi_dpe->mmbuf_crg_base);

	hisi_dpe->pmctrl_base = devm_platform_ioremap_resource(pdev, 6);
	if (IS_ERR(hisi_dpe->pmctrl_base))
		return PTR_ERR(hisi_dpe->pmctrl_base);

	hisi_dpe->media_crg_base = devm_platform_ioremap_resource(pdev, 7);
	if (IS_ERR(hisi_dpe->media_crg_base))
		return PTR_ERR(hisi_dpe->media_crg_base);

	hisi_dpe->irq = platform_get_irq(pdev, 0);
	if (hisi_dpe->irq < 0)
		return hisi_dpe->irq;

	/*
	 * Resolve the clocks and resets once, here. hisi_dpe_clk_init() uses
	 * devm_*get() and therefore allocates devres entries; calling it from
	 * runtime_resume() leaked one set of handles on every screen on/off.
	 */
	ret = hisi_dpe_clk_init(hisi_dpe);
	if (ret) {
		DRM_ERROR("failed to get DPE clocks\n");
		return ret;
	}

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &hisi_dpe_mode_config_funcs;

	/* plane must exist (and carry DRM_PLANE_TYPE_PRIMARY) before the CRTC
	 * that references it is registered, otherwise drm_crtc_init_with_planes()
	 * warns about the plane type. */
	ret = hisi_dpe_plane_init(drm, hisi_dpe);
	if (ret)
		return ret;

	ret = hisi_dpe_crtc_init(drm, hisi_dpe);
	if (ret)
		return ret;

	/* Initialize encoder */
	ret = drm_simple_encoder_init(drm, &hisi_dpe->encoder,
				      DRM_MODE_ENCODER_DSI);
	if (ret)
		return ret;
	drm_encoder_helper_add(&hisi_dpe->encoder,
			       &hisi_dpe_encoder_helper_funcs);
	hisi_dpe->encoder.possible_crtcs = drm_crtc_mask(&hisi_dpe->crtc);

	/*
	 * Resolve whatever is attached to our OF graph endpoint. It can be a
	 * DRM bridge (the DSI host controller) or a DRM panel (the attached
	 * LCD). In the panel case, wrap it in a bridge so the rest of the
	 * pipeline only has to deal with bridges; the DSI host sits behind the
	 * panel's bridge inside the DSI host driver, which registers itself as
	 * the bridge for this endpoint. devm_drm_of_get_bridge() alone only
	 * resolves bridges and would fail with -ENODEV on a panel endpoint.
	 */
	ret = drm_of_find_panel_or_bridge(dev->of_node, 0, 0,
					  &panel, &bridge);
	if (ret) {
		DRM_ERROR("failed to find DSI bridge or panel\n");
		return ret;
	}

	if (panel) {
		bridge = devm_drm_panel_bridge_add(dev, panel);
		if (IS_ERR(bridge)) {
			DRM_ERROR("failed to add panel bridge\n");
			return PTR_ERR(bridge);
		}
	}

	ret = drm_bridge_attach(&hisi_dpe->encoder, bridge, NULL, 0);
	if (ret) {
		DRM_ERROR("failed to attach bridge\n");
		return ret;
	}

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;

	/*
	 * Mask every interrupt source before enabling the handler. SPI 245 is
	 * level-triggered; an unmasked source that is already asserted would
	 * immediately storm. Mirrors Android's dpe_interrupt_mask(), which
	 * writes ~0 to every mask register.
	 *
	 * The LDI-level mask (LDI_CPU_ITF_INT_MSK) can be ~0 here because it
	 * gates the individual LDI sources, including the ITF0 aggregation.
	 *
	 * The global mask (GLB_CPU_PDP_INT_MSK) is the *aggregator* between
	 * the LDI/DPP/SMMU blocks and the GIC: an interrupt only reaches the
	 * CPU when its GLB mask bit is clear. Android's dpe_interrupt_unmask()
	 * (hisi_dpe_utils_kirin970.c:2070-2074) leaves BIT_ITF0_INTS (the LDI
	 * interface source) unmasked, so masking it here as well would mean
	 * the vsync interrupt never arrives and DRM vblank would hang forever.
	 * Only ITF0 is opened up: DPP (BIT_DPP_INTS) and the SMMU (BIT_MMU_)
	 * IRPT_NS) stay masked, because this handler acknowledges GLB and LDI
	 * only and would spin on a level-latched source it never clears.
	 */
	/*
	 * Interrupt masks are programmed in hisi_dpe_runtime_resume(): the DSS
	 * block clocks are gated at probe time, so writing them here would be
	 * lost (or fault on the gated APB port).
	 */

	ret = devm_request_irq(dev, hisi_dpe->irq, hisi_dpe_irq_handler,
			       IRQF_SHARED, "hisi-dpe", hisi_dpe);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	/*
	 * Enabled last so that no failure path has to undo it.  From here on
	 * hisi_dpe_power_up()/power_down() resolve to RPM resume/suspend, and
	 * the block starts out in the suspended (clocks off) state, which is
	 * what power_on = false already describes.
	 */
	pm_runtime_enable(dev);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		drm_kms_helper_poll_fini(drm);
		pm_runtime_disable(dev);
		return ret;
	}

	drm_client_setup(drm, NULL);

	return 0;
}

static void hisi_dpe_platform_remove(struct platform_device *pdev)
{
	struct hisi_dpe_device *hisi_dpe = platform_get_drvdata(pdev);
	struct drm_device *drm = &hisi_dpe->drm;

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	pm_runtime_disable(&pdev->dev);
}

static void hisi_dpe_platform_shutdown(struct platform_device *pdev)
{
	struct hisi_dpe_device *hisi_dpe = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&hisi_dpe->drm);
}

static const struct of_device_id hisi_dpe_dt_ids[] = {
	{ .compatible = "hisilicon,hi3670-dpe" },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, hisi_dpe_dt_ids);

static struct platform_driver hisi_dpe_platform_driver = {
	.probe	= hisi_dpe_platform_probe,
	.remove	= hisi_dpe_platform_remove,
	.shutdown = hisi_dpe_platform_shutdown,
	.driver	= {
		.name = "hisi-dpe",
		.of_match_table = hisi_dpe_dt_ids,
		.pm = &hisi_dpe_pm_ops,
	},
};

drm_module_platform_driver(hisi_dpe_platform_driver);

MODULE_AUTHOR("HiSilicon Limited");
MODULE_DESCRIPTION("HiSilicon Hi3670 DPE DRM Driver");
MODULE_LICENSE("GPL v2");
