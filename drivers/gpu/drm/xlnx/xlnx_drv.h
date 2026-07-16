/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx DRM KMS Header for Xilinx
 *
 *  Copyright (C) 2013 - 2018 Xilinx, Inc.
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

#ifndef _XLNX_DRV_H_
#define _XLNX_DRV_H_

struct drm_device;
struct xlnx_crtc_helper;

struct platform_device *xlnx_drm_pipeline_init(struct platform_device *parent);
void xlnx_drm_pipeline_exit(struct platform_device *pipeline);

struct platform_device *xlnx_drm_get_next_master(struct platform_device *master);
int xlnx_drm_register_component(struct platform_device *master,
				struct platform_device *component);

uint32_t xlnx_get_format(struct drm_device *drm);
unsigned int xlnx_get_align(struct drm_device *drm);
struct xlnx_crtc_helper *xlnx_get_crtc_helper(struct drm_device *drm);
struct xlnx_bridge_helper *xlnx_get_bridge_helper(struct drm_device *drm);

#if defined(CONFIG_DRM_FBDEV_EMULATION)
struct drm_fb_helper;
struct drm_fb_helper_surface_size;

int xlnx_fbdev_probe(struct drm_fb_helper *fb_helper,
		     struct drm_fb_helper_surface_size *size);
#define XLNX_DRM_FBDEV_DRIVER_OPS \
       .fbdev_probe = xlnx_fbdev_probe
#else
#define XLNX_DRM_FBDEV_DRIVER_OPS \
       .fbdev_probe = NULL
#endif

#endif /* _XLNX_DRV_H_ */
