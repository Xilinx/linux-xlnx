// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DRM bridge driver
 *
 *  Copyright (C) 2017 Xilinx, Inc.
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

#include <linux/list.h>

#include "xlnx_bridge.h"
#include "xlnx_drv.h"

/*
 * Overview
 * --------
 *
 * Similar to drm bridge, but this can be used by any DRM driver. There
 * is no limitation to be used by non DRM drivers as well. No complex topology
 * is modeled, thus it's assumed that the Xilinx bridge device is directly
 * attached to client. The client should call Xilinx bridge functions explicitly
 * where it's needed, as opposed to drm bridge functions which are called
 * implicitly by DRM core.
 * One Xlnx bridge can be owned by one driver at a time.
 */

/**
 * struct xlnx_bridge_helper - Xilinx bridge helper
 * @xlnx_bridges: list of Xilinx bridges
 * @lock: lock to protect @xlnx_crtcs
 * @refcnt: reference count
 * @error: flag if in error state
 */
struct xlnx_bridge_helper {
	struct list_head xlnx_bridges;
	struct mutex lock; /* lock for @xlnx_bridges */
	unsigned int refcnt;
	bool error;
};

static struct xlnx_bridge_helper helper;

/*
 * Client functions
 */

/**
 * xlnx_bridge_enable - Enable the bridge
 * @bridge: bridge to enable
 *
 * Enable bridge.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT in error state,
 * or return code from callback.
 */
int xlnx_bridge_enable(struct xlnx_bridge *bridge)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->enable)
		return bridge->enable(bridge);

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_enable);

/**
 * xlnx_bridge_disable - Disable the bridge
 * @bridge: bridge to disable
 *
 * Disable bridge.
 */
void xlnx_bridge_disable(struct xlnx_bridge *bridge)
{
	if (!bridge)
		return;

	if (helper.error)
		return;

	if (bridge->disable)
		bridge->disable(bridge);
}
EXPORT_SYMBOL(xlnx_bridge_disable);

/**
 * xlnx_bridge_set_input - Set the input of @bridge
 * @bridge: bridge to set
 * @width: width
 * @height: height
 * @bus_fmt: bus format (ex, MEDIA_BUS_FMT_*);
 *
 * Set the bridge input with height / width / format.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT if in error state,
 * or return code from callback.
 */
int xlnx_bridge_set_input(struct xlnx_bridge *bridge,
			  u32 width, u32 height, u32 bus_fmt)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->set_input)
		return bridge->set_input(bridge, width, height, bus_fmt);

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_set_input);

/**
 * xlnx_bridge_get_input_fmts - Get the supported input formats
 * @bridge: bridge to set
 * @fmts: pointer to formats
 * @count: pointer to format count
 *
 * Get the list of supported input bus formats.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT if in error state,
 * or return code from callback.
 */
int xlnx_bridge_get_input_fmts(struct xlnx_bridge *bridge,
			       const u32 **fmts, u32 *count)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->get_input_fmts)
		return bridge->get_input_fmts(bridge, fmts, count);

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_get_input_fmts);

/**
 * of_xlnx_bridge_get - Get the corresponding Xlnx bridge instance
 * @bridge_np: The device node of the bridge device
 *
 * The function walks through the Xlnx bridge list of @drm, and return
 * if any registered bridge matches the device node. The returned
 * bridge will not be accesible by others.
 *
 * Return: the matching Xlnx bridge instance, or NULL
 */
struct xlnx_bridge *of_xlnx_bridge_get(struct device_node *bridge_np)
{
	struct xlnx_bridge *found = NULL;
	struct xlnx_bridge *bridge;

	if (helper.error)
		return NULL;

	mutex_lock(&helper.lock);
	list_for_each_entry(bridge, &helper.xlnx_bridges, list) {
		if (bridge->of_node == bridge_np && !bridge->owned) {
			found = bridge;
			bridge->owned = true;
			break;
		}
	}
	mutex_unlock(&helper.lock);

	return found;
}
EXPORT_SYMBOL_GPL(of_xlnx_bridge_get);

/**
 * of_xlnx_bridge_put - Put the Xlnx bridge instance
 * @bridge: Xlnx bridge instance to release
 *
 * Return the @bridge. After this, the bridge will be available for
 * other drivers to use.
 */
void of_xlnx_bridge_put(struct xlnx_bridge *bridge)
{
	if (WARN_ON(helper.error))
		return;

	mutex_lock(&helper.lock);
	WARN_ON(!bridge->owned);
	bridge->owned = false;
	mutex_unlock(&helper.lock);
}
EXPORT_SYMBOL_GPL(of_xlnx_bridge_put);

/*
 * Provider functions
 */

/**
 * xlnx_bridge_register - Register the bridge instance
 * @bridge: Xlnx bridge instance to register
 *
 * Register @bridge to be available for clients.
 *
 * Return: 0 on success. -EPROBE_DEFER if helper is not initialized, or
 * -EFAULT if in error state.
 */
int xlnx_bridge_register(struct xlnx_bridge *bridge)
{
	if (!helper.refcnt)
		return -EPROBE_DEFER;

	if (helper.error)
		return -EFAULT;

	mutex_lock(&helper.lock);
	WARN_ON(!bridge->of_node);
	bridge->owned = false;
	list_add_tail(&bridge->list, &helper.xlnx_bridges);
	mutex_unlock(&helper.lock);

	return 0;
}
EXPORT_SYMBOL_GPL(xlnx_bridge_register);

/**
 * xlnx_bridge_unregister - Unregister the bridge instance
 * @bridge: Xlnx bridge instance to unregister
 *
 * Unregister @bridge. The bridge shouldn't be owned by any client
 * at this point.
 */
void xlnx_bridge_unregister(struct xlnx_bridge *bridge)
{
	if (helper.error)
		return;

	mutex_lock(&helper.lock);
	WARN_ON(bridge->owned);
	list_del(&bridge->list);
	mutex_unlock(&helper.lock);
}
EXPORT_SYMBOL_GPL(xlnx_bridge_unregister);

/*
 * Internal functions: used by Xlnx DRM
 */

/**
 * xlnx_bridge_helper_init - Initialize the bridge helper
 * @void: No arg
 *
 * Initialize the bridge helper or increment the reference count
 * if already initialized.
 *
 * Return: 0 on success, or -EFAULT if in error state.
 */
int xlnx_bridge_helper_init(void)
{
	if (helper.refcnt++ > 0) {
		if (helper.error)
			return -EFAULT;
		return 0;
	}

	INIT_LIST_HEAD(&helper.xlnx_bridges);
	mutex_init(&helper.lock);
	helper.error = false;

	return 0;
}

/**
 * xlnx_bridge_helper_fini - Release the bridge helper
 *
 * Clean up or decrement the reference of the bridge helper.
 */
void xlnx_bridge_helper_fini(void)
{
	if (--helper.refcnt > 0)
		return;

	xlnx_bridge_debugfs_fini();

	if (WARN_ON(!list_empty(&helper.xlnx_bridges))) {
		helper.error = true;
		pr_err("any further xlnx bridge call will fail\n");
	}

	mutex_destroy(&helper.lock);
}
