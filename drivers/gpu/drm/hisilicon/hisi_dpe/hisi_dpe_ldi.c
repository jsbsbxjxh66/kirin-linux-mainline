// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 DPE LDI (LCD Display Interface) functions
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <drm/drm_print.h>

#include "hisi_dpe_drv.h"
#include "hisi_dpe_reg.h"

void hisi_dpe_ldi_set_mode(struct hisi_dpe_device *hisi_dpe,
			   struct drm_display_mode *mode)
{
	void __iomem *ldi_base = hisi_dpe->dss_base + DSS_LDI0_OFFSET;
	u32 hfp, hbp, hsw, vfp, vbp, vsw;
	u32 plr_flags;
	u32 val;
	u32 width = mode->hdisplay;
	u32 height = mode->vdisplay;

	/* Calculate timing parameters */
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;

	/*
	 * Set polarity. LDI_PLR_CTRL packs the four polarities into bits [3:0]
	 * (vsync | hsync << 1 | pixelclk << 2 | data_en << 3) - see
	 * init_ldi() in hisi_dpe_utils_kirin970.c. The COR-AL00 panels carry
	 * hw,lcdkit-ldi-{h,v}sync-plr / pixel-clk-plr / data-en-plr = <0>, i.e.
	 * all active-high, which is DRM's "no flag" case, so this register is 0
	 * for this hardware. DRM_MODE_FLAG_{NHSYNC,NVSYNC} map to FLAG_NHSYNC and
	 * FLAG_NVSYNC respectively; there is no pixelclk/data_en flag in DRM.
	 */
	plr_flags = 0;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		plr_flags |= FLAG_NVSYNC;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		plr_flags |= FLAG_NHSYNC;

	/*
	 * Set DPI0 horizontal timing. For a VIDEO-mode panel Android writes
	 *	HRZ_CTRL0 = h_front_porch | ((h_back_porch + DSS_WIDTH(h_pulse_width)) << 16)
	 *	HRZ_CTRL1 = 0
	 *	HRZ_CTRL2 = DSS_WIDTH(width)
	 * (init_ldi(), is_mipi_video_panel() branch). The upper field is the
	 * back porch plus the pulse width *minus one*, not the back porch plus
	 * the full pulse width, because the field is stored decremented.
	 */
	writel(hfp | ((hbp + DSS_WIDTH(hsw)) << 16),
	       ldi_base + LDI_DPI0_HRZ_CTRL0);
	writel(0, ldi_base + LDI_DPI0_HRZ_CTRL1);
	writel(DSS_WIDTH(width), ldi_base + LDI_DPI0_HRZ_CTRL2);

	/* Set vertical timing */
	writel(vfp | (vbp << 16), ldi_base + LDI_VRT_CTRL0);
	writel(DSS_HEIGHT(vsw), ldi_base + LDI_VRT_CTRL1);
	writel(DSS_HEIGHT(height), ldi_base + LDI_VRT_CTRL2);

	/* Set polarity */
	writel(plr_flags, ldi_base + LDI_PLR_CTRL);

	/*
	 * Set LDI control. Two fields matter here:
	 *   - bpp (bits [4:3]) encodes the output colour depth; LCD_RGB888 == 0
	 *     (soc_dss_interface.h). A non-zero value programs the wrong pixel
	 *     packing and yields a blank/garbled panel. Mirrors
	 *     set_reg(ldi_base + LDI_CTRL, pinfo->bpp, 2, 3).
	 *   - bgr_fmt (bit 13) selects the channel order the LDI emits; the
	 *     COR-AL00 panels carry hw,lcdkit-panel-bgrfmt = <0>, so it is cleared
	 *     (RGB). Mirrors set_reg(ldi_base + LDI_CTRL, pinfo->bgr_fmt, 1, 13).
	 * Read-modify-write rather than a plain write: bit 0 is LDI_EN, which
	 * hisi_dpe_ldi_enable() sets afterwards, and a plain write of these fields
	 * alone would leave the register holding only the changed bits and clear
	 * LDI_EN out from under the enable path.
	 */
	val = readl(ldi_base + LDI_CTRL);
	val &= ~(((u32)LDI_BPP_MASK << LDI_BPP_POS) | BIT(13));
	val |= LDI_BPP_RGB888 << LDI_BPP_POS;
	writel(val, ldi_base + LDI_CTRL);

	/*
	 * DDR power-management hint: v-inactive mask length, written with the raw
	 * (non-decremented) front porch. Mirrors
	 * outp32(ldi_base + LDI_VINACT_MSK_LEN, pinfo->ldi.v_front_porch).
	 */
	writel(vfp, ldi_base + LDI_VINACT_MSK_LEN);

	/*
	 * Command-event select. Android writes this unconditionally in init_ldi()
	 * regardless of video/command mode. Mirrors
	 * outp32(ldi_base + LDI_CMD_EVENT_SEL, 0x1).
	 */
	writel(0x1, ldi_base + LDI_CMD_EVENT_SEL);

	/*
	 * Pixel clock divider/gating. This is a non-IFBC (single-MIPI, VIDEO
	 * mode) panel, i.e. the IFBC_TYPE_NONE row of g_mipi_ifbc_division:
	 * pxl0_div2_gt_en = 0, pxl0_div4_gt_en = 0, pxl0_divxcfg = 0,
	 * pxl0_dsi_gt_en = 1. Mirrors init_ldi_pxl_div().
	 */
	writel(0x0, ldi_base + LDI_PXL0_DIV2_GT_EN);
	writel(0x0, ldi_base + LDI_PXL0_DIV4_GT_EN);
	writel(0x1, ldi_base + LDI_PXL0_GT_EN);
	writel(0x1, ldi_base + LDI_PXL0_DSI_GT_EN);
	writel(0x0, ldi_base + LDI_PXL0_DIVXCFG);

	/* Set normal work mode */
	writel(0x1, ldi_base + LDI_WORK_MODE);

	/* DSI video mode: CMD_MOD_CTRL = 0x2 for video */
	writel(0x2, ldi_base + LDI_DSI_CMD_MOD_CTRL);

	DRM_DEBUG_ATOMIC("LDI mode: %dx%d, clock=%d kHz\n",
			 width, height, mode->clock);
}

void hisi_dpe_ldi_enable(struct hisi_dpe_device *hisi_dpe)
{
	void __iomem *ldi_base = hisi_dpe->dss_base + DSS_LDI0_OFFSET;
	u32 val;

	if (!hisi_dpe->power_on)
		return;

	/*
	 * Enable the MCTL block for OVL0. Android's hisi_dss_mctl_on() does this
	 * once when the panel comes up, before any layer is composed; without it
	 * the flush registers the plane path writes have no master control to
	 * act on. OVL0 maps to MCTL_CTL0 (g_dss_module_ovl_base).
	 */
	writel(0x1, hisi_dpe->dss_base + DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_EN);

	/* Enable LDI */
	val = readl(ldi_base + LDI_CTRL);
	val |= LDI_EN;
	writel(val, ldi_base + LDI_CTRL);

	/* Enable LDI clock */
	writel(0x1, ldi_base + LDI_CLK_EN);

	DRM_DEBUG_ATOMIC("LDI enabled\n");
}

void hisi_dpe_ldi_disable(struct hisi_dpe_device *hisi_dpe)
{
	void __iomem *ldi_base = hisi_dpe->dss_base + DSS_LDI0_OFFSET;
	u32 val;

	if (!hisi_dpe->power_on)
		return;

	/* Disable LDI */
	val = readl(ldi_base + LDI_CTRL);
	val &= ~LDI_EN;
	writel(val, ldi_base + LDI_CTRL);

	/* Disable LDI clock */
	writel(0x0, ldi_base + LDI_CLK_EN);

	DRM_DEBUG_ATOMIC("LDI disabled\n");
}
