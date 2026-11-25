// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 DPE plane functions
 *
 * Copyright (c) 2024 HiSilicon Limited.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include "hisi_dpe_drv.h"
#include "hisi_dpe_reg.h"

static const u32 hisi_dpe_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGB888,
};

static int hisi_dpe_plane_atomic_check(struct drm_plane *plane,
				       struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_crtc_state *crtc_state;

	if (!crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if ((new_state->src_w >> 16) != new_state->crtc_w ||
	    (new_state->src_h >> 16) != new_state->crtc_h)
		return -EINVAL;

	if ((new_state->src_x >> 16) + (new_state->src_w >> 16) > fb->width ||
	    (new_state->src_y >> 16) + (new_state->src_h >> 16) > fb->height)
		return -EINVAL;

	/*
	 * The DMA engine addresses memory in 16-byte beats: OFT_X0 is
	 * (src_x * cpp) / 16 and STRIDE0 is pitch / 16. Both divisions
	 * truncate, so an unaligned start or pitch would silently shift the
	 * image. Userspace has to provide an aligned framebuffer; dumb
	 * buffers already pad the pitch to 64 bytes.
	 */
	if (((new_state->src_x >> 16) * fb->format->cpp[0]) % DMA_ALIGN_BYTES ||
	    fb->pitches[0] % DMA_ALIGN_BYTES)
		return -EINVAL;

	return 0;
}

static void hisi_dpe_plane_atomic_update(struct drm_plane *plane,
					 struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct hisi_dpe_device *hisi_dpe =
		to_hisi_dpe_crtc(new_state->crtc);
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_gem_dma_object *obj;
	const struct hisi_dpe_dfc_format *dfc_fmt;
	u32 stride, addr, fmt;
	void __iomem *dma_base;
	void __iomem *ovl_base;
	void __iomem *dfc_base;
	void __iomem *mif_base;
	void __iomem *aif_base;

	if (!fb || !new_state->crtc)
		return;

	if (!hisi_dpe->power_on) {
		int ret = hisi_dpe_power_up(hisi_dpe);

		if (ret) {
			DRM_ERROR("failed to power up DPE\n");
			return;
		}
	}

	obj = drm_fb_dma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get GEM object\n");
		return;
	}

	stride = fb->pitches[0];
	addr = obj->dma_addr + (new_state->src_y >> 16) * stride;
	fmt = hisi_dpe_get_hw_format(fb->format->format);

	dma_base = hisi_dpe->dss_base + DSS_RCH_VG0_DMA_OFFSET;
	dfc_base = hisi_dpe->dss_base + DSS_RCH_VG0_DFC_OFFSET;
	mif_base = hisi_dpe->dss_base + DSS_MIF_VG0_OFFSET;
	aif_base = hisi_dpe->dss_base + DSS_AIF0_VG0_OFFSET;
	ovl_base = hisi_dpe->dss_base + DSS_OVL0_OFFSET;

	/*
	 * AIF (AXI interface) channel control. Android's hisi_dss_aif_ch_config()
	 * writes axi_sel into bit 0 of AIF_CH_CTL on every used channel; for the
	 * primary panel (ovl_idx DSS_OVL0) that is AXI_CHN0 = 0, which is also the
	 * register's reset value. Write it explicitly so the channel is not left
	 * pointing at whichever AXI port a previous framebuffer left selected.
	 */
	dpe_update_bits(aif_base, AIF_CH_CTL, BIT(0), 0);

	/*
	 * MIF (memory interface) for the VG0 read channel, i.e. MIF channel 2.
	 * hisi_dss_mif_on() enables the MIF block and then every channel's
	 * MIF_CTRL0; hisi_dss_mif_config() then fills MIF_CTRL1..5 for the layer.
	 * The primary plane runs with img.mmu_enable = 1 (see
	 * hisi_overlay_online.c), which takes the mmu_enable != 0 branch: bit 5 of
	 * MIF_CTRL1 is cleared, and for an unrotated, unstretched, untiled layer
	 * hisi_dss_mif_get_invalid_sel() returns 0, so bits [11:10] stay 0, bit 19
	 * is set, and MIF_CTRL2..5 stay at their hisi_dss_mif_init() defaults of 0.
	 */
	dpe_write(mif_base, MIF_CTRL0, 0x1);
	dpe_write(mif_base, MIF_CTRL1, 0x80000);
	dpe_write(mif_base, MIF_CTRL2, 0x0);
	dpe_write(mif_base, MIF_CTRL3, 0x0);
	dpe_write(mif_base, MIF_CTRL4, 0x0);
	dpe_write(mif_base, MIF_CTRL5, 0x0);

	DRM_DEBUG_ATOMIC("plane update: src=(%d,%d)-%dx%d crtc=(%d,%d)-%dx%d fmt=0x%x\n",
			 new_state->src_x >> 16, new_state->src_y >> 16,
			 new_state->src_w >> 16, new_state->src_h >> 16,
			 new_state->crtc_x, new_state->crtc_y,
			 new_state->crtc_w, new_state->crtc_h, fmt);

	dfc_fmt = hisi_dpe_get_dfc_format(fb->format->format);
	if (dfc_fmt) {
		/*
		 * DFC (display format converter). Android programs this block on
		 * every layer in hisi_dss_rdfc_config(), between the DMA and MIF
		 * writes in hisi_dss_module_set_regs(). The DMA block above reads
		 * the framebuffer in its own encoding; the DFC re-packs it into the
		 * channel order the rest of the pipeline (and the panel) expects,
		 * which is the only place the R/B swap the byte orders below need
		 * can be expressed. Without it the panel shows swapped red/blue.
		 */
		writel(DSS_HEIGHT(new_state->crtc_h) |
		       (DSS_WIDTH(new_state->crtc_w) << 16),
		       dfc_base + DFC_DISP_SIZE);
		/*
		 * DFC_PIX_IN_NUM is 1 only for the <= 16bpp formats, where the
		 * converter packs two pixels per beat; the primary plane's usual
		 * XRGB8888/ARGB8888 take 0. Mirrors
		 * dfc_pix_in_num = (dfc_bpp <= 2) ? 0x1 : 0x0.
		 */
		writel((fb->format->cpp[0] <= 2) ? 1 : 0, dfc_base + DFC_PIX_IN_NUM);
		/*
		 * DFC_DISP_FMT carries the DFC pixel format in bits [3:1] plus the
		 * U/V and R/B swap bits in bits 6 and 7. Android computes exactly
		 * ((dfc_fmt << 1) | (uv_swap << 6) | (rb_swap << 7)); the primary
		 * plane is never YUV, so uv_swap is always 0 here.
		 */
		writel((dfc_fmt->hw_format << 1) |
		       (dfc_fmt->rb_swap << 7),
		       dfc_base + DFC_DISP_FMT);
		/* No clipping: the plane covers the whole framebuffer. */
		writel(0x0, dfc_base + DFC_CLIP_CTL_HRZ);
		writel(0x0, dfc_base + DFC_CLIP_CTL_VRZ);
		/*
		 * DFC_CTL_CLIP_EN stays 1 even when nothing is clipped - Android
		 * carries a FIX_BUG comment on the else branch that sets it
		 * unconditionally.
		 */
		writel(0x1, dfc_base + DFC_CTL_CLIP_EN);
		writel(0x1, dfc_base + DFC_ICG_MODULE);
		writel(0x0, dfc_base + DFC_DITHER_ENABLE);
		writel(0x0, dfc_base + DFC_PADDING_CTL);
		writel(0x3, dfc_base + DFC_BITEXT_CTL);
	}

	/* DMA channel: CH_REG_DEFAULT reset */
	writel(0x1, dma_base + CH_REG_DEFAULT);
	writel(0x0, dma_base + CH_REG_DEFAULT);

	/*
	 * DMA source window. The DMA engine walks the framebuffer from
	 * (OFT_X0, OFT_Y0) to (OFT_X1, OFT_Y1), all in pixel units and inclusive of
	 * the far corner, so these describe the *read* rectangle rather than the
	 * displayed one - the OVL layer geometry below scales it onto the CRTC.
	 * Android's equivalent is
	 *	oft_x0 = aligned_rect.left / aligned_pixel
	 *	oft_y0 = aligned_rect.top
	 *	oft_x1 = aligned_rect.right / aligned_pixel
	 *	oft_y1 = aligned_rect.bottom
	 * because the hardware wants X offsets in 16-byte beats, not pixels.
	 * Compute them from the byte offset instead of dividing the pixel
	 * coordinate by DMA_ALIGN_BYTES / cpp: the latter is only equivalent
	 * for 16/32bpp and truncates incorrectly for 24bpp, where 16/3 is not
	 * an integer.  atomic_check() guarantees the start is beat-aligned.
	 * Y offsets are in lines (no division).  aligned_rect.bottom =
	 * bottom - DSS_HEIGHT(h), i.e. top + h - 1.
	 */
	{
		u32 cpp = new_state->fb->format->cpp[0];
		u32 x0 = new_state->src_x >> 16;
		u32 x1 = x0 + (new_state->src_w >> 16) - 1;

		writel(x0 * cpp / DMA_ALIGN_BYTES, dma_base + DMA_OFT_X0);
		writel(new_state->src_y >> 16, dma_base + DMA_OFT_Y0);
		writel(x1 * cpp / DMA_ALIGN_BYTES, dma_base + DMA_OFT_X1);
		writel((new_state->src_y >> 16) + (new_state->src_h >> 16) - 1,
		       dma_base + DMA_OFT_Y1);
	}
	/*
	 * DMA_STRETCH_SIZE_VRT carries only a vertical height in bits [18:0]
	 * (Android writes DSS_HEIGHT(h), or stretched_line_num << 13 above it when
	 * line-stretching); the width has no field here, so packing it into bits
	 * [31:16] both shifts the height into an ignored range and sets a bogus
	 * stretched-line count. We never stretch, so just the height. Mirrors
	 * hisi_dss_rdma_config().
	 */
	writel(DSS_HEIGHT(new_state->src_h >> 16),
	       dma_base + DMA_STRETCH_SIZE_VRT);
	/*
	 * DMA channel control. The pixel format lives in bits [7:3] of DMA_CTRL
	 * (soc_dss_interface.h DMA_CTRL, written by both the generic and the
	 * Kirin970 paths of hisi_dss_rdma_config() with
	 * set_bits32(dma->ctrl, rdma_format, 5, 3)). Shifting by any other amount
	 * hands the DMA engine an unknown format and it reads the framebuffer
	 * with the wrong pixel packing.
	 *
	 * Bit 8 is mmu_enable, and it has to agree with the MIF_CTRL1 branch
	 * programmed above: Android's primary framebuffer layer sets
	 * img.mmu_enable = 1 unconditionally (hisi_overlay_online.c:233), which
	 * produces DMA_CTRL[8] = 1 together with MIF_CTRL1 = 0x80000 (the
	 * mmu_enable != 0 branch of hisi_dss_mif_config(), bit 5 clear / bit 19
	 * set). Splitting the two - one for the MMU, one not - leaves the
	 * DMA/MIF pair describing two different address paths.
	 *
	 * The DSS SMMU itself is put in global bypass by
	 * hisi_dpe_inner_clk_enable() so that the physical address handed over
	 * by drm_gem_dma_helper passes through untranslated.
	 */
	writel((fmt << 3) | BIT(8), dma_base + DMA_CTRL);
	writel(addr, dma_base + DMA_DATA_ADDR0);
	/*
	 * DMA_STRIDE0 does not take the byte stride: the hardware wants the
	 * stride in 16-byte units, so Android writes
	 * rdma_stride / DMA_ALIGN_BYTES (DMA_ALIGN_BYTES = 128 / 8 = 16) into
	 * bits [15:0], with the interleave count in bits [19:16]. Passing the raw
	 * byte count makes the DMA engine step 16x too far between rows and the
	 * panel shows a sheared/tiled image. Mirrors hisi_dss_rdma_config().
	 */
	writel(stride / DMA_ALIGN_BYTES, dma_base + DMA_STRIDE0);
	/* Release shadow read */
	writel(0x1, dma_base + CH_RD_SHADOW);
	/* Enable DMA channel: bit0 = enable */
	writel(0x1, dma_base + CH_CTL);

	/*
	 * OV8 block-level registers, written on every frame by
	 * hisi_dss_ovl_set_reg() for any non-OVL3 index on Kirin 970: the shadow
	 * register is pulsed 1 then 0 to commit the block, BLOCK_DBG selects the
	 * AXI port, and BLOCK_SIZE carries the OV block height in bits [30:16]
	 * (hisi_dss_ovl_base_config() computes ov_block_rect.y + DSS_HEIGHT(h)).
	 */
	writel(0x1, ovl_base + OV8_REG_DEFAULT);
	writel(0x0, ovl_base + OV8_REG_DEFAULT);
	writel(0x4, ovl_base + OV8_BLOCK_DBG);
	writel(DSS_HEIGHT(new_state->crtc_y + new_state->crtc_h) << 16,
	       ovl_base + OV8_BLOCK_SIZE);

	/*
	 * OVL: destination window.  Android's register layout puts width (or
	 * x) in bits [14:0] and height (or y) in bits [30:16] - see
	 * hisi_dss_ovl_base_config() ovl_size computation and the
	 * OV_LAYER0_* helpers in hisi_overlay_utils.c:4554/4688/4718.
	 */
	writel(DSS_HEIGHT(new_state->crtc_h) << 16 | DSS_WIDTH(new_state->crtc_w),
	       ovl_base + OV_SIZE);
	writel(0, ovl_base + OV_DST_STARTPOS);
	writel(DSS_HEIGHT(new_state->crtc_h) << 16 | DSS_WIDTH(new_state->crtc_w),
	       ovl_base + OV_DST_ENDPOS);
	/*
	 * OV_GCFG: Android sets both bit 0 and bit 16 from hisi_dss_ovl_base_config()
	 * (ovl_gcfg = set_bits32(..., 0x1, 1, 0); ... 0x1, 1, 16).
	 */
	writel(0x10001, ovl_base + OV_GCFG);

	/*
	 * OV layer 0 geometry. Both OV_LAYER0_SIZE and OV_LAYER0_PEPOS hold the
	 * *destination* rectangle's end corner, not the source rectangle's size:
	 * Android computes DSS_WIDTH(dst_rect.x + dst_rect.w) and
	 * DSS_HEIGHT(dst_rect.y + dst_rect.h). They only coincide with the source
	 * dimensions when the plane is unscaled and its dst origin is 0, which is
	 * all the atomic_check above allows today, but using crtc_* keeps the
	 * intent right and matches hisi_dss_ovl_layer_config().
	 */
	writel(0, ovl_base + OV_LAYER0_POS);
	writel(DSS_HEIGHT(new_state->crtc_y + new_state->crtc_h) << 16 |
	       DSS_WIDTH(new_state->crtc_x + new_state->crtc_w),
	       ovl_base + OV_LAYER0_SIZE);
	/*
	 * Layer 0 blend. OV_LAYER0_ALPHA_MODE (0x50) is the *blend mode* register
	 * on this SoC, not a plain alpha value: the Kirin 970 OVL is the 8-layer
	 * "OV" block, where the same register is called ALPHA_MODE. Writing 0xFF
	 * here sets all eleven mode fields at once, which is not a valid blend
	 * mode and yields a blank or garbled image. An opaque layer with no
	 * per-pixel alpha uses DSS_BLEND_FIX_PER17, 0x01004000, and its global
	 * alpha goes into the separate OV_LAYER0_ALPHA_A (0x54) as
	 * (0xFF * 4) | ((0xFF * 4) << 16) = 0x03FC03FC. Mirrors
	 * hisi_dss_ovl_layer_config().
	 */
	writel(OV_LAYER0_ALPHA_MODE_BLEND,
	       ovl_base + OV_LAYER0_ALPHA_MODE);
	writel(OV_LAYER0_ALPHA_A_FULL,
	       ovl_base + OV_LAYER0_ALPHA_A);
	/*
	 * No colour-key / dim pattern, and the layer reads from the DMA channel
	 * rather than a constant colour (BIT_OVL_LAYER_SRC_CFG, bit 8, stays 0).
	 * Mirrors the else branch of hisi_dss_ovl_layer_config().
	 */
	writel(0, ovl_base + OV_LAYER0_PATTERN_RGB);
	writel(BIT_OVL_LAYER_ENABLE, ovl_base + OV_LAYER0_CFG);
	/* Layer 0 src/dst pos (PSPOS = layer origin, PEPOS = far corner;
	 * x in low 16 bits, y in high 16 bits per Android layout)
	 */
	writel(new_state->crtc_y << 16 | new_state->crtc_x,
	       ovl_base + OV_LAYER0_PSPOS);
	writel(DSS_HEIGHT(new_state->crtc_y + new_state->crtc_h) << 16 |
	       DSS_WIDTH(new_state->crtc_x + new_state->crtc_w),
	       ovl_base + OV_LAYER0_PEPOS);

	/*
	 * Route the VIGOR0 read channel to OVL0. Without this the channel feeds no
	 * overlay and nothing reaches the panel.
	 *
	 * MCTL_RCH_OV0_SEL is a 32-bit register holding one 4-bit field per OVL
	 * layer, and Android fills two of them:
	 *   - hisi_dss_mctl_ov_config()  writes the "ov base pattern" 0xE into
	 *     bits [3:0], the layer-0 slot's *base* selector;
	 *   - hisi_dss_mctl_ch_config()  then writes the channel index into
	 *     bits [7:4], the layer-0 slot's channel selector, as
	 *     set_bits32(chn_ov_sel, chn_idx, 4, (layer_idx + 1) * 4).
	 * VIGOR0 is DSS_RCHN_V0 = 2, so layer 0 needs 0xE | (2 << 4) = 0x2E.
	 * Writing 0xE alone leaves bits [7:4] at 0, which selects channel 0
	 * (DSS_RCHN_D2) - a channel this driver never programs - so the overlay
	 * layer reads nothing and the panel stays blank.
	 */
	dpe_write(hisi_dpe->dss_base, DSS_MCTRL_SYS_OFFSET + MCTL_RCH_OV0_SEL,
		  0xE | (DSS_RCHN_V0 << 4));

	/*
	 * MCTL mutexes. hisi_dss_mctl_ov_config() takes the OVL mutex for the
	 * interface (OVL0 -> 1), the data buffer and the overlay itself, and
	 * hisi_dss_mctl_ch_config() takes the per-channel mutex for VIGOR0
	 * (MCTL RCH2) so the channel can hold the OVL while it streams. Without
	 * the RCH2 mutex the flush below has nothing to arbitrate against and the
	 * frame can be dropped mid-scanout.
	 */
	dpe_write(hisi_dpe->dss_base,
		  DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_MUTEX_ITF, 0x1);
	dpe_write(hisi_dpe->dss_base,
		  DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_MUTEX_DBUF, 0x1);
	dpe_write(hisi_dpe->dss_base,
		  DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_MUTEX_OV, 0x1);
	dpe_write(hisi_dpe->dss_base,
		  DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_MUTEX_RCH2, 0x1);

	/*
	 * MCTL_RCH2_OV_OEN: output-enable bitmask telling the MCTL which OVL
	 * this read channel composes into. hisi_dss_mctl_ch_config() sets
	 * chn_ov_oen = 0x100 << ovl_idx for the primary panel; OVL0 is
	 * index 0 so the value is 0x100 (bit 8). Without this the channel's
	 * output is never enabled at the MCTL level and nothing reaches the
	 * data buffer.
	 */
	dpe_write(hisi_dpe->dss_base,
		  DSS_MCTRL_SYS_OFFSET + MCTL_RCH2_OV_OEN, 0x100);

	/*
	 * Flush the pipeline. Each block has its own flush-enable register under
	 * DSS_MCTRL_SYS_OFFSET (see g_dss_module_base[][MODULE_MCTL_CHN_FLUSH_EN]
	 * and hisi_dss_mctl_sys_set_reg in the Android driver). VIGOR0 is MCTL
	 * RCH2, so it flushes via MCTL_RCH2_FLUSH_EN with value 0x1; OVL0 is the
	 * first OVL block, and as the first block of the composition it flushes
	 * with 0xd.
	 *
	 * MCTL_CTL_TOP is NOT in the SYS block: it belongs to the MCTL_CTL0 block
	 * at DSS_MCTRL_CTL0_OFFSET (MCTL_CTL_TOP is 0x0050 into
	 * g_dss_module_ovl_base[][MODULE_MCTL_BASE], which is DSS_MCTRL_CTL0_OFFSET
	 * for OVL0). Writing it at DSS_MCTRL_SYS_OFFSET + 0x0050 instead hits
	 * MCTL_SYS_CHN_OV_OEN0. Android writes 0x2 for the primary panel
	 * (hisi_dss_mctl_ov_set_ctl_dbg_reg).
	 */
	dpe_write(hisi_dpe->dss_base, DSS_MCTRL_SYS_OFFSET + MCTL_RCH2_FLUSH_EN,
		  0x1);
	dpe_write(hisi_dpe->dss_base, DSS_MCTRL_SYS_OFFSET + MCTL_OV0_FLUSH_EN,
		  0xd);
	dpe_write(hisi_dpe->dss_base, DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_TOP, 0x2);
	/*
	 * MCTL_CTL_DBG selects the command-list vs. immediate (non-cmdlist) path
	 * and the primary-vs-external panel; mainline always runs the immediate
	 * primary path, matching the non-cmdlist PRIMARY_PANEL_IDX branch of
	 * hisi_dss_mctl_ov_set_ctl_dbg_reg().
	 */
	dpe_write(hisi_dpe->dss_base, DSS_MCTRL_CTL0_OFFSET + MCTL_CTL_DBG,
		  0xB13A00);
}

void hisi_dpe_plane_disable(struct hisi_dpe_device *hisi_dpe)
{
	if (!READ_ONCE(hisi_dpe->power_on))
		return;

	/* Disable OV layer 0 */
	writel(0, hisi_dpe->dss_base + DSS_OVL0_OFFSET + OV_LAYER0_CFG);

	/* Disable DMA channel */
	writel(0, hisi_dpe->dss_base + DSS_RCH_VG0_DMA_OFFSET + CH_CTL);
}

static void hisi_dpe_plane_atomic_disable(struct drm_plane *plane,
					  struct drm_atomic_commit *state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);

	/*
	 * commit_tail() has already swapped the new state into plane->state,
	 * so plane->state->crtc is NULL here and cannot be used to find the
	 * DSS instance. The old state still points at the CRTC being
	 * disabled; without it the DMA channel and OVL layer are never
	 * stopped when only the plane is turned off while the CRTC stays on.
	 */
	if (!old_state || !old_state->crtc)
		return;

	DRM_DEBUG_ATOMIC("plane disable\n");
	hisi_dpe_plane_disable(to_hisi_dpe_crtc(old_state->crtc));
}

static const struct drm_plane_helper_funcs hisi_dpe_plane_helper_funcs = {
	.atomic_check = hisi_dpe_plane_atomic_check,
	.atomic_update = hisi_dpe_plane_atomic_update,
	.atomic_disable = hisi_dpe_plane_atomic_disable,
};

static struct drm_plane_funcs hisi_dpe_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

int hisi_dpe_plane_init(struct drm_device *drm,
			struct hisi_dpe_device *hisi_dpe)
{
	int ret;

	ret = drm_universal_plane_init(drm, &hisi_dpe->primary,
				       1,
				       &hisi_dpe_plane_funcs,
				       hisi_dpe_formats,
				       ARRAY_SIZE(hisi_dpe_formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		DRM_ERROR("failed to init primary plane\n");
		return ret;
	}

	drm_plane_helper_add(&hisi_dpe->primary,
			     &hisi_dpe_plane_helper_funcs);

	return 0;
}
