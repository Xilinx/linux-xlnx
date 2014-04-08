/**
 * \file drm_stub.h
 * Stub support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_core.h>

unsigned int drm_debug = 0;	/* 1 to enable debug output */
EXPORT_SYMBOL(drm_debug);

unsigned int drm_rnodes = 0;	/* 1 to enable experimental render nodes API */
EXPORT_SYMBOL(drm_rnodes);

unsigned int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */
EXPORT_SYMBOL(drm_vblank_offdelay);

unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */
EXPORT_SYMBOL(drm_timestamp_precision);

/*
 * Default to use monotonic timestamps for wait-for-vblank and page-flip
 * complete events.
 */
unsigned int drm_timestamp_monotonic = 1;

MODULE_AUTHOR(CORE_AUTHOR);
MODULE_DESCRIPTION(CORE_DESC);
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(debug, "Enable debug output");
MODULE_PARM_DESC(rnodes, "Enable experimental render nodes API");
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs]");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");
MODULE_PARM_DESC(timestamp_monotonic, "Use monotonic timestamps");

module_param_named(debug, drm_debug, int, 0600);
module_param_named(rnodes, drm_rnodes, int, 0600);
module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
module_param_named(timestamp_monotonic, drm_timestamp_monotonic, int, 0600);

struct idr drm_minors_idr;

struct class *drm_class;
struct dentry *drm_debugfs_root;

int drm_err(const char *func, const char *format, ...)
{
	struct va_format vaf;
	va_list args;
	int r;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	r = printk(KERN_ERR "[" DRM_NAME ":%s] *ERROR* %pV", func, &vaf);

	va_end(args);

	return r;
}
EXPORT_SYMBOL(drm_err);

void drm_ut_debug_printk(unsigned int request_level,
			 const char *prefix,
			 const char *function_name,
			 const char *format, ...)
{
	va_list args;

	if (drm_debug & request_level) {
		if (function_name)
			printk(KERN_DEBUG "[%s:%s], ", prefix, function_name);
		va_start(args, format);
		vprintk(format, args);
		va_end(args);
	}
}
EXPORT_SYMBOL(drm_ut_debug_printk);

static int drm_minor_get_id(struct drm_device *dev, int type)
{
	int ret;
	int base = 0, limit = 63;

	if (type == DRM_MINOR_CONTROL) {
		base += 64;
		limit = base + 63;
	} else if (type == DRM_MINOR_RENDER) {
		base += 128;
		limit = base + 63;
	}

	mutex_lock(&dev->struct_mutex);
	ret = idr_alloc(&drm_minors_idr, NULL, base, limit, GFP_KERNEL);
	mutex_unlock(&dev->struct_mutex);

	return ret == -ENOSPC ? -EINVAL : ret;
}

struct drm_master *drm_master_create(struct drm_minor *minor)
{
	struct drm_master *master;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;

	kref_init(&master->refcount);
	spin_lock_init(&master->lock.spinlock);
	init_waitqueue_head(&master->lock.lock_queue);
	drm_ht_create(&master->magiclist, DRM_MAGIC_HASH_ORDER);
	INIT_LIST_HEAD(&master->magicfree);
	master->minor = minor;

	list_add_tail(&master->head, &minor->master_list);

	return master;
}

struct drm_master *drm_master_get(struct drm_master *master)
{
	kref_get(&master->refcount);
	return master;
}
EXPORT_SYMBOL(drm_master_get);

static void drm_master_destroy(struct kref *kref)
{
	struct drm_master *master = container_of(kref, struct drm_master, refcount);
	struct drm_magic_entry *pt, *next;
	struct drm_device *dev = master->minor->dev;
	struct drm_map_list *r_list, *list_temp;

	list_del(&master->head);

	if (dev->driver->master_destroy)
		dev->driver->master_destroy(dev, master);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head) {
		if (r_list->master == master) {
			drm_rmmap_locked(dev, r_list->map);
			r_list = NULL;
		}
	}

	if (master->unique) {
		kfree(master->unique);
		master->unique = NULL;
		master->unique_len = 0;
	}

	kfree(dev->devname);
	dev->devname = NULL;

	list_for_each_entry_safe(pt, next, &master->magicfree, head) {
		list_del(&pt->head);
		drm_ht_remove_item(&master->magiclist, &pt->hash_item);
		kfree(pt);
	}

	drm_ht_remove(&master->magiclist);

	kfree(master);
}

void drm_master_put(struct drm_master **master)
{
	kref_put(&(*master)->refcount, drm_master_destroy);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);

int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret = 0;

	if (file_priv->is_master)
		return 0;

	if (file_priv->minor->master && file_priv->minor->master != file_priv->master)
		return -EINVAL;

	if (!file_priv->master)
		return -EINVAL;

	if (file_priv->minor->master)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	file_priv->minor->master = drm_master_get(file_priv->master);
	file_priv->is_master = 1;
	if (dev->driver->master_set) {
		ret = dev->driver->master_set(dev, file_priv, false);
		if (unlikely(ret != 0)) {
			file_priv->is_master = 0;
			drm_master_put(&file_priv->minor->master);
		}
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	if (!file_priv->is_master)
		return -EINVAL;

	if (!file_priv->minor->master)
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	if (dev->driver->master_drop)
		dev->driver->master_drop(dev, file_priv, false);
	drm_master_put(&file_priv->minor->master);
	file_priv->is_master = 0;
	mutex_unlock(&dev->struct_mutex);
	return 0;
}

/**
 * drm_get_minor - Allocate and register new DRM minor
 * @dev: DRM device
 * @minor: Pointer to where new minor is stored
 * @type: Type of minor
 *
 * Allocate a new minor of the given type and register it. A pointer to the new
 * minor is returned in @minor.
 * Caller must hold the global DRM mutex.
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
static int drm_get_minor(struct drm_device *dev, struct drm_minor **minor,
			 int type)
{
	struct drm_minor *new_minor;
	int ret;
	int minor_id;

	DRM_DEBUG("\n");

	minor_id = drm_minor_get_id(dev, type);
	if (minor_id < 0)
		return minor_id;

	new_minor = kzalloc(sizeof(struct drm_minor), GFP_KERNEL);
	if (!new_minor) {
		ret = -ENOMEM;
		goto err_idr;
	}

	new_minor->type = type;
	new_minor->device = MKDEV(DRM_MAJOR, minor_id);
	new_minor->dev = dev;
	new_minor->index = minor_id;
	INIT_LIST_HEAD(&new_minor->master_list);

	idr_replace(&drm_minors_idr, new_minor, minor_id);

#if defined(CONFIG_DEBUG_FS)
	ret = drm_debugfs_init(new_minor, minor_id, drm_debugfs_root);
	if (ret) {
		DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
		goto err_mem;
	}
#endif

	ret = drm_sysfs_device_add(new_minor);
	if (ret) {
		printk(KERN_ERR
		       "DRM: Error sysfs_device_add.\n");
		goto err_debugfs;
	}
	*minor = new_minor;

	DRM_DEBUG("new minor assigned %d\n", minor_id);
	return 0;


err_debugfs:
#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_cleanup(new_minor);
err_mem:
#endif
	kfree(new_minor);
err_idr:
	idr_remove(&drm_minors_idr, minor_id);
	*minor = NULL;
	return ret;
}

/**
 * drm_unplug_minor - Unplug DRM minor
 * @minor: Minor to unplug
 *
 * Unplugs the given DRM minor but keeps the object. So after this returns,
 * minor->dev is still valid so existing open-files can still access it to get
 * device information from their drm_file ojects.
 * If the minor is already unplugged or if @minor is NULL, nothing is done.
 * The global DRM mutex must be held by the caller.
 */
static void drm_unplug_minor(struct drm_minor *minor)
{
	if (!minor || !minor->kdev)
		return;

#if defined(CONFIG_DEBUG_FS)
	drm_debugfs_cleanup(minor);
#endif

	drm_sysfs_device_remove(minor);
	idr_remove(&drm_minors_idr, minor->index);
}

/**
 * drm_put_minor - Destroy DRM minor
 * @minor: Minor to destroy
 *
 * This calls drm_unplug_minor() on the given minor and then frees it. Nothing
 * is done if @minor is NULL. It is fine to call this on already unplugged
 * minors.
 * The global DRM mutex must be held by the caller.
 */
static void drm_put_minor(struct drm_minor *minor)
{
	if (!minor)
		return;

	DRM_DEBUG("release secondary minor %d\n", minor->index);

	drm_unplug_minor(minor);
	kfree(minor);
}

/**
 * Called via drm_exit() at module unload time or when pci device is
 * unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 */
void drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_free(dev);
}
EXPORT_SYMBOL(drm_put_dev);

void drm_unplug_dev(struct drm_device *dev)
{
	/* for a USB device */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_unplug_minor(dev->control);
	if (dev->render)
		drm_unplug_minor(dev->render);
	drm_unplug_minor(dev->primary);

	mutex_lock(&drm_global_mutex);

	drm_device_set_unplugged(dev);

	if (dev->open_count == 0) {
		drm_put_dev(dev);
	}
	mutex_unlock(&drm_global_mutex);
}
EXPORT_SYMBOL(drm_unplug_dev);

/**
 * drm_dev_alloc - Allocate new drm device
 * @driver: DRM driver to allocate device for
 * @parent: Parent device object
 *
 * Allocate and initialize a new DRM device. No device registration is done.
 * Call drm_dev_register() to advertice the device to user space and register it
 * with other core subsystems.
 *
 * RETURNS:
 * Pointer to new DRM device, or NULL if out of memory.
 */
struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent)
{
	struct drm_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->dev = parent;
	dev->driver = driver;

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->ctxlist);
	INIT_LIST_HEAD(&dev->vmalist);
	INIT_LIST_HEAD(&dev->maplist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->count_lock);
	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->ctxlist_mutex);

	if (drm_ht_create(&dev->map_hash, 12))
		goto err_free;

	ret = drm_ctxbitmap_init(dev);
	if (ret) {
		DRM_ERROR("Cannot allocate memory for context bitmap.\n");
		goto err_ht;
	}

	if (driver->driver_features & DRIVER_GEM) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cannot initialize graphics execution manager (GEM)\n");
			goto err_ctxbitmap;
		}
	}

	return dev;

err_ctxbitmap:
	drm_ctxbitmap_cleanup(dev);
err_ht:
	drm_ht_remove(&dev->map_hash);
err_free:
	kfree(dev);
	return NULL;
}
EXPORT_SYMBOL(drm_dev_alloc);

/**
 * drm_dev_free - Free DRM device
 * @dev: DRM device to free
 *
 * Free a DRM device that has previously been allocated via drm_dev_alloc().
 * You must not use kfree() instead or you will leak memory.
 *
 * This must not be called once the device got registered. Use drm_put_dev()
 * instead, which then calls drm_dev_free().
 */
void drm_dev_free(struct drm_device *dev)
{
	drm_put_minor(dev->control);
	drm_put_minor(dev->render);
	drm_put_minor(dev->primary);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);

	drm_ctxbitmap_cleanup(dev);
	drm_ht_remove(&dev->map_hash);

	kfree(dev->devname);
	kfree(dev);
}
EXPORT_SYMBOL(drm_dev_free);

/**
 * drm_dev_register - Register DRM device
 * @dev: Device to register
 *
 * Register the DRM device @dev with the system, advertise device to user-space
 * and start normal device operation. @dev must be allocated via drm_dev_alloc()
 * previously.
 *
 * Never call this twice on any device!
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	int ret;

	mutex_lock(&drm_global_mutex);

	if (dev->driver->bus->agp_init) {
		ret = dev->driver->bus->agp_init(dev);
		if (ret)
			goto out_unlock;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
		if (ret)
			goto err_agp;
	}

	if (drm_core_check_feature(dev, DRIVER_RENDER) && drm_rnodes) {
		ret = drm_get_minor(dev, &dev->render, DRM_MINOR_RENDER);
		if (ret)
			goto err_control_node;
	}

	ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY);
	if (ret)
		goto err_render_node;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, flags);
		if (ret)
			goto err_primary_node;
	}

	/* setup grouping for legacy outputs */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_mode_group_init_legacy_group(dev,
				&dev->primary->mode_group);
		if (ret)
			goto err_unload;
	}

	list_add_tail(&dev->driver_item, &dev->driver->device_list);

	ret = 0;
	goto out_unlock;

err_unload:
	if (dev->driver->unload)
		dev->driver->unload(dev);
err_primary_node:
	drm_unplug_minor(dev->primary);
err_render_node:
	drm_unplug_minor(dev->render);
err_control_node:
	drm_unplug_minor(dev->control);
err_agp:
	if (dev->driver->bus->agp_destroy)
		dev->driver->bus->agp_destroy(dev);
out_unlock:
	mutex_unlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_dev_register);

/**
 * drm_dev_unregister - Unregister DRM device
 * @dev: Device to unregister
 *
 * Unregister the DRM device from the system. This does the reverse of
 * drm_dev_register() but does not deallocate the device. The caller must call
 * drm_dev_free() to free all resources.
 */
void drm_dev_unregister(struct drm_device *dev)
{
	struct drm_map_list *r_list, *list_temp;

	drm_lastclose(dev);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	if (dev->driver->bus->agp_destroy)
		dev->driver->bus->agp_destroy(dev);

	drm_vblank_cleanup(dev);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head)
		drm_rmmap(dev, r_list->map);

	drm_unplug_minor(dev->control);
	drm_unplug_minor(dev->render);
	drm_unplug_minor(dev->primary);

	list_del(&dev->driver_item);
}
EXPORT_SYMBOL(drm_dev_unregister);
