/*
 * Xylon DRM driver fb device functions
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
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "xylon_crtc.h"
#include "xylon_drv.h"
#include "xylon_fb.h"
#include "xylon_fbdev.h"

struct xylon_drm_fb_device {
	struct drm_fb_helper fb_helper;
};

static struct fb_ops xylon_drm_fbdev_ops = {
	.owner = THIS_MODULE,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_blank = drm_fb_helper_blank,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_setcmap = drm_fb_helper_setcmap,
};

static int xylon_drm_fbdev_create(struct drm_fb_helper *helper,
				  struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = helper->dev;
	struct drm_framebuffer *fb;
	struct drm_gem_cma_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct fb_info *fbi;
	struct xylon_drm_device *xdev = dev->dev_private;
	unsigned long offset;
	unsigned int bytes_per_pixel;
	unsigned int buff_width;
	size_t size;
	int ret;

	ret = xylon_drm_crtc_get_param(xdev->crtc, &buff_width,
				       XYLON_DRM_CRTC_BUFF_WIDTH);
	if (ret)
		return ret;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	memset(&mode_cmd, 0, sizeof(mode_cmd));

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = buff_width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	obj = drm_gem_cma_create(dev, size);
	if (IS_ERR(obj))
		return -ENOMEM;

	fb = xylon_drm_fb_init(dev, &mode_cmd, &obj->base);
	if (IS_ERR(fb)) {
		DRM_ERROR("failed initialize fb\n");
		goto err_fb_init;
	}

	fbi = framebuffer_alloc(0, dev->dev);
	if (!fbi) {
		DRM_ERROR("failed allocate framebuffer info\n");
		ret = -ENOMEM;
		goto err_fb_alloc;
	}

	helper->fb = fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &xylon_drm_fbdev_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("failed allocate color map\n");
		goto err_fb_alloc_cmap;
	}

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	dev->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = (char __iomem *)(obj->vaddr + offset);
	fbi->fix.smem_start = (unsigned long)(obj->paddr + offset);
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	return 0;

err_fb_alloc_cmap:
	drm_framebuffer_unregister_private(fb);
	drm_framebuffer_remove(fb);
err_fb_init:
	framebuffer_release(fbi);
err_fb_alloc:
	drm_gem_cma_free_object(&obj->base);

	return ret;
}

static struct drm_fb_helper_funcs xylon_drm_fbdev_helper_funcs = {
	.fb_probe = xylon_drm_fbdev_create,
};

struct xylon_drm_fb_device *
xylon_drm_fbdev_init(struct drm_device *dev,
		     unsigned int preferred_bpp, unsigned int num_crtc,
		     unsigned int max_conn_count)
{
	struct drm_fb_helper *helper;
	struct xylon_drm_fb_device *fbdev;
	int ret;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("failed allocate fbdev\n");
		return ERR_PTR(-ENOMEM);
	}

	fbdev->fb_helper.funcs = &xylon_drm_fbdev_helper_funcs;
	helper = &fbdev->fb_helper;

	ret = drm_fb_helper_init(dev, helper, num_crtc, max_conn_count);
	if (ret < 0) {
		DRM_ERROR("failed fb init\n");
		goto err_fb_helper_init;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DRM_ERROR("failed add connectors\n");
		goto err_fb_helper_single_add;
	}

	drm_helper_disable_unused_functions(dev);

	if (drm_fb_helper_initial_config(helper, preferred_bpp)) {
		DRM_ERROR("failed fb initial config\n");
		ret = -EINVAL;
		goto err_fb_helper_single_add;
	}

	return fbdev;

err_fb_helper_single_add:
	drm_fb_helper_fini(helper);
err_fb_helper_init:
	kfree(fbdev);

	return ERR_PTR(ret);
}

void xylon_drm_fbdev_fini(struct xylon_drm_fb_device *fbdev)
{
	struct fb_info *info;
	int ret;

	if (fbdev->fb_helper.fbdev) {
		info = fbdev->fb_helper.fbdev;

		ret = unregister_framebuffer(info);
		if (ret < 0)
			DRM_INFO("failed unregister fb\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	drm_framebuffer_unregister_private(fbdev->fb_helper.fb);
	drm_framebuffer_remove(fbdev->fb_helper.fb);

	drm_fb_helper_fini(&fbdev->fb_helper);

	kfree(fbdev);
}

void xylon_drm_fbdev_restore_mode(struct xylon_drm_fb_device *fbdev)
{
	struct drm_device *dev;

	if (fbdev) {
		dev = fbdev->fb_helper.dev;

		drm_modeset_lock_all(dev);
		drm_fb_helper_restore_fbdev_mode(&fbdev->fb_helper);
		drm_modeset_unlock_all(dev);
	}
}

void xylon_drm_fbdev_hotplug_event(struct xylon_drm_fb_device *fbdev)
{
	if (fbdev)
		drm_fb_helper_hotplug_event(&fbdev->fb_helper);
}
