/*
 * Xylon DRM driver plane functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Based on Xilinx DRM plane driver.
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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/device.h>
#include <linux/platform_device.h>

#include "xylon_drv.h"
#include "xylon_fb.h"
#include "xylon_logicvc_helper.h"
#include "xylon_logicvc_layer.h"
#include "xylon_plane.h"
#include "xylon_property.h"

struct xylon_drm_plane_properties {
	struct drm_property *color_transparency;
	struct drm_property *interlace;
	struct drm_property *transparency;
	struct drm_property *transparent_color;
};

struct xylon_drm_plane {
	struct drm_plane base;
	struct xylon_drm_plane_manager *manager;
	struct xylon_drm_plane_properties properties;
	dma_addr_t paddr;
	u32 x;
	u32 y;
	unsigned int id;
};

struct xylon_drm_plane_manager {
	struct drm_device *dev;
	struct xylon_cvc *cvc;
	struct xylon_drm_plane **plane;
	int planes;
};

#define to_xylon_plane(x) container_of(x, struct xylon_drm_plane, base)

static void
xylon_drm_plane_set_parameters(struct xylon_drm_plane *plane,
			       struct xylon_drm_plane_manager *manager,
			       dma_addr_t paddr, u32 x, u32 y)
{
	plane->paddr = paddr;
	plane->x = x;
	plane->y = y;

	xylon_cvc_layer_set_address(manager->cvc, plane->id, plane->paddr,
				    plane->x, plane->y);
}

void xylon_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xylon_cvc_layer_enable(manager->cvc, plane->id);
		break;
	default:
		xylon_cvc_layer_disable(manager->cvc, plane->id);
		break;
	}
}

void xylon_drm_plane_commit(struct drm_plane *base_plane)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;

	xylon_cvc_layer_update(manager->cvc, plane->id);
}

int xylon_drm_plane_fb_set(struct drm_plane *base_plane,
			   struct drm_framebuffer *fb,
			   int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   u32 src_x, u32 src_y,
			   u32 src_w, u32 src_h)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;
	int id = plane->id;
	int ret;

	if (fb->pixel_format != base_plane->format_types[0]) {
		DRM_ERROR("unsupported pixel format %08x %08x\n",
			  fb->pixel_format, base_plane->format_types[0]);
		return -EINVAL;
	}

	gem_obj = xylon_drm_fb_get_gem_obj(fb);
	if (!gem_obj) {
		DRM_ERROR("failed get gem obj for fb\n");
		return -EINVAL;
	}

	cma_obj = to_drm_gem_cma_obj(gem_obj);

	DRM_DEBUG("paddr: %p, h: %d(%d), v: %d(%d), bpp: %d\n",
		  (void *)cma_obj->paddr, src_w, crtc_x, src_h, crtc_y,
		  fb->bits_per_pixel);

	xylon_drm_plane_set_parameters(plane, manager, cma_obj->paddr,
				       src_x, src_y);

	ret = xylon_cvc_layer_set_size_position(manager->cvc, id,
						src_x, src_y, src_w, src_h,
						crtc_x, crtc_y, crtc_w, crtc_h);
	if (ret) {
		DRM_ERROR("failed setting layer size parameters\n");
		return -EINVAL;
	}

	return 0;
}

static int xylon_drm_plane_update(struct drm_plane *base_plane,
				  struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  int crtc_x, int crtc_y,
				  unsigned int crtc_w, unsigned int crtc_h,
				  u32 src_x, u32 src_y,
				  u32 src_w, u32 src_h)
{
	int ret;

	crtc->x = crtc_x;
	crtc->y = crtc_y;

	ret = xylon_drm_plane_fb_set(base_plane, fb,
				     crtc_x, crtc_y, crtc_w, crtc_h,
				     src_x >> 16, src_y >> 16,
				     src_w >> 16, src_h >> 16);
	if (ret) {
		DRM_ERROR("failed update plane\n");
		return ret;
	}

	xylon_drm_plane_commit(base_plane);

	xylon_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);

	return 0;
}

static int xylon_drm_plane_disable(struct drm_plane *base_plane)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;

	xylon_cvc_layer_disable(manager->cvc, plane->id);

	xylon_drm_plane_set_parameters(plane, manager, 0, 0, 0);

	return 0;
}

static void xylon_drm_plane_destroy(struct drm_plane *base_plane)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;
	int id = plane->id;

	xylon_cvc_layer_disable(manager->cvc, id);

	drm_plane_cleanup(base_plane);
}

static int xylon_drm_plane_set_property(struct drm_plane *base_plane,
					struct drm_property *property,
					u64 value)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_properties *props = &plane->properties;
	struct xylon_drm_plane_op op;
	unsigned int val = (unsigned int)value;

	if (property == props->color_transparency) {
		op.id = XYLON_DRM_PLANE_OP_ID_COLOR_TRANSPARENCY;
		op.param = (bool)val;
	} else if (property == props->interlace) {
		op.id = XYLON_DRM_PLANE_OP_ID_INTERLACE;
		op.param = (bool)val;
	} else if (property == props->transparency) {
		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENCY;
		op.param = (u32)val;
	} else if (property == props->transparent_color) {
		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR;
		op.param = (u32)val;
	} else {
		return -EINVAL;
	}

	xylon_drm_plane_op(base_plane, &op);

	return 0;
}

static const struct drm_plane_funcs xylon_drm_plane_funcs = {
	.update_plane = xylon_drm_plane_update,
	.disable_plane = xylon_drm_plane_disable,
	.destroy = xylon_drm_plane_destroy,
	.set_property = xylon_drm_plane_set_property,
};

static int xylon_drm_plane_create_properties(struct drm_plane *base_plane)
{
	struct drm_device *dev = base_plane->dev;
	struct drm_mode_object *obj = &base_plane->base;
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_properties *props = &plane->properties;
	int size;
	bool bg_layer = xylon_cvc_get_info(plane->manager->cvc,
					   LOGICVC_INFO_BACKGROUND_LAYER,
					   0);
	bool last_plane = xylon_cvc_get_info(plane->manager->cvc,
					     LOGICVC_INFO_LAST_LAYER,
					     plane->id);

	if (bg_layer || !last_plane) {
		size = xylon_drm_property_size(property_color_transparency);
		if (xylon_drm_property_create_list(dev, obj,
						   &props->color_transparency,
						   property_color_transparency,
						   "color_transparency",
						   size))
			return -EINVAL;
	}
	if (bg_layer || !last_plane) {
		if (xylon_drm_property_create_range(dev, obj,
					    &props->transparency,
					    "transparency",
					    XYLON_DRM_PROPERTY_ALPHA_MIN,
					    XYLON_DRM_PROPERTY_ALPHA_MAX,
					    XYLON_DRM_PROPERTY_ALPHA_MAX))
			return -EINVAL;
	}
	if (bg_layer || !last_plane) {
		if (xylon_drm_property_create_range(dev, obj,
					    &props->transparent_color,
					    "transparent_color",
					    XYLON_DRM_PROPERTY_COLOR_MIN,
					    XYLON_DRM_PROPERTY_COLOR_MAX,
					    XYLON_DRM_PROPERTY_COLOR_MIN))
			return -EINVAL;
	}
	size = xylon_drm_property_size(property_interlace);
	if (xylon_drm_property_create_list(dev, obj,
					   &props->interlace,
					   property_interlace,
					   "interlace",
					   size))
		return -EINVAL;

	return 0;
}

static void
xylon_drm_plane_properties_initial_value(struct drm_plane *base_plane)
{
	struct drm_mode_object *obj = &base_plane->base;
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_properties *props = &plane->properties;
	struct xylon_drm_plane_op op;
	bool val;

	if (props->color_transparency) {
		op.id = XYLON_DRM_PLANE_OP_ID_COLOR_TRANSPARENCY;
		op.param = false;
		xylon_drm_plane_op(base_plane, &op);

		val = xylon_cvc_get_info(plane->manager->cvc,
					 LOGICVC_INFO_LAYER_COLOR_TRANSPARENCY,
					 plane->id);
		drm_object_property_set_value(obj, props->color_transparency,
					      val);
	}
}

static struct drm_plane *
xylon_drm_plane_create(struct xylon_drm_plane_manager *manager,
		       unsigned int possible_crtcs, bool primary,
		       unsigned int primary_id)
{
	struct device *dev = manager->dev->dev;
	struct xylon_drm_plane *plane;
	struct xylon_cvc *cvc = manager->cvc;
	enum drm_plane_type type;
	u32 format;
	int i, ret;

	if (primary) {
		i = primary_id;
	} else {
		for (i = 0; i < manager->planes; i++)
			if (!manager->plane[i])
				break;
	}

	if (i >= manager->planes) {
		DRM_ERROR("failed get plane\n");
		return ERR_PTR(-ENODEV);
	}

	plane = devm_kzalloc(dev, sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("failed allocate plane\n");
		return ERR_PTR(-ENOMEM);
	}

	plane->id = i;

	format = xylon_cvc_layer_get_format(cvc, i);
	if (primary)
		type = DRM_PLANE_TYPE_PRIMARY;
	else
		type = DRM_PLANE_TYPE_OVERLAY;

	ret = drm_universal_plane_init(manager->dev, &plane->base,
				       possible_crtcs, &xylon_drm_plane_funcs,
				       &format, 1, type);
	if (ret) {
		DRM_ERROR("failed initialize plane\n");
		goto err_init;
	}
	plane->manager = manager;
	manager->plane[i] = plane;

	xylon_drm_plane_create_properties(&plane->base);

	xylon_drm_plane_properties_initial_value(&plane->base);

	return &plane->base;

err_init:
	xylon_cvc_layer_disable(cvc, plane->id);

	return ERR_PTR(ret);
}

void xylon_drm_plane_properties_restore(struct xylon_drm_plane_manager *manager)
{
	struct drm_mode_object *obj;
	struct drm_plane *base_plane;
	struct xylon_drm_plane *plane;
	struct xylon_drm_plane_properties *props;
	struct xylon_drm_plane_op op;
	int i;

	for (i = 0; i < manager->planes; i++) {
		if (!manager->plane[i])
			continue;

		plane = manager->plane[i];
		base_plane = &plane->base;
		obj = &base_plane->base;
		props = &plane->properties;

		op.id = XYLON_DRM_PLANE_OP_ID_COLOR_TRANSPARENCY;
		op.param = false;
		xylon_drm_plane_op(base_plane, &op);
		drm_object_property_set_value(obj, props->color_transparency,
					      false);

		op.id = XYLON_DRM_PLANE_OP_ID_INTERLACE;
		xylon_drm_plane_op(base_plane, &op);
		drm_object_property_set_value(obj, props->interlace, false);

		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENCY;
		op.param = XYLON_DRM_PROPERTY_ALPHA_MAX;
		xylon_drm_plane_op(base_plane, &op);
		drm_object_property_set_value(obj, props->transparency,
					      XYLON_DRM_PROPERTY_ALPHA_MAX);

		op.id = XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR;
		op.param = XYLON_DRM_PROPERTY_COLOR_MIN;
		xylon_drm_plane_op(base_plane, &op);
		drm_object_property_set_value(obj, props->transparent_color,
					      XYLON_DRM_PROPERTY_COLOR_MIN);
	}
}

int xylon_drm_plane_create_all(struct xylon_drm_plane_manager *manager,
			       unsigned int possible_crtcs,
			       unsigned int primary_id)
{
	struct drm_plane *plane;
	int i, ret;
	bool primary;

	for (i = 0; i < manager->planes; i++) {
		if (manager->plane[i])
			continue;

		primary = false;
		if (i == primary_id)
			primary = true;

		plane = xylon_drm_plane_create(manager, possible_crtcs,
					       primary, primary_id);
		if (IS_ERR(plane)) {
			DRM_ERROR("failed allocate plane\n");
			ret = PTR_ERR(plane);
			goto err_out;
		}
	}

	return 0;

err_out:
	return ret;
}

struct drm_plane *
xylon_drm_plane_get_base(struct xylon_drm_plane_manager *manager,
			 unsigned int id)
{
	return &manager->plane[id]->base;
}

bool xylon_drm_plane_check_format(struct xylon_drm_plane_manager *manager,
				  u32 format)
{
	struct drm_plane *base_plane;
	int i;

	for (i = 0; i < manager->planes; i++) {
		if (manager->plane[i]) {
			base_plane = &manager->plane[i]->base;
			if (format == base_plane->format_types[0])
				return true;
		}
	}

	return false;
}

unsigned int xylon_drm_plane_get_bits_per_pixel(struct drm_plane *base_plane)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;

	return xylon_cvc_layer_get_bits_per_pixel(manager->cvc, plane->id);
}

int xylon_drm_plane_op(struct drm_plane *base_plane,
		       struct xylon_drm_plane_op *op)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base_plane);
	struct xylon_drm_plane_manager *manager = plane->manager;
	struct xylon_cvc *cvc = manager->cvc;
	int id = plane->id;
	int par;

	switch (op->id) {
	case XYLON_DRM_PLANE_OP_ID_BACKGROUND_COLOR:
		xylon_cvc_layer_set_color_reg(cvc,
					      BACKGROUND_LAYER_ID,
					      op->param);
		return 0;
	case XYLON_DRM_PLANE_OP_ID_TRANSPARENCY:
		xylon_cvc_layer_set_alpha(cvc, id, op->param);
		return 0;
	case XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR:
		xylon_cvc_layer_set_color_reg(cvc, id, op->param);
		return 0;
	case XYLON_DRM_PLANE_OP_ID_COLOR_TRANSPARENCY:
		if (op->param)
			par = LOGICVC_LAYER_COLOR_TRANSPARENCY_ENABLE;
		else
			par = LOGICVC_LAYER_COLOR_TRANSPARENCY_DISABLE;
		break;
	case XYLON_DRM_PLANE_OP_ID_INTERLACE:
		if (op->param)
			par = LOGICVC_LAYER_INTERLACE_ENABLE;
		else
			par = LOGICVC_LAYER_INTERLACE_DISABLE;
		break;
	}

	xylon_cvc_layer_ctrl(cvc, id, par);

	return 0;
}

struct xylon_drm_plane_manager *
xylon_drm_plane_probe_manager(struct drm_device *drm_dev,
			      struct xylon_cvc *cvc)
{
	struct device *dev = drm_dev->dev;
	struct xylon_drm_plane **plane;
	struct xylon_drm_plane_manager *manager;

	manager = devm_kzalloc(dev, sizeof(*manager), GFP_KERNEL);
	if (!manager)
		return ERR_PTR(-ENOMEM);

	manager->dev = drm_dev;
	manager->cvc = cvc;
	manager->planes = xylon_cvc_layer_get_total_count(cvc);

	plane = devm_kzalloc(dev, sizeof(**plane) * manager->planes,
			     GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	manager->plane = plane;

	DRM_DEBUG("%d %s\n", manager->planes,
		  manager->planes == 1 ? "plane" : "planes");

	return manager;
}
