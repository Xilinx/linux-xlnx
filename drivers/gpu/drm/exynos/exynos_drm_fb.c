/* exynos_drm_fb.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <uapi/drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_iommu.h"
#include "exynos_drm_crtc.h"

#define to_exynos_fb(x)	container_of(x, struct exynos_drm_fb, fb)

/*
 * exynos specific framebuffer structure.
 *
 * @fb: drm framebuffer obejct.
 * @exynos_gem: array of exynos specific gem object containing a gem object.
 */
struct exynos_drm_fb {
	struct drm_framebuffer	fb;
	struct exynos_drm_gem	*exynos_gem[MAX_FB_BUFFER];
	dma_addr_t			dma_addr[MAX_FB_BUFFER];
};

static int check_fb_gem_memory_type(struct drm_device *drm_dev,
				    struct exynos_drm_gem *exynos_gem)
{
	unsigned int flags;

	/*
	 * if exynos drm driver supports iommu then framebuffer can use
	 * all the buffer types.
	 */
	if (is_drm_iommu_supported(drm_dev))
		return 0;

	flags = exynos_gem->flags;

	/*
	 * Physically non-contiguous memory type for framebuffer is not
	 * supported without IOMMU.
	 */
	if (IS_NONCONTIG_BUFFER(flags)) {
		DRM_ERROR("Non-contiguous GEM memory is not supported.\n");
		return -EINVAL;
	}

	return 0;
}

static void exynos_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct exynos_drm_fb *exynos_fb = to_exynos_fb(fb);
	unsigned int i;

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < ARRAY_SIZE(exynos_fb->exynos_gem); i++) {
		struct drm_gem_object *obj;

		if (exynos_fb->exynos_gem[i] == NULL)
			continue;

		obj = &exynos_fb->exynos_gem[i]->base;
		drm_gem_object_unreference_unlocked(obj);
	}

	kfree(exynos_fb);
	exynos_fb = NULL;
}

static int exynos_drm_fb_create_handle(struct drm_framebuffer *fb,
					struct drm_file *file_priv,
					unsigned int *handle)
{
	struct exynos_drm_fb *exynos_fb = to_exynos_fb(fb);

	return drm_gem_handle_create(file_priv,
				     &exynos_fb->exynos_gem[0]->base, handle);
}

static const struct drm_framebuffer_funcs exynos_drm_fb_funcs = {
	.destroy	= exynos_drm_fb_destroy,
	.create_handle	= exynos_drm_fb_create_handle,
};

struct drm_framebuffer *
exynos_drm_framebuffer_init(struct drm_device *dev,
			    const struct drm_mode_fb_cmd2 *mode_cmd,
			    struct exynos_drm_gem **exynos_gem,
			    int count)
{
	struct exynos_drm_fb *exynos_fb;
	int i;
	int ret;

	exynos_fb = kzalloc(sizeof(*exynos_fb), GFP_KERNEL);
	if (!exynos_fb)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		ret = check_fb_gem_memory_type(dev, exynos_gem[i]);
		if (ret < 0)
			goto err;

		exynos_fb->exynos_gem[i] = exynos_gem[i];
		exynos_fb->dma_addr[i] = exynos_gem[i]->dma_addr
						+ mode_cmd->offsets[i];
	}

	drm_helper_mode_fill_fb_struct(&exynos_fb->fb, mode_cmd);

	ret = drm_framebuffer_init(dev, &exynos_fb->fb, &exynos_drm_fb_funcs);
	if (ret < 0) {
		DRM_ERROR("failed to initialize framebuffer\n");
		goto err;
	}

	return &exynos_fb->fb;

err:
	kfree(exynos_fb);
	return ERR_PTR(ret);
}

static struct drm_framebuffer *
exynos_user_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		      const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct exynos_drm_gem *exynos_gem[MAX_FB_BUFFER];
	struct drm_gem_object *obj;
	struct drm_framebuffer *fb;
	int i;
	int ret;

	for (i = 0; i < drm_format_num_planes(mode_cmd->pixel_format); i++) {
		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("failed to lookup gem object\n");
			ret = -ENOENT;
			goto err;
		}

		exynos_gem[i] = to_exynos_gem(obj);
	}

	fb = exynos_drm_framebuffer_init(dev, mode_cmd, exynos_gem, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	while (i--)
		drm_gem_object_unreference_unlocked(&exynos_gem[i]->base);

	return ERR_PTR(ret);
}

dma_addr_t exynos_drm_fb_dma_addr(struct drm_framebuffer *fb, int index)
{
	struct exynos_drm_fb *exynos_fb = to_exynos_fb(fb);

	if (index >= MAX_FB_BUFFER)
		return DMA_ERROR_CODE;

	return exynos_fb->dma_addr[index];
}

static const struct drm_mode_config_funcs exynos_drm_mode_config_funcs = {
	.fb_create = exynos_user_fb_create,
	.output_poll_changed = exynos_drm_output_poll_changed,
	.atomic_check = exynos_atomic_check,
	.atomic_commit = exynos_atomic_commit,
};

void exynos_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	dev->mode_config.funcs = &exynos_drm_mode_config_funcs;
}
