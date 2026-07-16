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

#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

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

#define MAX_CRTC	3

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
 * @master_count: Counter to track number of fake master instances
 */
struct xlnx_drm {
	struct drm_device *drm;
	struct xlnx_crtc_helper *crtc;
	struct drm_fb_helper *fb;
	struct platform_device *master;
	struct drm_atomic_state *suspend_state;
	u32 master_count;
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

static const struct drm_mode_config_funcs xlnx_mode_config_funcs = {
	.fb_create		= xlnx_fb_create,
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
		xlnx_drm->master_count++;
	}

	return 0;
}

static int xlnx_drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file = filp->private_data;
	struct drm_minor *minor = file->minor;
	struct drm_device *drm = minor->dev;
	struct xlnx_drm *xlnx_drm = drm->dev_private;

	if (file->is_master && xlnx_drm->master_count) {
		xlnx_drm->master_count--;
		file->is_master = 0;
	}

	return drm_release(inode, filp);
}

static const struct file_operations xlnx_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= xlnx_drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.mmap		= drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.llseek		= noop_llseek,
	.fop_flags      = FOP_UNSIGNED_OFFSET,
};

static struct drm_driver xlnx_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC,
	.open				= xlnx_drm_open,

	DRM_GEM_DMA_DRIVER_OPS_VMAP_WITH_DUMB_CREATE(xlnx_gem_cma_dumb_create),
	XLNX_DRM_FBDEV_DRIVER_OPS,
	.fops				= &xlnx_fops,

	.name				= DRIVER_NAME,
	.desc				= DRIVER_DESC,
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

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto err_drm;
	}

	drm->dev_private = xlnx_drm;
	xlnx_drm->drm = drm;
	xlnx_drm->master = master;
	drm_kms_helper_poll_init(drm);
	platform_set_drvdata(master, xlnx_drm);

	xlnx_drm->crtc = xlnx_crtc_helper_init(drm);
	if (IS_ERR(xlnx_drm->crtc)) {
		ret = PTR_ERR(xlnx_drm->crtc);
		goto err_drm;
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
err_drm:
	drm_dev_put(drm);
	return ret;
}

static void xlnx_unbind(struct device *dev)
{
	struct xlnx_drm *xlnx_drm = dev_get_drvdata(dev);
	struct drm_device *drm = xlnx_drm->drm;

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	component_unbind_all(&xlnx_drm->master->dev, drm);
	if (xlnx_drm->fb) {
		xlnx_fb_fini(xlnx_drm->fb);
		xlnx_drm->fb = NULL;
	}
	xlnx_crtc_helper_fini(drm, xlnx_drm->crtc);
	drm_kms_helper_poll_fini(drm);
	drm_dev_put(drm);
}

static const struct component_master_ops xlnx_master_ops = {
	.bind	= xlnx_bind,
	.unbind	= xlnx_unbind,
};

/*
 * This is the list of the compatible strings for the PL DRM drivers that still
 * utilize the xlnx bridge interface. This list needs to be updated with every
 * additions of the new drivers, compatible string updates and drivers
 * transitions to the DRM bridge framework. The list should be removed as soon
 * as the transition of all PL DRM drivers complete.
 */
static const char * const xlnx_compatible_components_list[] = {
	"xlnx,v-mix-5.2",
	"xlnx,v-mix-5.3",
	"xlnx,mixer-5.0",
	"xlnx,mixer-4.0",
	"xlnx,mixer-3.0",
	"xlnx,vpss-csc",
	"xlnx,v-dp-txss-3.0",
	"xlnx,v-dp-txss-3.1",
	"xlnx,dsi",
	"xlnx,v-hdmi-txss1-1.1",
	"xlnx,v-hdmi-txss1-1.2",
	"xlnx,v-hdmi-tx-ss-3.1",
	"xlnx,pl-disp",
	"xlnx,vpss-scaler",
	"xlnx,vpss-scaler-2.2",
	"xlnx,sdi-tx",
	"xlnx,bridge-v-tc-6.1",
};

static bool xlnx_check_compatible_component(struct device_node *node)
{
	const char *comp_str = NULL;
	struct property *comp_prop = of_find_property(node, "compatible", NULL);

	do {
		int i;

		comp_str = of_prop_next_string(comp_prop, comp_str);
		for (i = 0; comp_str && i < ARRAY_SIZE(xlnx_compatible_components_list); ++i)
			if (!strcmp(comp_str, xlnx_compatible_components_list[i]))
				return true;
	} while (comp_prop && comp_str);

	return false;
}

static int xlnx_compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/**
 * struct xlnx_component_node - Cached pipeline component device tree node
 * @node: List node linking into &xlnx_master_entry.components
 * @np: The component device tree node (holds a reference)
 */
struct xlnx_component_node {
	struct list_head node;
	struct device_node *np;
};

/**
 * struct xlnx_master_entry - Global list entry tracking a master device
 * @list: List node linking into @xlnx_master_list
 * @master: The logical master platform device
 * @components: Cached list of pipeline component device tree nodes
 *
 * The cached component node list is the recipe used to (re)build the master's
 * component match. It is seeded by the initial topology walk and extended by
 * xlnx_drm_register_component().
 */
struct xlnx_master_entry {
	struct list_head list;
	struct platform_device *master;
	struct list_head components;
};

/* Global list of registered master devices, ordered by creation */
static LIST_HEAD(xlnx_master_list);
static DEFINE_MUTEX(xlnx_master_lock);

/* Find the tracking entry for @master, or NULL if not registered. */
static struct xlnx_master_entry *xlnx_master_find(struct platform_device *master)
{
	struct xlnx_master_entry *entry, *found = NULL;

	mutex_lock(&xlnx_master_lock);
	list_for_each_entry(entry, &xlnx_master_list, list)
		if (entry->master == master) {
			found = entry;
			break;
		}
	mutex_unlock(&xlnx_master_lock);

	return found;
}

/* devm action dropping the device tree reference of a cached component node. */
static void xlnx_of_node_put(void *data)
{
	of_node_put(data);
}

/* Cache @np as a pipeline component of @entry, ignoring duplicates. */
static int xlnx_master_add_component(struct xlnx_master_entry *entry,
				     struct device_node *np)
{
	struct xlnx_component_node *cn;
	int ret = 0;

	if (!np)
		return 0;

	mutex_lock(&xlnx_master_lock);
	list_for_each_entry(cn, &entry->components, node)
		if (cn->np == np)
			goto out;

	cn = devm_kzalloc(&entry->master->dev, sizeof(*cn), GFP_KERNEL);
	if (!cn) {
		ret = -ENOMEM;
		goto out;
	}
	cn->np = of_node_get(np);
	ret = devm_add_action_or_reset(&entry->master->dev, xlnx_of_node_put,
				       np);
	if (ret)
		goto out;
	list_add_tail(&cn->node, &entry->components);
out:
	mutex_unlock(&xlnx_master_lock);

	return ret;
}

/*
 * Remove the cached component node matching @np from @entry's list. The backing
 * devm allocation and OF reference are reclaimed when the master is torn down.
 */
static void xlnx_master_drop_component(struct xlnx_master_entry *entry,
				       struct device_node *np)
{
	struct xlnx_component_node *cn;

	if (!np)
		return;

	mutex_lock(&xlnx_master_lock);
	list_for_each_entry(cn, &entry->components, node)
		if (cn->np == np) {
			list_del(&cn->node);
			break;
		}
	mutex_unlock(&xlnx_master_lock);
}

/* Build a fresh component match from @entry's cached component node list. */
static struct component_match *
xlnx_master_build_match(struct xlnx_master_entry *entry)
{
	struct device *master_dev = &entry->master->dev;
	struct component_match *match = NULL;
	struct xlnx_component_node *cn;

	mutex_lock(&xlnx_master_lock);
	list_for_each_entry(cn, &entry->components, node) {
		component_match_add(master_dev, &match, xlnx_compare_of, cn->np);
		if (IS_ERR(match))
			break;
	}
	mutex_unlock(&xlnx_master_lock);

	return match;
}

/**
 * xlnx_of_collect_components - Discover and cache pipeline component nodes
 * @dev: The real device owning the OF pipeline topology
 * @entry: The master tracking entry to populate
 *
 * Walk the device tree ports and OF graph of @dev and cache every compatible
 * component node, alongside @dev itself, into @entry. Pre-existing entries are
 * preserved so the function may be called to refresh the topology.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
static int xlnx_of_collect_components(struct device *dev,
				      struct xlnx_master_entry *entry)
{
	struct device_node *ep, *port, *remote, *parent;
	int i, ret;

	ret = xlnx_master_add_component(entry, dev->of_node);
	if (ret)
		return ret;

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

		if (xlnx_check_compatible_component(parent)) {
			ret = xlnx_master_add_component(entry, parent);
			if (ret) {
				of_node_put(parent);
				of_node_put(port);
				return ret;
			}
		}

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

		if (!of_graph_is_present(parent)) {
			of_node_put(parent);
			break;
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

			if (xlnx_check_compatible_component(remote)) {
				ret = xlnx_master_add_component(entry, remote);
				if (ret) {
					of_node_put(remote);
					of_node_put(ep);
					of_node_put(parent);
					return ret;
				}
			}

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

	return 0;
}

/**
 * xlnx_of_component_probe - Assemble the aggregate master from OF topology
 * @master_dev: The logical master device being probed
 *
 * Cache the pipeline component nodes derived from the master's parent OF node
 * and register the aggregate master with a match built from that cache.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
static int xlnx_of_component_probe(struct device *master_dev)
{
	struct device *dev = master_dev->parent;
	struct xlnx_master_entry *entry;
	struct component_match *match;
	int ret;

	if (!dev->of_node)
		return -EINVAL;

	entry = xlnx_master_find(to_platform_device(master_dev));
	if (!entry)
		return -ENODEV;

	ret = xlnx_of_collect_components(dev, entry);
	if (ret)
		return ret;

	match = xlnx_master_build_match(entry);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(master_dev, &xlnx_master_ops,
					       match);
}

static int xlnx_platform_probe(struct platform_device *pdev)
{
	return xlnx_of_component_probe(&pdev->dev);
}

static void xlnx_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &xlnx_master_ops);
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
 * xlnx_drm_get_next_master - Get the next registered master device
 * @master: The current master device, or NULL to get the first one
 *
 * Traverse the global list of master devices and return the master that
 * follows @master. If @master is NULL, return the first master in the list.
 *
 * Return: The next master platform device, or NULL if @master is the last
 * entry or the list is empty.
 */
struct platform_device *xlnx_drm_get_next_master(struct platform_device *master)
{
	struct xlnx_master_entry *entry;
	struct platform_device *next = NULL;

	mutex_lock(&xlnx_master_lock);

	if (!master) {
		entry = list_first_entry_or_null(&xlnx_master_list,
						 struct xlnx_master_entry, list);
		if (entry)
			next = entry->master;
		goto out;
	}

	list_for_each_entry(entry, &xlnx_master_list, list) {
		if (entry->master != master)
			continue;

		if (!list_is_last(&entry->list, &xlnx_master_list))
			next = list_next_entry(entry, list)->master;
		break;
	}

out:
	mutex_unlock(&xlnx_master_lock);

	return next;
}
EXPORT_SYMBOL_GPL(xlnx_drm_get_next_master);

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
	struct xlnx_master_entry *entry;
	struct platform_device *master;
	int id, ret;

	id = ffs(xlnx_master_ids);
	if (!id)
		return ERR_PTR(-ENOSPC);

	master = platform_device_alloc("xlnx-drm", id - 1);
	if (!master)
		return ERR_PTR(-ENOMEM);

	/*
	 * Track the master before adding it: platform_device_add() probes the
	 * master synchronously, and that probe looks the entry up to cache the
	 * pipeline topology and build the component match.
	 */
	entry = devm_kzalloc(&pdev->dev, sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto err_put;
	}
	entry->master = master;
	INIT_LIST_HEAD(&entry->components);

	mutex_lock(&xlnx_master_lock);
	list_add_tail(&entry->list, &xlnx_master_list);
	mutex_unlock(&xlnx_master_lock);

	master->dev.parent = &pdev->dev;
	ret = platform_device_add(master);
	if (ret)
		goto err_del;

	WARN_ON(master->id != id - 1);
	xlnx_master_ids &= ~BIT(master->id);
	return master;

err_del:
	mutex_lock(&xlnx_master_lock);
	list_del(&entry->list);
	mutex_unlock(&xlnx_master_lock);
err_put:
	platform_device_put(master);
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
	struct xlnx_master_entry *entry, *tmp;

	mutex_lock(&xlnx_master_lock);
	list_for_each_entry_safe(entry, tmp, &xlnx_master_list, list) {
		if (entry->master != master)
			continue;
		list_del(&entry->list);
		break;
	}
	mutex_unlock(&xlnx_master_lock);

	xlnx_master_ids |= BIT(master->id);
	platform_device_unregister(master);
}
EXPORT_SYMBOL_GPL(xlnx_drm_pipeline_exit);

/**
 * xlnx_drm_register_component - Add a component to an existing aggregate master
 * @master: The logical master device to extend
 * @component: The component platform device to register with @master
 *
 * Cache @component's device tree node against @master, then rebuild the
 * aggregate from scratch: tear the current master down (if already registered)
 * and re-register it with a fresh component match built from the cached node
 * list. A fresh match is mandatory because component_master_del() leaves stale
 * component pointers in the old match that find_components() would skip.
 *
 * The caller must have registered @component with component_add() beforehand,
 * and must not hold any lock taken by the master bind/unbind paths.
 *
 * Return: 0 on success, or a negative error code otherwise
 */
int xlnx_drm_register_component(struct platform_device *master,
				struct platform_device *component)
{
	struct xlnx_master_entry *entry;
	struct component_match *match;
	int ret;

	entry = xlnx_master_find(master);
	if (!entry)
		return -ENODEV;

	ret = xlnx_master_add_component(entry, component->dev.of_node);
	if (ret)
		return ret;

	match = xlnx_master_build_match(entry);
	if (IS_ERR(match))
		return PTR_ERR(match);

	component_master_del(&master->dev, &xlnx_master_ops);

	ret = component_master_add_with_match(&master->dev, &xlnx_master_ops,
					      match);
	if (ret) {
		/*
		 * Re-registration failed after the previously working aggregate
		 * was already torn down. Drop the component we just added and
		 * restore the master with the prior match so the existing
		 * CRTC(s) keep running.
		 */
		dev_err(&master->dev,
			"failed to re-register aggregate master: %d\n", ret);
		xlnx_master_drop_component(entry, component->dev.of_node);

		match = xlnx_master_build_match(entry);
		if (!IS_ERR(match) &&
		    !component_master_add_with_match(&master->dev,
						     &xlnx_master_ops, match))
			return ret;

		dev_err(&master->dev, "failed to restore aggregate master\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(xlnx_drm_register_component);

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
MODULE_LICENSE("GPL");
