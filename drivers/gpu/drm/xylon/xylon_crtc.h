/*
 * Xylon DRM driver CRTC header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Based on Xilinx DRM crtc header.
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

#ifndef _XYLON_DRM_CRTC_H_
#define _XYLON_DRM_CRTC_H_

#ifndef KHZ
#define KHZ (1000)
#endif

#define XYLON_DRM_CRTC_BUFF_WIDTH  0
#define XYLON_DRM_CRTC_BUFF_HEIGHT 1

void xylon_drm_crtc_vblank(struct drm_crtc *base_crtc, bool enabled);

void xylon_drm_crtc_int_handle(struct drm_crtc *base_crtc);
void xylon_drm_crtc_int_hw_enable(struct drm_crtc *base_crtc);
void xylon_drm_crtc_int_hw_disable(struct drm_crtc *base_crtc);
int xylon_drm_crtc_int_request(struct drm_crtc *base_crtc, unsigned long flags,
			       irq_handler_t handler, void *dev);
void xylon_drm_crtc_int_free(struct drm_crtc *base_crtc, void *dev);

void xylon_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
				     struct drm_file *file);

void xylon_drm_crtc_get_fix_parameters(struct drm_crtc *base_crtc);
int xylon_drm_crtc_get_bits_per_pixel(struct drm_crtc *base_crtc);
bool xylon_drm_crtc_check_format(struct drm_crtc *base_crtc, u32 fourcc);
int xylon_drm_crtc_get_param(struct drm_crtc *base_crtc, unsigned int *p,
			     int cmd);

struct drm_crtc *xylon_drm_crtc_create(struct drm_device *dev);
void xylon_drm_crtc_destroy(struct drm_crtc *base_crtc);

#endif /* _XYLON_DRM_CRTC_H_ */
