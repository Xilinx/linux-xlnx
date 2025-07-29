// SPDX-License-Identifier: GPL-2.0
/*
 * MMI Display Controller Non-Live Vide Plane Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc.h"
#include "mmi_dc_dma.h"
#include "mmi_dc_plane.h"

#include <drm/drm_blend.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

/* ----------------------------------------------------------------------------
 * DC HW Registers
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
#define MMI_DC_AV_BUF_PLANE_CC_SCALE_FACTOR(layer, cc)	(0x0200 + 0x0c * \
							 MMI_DC_SWAP(layer) + 4 * (cc))

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

/* ----------------------------------------------------------------------------
 * DC Video Plane Misc Defines
 */

#define MMI_DC_MAX_NUM_SUB_PLANES	(MMI_DC_NUM_CC)
#define MMI_DC_SWAP(layer)		(MMI_DC_NUM_PLANES - 1 - (layer))

/* ----------------------------------------------------------------------------
 * DC Video Plane Data
 */

/**
 * struct mmi_dc_format - DC HW config format data
 * @drm_format: DRM fourcc format
 * @buf_format: internal DC pixel format
 * @swap: swap color (U/V or R/B) channels
 * @sf: CSC scaling factors (4,5,6,8 or 10 bpc to 12 bpc)
 */
struct mmi_dc_format {
	u32		drm_format;
	u32		buf_format;
	bool		swap;
	const u32	*sf;
};

/**
 * struct mmi_dc_format_info - Cached DRM format info / HW format pair
 * @drm: DRM format info
 * @hw: DC format info
 */
struct mmi_dc_format_info {
	const struct drm_format_info	*drm;
	const struct mmi_dc_format	*hw;
};

/**
 * struct mmi_dc_video_plane - DC non-live video plane
 * @base: generic MMI DC plane
 * @format: active pixel format
 * @dmas: DMA channels used
 */
struct mmi_dc_video_plane {
	struct mmi_dc_plane		base;
	struct mmi_dc_format_info	format;
	struct mmi_dc_dma_chan		*dmas[MMI_DC_MAX_NUM_SUB_PLANES];
};

/* TODO: more scaling factors */
static const u32 scaling_factors_888[] = {
	MMI_DC_AV_BUF_8BIT_SF,
	MMI_DC_AV_BUF_8BIT_SF,
	MMI_DC_AV_BUF_8BIT_SF,
};

/* TODO: more formats */
static const struct mmi_dc_format video_plane_formats[] = {
	{
		.drm_format	= DRM_FORMAT_VYUY,
		.buf_format	= MMI_DC_AV_BUF_FMT_CR_Y0_CB_Y1,
		.swap		= true,
		.sf		= scaling_factors_888,
	},
	{
		.drm_format	= DRM_FORMAT_YUYV,
		.buf_format	= MMI_DC_AV_BUF_FMT_Y0_CB_Y1_CR,
		.swap		= true,
		.sf		= scaling_factors_888,
	},
	{
		.drm_format	= DRM_FORMAT_YUV444,
		.buf_format	= MMI_DC_AV_BUF_FMT_YV24,
		.swap		= false,
		.sf		= scaling_factors_888,
	},
	{
		.drm_format	= DRM_FORMAT_XRGB8888,
		.buf_format	= MMI_DC_AV_BUF_FMT_RGBA8888,
		.swap		= true,
		.sf		= scaling_factors_888,
	},
	{
		.drm_format	= DRM_FORMAT_RGB888,
		.buf_format	= MMI_DC_AV_BUF_FMT_RGB888,
		.swap		= true,
		.sf		= scaling_factors_888,
	},
	{
		.drm_format	= DRM_FORMAT_NV12,
		.buf_format	= MMI_DC_AV_BUF_FMT_YV16CI_420,
		.swap		= false,
		.sf		= scaling_factors_888,
	},
};

/* ----------------------------------------------------------------------------
 * DC Blender Ops
 */

/**
 * mmi_dc_blend_plane_set_csc - Set input CSC
 * @plane: DC video plane
 * @coeffs: CSC matrix coefficients
 * @offsets: CSC color offsets
 *
 * Setup input color space converter.
 */
static void mmi_dc_blend_plane_set_csc(struct mmi_dc_video_plane *plane,
				       const u16 *coeffs, const u32 *offsets)
{
	struct mmi_dc *dc = plane->base.dc;
	unsigned int i, reg, swap[] = { 0, 1, 2 };

	if (plane->format.hw && plane->format.hw->swap) {
		if (plane->format.drm->is_yuv) {
			/* swap U and V */
			swap[1] = 2;
			swap[2] = 1;
		} else {
			/* swap R and B */
			swap[0] = 2;
			swap[2] = 0;
		}
	}

	for (i = 0; i < MMI_DC_CSC_NUM_COEFFS; ++i) {
		reg = MMI_DC_V_BLEND_INCSC_COEFF(plane->base.id, i);
		dc_write_blend(dc, reg, coeffs[i - i % 3 + swap[i % 3]]);
	}

	for (i = 0; i < MMI_DC_CSC_NUM_OFFSETS; ++i) {
		reg = MMI_DC_V_BLEND_CC_INCSC_OFFSET(plane->base.id, i);
		dc_write_blend(dc, reg, offsets[i]);
	}
}

/**
 * mmi_dc_blend_plane_enable - Enable blender input for given DC plane
 * @plane: DC video plane
 */
static void mmi_dc_blend_plane_enable(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;
	const u16 *coeffs;
	const u32 *offsets;
	u32 val;

	val = (plane->format.drm->is_yuv ? 0 : MMI_DC_V_BLEND_RGB_MODE) |
	      (plane->format.drm->hsub > 1 ? MMI_DC_V_BLEND_EN_US : 0);

	dc_write_blend(dc, MMI_DC_V_BLEND_LAYER_CONTROL(plane->base.id), val);

	if (plane->format.drm->is_yuv) {
		coeffs = csc_sdtv_to_rgb_matrix;
		offsets = csc_sdtv_to_rgb_offsets;
	} else {
		coeffs = csc_identity_matrix;
		offsets = csc_zero_offsets;
	}

	mmi_dc_blend_plane_set_csc(plane, coeffs, offsets);
}

/**
 * mmi_dc_blend_plane_disable - Disable blender input for given DC plane
 * @plane: DC video plane
 */
static void mmi_dc_blend_plane_disable(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;

	dc_write_blend(dc, MMI_DC_V_BLEND_LAYER_CONTROL(plane->base.id), 0);
	mmi_dc_blend_plane_set_csc(plane, csc_zero_matrix, csc_zero_offsets);
}

/* ----------------------------------------------------------------------------
 * DC AV Buffer Ops
 */

/**
 * mmi_dc_avbuf_plane_set_format - Set AVBUF format
 * @plane: DC video plane
 *
 * Configure the audio/video buffer manager according to the given pixel format
 */
static void mmi_dc_avbuf_plane_set_format(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;
	u32 val, i;

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_FORMAT);
	val &= ~MMI_DC_AV_BUF_FMT_MASK(plane->base.id);
	val |= plane->format.hw->buf_format <<
		MMI_DC_AV_BUF_FMT_SHIFT(plane->base.id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_FORMAT, val);

	for (i = 0; i < MMI_DC_NUM_CC; ++i) {
		u32 reg = MMI_DC_AV_BUF_PLANE_CC_SCALE_FACTOR(plane->base.id,
							      i);

		dc_write_avbuf(dc, reg, plane->format.hw->sf[i]);
	}
}

/**
 * mmi_dc_avbuf_plane_enable - Enable AV buffer input for the given plane
 * @plane: DC video plane
 */
static void mmi_dc_avbuf_plane_enable(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;
	u32 val;
	int ch;

	for (ch = 0; ch < plane->format.drm->num_planes; ++ch)
		dc_write_avbuf(dc, MMI_DC_AV_CHBUF(plane->base.id * 3 + ch),
			       MMI_DC_AV_CHBUF_EN | MMI_DC_AV_CHBUF_BURST);

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~MMI_DC_AV_BUF_VID_STREAM_SEL_MASK(plane->base.id);
	val |= MMI_DC_AV_BUF_VID_STREAM_SEL_MEM(plane->base.id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);
}

/**
 * mmi_dc_avbuf_plane_disable - Disable AV buffer input for the given plane
 * @plane: DC video plane
 */
static void mmi_dc_avbuf_plane_disable(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;
	u32 val;
	int ch;

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~MMI_DC_AV_BUF_VID_STREAM_SEL_MASK(plane->base.id);
	val |= MMI_DC_AV_BUF_VID_STREAM_SEL_NONE(plane->base.id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);

	for (ch = 0; ch < plane->format.drm->num_planes; ++ch)
		dc_write_avbuf(dc, MMI_DC_AV_CHBUF(plane->base.id * 3 + ch),
			       MMI_DC_AV_CHBUF_FLUSH);
}

/* ----------------------------------------------------------------------------
 * DC Video Plane Utils
 */

/**
 * to_video_plane - Convert generic MMI DC plane to video plane
 * @plane: MMI DC plane
 *
 * Return: Corresponding video plane.
 */
static inline struct mmi_dc_video_plane *
to_video_plane(struct mmi_dc_plane *plane)
{
	return container_of(plane, struct mmi_dc_video_plane, base);
}

/**
 * mmi_dc_planes_get_dma_align - Get DMA align
 * @dc: DC device
 *
 * Return: DC DMA alignment constraint.
 */
unsigned int mmi_dc_planes_get_dma_align(struct mmi_dc *dc)
{
	struct mmi_dc_plane *plane = dc->planes[MMI_DC_PLANE1];
	struct mmi_dc_video_plane *video_plane = to_video_plane(plane);

	return mmi_dc_dma_copy_align(video_plane->dmas[0]);
}

/**
 * mmi_dc_video_plane_request_dma - Request DMA channels for the plane
 * @plane: DC video plane
 *
 * Request DMA channels for each sub-plane.
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dc_video_plane_request_dma(struct mmi_dc_video_plane *plane)
{
	struct mmi_dc *dc = plane->base.dc;
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_CC; ++i) {
		struct mmi_dc_dma_chan *dma_chan;
		char dma_chan_name[32];

		snprintf(dma_chan_name, sizeof(dma_chan_name), "vid.%u.%u",
			 plane->base.id, i);
		dma_chan = mmi_dc_dma_request_channel(dc->dev, dma_chan_name);
		if (IS_ERR(dma_chan))
			return dev_err_probe(dc->dev, PTR_ERR(dma_chan),
					     "failed to request dma channel");
		plane->dmas[i] = dma_chan;
	}

	return 0;
}

/**
 * mmi_dc_video_plane_release_dma - Release assigned DMA channels
 * @plane: DC video plane
 */
static void mmi_dc_video_plane_release_dma(struct mmi_dc_video_plane *plane)
{
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_CC; ++i) {
		struct mmi_dc_dma_chan *dma_chan = plane->dmas[i];

		if (dma_chan) {
			mmi_dc_dma_stop_transfer(dma_chan);
			mmi_dc_dma_release_channel(dma_chan);
		}
	}
}

/**
 * mmi_dc_video_plane_submit_dma - Submit DMA transfer
 * @plane: DC plane
 * @state: DRM plane state to update to
 *
 * Prepare and submit DMA transfers.
 */
static void mmi_dc_video_plane_submit_dma(struct mmi_dc_video_plane *plane,
					  struct drm_plane_state *state)
{
	const struct drm_format_info *info = plane->format.drm;
	unsigned int i;

	for (i = 0; i < info->num_planes; ++i) {
		size_t width = state->crtc_w / (i ? info->hsub : 1);
		size_t height = state->crtc_h / (i ? info->vsub : 1);
		size_t pitch = state->fb->pitches[i];
		size_t size = width * info->cpp[i];
		struct mmi_dc_dma_chan *dma_chan = plane->dmas[i];
		dma_addr_t src_addr =
			drm_fb_dma_get_gem_addr(state->fb, state, i);

		mmi_dc_dma_start_transfer(dma_chan, src_addr, size, pitch,
					  height, true);
	}
}

/**
 * mmi_dc_video_plane_find_format - Find video plane format
 * @plane: DC video plane
 * @drm_format: format to lookup for
 *
 * Return: Found DC pixel format corresponding to the given DRM format, or
 *         NULL.
 */
static const struct mmi_dc_format *
mmi_dc_video_plane_find_format(struct mmi_dc_video_plane *plane,
			       u32 drm_format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(video_plane_formats); ++i) {
		if (video_plane_formats[i].drm_format == drm_format)
			return &video_plane_formats[i];
	}

	return NULL;
}

/**
 * mmi_dc_video_plane_set_format - Set video plane DRM format
 * @plane: DC video plane
 * @info: DRM format description
 *
 * Set DRM format, program blender, and AV buffer manager accordingly.
 */
static void mmi_dc_video_plane_set_format(struct mmi_dc_video_plane *plane,
					  const struct drm_format_info *info)
{
	unsigned int i;

	plane->format.drm = info;
	plane->format.hw = mmi_dc_video_plane_find_format(plane, info->format);
	if (WARN_ON(!plane->format.hw))
		return;

	mmi_dc_avbuf_plane_set_format(plane);

	for (i = 0; i < info->num_planes; ++i)
		mmi_dc_dma_config_channel(plane->dmas[i], 0, true);
}

/**
 * mmi_dc_drm_video_plane_create - Allocate and initialize DRM plane
 * @drm: DRM device
 * @type: DRM plane type
 * @id: DC plane id
 *
 * Return: Allocated DC video plane on success or error pointer otherwise.
 */
static struct mmi_dc_video_plane *
mmi_dc_drm_video_plane_create(struct drm_device *drm,
			      enum drm_plane_type type,
			      enum mmi_dc_plane_id id)
{
	static u32 drm_formats[ARRAY_SIZE(video_plane_formats)] = {};
	struct mmi_dc_video_plane *plane;

	/* Lazy initialize the format list. Reuse it for multiple planes */
	if (drm_formats[0] == 0) {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(drm_formats); ++i)
			drm_formats[i] = video_plane_formats[i].drm_format;
	}

	plane = drmm_universal_plane_alloc(drm, struct mmi_dc_video_plane,
					   base.base, 0,
					   &mmi_dc_drm_plane_funcs,
					   &drm_formats[0],
					   ARRAY_SIZE(drm_formats),
					   NULL, type, NULL);
	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(&plane->base.base,
			     &mmi_dc_drm_plane_helper_funcs);

	drm_plane_create_zpos_immutable_property(&plane->base.base, id);

	return plane;
}

/* ----------------------------------------------------------------------------
 * DC Plane Interface Implementation
 */

static void mmi_dc_video_plane_destroy(struct mmi_dc_plane *plane)
{
	struct mmi_dc_video_plane *video_plane = to_video_plane(plane);

	mmi_dc_video_plane_release_dma(video_plane);
}

static int mmi_dc_video_plane_check(struct mmi_dc_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, &plane->base);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_crtc_state(state, plane_state->crtc);

	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void mmi_dc_video_plane_update(struct mmi_dc_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane *drm_plane = &plane->base;
	struct mmi_dc_video_plane *video_plane = to_video_plane(plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, drm_plane);

	if (!new_state->fb)
		return;

	/* Actual FB format changed - we have to reset DC */
	if (video_plane->format.hw &&
	    new_state->fb->format->format !=
	    video_plane->format.hw->drm_format)
		plane->dc->reconfig_hw = true;

	if (plane->dc->reconfig_hw)
		return;

	if (!video_plane->format.hw || new_state->fb != old_state->fb) {
		mmi_dc_video_plane_set_format(video_plane,
					      new_state->fb->format);
		mmi_dc_avbuf_plane_enable(video_plane);
		mmi_dc_blend_plane_enable(video_plane);
		mmi_dc_video_plane_submit_dma(video_plane, new_state);
	}
}

static void mmi_dc_video_plane_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc_video_plane *video_plane = to_video_plane(plane);
	int i;

	for (i = 0; i < video_plane->format.drm->num_planes; ++i)
		mmi_dc_dma_stop_transfer(video_plane->dmas[i]);

	mmi_dc_avbuf_plane_disable(video_plane);
	mmi_dc_blend_plane_disable(video_plane);
}

static void mmi_dc_video_plane_reset(struct mmi_dc_plane *plane)
{
	struct mmi_dc_video_plane *video_plane = to_video_plane(plane);

	video_plane->format.hw = NULL;
}

/* ----------------------------------------------------------------------------
 * DC Plane Interface Overrides
 */

static void mmi_dc_overlay_plane_update(struct mmi_dc_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, &plane->base);

	mmi_dc_set_global_alpha(plane->dc, plane_state->alpha >> 8, true);
	mmi_dc_video_plane_update(plane, state);
}

static void mmi_dc_overlay_plane_disable(struct mmi_dc_plane *plane)
{
	mmi_dc_video_plane_disable(plane);
	mmi_dc_set_global_alpha(plane->dc, 0, false);
}

/* ----------------------------------------------------------------------------
 * DC Video Plane Factory
 */

/**
 * mmi_dc_video_plane_create - Create and initialize generic DC video plane
 * @dc: pointer to MMI DC
 * @drm: DRM device
 * @id: DC plane id
 * @type: DRM plane type
 *
 * Return: New DC video plane on success or error pointer otherwise.
 */
static struct mmi_dc_video_plane *
mmi_dc_video_plane_create(struct mmi_dc *dc, struct drm_device *drm,
			  enum mmi_dc_plane_id id, enum drm_plane_type type)
{
	struct mmi_dc_video_plane *plane;
	int ret;

	if (id > MMI_DC_PLANE1)
		return ERR_PTR(-EINVAL);

	plane = mmi_dc_drm_video_plane_create(drm, type, id);
	if (IS_ERR(plane))
		return plane;

	plane->base.id = id;
	plane->base.dc = dc;
	plane->base.funcs.destroy = mmi_dc_video_plane_destroy;
	plane->base.funcs.check = mmi_dc_video_plane_check;
	plane->base.funcs.update = mmi_dc_video_plane_update;
	plane->base.funcs.disable = mmi_dc_video_plane_disable;
	plane->base.funcs.reset = mmi_dc_video_plane_reset;

	ret = mmi_dc_video_plane_request_dma(plane);
	if (ret < 0)
		return ERR_PTR(ret);

	return plane;
}

/**
 * mmi_dc_create_primary_plane - Create and initialize primary DC plane
 * @dc: pointer to MMI DC
 * @drm: DRM device
 * @id: DC plane id
 *
 * Return: New primary DC plane on success or error pointer otherwise.
 */
struct mmi_dc_plane *mmi_dc_create_primary_plane(struct mmi_dc *dc,
						 struct drm_device *drm,
						 enum mmi_dc_plane_id id)
{
	struct mmi_dc_video_plane *plane =
		mmi_dc_video_plane_create(dc, drm, id, DRM_PLANE_TYPE_PRIMARY);

	if (IS_ERR(plane))
		return (void *)plane;

	return &plane->base;
}

/**
 * mmi_dc_create_overlay_plane - Create and initialize overlay DC plane
 * @dc: pointer to MMI DC
 * @drm: DRM device
 * @id: DC plane id
 *
 * Return: New overlay DC plane on success or error pointer otherwise.
 */
struct mmi_dc_plane *mmi_dc_create_overlay_plane(struct mmi_dc *dc,
						 struct drm_device *drm,
						 enum mmi_dc_plane_id id)
{
	struct mmi_dc_video_plane *plane =
		mmi_dc_video_plane_create(dc, drm, id, DRM_PLANE_TYPE_OVERLAY);

	if (IS_ERR(plane))
		return (void *)plane;

	drm_plane_create_alpha_property(&plane->base.base);

	plane->base.funcs.update = mmi_dc_overlay_plane_update;
	plane->base.funcs.disable = mmi_dc_overlay_plane_disable;

	return  &plane->base;
}
