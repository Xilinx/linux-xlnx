/*
 * Xilinx DRM connector header for Xilinx
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

#ifndef _XILINX_DRM_CONNECTOR_H_
#define _XILINX_DRM_CONNECTOR_H_

struct drm_device;
struct drm_connector;

struct drm_connector *
xilinx_drm_connector_create(struct drm_device *drm,
			    struct drm_encoder *base_encoder);
void xilinx_drm_connector_destroy(struct drm_connector *base_connector);

#endif /* _XILINX_DRM_CONNECTOR_H_ */
