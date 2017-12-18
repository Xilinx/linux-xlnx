/*
 * Xilinx DRM plane header for Xilinx
 *
 *  Copyright (C) 2013 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
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

#ifndef _XILINX_DRM_PLANE_H_
#define _XILINX_DRM_PLANE_H_

struct drm_crtc;
struct drm_plane;

/* plane operations */
void xilinx_drm_plane_dpms(struct drm_plane *base_plane, int dpms);
void xilinx_drm_plane_commit(struct drm_plane *base_plane);
int xilinx_drm_plane_mode_set(struct drm_plane *base_plane,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      u32 src_x, u32 src_y,
			      u32 src_w, u32 src_h);
int xilinx_drm_plane_get_max_width(struct drm_plane *base_plane);
u32 xilinx_drm_plane_get_format(struct drm_plane *base_plane);
unsigned int xilinx_drm_plane_get_align(struct drm_plane *base_plane);

/* plane manager operations */
struct xilinx_drm_plane_manager;

void
xilinx_drm_plane_manager_mode_set(struct xilinx_drm_plane_manager *manager,
				  unsigned int crtc_w, unsigned int crtc_h);
void xilinx_drm_plane_manager_dpms(struct xilinx_drm_plane_manager *manager,
				   int dpms);
struct drm_plane *
xilinx_drm_plane_create_primary(struct xilinx_drm_plane_manager *manager,
				unsigned int possible_crtcs);
int xilinx_drm_plane_create_planes(struct xilinx_drm_plane_manager *manager,
				   unsigned int possible_crtcs);

bool xilinx_drm_plane_check_format(struct xilinx_drm_plane_manager *manager,
				   u32 format);
int xilinx_drm_plane_get_num_planes(struct xilinx_drm_plane_manager *manager);

void xilinx_drm_plane_restore(struct xilinx_drm_plane_manager *manager);

struct xilinx_drm_plane_manager *
xilinx_drm_plane_probe_manager(struct drm_device *drm);
void xilinx_drm_plane_remove_manager(struct xilinx_drm_plane_manager *manager);

#endif /* _XILINX_DRM_PLANE_H_ */
