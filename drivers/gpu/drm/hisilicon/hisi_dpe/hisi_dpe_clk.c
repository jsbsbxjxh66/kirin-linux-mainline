// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 DPE clock management
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "hisi_dpe_drv.h"
#include "hisi_dpe_reg.h"

static const char * const hisi_dpe_clk_names[] = {
	[HISI_DPE_CLK_AXI] = "dss_axi_clk",
	[HISI_DPE_CLK_PCLK_DSS] = "dss_pclk_dss_clk",
	[HISI_DPE_CLK_PRI] = "dss_pri_clk",
	[HISI_DPE_CLK_PXL0] = "dss_pxl0_clk",
	[HISI_DPE_CLK_MMBUF] = "dss_mmbuf_clk",
	[HISI_DPE_CLK_PCLK_MMBUF] = "dss_pclk_mmbuf",
	[HISI_DPE_CLK_DPHY0_REF] = "dss_dphy0_ref_clk",
	[HISI_DPE_CLK_DPHY0_CFG] = "dss_dphy0_cfg_clk",
	[HISI_DPE_CLK_PCLK_DSI0] = "dss_pclk_dsi0_clk",
};

/* Enable inner clock gating (from Android dss_lp_set_reg + dss_inner_clk_pdp_enable) */
static void hisi_dpe_inner_clk_enable(void __iomem *dss_base)
{
	/* Core AXI / MMBUF clock select */
	writel(0x00000001, dss_base + GLB_MODULE_CLK_SEL);

	/* VBIF AIF module clock select */
	writel(0x00000000, dss_base + DSS_VBIF0_AIF + AIF_MODULE_CLK_SEL);
	writel(0x00000000, dss_base + DSS_VBIF1_AIF + AIF_MODULE_CLK_SEL);

	/* CMD list clock */
	writel(0x00000000, dss_base + DSS_CMDLIST_OFFSET + CMD_CLK_SEL);

	/* VBIF AIF clock select */
	writel(0x00000000, dss_base + DSS_VBIF0_AIF + AIF_CLK_SEL0);
	writel(0x00000000, dss_base + DSS_VBIF0_AIF + AIF_CLK_SEL1);

	/* SMMU low power */
	writel(0x00000001, dss_base + DSS_SMMU_OFFSET + SMMU_LP_CTRL);

	/*
	 * DSS SMMU: force global bypass (SMMU_SCR bit 0). Android *clears*
	 * that bit ("global bypass cancel", hisi_dss_smmu_on() in
	 * hisi_overlay_utils_kirin970.c:944) and then points TTBR0 at its own
	 * page table and programs per-channel stream IDs. This port does none
	 * of that - the DTS carries no "iommus" property on the dpe node, so
	 * drm_gem_dma_helper hands the DMA a physical address. Leaving the
	 * bypass bit to whatever the bootloader left behind would make that
	 * assumption depend on the boot chain, so set it explicitly: bit 0 = 1
	 * makes the SMMU forward addresses untranslated. Only bit 0 is touched;
	 * the PTW/outstanding/cache fields keep their current values.
	 */
	dpe_update_bits(dss_base + DSS_SMMU_OFFSET, SMMU_SCR, BIT(0), BIT(0));

	/* VBIF1 AIF clock select (MMBUF) */
	writel(0x00000000, dss_base + DSS_VBIF1_AIF + AIF_CLK_SEL0);
	writel(0x00000000, dss_base + DSS_VBIF1_AIF + AIF_CLK_SEL1);

	/* MIF clock control */
	writel(0x00000001, dss_base + DSS_MIF_OFFSET + MIF_CLK_CTL);

	/*
	 * AFBC decode/encode clock gates. Android's dss_lp_set_reg() writes
	 * these to 0 first and then to 0x0c in the channel section
	 * (hisi_dpe_utils_kirin970.c:563-569 then :577-597), so the net
	 * runtime value is 0x0c; they end up gated-off only transiently.
	 * Leaving them at their reset value keeps the compression block on a
	 * clock that is not selected, which is exactly the "half-gated channel"
	 * failure mode this sequence exists to avoid.
	 */
	writel(0x0000000c,
	       dss_base + DSS_RCH_VG0_DMA_OFFSET + FBCD_CREG_FBCD_CTRL_GATE);
	writel(0x0000000c,
	       dss_base + DSS_RCH_VG1_DMA_OFFSET + FBCD_CREG_FBCD_CTRL_GATE);
	writel(0x0000000c,
	       dss_base + DSS_RCH_G0_DMA_OFFSET + FBCD_CREG_FBCD_CTRL_GATE);
	writel(0x0000000c,
	       dss_base + DSS_RCH_G1_DMA_OFFSET + FBCD_CREG_FBCD_CTRL_GATE);
	writel(0x0000000c,
	       dss_base + DSS_RCH_D0_DMA_OFFSET + FBCD_CREG_FBCD_CTRL_GATE);
	writel(0x0000000c, dss_base + DSS_WCH0_FBCE_CREG_CTRL_GATE);
	writel(0x0000000c, dss_base + DSS_WCH1_FBCE_CREG_CTRL_GATE);

	/*
	 * MIF block enable. hisi_dss_mif_on() enables the MIF block itself
	 * (MIF_ENABLE at DSS_MIF_OFFSET) and then each channel's MIF_CTRL0; the
	 * per-channel part is done per-frame in hisi_dpe_plane_atomic_update().
	 * MIF_ENABLE is the block-level gate, so it belongs here with the other
	 * DPE-internal clock gating rather than in the plane path.
	 */
	writel(0x00000001, dss_base + DSS_MIF_OFFSET + MIF_ENABLE);

	/* MCTL clock selects */
	writel(0x00000000, dss_base + DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_CTL1_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_CTL2_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_CTL3_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_CTL4_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_CTL5_OFFSET + MCTL_CTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_SYS_OFFSET + MCTL_MCTL_CLK_SEL);
	writel(0x00000000, dss_base + DSS_MCTRL_SYS_OFFSET + MCTL_MOD_CLK_SEL);

	/* Channel clock selects */
	writel(0x00000000, dss_base + DSS_RCH_VG0_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_VG1_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_VG2_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_G0_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_G1_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_D2_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_D3_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_D0_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_RCH_D1_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_WCH0_DMA_OFFSET + CH_CLK_SEL);
	writel(0x00000000, dss_base + DSS_WCH1_DMA_OFFSET + CH_CLK_SEL);

	/* OVL clock selects */
	writel(0x00000000, dss_base + DSS_OVL0_OFFSET + OV8_CLK_SEL);
	writel(0x00000000, dss_base + DSS_OVL1_OFFSET + OV8_CLK_SEL);
	writel(0x00000000, dss_base + DSS_OVL2_OFFSET + OV8_CLK_SEL);
	writel(0x00000000, dss_base + DSS_OVL3_OFFSET + OV2_CLK_SEL);

	/* Global memory control */
	writel(0x02605550, dss_base + GLB_DSS_MEM_CTRL);

	/* PDP (Primary Display Path) inner clock select */
	writel(0xffffff06, dss_base + DSS_LDI0_OFFSET + LDI_MODULE_CLK_SEL);
	writel(0x00000000, dss_base + DSS_HI_ACE_OFFSET + DPE_RAMCLK_FUNC);
	writel(0x00000400, dss_base + DSS_DPP_OFFSET + DPP_CLK_SEL);
	writel(0x00000000, dss_base + DSS_IFBC_OFFSET + IFBC_CLK_SEL);
	writel(0x00000000, dss_base + DSS_DSC_OFFSET + DSC_CLK_SEL);
	writel(0x00000000, dss_base + DSS_LDI0_OFFSET + LDI_CLK_SEL);
	writel(0x00000000, dss_base + DSS_DBUF0_OFFSET + DBUF_CLK_SEL);
}

int hisi_dpe_clk_init(struct hisi_dpe_device *hisi_dpe)
{
	struct device *dev = &hisi_dpe->pdev->dev;
	int i, ret;

	for (i = 0; i < HISI_DPE_CLK_MAX; i++) {
		hisi_dpe->clks[i] = devm_clk_get(dev, hisi_dpe_clk_names[i]);
		if (IS_ERR(hisi_dpe->clks[i])) {
			ret = PTR_ERR(hisi_dpe->clks[i]);
			if (ret == -ENOENT) {
				hisi_dpe->clks[i] = NULL;
				continue;
			}
			dev_err(dev, "failed to get clock %s: %d\n",
				hisi_dpe_clk_names[i], ret);
			return ret;
		}
	}

	hisi_dpe->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(hisi_dpe->rst)) {
		ret = PTR_ERR(hisi_dpe->rst);
		if (ret == -ENOENT)
			hisi_dpe->rst = NULL;
		else
			return ret;
	}

	return 0;
}

/*
 * Mirrors the Kirin970 DPE power-on order in
 * hisi_dpe.c::hisi_dpe_power_on: clk_set_rate() (see hisi_dpe_power_up) runs
 * first, then the CCF clocks are prepared/enabled, the DSS reset is deasserted,
 * and only then the DPE-internal sub-module clock gates are programmed.
 */
int hisi_dpe_clk_enable(struct hisi_dpe_device *hisi_dpe)
{
	struct device *dev = &hisi_dpe->pdev->dev;
	int i, ret;

	for (i = 0; i < HISI_DPE_CLK_MAX; i++) {
		if (!hisi_dpe->clks[i])
			continue;

		ret = clk_prepare_enable(hisi_dpe->clks[i]);
		if (ret) {
			dev_err(dev, "failed to enable clock %s: %d\n",
				hisi_dpe_clk_names[i], ret);
			goto err_disable_clks;
		}
	}

	if (hisi_dpe->rst) {
		ret = reset_control_deassert(hisi_dpe->rst);
		if (ret) {
			dev_err(dev, "failed to deassert reset: %d\n", ret);
			goto err_disable_clks;
		}
	}

	hisi_dpe_inner_clk_enable(hisi_dpe->dss_base);

	return 0;

err_disable_clks:
	for (i--; i >= 0; i--) {
		if (hisi_dpe->clks[i])
			clk_disable_unprepare(hisi_dpe->clks[i]);
	}
	return ret;
}

void hisi_dpe_clk_disable(struct hisi_dpe_device *hisi_dpe)
{
	int i;

	if (hisi_dpe->rst)
		reset_control_assert(hisi_dpe->rst);

	for (i = HISI_DPE_CLK_MAX - 1; i >= 0; i--) {
		if (hisi_dpe->clks[i])
			clk_disable_unprepare(hisi_dpe->clks[i]);
	}
}

/*
 * Pixel (DPI0/LDI0) clock for the COR-AL00 panel. Android sets dss_pxl0_clk to
 * panel_info.pxl_clk_rate directly (see dpe_set_clk_rate in
 * hisi_dpe_utils_kirin970.c), not derived as a divider of the DPE core clock.
 *
 * hw,lcdkit-panel-pxl-clk = <192> in cor_vn2_boe_ft8719_6p3_1080p_video.dtsi is
 * in MHz (lcdkit_panel_adaptor_hs.c multiplies it by 1000000), and it matches
 * the mode's drm_display_mode.clock = 192000 kHz:
 *
 *   htotal 1272 * vtotal 2515 * 60 fps = 191944 kHz  ->  rounded to 192 MHz
 *
 * The earlier 151.2 MHz figure was a 1272*2515*47.5fps arithmetic slip; the
 * panel is a 60 Hz panel (hw,lcdkit-panel-fps / the fps_func_switch default),
 * so 192 MHz is the rate the LDI actually has to run at.
 */
#define HISI_DPE_PXL0_CLK_RATE	192000000

int hisi_dpe_clk_set_rate(struct hisi_dpe_device *hisi_dpe,
			  unsigned long rate)
{
	struct device *dev = &hisi_dpe->pdev->dev;
	int ret;

	/*
	 * PRI (DSS core) clock.  Android's get_dss_vote_cmd() picks
	 * DEFAULT_DSS_CORE_CLK_RATE_L1 (300 MHz) for a 1080p primary panel
	 * on Kirin970 (RES_1080P branch, core_clk_upt_support = 1).
	 * 554 MHz is DEFAULT_DSS_CORE_CLK_RATE_L3 which only applies to
	 * 4K or >110 Hz panels; running at L3 without the matching
	 * 0.80V regulator vote can be unstable.
	 */
	if (hisi_dpe->clks[HISI_DPE_CLK_PRI]) {
		ret = clk_set_rate(hisi_dpe->clks[HISI_DPE_CLK_PRI], rate);
		if (ret) {
			dev_err(dev, "failed to set pri clk rate: %d\n", ret);
			return ret;
		}
	}

	/*
	 * MMBUF clock.  Android's dpe_set_clk_rate() always sets this for
	 * Kirin970; the 1080p vote is DEFAULT_DSS_MMBUF_CLK_RATE_L1 =
	 * 238 MHz.  Without it the clock sits at its boot default and the
	 * memory-buffer bandwidth calculation (init_dbuf) is wrong.
	 */
	if (hisi_dpe->clks[HISI_DPE_CLK_MMBUF]) {
		ret = clk_set_rate(hisi_dpe->clks[HISI_DPE_CLK_MMBUF],
				   238000000);
		if (ret)
			dev_err(dev, "failed to set mmbuf clk rate: %d\n",
				ret);
	}

	if (hisi_dpe->clks[HISI_DPE_CLK_PXL0]) {
		ret = clk_set_rate(hisi_dpe->clks[HISI_DPE_CLK_PXL0],
				   HISI_DPE_PXL0_CLK_RATE);
		if (ret) {
			dev_err(dev, "failed to set pxl0 clk rate: %d\n", ret);
			return ret;
		}
	}

	return 0;
}
