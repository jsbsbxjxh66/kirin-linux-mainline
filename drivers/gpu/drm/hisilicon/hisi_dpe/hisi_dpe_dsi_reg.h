/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HiSilicon Hi3670 DPE DSI register definitions
 * Based on Android kernel soc_dss_interface.h for Kirin 970
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#ifndef __HISI_DPE_DSI_REG_H__
#define __HISI_DPE_DSI_REG_H__

#include <linux/io.h>

/* DSI core registers (from Android MIPIDSI_*_OFFSET) */
#define DSI_VERSION			0x0000
#define DSI_PWR_UP			0x0004
#define DSI_CLKMGR_CFG			0x0008
#define DSI_DPI_VCID			0x000C
#define DSI_DPI_COLOR_CODING		0x0010
#define DSI_DPI_CFG_POL			0x0014
#define DSI_DPI_LP_CMD_TIM		0x0018
#define DSI_PCKHDL_CFG			0x002C
#define DSI_GEN_VCID			0x0030
#define DSI_MODE_CFG			0x0034
#define DSI_VID_MODE_CFG		0x0038
#define DSI_VID_PKT_SIZE		0x003C
#define DSI_VID_NUM_CHUNKS		0x0040
#define DSI_VID_NULL_SIZE		0x0044
#define DSI_VID_HSA_TIME		0x0048
#define DSI_VID_HBP_TIME		0x004C
#define DSI_VID_HLINE_TIME		0x0050
#define DSI_VID_VSA_LINES		0x0054
#define DSI_VID_VBP_LINES		0x0058
#define DSI_VID_VFP_LINES		0x005C
#define DSI_VID_VACTIVE_LINES		0x0060
#define DSI_EDPI_CMD_SIZE		0x0064
#define DSI_CMD_MODE_CFG			0x0068
#define DSI_GEN_HDR			0x006C
#define DSI_GEN_PLD_DATA			0x0070
#define DSI_CMD_PKT_STATUS		0x0074
#define DSI_TO_CNT_CFG			0x0078
#define DSI_HS_RD_TO_CNT			0x007C
#define DSI_LP_RD_TO_CNT			0x0080
#define DSI_HS_WR_TO_CNT			0x0084
#define DSI_LP_WR_TO_CNT			0x0088
#define DSI_BTA_TO_CNT			0x008C
#define DSI_LPCLK_CTRL			0x0094
#define DSI_PHY_TMR_LPCLK_CFG		0x0098
#define DSI_PHY_TMR_CFG			0x009C
#define DSI_PHY_RSTZ			0x00A0
#define DSI_PHY_IF_CFG			0x00A4
#define DSI_PHY_ULPS_CTRL		0x00A8
#define DSI_PHY_STATUS			0x00B0
#define DSI_PHY_TST_CTRL0		0x00B4
#define DSI_PHY_TST_CTRL1		0x00B8
#define DSI_INT_ST0			0x00BC
#define DSI_INT_ST1			0x00C0
#define DSI_INT_MSK0			0x00C4
#define DSI_INT_MSK1			0x00C8
#define DSI_CLK_DATA_TMR_CFG		0x00CC
#define DSI_DSC_PARAMETER		0x00F0
#define DSI_PHY_TMR_RD_CFG		0x00F4
#define DSI_MEM_CTRL			0x0194

/* DSI power states */
#define DSI_RESET			0
#define DSI_POWERUP			BIT(0)

/* DSI LPCLK control bits */
#define DSI_PHY_TXREQUESTCLKHS		BIT(0)
#define DSI_LPCLK_CONTCLKHS		BIT(1)

/* DSI PHY reset bits */
#define DSI_PHY_ENABLECLK		BIT(2)
#define DSI_PHY_UNRSTZ			BIT(1)
#define DSI_PHY_UNSHUTDOWNZ		BIT(0)

/* DSI PHY status bits */
#define DSI_PHY_LOCK			BIT(0)
#define DSI_PHY_STOPSTATE_0		BIT(0)
#define DSI_PHY_STOPSTATE_CLK		BIT(2)

/*
 * GEN_HDR layout (25 bits). Same encoding as the Android DSI_HDR_* macros
 * in hisi_mipi_dsi.h: data type in [5:0], virtual channel in [7:6], then
 * either the word count [23:8] for a long write or the first two payload
 * bytes in [15:8] / [23:16] for a short write. GEN_VID_LP_CMD (bit 24)
 * marks the transfer as low-power while the link is in video mode, which
 * is what every lcdkit command list (dsi_lp_mode) requires.
 */
#define DSI_HDR_DTYPE(dtype)		((dtype) & 0x03f)
#define DSI_HDR_VC(vc)			(((vc) & 0x03) << 6)
#define DSI_HDR_DATA1(data)		(((data) & 0x0ff) << 8)
#define DSI_HDR_DATA2(data)		(((data) & 0x0ff) << 16)
#define DSI_HDR_WC(wc)			(((wc) & 0x0ffff) << 8)
#define DSI_GEN_VID_LP_CMD		BIT(24)

/* CMD_PKT_STATUS bits */
#define DSI_CMD_FIFO_EMPTY		BIT(0)
#define DSI_CMD_FIFO_BUSY		BIT(1)
/* gen_pld_r_empty: set while the read-payload FIFO has nothing in it */
#define DSI_GEN_PLD_R_EMPTY		BIT(4)

/* DSI video mode types */
enum dsi_video_mode_type {
	DSI_NON_BURST_SYNC_PULSES = 0,
	DSI_NON_BURST_SYNC_EVENTS,
	DSI_BURST_SYNC_PULSES_1,
	DSI_BURST_SYNC_PULSES_2,
};

/* DSI work mode */
enum dsi_work_mode {
	DSI_VIDEO_MODE = 0,
	DSI_COMMAND_MODE,
};

/* DPI color coding (values match Synopsys DWC-DSI / Android DSI_24BITS_1=5) */
enum dsi_dpi_color_coding {
	DSI_DPI_COLOR_RGB565_1 = 0,
	DSI_DPI_COLOR_RGB565_2,
	DSI_DPI_COLOR_RGB666_1,
	DSI_DPI_COLOR_RGB666_2,
	DSI_DPI_COLOR_RGB666_LOOSE,
	DSI_DPI_COLOR_RGB888,
};

/* PHY test interface addresses (from Android MIPIDSI_PHY_TST_*) */
#define DSI_PHY_TST_CLK_PRE_DELAY		0x000100B0
#define DSI_PHY_TST_CLK_POST_DELAY		0x000100B1
#define DSI_PHY_TST_CLK_TLPX			0x000100B2
#define DSI_PHY_TST_CLK_PREPARE			0x000100B3
#define DSI_PHY_TST_CLK_ZERO			0x000100B4
#define DSI_PHY_TST_CLK_TRAIL			0x000100B5

#define DSI_PHY_TST_DATA_PRE_DELAY		0x00010070
#define DSI_PHY_TST_DATA_POST_DELAY		0x00010071
#define DSI_PHY_TST_DATA_TLPX			0x00010072
#define DSI_PHY_TST_DATA_PREPARE		0x00010073
#define DSI_PHY_TST_DATA_ZERO			0x00010074
#define DSI_PHY_TST_DATA_TRAIL			0x00010075

#define DSI_PHY_TST_LANE_TRANSMISSION_PROPERTY	0x00010077

/* PHY test interface write (same protocol as Android mipi_config_phy_test_code) */
static inline void dsi_phy_tst_write(void __iomem *base, u32 test_code_addr,
				     u32 test_code_parameter)
{
	/* Set address */
	writel(test_code_addr, base + DSI_PHY_TST_CTRL1);
	writel(0x02, base + DSI_PHY_TST_CTRL0);
	writel(0x00, base + DSI_PHY_TST_CTRL0);

	/* Write data */
	writel(test_code_parameter, base + DSI_PHY_TST_CTRL1);
	writel(0x02, base + DSI_PHY_TST_CTRL0);
	writel(0x00, base + DSI_PHY_TST_CTRL0);
}

/* Register read/write helper */
static inline void dsi_update_bits(void __iomem *addr, u32 bit_start,
				   u32 mask, u32 val)
{
	u32 tmp, orig;

	orig = readl(addr);
	tmp = orig & ~(mask << bit_start);
	tmp |= (val & mask) << bit_start;
	writel(tmp, addr);
}

#define DSI_MASK(x)			(BIT(x) - 1)

#endif /* __HISI_DPE_DSI_REG_H__ */
