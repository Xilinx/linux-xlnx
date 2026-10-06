// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx PL DRM/KMS aggregate frame driver
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
 *
 * This driver models a programmable-logic (PL) display pipeline as a single
 * aggregate device. The pipeline's building blocks (CRTC sources such as the
 * AVPG pattern generator and the PL display / frame-buffer reader) are
 * described as child device tree nodes of an "xlnx,pl-drm-1.0" parent. The
 * aggregate driver instantiates those children and binds them together through
 * the component framework so that a single DRM device is built from the whole
 * pipeline exactly once.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "xlnx_crtc.h"

#define XLNX_PL_DRM_NAME	"xlnx-pl"
#define XLNX_PL_DRM_DESC	"Xilinx PL DRM/KMS Driver"
#define XLNX_PL_DRM_MAJOR	1
#define XLNX_PL_DRM_MINOR	0

#define XLNX_PL_DRM_MAX_CRTC	4

/* Static mode configuration limits for the pipeline. */
#define XLNX_PL_DRM_MAX_WIDTH	8192
#define XLNX_PL_DRM_MAX_HEIGHT	8192

/*
 * struct xlnx_pl_drm - Aggregate PL DRM device state
 * @drm: The DRM device built from the whole pipeline
 */
struct xlnx_pl_drm {
	struct drm_device *drm;
};

/*
 * Compatible strings of the pipeline sub-components this aggregate driver
 * knows how to bind. Only DT children matching one of these are added to the
 * component match, so unrelated child nodes (clocks, gpios, ...) that may be
 * present under the aggregate node are never accidentally picked up.
 */
static const char * const xlnx_pl_drm_components[] = {
	"xlnx,psdpdc-av-pat-gen-2.0",
	"xlnx,pl-disp",
};

static bool xlnx_pl_drm_is_component(struct device_node *node)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xlnx_pl_drm_components); i++)
		if (of_device_is_compatible(node, xlnx_pl_drm_components[i]))
			return true;

	return false;
}

static int xlnx_pl_drm_compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static void xlnx_pl_drm_release_of(struct device *dev, void *data)
{
	of_node_put(data);
}

static const struct drm_mode_config_funcs xlnx_pl_drm_mode_config_funcs = {
	.fb_create	= drm_gem_fb_create,
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_DMA_FOPS(xlnx_pl_drm_fops);

static const struct drm_driver xlnx_pl_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	/*
	 * TODO: The generic dumb-create helper computes the pitch as
	 * width * bytes-per-pixel and does not honour the DMA channel's
	 * copy_align requirement. For some formats/widths (e.g. 4-PPC
	 * XR24 at width 1364) the framebuffer stride ends up unaligned
	 * for the PL display DMA. Provide an aggregate dumb-create
	 * callback that rounds the pitch up to the channel alignment.
	 */
	DRM_GEM_DMA_DRIVER_OPS,
	.fops			= &xlnx_pl_drm_fops,

	.name			= XLNX_PL_DRM_NAME,
	.desc			= XLNX_PL_DRM_DESC,
	.major			= XLNX_PL_DRM_MAJOR,
	.minor			= XLNX_PL_DRM_MINOR,
};

static void xlnx_pl_drm_mode_config_init(struct drm_device *drm)
{
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = XLNX_PL_DRM_MAX_WIDTH;
	drm->mode_config.max_height = XLNX_PL_DRM_MAX_HEIGHT;
}

static int xlnx_pl_drm_bind(struct device *dev)
{
	struct xlnx_pl_drm *xlnx_pl_drm;
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&xlnx_pl_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	xlnx_pl_drm = devm_kzalloc(drm->dev, sizeof(*xlnx_pl_drm), GFP_KERNEL);
	if (!xlnx_pl_drm) {
		ret = -ENOMEM;
		goto err_drm;
	}

	drm_mode_config_init(drm);
	drm->mode_config.funcs = &xlnx_pl_drm_mode_config_funcs;

	ret = drm_vblank_init(drm, XLNX_PL_DRM_MAX_CRTC);
	if (ret) {
		dev_err(dev, "failed to initialize vblank\n");
		goto err_drm;
	}

	drm->dev_private = xlnx_pl_drm;
	xlnx_pl_drm->drm = drm;
	drm_kms_helper_poll_init(drm);
	dev_set_drvdata(dev, xlnx_pl_drm);

	ret = component_bind_all(dev, drm);
	if (ret)
		goto err_poll;

	xlnx_pl_drm_mode_config_init(drm);
	drm_mode_config_reset(drm);
	dma_set_mask(drm->dev, DMA_BIT_MASK(64));

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_unbind;

	return 0;

err_unbind:
	component_unbind_all(dev, drm);
err_poll:
	drm_kms_helper_poll_fini(drm);
err_drm:
	drm_dev_put(drm);
	return ret;
}

static void xlnx_pl_drm_unbind(struct device *dev)
{
	struct xlnx_pl_drm *xlnx_pl_drm = dev_get_drvdata(dev);
	struct drm_device *drm = xlnx_pl_drm->drm;

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	component_unbind_all(dev, drm);
	drm_kms_helper_poll_fini(drm);
	drm_dev_put(drm);
}

static const struct component_master_ops xlnx_pl_drm_ops = {
	.bind	= xlnx_pl_drm_bind,
	.unbind	= xlnx_pl_drm_unbind,
};

/*
 * Build the component match from the available child nodes of the aggregate.
 * Each child (AVPG, PL display, ...) registers itself with component_add() from
 * its own driver's probe; here we simply enumerate the DT children and add a
 * matcher for each so the master binds once they have all probed.
 */
static int xlnx_pl_drm_build_match(struct device *dev,
				   struct component_match **match)
{
	struct device_node *child;
	unsigned int count = 0;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!xlnx_pl_drm_is_component(child))
			continue;

		component_match_add_release(dev, match,
					    xlnx_pl_drm_release_of,
					    xlnx_pl_drm_compare_of,
					    of_node_get(child));
		if (IS_ERR(*match)) {
			of_node_put(child);
			return PTR_ERR(*match);
		}
		count++;
	}

	if (!count)
		return -ENODEV;

	dev_dbg(dev, "matched %u pipeline component(s)\n", count);

	return 0;
}

static int xlnx_pl_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	int ret;

	/* Instantiate the child pipeline components described in the DT. */
	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to populate pipeline children\n");

	ret = xlnx_pl_drm_build_match(dev, &match);
	if (ret) {
		dev_err_probe(dev, ret, "failed to build component match\n");
		goto err_depopulate;
	}

	ret = component_master_add_with_match(dev, &xlnx_pl_drm_ops, match);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add component master\n");
		goto err_depopulate;
	}

	return 0;

err_depopulate:
	of_platform_depopulate(dev);
	return ret;
}

static void xlnx_pl_drm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_master_del(dev, &xlnx_pl_drm_ops);
	of_platform_depopulate(dev);
}

static const struct of_device_id xlnx_pl_drm_of_match[] = {
	{ .compatible = "xlnx,pl-drm-1.0", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xlnx_pl_drm_of_match);

static struct platform_driver xlnx_pl_drm_platform_driver = {
	.probe	= xlnx_pl_drm_probe,
	.remove	= xlnx_pl_drm_remove,
	.driver	= {
		.name		= "xlnx-pl-drm",
		.of_match_table	= xlnx_pl_drm_of_match,
	},
};

module_platform_driver(xlnx_pl_drm_platform_driver);

MODULE_AUTHOR("Anatoliy Klymenko");
MODULE_DESCRIPTION("Xilinx PL DRM/KMS Aggregate Frame Driver");
MODULE_LICENSE("GPL");
