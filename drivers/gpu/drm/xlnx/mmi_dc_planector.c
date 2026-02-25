// SPDX-License-Identifier: GPL-2.0
/*
 * MMI Display Controller Live Video Plane / Bridge Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc.h"
#include "mmi_dc_bridge.h"
#include "mmi_dc_plane.h"

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
/**
 * struct mmi_dc_planector - MMI DC planector
 * @base: generic MMI DC plane
 * @bridge: associated MMI DC bridge
 */
struct mmi_dc_planector {
	struct mmi_dc_plane		base;
	struct mmi_dc_bridge		*bridge;
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
	struct drm_crtc_state *crtc_state;

	if (!plane_state->fb || !plane_state->crtc)
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state,
						   plane_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mmi_dc_bridge_connect(planector->bridge, &crtc_state->adjusted_mode);
}

static void mmi_dc_planector_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc_planector *planector = plane_to_planector(plane);

	mmi_dc_bridge_disconnect(planector->bridge);
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
		return ERR_CAST(planector);

	planector->base.id = id;
	planector->base.dc = dc;
	planector->base.funcs.check = mmi_dc_planector_check;
	planector->base.funcs.update = mmi_dc_planector_update;
	planector->base.funcs.disable = mmi_dc_planector_disable;

	drm_plane_helper_add(&planector->base.base,
			     &mmi_dc_drm_plane_helper_funcs);

	drm_plane_create_zpos_immutable_property(&planector->base.base, id);

	planector->bridge = mmi_dc_bridge_init(dc->dev, &planector->base);
	if (IS_ERR(planector->bridge))
		return ERR_CAST(planector->bridge);

	return &planector->base;
}
