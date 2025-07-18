/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DC_PLANE_H__
#define __MMI_DC_PLANE_H__

#include <linux/dmaengine.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane.h>

/* ----------------------------------------------------------------------------
 * DC Plane Interface
 */

struct mmi_dc_plane;

/**
 * struct mmi_dc_plane_funcs - DC plane interface callbacks (vtable)
 */
struct mmi_dc_plane_funcs {
	/**
	 * @destroy:
	 *
	 * Cleanup plane resources. This called during driver unload.
	 */
	void (*destroy)(struct mmi_dc_plane *plane);

	/**
	 * @check:
	 *
	 * This one is being called during DRM atomic check phase. Plane is
	 * expected to validate the incoming atomic state and report the
	 * validation status.
	 *
	 * Returns:
	 *
	 * 0 if the new state passed the validation or error code otherwise.
	 */
	int (*check)(struct mmi_dc_plane *plane,
		     struct drm_atomic_state *state);
	/**
	 * @update:
	 *
	 * Plane HW update request. The plane is expected to perform required
	 * HW manipulations to reflect the expected state. These manipulations
	 * include AV buffer and blender configuration update as well as
	 * triggering new DMA transfers if needed.
	 */
	void (*update)(struct mmi_dc_plane *plane,
		       struct drm_atomic_state *state);
	/**
	 * @disable:
	 *
	 * The plane should stop all DMA transfers and disable plane related AV
	 * buffer and blender configurations.
	 */
	void (*disable)(struct mmi_dc_plane *plane);

	/**
	 * @reset:
	 *
	 * This is being called after hardware reset. The plane is expected to
	 * adjust its software state accordingly.
	 */
	void (*reset)(struct mmi_dc_plane *plane);
};

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

struct mmi_dc;

/**
 * struct mmi_dc_plane - DC plane
 * @base: generic DRM plane
 * @dc: back pointer to the display controller device
 * @id: unique plane id
 * @funcs: plane virtual table
 */
struct mmi_dc_plane {
	struct drm_plane		base;
	struct mmi_dc			*dc;
	enum mmi_dc_plane_id		id;
	struct mmi_dc_plane_funcs	funcs;
};

/* ----------------------------------------------------------------------------
 * DC Plane Factory
 */

extern const struct drm_plane_helper_funcs mmi_dc_drm_plane_helper_funcs;
extern const struct drm_plane_funcs mmi_dc_drm_plane_funcs;

struct mmi_dc_plane *mmi_dc_create_primary_plane(struct mmi_dc *dc,
						 struct drm_device *drm,
						 enum mmi_dc_plane_id id);

struct mmi_dc_plane *mmi_dc_create_overlay_plane(struct mmi_dc *dc,
						 struct drm_device *drm,
						 enum mmi_dc_plane_id id);

struct mmi_dc_plane *mmi_dc_create_cursor_plane(struct mmi_dc *dc,
						struct drm_device *drm,
						enum mmi_dc_plane_id id);

/* ----------------------------------------------------------------------------
 * DC Plane to CRTC Interface
 */

struct drm_plane *mmi_dc_plane_get_primary(struct mmi_dc *dc);
struct drm_plane *mmi_dc_plane_get_cursor(struct mmi_dc *dc);
void mmi_dc_planes_set_possible_crtc(struct mmi_dc *dc, u32 crtc_mask);
unsigned int mmi_dc_planes_get_dma_align(struct mmi_dc *dc);
int mmi_dc_create_planes(struct mmi_dc *dc, struct drm_device *drm);
void mmi_dc_destroy_planes(struct mmi_dc *dc);
void mmi_dc_reconfig_planes(struct mmi_dc *dc, struct drm_atomic_state *state);
void mmi_dc_reset_planes(struct mmi_dc *dc);
bool mmi_dc_has_visible_planes(struct mmi_dc *dc,
			       struct drm_atomic_state *state);

#endif /* __MMI_DC_PLANE_H__ */
