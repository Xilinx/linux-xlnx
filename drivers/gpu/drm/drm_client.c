// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Noralf Trønnes
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <drm/drm_client.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>
#include <drm/drm_print.h>
#include <drm/drmP.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * This library provides support for clients running in the kernel like fbdev and bootsplash.
 * Currently it's only partially implemented, just enough to support fbdev.
 *
 * GEM drivers which provide a GEM based dumb buffer with a virtual address are supported.
 */

static int drm_client_open(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	struct drm_file *file;

	file = drm_file_alloc(dev->primary);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&dev->filelist_mutex);
	list_add(&file->lhead, &dev->filelist_internal);
	mutex_unlock(&dev->filelist_mutex);

	client->file = file;

	return 0;
}

static void drm_client_close(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;

	mutex_lock(&dev->filelist_mutex);
	list_del(&client->file->lhead);
	mutex_unlock(&dev->filelist_mutex);

	drm_file_free(client->file);
}
EXPORT_SYMBOL(drm_client_close);

/**
 * drm_client_init - Initialise a DRM client
 * @dev: DRM device
 * @client: DRM client
 * @name: Client name
 * @funcs: DRM client functions (optional)
 *
 * This initialises the client and opens a &drm_file. Use drm_client_add() to complete the process.
 * The caller needs to hold a reference on @dev before calling this function.
 * The client is freed when the &drm_device is unregistered. See drm_client_release().
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_init(struct drm_device *dev, struct drm_client_dev *client,
		    const char *name, const struct drm_client_funcs *funcs)
{
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET) || !dev->driver->dumb_create)
		return -ENOTSUPP;

	if (funcs && !try_module_get(funcs->owner))
		return -ENODEV;

	client->dev = dev;
	client->name = name;
	client->funcs = funcs;

	ret = drm_client_open(client);
	if (ret)
		goto err_put_module;

	drm_dev_get(dev);

	return 0;

err_put_module:
	if (funcs)
		module_put(funcs->owner);

	return ret;
}
EXPORT_SYMBOL(drm_client_init);

/**
 * drm_client_add - Add client to the device list
 * @client: DRM client
 *
 * Add the client to the &drm_device client list to activate its callbacks.
 * @client must be initialized by a call to drm_client_init(). After
 * drm_client_add() it is no longer permissible to call drm_client_release()
 * directly (outside the unregister callback), instead cleanup will happen
 * automatically on driver unload.
 */
void drm_client_add(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;

	mutex_lock(&dev->clientlist_mutex);
	list_add(&client->list, &dev->clientlist);
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_add);

/**
 * drm_client_release - Release DRM client resources
 * @client: DRM client
 *
 * Releases resources by closing the &drm_file that was opened by drm_client_init().
 * It is called automatically if the &drm_client_funcs.unregister callback is _not_ set.
 *
 * This function should only be called from the unregister callback. An exception
 * is fbdev which cannot free the buffer if userspace has open file descriptors.
 *
 * Note:
 * Clients cannot initiate a release by themselves. This is done to keep the code simple.
 * The driver has to be unloaded before the client can be unloaded.
 */
void drm_client_release(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;

	DRM_DEV_DEBUG_KMS(dev->dev, "%s\n", client->name);

	drm_client_close(client);
	drm_dev_put(dev);
	if (client->funcs)
		module_put(client->funcs->owner);
}
EXPORT_SYMBOL(drm_client_release);

void drm_client_dev_unregister(struct drm_device *dev)
{
	struct drm_client_dev *client, *tmp;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry_safe(client, tmp, &dev->clientlist, list) {
		list_del(&client->list);
		if (client->funcs && client->funcs->unregister) {
			client->funcs->unregister(client);
		} else {
			drm_client_release(client);
			kfree(client);
		}
	}
	mutex_unlock(&dev->clientlist_mutex);
}

/**
 * drm_client_dev_hotplug - Send hotplug event to clients
 * @dev: DRM device
 *
 * This function calls the &drm_client_funcs.hotplug callback on the attached clients.
 *
 * drm_kms_helper_hotplug_event() calls this function, so drivers that use it
 * don't need to call this function themselves.
 */
void drm_client_dev_hotplug(struct drm_device *dev)
{
	struct drm_client_dev *client;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if (!client->funcs || !client->funcs->hotplug)
			continue;

		ret = client->funcs->hotplug(client);
		DRM_DEV_DEBUG_KMS(dev->dev, "%s: ret=%d\n", client->name, ret);
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_dev_hotplug);

void drm_client_dev_restore(struct drm_device *dev)
{
	struct drm_client_dev *client;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list) {
		if (!client->funcs || !client->funcs->restore)
			continue;

		ret = client->funcs->restore(client);
		DRM_DEV_DEBUG_KMS(dev->dev, "%s: ret=%d\n", client->name, ret);
		if (!ret) /* The first one to return zero gets the privilege to restore */
			break;
	}
	mutex_unlock(&dev->clientlist_mutex);
}

static void drm_client_buffer_delete(struct drm_client_buffer *buffer)
{
	struct drm_device *dev = buffer->client->dev;

	drm_gem_vunmap(buffer->gem, buffer->vaddr);

	if (buffer->gem)
		drm_gem_object_put_unlocked(buffer->gem);

	if (buffer->handle)
		drm_mode_destroy_dumb(dev, buffer->handle, buffer->client->file);

	kfree(buffer);
}

static struct drm_client_buffer *
drm_client_buffer_create(struct drm_client_dev *client, u32 width, u32 height, u32 format)
{
	struct drm_mode_create_dumb dumb_args = { };
	struct drm_device *dev = client->dev;
	struct drm_client_buffer *buffer;
	struct drm_gem_object *obj;
	void *vaddr;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->client = client;

	dumb_args.width = width;
	dumb_args.height = height;
	dumb_args.bpp = drm_format_plane_cpp(format, 0) * 8;
	ret = drm_mode_create_dumb(dev, &dumb_args, client->file);
	if (ret)
		goto err_delete;

	buffer->handle = dumb_args.handle;
	buffer->pitch = dumb_args.pitch;

	obj = drm_gem_object_lookup(client->file, dumb_args.handle);
	if (!obj)  {
		ret = -ENOENT;
		goto err_delete;
	}

	buffer->gem = obj;

	/*
	 * FIXME: The dependency on GEM here isn't required, we could
	 * convert the driver handle to a dma-buf instead and use the
	 * backend-agnostic dma-buf vmap support instead. This would
	 * require that the handle2fd prime ioctl is reworked to pull the
	 * fd_install step out of the driver backend hooks, to make that
	 * final step optional for internal users.
	 */
	vaddr = drm_gem_vmap(obj);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto err_delete;
	}

	buffer->vaddr = vaddr;

	return buffer;

err_delete:
	drm_client_buffer_delete(buffer);

	return ERR_PTR(ret);
}

static void drm_client_buffer_rmfb(struct drm_client_buffer *buffer)
{
	int ret;

	if (!buffer->fb)
		return;

	ret = drm_mode_rmfb(buffer->client->dev, buffer->fb->base.id, buffer->client->file);
	if (ret)
		DRM_DEV_ERROR(buffer->client->dev->dev,
			      "Error removing FB:%u (%d)\n", buffer->fb->base.id, ret);

	buffer->fb = NULL;
}

static int drm_client_buffer_addfb(struct drm_client_buffer *buffer,
				   u32 width, u32 height, u32 format)
{
	struct drm_client_dev *client = buffer->client;
	struct drm_mode_fb_cmd fb_req = { };
	const struct drm_format_info *info;
	int ret;

	info = drm_format_info(format);
	fb_req.bpp = info->cpp[0] * 8;
	fb_req.depth = info->depth;
	fb_req.width = width;
	fb_req.height = height;
	fb_req.handle = buffer->handle;
	fb_req.pitch = buffer->pitch;

	ret = drm_mode_addfb(client->dev, &fb_req, client->file);
	if (ret)
		return ret;

	buffer->fb = drm_framebuffer_lookup(client->dev, buffer->client->file, fb_req.fb_id);
	if (WARN_ON(!buffer->fb))
		return -ENOENT;

	/* drop the reference we picked up in framebuffer lookup */
	drm_framebuffer_put(buffer->fb);

	strscpy(buffer->fb->comm, client->name, TASK_COMM_LEN);

	return 0;
}

/**
 * drm_client_framebuffer_create - Create a client framebuffer
 * @client: DRM client
 * @width: Framebuffer width
 * @height: Framebuffer height
 * @format: Buffer format
 *
 * This function creates a &drm_client_buffer which consists of a
 * &drm_framebuffer backed by a dumb buffer.
 * Call drm_client_framebuffer_delete() to free the buffer.
 *
 * Returns:
 * Pointer to a client buffer or an error pointer on failure.
 */
struct drm_client_buffer *
drm_client_framebuffer_create(struct drm_client_dev *client, u32 width, u32 height, u32 format)
{
	struct drm_client_buffer *buffer;
	int ret;

	buffer = drm_client_buffer_create(client, width, height, format);
	if (IS_ERR(buffer))
		return buffer;

	ret = drm_client_buffer_addfb(buffer, width, height, format);
	if (ret) {
		drm_client_buffer_delete(buffer);
		return ERR_PTR(ret);
	}

	return buffer;
}
EXPORT_SYMBOL(drm_client_framebuffer_create);

/**
 * drm_client_framebuffer_delete - Delete a client framebuffer
 * @buffer: DRM client buffer (can be NULL)
 */
void drm_client_framebuffer_delete(struct drm_client_buffer *buffer)
{
	if (!buffer)
		return;

	drm_client_buffer_rmfb(buffer);
	drm_client_buffer_delete(buffer);
}
EXPORT_SYMBOL(drm_client_framebuffer_delete);

#ifdef CONFIG_DEBUG_FS
static int drm_client_debugfs_internal_clients(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_client_dev *client;

	mutex_lock(&dev->clientlist_mutex);
	list_for_each_entry(client, &dev->clientlist, list)
		drm_printf(&p, "%s\n", client->name);
	mutex_unlock(&dev->clientlist_mutex);

	return 0;
}

static const struct drm_info_list drm_client_debugfs_list[] = {
	{ "internal_clients", drm_client_debugfs_internal_clients, 0 },
};

int drm_client_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(drm_client_debugfs_list,
					ARRAY_SIZE(drm_client_debugfs_list),
					minor->debugfs_root, minor);
}
#endif
