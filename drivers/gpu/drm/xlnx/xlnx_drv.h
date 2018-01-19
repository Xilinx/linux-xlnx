// SPDX-License-Identifier: GPL-2.0
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

uint32_t xlnx_get_format(struct drm_device *drm);
unsigned int xlnx_get_align(struct drm_device *drm);
struct xlnx_crtc_helper *xlnx_get_crtc_helper(struct drm_device *drm);
struct xlnx_bridge_helper *xlnx_get_bridge_helper(struct drm_device *drm);

#endif /* _XLNX_DRV_H_ */
