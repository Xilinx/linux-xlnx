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
#include "xylon_logicvc.h"
#include "xylon_logicvc_helper.h"
#include "xylon_plane.h"

struct xylon_drm_plane {
	struct drm_plane base;
	struct xylon_drm_plane_manager *manager;
	dma_addr_t paddr;
	u32 format;
	u32 x;
	u32 y;
	int bpp;
	int id;
	bool priv;
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

void xylon_drm_plane_dpms(struct drm_plane *base, int dpms)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);
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

void xylon_drm_plane_commit(struct drm_plane *base)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);
	struct xylon_drm_plane_manager *manager = plane->manager;

	xylon_cvc_layer_update(manager->cvc, plane->id);
}

int xylon_drm_plane_fb_set(struct drm_plane *base,
			   struct drm_framebuffer *fb,
			   int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   u32 src_x, u32 src_y,
			   u32 src_w, u32 src_h)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	struct xylon_drm_plane *plane = to_xylon_plane(base);
	struct xylon_drm_plane_manager *manager = plane->manager;
	int id = plane->id;
	int ret;

	if (fb->pixel_format != plane->format) {
		DRM_ERROR("unsupported pixel format %08x %08x\n",
			  fb->pixel_format, plane->format);
		return -EINVAL;
	}

	ret = xylon_cvc_layer_set_size_position(manager->cvc, id,
						src_x, src_y, src_w, src_h,
						crtc_x, crtc_y, crtc_w, crtc_h);
	if (ret) {
		DRM_ERROR("failed setting layer size parameters\n");
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

	return 0;
}

static int xylon_drm_plane_update(struct drm_plane *base,
				  struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  int crtc_x, int crtc_y,
				  unsigned int crtc_w, unsigned int crtc_h,
				  u32 src_x, u32 src_y,
				  u32 src_w, u32 src_h)
{
	int ret;

	ret = xylon_drm_plane_fb_set(base, fb, crtc_x, crtc_y, crtc_w, crtc_h,
				     src_x >> 16, src_y >> 16,
				     src_w >> 16, src_h >> 16);
	if (ret) {
		DRM_ERROR("failed update plane\n");
		return ret;
	}

	xylon_drm_plane_commit(base);

	xylon_drm_plane_dpms(base, DRM_MODE_DPMS_ON);

	return 0;
}

static int xylon_drm_plane_disable(struct drm_plane *base)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);
	struct xylon_drm_plane_manager *manager = plane->manager;

	xylon_cvc_layer_disable(manager->cvc, plane->id);

	xylon_drm_plane_set_parameters(plane, manager, 0, 0, 0);

	return 0;
}

void xylon_drm_plane_destroy(struct drm_plane *base)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);
	struct xylon_drm_plane_manager *manager = plane->manager;
	int id = plane->id;

	xylon_cvc_layer_disable(manager->cvc, id);

	drm_plane_cleanup(base);
}

static int xylon_drm_plane_set_property(struct drm_plane *base,
					struct drm_property *property,
					u64 val)
{
	return -EINVAL;
}

static struct drm_plane_funcs xylon_drm_plane_funcs = {
	.update_plane = xylon_drm_plane_update,
	.disable_plane = xylon_drm_plane_disable,
	.destroy = xylon_drm_plane_destroy,
	.set_property = xylon_drm_plane_set_property,
};

struct drm_plane *
xylon_drm_plane_create(struct xylon_drm_plane_manager *manager,
		       unsigned int possible_crtcs, bool priv, int priv_id)
{
	struct device *dev = manager->dev->dev;
	struct xylon_drm_plane *plane;
	struct xylon_cvc *cvc = manager->cvc;
	int i, ret;

	if (priv) {
		i = priv_id;
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

	plane->format = xylon_cvc_layer_get_format(cvc, i);
	plane->bpp = xylon_cvc_layer_get_bits_per_pixel(cvc, i);
	plane->id = i;
	plane->priv = priv;

	ret = drm_plane_init(manager->dev, &plane->base, possible_crtcs,
			     &xylon_drm_plane_funcs, &plane->format, 1, priv);
	if (ret) {
		DRM_ERROR("failed initialize plane\n");
		goto err_init;
	}
	plane->manager = manager;
	manager->plane[i] = plane;

	return &plane->base;

err_init:
	xylon_cvc_layer_disable(cvc, plane->id);

	return ERR_PTR(ret);
}

void xylon_drm_plane_destroy_all(struct xylon_drm_plane_manager *manager)
{
	struct xylon_drm_plane *plane;
	int i;

	for (i = 0; i < manager->planes; i++) {
		plane = manager->plane[i];
		if (plane && !plane->priv) {
			xylon_drm_plane_destroy(&plane->base);
			manager->plane[i] = NULL;
		}
	}
}

int xylon_drm_plane_create_all(struct xylon_drm_plane_manager *manager,
			       unsigned int possible_crtcs)
{
	struct drm_plane *plane;
	int i, ret;

	for (i = 0; i < manager->planes; i++) {
		if (manager->plane[i])
			continue;
		plane = xylon_drm_plane_create(manager, possible_crtcs,
					       false, 0);
		if (IS_ERR(plane)) {
			DRM_ERROR("failed allocate plane\n");
			ret = PTR_ERR(plane);
			goto err_out;
		}
	}

	return 0;

err_out:
	xylon_drm_plane_destroy_all(manager);
	return ret;
}

bool xylon_drm_plane_check_format(struct xylon_drm_plane_manager *manager,
				  u32 format)
{
	int i;

	for (i = 0; i < manager->planes; i++)
		if (manager->plane[i] && (manager->plane[i]->format == format))
			return true;

	return false;
}

int xylon_drm_plane_get_bits_per_pixel(struct drm_plane *base)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);

	return plane->bpp;
}

int xylon_drm_plane_op(struct drm_plane *base, struct xylon_drm_plane_op *op)
{
	struct xylon_drm_plane *plane = to_xylon_plane(base);
	struct xylon_drm_plane_manager *manager = plane->manager;
	struct xylon_cvc *cvc = manager->cvc;
	int id = plane->id;
	int param;

	switch (op->id) {
	case XYLON_DRM_PLANE_OP_ID_CTRL:
		param = LOGICVC_LAYER_CTRL_NONE;
		switch (op->sid) {
		case XYLON_DRM_PLANE_OP_SID_CTRL_COLOR_TRANSPARENCY:
			switch (op->param) {
			case XYLON_DRM_PLANE_OP_DISABLE:
				param = LOGICVC_LAYER_CTRL_COLOR_TRANSP_DISABLE;
				break;
			case XYLON_DRM_PLANE_OP_ENABLE:
				param = LOGICVC_LAYER_CTRL_COLOR_TRANSP_ENABLE;
				break;
			}
			break;
		case XYLON_DRM_PLANE_OP_SID_CTRL_PIXEL_FORMAT:
			switch (op->param) {
			case XYLON_DRM_PLANE_OP_PIXEL_FORMAT_NORMAL:
				param = LOGICVC_LAYER_CTRL_PIXEL_FORMAT_NORMAL;
				break;
			case XYLON_DRM_PLANE_OP_PIXEL_FORMAT_ANDROID:
				param = LOGICVC_LAYER_CTRL_PIXEL_FORMAT_ANDROID;
				break;
			}
			break;
		default:
			return -EINVAL;
		}
		xylon_cvc_layer_ctrl(cvc, id, param);
		break;
	case XYLON_DRM_PLANE_OP_ID_TRANSPARENCY:
		xylon_cvc_layer_set_alpha(cvc, id, op->param);
		break;
	case XYLON_DRM_PLANE_OP_ID_TRANSPARENT_COLOR:
		xylon_cvc_set_hw_color(cvc, id, op->param);
		break;
	case XYLON_DRM_PLANE_OP_ID_BACKGORUND_COLOR:
		xylon_cvc_set_hw_color(cvc, CVC_BACKGROUND_LAYER, op->param);
		break;
	default:
		return -EINVAL;
	}

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
	manager->planes = xylon_cvc_get_layers_num(cvc);

	plane = devm_kzalloc(dev, sizeof(**plane) * manager->planes,
			     GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	manager->plane = plane;

	DRM_DEBUG("%d %s\n", manager->planes,
		  manager->planes == 1 ? "plane" : "planes");

	return manager;
}

void xylon_drm_plane_remove_manager(struct xylon_drm_plane_manager *manager)
{
	int i;

	for (i = 0; i < manager->planes; i++) {
		if (manager->plane[i] && !manager->plane[i]->priv) {
			xylon_drm_plane_destroy(&manager->plane[i]->base);
			manager->plane[i] = NULL;
		}
	}

	manager->plane = NULL;
}
