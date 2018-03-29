// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM KMS Driver
 *
 *  Copyright (C) 2013 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reservation.h>

#include "xlnx_bridge.h"
#include "xlnx_crtc.h"
#include "xlnx_drv.h"
#include "xlnx_fb.h"
#include "xlnx_gem.h"

#define DRIVER_NAME	"xlnx"
#define DRIVER_DESC	"Xilinx DRM KMS Driver"
#define DRIVER_DATE	"20130509"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static uint xlnx_fbdev_vres = 2;
module_param_named(fbdev_vres, xlnx_fbdev_vres, uint, 0444);
MODULE_PARM_DESC(fbdev_vres,
		 "fbdev virtual resolution multiplier for fb (default: 2)");

/**
 * struct xlnx_drm - Xilinx DRM private data
 * @drm: DRM core
 * @crtc: Xilinx DRM CRTC helper
 * @fb: DRM fb helper
 * @master: logical master device for pipeline
 * @suspend_state: atomic state for suspend / resume
 * @is_master: A flag to indicate if this instance is fake master
 */
struct xlnx_drm {
	struct drm_device *drm;
	struct xlnx_crtc_helper *crtc;
	struct drm_fb_helper *fb;
	struct platform_device *master;
	struct drm_atomic_state *suspend_state;
	bool is_master;
};

/**
 * xlnx_get_crtc_helper - Return the crtc helper instance
 * @drm: DRM device
 *
 * Return: the crtc helper instance
 */
struct xlnx_crtc_helper *xlnx_get_crtc_helper(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	return xlnx_drm->crtc;
}

/**
 * xlnx_get_align - Return the align requirement through CRTC helper
 * @drm: DRM device
 *
 * Return: the alignment requirement
 */
unsigned int xlnx_get_align(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	return xlnx_crtc_helper_get_align(xlnx_drm->crtc);
}

/**
 * xlnx_get_format - Return the current format of CRTC
 * @drm: DRM device
 *
 * Return: the current CRTC format
 */
uint32_t xlnx_get_format(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	return xlnx_crtc_helper_get_format(xlnx_drm->crtc);
}

static void xlnx_output_poll_changed(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	if (xlnx_drm->fb)
		drm_fb_helper_hotplug_event(xlnx_drm->fb);
}

static const struct drm_mode_config_funcs xlnx_mode_config_funcs = {
	.fb_create		= xlnx_fb_create,
	.output_poll_changed	= xlnx_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static void xlnx_mode_config_init(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;
	struct xlnx_crtc_helper *crtc = xlnx_drm->crtc;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = xlnx_crtc_helper_get_max_width(crtc);
	drm->mode_config.max_height = xlnx_crtc_helper_get_max_height(crtc);
	drm->mode_config.cursor_width =
		xlnx_crtc_helper_get_cursor_width(crtc);
	drm->mode_config.cursor_height =
		xlnx_crtc_helper_get_cursor_height(crtc);
}

static int xlnx_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct xlnx_drm *xlnx_drm = dev->dev_private;

	/* This is a hacky way to allow the root user to run as a master */
	if (!(drm_is_primary_client(file) && !dev->master) &&
	    !file->is_master && capable(CAP_SYS_ADMIN)) {
		file->is_master = 1;
		xlnx_drm->is_master = true;
	}

	return 0;
}

static int xlnx_drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file = filp->private_data;
	struct drm_minor *minor = file->minor;
	struct drm_device *drm = minor->dev;
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	if (xlnx_drm->is_master) {
		xlnx_drm->is_master = false;
		file->is_master = 0;
	}

	return drm_release(inode, filp);
}

static void xlnx_lastclose(struct drm_device *drm)
{
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	if (xlnx_drm->fb)
		drm_fb_helper_restore_fbdev_mode_unlocked(xlnx_drm->fb);
}

static const struct file_operations xlnx_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= xlnx_drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.mmap		= drm_gem_cma_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.llseek		= noop_llseek,
};

static struct drm_driver xlnx_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC | DRIVER_PRIME,
	.open				= xlnx_drm_open,
	.lastclose			= xlnx_lastclose,

	.prime_handle_to_fd		= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle		= drm_gem_prime_fd_to_handle,
	.gem_prime_export		= drm_gem_prime_export,
	.gem_prime_import		= drm_gem_prime_import,
	.gem_prime_get_sg_table		= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table	= drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap			= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap		= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap			= drm_gem_cma_prime_mmap,
	.gem_free_object		= drm_gem_cma_free_object,
	.gem_vm_ops			= &drm_gem_cma_vm_ops,
	.dumb_create			= xlnx_gem_cma_dumb_create,
	.dumb_destroy			= drm_gem_dumb_destroy,

	.fops				= &xlnx_fops,

	.name				= DRIVER_NAME,
	.desc				= DRIVER_DESC,
	.date				= DRIVER_DATE,
	.major				= DRIVER_MAJOR,
	.minor				= DRIVER_MINOR,
};

static int xlnx_bind(struct device *dev)
{
	struct xlnx_drm *xlnx_drm;
	struct drm_device *drm;
	const struct drm_format_info *info;
	struct platform_device *master = to_platform_device(dev);
	struct platform_device *pdev = to_platform_device(dev->parent);
	int ret;
	u32 format;

	drm = drm_dev_alloc(&xlnx_drm_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	xlnx_drm = devm_kzalloc(drm->dev, sizeof(*xlnx_drm), GFP_KERNEL);
	if (!xlnx_drm) {
		ret = -ENOMEM;
		goto err_drm;
	}

	drm_mode_config_init(drm);
	drm->mode_config.funcs = &xlnx_mode_config_funcs;

	ret = drm_vblank_init(drm, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto err_xlnx_drm;
	}

	drm->irq_enabled = 1;
	drm->dev_private = xlnx_drm;
	xlnx_drm->drm = drm;
	xlnx_drm->master = master;
	drm_kms_helper_poll_init(drm);
	platform_set_drvdata(master, xlnx_drm);

	xlnx_drm->crtc = xlnx_crtc_helper_init(drm);
	if (IS_ERR(xlnx_drm->crtc)) {
		ret = PTR_ERR(xlnx_drm->crtc);
		goto err_xlnx_drm;
	}

	ret = component_bind_all(&master->dev, drm);
	if (ret)
		goto err_crtc;

	xlnx_mode_config_init(drm);
	drm_mode_config_reset(drm);
	dma_set_mask(drm->dev, xlnx_crtc_helper_get_dma_mask(xlnx_drm->crtc));

	format = xlnx_crtc_helper_get_format(xlnx_drm->crtc);
	info = drm_format_info(format);
	if (info && info->depth && info->cpp[0]) {
		unsigned int align;

		align = xlnx_crtc_helper_get_align(xlnx_drm->crtc);
		xlnx_drm->fb = xlnx_fb_init(drm, info->cpp[0] * 8, 1, align,
					    xlnx_fbdev_vres);
		if (IS_ERR(xlnx_drm->fb)) {
			dev_err(&pdev->dev,
				"failed to initialize drm fb\n");
			xlnx_drm->fb = NULL;
		}
	} else {
		/* fbdev emulation is optional */
		dev_info(&pdev->dev, "fbdev is not initialized\n");
	}

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_fb;

	return 0;

err_fb:
	if (xlnx_drm->fb)
		xlnx_fb_fini(xlnx_drm->fb);
	component_unbind_all(drm->dev, drm);
err_crtc:
	xlnx_crtc_helper_fini(drm, xlnx_drm->crtc);
err_xlnx_drm:
	drm_mode_config_cleanup(drm);
err_drm:
	drm_dev_unref(drm);
	return ret;
}

static void xlnx_unbind(struct device *dev)
{
	struct xlnx_drm *xlnx_drm = dev_get_drvdata(dev);
	struct drm_device *drm = xlnx_drm->drm;

	drm_dev_unregister(drm);
	if (xlnx_drm->fb)
		xlnx_fb_fini(xlnx_drm->fb);
	component_unbind_all(&xlnx_drm->master->dev, drm);
	xlnx_crtc_helper_fini(drm, xlnx_drm->crtc);
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
	drm_dev_unref(drm);
}

static const struct component_master_ops xlnx_master_ops = {
	.bind	= xlnx_bind,
	.unbind	= xlnx_unbind,
};

static int xlnx_of_component_probe(struct device *master_dev,
				   int (*compare_of)(struct device *, void *),
				   const struct component_master_ops *m_ops)
{
	struct device *dev = master_dev->parent;
	struct device_node *ep, *port, *remote, *parent;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_node)
		return -EINVAL;

	component_match_add(master_dev, &match, compare_of, dev->of_node);

	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		parent = port->parent;
		if (!of_node_cmp(parent->name, "ports"))
			parent = parent->parent;
		parent = of_node_get(parent);

		if (!of_device_is_available(parent)) {
			of_node_put(parent);
			of_node_put(port);
			continue;
		}

		component_match_add(master_dev, &match, compare_of, parent);
		of_node_put(parent);
		of_node_put(port);
	}

	parent = dev->of_node;
	for (i = 0; ; i++) {
		parent = of_node_get(parent);
		if (!of_device_is_available(parent)) {
			of_node_put(parent);
			continue;
		}

		for_each_endpoint_of_node(parent, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote) ||
			    remote == dev->of_node) {
				of_node_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(dev, "parent dev of %s unavailable\n",
					 remote->full_name);
				of_node_put(remote);
				continue;
			}
			component_match_add(master_dev, &match, compare_of,
					    remote);
			of_node_put(remote);
		}
		of_node_put(parent);

		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		parent = port->parent;
		if (!of_node_cmp(parent->name, "ports"))
			parent = parent->parent;
		of_node_put(port);
	}

	return component_master_add_with_match(master_dev, m_ops, match);
}

static int xlnx_compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int xlnx_platform_probe(struct platform_device *pdev)
{
	return xlnx_of_component_probe(&pdev->dev, xlnx_compare_of,
				       &xlnx_master_ops);
}

static int xlnx_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &xlnx_master_ops);
	return 0;
}

static void xlnx_platform_shutdown(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &xlnx_master_ops);
}

static int __maybe_unused xlnx_pm_suspend(struct device *dev)
{
	struct xlnx_drm *xlnx_drm = dev_get_drvdata(dev);
	struct drm_device *drm = xlnx_drm->drm;

	drm_kms_helper_poll_disable(drm);

	xlnx_drm->suspend_state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(xlnx_drm->suspend_state)) {
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(xlnx_drm->suspend_state);
	}

	return 0;
}

static int __maybe_unused xlnx_pm_resume(struct device *dev)
{
	struct xlnx_drm *xlnx_drm = dev_get_drvdata(dev);
	struct drm_device *drm = xlnx_drm->drm;

	drm_atomic_helper_resume(drm, xlnx_drm->suspend_state);
	drm_kms_helper_poll_enable(drm);

	return 0;
}

static const struct dev_pm_ops xlnx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xlnx_pm_suspend, xlnx_pm_resume)
};

static struct platform_driver xlnx_driver = {
	.probe			= xlnx_platform_probe,
	.remove			= xlnx_platform_remove,
	.shutdown		= xlnx_platform_shutdown,
	.driver			= {
		.name		= "xlnx-drm",
		.pm		= &xlnx_pm_ops,
	},
};

/* bitmap for master id */
static u32 xlnx_master_ids = GENMASK(31, 0);

/**
 * xlnx_drm_pipeline_init - Initialize the drm pipeline for the device
 * @pdev: The platform device to initialize the drm pipeline device
 *
 * This function initializes the drm pipeline device, struct drm_device,
 * on @pdev by creating a logical master platform device. The logical platform
 * device acts as a master device to bind slave devices and represents
 * the entire pipeline.
 * The logical master uses the port bindings of the calling device to
 * figure out the pipeline topology.
 *
 * Return: the logical master platform device if the drm device is initialized
 * on @pdev. Error code otherwise.
 */
struct platform_device *xlnx_drm_pipeline_init(struct platform_device *pdev)
{
	struct platform_device *master;
	int id, ret;

	id = ffs(xlnx_master_ids);
	if (!id)
		return ERR_PTR(-ENOSPC);

	master = platform_device_alloc("xlnx-drm", id - 1);
	if (!master)
		return ERR_PTR(-ENOMEM);

	master->dev.parent = &pdev->dev;
	ret = platform_device_add(master);
	if (ret)
		goto err_out;

	WARN_ON(master->id != id - 1);
	xlnx_master_ids &= ~BIT(master->id);
	return master;

err_out:
	platform_device_unregister(master);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(xlnx_drm_pipeline_init);

/**
 * xlnx_drm_pipeline_exit - Release the drm pipeline for the device
 * @master: The master pipeline device to release
 *
 * Release the logical pipeline device returned by xlnx_drm_pipeline_init().
 */
void xlnx_drm_pipeline_exit(struct platform_device *master)
{
	xlnx_master_ids |= BIT(master->id);
	platform_device_unregister(master);
}
EXPORT_SYMBOL_GPL(xlnx_drm_pipeline_exit);

static int __init xlnx_drm_drv_init(void)
{
	xlnx_bridge_helper_init();
	platform_driver_register(&xlnx_driver);
	return 0;
}

static void __exit xlnx_drm_drv_exit(void)
{
	platform_driver_unregister(&xlnx_driver);
	xlnx_bridge_helper_fini();
}

module_init(xlnx_drm_drv_init);
module_exit(xlnx_drm_drv_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DRM KMS Driver");
MODULE_LICENSE("GPL v2");
