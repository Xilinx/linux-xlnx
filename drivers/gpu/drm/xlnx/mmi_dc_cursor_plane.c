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
#include <drm/drm_gem_dma_helper.h>

#include <linux/dma-mapping.h>

#define MMI_DC_CURSOR_CTRL		(0x0c20)
#define MMI_DC_CURSOR_SIZE		(0x0c24)

#define MMI_DC_CURSOR_FIXED_SIZE	(0x8080) /* 128x128 px */
#define MMI_DC_CURSOR_ENABLE		BIT(28)
#define MMI_DC_CURSOR_POSITION_X_MASK	GENMASK(13, 0)
#define MMI_DC_CURSOR_POSITION_X_SHIFT	(0)
#define MMI_DC_CURSOR_POSITION_Y_MASK	GENMASK(27, 14)
#define MMI_DC_CURSOR_POSITION_Y_SHIFT	(14)

#define MMI_DC_CURSOR_CPP		(2) /* 2 bytes per pixel */

/**
 * struct mmi_dc_shadow_buffer - Intermediate cursor data buffer
 * @dma_addr: buffer DMA address
 * @vmap_addr: mapped buffer virtual address
 * @size: buffer size in bytes
 */
struct mmi_dc_shadow_buffer {
	dma_addr_t	dma_addr;
	void		*vmap_addr;
	size_t		size;
};

/**
 * struct mmi_dc_cursor - MMI DC cursor plane data
 * @base: base MMI DC plane
 * @shadow: cursor shadow buffer
 * @dma: MMI DC DMA channel
 */
struct mmi_dc_cursor {
	struct mmi_dc_plane		base;
	struct mmi_dc_shadow_buffer	shadow;
	struct mmi_dc_dma_chan		*dma;
};

/* ----------------------------------------------------------------------------
 * DC Cursor Ops
 */

/**
 * mmi_dc_ensure_cursor_enabled - Check and enable the cursor if needed
 * @cursor: cursor plane
 */
static void mmi_dc_ensure_cursor_enabled(struct mmi_dc_cursor *cursor)
{
	u32 val = dc_read_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL);

	if (val & MMI_DC_CURSOR_ENABLE)
		return;

	val |= MMI_DC_CURSOR_ENABLE;
	dc_write_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL, val);
}

/**
 * mmi_dc_disable_cursor - Disable the cursor
 * @cursor: cursor plane
 */
static void mmi_dc_disable_cursor(struct mmi_dc_cursor *cursor)
{
	u32 val = dc_read_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL);

	val &= ~MMI_DC_CURSOR_ENABLE;
	dc_write_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL, val);
}

/**
 * mmi_dc_move_cursor - Reposition the cursor
 * @cursor: cursor plane
 * @x: cursor's horizontal position on the screen
 * @y: cursor's vertical position on the screen
 */
static void mmi_dc_move_cursor(struct mmi_dc_cursor *cursor, uint32_t x,
			       uint32_t y)
{
	u32 val = dc_read_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL);

	val = (val & MMI_DC_CURSOR_ENABLE) |
	      (x << MMI_DC_CURSOR_POSITION_X_SHIFT &
		    MMI_DC_CURSOR_POSITION_X_MASK) |
	      (y << MMI_DC_CURSOR_POSITION_Y_SHIFT &
		    MMI_DC_CURSOR_POSITION_Y_MASK);
	dc_write_misc(cursor->base.dc, MMI_DC_CURSOR_CTRL, val);
}

/**
 * mmi_dc_init_cursor - Initialize the cursor
 * @cursor: cursor plane
 */
static void mmi_dc_init_cursor(struct mmi_dc_cursor *cursor)
{
	dc_write_misc(cursor->base.dc, MMI_DC_CURSOR_SIZE,
		      MMI_DC_CURSOR_FIXED_SIZE);
	mmi_dc_disable_cursor(cursor);
}

/* ----------------------------------------------------------------------------
 * DC Cursor Utilities
 */

/**
 * to_cursor - Convert generic MMI DC plane to cursor plane
 * @plane: MMI DC plane
 *
 * Return: Corresponding DC cursor plane.
 */
static inline struct mmi_dc_cursor *to_cursor(struct mmi_dc_plane *plane)
{
	return container_of(plane, struct mmi_dc_cursor, base);
}

/**
 * mmi_dc_cursor_alloc_shadow_buffer - Allocate shadow buffer
 * @cursor: cursor plane
 *
 * Return: 0 on success, error code otherwise.
 */
static int mmi_dc_cursor_alloc_shadow_buffer(struct mmi_dc_cursor *cursor)
{
	struct mmi_dc_shadow_buffer *shadow = &cursor->shadow;

	shadow->size = MMI_DC_CURSOR_WIDTH * MMI_DC_CURSOR_HEIGHT *
		       MMI_DC_CURSOR_CPP;
	shadow->vmap_addr = dma_alloc_coherent(cursor->base.dc->dev,
					       shadow->size, &shadow->dma_addr,
					       GFP_KERNEL);

	return shadow->vmap_addr ? 0 : -ENOMEM;
}

/**
 * mmi_dc_cursor_free_shadow_buffer - Free shadow buffer
 * @cursor: cursor plane
 */
static void mmi_dc_cursor_free_shadow_buffer(struct mmi_dc_cursor *cursor)
{
	struct mmi_dc_shadow_buffer *shadow = &cursor->shadow;

	if (!shadow->vmap_addr)
		return;

	dma_free_coherent(cursor->base.dc->dev, shadow->size,
			  shadow->vmap_addr, shadow->dma_addr);
}

/**
 * mmi_dc_cursor_shadow_copy - Copy from DRM fb to shadow buffer
 * @cursor: cursor plane
 * @fb: DRM framebuffer
 *
 * Copy framebuffer data and convert it from AR24 to AB12.
 */
static void mmi_dc_cursor_shadow_copy(struct mmi_dc_cursor *cursor,
				      struct drm_framebuffer *fb)
{
	struct mmi_dc *dc = cursor->base.dc;
	static const unsigned int num_px = MMI_DC_CURSOR_WIDTH *
					   MMI_DC_CURSOR_HEIGHT;
	struct drm_gem_dma_object *gem = drm_fb_dma_get_gem_obj(fb, 0);
	u32 *src = gem->vaddr;
	u16 *dst = cursor->shadow.vmap_addr;
	unsigned int px;

	dma_sync_single_for_cpu(dc->dev, cursor->shadow.dma_addr,
				cursor->shadow.size, DMA_TO_DEVICE);
	for (px = 0; px < num_px; ++px, ++src, ++dst) {
		*dst = ((*src & GENMASK(7,   0)) >>  4) <<  0 |
		       ((*src & GENMASK(15,  8)) >> 12) << 12 |
		       ((*src & GENMASK(23, 16)) >> 20) <<  8 |
		       ((*src & GENMASK(31, 24)) >> 28) <<  4;
	}
	dma_sync_single_for_device(dc->dev, cursor->shadow.dma_addr,
				   cursor->shadow.size, DMA_TO_DEVICE);
}

/**
 * mmi_dc_cursor_submit_dma - Submit DMA transfer
 * @cursor: cursor plane
 * @state: DRM plane state to update to
 *
 * Prepare and submit DMA transfers.
 */
static void mmi_dc_cursor_submit_dma(struct mmi_dc_cursor *cursor,
				     struct drm_plane_state *state)
{
	size_t size = MMI_DC_CURSOR_WIDTH * MMI_DC_CURSOR_CPP;

	mmi_dc_cursor_shadow_copy(cursor, state->fb);
	mmi_dc_dma_start_transfer(cursor->dma, cursor->shadow.dma_addr, size,
				  size, MMI_DC_CURSOR_HEIGHT, false);
}

/* ----------------------------------------------------------------------------
 * DC Plane Interface Implementation
 */

static void mmi_dc_cursor_destroy(struct mmi_dc_plane *plane)
{
	struct mmi_dc_cursor *cursor = to_cursor(plane);

	if (cursor->dma) {
		mmi_dc_dma_stop_transfer(cursor->dma);
		mmi_dc_dma_release_channel(cursor->dma);
	}

	mmi_dc_cursor_free_shadow_buffer(cursor);
}

static int mmi_dc_cursor_check(struct mmi_dc_plane *plane,
			       struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, &plane->base);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_crtc_state(state, plane_state->crtc);

	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (plane_state->fb &&
	    (plane_state->fb->width != MMI_DC_CURSOR_WIDTH ||
	     plane_state->fb->height != MMI_DC_CURSOR_HEIGHT))
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void mmi_dc_cursor_update(struct mmi_dc_plane *plane,
				 struct drm_atomic_state *state)
{
	struct drm_plane *drm_plane = &plane->base;
	struct mmi_dc_cursor *cursor = to_cursor(plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, drm_plane);

	if (!new_state->fb)
		return;

	if (new_state->fb != old_state->fb)
		mmi_dc_cursor_submit_dma(cursor, new_state);

	mmi_dc_ensure_cursor_enabled(cursor);
	mmi_dc_move_cursor(cursor, new_state->crtc_x, new_state->crtc_y);
}

static void mmi_dc_cursor_disable(struct mmi_dc_plane *plane)
{
	struct mmi_dc_cursor *cursor = to_cursor(plane);

	mmi_dc_dma_stop_transfer(cursor->dma);
	mmi_dc_disable_cursor(cursor);
}

/* ----------------------------------------------------------------------------
 * DC Video Plane Factory
 */

/**
 * mmi_dc_create_cursor_plane - Create and initialize cursor plane
 * @dc: pointer to MMI DC
 * @drm: DRM device
 * @id: DC plane id
 *
 * Return: New overlay DC plane on success or error pointer otherwise.
 */
struct mmi_dc_plane *mmi_dc_create_cursor_plane(struct mmi_dc *dc,
						struct drm_device *drm,
						enum mmi_dc_plane_id id)
{
	static const u32 formats[] = { DRM_FORMAT_ARGB8888 };
	static const dma_addr_t cursor_dma_target = 1;
	struct mmi_dc_cursor *cursor;
	struct mmi_dc_dma_chan *dma_chan;
	int ret;

	cursor = drmm_universal_plane_alloc(drm, struct mmi_dc_cursor,
					    base.base, 0,
					    &mmi_dc_drm_plane_funcs, formats,
					    ARRAY_SIZE(formats), NULL,
					    DRM_PLANE_TYPE_CURSOR, NULL);
	if (IS_ERR(cursor))
		return (void *)cursor;

	cursor->base.id = id;
	cursor->base.dc = dc;
	cursor->base.funcs.destroy = mmi_dc_cursor_destroy;
	cursor->base.funcs.check = mmi_dc_cursor_check;
	cursor->base.funcs.update = mmi_dc_cursor_update;
	cursor->base.funcs.disable = mmi_dc_cursor_disable;

	ret = mmi_dc_cursor_alloc_shadow_buffer(cursor);
	if (ret < 0)
		return ERR_PTR(ret);

	dma_chan = mmi_dc_dma_request_channel(dc->dev, "cur");
	if (IS_ERR(dma_chan))
		return (void *)dma_chan;
	mmi_dc_dma_config_channel(dma_chan, cursor_dma_target, false);
	cursor->dma = dma_chan;

	mmi_dc_init_cursor(cursor);

	drm_plane_helper_add(&cursor->base.base,
			     &mmi_dc_drm_plane_helper_funcs);

	drm_plane_create_zpos_immutable_property(&cursor->base.base, id);

	return &cursor->base;
}
