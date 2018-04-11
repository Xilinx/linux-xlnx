// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM KMS Framebuffer helper
 *
 *  Copyright (C) 2015 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *
 * Based on drm_fb_cma_helper.c
 *
 *  Copyright (C) 2012 Analog Device Inc.
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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "xlnx_crtc.h"
#include "xlnx_drv.h"
#include "xlnx_fb.h"

#define XLNX_MAX_PLANES	4

struct xlnx_fbdev {
	struct drm_fb_helper fb_helper;
	struct drm_framebuffer *fb;
	unsigned int align;
	unsigned int vres_mult;
};

static inline struct xlnx_fbdev *to_fbdev(struct drm_fb_helper *fb_helper)
{
	return container_of(fb_helper, struct xlnx_fbdev, fb_helper);
}

static struct drm_framebuffer_funcs xlnx_fb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
};

static int
xlnx_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct drm_fb_helper *fb_helper = info->par;
	unsigned int i;
	int ret = 0;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		for (i = 0; i < fb_helper->crtc_count; i++) {
			struct drm_mode_set *mode_set;
			struct drm_crtc *crtc;

			mode_set = &fb_helper->crtc_info[i].mode_set;
			crtc = mode_set->crtc;
			ret = drm_crtc_vblank_get(crtc);
			if (!ret) {
				drm_crtc_wait_one_vblank(crtc);
				drm_crtc_vblank_put(crtc);
			}
		}
		return ret;
	default:
		return -ENOTTY;
	}

	return 0;
}

static struct fb_ops xlnx_fbdev_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_setcmap	= drm_fb_helper_setcmap,
	.fb_ioctl	= xlnx_fb_ioctl,
};

/**
 * xlnx_fbdev_create - Create the fbdev with a framebuffer
 * @fb_helper: fb helper structure
 * @size: framebuffer size info
 *
 * This function is based on drm_fbdev_cma_create().
 *
 * Return: 0 if successful, or the error code.
 */
static int xlnx_fbdev_create(struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *size)
{
	struct xlnx_fbdev *fbdev = to_fbdev(fb_helper);
	struct drm_device *drm = fb_helper->dev;
	struct drm_gem_cma_object *obj;
	struct drm_framebuffer *fb;
	unsigned int bytes_per_pixel;
	unsigned long offset;
	struct fb_info *fbi;
	u32 format;
	const struct drm_format_info *info;
	size_t bytes;
	int ret;

	dev_dbg(drm->dev, "surface width(%d), height(%d) and bpp(%d)\n",
		size->surface_width, size->surface_height, size->surface_bpp);

	size->surface_height *= fbdev->vres_mult;
	bytes_per_pixel = DIV_ROUND_UP(size->surface_bpp, 8);
	bytes = ALIGN(size->surface_width * bytes_per_pixel, fbdev->align);
	bytes *= size->surface_height;

	obj = drm_gem_cma_create(drm, bytes);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	fbi = framebuffer_alloc(0, drm->dev);
	if (!fbi) {
		dev_err(drm->dev, "Failed to allocate framebuffer info.\n");
		ret = -ENOMEM;
		goto err_drm_gem_cma_free_object;
	}

	/* Override the depth given by fb helper with current format value */
	format = xlnx_get_format(drm);
	info = drm_format_info(format);
	if (size->surface_bpp == info->cpp[0] * 8)
		size->surface_depth = info->depth;

	fbdev->fb = drm_gem_fbdev_fb_create(drm, size, fbdev->align, &obj->base,
					    &xlnx_fb_funcs);
	if (IS_ERR(fbdev->fb)) {
		dev_err(drm->dev, "Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(fbdev->fb);
		goto err_framebuffer_release;
	}

	fb = fbdev->fb;
	fb_helper->fb = fb;
	fb_helper->fbdev = fbi;
	fbi->par = fb_helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &xlnx_fbdev_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		dev_err(drm->dev, "Failed to allocate color map.\n");
		goto err_fb_destroy;
	}

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(fbi, fb_helper, fb->width, fb->height);
	fbi->var.yres = fb->height / fbdev->vres_mult;

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	drm->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = (char __iomem *)(obj->vaddr + offset);
	fbi->fix.smem_start = (unsigned long)(obj->paddr + offset);
	fbi->screen_size = bytes;
	fbi->fix.smem_len = bytes;

	return 0;

err_fb_destroy:
	drm_framebuffer_unregister_private(fb);
	drm_gem_fb_destroy(fb);
err_framebuffer_release:
	framebuffer_release(fbi);
err_drm_gem_cma_free_object:
	drm_gem_cma_free_object(&obj->base);
	return ret;
}

static struct drm_fb_helper_funcs xlnx_fb_helper_funcs = {
	.fb_probe = xlnx_fbdev_create,
};

/**
 * xlnx_fb_init - Allocate and initializes the Xilinx framebuffer
 * @drm: DRM device
 * @preferred_bpp: preferred bits per pixel for the device
 * @max_conn_count: maximum number of connectors
 * @align: alignment value for pitch
 * @vres_mult: multiplier for virtual resolution
 *
 * This function is based on drm_fbdev_cma_init().
 *
 * Return: a newly allocated drm_fb_helper struct or a ERR_PTR.
 */
struct drm_fb_helper *
xlnx_fb_init(struct drm_device *drm, int preferred_bpp,
	     unsigned int max_conn_count, unsigned int align,
	     unsigned int vres_mult)
{
	struct xlnx_fbdev *fbdev;
	struct drm_fb_helper *fb_helper;
	int ret;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return ERR_PTR(-ENOMEM);

	fbdev->vres_mult = vres_mult;
	fbdev->align = align;
	fb_helper = &fbdev->fb_helper;
	drm_fb_helper_prepare(drm, fb_helper, &xlnx_fb_helper_funcs);

	ret = drm_fb_helper_init(drm, fb_helper, max_conn_count);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialize drm fb helper.\n");
		goto err_free;
	}

	ret = drm_fb_helper_single_add_all_connectors(fb_helper);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to add connectors.\n");
		goto err_drm_fb_helper_fini;
	}

	ret = drm_fb_helper_initial_config(fb_helper, preferred_bpp);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to set initial hw configuration.\n");
		goto err_drm_fb_helper_fini;
	}

	return fb_helper;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_free:
	kfree(fbdev);
	return ERR_PTR(ret);
}

/**
 * xlnx_fbdev_defio_fini - Free the defio fb
 * @fbi: fb_info struct
 *
 * This function is based on drm_fbdev_cma_defio_fini().
 */
static void xlnx_fbdev_defio_fini(struct fb_info *fbi)
{
	if (!fbi->fbdefio)
		return;

	fb_deferred_io_cleanup(fbi);
	kfree(fbi->fbdefio);
	kfree(fbi->fbops);
}

/**
 * xlnx_fbdev_fini - Free the Xilinx framebuffer
 * @fb_helper: drm_fb_helper struct
 *
 * This function is based on drm_fbdev_cma_fini().
 */
void xlnx_fb_fini(struct drm_fb_helper *fb_helper)
{
	struct xlnx_fbdev *fbdev = to_fbdev(fb_helper);

	drm_fb_helper_unregister_fbi(&fbdev->fb_helper);
	if (fbdev->fb_helper.fbdev)
		xlnx_fbdev_defio_fini(fbdev->fb_helper.fbdev);

	if (fbdev->fb_helper.fb)
		drm_framebuffer_remove(fbdev->fb_helper.fb);

	drm_fb_helper_fini(&fbdev->fb_helper);
	kfree(fbdev);
}

/**
 * xlnx_fb_create - (struct drm_mode_config_funcs *)->fb_create callback
 * @drm: DRM device
 * @file_priv: drm file private data
 * @mode_cmd: mode command for fb creation
 *
 * This functions creates a drm_framebuffer with xlnx_fb_funcs for given mode
 * @mode_cmd. This functions is intended to be used for the fb_create callback
 * function of drm_mode_config_funcs.
 *
 * Return: a drm_framebuffer object if successful, or
 * ERR_PTR from drm_gem_fb_create_with_funcs().
 */
struct drm_framebuffer *
xlnx_fb_create(struct drm_device *drm, struct drm_file *file_priv,
	       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_funcs(drm, file_priv, mode_cmd,
					    &xlnx_fb_funcs);
}
