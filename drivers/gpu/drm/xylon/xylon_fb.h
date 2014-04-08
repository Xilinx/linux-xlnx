/*
 * Xylon DRM driver fb functions header
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

#ifndef _XYLON_DRM_FB_H_
#define _XYLON_DRM_FB_H_

struct drm_gem_object *xylon_drm_fb_get_gem_obj(struct drm_framebuffer *fb);
struct drm_framebuffer *xylon_drm_fb_init(struct drm_device *dev,
					  struct drm_mode_fb_cmd2 *mode_cmd,
					  struct drm_gem_object *obj);
void xylon_drm_mode_config_init(struct drm_device *dev);

#endif /* _XYLON_DRM_FB_H_ */
