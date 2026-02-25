// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated Display Controller Bridge Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc.h"
#include "mmi_dc_bridge.h"
#include "mmi_dc_plane.h"

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <linux/media-bus-format.h>

#define MMI_DC_LIVE_VID_BPC8		(0x0001)
#define MMI_DC_LIVE_VID_BPC10		(0x0002)
#define MMI_DC_LIVE_VID_BPC12		(0x0003)
#define MMI_DC_LIVE_VID_FORMAT_SHIFT	(4)
#define MMI_DC_LIVE_VID_FORMAT_CB_FIRST	(0x0100)

/* TODO: more formats */
static const struct mmi_dc_format live_video_formats[] = {
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB888_1X24,
		.buf_format		= MMI_DC_LIVE_VID_BPC8 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_888,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB888_0_5X48,
		.buf_format		= MMI_DC_LIVE_VID_BPC8 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_888,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB888_0_25X96,
		.buf_format		= MMI_DC_LIVE_VID_BPC8 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_888,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB101010_1X30,
		.buf_format		= MMI_DC_LIVE_VID_BPC10 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_101010,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB101010_0_5X60,
		.buf_format		= MMI_DC_LIVE_VID_BPC10 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_101010,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB101010_0_25X120,
		.buf_format		= MMI_DC_LIVE_VID_BPC10 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_101010,
	},
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
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB121212_0_5X72,
		.buf_format		= MMI_DC_LIVE_VID_BPC12 |
					  MMI_DC_FORMAT_RGB <<
					  MMI_DC_LIVE_VID_FORMAT_SHIFT,
		.format_flags		= MMI_DC_FMT_LIVE,
		.csc_matrix		= csc_identity_matrix,
		.csc_offsets		= csc_zero_offsets,
		.csc_scaling_factors	= csc_scaling_factors_121212,
	},
	{
		.mbus_format		= MEDIA_BUS_FMT_RGB121212_0_25X144,
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
 * to_mmi_dc_bridge - Convert DRM bridge to MMI DC bridge
 * @bridge: DRM bridge
 *
 * Return: Corresponding MMI DC bridge.
 */
static inline struct mmi_dc_bridge *to_mmi_dc_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mmi_dc_bridge, base);
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
 * mmi_dc_attach_bypass_bridge - Create and attach downstream bridge chain
 * @bridge: MMI DC bridge instance operating in bypass mode (no plane)
 * @encoder: DRM encoder to attach the chain to
 * @flags: DRM bridge attach flags from the core
 *
 * Bypass mode does not expose a local connector. Instead, the MMI DC bridge
 * acts as an upstream element and dynamically discovers the next bridge from
 * the device tree using the DP Tx port derived from @bridge->mst_id. The next
 * bridge is then attached to @encoder.
 *
 * Return: 0 on success, -EINVAL if a connector was requested, or a negative
 * errno if bridge discovery/attach fails.
 */
static int mmi_dc_attach_bypass_bridge(struct mmi_dc_bridge *bridge,
				       struct drm_encoder *encoder,
				       enum drm_bridge_attach_flags flags)
{
	struct mmi_dc *dc = bridge->dc;
	struct drm_bridge *next_bridge;
	u32 dptx_port = MMI_DC_DPTX_PORT_0 + bridge->mst_id;
	int ret;

	/* We don't want to host a connector for the bypass bridge */
	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	/* Build the bridge chain */
	next_bridge = devm_drm_of_get_bridge(dc->dev, dc->dev->of_node,
					     dptx_port, 0);
	if (IS_ERR(next_bridge)) {
		ret = PTR_ERR(next_bridge);
		dev_err(dc->dev, "failed to find dptx bridge: %d\n", ret);
		return ret;
	}

	return drm_bridge_attach(encoder, next_bridge, &bridge->base, flags);
}

static int mmi_dc_bridge_attach(struct drm_bridge *drm_bridge,
				struct drm_encoder *encoder,
				enum drm_bridge_attach_flags flags)
{
	struct drm_connector *connector;
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);
	int ret;

	if (!bridge->plane)
		return mmi_dc_attach_bypass_bridge(bridge, encoder, flags);

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	connector = drm_bridge_connector_init(drm_bridge->dev, encoder);
	if (IS_ERR(connector))
		return PTR_ERR(connector);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	return 0;
}

static void mmi_dc_bridge_enable(struct drm_bridge *drm_bridge,
				 struct drm_atomic_state *state)
{
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);
	struct drm_bridge_state *new_state =
		drm_atomic_get_new_bridge_state(state, drm_bridge);
	const struct mmi_dc_format *format =
		mmi_dc_find_live_format(new_state->input_bus_cfg.format);

	if (!bridge->plane)
		return;

	if (WARN_ON(!format))
		return;

	mmi_dc_compositor_enable(bridge->plane, format);
	/* TODO: enable external timing source here */
	mmi_dc_set_video_timing_source(bridge->dc, MMI_DC_VT_INTERNAL);
}

static void mmi_dc_bridge_disable(struct drm_bridge *drm_bridge,
				  struct drm_atomic_state *state)
{
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);

	if (!bridge->plane)
		return;

	mmi_dc_compositor_disable(bridge->plane);
	/* TODO: this should be ref counted for 2 live video case */
	mmi_dc_set_video_timing_source(bridge->dc, MMI_DC_VT_INTERNAL);
}

static enum drm_connector_status
mmi_dc_bridge_detect(struct drm_bridge *drm_bridge,
		     struct drm_connector *connector)
{
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);

	if (WARN_ON(!bridge->plane))
		return connector_status_unknown;

	return bridge->connector_status;
}

static int mmi_dc_bridge_get_modes(struct drm_bridge *drm_bridge,
				   struct drm_connector *connector)
{
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);
	struct drm_display_mode *mode;
	struct mmi_dc *dc = bridge->dc;

	if (WARN_ON(!bridge->plane))
		return 0;

	mode = drm_mode_create(drm_bridge->dev);

	if (mode) {
		mmi_dc_copy_display_mode(mode, &bridge->display_mode);
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
		return 1;
	}

	dev_err(dc->dev, "failed to create %dx%d display mode\n",
		bridge->display_mode.hdisplay, bridge->display_mode.vdisplay);

	return 0;
}

static u32 *
mmi_dc_bridge_get_input_bus_fmts(struct drm_bridge *drm_bridge,
				 struct drm_bridge_state *bridge_state,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state,
				 u32 output_fmt,
				 unsigned int *num_input_fmts)
{
	struct mmi_dc_bridge *bridge = to_mmi_dc_bridge(drm_bridge);
	bool can_transcode = !!bridge->plane;
	u32 *input_fmts;
	unsigned int i;

	*num_input_fmts = can_transcode ? ARRAY_SIZE(live_video_formats) : 1;
	input_fmts = kcalloc(*num_input_fmts, sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts) {
		*num_input_fmts = 0;
		return input_fmts;
	}

	if (can_transcode) {
		/* TODO: should we validate input format ppc? */
		for (i = 0; i < *num_input_fmts; ++i)
			input_fmts[i] = live_video_formats[i].mbus_format;
	} else {
		for (i = 0; i < ARRAY_SIZE(live_video_formats); ++i)
			if (live_video_formats[i].mbus_format == output_fmt)
				break;

		if (i == ARRAY_SIZE(live_video_formats)) {
			kfree(input_fmts);
			*num_input_fmts = 0;
			input_fmts = NULL;
		} else {
			input_fmts[0] = output_fmt;
		}
	}

	return input_fmts;
}

static u32 *
mmi_dc_bridge_get_output_bus_fmts(struct drm_bridge *drm_bridge,
				  struct drm_bridge_state *bridge_state,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state,
				  unsigned int *num_output_fmts)
{
	u32 *output_fmts = kcalloc(ARRAY_SIZE(live_video_formats),
				   sizeof(*output_fmts), GFP_KERNEL);
	unsigned int i;

	if (!output_fmts) {
		*num_output_fmts = 0;
		return output_fmts;
	}

	for (i = 0; i < ARRAY_SIZE(live_video_formats); ++i)
		output_fmts[i] = live_video_formats[i].mbus_format;
	*num_output_fmts = ARRAY_SIZE(live_video_formats);

	return output_fmts;
}

static const struct drm_bridge_funcs mmi_dc_bridge_funcs = {
	.attach				= mmi_dc_bridge_attach,
	.atomic_enable			= mmi_dc_bridge_enable,
	.atomic_disable			= mmi_dc_bridge_disable,
	.detect				= mmi_dc_bridge_detect,
	.get_modes			= mmi_dc_bridge_get_modes,
	.atomic_get_input_bus_fmts	= mmi_dc_bridge_get_input_bus_fmts,
	.atomic_get_output_bus_fmts	= mmi_dc_bridge_get_output_bus_fmts,

	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
};

/**
 * mmi_dc_bridge_connect - Propagate live-video connect event
 * @bridge: Bridge wrapper tied to the active MMI DC plane
 * @mode: Display mode detected on the live input stream
 *
 * Marks the bridge as connected, caches the mode for future probe cycles, and
 * emits an HPD notification toward the DRM bridge core.
 */
void mmi_dc_bridge_connect(struct mmi_dc_bridge *bridge,
			   const struct drm_display_mode *mode)
{
	if (WARN_ON(!bridge->plane))
		return;

	if (bridge->connector_status == connector_status_connected)
		return;

	bridge->connector_status = connector_status_connected;
	mmi_dc_copy_display_mode(&bridge->display_mode, mode);
	drm_bridge_hpd_notify(&bridge->base, bridge->connector_status);
}

/**
 * mmi_dc_bridge_disconnect - Propagate live-video disconnect event
 * @bridge: Bridge wrapper tied to the active MMI DC plane
 *
 * Marks the bridge as disconnected and generates an HPD notification so
 * user-space can respond to the link loss.
 */
void mmi_dc_bridge_disconnect(struct mmi_dc_bridge *bridge)
{
	if (WARN_ON(!bridge->plane))
		return;

	if (bridge->connector_status == connector_status_disconnected)
		return;

	bridge->connector_status = connector_status_disconnected;
	drm_bridge_hpd_notify(&bridge->base, bridge->connector_status);
}

/**
 * mmi_dc_bridge_init - Initialize and register the bridge wrapper
 * @dev: Device owning the bridge
 * @plane: Plane associated with this bridge, or NULL for bypass mode
 *
 * Configures the DRM bridge ops advertised to the core and registers the
 * bridge via the managed helper. Returns 0 on success or a negative errno from
 * the registration path.
 */
struct mmi_dc_bridge *mmi_dc_bridge_init(struct device *dev,
					 struct mmi_dc_plane *plane)
{
	struct mmi_dc_bridge *bridge =
		devm_drm_bridge_alloc(dev, struct mmi_dc_bridge, base,
				      &mmi_dc_bridge_funcs);
	struct drm_bridge *drm_bridge;
	int ret;

	if (IS_ERR(bridge))
		return bridge;

	bridge->plane = plane;

	drm_bridge = &bridge->base;
	drm_bridge->ops = bridge->plane ? DRM_BRIDGE_OP_DETECT
					| DRM_BRIDGE_OP_HPD
					| DRM_BRIDGE_OP_MODES
					: 0;
	drm_bridge->type = bridge->plane ? DRM_MODE_CONNECTOR_VIRTUAL
					 : DRM_MODE_CONNECTOR_Unknown;
	drm_bridge->of_node = dev->of_node;

	bridge->connector_status = connector_status_disconnected;

	ret = devm_drm_bridge_add(dev, drm_bridge);
	if (ret) {
		dev_err(dev, "failed to register MMI DC bridge: %d\n", ret);
		return ERR_PTR(ret);
	}

	return bridge;
}
