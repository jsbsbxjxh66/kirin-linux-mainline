/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HiSilicon Hi3670 DPE (Display Processing Engine) register definitions
 * Based on Android kernel soc_dss_interface.h for Kirin 970
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#ifndef __HISI_DPE_REG_H__
#define __HISI_DPE_REG_H__

/* DSS sub-block offsets (relative to DSS base 0xE8600000) */
#define DSS_CMDLIST_OFFSET		0x2000
#define DSS_VBIF0_AIF			0x7000
#define DSS_VBIF1_AIF			0x9000
#define DSS_MIF_OFFSET			0xA000
#define DSS_MCTRL_SYS_OFFSET		0x10000
#define DSS_MCTRL_CTL0_OFFSET		0x10800
#define DSS_GLB0_OFFSET			0x12000
#define DSS_SMMU_OFFSET			0x80000

/* Read channel offsets */
#define DSS_RCH_VG0_DMA_OFFSET		0x20000
#define DSS_RCH_VG0_DFC_OFFSET		0x20100
#define DSS_RCH_VG0_SCL_OFFSET		0x20200
#define DSS_RCH_VG0_CSC_OFFSET		0x20500
#define DSS_RCH_VG0_VPP_OFFSET		0x20700
#define DSS_RCH_VG0_DMA_BUF_OFFSET	0x20800
#define DSS_RCH_VG0_AFBCD_OFFSET	0x20900

#define DSS_RCH_G0_DMA_OFFSET		0x38000
#define DSS_RCH_G0_DFC_OFFSET		0x38100
#define DSS_RCH_G0_CSC_OFFSET		0x38500
#define DSS_RCH_G0_DMA_BUF_OFFSET	0x38800
#define DSS_RCH_G0_AFBCD_OFFSET		0x38900

/* Read channel offsets (continued) */
#define DSS_RCH_VG1_DMA_OFFSET		0x28000
#define DSS_RCH_VG2_DMA_OFFSET		0x30000
#define DSS_RCH_G1_DMA_OFFSET		0x40000

/* DMA channel offsets */
#define DSS_RCH_D2_DMA_OFFSET		0x50000
#define DSS_RCH_D3_DMA_OFFSET		0x51000
#define DSS_RCH_D0_DMA_OFFSET		0x52000
#define DSS_RCH_D1_DMA_OFFSET		0x53000

/* Write channel offsets */
#define DSS_WCH0_DMA_OFFSET		0x5A000
#define DSS_WCH0_FBCE_CREG_CTRL_GATE	0x5A964
#define DSS_WCH1_DMA_OFFSET		0x5C000
#define DSS_WCH1_FBCE_CREG_CTRL_GATE	0x5C964

/* Overlay (OVL) offsets */
#define DSS_OVL0_OFFSET			0x60000
#define DSS_OVL1_OFFSET			0x60400
#define DSS_OVL2_OFFSET			0x60800
#define DSS_OVL3_OFFSET			0x60C00
#define OVL_STRIDE			0x400

/* Data buffer (DBUF) offsets */
#define DSS_DBUF0_OFFSET		0x6D000
#define DBUF_CLK_SEL			0x005C
#define DSS_DBUF1_OFFSET		0x6E000
#define DBUF_FRM_SIZE			0x0000
#define DBUF_FRM_HSIZE			0x0004
#define DBUF_SRAM_VALID_NUM		0x0008
#define DBUF_THD_RQOS			0x0018
#define DBUF_THD_WQOS			0x001C
#define DBUF_THD_CG			0x0020
#define DBUF_THD_OTHER			0x0024
#define DBUF_THD_FLUX_REQ_BEF		0x0048
#define DBUF_DFS_LP_CTRL		0x004C
#define DBUF_THD_FLUX_REQ_AFT		0x0064
#define DBUF_THD_DFS_OK			0x0068
#define DBUF_FLUX_REQ_CTRL		0x006C
#define DBUF_DFS_RAM_MANAGE		0x00A8

/* Display Post Processor (DPP) */
#define DSS_DPP_OFFSET			0x70000
#define DSS_DPP_DITHER_OFFSET		0x70200
#define DSS_DPP_GAMA_LUT_OFFSET		0x71000

/* Display ACE */
#define DSS_HI_ACE_OFFSET		0x6F000

/* LCD Display Interface (LDI) */
#define DSS_LDI0_OFFSET			0x7D000
#define DSS_IFBC_OFFSET			0x7D800
#define DSS_DSC_OFFSET			0x7DC00
#define DSS_LDI1_OFFSET			0x7E000

/* MIPI DSI */
#define DSS_MIPI_DSI0_OFFSET		0x1000
#define DSS_MIPI_DSI1_OFFSET		0x1400

/* Global control registers (GLB0) */
#define GLB_DSS_TAG			(DSS_GLB0_OFFSET + 0x0000)
#define GLB_APB_CTL			(DSS_GLB0_OFFSET + 0x0004)
#define GLB_DSS_AXI_RST_EN		(DSS_GLB0_OFFSET + 0x0118)
#define GLB_DSS_APB_RST_EN		(DSS_GLB0_OFFSET + 0x011C)
#define GLB_DSS_CORE_RST_EN		(DSS_GLB0_OFFSET + 0x0120)
#define GLB_PXL0_DIV2_RST_EN		(DSS_GLB0_OFFSET + 0x0124)
#define GLB_PXL0_DIV4_RST_EN		(DSS_GLB0_OFFSET + 0x0128)
#define GLB_PXL0_RST_EN			(DSS_GLB0_OFFSET + 0x012C)
#define GLB_PXL0_DSI_RST_EN		(DSS_GLB0_OFFSET + 0x0130)
#define GLB_MM_AXI_CLK_RST_EN		(DSS_GLB0_OFFSET + 0x0138)

/* Global interrupt status */
#define GLB_CPU_PDP_INTS		(DSS_GLB0_OFFSET + 0x224)
#define GLB_CPU_PDP_INT_MSK		(DSS_GLB0_OFFSET + 0x228)

/* Global clock select */
#define GLB_MODULE_CLK_SEL		(DSS_GLB0_OFFSET + 0x0300)
#define GLB_MODULE_CLK_EN		(DSS_GLB0_OFFSET + 0x0304)

/* Memory control */
#define GLB_DSS_MEM_CTRL		(DSS_GLB0_OFFSET + 0x0600)

/* PCTRL registers (absolute offsets into the pctrl syscon @ 0xE8A09000) */
#define PERI_CTRL29			0x0078
#define PERI_CTRL32			0x0084
/* APB rate Android's dpe_get_dss_vote_cmd() assumes for PCTRL (80 MHz) */
#define PCLK_PCTRL_RATE_HZ		80000000UL

/* LDI register offsets (relative to DSS_LDI0_OFFSET, Android values) */
#define LDI_DPI0_HRZ_CTRL0		0x0000
#define LDI_DPI0_HRZ_CTRL1		0x0004
#define LDI_DPI0_HRZ_CTRL2		0x0008
#define LDI_VRT_CTRL0			0x000C
#define LDI_VRT_CTRL1			0x0010
#define LDI_VRT_CTRL2			0x0014
#define LDI_PLR_CTRL			0x0018
#define LDI_SH_MASK_INT			0x001C
#define LDI_3D_CTRL			0x0020
#define LDI_CTRL			0x0024
#define LDI_WORK_MODE			0x0028
#define LDI_DE_SPACE_LOW		0x002C
#define LDI_DSI_CMD_MOD_CTRL		0x0030
#define LDI_DSI_TE_CTRL			0x0034
#define LDI_DSI_TE_HS_NUM		0x0038
#define LDI_DSI_TE_HS_WD		0x003C
#define LDI_DSI_TE_VS_WD		0x0040
#define LDI_FRM_MSK			0x0044
#define LDI_FRM_MSK_UP			0x0048
#define LDI_VINACT_MSK_LEN		0x0050
#define LDI_CMD_EVENT_SEL		0x0060
#define LDI_DPI_SET			0x0070
#define LDI_DPI1_HRZ_CTRL0		0x00F0
#define LDI_DPI1_HRZ_CTRL1		0x00F4
#define LDI_DPI1_HRZ_CTRL2		0x00F8
#define LDI_OVERLAP_SIZE		0x00FC
#define LDI_MEM_CTRL			0x0100
#define LDI_PM_CTRL			0x0104
#define LDI_CLK_SEL			0x0108
#define LDI_CLK_EN			0x010C
#define LDI_PXL0_DIV2_GT_EN		0x0210
#define LDI_PXL0_DIV4_GT_EN		0x0214
#define LDI_PXL0_GT_EN			0x0218
#define LDI_PXL0_DSI_GT_EN		0x021C
#define LDI_PXL0_DIVXCFG		0x0220
#define LDI_MODULE_CLK_SEL		0x0258
#define LDI_DSI1_RST_SEL		0x0238
#define LDI_CPU_ITF_INTS		0x0248
#define LDI_CPU_ITF_INT_MSK		0x024C

/* LDI control bits */
#define LDI_EN				BIT(0)
#define DATA_GATE_EN			BIT(1)
#define LDI_BPP_POS			3
#define LDI_BPP_MASK			0x3
#define LDI_BPP_RGB888			0

/* LDI polarity bits */
#define FLAG_NVSYNC			BIT(0)
#define FLAG_NHSYNC			BIT(1)

/* DMA channel register offsets (relative to channel DMA base) */
#define DMA_OFT_X0			0x0000
#define DMA_OFT_Y0			0x0004
#define DMA_OFT_X1			0x0008
#define DMA_OFT_Y1			0x000C
#define DMA_STRETCH_SIZE_VRT		0x0018
#define DMA_CTRL			0x001C
#define DMA_DATA_ADDR0			0x0060
#define DMA_STRIDE0			0x0064
#define CH_RD_SHADOW			0x00D0
#define CH_CTL				0x00D4
#define CH_REG_DEFAULT			0x0A00

/*
 * DMA stride unit. The DMA engine's stride fields are in 16-byte units, not
 * bytes, so the byte stride is divided by this before being written to
 * DMA_STRIDE0 (soc_dss_interface.h DMA_ALIGN_BYTES = 128 / BITS_PER_BYTE).
 */
#define DMA_ALIGN_BYTES			16

/* DMA_BUF register offsets (relative to channel DMA_BUF base) */
#define DMA_BUF_ADDR_OFFSET		0x000
#define DMA_BUF_STRIDE			0x004
#define DMA_BUF_SIZE			0x850

/*
 * OVL register offsets (relative to OVL base).
 *
 * Kirin 970 (dss_version_tag = FB_ACCEL_KIRIN970 = 0x40, see
 * kirin970_udp_cs_overlay.dtsi) uses the 8-layer "OV" register map from
 * soc_dss_interface.h, NOT the 6-layer "OVL" map the earlier SoCs use:
 * hisi_dss_ovl_set_reg() / hisi_dss_ovl_init() branch on exactly that tag and
 * only this map is reachable here. The two maps share the block-level
 * registers but disagree on every layer register, and the layer stride is
 * 0x60 rather than 0x3C. Programming the 6-layer offsets on this SoC writes
 * the wrong registers entirely and the plane never reaches the panel.
 */
#define OV_SIZE			0x000
#define OV_BG_COLOR_RGB		0x004
#define OV_BG_COLOR_A		0x008
#define OV_DST_STARTPOS		0x00C
#define OV_DST_ENDPOS		0x010
#define OV_GCFG			0x014
#define OV8_CLK_SEL		0x0348
#define OV2_CLK_SEL		0x0208
#define OV8_BLOCK_SIZE		0x0350
#define OV8_BLOCK_DBG		0x0354
#define OV8_REG_DEFAULT		0x0358

/* OV layer 0 registers (layer n starts at OV_LAYER0_* + n * 0x60) */
#define OV_LAYER0_POS		0x030
#define OV_LAYER0_SIZE		0x034
#define OV_LAYER0_SRCLOKEY	0x038
#define OV_LAYER0_SRCHIKEY	0x03C
#define OV_LAYER0_DSTLOKEY	0x040
#define OV_LAYER0_DSTHIKEY	0x044
#define OV_LAYER0_PATTERN_RGB	0x048
#define OV_LAYER0_PATTERN_A	0x04C
#define OV_LAYER0_ALPHA_MODE	0x050
#define OV_LAYER0_ALPHA_A	0x054
#define OV_LAYER0_CFG		0x058
#define OV_LAYER0_PSPOS		0x05C
#define OV_LAYER0_PEPOS		0x060
#define OV_LAYER0_INFO_ALPHA	0x064
#define OV_LAYER0_INFO_SRCCOLOR	0x068
#define OV_LAYER0_DBG_INFO	0x06C

/*
 * OV layer blending. On Kirin 970 the OVL is the 8-layer "OV" block
 * (dss_version_tag = FB_ACCEL_KIRIN970 = 0x40), whose layer registers are
 * named ALPHA_MODE (blend mode) and ALPHA_A (global alpha); the 6-layer
 * register map calls the same two ALPHA and INFO_ALPHA. See
 * hisi_dss_ovl_layer_config() in the Android driver, which fills both
 * registers on every used layer.
 */
#define BIT_OVL_LAYER_ENABLE		BIT(0)
#define BIT_OVL_LAYER_SRC_CFG		BIT(8)

/*
 * OV_LAYER0_ALPHA_MODE (blend mode) for an opaque, non-premultiplied layer
 * with no per-pixel alpha - Android's DSS_BLEND_FIX_PER17 row of g_ovl_alpha[]
 * (blending == HISI_FB_BLENDING_NONE, glb_alpha == 0xFF): {0,0,0,0,0, 1,
 * 1,0,0,0, 0} over (src_amode, src_gmode, alpha_offsrc, src_lmode, src_pmode,
 * alpha_smode, dst_amode, dst_gmode, alpha_offdst, dst_pmode, fix_mode) laid
 * out by
 *	(fix_mode << 8) | (dst_pmode << 9) | (alpha_offdst << 10) |
 *	(dst_gmode << 12) | (dst_amode << 14) | (alpha_smode << 24) |
 *	(src_pmode << 25) | (src_lmode << 26) | (alpha_offsrc << 27) |
 *	(src_gmode << 28) | (src_amode << 30)
 */
#define OV_LAYER0_ALPHA_MODE_BLEND	0x01004000
/*
 * OV_LAYER0_ALPHA_A (global alpha) for glb_alpha = 0xFF. Android scales the
 * alpha into a 10-bit field first (OVL_PATTERN_RATIO = 4) and writes it into
 * both halves of the register: 0xFF * 4 | (0xFF * 4) << 16 = 0x03FC03FC.
 */
#define OV_LAYER0_ALPHA_A_FULL		0x03fc03fc

/* DFC register offsets (relative to channel DFC base, soc_dss_interface.h) */
#define DFC_DISP_SIZE			0x0000
#define DFC_PIX_IN_NUM			0x0004
#define DFC_GLB_ALPHA01			0x0008
#define DFC_DISP_FMT			0x000C
#define DFC_CLIP_CTL_HRZ		0x0010
#define DFC_CLIP_CTL_VRZ		0x0014
#define DFC_CTL_CLIP_EN			0x0018
#define DFC_ICG_MODULE			0x001C
#define DFC_DITHER_ENABLE		0x0020
#define DFC_PADDING_CTL			0x0024
#define DFC_GLB_ALPHA23			0x0028
#define DFC_BITEXT_CTL			0x0040

/* MCTL (Master Control) register offsets */
#define MCTL_CTL_EN			0x0000
#define MCTL_CTL_MUTEX			0x0004
#define MCTL_CTL_MUTEX_STATUS		0x0008
#define MCTL_CTL_MUTEX_ITF		0x000C
#define MCTL_CTL_MUTEX_DBUF		0x0010
#define MCTL_CTL_MUTEX_SCF		0x0014
#define MCTL_CTL_MUTEX_OV		0x0018
#define MCTL_CTL_MUTEX_WCH0		0x0020
#define MCTL_CTL_MUTEX_WCH1		0x0024
#define MCTL_CTL_MUTEX_WCH2		0x0028
#define MCTL_CTL_MUTEX_RCH8		0x002C
#define MCTL_CTL_MUTEX_RCH0		0x0030
#define MCTL_CTL_MUTEX_RCH1		0x0034
#define MCTL_CTL_MUTEX_RCH2		0x0038
#define MCTL_CTL_MUTEX_RCH3		0x003C
#define MCTL_CTL_MUTEX_RCH4		0x0040
#define MCTL_CTL_MUTEX_RCH5		0x0044
#define MCTL_CTL_MUTEX_RCH6		0x0048
#define MCTL_CTL_MUTEX_RCH7		0x004C
#define MCTL_CTL_TOP			0x0050
#define MCTL_CTL_FLUSH_STATUS		0x0054
#define MCTL_CTL_CLK_SEL		0x0080
#define MCTL_CTL_CLK_EN			0x0084
#define MCTL_CTL_DBG			0x00E0

/*
 * MCTL per-channel registers (relative to DSS_MCTRL_SYS_OFFSET,
 * soc_dss_interface.h). VG0 is read channel 2.
 */
#define MCTL_RCH2_OV_OEN		0x0168
#define MCTL_MOD2_DBG			0x0208

/* MCTL per-module flush-enable registers (relative to DSS_MCTRL_SYS_OFFSET) */
#define MCTL_RCH0_FLUSH_EN		0x0100
#define MCTL_RCH1_FLUSH_EN		0x0104
#define MCTL_RCH2_FLUSH_EN		0x0108
#define MCTL_RCH3_FLUSH_EN		0x010C
#define MCTL_RCH4_FLUSH_EN		0x0110
#define MCTL_RCH5_FLUSH_EN		0x0114
#define MCTL_RCH6_FLUSH_EN		0x0118
#define MCTL_RCH7_FLUSH_EN		0x011C
#define MCTL_WCH0_FLUSH_EN		0x0120
#define MCTL_WCH1_FLUSH_EN		0x0124
#define MCTL_OV0_FLUSH_EN		0x0128
#define MCTL_OV1_FLUSH_EN		0x012C
#define MCTL_OV2_FLUSH_EN		0x0130
#define MCTL_OV3_FLUSH_EN		0x0134
#define MCTL_RCH8_FLUSH_EN		0x0138
#define MCTL_WCH2_FLUSH_EN		0x013C
#define MCTL_RCH_OV0_SEL		0x0180
#define MCTL_RCH_OV0_SEL1		0x0190
#define DSS_MCTRL_CTL1_OFFSET		0x10900
#define DSS_MCTRL_CTL2_OFFSET		0x10A00
#define DSS_MCTRL_CTL3_OFFSET		0x10B00
#define DSS_MCTRL_CTL4_OFFSET		0x10C00
#define DSS_MCTRL_CTL5_OFFSET		0x10D00
#define MCTL_MCTL_CLK_SEL		0x01F0
#define MCTL_MOD_CLK_SEL		0x01F8

/* MIF clock control */
#define MIF_CLK_CTL			0x0508

/* MIF (Memory Interface) per-channel registers, soc_dss_interface.h */
#define MIF_ENABLE			0x0000
#define MIF_CTRL0			0x0000
#define MIF_CTRL1			0x0004
#define MIF_CTRL2			0x0008
#define MIF_CTRL3			0x000C
#define MIF_CTRL4			0x0010
#define MIF_CTRL5			0x0014
#define MIF_CTRL_OFFSET			0x0020
/* VG0 (V0) is MIF channel 2 */
#define DSS_MIF_VG0_OFFSET		(DSS_MIF_OFFSET + MIF_CTRL_OFFSET * 3)

/* SMMU low power control */
#define SMMU_LP_CTRL			0x0008
#define SMMU_SCR			0x0000
#define SMMU_SMRx_NS			0x0020
#define SMMU_RLD_EN0_NS		0x01F0
#define SMMU_RLD_EN1_NS		0x01F4
#define SMMU_CB_TTBCR		0x020C
#define SMMU_FAMA_CTRL0		0x0220
#define SMMU_FAMA_CTRL1		0x0224

/* VBIF AIF clock register offsets */
#define AIF_CLK_SEL0			0x0820
#define AIF_CLK_SEL1			0x0824
#define AIF_CLK_EN0			0x0828
#define AIF_CLK_EN1			0x082C
#define AIF_MODULE_CLK_SEL		0x0A04

/*
 * VBIF AIF per-channel registers (soc_dss_interface.h). VG0 (V0) is
 * AIF0 channel 2, i.e. DSS_VBIF0_AIF + 0x40.
 */
#define AIF_CH_CTL			0x0000
#define AIF_CH_HS			0x0004
#define AIF_CH_LS			0x0008
#define DSS_AIF0_VG0_OFFSET		(DSS_VBIF0_AIF + 0x40)

/* CMDLIST clock select */
#define CMD_CLK_SEL			0x0740

/* DPP register offsets */
#define DPP_CLK_SEL			0x020
#define DPP_CSC_IDC			0x040
#define DPP_CSC_ODC			0x044
#define DPP_CSC_P0			0x048
#define DPP_CSC_P1			0x04C
#define DPP_CSC_P2			0x050
#define DPP_CSC_P3			0x054
#define DPP_CSC_P4			0x058

/* DPE RAM clock */
#define DPE_RAMCLK_FUNC			0x0128

/* IFBC clock select */
#define IFBC_CLK_SEL			0x0044

/* DSC clock select */
#define DSC_CLK_SEL			0x007C

/* Channel clock select (relative to channel base) */
#define CH_CLK_SEL			0x00E0

/* AFBC decode / encode clock gates (relative to channel base) */
#define FBCD_CREG_FBCD_CTRL_GATE	0x098C

/* PDP interrupt bits */
#define BIT_ITF0_INTS			BIT(16)
#define BIT_DPP_INTS			BIT(15)
#define BIT_MMU_IRPT_NS			BIT(28)
#define BIT_MMU_IRPT_S			BIT(29)
#define BIT_DSS_GLB_INTS		BIT(30)

/* LDI interrupt bits */
#define BIT_FRM_START			BIT(0)
#define BIT_FRM_END			BIT(1)
#define BIT_LDI_UNFLOW			BIT(2)
#define BIT_VSYNC			BIT(4)

/* Flush enable bits */
#define BIT_CTL_FLUSH_EN		BIT(21)
#define BIT_DPP0_FLUSH_EN		BIT(18)
#define BIT_DBUF1_FLUSH_EN		BIT(17)
#define BIT_DBUF0_FLUSH_EN		BIT(16)
#define BIT_OV3_FLUSH_EN		BIT(15)
#define BIT_OV2_FLUSH_EN		BIT(14)
#define BIT_OV1_FLUSH_EN		BIT(13)
#define BIT_OV0_FLUSH_EN		BIT(12)
#define BIT_DMA3_FLUSH_EN		BIT(9)
#define BIT_DMA2_FLUSH_EN		BIT(8)
#define BIT_DMA1_FLUSH_EN		BIT(7)
#define BIT_DMA0_FLUSH_EN		BIT(6)

/* DMA channel format definitions */
enum dss_dma_format {
	DMA_PIXEL_FORMAT_RGB_565 = 0,
	DMA_PIXEL_FORMAT_ARGB_4444,
	DMA_PIXEL_FORMAT_XRGB_4444,
	DMA_PIXEL_FORMAT_ARGB_5551,
	DMA_PIXEL_FORMAT_XRGB_5551,
	DMA_PIXEL_FORMAT_ARGB_8888,
	DMA_PIXEL_FORMAT_XRGB_8888,
	DMA_PIXEL_FORMAT_RESERVED0,
	DMA_PIXEL_FORMAT_YUYV_422_PKG,
	DMA_PIXEL_FORMAT_YUV_420_SP_HP,
	DMA_PIXEL_FORMAT_YUV_420_P_HP,
	DMA_PIXEL_FORMAT_YUV_422_SP_HP,
	DMA_PIXEL_FORMAT_YUV_422_P_HP,
	DMA_PIXEL_FORMAT_AYUV_4444,
	DMA_PIXEL_FORMAT_RGBA_1010102 = 16,
};

/*
 * DFC format definitions (soc_dss_interface.h enum dss_dfc_format). Only the
 * RGB entries the primary plane can carry are listed; the OVL/MCTL path never
 * sees YUV.
 */
enum dss_dfc_format {
	DFC_PIXEL_FORMAT_RGB_565 = 0,
	DFC_PIXEL_FORMAT_XRGB_4444,
	DFC_PIXEL_FORMAT_ARGB_4444,
	DFC_PIXEL_FORMAT_XRGB_5551,
	DFC_PIXEL_FORMAT_ARGB_5551,
	DFC_PIXEL_FORMAT_XRGB_8888,
	DFC_PIXEL_FORMAT_ARGB_8888,
	DFC_PIXEL_FORMAT_BGR_565,
	DFC_PIXEL_FORMAT_XBGR_4444,
	DFC_PIXEL_FORMAT_ABGR_4444,
	DFC_PIXEL_FORMAT_XBGR_5551,
	DFC_PIXEL_FORMAT_ABGR_5551,
	DFC_PIXEL_FORMAT_XBGR_8888,
	DFC_PIXEL_FORMAT_ABGR_8888,
	DFC_PIXEL_FORMAT_RGBA_1010102 = 20,
};

/*
 * Read channel indices. These are the *hardware* channel numbers the MCTL,
 * MIF and AIF blocks use, and they are NOT the order the channel register
 * blocks appear in the memory map: the map runs VG0, VG1, VG2, G0, G1, D2, D3,
 * D0, D1, but the MCTL/MIF/AIF channel numbering runs D2, D3, V0, G0, V1, G1,
 * D0, D1. See enum dss_chn_idx in the Android tree (hisi_dss.h) - VIGOR0 is
 * DSS_RCHN_V0 = 2, which is why the VG0 register block at 0x20000 is MCTL
 * RCH2 / MIF CH2 / AIF0 CH2.
 */
enum dss_chn_idx {
	DSS_RCHN_D2 = 0,
	DSS_RCHN_D3,
	DSS_RCHN_V0,
	DSS_RCHN_G0,
	DSS_RCHN_V1,
	DSS_RCHN_G1,
	DSS_RCHN_D0,
	DSS_RCHN_D1,
	DSS_CHN_MAX
};

/* Overlay indices */
enum dss_ovl_idx {
	DSS_OVL0 = 0,
	DSS_OVL1,
	DSS_OVL2,
	DSS_OVL3,
	DSS_OVL_MAX
};

/* Helper macros */
#define DSS_WIDTH(width)		((width) - 1)
#define DSS_HEIGHT(height)		((height) - 1)

#endif /* __HISI_DPE_REG_H__ */
