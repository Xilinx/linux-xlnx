/*
 * Xylon DRM driver plane header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Based on Xilinx DRM plane header.
 * Copyright (C) 2013 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _XYLON_DRM_PLANE_H_
#define _XYLON_DRM_PLANE_H_

enum xylon_drm_plane_op_id {
	XYLON_DRM_PLANE_OP_ID_BACKGROUND_COLOR,
	XYLON_DRM_PLANE_OP_ID_COLOR_TRANSPARENCY,
	XYLON_DRM_PLANE_OP_ID_INTERLACE,
	XYLON_DRM_PLANE_OP_ID_TRANSPARENCY,
	XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR
};

struct xylon_drm_plane_op {
	enum xylon_drm_plane_op_id id;
	u32 param;
};

struct xylon_drm_plane_manager;

void xylon_drm_plane_dpms(struct drm_plane *base_plane, int dpms);
int xylon_drm_plane_fb_set(struct drm_plane *base_plane,
			   struct drm_framebuffer *fb,
			   int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   u32 src_x, u32 src_y,
			   u32 src_w, u32 src_h);
void xylon_drm_plane_commit(struct drm_plane *base_plane);

void
xylon_drm_plane_properties_restore(struct xylon_drm_plane_manager *manager);

int xylon_drm_plane_create_all(struct xylon_drm_plane_manager *manager,
			       unsigned int possible_crtcs,
			       unsigned int primary_id);

struct drm_plane *
xylon_drm_plane_get_base(struct xylon_drm_plane_manager *manager,
			 unsigned int id);

bool xylon_drm_plane_check_format(struct xylon_drm_plane_manager *manager,
				  u32 format);
unsigned int xylon_drm_plane_get_bits_per_pixel(struct drm_plane *base_plane);

int xylon_drm_plane_op(struct drm_plane *base_plane,
		       struct xylon_drm_plane_op *op);

struct xylon_drm_plane_manager *
xylon_drm_plane_probe_manager(struct drm_device *dev,
			      struct xylon_cvc *cvc);

#endif /* _XYLON_DRM_PLANE_H_ */
