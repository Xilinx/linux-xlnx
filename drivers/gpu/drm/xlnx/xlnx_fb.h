// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM KMS Framebuffer helper header
 *
 *  Copyright (C) 2015 - 2018 Xilinx, Inc.
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

#ifndef _XLNX_FB_H_
#define _XLNX_FB_H_

struct drm_fb_helper;

struct drm_framebuffer *
xlnx_fb_create(struct drm_device *drm, struct drm_file *file_priv,
	       const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_fb_helper *
xlnx_fb_init(struct drm_device *drm, int preferred_bpp,
	     unsigned int max_conn_count, unsigned int align,
	     unsigned int vres_mult);
void xlnx_fb_fini(struct drm_fb_helper *fb_helper);

#endif /* _XLNX_FB_H_ */
