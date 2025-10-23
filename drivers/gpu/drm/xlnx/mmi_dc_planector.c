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

struct mmi_dc_planector {
	struct mmi_dc_plane		base;
	struct drm_bridge		bridge;
	enum drm_connector_status	connector_status;
	int				hdisplay;
	int				vdisplay;
	int				vrefresh;
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
			       struct drm_bridge_state *old_bridge_state)
{
	/* TODO: config blender and avbuf */
}

static void
mmi_dc_planector_bridge_disable(struct drm_bridge *bridge,
				struct drm_bridge_state *old_bridge_state)
{
	/* TODO: disable blender and avbuf */
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
	/* For now we support only progressive, regular blanking video modes */
	struct drm_display_mode *mode = drm_cvt_mode(bridge->dev,
						     planector->hdisplay,
						     planector->vdisplay,
						     planector->vrefresh,
						     false, false, false);

	if (mode) {
		drm_mode_probed_add(connector, mode);
		return 1;
	}

	dev_err(dc->dev, "failed to create %dx%d-%d mode\n",
		planector->hdisplay, planector->vdisplay, planector->vrefresh);

	return 0;
}

static const struct drm_bridge_funcs mmi_dc_planector_bridge_funcs = {
	.attach			= mmi_dc_planector_bridge_attach,
	.atomic_enable		= mmi_dc_planector_bridge_enable,
	.atomic_disable		= mmi_dc_planector_bridge_disable,
	.detect			= mmi_dc_planector_bridge_detect,
	.get_modes		= mmi_dc_planector_bridge_get_modes,

	/* TODO: input bus formats handling */

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

	drm_bridge_add(bridge);
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
		planector->hdisplay = crtc_state->adjusted_mode.hdisplay;
		planector->vdisplay = crtc_state->adjusted_mode.vdisplay;
		planector->vrefresh =
			drm_mode_vrefresh(&crtc_state->adjusted_mode);
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
