/*
 * Xilinx DRM KMS Framebuffer helper header
 *
 *  Copyright (C) 2015 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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

#ifndef _XILINX_DRM_FB_H_
#define _XILINX_DRM_FB_H_

struct drm_fb_helper;

struct drm_gem_cma_object *
xilinx_drm_fb_get_gem_obj(struct drm_framebuffer *base_fb, unsigned int plane);

struct drm_fb_helper *
xilinx_drm_fb_init(struct drm_device *drm, unsigned int preferred_bpp,
		   unsigned int num_crtc, unsigned int max_conn_count,
		   unsigned int align);
void xilinx_drm_fb_fini(struct drm_fb_helper *fb_helper);

void xilinx_drm_fb_restore_mode(struct drm_fb_helper *fb_helper);
struct drm_framebuffer *xilinx_drm_fb_create(struct drm_device *drm,
					     struct drm_file *file_priv,
					     struct drm_mode_fb_cmd2 *mode_cmd);
void xilinx_drm_fb_hotplug_event(struct drm_fb_helper *fb_helper);

#endif /* _XILINX_DRM_FB_H_ */
