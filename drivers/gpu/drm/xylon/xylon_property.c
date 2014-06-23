/*
 * Xylon DRM property
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

#include <drm/drm_crtc.h>

#include "xylon_property.h"

const struct drm_prop_enum_list property_layer_update[] = {
	{ 0, "Layer Update Disable" },
	{ 1, "Layer Update Enable" },
	{/* end */}
};

const struct drm_prop_enum_list property_pixel_data_polarity[] = {
	{ 0, "Pixel Data Polarity Normal" },
	{ 1, "Pixel Data Polarity Invert" },
	{/* end */}
};

const struct drm_prop_enum_list property_pixel_data_trigger[] = {
	{ 0, "Pixel Data Trigger Falling" },
	{ 1, "Pixel Data Trigger Rising" },
	{/* end */}
};

const struct drm_prop_enum_list property_control[] = {
	{ 0, "Plane Disable" },
	{ 1, "Plane Enable" },
	{/* end */}
};

const struct drm_prop_enum_list property_color_transparency[] = {
	{ 0, "Plane Color Transparency Disable" },
	{ 1, "Plane Color Transparency Enable" },
	{/* end */}
};

const struct drm_prop_enum_list property_interlace[] = {
	{ 0, "Plane Interlace Disable" },
	{ 1, "Plane Interlace Enable" },
	{/* end */}
};

const struct drm_prop_enum_list property_pixel_format[] = {
	{ 0, "Plane ABGR Format Disable" },
	{ 1, "Plane ABGR Format Enable" },
	{/* end */}
};

int xylon_drm_property_size(const struct drm_prop_enum_list *list)
{
	int i = 0;

	while (list[i].name != NULL)
		i++;

	return i;
}

int xylon_drm_property_create_list(struct drm_device *dev,
				   struct drm_mode_object *obj,
				   struct drm_property **prop,
				   const struct drm_prop_enum_list *list,
				   const char *name,
				   int size)
{
	if (*prop)
		return 0;

	*prop = drm_property_create_enum(dev, 0, name, list, size);
	if (*prop == NULL)
		return -EINVAL;

	drm_object_attach_property(obj, *prop, 0);

	return 0;
}

int xylon_drm_property_create_range(struct drm_device *dev,
				    struct drm_mode_object *obj,
				    struct drm_property **prop,
				    const char *name,
				    u64 min, u64 max, u64 init)
{
	if (*prop)
		return 0;

	*prop = drm_property_create_range(dev, 0, name, min, max);
	if (*prop == NULL)
		return -EINVAL;

	drm_object_attach_property(obj, *prop, init);

	return 0;
}
