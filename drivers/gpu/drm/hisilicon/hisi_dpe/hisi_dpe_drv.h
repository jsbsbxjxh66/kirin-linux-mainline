/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HiSilicon Hi3670 DPE (Display Processing Engine) DRM driver
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#ifndef __HISI_DPE_DRV_H__
#define __HISI_DPE_DRV_H__

#include <linux/io.h>
#include <linux/mutex.h>
#include <drm/drm_device.h>
#include <drm/drm_plane.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>

#define HISI_DPE_MAX_PLANE	4
#define HISI_DPE_MAX_CRTC	1
#define HISI_DPE_MAX_ENCODER	1
#define HISI_DPE_MAX_CONNECTOR	1

/* Clock indices */
enum hisi_dpe_clk {
	HISI_DPE_CLK_AXI = 0,
	HISI_DPE_CLK_PCLK_DSS,
	HISI_DPE_CLK_PRI,
	HISI_DPE_CLK_PXL0,
	HISI_DPE_CLK_MMBUF,
	HISI_DPE_CLK_PCLK_MMBUF,
	HISI_DPE_CLK_DPHY0_REF,
	HISI_DPE_CLK_DPHY0_CFG,
	HISI_DPE_CLK_PCLK_DSI0,
	HISI_DPE_CLK_MAX
};

struct hisi_dpe_device {
	struct drm_device drm;
	struct platform_device *pdev;

	void __iomem *dss_base;
	void __iomem *peri_crg_base;
	void __iomem *sctrl_base;
	void __iomem *pctrl_base;
	void __iomem *noc_dss_base;
	void __iomem *mmbuf_crg_base;
	void __iomem *pmctrl_base;
	void __iomem *media_crg_base;

	struct clk *clks[HISI_DPE_CLK_MAX];
	struct reset_control *rst;

	int irq;
	bool power_on;
	/*
	 * Serialises hisi_dpe_power_up() / hisi_dpe_power_down(). Two atomic
	 * commits that disagree about whether the CRTC is active would
	 * otherwise interleave clk_prepare_enable() with clk_disable_unprepare().
	 * Readers that cannot sleep (enable_vblank runs with vbl_lock held and
	 * IRQs off) use READ_ONCE() on power_on instead of taking this.
	 */
	struct mutex power_lock;

	/* DRM objects */
	struct drm_crtc crtc;
	struct drm_plane primary;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_bridge *bridge;

	/* Hardware context */
	void *hw_ctx;
};

#define to_hisi_dpe_crtc(c) \
	container_of(c, struct hisi_dpe_device, crtc)

#define to_hisi_dpe_plane(p) \
	container_of(p, struct hisi_dpe_device, primary)

/* DPE operations callbacks */
struct hisi_dpe_funcs {
	int (*init)(struct hisi_dpe_device *hisi_dpe);
	void (*deinit)(struct hisi_dpe_device *hisi_dpe);
	int (*power_on)(struct hisi_dpe_device *hisi_dpe);
	void (*power_off)(struct hisi_dpe_device *hisi_dpe);
	int (*set_mode)(struct hisi_dpe_device *hisi_dpe,
			struct drm_display_mode *mode);
	void (*enable)(struct hisi_dpe_device *hisi_dpe);
	void (*disable)(struct hisi_dpe_device *hisi_dpe);
	void (*flush)(struct hisi_dpe_device *hisi_dpe);
};

/* Platform data from device tree */
struct hisi_dpe_plat_data {
	const struct hisi_dpe_funcs *funcs;
	u32 num_planes;
};

/* Format mapping table */
struct hisi_dpe_format {
	u32 drm_format;
	u32 hw_format;
};

/*
 * DFC (display format converter) mapping: the DFC block has its own pixel
 * format encoding, distinct from the DMA block's, plus an R/B channel swap
 * bit for the byte orders the hardware format alone cannot express.
 */
struct hisi_dpe_dfc_format {
	u32 drm_format;
	u32 hw_format;
	u32 rb_swap;
};

/* Read/write helpers */
static inline u32 dpe_read(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline void dpe_write(void __iomem *base, u32 reg, u32 val)
{
	writel(val, base + reg);
}

static inline void dpe_update_bits(void __iomem *base, u32 reg,
				   u32 mask, u32 val)
{
	u32 tmp;

	tmp = readl(base + reg);
	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, base + reg);
}

extern const struct hisi_dpe_funcs dpe_funcs;
extern const struct hisi_dpe_plat_data hi3670_dpe_plat_data;

/* Core functions */
int hisi_dpe_plane_init(struct drm_device *drm,
			struct hisi_dpe_device *hisi_dpe);
void hisi_dpe_plane_disable(struct hisi_dpe_device *hisi_dpe);

/* Clock functions */
int hisi_dpe_clk_init(struct hisi_dpe_device *hisi_dpe);
int hisi_dpe_clk_enable(struct hisi_dpe_device *hisi_dpe);
void hisi_dpe_clk_disable(struct hisi_dpe_device *hisi_dpe);
int hisi_dpe_clk_set_rate(struct hisi_dpe_device *hisi_dpe, unsigned long rate);

/* LDI functions */
void hisi_dpe_ldi_set_mode(struct hisi_dpe_device *hisi_dpe,
			   struct drm_display_mode *mode);
void hisi_dpe_ldi_enable(struct hisi_dpe_device *hisi_dpe);
void hisi_dpe_ldi_disable(struct hisi_dpe_device *hisi_dpe);

/* Power management */
int hisi_dpe_power_up(struct hisi_dpe_device *hisi_dpe);
void hisi_dpe_power_down(struct hisi_dpe_device *hisi_dpe);

/* Format mapping */
u32 hisi_dpe_get_hw_format(u32 drm_format);
const struct hisi_dpe_dfc_format *hisi_dpe_get_dfc_format(u32 drm_format);

#endif /* __HISI_DPE_DRV_H__ */
