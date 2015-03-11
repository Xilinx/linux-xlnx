/*
 * Xilinx DRM KMS Framebuffer helper
 *
 *  Copyright (C) 2015 Xilinx, Inc.
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

#include "xilinx_drm_drv.h"
#include "xilinx_drm_fb.h"

struct xilinx_drm_fb {
	struct drm_framebuffer		base;
	struct drm_gem_cma_object	*obj[4];
};

struct xilinx_drm_fbdev {
	struct drm_fb_helper	fb_helper;
	struct xilinx_drm_fb	*fb;
	unsigned int align;
};

static inline struct xilinx_drm_fbdev *to_fbdev(struct drm_fb_helper *fb_helper)
{
	return container_of(fb_helper, struct xilinx_drm_fbdev, fb_helper);
}

static inline struct xilinx_drm_fb *to_fb(struct drm_framebuffer *base_fb)
{
	return container_of(base_fb, struct xilinx_drm_fb, base);
}

static void xilinx_drm_fb_destroy(struct drm_framebuffer *base_fb)
{
	struct xilinx_drm_fb *fb = to_fb(base_fb);
	int i;

	for (i = 0; i < 4; i++)
		if (fb->obj[i])
			drm_gem_object_unreference_unlocked(&fb->obj[i]->base);

	drm_framebuffer_cleanup(base_fb);
	kfree(fb);
}

static int xilinx_drm_fb_create_handle(struct drm_framebuffer *base_fb,
				       struct drm_file *file_priv,
				       unsigned int *handle)
{
	struct xilinx_drm_fb *fb = to_fb(base_fb);

	return drm_gem_handle_create(file_priv, &fb->obj[0]->base, handle);
}

static struct drm_framebuffer_funcs xilinx_drm_fb_funcs = {
	.destroy	= xilinx_drm_fb_destroy,
	.create_handle	= xilinx_drm_fb_create_handle,
};

/**
 * xilinx_drm_fb_alloc - Allocate a xilinx_drm_fb
 * @drm: DRM object
 * @mode_cmd: drm_mode_fb_cmd2 struct
 * @obj: pointers for returned drm_gem_cma_objects
 * @num_planes: number of planes to be allocated
 *
 * This function is based on drm_fb_cma_alloc().
 *
 * Return: a xilinx_drm_fb object, or ERR_PTR.
 */
static struct xilinx_drm_fb *
xilinx_drm_fb_alloc(struct drm_device *drm, struct drm_mode_fb_cmd2 *mode_cmd,
		    struct drm_gem_cma_object **obj, unsigned int num_planes)
{
	struct xilinx_drm_fb *fb;
	int ret;
	int i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = obj[i];

	ret = drm_framebuffer_init(drm, &fb->base, &xilinx_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("Failed to initialize framebuffer: %d\n", ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

/**
 * xilinx_drm_fb_get_gem_obj - Get CMA GEM object for framebuffer
 * @base_fb: the framebuffer
 * @plane: which plane
 *
 * This function is based on drm_fb_cma_get_gem_obj().
 *
 * Return: a CMA GEM object for given framebuffer, or NULL if not available.
 */
struct drm_gem_cma_object *
xilinx_drm_fb_get_gem_obj(struct drm_framebuffer *base_fb, unsigned int plane)
{
	struct xilinx_drm_fb *fb = to_fb(base_fb);

	if (plane >= 4)
		return NULL;

	return fb->obj[plane];
}

static struct fb_ops xilinx_drm_fbdev_ops = {
	.owner		= THIS_MODULE,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_setcmap	= drm_fb_helper_setcmap,
};

/**
 * xilinx_drm_fbdev_create - Create the fbdev with a framebuffer
 * @fb_helper: fb helper structure
 * @sizes: framebuffer size info
 *
 * This function is based on drm_fbdev_cma_create().
 *
 * Return: 0 if successful, or the error code.
 */
static int xilinx_drm_fbdev_create(struct drm_fb_helper *fb_helper,
				   struct drm_fb_helper_surface_size *sizes)
{
	struct xilinx_drm_fbdev *fbdev = to_fbdev(fb_helper);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_device *drm = fb_helper->dev;
	struct drm_gem_cma_object *obj;
	struct drm_framebuffer *base_fb;
	unsigned int bytes_per_pixel;
	unsigned long offset;
	struct fb_info *fbi;
	size_t size;
	int ret;

	DRM_DEBUG_KMS("surface width(%d), height(%d) and bpp(%d)\n",
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = ALIGN(sizes->surface_width * bytes_per_pixel,
				    fbdev->align);
	mode_cmd.pixel_format = xilinx_drm_get_format(drm);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	obj = drm_gem_cma_create(drm, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	fbi = framebuffer_alloc(0, drm->dev);
	if (!fbi) {
		DRM_ERROR("Failed to allocate framebuffer info.\n");
		ret = -ENOMEM;
		goto err_drm_gem_cma_free_object;
	}

	fbdev->fb = xilinx_drm_fb_alloc(drm, &mode_cmd, &obj, 1);
	if (IS_ERR(fbdev->fb)) {
		DRM_ERROR("Failed to allocate DRM framebuffer.\n");
		ret = PTR_ERR(fbdev->fb);
		goto err_framebuffer_release;
	}

	base_fb = &fbdev->fb->base;
	fb_helper->fb = base_fb;
	fb_helper->fbdev = fbi;

	fbi->par = fb_helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &xilinx_drm_fbdev_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("Failed to allocate color map.\n");
		goto err_xilinx_drm_fb_destroy;
	}

	drm_fb_helper_fill_fix(fbi, base_fb->pitches[0], base_fb->depth);
	drm_fb_helper_fill_var(fbi, fb_helper, base_fb->width, base_fb->height);

	offset = fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * base_fb->pitches[0];

	drm->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = (char __iomem *)(obj->vaddr + offset);
	fbi->fix.smem_start = (unsigned long)(obj->paddr + offset);
	fbi->screen_size = size;
	fbi->fix.smem_len = size;

	return 0;

err_xilinx_drm_fb_destroy:
	drm_framebuffer_unregister_private(base_fb);
	xilinx_drm_fb_destroy(base_fb);
err_framebuffer_release:
	framebuffer_release(fbi);
err_drm_gem_cma_free_object:
	drm_gem_cma_free_object(&obj->base);
	return ret;
}

static struct drm_fb_helper_funcs xilinx_drm_fb_helper_funcs = {
	.fb_probe = xilinx_drm_fbdev_create,
};

/**
 * xilinx_drm_fb_init - Allocate and initializes the Xilinx framebuffer
 * @drm: DRM device
 * @preferred_bpp: preferred bits per pixel for the device
 * @num_crtc: number of CRTCs
 * @max_conn_count: maximum number of connectors
 * @align: alignment value for pitch
 *
 * This function is based on drm_fbdev_cma_init().
 *
 * Return: a newly allocated drm_fb_helper struct or a ERR_PTR.
 */
struct drm_fb_helper *
xilinx_drm_fb_init(struct drm_device *drm, unsigned int preferred_bpp,
		   unsigned int num_crtc, unsigned int max_conn_count,
		   unsigned int align)
{
	struct xilinx_drm_fbdev *fbdev;
	struct drm_fb_helper *fb_helper;
	int ret;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("Failed to allocate drm fbdev.\n");
		return ERR_PTR(-ENOMEM);
	}

	fbdev->align = align;
	fb_helper = &fbdev->fb_helper;
	drm_fb_helper_prepare(drm, fb_helper, &xilinx_drm_fb_helper_funcs);

	ret = drm_fb_helper_init(drm, fb_helper, num_crtc, max_conn_count);
	if (ret < 0) {
		DRM_ERROR("Failed to initialize drm fb helper.\n");
		goto err_free;
	}

	ret = drm_fb_helper_single_add_all_connectors(fb_helper);
	if (ret < 0) {
		DRM_ERROR("Failed to add connectors.\n");
		goto err_drm_fb_helper_fini;

	}

	drm_helper_disable_unused_functions(drm);

	ret = drm_fb_helper_initial_config(fb_helper, preferred_bpp);
	if (ret < 0) {
		DRM_ERROR("Failed to set initial hw configuration.\n");
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
 * xilinx_drm_fbdev_fini - Free the Xilinx framebuffer
 * @fb_helper: drm_fb_helper struct
 *
 * This function is based on drm_fbdev_cma_fini().
 */
void xilinx_drm_fb_fini(struct drm_fb_helper *fb_helper)
{
	struct xilinx_drm_fbdev *fbdev = to_fbdev(fb_helper);

	if (fbdev->fb_helper.fbdev) {
		struct fb_info *info;
		int ret;

		info = fbdev->fb_helper.fbdev;
		ret = unregister_framebuffer(info);
		if (ret < 0)
			DRM_DEBUG_KMS("failed unregister_framebuffer()\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	if (fbdev->fb) {
		drm_framebuffer_unregister_private(&fbdev->fb->base);
		xilinx_drm_fb_destroy(&fbdev->fb->base);
	}

	drm_fb_helper_fini(&fbdev->fb_helper);
	kfree(fbdev);
}

/**
 * xilinx_drm_fb_restore_mode - Restores initial framebuffer mode
 * @fb_helper: drm_fb_helper struct, may be NULL
 *
 * This function is based on drm_fbdev_cma_restore_mode() and usually called
 * from the Xilinx DRM drivers lastclose callback.
 */
void xilinx_drm_fb_restore_mode(struct drm_fb_helper *fb_helper)
{
	if (fb_helper)
		drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);
}

/**
 * xilinx_drm_fb_create - (struct drm_mode_config_funcs *)->fb_create callback
 * @drm: DRM device
 * @file_priv: drm file private data
 * @mode_cmd: mode command for fb creation
 *
 * This functions creates a drm_framebuffer for given mode @mode_cmd. This
 * functions is intended to be used for the fb_create callback function of
 * drm_mode_config_funcs.
 *
 * Return: a drm_framebuffer object if successful, or ERR_PTR.
 */
struct drm_framebuffer *xilinx_drm_fb_create(struct drm_device *drm,
					     struct drm_file *file_priv,
					     struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct xilinx_drm_fb *fb;
	struct drm_gem_cma_object *objs[4];
	struct drm_gem_object *obj;
	unsigned int hsub;
	unsigned int vsub;
	int ret;
	int i;

	if (!xilinx_drm_check_format(drm, mode_cmd->pixel_format)) {
		DRM_ERROR("unsupported pixel format %08x\n",
			  mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	hsub = drm_format_horz_chroma_subsampling(mode_cmd->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(mode_cmd->pixel_format);

	for (i = 0; i < drm_format_num_planes(mode_cmd->pixel_format); i++) {
		unsigned int width = mode_cmd->width / (i ? hsub : 1);
		unsigned int height = mode_cmd->height / (i ? vsub : 1);
		unsigned int min_size;

		obj = drm_gem_object_lookup(drm, file_priv,
					    mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		min_size = (height - 1) * mode_cmd->pitches[i] + width *
			   drm_format_plane_cpp(mode_cmd->pixel_format, i) +
			   mode_cmd->offsets[i];

		if (obj->size < min_size) {
			drm_gem_object_unreference_unlocked(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = to_drm_gem_cma_obj(obj);
	}

	fb = xilinx_drm_fb_alloc(drm, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err_gem_object_unreference;
	}

	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &fb->base.depth,
			     &fb->base.bits_per_pixel);
	if (!fb->base.bits_per_pixel)
		fb->base.bits_per_pixel =
			xilinx_drm_format_bpp(mode_cmd->pixel_format);

	return &fb->base;

err_gem_object_unreference:
	for (i--; i >= 0; i--)
		drm_gem_object_unreference_unlocked(&objs[i]->base);
	return ERR_PTR(ret);
}


/**
 * xilinx_drm_fb_hotplug_event - Poll for hotpulug events
 * @fb_helper: drm_fb_helper struct, may be NULL
 *
 * This function is based on drm_fbdev_cma_hotplug_event() and usually called
 * from the Xilinx DRM drivers output_poll_changed callback.
 */
void xilinx_drm_fb_hotplug_event(struct drm_fb_helper *fb_helper)
{
	if (fb_helper)
		drm_fb_helper_hotplug_event(fb_helper);
}
