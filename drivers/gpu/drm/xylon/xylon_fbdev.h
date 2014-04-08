/*
 * Xylon DRM driver fb device functions header
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
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

#ifndef _XYLON_DRM_FBDEV_H_
#define _XYLON_DRM_FBDEV_H_

struct xylon_drm_fb_device *
xylon_drm_fbdev_init(struct drm_device *dev,
		     unsigned int preferred_bpp, unsigned int num_crtc,
		     unsigned int max_conn_count);
void xylon_drm_fbdev_fini(struct xylon_drm_fb_device *fbdev);
void xylon_drm_fbdev_restore_mode(struct xylon_drm_fb_device *fbdev);
void xylon_drm_fbdev_hotplug_event(struct xylon_drm_fb_device *fbdev);

#endif /* _XYLON_DRM_FBDEV_H_ */
