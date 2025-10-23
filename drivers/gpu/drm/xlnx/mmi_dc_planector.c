// SPDX-License-Identifier: GPL-2.0
/*
 * MMI Display Controller Live Video Plane / Bridge Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc.h"
#include "mmi_dc_plane.h"

#include <drm/drm_blend.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <linux/media-bus-format.h>

#define MMI_DC_LIVE_VID_BPC8		(0x0001)
#define MMI_DC_LIVE_VID_BPC10		(0x0002)
#define MMI_DC_LIVE_VID_BPC12		(0x0003)
#define MMI_DC_LIVE_VID_FORMAT_SHIFT	(4)
#define MMI_DC_LIVE_VID_FORMAT_CB_FIRST	(0x0100)

/**
 * struct mmi_dc_planector - MMI DC planector
 * @base: generic MMI DC plane
 * @bridge: DRM bridge
 * @connector_status: current connector status
 * @display_mode: current display mode
 */
struct mmi_dc_planector {
	struct mmi_dc_plane		base;
	struct drm_bridge		bridge;
	enum drm_connector_status	connector_status;
	struct drm_display_mode		display_mode;
};

/* TODO: more formats */
static const struct mmi_dc_format live_video_formats[] = {
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB121212_1X36,
		.buf_format		= MMI_DC_LIVE_VID_BPC12 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_121212,
	},
};

/**
 * plane_to_planector - Convert generic MMI DC plane to planector
 * @plane: MMI DC plane
 *
 * Return: Corresponding planector.
 */
static inline struct mmi_dc_planector *
plane_to_planector(struct mmi_dc_plane *plane)
{
	return container_of(plane, struct mmi_dc_planector, base);
}

/**
 * bridge_to_planector - Convert DRM bridge to planector
 * @bridge: DRM bridge
 *
 * Return: Corresponding planector.
 */
static inline struct mmi_dc_planector *
bridge_to_planector(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mmi_dc_planector, bridge);
}

static const struct mmi_dc_format *mmi_dc_find_live_format(u32 mbus_format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(live_video_formats); ++i) {
		if (live_video_formats[i].mbus_format == mbus_format)
			return &live_video_formats[i];
	}

	return NULL;
}

/**
 * mmi_dc_copy_display_mode - Copy essential display mode info (timing & flags)
 * @dst: display mode to copy to
 * @src: display mode to copy from
 */
static void mmi_dc_copy_display_mode(struct drm_display_mode *dst,
				     const struct drm_display_mode *src)
{
	dst->hdisplay = src->hdisplay;
	dst->hsync_start = src->hsync_start;
	dst->hsync_end = src->hsync_end;
	dst->htotal = src->htotal;

	dst->vdisplay = src->vdisplay;
	dst->vsync_start = src->vsync_start;
	dst->vsync_end = src->vsync_end;
	dst->vtotal = src->vtotal;

	dst->clock = src->clock;

	dst->flags = src->flags;
}

/* ----------------------------------------------------------------------------
 * DRM Bridge
 */

static int mmi_dc_planector_bridge_attach(struct drm_bridge *bridge,
					  enum drm_bridge_attach_flags flags)
{
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_connector *connector;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	connector = drm_bridge_connector_init(bridge->dev, encoder);
	if (IS_ERR(connector))
		return PTR_ERR(connector);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	return 0;
}

static void
mmi_dc_planector_bridge_enable(struct drm_bridge *bridge,
			       struct drm_bridge_state *old_state)
{
	struct mmi_dc_planector *planector = bridge_to_planector(bridge);
	struct drm_bridge_state *new_state =
		drm_atomic_get_new_bridge_state(old_state->base.state, bridge);
	const struct mmi_dc_format *format =
		mmi_dc_find_live_format(new_state->input_bus_cfg.format);

	if (WARN_ON(!format))
		return;

	mmi_dc_compositor_enable(&planector->base, format);
	/* TODO: enable external timing source here */
	mmi_dc_set_video_timing_source(planector->base.dc, MMI_DC_VT_INTERNAL);
}

static void
mmi_dc_planector_bridge_disable(struct drm_bridge *bridge,
				struct drm_bridge_state *old_state)
{
	struct mmi_dc_planector *planector = bridge_to_planector(bridge);

	mmi_dc_compositor_disable(&planector->base);
	/* TODO: this should be ref counted for 2 live video case */
	mmi_dc_set_video_timing_source(planector->base.dc, MMI_DC_VT_INTERNAL);
}

static enum drm_connector_status
mmi_dc_planector_bridge_detect(struct drm_bridge *bridge)
{
	struct mmi_dc_planector *planector = bridge_to_planector(bridge);

	return planector->connector_status;
}

static int mmi_dc_planector_bridge_get_modes(struct drm_bridge *bridge,
					     struct drm_connector *connector)
{
	struct mmi_dc_planector *planector = bridge_to_planector(bridge);
	struct mmi_dc *dc = planector->base.dc;
	struct drm_display_mode *mode = drm_mode_create(bridge->dev);

	if (mode) {
		mmi_dc_copy_display_mode(mode, &planector->display_mode);
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
		return 1;
	}

	dev_err(dc->dev, "failed to create %dx%d display mode\n",
		planector->display_mode.hdisplay,
		planector->display_mode.vdisplay);

	return 0;
}

static u32 *
mmi_dc_planector_get_input_bus_fmts(struct drm_bridge *bridge,
				    struct drm_bridge_state *bridge_state,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state,
				    u32 output_fmt,
				    unsigned int *num_input_fmts)
{
	u32 *input_fmts = kcalloc(ARRAY_SIZE(live_video_formats), sizeof(u32),
				  GFP_KERNEL);
	unsigned int i;

	if (!input_fmts) {
		*num_input_fmts = 0;
		return input_fmts;
	}

	for (i = 0; i < ARRAY_SIZE(live_video_formats); ++i)
		input_fmts[i] = live_video_formats[i].mbus_format;
	*num_input_fmts = ARRAY_SIZE(live_video_formats);

	return input_fmts;
}

static const struct drm_bridge_funcs mmi_dc_planector_bridge_funcs = {
	.attach				= mmi_dc_planector_bridge_attach,
	.atomic_enable			= mmi_dc_planector_bridge_enable,
	.atomic_disable			= mmi_dc_planector_bridge_disable,
	.detect				= mmi_dc_planector_bridge_detect,
	.get_modes			= mmi_dc_planector_bridge_get_modes,
	.atomic_get_input_bus_fmts	= mmi_dc_planector_get_input_bus_fmts,

	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

/**
 * mmi_dc_planector_bridge_init - Initialize DRM bridge part of planector
 * @planector: pointer to DC planector
 */
static void mmi_dc_planector_bridge_init(struct mmi_dc_planector *planector)
{
	struct drm_bridge *bridge = &planector->bridge;

	bridge->funcs = &mmi_dc_planector_bridge_funcs;
	bridge->ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_HPD
		    | DRM_BRIDGE_OP_MODES;
	bridge->type = DRM_MODE_CONNECTOR_VIRTUAL;
	bridge->of_node = planector->base.dc->dev->of_node;

	planector->connector_status = connector_status_disconnected;

	devm_drm_bridge_add(planector->base.dc->dev, bridge);
}

/* ----------------------------------------------------------------------------
 * DC Plane Interface Implementation
 */

static int mmi_dc_planector_check(struct mmi_dc_plane *plane,
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

static void mmi_dc_planector_update(struct mmi_dc_plane *plane,
				    struct drm_atomic_state *state)
{
	struct mmi_dc_planector *planector = plane_to_planector(plane);
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, &plane->base);

	if (!plane_state->fb)
		return;

	if (planector->connector_status != connector_status_connected) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_new_crtc_state(state,
						      plane_state->crtc);

		planector->connector_status = connector_status_connected;
		mmi_dc_copy_display_mode(&planector->display_mode,
					 &crtc_state->adjusted_mode);

		drm_bridge_hpd_notify(&planector->bridge,
				      planector->connector_status);
	}
}

static void mmi_dc_planector_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc_planector *planector = plane_to_planector(plane);

	planector->connector_status = connector_status_disconnected;

	drm_bridge_hpd_notify(&planector->bridge, planector->connector_status);
}

/**
 * mmi_dc_create_planector - Create and initialize DC planector
 * @dc: pointer to MMI DC
 * @drm: DRM device
 * @id: DC plane id
 *
 * Return: New DC planector on success or error pointer otherwise.
 */
struct mmi_dc_plane *mmi_dc_create_planector(struct mmi_dc *dc,
					     struct drm_device *drm,
					     enum mmi_dc_plane_id id)
{
	static const u32 format = DRM_FORMAT_XRGB8888;
	struct mmi_dc_planector *planector;
	enum drm_plane_type plane_type = id == MMI_DC_PLANE0
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;

	if (id > MMI_DC_PLANE1)
		return ERR_PTR(-EINVAL);

	planector = drmm_universal_plane_alloc(drm, struct mmi_dc_planector,
					       base.base, 0,
					       &mmi_dc_drm_plane_funcs,
					       &format, 1, NULL, plane_type,
					       NULL);
	if (IS_ERR(planector))
		return (void *)planector;

	planector->base.id = id;
	planector->base.dc = dc;
	planector->base.funcs.check = mmi_dc_planector_check;
	planector->base.funcs.update = mmi_dc_planector_update;
	planector->base.funcs.disable = mmi_dc_planector_disable;

	drm_plane_helper_add(&planector->base.base,
			     &mmi_dc_drm_plane_helper_funcs);

	drm_plane_create_zpos_immutable_property(&planector->base.base, id);

	mmi_dc_planector_bridge_init(planector);

	return &planector->base;
}
