// SPDX-License-Identifier: GPL-2.0
/*
 * MMI Display Controller Plane Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dpdma.h>

#include "mmi_dc.h"

/* ----------------------------------------------------------------------------
 * DC Plane Data
 */

/**
 * enum mmi_dc_plane_id - MMI DC plane id
 * @MMI_DC_PLANE0: video/graphics plane 0 (zorder based)
 * @MMI_DC_PLANE1: video/graphics plane 1
 * @MMI_DC_CURSOR: hardware cursor plane
 */
enum mmi_dc_plane_id {
	MMI_DC_PLANE0,
	MMI_DC_PLANE1,
	MMI_DC_CURSOR,
};

/**
 * struct mmi_dc_format - DC format
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

/* TODO: more scaling factors */
static const u32 scaling_factors_888[] = {
	MMI_DC_AV_BUF_8BIT_SF,
	MMI_DC_AV_BUF_8BIT_SF,
	MMI_DC_AV_BUF_8BIT_SF,
};

/* TODO: more formats */
static const struct mmi_dc_format plane_formats[] = {
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

/**
 * struct mmi_dc_plane_info - DC plane info
 * @formats: list of supported pixel formats
 * @num_formats: number of supported formats
 * @num_channels: number of DMA channels
 */
struct mmi_dc_plane_info {
	const struct mmi_dc_format	*formats;
	unsigned int			num_formats;
	unsigned int			num_channels;
};

/**
 * struct mmi_dc_plane - DC plane
 * @base: generic DRM plane
 * @dc: back pointer to the display controller device
 * @id: unique plane id
 * @info: corresponding plane info
 * @dc_format: current pixel format
 * @drm_format: DRM format description
 * @dmas: DMA channels used
 * @xt: DMA transfer template
 */
struct mmi_dc_plane {
	struct drm_plane		base;
	struct mmi_dc			*dc;
	enum mmi_dc_plane_id		id;
	const struct mmi_dc_plane_info	*info;
	const struct mmi_dc_format	*dc_format;
	const struct drm_format_info	*drm_format;
	struct dma_chan			*dmas[MMI_DC_MAX_NUM_SUB_PLANES];
	struct dma_interleaved_template *xt;
};

/**
 * drm_to_dc_plane - Convert DRM plane to DC plane
 * @plane: generic DRM plane
 *
 * Return: Corresponding DC plane.
 */
static inline struct mmi_dc_plane *drm_to_dc_plane(struct drm_plane *plane)
{
	return container_of(plane, struct mmi_dc_plane, base);
}

/* ----------------------------------------------------------------------------
 * DC Blender Ops
 */

/**
 * mmi_dc_blend_plane_set_csc - Set input CSC
 * @plane: DC plane
 * @coeffs: CSC matrix coefficients
 * @offsets: CSC color offsets
 *
 * Setup input color space converter.
 */
static void mmi_dc_blend_plane_set_csc(struct mmi_dc_plane *plane,
				       const u16 *coeffs, const u32 *offsets)
{
	struct mmi_dc *dc = plane->dc;
	unsigned int i, reg, swap[] = { 0, 1, 2 };

	if (plane->dc_format->swap) {
		if (plane->drm_format->is_yuv) {
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
		reg = MMI_DC_V_BLEND_INCSC_COEFF(plane->id, i);
		dc_write_blend(dc, reg, coeffs[i - i % 3 + swap[i % 3]]);
	}

	for (i = 0; i < MMI_DC_CSC_NUM_OFFSETS; ++i) {
		reg = MMI_DC_V_BLEND_CC_INCSC_OFFSET(plane->id, i);
		dc_write_blend(dc, reg, offsets[i]);
	}
}

/**
 * mmi_dc_blend_plane_enable - Enable blender input for given DC plane
 * @plane: DC plane
 */
static void mmi_dc_blend_plane_enable(struct mmi_dc_plane *plane)
{
	struct mmi_dc *dc = plane->dc;
	const u16 *coeffs;
	const u32 *offsets;
	u32 val;

	val = (plane->drm_format->is_yuv ? 0 : MMI_DC_V_BLEND_RGB_MODE) |
	      (plane->drm_format->hsub > 1 ? MMI_DC_V_BLEND_EN_US : 0);

	dc_write_blend(dc, MMI_DC_V_BLEND_LAYER_CONTROL(plane->id), val);

	if (plane->drm_format->is_yuv) {
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
 * @plane: DC plane
 */
static void mmi_dc_blend_plane_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc *dc = plane->dc;

	dc_write_blend(dc, MMI_DC_V_BLEND_LAYER_CONTROL(plane->id), 0);
	mmi_dc_blend_plane_set_csc(plane, csc_zero_matrix, csc_zero_offsets);
}

/* ----------------------------------------------------------------------------
 * DC AV Buffer Ops
 */

/**
 * mmi_dc_avbuf_plane_set_format - Set AVBUF format
 * @plane: DC plane
 * @format: format to set
 *
 * Configure the audio/video buffer manager according to the given pixel format
 */
static void mmi_dc_avbuf_plane_set_format(struct mmi_dc_plane *plane,
					  const struct mmi_dc_format *format)
{
	struct mmi_dc *dc = plane->dc;
	u32 val, i;

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_FORMAT);
	val &= ~MMI_DC_AV_BUF_FMT_MASK(plane->id);
	val |= format->buf_format << MMI_DC_AV_BUF_FMT_SHIFT(plane->id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_FORMAT, val);

	for (i = 0; i < MMI_DC_AV_BUF_NUM_SF; ++i) {
		u32 reg = MMI_DC_AV_BUF_PLANE_CC_SCALE_FACTOR(plane->id, i);

		dc_write_avbuf(dc, reg, format->sf[i]);
	}
}

/**
 * mmi_dc_avbuf_plane_enable - Enable AV buffer input for the given plane
 * @plane: DC plane
 */
static void mmi_dc_avbuf_plane_enable(struct mmi_dc_plane *plane)
{
	struct mmi_dc *dc = plane->dc;
	u32 val;
	int ch;

	for (ch = 0; ch < plane->drm_format->num_planes; ++ch)
		dc_write_avbuf(dc, MMI_DC_AV_CHBUF(plane->id * 3 + ch),
			       MMI_DC_AV_CHBUF_EN | MMI_DC_AV_CHBUF_BURST);

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~MMI_DC_AV_BUF_VID_STREAM_SEL_MASK(plane->id);
	val |= MMI_DC_AV_BUF_VID_STREAM_SEL_MEM(plane->id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);
}

/**
 * mmi_dc_avbuf_plane_disable - Disable AV buffer input for the given plane
 * @plane: DC plane
 */
static void mmi_dc_avbuf_plane_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc *dc = plane->dc;
	u32 val;
	int ch;

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~MMI_DC_AV_BUF_VID_STREAM_SEL_MASK(plane->id);
	val |= MMI_DC_AV_BUF_VID_STREAM_SEL_NONE(plane->id);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);

	for (ch = 0; ch < plane->drm_format->num_planes; ++ch)
		dc_write_avbuf(dc, MMI_DC_AV_CHBUF(plane->id * 3 + ch),
			       MMI_DC_AV_CHBUF_FLUSH);
}

/* ----------------------------------------------------------------------------
 * DC Plane Utils
 */

/**
 * mmi_dc_plane_find_format - Find plane format
 * @plane: DC plane
 * @drm_format: format to lookup for
 *
 * Return: Found DC pixel format corresponding to the given DRM format, or
 *         NULL.
 */
static const struct mmi_dc_format *
mmi_dc_plane_find_format(struct mmi_dc_plane *plane, u32 drm_format)
{
	unsigned int i;

	for (i = 0; i < plane->info->num_formats; ++i) {
		if (plane->info->formats[i].drm_format == drm_format)
			return &plane->info->formats[i];
	}

	return NULL;
}

/**
 * mmi_dc_plane_request_dma - Request DMA channels for the plane
 * @plane: DC plane
 *
 * Request DMA channels for each sub-plane.
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dc_plane_request_dma(struct mmi_dc_plane *plane)
{
	struct mmi_dc *dc = plane->dc;
	unsigned int i;

	for (i = 0; i < plane->info->num_channels; ++i) {
		struct dma_chan *dma_channel;
		char dma_channel_name[32];

		snprintf(dma_channel_name, sizeof(dma_channel_name),
			 "vid.%u.%u", plane->id, i);
		dma_channel = dma_request_chan(dc->dev, dma_channel_name);
		if (IS_ERR(dma_channel))
			return dev_err_probe(dc->dev, PTR_ERR(dma_channel),
					     "failed to request dma channel");
		plane->dmas[i] = dma_channel;
	}

	return 0;
}

/**
 * mmi_dc_plane_release_dma - Release assigned DMA channels
 * @plane: DC plane
 */
static void mmi_dc_plane_release_dma(struct mmi_dc_plane *plane)
{
	unsigned int i;

	for (i = 0; i < plane->info->num_channels; ++i) {
		struct dma_chan *dma_channel = plane->dmas[i];

		if (dma_channel) {
			dmaengine_terminate_sync(dma_channel);
			dma_release_channel(dma_channel);
		}
	}
}

/**
 * mmi_dc_plane_drm_formats - Get the list of supported DRM formats
 * @info: DC plane info
 * @num_formats: number of supported formats
 *
 * Allocate and fill an array of supported DRM formats. Assign supported format
 * numbers in @num_formats. The caller is expected to free the list.
 *
 * Return: Pointer to the supported format list.
 */
static u32 *mmi_dc_plane_drm_formats(const struct mmi_dc_plane_info *info,
				     unsigned int *num_formats)
{
	unsigned int i;
	u32 *formats;

	formats = kcalloc(info->num_formats, sizeof(*formats), GFP_KERNEL);

	if (!formats) {
		*num_formats = 0;
		return NULL;
	}

	for (i = 0; i < info->num_formats; ++i)
		formats[i] = info->formats[i].drm_format;

	*num_formats = info->num_formats;

	return formats;
}

/**
 * mmi_dc_plane_set_format - Set plane DRM format
 * @plane: DC plane
 * @info: DRM format description
 *
 * Set DRM format, program blender, and AV buffer manager accordingly.
 */
static void mmi_dc_plane_set_format(struct mmi_dc_plane *plane,
				    const struct drm_format_info *info)
{
	unsigned int i;

	plane->dc_format = mmi_dc_plane_find_format(plane, info->format);
	if (WARN_ON(!plane->dc_format))
		return;
	plane->drm_format = info;

	mmi_dc_avbuf_plane_set_format(plane, plane->dc_format);

	for (i = 0; i < info->num_planes; ++i) {
		struct dma_chan *dma_channel = plane->dmas[i];
		struct xilinx_dpdma_peripheral_config pconfig = {
			.video_group = true,
		};
		struct dma_slave_config sconfig = {
			.direction = DMA_MEM_TO_DEV,
			.peripheral_config = &pconfig,
			.peripheral_size = sizeof(pconfig),
		};

		dmaengine_slave_config(dma_channel, &sconfig);
	}
}

/**
 * mmi_dc_plane_update - Update DC plane
 * @plane: DC plane
 * @state: DRM plane state to update to
 *
 * Update DC plane buffer. Prepare and submit DMA transfers.
 */
static void mmi_dc_plane_update(struct mmi_dc_plane *plane,
				struct drm_plane_state *state)
{
	struct mmi_dc *dc = plane->dc;
	const struct drm_format_info *info = plane->drm_format;
	unsigned int i;

	for (i = 0; i < info->num_planes; ++i) {
		unsigned int width = state->crtc_w / (i ? info->hsub : 1);
		unsigned int height = state->crtc_h / (i ? info->vsub : 1);
		struct dma_chan *dma_channel = plane->dmas[i];
		struct dma_async_tx_descriptor *desc;

		plane->xt->numf = height;
		plane->xt->src_start = drm_fb_dma_get_gem_addr(state->fb,
							       state, i);
		plane->xt->sgl[0].size = width * info->cpp[i];
		plane->xt->sgl[0].icg = state->fb->pitches[i] -
					plane->xt->sgl[0].size;

		desc = dmaengine_prep_interleaved_dma(dma_channel, plane->xt,
						      DMA_CTRL_ACK |
						      DMA_PREP_REPEAT |
						      DMA_PREP_LOAD_EOT);
		if (!desc) {
			dev_err(dc->dev, "failed to prepare DMA descriptor\n");
			return;
		}

		dmaengine_submit(desc);
		dma_async_issue_pending(dma_channel);
	}
}

/**
 * mmi_dc_plane_enable - Enable DC plane
 * @plane: DC plane
 */
static void mmi_dc_plane_enable(struct mmi_dc_plane *plane)
{
	mmi_dc_avbuf_plane_enable(plane);
	mmi_dc_blend_plane_enable(plane);
}

/**
 * mmi_dc_plane_disable - Disable DC plane
 * @plane: DC plane
 */
static void mmi_dc_plane_disable(struct mmi_dc_plane *plane)
{
	unsigned int i;

	for (i = 0; i < plane->drm_format->num_planes; ++i)
		dmaengine_terminate_sync(plane->dmas[i]);

	mmi_dc_avbuf_plane_disable(plane);
	mmi_dc_blend_plane_disable(plane);
}

/* ----------------------------------------------------------------------------
 * DRM Plane
 */

static int mmi_dc_plane_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;

	if (!new_plane_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void mmi_dc_plane_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct mmi_dc_plane *dc_plane = drm_to_dc_plane(plane);
	bool format_changed = false;

	if (!old_state->fb ||
	    old_state->fb->format->format != new_state->fb->format->format)
		format_changed = true;

	if (format_changed) {
		if (old_state->fb)
			mmi_dc_plane_disable(dc_plane);
		mmi_dc_plane_set_format(dc_plane, new_state->fb->format);
	}

	mmi_dc_plane_update(dc_plane, new_state);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY)
		mmi_dc_set_global_alpha(dc_plane->dc, plane->state->alpha >> 8,
					true);

	if (format_changed)
		mmi_dc_plane_enable(dc_plane);
}

static void mmi_dc_plane_atomic_disable(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct mmi_dc_plane *dc_plane = drm_to_dc_plane(plane);

	if (!old_state->fb)
		return;

	mmi_dc_plane_disable(dc_plane);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY)
		mmi_dc_set_global_alpha(dc_plane->dc, plane->state->alpha >> 8,
					false);
}

static const struct drm_plane_helper_funcs mmi_dc_plane_helper_funcs = {
	.atomic_check		= mmi_dc_plane_atomic_check,
	.atomic_update		= mmi_dc_plane_atomic_update,
	.atomic_disable		= mmi_dc_plane_atomic_disable,
};

static const struct drm_plane_funcs mmi_dc_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

/**
 * mmi_dc_drm_plane_init - Allocate and initialize the DC plane
 * @drm: DRM device
 * @info: DC plane info
 * @type: DRM plane type
 * @id: DC plane id
 *
 * Return: New DC plane on success or error pointer otherwise.
 */
static struct mmi_dc_plane *
mmi_dc_drm_plane_init(struct drm_device *drm,
		      const struct mmi_dc_plane_info *info,
		      enum drm_plane_type type,
		      enum mmi_dc_plane_id id)
{
	unsigned int num_formats;
	u32 *formats = mmi_dc_plane_drm_formats(info, &num_formats);
	struct mmi_dc_plane *plane;

	plane = drmm_universal_plane_alloc(drm, struct mmi_dc_plane,
					   base, 0, &mmi_dc_plane_funcs,
					   formats, num_formats, NULL, type,
					   NULL);
	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(&plane->base, &mmi_dc_plane_helper_funcs);

	drm_plane_create_zpos_immutable_property(&plane->base, id);
	if (type == DRM_PLANE_TYPE_PRIMARY)
		drm_plane_create_alpha_property(&plane->base);

	return plane;
}

/* ----------------------------------------------------------------------------
 * DC Plane Interface
 */

/**
 * mmi_dc_plane_get_primary - Get DC primary plane
 * @dc: DC device
 *
 * Return: Primary DC plane.
 */
struct drm_plane *mmi_dc_plane_get_primary(struct mmi_dc *dc)
{
	return &dc->planes[MMI_DC_PLANE1]->base;
}

/**
 * mmi_dc_planes_set_possible_crtc - Set possible CRTC for all planes
 * @dc: DC device
 * @crtc_mask: CRTC mask to assign
 */
void mmi_dc_planes_set_possible_crtc(struct mmi_dc *dc, u32 crtc_mask)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dc->planes); ++i)
		dc->planes[i]->base.possible_crtcs = crtc_mask;
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
	struct dma_chan *dma_channel = plane->dmas[0];

	return 1 << dma_channel->device->copy_align;
}

/**
 * mmi_dc_create_planes - Create all DC planes.
 * @dc: DC device
 * @drm: DRM device
 *
 * Return: 0 on success or error code otherwise.
 */
int mmi_dc_create_planes(struct mmi_dc *dc, struct drm_device *drm)
{
	static const struct mmi_dc_plane_info plane_info[] = {
		[MMI_DC_PLANE0] = {
			.formats = plane_formats,
			.num_formats = ARRAY_SIZE(plane_formats),
			.num_channels = MMI_DC_MAX_NUM_SUB_PLANES,
		},
		[MMI_DC_PLANE1] = {
			.formats = plane_formats,
			.num_formats = ARRAY_SIZE(plane_formats),
			.num_channels = MMI_DC_MAX_NUM_SUB_PLANES,
		},
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dc->planes); ++i) {
		int ret;
		size_t xt_alloc_size;
		enum drm_plane_type type = i == MMI_DC_PLANE1 ?
						DRM_PLANE_TYPE_PRIMARY :
						DRM_PLANE_TYPE_OVERLAY;
		const struct mmi_dc_plane_info *info = &plane_info[i];
		struct mmi_dc_plane *plane = mmi_dc_drm_plane_init(drm, info,
								   type, i);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			dev_err(dc->dev, "failed to create DRM plane: %d\n",
				ret);
			return ret;
		}

		plane->id = i;
		plane->dc = dc;
		plane->info = info;

		ret = mmi_dc_plane_request_dma(plane);
		if (ret < 0)
			return ret;

		xt_alloc_size = sizeof(struct dma_interleaved_template) +
				sizeof(struct data_chunk);
		plane->xt = devm_kzalloc(dc->dev, xt_alloc_size, GFP_KERNEL);
		if (!plane->xt)
			return -ENOMEM;

		plane->xt->dir = DMA_MEM_TO_DEV;
		plane->xt->src_sgl = true;
		plane->xt->frame_size = 1;

		dc->planes[i] = plane;
	}

	/* Reset video / audio select */
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, 0x1F);

	return 0;
}

/**
 * mmi_dc_destroy_planes - Destroy all DC planes.
 * @dc: DC device
 */
void mmi_dc_destroy_planes(struct mmi_dc *dc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dc->planes); ++i)
		mmi_dc_plane_release_dma(dc->planes[i]);
}
