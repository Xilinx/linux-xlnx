/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DC_H__
#define __MMI_DC_H__

#include <linux/device.h>
#include <drm/drm_modes.h>

#define MMI_DC_NUM_PLANES		(3)
#define MMI_DC_NUM_CC			(3)
#define MMI_DC_CURSOR_WIDTH		(128)
#define MMI_DC_CURSOR_HEIGHT		(128)

#define MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT		(0x0070)

/* ----------------------------------------------------------------------------
 * CSC Data
 */

#define MMI_DC_CSC_NUM_COEFFS		(MMI_DC_NUM_CC * MMI_DC_NUM_CC)
#define MMI_DC_CSC_NUM_OFFSETS		(MMI_DC_NUM_CC)

extern const u16 csc_zero_matrix[MMI_DC_CSC_NUM_COEFFS];
extern const u16 csc_identity_matrix[MMI_DC_CSC_NUM_COEFFS];
extern const u16 csc_rgb_to_sdtv_matrix[MMI_DC_CSC_NUM_COEFFS];
extern const u16 csc_sdtv_to_rgb_matrix[MMI_DC_CSC_NUM_COEFFS];

extern const u32 csc_zero_offsets[MMI_DC_CSC_NUM_OFFSETS];
extern const u32 csc_rgb_to_sdtv_offsets[MMI_DC_CSC_NUM_OFFSETS];
extern const u32 csc_sdtv_to_rgb_offsets[MMI_DC_CSC_NUM_OFFSETS];

/**
 * enum mmi_dc_out_format - MMI DC output formats
 * @MMI_DC_FORMAT_RGB: RGB output
 * @MMI_DC_FORMAT_YCBCR444: non-subsampled YCbCr output
 * @MMI_DC_FORMAT_YCBCR422: 422 subsampled YCbCr output
 * @MMI_DC_FORMAT_YONLY: luma only (greyscale) output
 */
enum mmi_dc_out_format {
	MMI_DC_FORMAT_RGB,
	MMI_DC_FORMAT_YCBCR444,
	MMI_DC_FORMAT_YCBCR422,
	MMI_DC_FORMAT_YONLY,
};

struct mmi_dc_drm;
struct mmi_dc_plane;
struct mmi_audio;

/**
 * struct mmi_dc - MMI DC device
 * @dev: generic device
 * @drm: MMI DC specific DRM data
 * @planes: DC planes
 * @dma_align: DMA alignment
 * @reconfig_hw: reset and reconfig HW in crtc flush callback
 * @dp: output to DP Tx control registers space
 * @blend: blender control register space
 * @avbuf: AV buffer manager control register space
 * @misc: misc control register space
 * @irq: interrupt control register space
 * @rst: external reset
 * @pl_pixel_clk: PL pixel clock pl_dc2x or pl_dc1x
 * @ps_pixel_clk: PS pixel clock mmi_aux0_ref_clk
 * @aud_clk: audio clock
 * @audio: Audio data
 * @irq_num: interrupt lane number
 */
struct mmi_dc {
	struct device		*dev;
	struct mmi_dc_drm	*drm;

	struct mmi_dc_plane	*planes[MMI_DC_NUM_PLANES];
	unsigned int		dma_align;
	bool			reconfig_hw;

	void __iomem		*dp;
	void __iomem		*blend;
	void __iomem		*avbuf;
	void __iomem		*misc;
	void __iomem		*irq;
	struct reset_control	*rst;
	struct clk		*pl_pixel_clk;
	struct clk		*ps_pixel_clk;
	struct clk		*aud_clk;
	struct mmi_audio	*audio;
	int			irq_num;
};

#define DEFINE_REGISTER_OPS(iomem)					\
static inline __maybe_unused u32					\
dc_read_##iomem(struct mmi_dc *dc, u32 reg)				\
{									\
	return readl(dc->iomem + reg);					\
}									\
static inline __maybe_unused void					\
dc_write_##iomem(struct mmi_dc *dc, u32 reg, u32 val)			\
{									\
	writel(val, dc->iomem + reg);					\
}									\

DEFINE_REGISTER_OPS(dp);
DEFINE_REGISTER_OPS(blend);
DEFINE_REGISTER_OPS(avbuf);
DEFINE_REGISTER_OPS(misc);
DEFINE_REGISTER_OPS(irq);

/**
 * enum mmi_dc_vid_clk_src: Source of video clock for the MMI DC
 *
 * @MMIDC_AUX0_REF_CLK : PS pixel clock source
 * @MMIDC_PL_CLK : PL pixel clock source
 * @MMIDC_VID_CLK_SRC_COUNT : Count of video clock source enums
 */

enum mmi_dc_vid_clk_src {
	MMIDC_AUX0_REF_CLK,
	MMIDC_PL_CLK,
	MMIDC_VID_CLK_SRC_COUNT
};

void mmi_dc_set_global_alpha(struct mmi_dc *dc, u8 alpha, bool enable);
void mmi_dc_enable_vblank(struct mmi_dc *dc);
void mmi_dc_disable_vblank(struct mmi_dc *dc);
void mmi_dc_enable(struct mmi_dc *dc, struct drm_display_mode *mode);
void mmi_dc_disable(struct mmi_dc *dc);
int mmi_dc_init(struct mmi_dc *dc, struct drm_device *drm);
void mmi_dc_fini(struct mmi_dc *dc);
void mmi_dc_reset_hw(struct mmi_dc *dc);
void mmi_dc_drm_handle_vblank(struct mmi_dc_drm *drm);
int mmi_dc_set_vid_clk_src(struct mmi_dc *dc, enum mmi_dc_vid_clk_src vidclksrc);
enum mmi_dc_vid_clk_src mmi_dc_get_vid_clk_src(struct mmi_dc *dc);

#endif /* __MMI_DC_H__ */
