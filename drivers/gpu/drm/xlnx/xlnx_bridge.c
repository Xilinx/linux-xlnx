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

struct videomode;
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
 * xlnx_bridge_set_output - Set the output of @bridge
 * @bridge: bridge to set
 * @width: width
 * @height: height
 * @bus_fmt: bus format (ex, MEDIA_BUS_FMT_*);
 *
 * Set the bridge output with height / width / format.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT if in error state,
 * or return code from callback.
 */
int xlnx_bridge_set_output(struct xlnx_bridge *bridge,
			   u32 width, u32 height, u32 bus_fmt)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->set_output)
		return bridge->set_output(bridge, width, height, bus_fmt);

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_set_output);

/**
 * xlnx_bridge_get_output_fmts - Get the supported output formats
 * @bridge: bridge to set
 * @fmts: pointer to formats
 * @count: pointer to format count
 *
 * Get the list of supported output bus formats.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT if in error state,
 * or return code from callback.
 */
int xlnx_bridge_get_output_fmts(struct xlnx_bridge *bridge,
				const u32 **fmts, u32 *count)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->get_output_fmts)
		return bridge->get_output_fmts(bridge, fmts, count);

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_get_output_fmts);

/**
 * xlnx_bridge_set_timing - Set the video timing
 * @bridge: bridge to set
 * @vm: Videomode
 *
 * Set the video mode so that timing can be generated using this
 * by the video timing controller.
 *
 * Return: 0 on success. -ENOENT if no callback, -EFAULT if in error state,
 * or return code from callback.
 */
int xlnx_bridge_set_timing(struct xlnx_bridge *bridge, struct videomode *vm)
{
	if (!bridge)
		return 0;

	if (helper.error)
		return -EFAULT;

	if (bridge->set_timing) {
		bridge->set_timing(bridge, vm);
		return 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(xlnx_bridge_set_timing);

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

#ifdef CONFIG_DRM_XLNX_BRIDGE_DEBUG_FS

#include <linux/debugfs.h>

struct xlnx_bridge_debugfs_dir {
	struct dentry *dir;
	int ref_cnt;
};

static struct xlnx_bridge_debugfs_dir *dir;

struct xlnx_bridge_debugfs_file {
	struct dentry *file;
	const char *status;
};

#define XLNX_BRIDGE_DEBUGFS_MAX_BYTES	16

static ssize_t xlnx_bridge_debugfs_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct xlnx_bridge *bridge = f->f_inode->i_private;
	int ret;

	if (size <= 0)
		return -EINVAL;

	if (*pos != 0)
		return 0;

	size = min(size, strlen(bridge->debugfs_file->status));
	ret = copy_to_user(buf, bridge->debugfs_file->status, size);
	if (ret)
		return ret;

	*pos = size + 1;
	return size;
}

static ssize_t xlnx_bridge_debugfs_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	struct xlnx_bridge *bridge = f->f_inode->i_private;

	if (*pos != 0 || size <= 0)
		return -EINVAL;

	if (!strncmp(buf, "enable", 5)) {
		xlnx_bridge_enable(bridge);
	} else if (!strncmp(buf, "disable", 6)) {
		xlnx_bridge_disable(bridge);
	} else if (!strncmp(buf, "set_input", 3)) {
		char *cmd, **tmp;
		char *w, *h, *f;
		u32 width, height, fmt;
		int ret = -EINVAL;

		cmd = kzalloc(size, GFP_KERNEL);
		ret = strncpy_from_user(cmd, buf, size);
		if (ret < 0) {
			pr_err("%s %d failed to copy the command  %s\n",
			       __func__, __LINE__, buf);
			return ret;
		}

		tmp = &cmd;
		strsep(tmp, " ");
		w = strsep(tmp, " ");
		h = strsep(tmp, " ");
		f = strsep(tmp, " ");
		if (w && h && f) {
			ret = kstrtouint(w, 0, &width);
			ret |= kstrtouint(h, 0, &height);
			ret |= kstrtouint(f, 0, &fmt);
		}

		kfree(cmd);
		if (ret) {
			pr_err("%s %d invalid command: %s\n",
			       __func__, __LINE__, buf);
			return -EINVAL;
		}
		xlnx_bridge_set_input(bridge, width, height, fmt);
	}

	return size;
}

static const struct file_operations xlnx_bridge_debugfs_fops = {
	.owner	= THIS_MODULE,
	.read	= xlnx_bridge_debugfs_read,
	.write	= xlnx_bridge_debugfs_write,
};

static int xlnx_bridge_debugfs_register(struct xlnx_bridge *bridge)
{
	struct xlnx_bridge_debugfs_file *file;
	char file_name[32];

	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	snprintf(file_name, sizeof(file_name), "xlnx_bridge-%s",
		 bridge->of_node->name);
	file->file = debugfs_create_file(file_name, 0444, dir->dir, bridge,
					 &xlnx_bridge_debugfs_fops);
	bridge->debugfs_file = file;

	return 0;
}

static void xlnx_bridge_debugfs_unregister(struct xlnx_bridge *bridge)
{
	debugfs_remove(bridge->debugfs_file->file);
	kfree(bridge->debugfs_file);
}

static int xlnx_bridge_debugfs_init(void)
{
	if (dir) {
		dir->ref_cnt++;
		return 0;
	}

	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		return -ENOMEM;

	dir->dir = debugfs_create_dir("xlnx-bridge", NULL);
	if (!dir->dir)
		return -ENODEV;
	dir->ref_cnt++;

	return 0;
}

static void xlnx_bridge_debugfs_fini(void)
{
	if (--dir->ref_cnt)
		return;

	debugfs_remove_recursive(dir->dir);
	dir = NULL;
}

#else

static int xlnx_bridge_debugfs_register(struct xlnx_bridge *bridge)
{
	return 0;
}

static void xlnx_bridge_debugfs_unregister(struct xlnx_bridge *bridge)
{
}

static int xlnx_bridge_debugfs_init(void)
{
	return 0;
}

static void xlnx_bridge_debugfs_fini(void)
{
}

#endif

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
	xlnx_bridge_debugfs_register(bridge);
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
	xlnx_bridge_debugfs_unregister(bridge);
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

	if (xlnx_bridge_debugfs_init())
		pr_err("failed to init xlnx bridge debugfs\n");

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
