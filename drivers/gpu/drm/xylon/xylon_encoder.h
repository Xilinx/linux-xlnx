/*
 * Xylon DRM encoder functions header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Reused Xilinx DRM encoder header.
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

#ifndef _XYLON_DRM_ENCODER_H_
#define _XYLON_DRM_ENCODER_H_

struct drm_encoder *xylon_drm_encoder_create(struct drm_device *dev);
void xylon_drm_encoder_destroy(struct drm_encoder *base_encoder);

#endif /* _XYLON_DRM_ENCODER_H_ */
