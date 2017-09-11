/*
 * Xilinx DRM Mixer encoder header
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx encoder driver by Hyun Kwon <hyunk@xilinx.com>
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

#ifndef _XILINX_DRM_MIXER_ENCODER_H_
#define _XILINX_DRM_MIXER_ENCODER_H_

struct drm_device;
struct drm_encoder;

struct drm_encoder *xvmixer_drm_encoder_create(struct drm_device *drm,
					      struct device_node *node);
void xvmixer_drm_encoder_destroy(struct drm_encoder *base_encoder);

#endif /* _XILINX_DRM_MIXER_ENCODER_H_ */
