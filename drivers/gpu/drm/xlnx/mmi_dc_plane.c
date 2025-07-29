// SPDX-License-Identifier: GPL-2.0
/*
 * MMI Display Controller Plane Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include "mmi_dc.h"
#include "mmi_dc_plane.h"

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
 * DRM Plane Interface Implementation
 */

static int mmi_dc_plane_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct mmi_dc_plane *dc_plane = drm_to_dc_plane(plane);

	if (!new_plane_state->crtc || !dc_plane->funcs.check)
		return 0;

	return dc_plane->funcs.check(dc_plane, state);
}

static void mmi_dc_plane_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct mmi_dc_plane *dc_plane = drm_to_dc_plane(plane);

	if (dc_plane->funcs.update)
		dc_plane->funcs.update(dc_plane, state);
}

static void mmi_dc_plane_atomic_disable(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, plane);
	struct mmi_dc_plane *dc_plane = drm_to_dc_plane(plane);

	if (!old_state->fb)
		return;

	if (dc_plane->funcs.disable)
		dc_plane->funcs.disable(dc_plane);
}

const struct drm_plane_helper_funcs mmi_dc_drm_plane_helper_funcs = {
	.atomic_check		= mmi_dc_plane_atomic_check,
	.atomic_update		= mmi_dc_plane_atomic_update,
	.atomic_disable		= mmi_dc_plane_atomic_disable,
};

const struct drm_plane_funcs mmi_dc_drm_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

/* ----------------------------------------------------------------------------
 * DC Plane to CRTC Interface
 */

/**
 * mmi_dc_plane_get_primary - Get DC primary plane
 * @dc: DC device
 *
 * Return: Primary DC plane.
 */
struct drm_plane *mmi_dc_plane_get_primary(struct mmi_dc *dc)
{
	return &dc->planes[MMI_DC_PLANE0]->base;
}

/**
 * mmi_dc_plane_get_cursor - Get DC cursor plane
 * @dc: DC device
 *
 * Return: DRM cursor plane.
 */
struct drm_plane *mmi_dc_plane_get_cursor(struct mmi_dc *dc)
{
	return &dc->planes[MMI_DC_CURSOR]->base;
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
 * mmi_dc_create_planes - Create all DC planes.
 * @dc: DC device
 * @drm: DRM device
 *
 * Return: 0 on success or error code otherwise.
 */
int mmi_dc_create_planes(struct mmi_dc *dc, struct drm_device *drm)
{
	static struct mmi_dc_plane *(*factory[])(struct mmi_dc *dc,
						 struct drm_device *drm,
						 enum mmi_dc_plane_id id) = {
		[MMI_DC_PLANE0] = mmi_dc_create_primary_plane,
		[MMI_DC_PLANE1] = mmi_dc_create_overlay_plane,
		[MMI_DC_CURSOR] = mmi_dc_create_cursor_plane,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dc->planes); ++i) {
		struct mmi_dc_plane *plane = factory[i](dc, drm, i);
		int ret;

		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			dev_err(dc->dev, "failed to create plane: %d\n", ret);
			return ret;
		}

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

	for (i = 0; i < ARRAY_SIZE(dc->planes); i++) {
		struct mmi_dc_plane *dc_plane = dc->planes[i];

		if (dc_plane && dc_plane->funcs.destroy)
			dc_plane->funcs.destroy(dc_plane);
	}
}

/**
 * mmi_dc_disable_planes - Stop DMA transfers and disable all planes.
 * @dc: DC device
 * @state: New atomic state to apply
 */
static void mmi_dc_disable_planes(struct mmi_dc *dc,
				  struct drm_atomic_state *state)
{
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_PLANES; ++i) {
		struct mmi_dc_plane *dc_plane = dc->planes[i];
		struct drm_plane *plane = &dc_plane->base;
		struct drm_plane_state *old_state =
			drm_atomic_get_old_plane_state(state, plane);

		if (old_state && old_state->fb && dc_plane->funcs.disable)
			dc_plane->funcs.disable(dc_plane);
	}
}

/**
 * mmi_dc_update_planes - Start DMA transfers to flush FBs.
 * @dc: DC device
 * @state: New atomic state to apply
 */
static void mmi_dc_update_planes(struct mmi_dc *dc,
				 struct drm_atomic_state *state)
{
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_PLANES; ++i) {
		struct mmi_dc_plane *dc_plane = dc->planes[i];
		struct drm_plane *plane = &dc_plane->base;
		struct drm_plane_state *new_state =
			drm_atomic_get_new_plane_state(state, plane);

		if (new_state && dc_plane->funcs.update)
			dc_plane->funcs.update(dc_plane, state);
	}
}

/**
 * mmi_dc_reconfig_planes - Restore DC planes configuration.
 * @dc: DC device
 * @state: New atomic state to apply
 */
void mmi_dc_reconfig_planes(struct mmi_dc *dc, struct drm_atomic_state *state)
{
	mmi_dc_disable_planes(dc, state);
	mmi_dc_update_planes(dc, state);
}

/**
 * mmi_dc_reset_planes - Reset planes historic format info.
 * @dc: DC device
 */
void mmi_dc_reset_planes(struct mmi_dc *dc)
{
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_PLANES; ++i) {
		struct mmi_dc_plane *dc_plane = dc->planes[i];

		if (dc_plane && dc_plane->funcs.reset)
			dc_plane->funcs.reset(dc_plane);
	}
}

/**
 * mmi_dc_has_visible_planes - Check if at least one plane is visible.
 * @dc: DC device
 * @state: New atomic state to check planes visibility against
 */
bool mmi_dc_has_visible_planes(struct mmi_dc *dc,
			       struct drm_atomic_state *state)
{
	unsigned int i;

	for (i = 0; i < MMI_DC_NUM_PLANES; ++i) {
		struct mmi_dc_plane *dc_plane = dc->planes[i];
		struct drm_plane *plane = &dc_plane->base;
		struct drm_plane_state *new_state =
			drm_atomic_get_new_plane_state(state, plane);

		if (new_state && new_state->fb)
			return true;
	}

	return false;
}
