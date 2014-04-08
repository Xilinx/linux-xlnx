/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 *
 * This file is released under the GPLv2.
 */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>

#include "internal.h"

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(fmt, ...)						\
	printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)						\
do {								\
	if (0)							\
		printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__);	\
} while (0)
#endif
static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

#define PHYSICAL_NODE_STRING "physical_node"
#define PHYSICAL_NODE_NAME_SIZE (sizeof(PHYSICAL_NODE_STRING) + 10)

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->match && type->find_device) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n", type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(register_acpi_bus_type);

int unregister_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return 0;
	if (type) {
		down_write(&bus_type_sem);
		list_del_init(&type->list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s unregistered\n",
		       type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(unregister_acpi_bus_type);

static struct acpi_bus_type *acpi_get_bus_type(struct device *dev)
{
	struct acpi_bus_type *tmp, *ret = NULL;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->match(dev)) {
			ret = tmp;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

#define FIND_CHILD_MIN_SCORE	1
#define FIND_CHILD_MAX_SCORE	2

static acpi_status acpi_dev_present(acpi_handle handle, u32 lvl_not_used,
				  void *not_used, void **ret_p)
{
	struct acpi_device *adev = NULL;

	acpi_bus_get_device(handle, &adev);
	if (adev) {
		*ret_p = handle;
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

static int do_find_child_checks(acpi_handle handle, bool is_bridge)
{
	bool sta_present = true;
	unsigned long long sta;
	acpi_status status;

	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (status == AE_NOT_FOUND)
		sta_present = false;
	else if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_ENABLED))
		return -ENODEV;

	if (is_bridge) {
		void *test = NULL;

		/* Check if this object has at least one child device. */
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				    acpi_dev_present, NULL, NULL, &test);
		if (!test)
			return -ENODEV;
	}
	return sta_present ? FIND_CHILD_MAX_SCORE : FIND_CHILD_MIN_SCORE;
}

struct find_child_context {
	u64 addr;
	bool is_bridge;
	acpi_handle ret;
	int ret_score;
};

static acpi_status do_find_child(acpi_handle handle, u32 lvl_not_used,
				 void *data, void **not_used)
{
	struct find_child_context *context = data;
	unsigned long long addr;
	acpi_status status;
	int score;

	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &addr);
	if (ACPI_FAILURE(status) || addr != context->addr)
		return AE_OK;

	if (!context->ret) {
		/* This is the first matching object.  Save its handle. */
		context->ret = handle;
		return AE_OK;
	}
	/*
	 * There is more than one matching object with the same _ADR value.
	 * That really is unexpected, so we are kind of beyond the scope of the
	 * spec here.  We have to choose which one to return, though.
	 *
	 * First, check if the previously found object is good enough and return
	 * its handle if so.  Second, check the same for the object that we've
	 * just found.
	 */
	if (!context->ret_score) {
		score = do_find_child_checks(context->ret, context->is_bridge);
		if (score == FIND_CHILD_MAX_SCORE)
			return AE_CTRL_TERMINATE;
		else
			context->ret_score = score;
	}
	score = do_find_child_checks(handle, context->is_bridge);
	if (score == FIND_CHILD_MAX_SCORE) {
		context->ret = handle;
		return AE_CTRL_TERMINATE;
	} else if (score > context->ret_score) {
		context->ret = handle;
		context->ret_score = score;
	}
	return AE_OK;
}

acpi_handle acpi_find_child(acpi_handle parent, u64 addr, bool is_bridge)
{
	if (parent) {
		struct find_child_context context = {
			.addr = addr,
			.is_bridge = is_bridge,
		};

		acpi_walk_namespace(ACPI_TYPE_DEVICE, parent, 1, do_find_child,
				    NULL, &context, NULL);
		return context.ret;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(acpi_find_child);

static void acpi_physnode_link_name(char *buf, unsigned int node_id)
{
	if (node_id > 0)
		snprintf(buf, PHYSICAL_NODE_NAME_SIZE,
			 PHYSICAL_NODE_STRING "%u", node_id);
	else
		strcpy(buf, PHYSICAL_NODE_STRING);
}

int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev = NULL;
	struct acpi_device_physical_node *physical_node, *pn;
	char physical_node_name[PHYSICAL_NODE_NAME_SIZE];
	struct list_head *physnode_list;
	unsigned int node_id;
	int retval = -EINVAL;

	if (ACPI_COMPANION(dev)) {
		if (handle) {
			dev_warn(dev, "ACPI companion already set\n");
			return -EINVAL;
		} else {
			acpi_dev = ACPI_COMPANION(dev);
		}
	} else {
		acpi_bus_get_device(handle, &acpi_dev);
	}
	if (!acpi_dev)
		return -EINVAL;

	get_device(&acpi_dev->dev);
	get_device(dev);
	physical_node = kzalloc(sizeof(*physical_node), GFP_KERNEL);
	if (!physical_node) {
		retval = -ENOMEM;
		goto err;
	}

	mutex_lock(&acpi_dev->physical_node_lock);

	/*
	 * Keep the list sorted by node_id so that the IDs of removed nodes can
	 * be recycled easily.
	 */
	physnode_list = &acpi_dev->physical_node_list;
	node_id = 0;
	list_for_each_entry(pn, &acpi_dev->physical_node_list, node) {
		/* Sanity check. */
		if (pn->dev == dev) {
			mutex_unlock(&acpi_dev->physical_node_lock);

			dev_warn(dev, "Already associated with ACPI node\n");
			kfree(physical_node);
			if (ACPI_COMPANION(dev) != acpi_dev)
				goto err;

			put_device(dev);
			put_device(&acpi_dev->dev);
			return 0;
		}
		if (pn->node_id == node_id) {
			physnode_list = &pn->node;
			node_id++;
		}
	}

	physical_node->node_id = node_id;
	physical_node->dev = dev;
	list_add(&physical_node->node, physnode_list);
	acpi_dev->physical_node_count++;

	if (!ACPI_COMPANION(dev))
		ACPI_COMPANION_SET(dev, acpi_dev);

	acpi_physnode_link_name(physical_node_name, node_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				   physical_node_name);
	if (retval)
		dev_err(&acpi_dev->dev, "Failed to create link %s (%d)\n",
			physical_node_name, retval);

	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				   "firmware_node");
	if (retval)
		dev_err(dev, "Failed to create link firmware_node (%d)\n",
			retval);

	mutex_unlock(&acpi_dev->physical_node_lock);

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;

 err:
	ACPI_COMPANION_SET(dev, NULL);
	put_device(dev);
	put_device(&acpi_dev->dev);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

int acpi_unbind_one(struct device *dev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	struct acpi_device_physical_node *entry;

	if (!acpi_dev)
		return 0;

	mutex_lock(&acpi_dev->physical_node_lock);

	list_for_each_entry(entry, &acpi_dev->physical_node_list, node)
		if (entry->dev == dev) {
			char physnode_name[PHYSICAL_NODE_NAME_SIZE];

			list_del(&entry->node);
			acpi_dev->physical_node_count--;

			acpi_physnode_link_name(physnode_name, entry->node_id);
			sysfs_remove_link(&acpi_dev->dev.kobj, physnode_name);
			sysfs_remove_link(&dev->kobj, "firmware_node");
			ACPI_COMPANION_SET(dev, NULL);
			/* Drop references taken by acpi_bind_one(). */
			put_device(dev);
			put_device(&acpi_dev->dev);
			kfree(entry);
			break;
		}

	mutex_unlock(&acpi_dev->physical_node_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

void acpi_preset_companion(struct device *dev, acpi_handle parent, u64 addr)
{
	struct acpi_device *adev;

	if (!acpi_bus_get_device(acpi_get_child(parent, addr), &adev))
		ACPI_COMPANION_SET(dev, adev);
}
EXPORT_SYMBOL_GPL(acpi_preset_companion);

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type = acpi_get_bus_type(dev);
	acpi_handle handle;
	int ret;

	ret = acpi_bind_one(dev, NULL);
	if (ret && type) {
		ret = type->find_device(dev, &handle);
		if (ret) {
			DBG("Unable to get handle for %s\n", dev_name(dev));
			goto out;
		}
		ret = acpi_bind_one(dev, handle);
		if (ret)
			goto out;
	}

	if (type && type->setup)
		type->setup(dev);

 out:
#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(ACPI_HANDLE(dev), ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev_name(dev), (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev_name(dev));
#endif

	return ret;
}

static int acpi_platform_notify_remove(struct device *dev)
{
	struct acpi_bus_type *type;

	type = acpi_get_bus_type(dev);
	if (type && type->cleanup)
		type->cleanup(dev);

	acpi_unbind_one(dev);
	return 0;
}

int __init init_acpi_device_notify(void)
{
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return 0;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
	return 0;
}
