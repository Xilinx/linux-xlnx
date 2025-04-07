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
#include <drm/drm_plane.h>

#define MMI_DC_NUM_PLANES		(2)
#define MMI_DC_MAX_NUM_SUB_PLANES	(3)
#define MMI_DC_VBLANKS			(3)
#define MMI_DC_DPTX_PORT_0		(12)
#define MMI_DC_MAX_WIDTH		(4096)
#define MMI_DC_MAX_HEIGHT		(4096)

/* ----------------------------------------------------------------------------
 * CSC Data
 */

#define MMI_DC_CSC_NUM_COEFFS		(9)
#define MMI_DC_CSC_NUM_OFFSETS		(3)

/* ----------------------------------------------------------------------------
 * MMI DC Plane Interface
 */

/* Blender Registers */
#define MMI_DC_V_BLEND_LAYER_CONTROL(layer)		(0x0018 + 4 * (layer))
#define MMI_DC_V_BLEND_INCSC_COEFF(layer, coeff)	(0x0044 + 0x3c * \
							(layer) + 4 * (coeff))
#define MMI_DC_V_BLEND_CC_INCSC_OFFSET(layer, cc)	(0x0068 + 0x3c * \
							(layer) + 4 * (cc))

#define MMI_DC_V_BLEND_RGB_MODE				BIT(1)
#define MMI_DC_V_BLEND_EN_US				BIT(0)

/* AV Buffer Registers */
#define MMI_DC_AV_BUF_FORMAT				(0)
#define MMI_DC_AV_CHBUF(channel)			(0x0010 + 4 * (channel))
#define MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT		(0x0070)
#define MMI_DC_AV_BUF_PLANE_CC_SCALE_FACTOR(layer, cc)	(0x0200 + 0x0c * \
							(layer) + 4 * (cc))

#define MMI_DC_AV_CHBUF_BURST				(0x000f << 2)
#define MMI_DC_AV_CHBUF_FLUSH				BIT(1)
#define MMI_DC_AV_CHBUF_EN				BIT(0)

#define MMI_DC_AV_BUF_FMT_CR_Y0_CB_Y1			(1)
#define MMI_DC_AV_BUF_FMT_Y0_CB_Y1_CR			(3)
#define MMI_DC_AV_BUF_FMT_YV24				(5)

#define MMI_DC_AV_BUF_FMT_RGB888			(10)
#define MMI_DC_AV_BUF_FMT_YV16CI_420			(20)
#define MMI_DC_AV_BUF_FMT_RGBA8888			(32)

#define MMI_DC_AV_BUF_FMT_SHIFT(layer)			(8 * (layer))
#define MMI_DC_AV_BUF_FMT_MASK(layer)			(0xff << \
							 MMI_DC_AV_BUF_FMT_SHIFT(layer))
#define MMI_DC_AV_BUF_VID_STREAM_SEL_MASK(layer)	(0x0003 << 2 * (layer))
#define MMI_DC_AV_BUF_VID_STREAM_SEL_MEM(layer)		(0x0001 << 2 * (layer))
#define MMI_DC_AV_BUF_VID_STREAM_SEL_NONE(layer)	(0x0003 << 2 * (layer))
#define MMI_DC_AV_BUF_8BIT_SF				(0x00010101)
#define MMI_DC_AV_BUF_NUM_SF				(9)

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

/**
 * struct mmi_dc - MMI DC device
 * @dev: generic device
 * @drm: MMI DC specific DRM data
 * @planes: DC planes
 * @dma_align: DMA alignment
 * @dp: output to DP Tx control registers space
 * @blend: blender control register space
 * @avbuf: AV buffer manager control register space
 * @misc: misc control register space
 * @irq: interrupt control register space
 * @irq_num: interrupt lane number
 */
struct mmi_dc {
	struct device		*dev;
	struct mmi_dc_drm	*drm;

	struct mmi_dc_plane	*planes[MMI_DC_NUM_PLANES];
	unsigned int		dma_align;

	void __iomem		*dp;
	void __iomem		*blend;
	void __iomem		*avbuf;
	void __iomem		*misc;
	void __iomem		*irq;
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

void mmi_dc_set_global_alpha(struct mmi_dc *dc, u8 alpha, bool enable);
void mmi_dc_enable_vblank(struct mmi_dc *dc);
void mmi_dc_disable_vblank(struct mmi_dc *dc);
void mmi_dc_enable(struct mmi_dc *dc, struct drm_display_mode *mode);
void mmi_dc_disable(struct mmi_dc *dc);
int mmi_dc_init(struct mmi_dc *dc, struct drm_device *drm);
void mmi_dc_fini(struct mmi_dc *dc);

void mmi_dc_drm_handle_vblank(struct mmi_dc_drm *drm);
struct drm_plane *mmi_dc_plane_get_primary(struct mmi_dc *dc);
void mmi_dc_planes_set_possible_crtc(struct mmi_dc *dc, u32 crtc_mask);
unsigned int mmi_dc_planes_get_dma_align(struct mmi_dc *dc);
int mmi_dc_create_planes(struct mmi_dc *dc, struct drm_device *drm);
void mmi_dc_destroy_planes(struct mmi_dc *dc);

#endif /* __MMI_DC_H__ */
