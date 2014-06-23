/*
 * Xylon DRM property header
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

#ifndef __XYLON_DRM_PROPERTY_H__
#define __XYLON_DRM_PROPERTY_H__

#define XYLON_DRM_PROPERTY_ALPHA_MIN 0
#define XYLON_DRM_PROPERTY_ALPHA_MAX 255
#define XYLON_DRM_PROPERTY_COLOR_MIN 0
#define XYLON_DRM_PROPERTY_COLOR_MAX 0xFFFFFFFF

extern const struct drm_prop_enum_list property_layer_update[];
extern const struct drm_prop_enum_list property_pixel_data_polarity[];
extern const struct drm_prop_enum_list property_pixel_data_trigger[];
extern const struct drm_prop_enum_list property_control[];
extern const struct drm_prop_enum_list property_color_transparency[];
extern const struct drm_prop_enum_list property_interlace[];
extern const struct drm_prop_enum_list property_pixel_format[];

int xylon_drm_property_size(const struct drm_prop_enum_list *list);

int xylon_drm_property_create_list(struct drm_device *dev,
				   struct drm_mode_object *base,
				   struct drm_property **prop,
				   const struct drm_prop_enum_list *list,
				   const char *name,
				   int size);
int xylon_drm_property_create_range(struct drm_device *dev,
				    struct drm_mode_object *base,
				    struct drm_property **prop,
				    const char *name,
				    u64 min, u64 max, u64 init);

#endif /* __XYLON_DRM_PROPERTY_H__ */
