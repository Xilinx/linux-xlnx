/*
 * Xilinx DRM Mixer connector header
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx connector driver by Hyun Kwon <hyunk@xilinx.com>
 *  Copyright (C) 2013
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

#ifndef _XILINX_DRM_MIXER_CONNECTOR_H_
#define _XILINX_DRM_MIXER_CONNECTOR_H_

struct drm_device;
struct drm_connector;

struct drm_connector *
xvmixer_drm_connector_create(struct drm_device *drm,
			    struct drm_encoder *base_encoder, int id);
void xvmixer_drm_connector_destroy(struct drm_connector *base_connector);

#endif /* _XILINX_DRM_MIXER_CONNECTOR_H_ */
