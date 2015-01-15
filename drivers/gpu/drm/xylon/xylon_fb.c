/*
 * Xylon DRM driver fb functions
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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem.h>

#include "xylon_crtc.h"
#include "xylon_drv.h"
#include "xylon_fb.h"
#include "xylon_fbdev.h"

#define fb_to_xylon_drm_fb(x) container_of(x, struct xylon_drm_fb, fb)

struct xylon_drm_fb {
	struct drm_framebuffer fb;
	struct drm_gem_object *obj;
};

static void xylon_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct drm_gem_object *obj;
	struct xylon_drm_fb *xfb = fb_to_xylon_drm_fb(fb);

	drm_framebuffer_cleanup(fb);

	obj = xfb->obj;
	if (obj)
		drm_gem_object_unreference_unlocked(obj);

	kfree(xfb);
}

static int xylon_drm_fb_create_handle(struct drm_framebuffer *fb,
				      struct drm_file *file_priv,
				      unsigned int *handle)
{
	struct xylon_drm_fb *xfb = fb_to_xylon_drm_fb(fb);

	return drm_gem_handle_create(file_priv, xfb->obj, handle);
}

static struct drm_framebuffer_funcs xylon_fb_funcs = {
	.destroy = xylon_drm_fb_destroy,
	.create_handle = xylon_drm_fb_create_handle,
};

struct drm_framebuffer *xylon_drm_fb_init(struct drm_device *dev,
					  struct drm_mode_fb_cmd2 *mode_cmd,
					  struct drm_gem_object *obj)
{
	struct drm_framebuffer *fb;
	struct xylon_drm_fb *xfb;
	int ret;

	xfb = kzalloc(sizeof(*xfb), GFP_KERNEL);
	if (!xfb) {
		DRM_ERROR("failed allocate framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	xfb->obj = obj;

	fb = &xfb->fb;

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &xylon_fb_funcs);
	if (ret) {
		DRM_ERROR("failed framebuffer init\n");
		goto err;
	}

	return fb;

err:
	xylon_drm_fb_destroy(fb);

	return ERR_PTR(ret);
}

struct drm_gem_object *xylon_drm_fb_get_gem_obj(struct drm_framebuffer *fb)
{
	struct xylon_drm_fb *xfb = fb_to_xylon_drm_fb(fb);

	return xfb->obj;
}

static struct drm_framebuffer *
xylon_drm_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		    struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct xylon_drm_device *xdev = dev->dev_private;
	bool res;

	res = xylon_drm_crtc_check_format(xdev->crtc, mode_cmd->pixel_format);
	if (!res) {
		DRM_ERROR("unsupported pixel format %08x\n",
			  mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-EINVAL);

	return xylon_drm_fb_init(dev, mode_cmd, obj);
}

static void xylon_drm_output_poll_changed(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_fbdev_hotplug_event(xdev->fbdev);
}

static const struct drm_mode_config_funcs xylon_drm_mode_config_funcs = {
	.fb_create = xylon_drm_fb_create,
	.output_poll_changed = xylon_drm_output_poll_changed,
};

void xylon_drm_mode_config_init(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_get_fix_parameters(xdev->crtc);

	dev->mode_config.funcs = &xylon_drm_mode_config_funcs;
}
