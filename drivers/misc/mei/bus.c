/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/mei_cl_bus.h>

#include "mei_dev.h"
#include "hw-me.h"
#include "client.h"

#define to_mei_cl_driver(d) container_of(d, struct mei_cl_driver, driver)
#define to_mei_cl_device(d) container_of(d, struct mei_cl_device, dev)

static int mei_cl_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver = to_mei_cl_driver(drv);
	const struct mei_cl_device_id *id;

	if (!device)
		return 0;

	if (!driver || !driver->id_table)
		return 0;

	id = driver->id_table;

	while (id->name[0]) {
		if (!strncmp(dev_name(dev), id->name, sizeof(id->name)))
			return 1;

		id++;
	}

	return 0;
}

static int mei_cl_device_probe(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;
	struct mei_cl_device_id id;

	if (!device)
		return 0;

	driver = to_mei_cl_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	dev_dbg(dev, "Device probe\n");

	strncpy(id.name, dev_name(dev), sizeof(id.name));

	return driver->probe(device, &id);
}

static int mei_cl_device_remove(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;

	if (!device || !dev->driver)
		return 0;

	if (device->event_cb) {
		device->event_cb = NULL;
		cancel_work_sync(&device->event_work);
	}

	driver = to_mei_cl_driver(dev->driver);
	if (!driver->remove) {
		dev->driver = NULL;

		return 0;
	}

	return driver->remove(device);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	int len;

	len = snprintf(buf, PAGE_SIZE, "mei:%s\n", dev_name(dev));

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *mei_cl_dev_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mei_cl_dev);

static int mei_cl_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "MODALIAS=mei:%s", dev_name(dev)))
		return -ENOMEM;

	return 0;
}

static struct bus_type mei_cl_bus_type = {
	.name		= "mei",
	.dev_groups	= mei_cl_dev_groups,
	.match		= mei_cl_device_match,
	.probe		= mei_cl_device_probe,
	.remove		= mei_cl_device_remove,
	.uevent		= mei_cl_uevent,
};

static void mei_cl_dev_release(struct device *dev)
{
	kfree(to_mei_cl_device(dev));
}

static struct device_type mei_cl_device_type = {
	.release	= mei_cl_dev_release,
};

static struct mei_cl *mei_bus_find_mei_cl_by_uuid(struct mei_device *dev,
						uuid_le uuid)
{
	struct mei_cl *cl, *next;

	list_for_each_entry_safe(cl, next, &dev->device_list, device_link) {
		if (!uuid_le_cmp(uuid, cl->device_uuid))
			return cl;
	}

	return NULL;
}
struct mei_cl_device *mei_cl_add_device(struct mei_device *dev,
					uuid_le uuid, char *name,
					struct mei_cl_ops *ops)
{
	struct mei_cl_device *device;
	struct mei_cl *cl;
	int status;

	cl = mei_bus_find_mei_cl_by_uuid(dev, uuid);
	if (cl == NULL)
		return NULL;

	device = kzalloc(sizeof(struct mei_cl_device), GFP_KERNEL);
	if (!device)
		return NULL;

	device->cl = cl;
	device->ops = ops;

	device->dev.parent = &dev->pdev->dev;
	device->dev.bus = &mei_cl_bus_type;
	device->dev.type = &mei_cl_device_type;

	dev_set_name(&device->dev, "%s", name);

	status = device_register(&device->dev);
	if (status) {
		dev_err(&dev->pdev->dev, "Failed to register MEI device\n");
		kfree(device);
		return NULL;
	}

	cl->device = device;

	dev_dbg(&device->dev, "client %s registered\n", name);

	return device;
}
EXPORT_SYMBOL_GPL(mei_cl_add_device);

void mei_cl_remove_device(struct mei_cl_device *device)
{
	device_unregister(&device->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_remove_device);

int __mei_cl_driver_register(struct mei_cl_driver *driver, struct module *owner)
{
	int err;

	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.bus = &mei_cl_bus_type;

	err = driver_register(&driver->driver);
	if (err)
		return err;

	pr_debug("mei: driver [%s] registered\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL_GPL(__mei_cl_driver_register);

void mei_cl_driver_unregister(struct mei_cl_driver *driver)
{
	driver_unregister(&driver->driver);

	pr_debug("mei: driver [%s] unregistered\n", driver->driver.name);
}
EXPORT_SYMBOL_GPL(mei_cl_driver_unregister);

static int ___mei_cl_send(struct mei_cl *cl, u8 *buf, size_t length,
			bool blocking)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	int id;
	int rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (cl->state != MEI_FILE_CONNECTED)
		return -ENODEV;

	/* Check if we have an ME client device */
	id = mei_me_cl_by_id(dev, cl->me_client_id);
	if (id < 0)
		return id;

	if (length > dev->me_clients[id].props.max_msg_length)
		return -EINVAL;

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	rets = mei_io_cb_alloc_req_buf(cb, length);
	if (rets < 0) {
		mei_io_cb_free(cb);
		return rets;
	}

	memcpy(cb->request_buffer.data, buf, length);

	mutex_lock(&dev->device_lock);

	rets = mei_cl_write(cl, cb, blocking);

	mutex_unlock(&dev->device_lock);
	if (rets < 0)
		mei_io_cb_free(cb);

	return rets;
}

int __mei_cl_recv(struct mei_cl *cl, u8 *buf, size_t length)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	size_t r_length;
	int err;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (!cl->read_cb) {
		err = mei_cl_read_start(cl, length);
		if (err < 0) {
			mutex_unlock(&dev->device_lock);
			return err;
		}
	}

	if (cl->reading_state != MEI_READ_COMPLETE &&
	    !waitqueue_active(&cl->rx_wait)) {

		mutex_unlock(&dev->device_lock);

		if (wait_event_interruptible(cl->rx_wait,
				cl->reading_state == MEI_READ_COMPLETE  ||
				mei_cl_is_transitioning(cl))) {

			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		mutex_lock(&dev->device_lock);
	}

	cb = cl->read_cb;

	if (cl->reading_state != MEI_READ_COMPLETE) {
		r_length = 0;
		goto out;
	}

	r_length = min_t(size_t, length, cb->buf_idx);

	memcpy(buf, cb->response_buffer.data, r_length);

	mei_io_cb_free(cb);
	cl->reading_state = MEI_IDLE;
	cl->read_cb = NULL;

out:
	mutex_unlock(&dev->device_lock);

	return r_length;
}

inline int __mei_cl_async_send(struct mei_cl *cl, u8 *buf, size_t length)
{
	return ___mei_cl_send(cl, buf, length, 0);
}

inline int __mei_cl_send(struct mei_cl *cl, u8 *buf, size_t length)
{
	return ___mei_cl_send(cl, buf, length, 1);
}

int mei_cl_send(struct mei_cl_device *device, u8 *buf, size_t length)
{
	struct mei_cl *cl = device->cl;

	if (cl == NULL)
		return -ENODEV;

	if (device->ops && device->ops->send)
		return device->ops->send(device, buf, length);

	return __mei_cl_send(cl, buf, length);
}
EXPORT_SYMBOL_GPL(mei_cl_send);

int mei_cl_recv(struct mei_cl_device *device, u8 *buf, size_t length)
{
	struct mei_cl *cl =  device->cl;

	if (cl == NULL)
		return -ENODEV;

	if (device->ops && device->ops->recv)
		return device->ops->recv(device, buf, length);

	return __mei_cl_recv(cl, buf, length);
}
EXPORT_SYMBOL_GPL(mei_cl_recv);

static void mei_bus_event_work(struct work_struct *work)
{
	struct mei_cl_device *device;

	device = container_of(work, struct mei_cl_device, event_work);

	if (device->event_cb)
		device->event_cb(device, device->events, device->event_context);

	device->events = 0;

	/* Prepare for the next read */
	mei_cl_read_start(device->cl, 0);
}

int mei_cl_register_event_cb(struct mei_cl_device *device,
			  mei_cl_event_cb_t event_cb, void *context)
{
	if (device->event_cb)
		return -EALREADY;

	device->events = 0;
	device->event_cb = event_cb;
	device->event_context = context;
	INIT_WORK(&device->event_work, mei_bus_event_work);

	mei_cl_read_start(device->cl, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cl_register_event_cb);

void *mei_cl_get_drvdata(const struct mei_cl_device *device)
{
	return dev_get_drvdata(&device->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_get_drvdata);

void mei_cl_set_drvdata(struct mei_cl_device *device, void *data)
{
	dev_set_drvdata(&device->dev, data);
}
EXPORT_SYMBOL_GPL(mei_cl_set_drvdata);

int mei_cl_enable_device(struct mei_cl_device *device)
{
	int err;
	struct mei_device *dev;
	struct mei_cl *cl = device->cl;

	if (cl == NULL)
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	cl->state = MEI_FILE_CONNECTING;

	err = mei_cl_connect(cl, NULL);
	if (err < 0) {
		mutex_unlock(&dev->device_lock);
		dev_err(&dev->pdev->dev, "Could not connect to the ME client");

		return err;
	}

	mutex_unlock(&dev->device_lock);

	if (device->event_cb && !cl->read_cb)
		mei_cl_read_start(device->cl, 0);

	if (!device->ops || !device->ops->enable)
		return 0;

	return device->ops->enable(device);
}
EXPORT_SYMBOL_GPL(mei_cl_enable_device);

int mei_cl_disable_device(struct mei_cl_device *device)
{
	int err;
	struct mei_device *dev;
	struct mei_cl *cl = device->cl;

	if (cl == NULL)
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (cl->state != MEI_FILE_CONNECTED) {
		mutex_unlock(&dev->device_lock);
		dev_err(&dev->pdev->dev, "Already disconnected");

		return 0;
	}

	cl->state = MEI_FILE_DISCONNECTING;

	err = mei_cl_disconnect(cl);
	if (err < 0) {
		mutex_unlock(&dev->device_lock);
		dev_err(&dev->pdev->dev,
			"Could not disconnect from the ME client");

		return err;
	}

	/* Flush queues and remove any pending read */
	mei_cl_flush_queues(cl);

	if (cl->read_cb) {
		struct mei_cl_cb *cb = NULL;

		cb = mei_cl_find_read_cb(cl);
		/* Remove entry from read list */
		if (cb)
			list_del(&cb->list);

		cb = cl->read_cb;
		cl->read_cb = NULL;

		if (cb) {
			mei_io_cb_free(cb);
			cb = NULL;
		}
	}

	device->event_cb = NULL;

	mutex_unlock(&dev->device_lock);

	if (!device->ops || !device->ops->disable)
		return 0;

	return device->ops->disable(device);
}
EXPORT_SYMBOL_GPL(mei_cl_disable_device);

void mei_cl_bus_rx_event(struct mei_cl *cl)
{
	struct mei_cl_device *device = cl->device;

	if (!device || !device->event_cb)
		return;

	set_bit(MEI_CL_EVENT_RX, &device->events);

	schedule_work(&device->event_work);
}

int __init mei_cl_bus_init(void)
{
	return bus_register(&mei_cl_bus_type);
}

void __exit mei_cl_bus_exit(void)
{
	bus_unregister(&mei_cl_bus_type);
}
