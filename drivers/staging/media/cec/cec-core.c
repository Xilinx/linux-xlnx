/*
 * cec-core.c - HDMI Consumer Electronics Control framework - Core
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "cec-priv.h"

#define CEC_NUM_DEVICES	256
#define CEC_NAME	"cec"

int cec_debug;
module_param_named(debug, cec_debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

static dev_t cec_dev_t;

/* Active devices */
static DEFINE_MUTEX(cec_devnode_lock);
static DECLARE_BITMAP(cec_devnode_nums, CEC_NUM_DEVICES);

static struct dentry *top_cec_dir;

/* dev to cec_devnode */
#define to_cec_devnode(cd) container_of(cd, struct cec_devnode, dev)

int cec_get_device(struct cec_devnode *devnode)
{
	/*
	 * Check if the cec device is available. This needs to be done with
	 * the devnode->lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the devnode->registered check and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&devnode->lock);
	/*
	 * return ENXIO if the cec device has been removed
	 * already or if it is not registered anymore.
	 */
	if (!devnode->registered) {
		mutex_unlock(&devnode->lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&devnode->dev);
	mutex_unlock(&devnode->lock);
	return 0;
}

void cec_put_device(struct cec_devnode *devnode)
{
	put_device(&devnode->dev);
}

/* Called when the last user of the cec device exits. */
static void cec_devnode_release(struct device *cd)
{
	struct cec_devnode *devnode = to_cec_devnode(cd);

	mutex_lock(&cec_devnode_lock);
	/* Mark device node number as free */
	clear_bit(devnode->minor, cec_devnode_nums);
	mutex_unlock(&cec_devnode_lock);

	cec_delete_adapter(to_cec_adapter(devnode));
}

static struct bus_type cec_bus_type = {
	.name = CEC_NAME,
};

/*
 * Register a cec device node
 *
 * The registration code assigns minor numbers and registers the new device node
 * with the kernel. An error is returned if no free minor number can be found,
 * or if the registration of the device node fails.
 *
 * Zero is returned on success.
 *
 * Note that if the cec_devnode_register call fails, the release() callback of
 * the cec_devnode structure is *not* called, so the caller is responsible for
 * freeing any data.
 */
static int __must_check cec_devnode_register(struct cec_devnode *devnode,
					     struct module *owner)
{
	int minor;
	int ret;

	/* Initialization */
	INIT_LIST_HEAD(&devnode->fhs);
	mutex_init(&devnode->lock);

	/* Part 1: Find a free minor number */
	mutex_lock(&cec_devnode_lock);
	minor = find_next_zero_bit(cec_devnode_nums, CEC_NUM_DEVICES, 0);
	if (minor == CEC_NUM_DEVICES) {
		mutex_unlock(&cec_devnode_lock);
		pr_err("could not get a free minor\n");
		return -ENFILE;
	}

	set_bit(minor, cec_devnode_nums);
	mutex_unlock(&cec_devnode_lock);

	devnode->minor = minor;
	devnode->dev.bus = &cec_bus_type;
	devnode->dev.devt = MKDEV(MAJOR(cec_dev_t), minor);
	devnode->dev.release = cec_devnode_release;
	devnode->dev.parent = devnode->parent;
	dev_set_name(&devnode->dev, "cec%d", devnode->minor);
	device_initialize(&devnode->dev);

	/* Part 2: Initialize and register the character device */
	cdev_init(&devnode->cdev, &cec_devnode_fops);
	devnode->cdev.kobj.parent = &devnode->dev.kobj;
	devnode->cdev.owner = owner;

	ret = cdev_add(&devnode->cdev, devnode->dev.devt, 1);
	if (ret < 0) {
		pr_err("%s: cdev_add failed\n", __func__);
		goto clr_bit;
	}

	ret = device_add(&devnode->dev);
	if (ret)
		goto cdev_del;

	devnode->registered = true;
	return 0;

cdev_del:
	cdev_del(&devnode->cdev);
clr_bit:
	mutex_lock(&cec_devnode_lock);
	clear_bit(devnode->minor, cec_devnode_nums);
	mutex_unlock(&cec_devnode_lock);
	return ret;
}

/*
 * Unregister a cec device node
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * This function can safely be called if the device node has never been
 * registered or has already been unregistered.
 */
static void cec_devnode_unregister(struct cec_devnode *devnode)
{
	struct cec_fh *fh;

	mutex_lock(&devnode->lock);

	/* Check if devnode was never registered or already unregistered */
	if (!devnode->registered || devnode->unregistered) {
		mutex_unlock(&devnode->lock);
		return;
	}

	list_for_each_entry(fh, &devnode->fhs, list)
		wake_up_interruptible(&fh->wait);

	devnode->registered = false;
	devnode->unregistered = true;
	mutex_unlock(&devnode->lock);

	device_del(&devnode->dev);
	cdev_del(&devnode->cdev);
	put_device(&devnode->dev);
}

struct cec_adapter *cec_allocate_adapter(const struct cec_adap_ops *ops,
					 void *priv, const char *name, u32 caps,
					 u8 available_las, struct device *parent)
{
	struct cec_adapter *adap;
	int res;

	if (WARN_ON(!parent))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(!caps))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(!ops))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(!available_las || available_las > CEC_MAX_LOG_ADDRS))
		return ERR_PTR(-EINVAL);
	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return ERR_PTR(-ENOMEM);
	adap->owner = parent->driver->owner;
	adap->devnode.parent = parent;
	strlcpy(adap->name, name, sizeof(adap->name));
	adap->phys_addr = CEC_PHYS_ADDR_INVALID;
	adap->log_addrs.cec_version = CEC_OP_CEC_VERSION_2_0;
	adap->log_addrs.vendor_id = CEC_VENDOR_ID_NONE;
	adap->capabilities = caps;
	adap->available_log_addrs = available_las;
	adap->sequence = 0;
	adap->ops = ops;
	adap->priv = priv;
	memset(adap->phys_addrs, 0xff, sizeof(adap->phys_addrs));
	mutex_init(&adap->lock);
	INIT_LIST_HEAD(&adap->transmit_queue);
	INIT_LIST_HEAD(&adap->wait_queue);
	init_waitqueue_head(&adap->kthread_waitq);

	adap->kthread = kthread_run(cec_thread_func, adap, "cec-%s", name);
	if (IS_ERR(adap->kthread)) {
		pr_err("cec-%s: kernel_thread() failed\n", name);
		res = PTR_ERR(adap->kthread);
		kfree(adap);
		return ERR_PTR(res);
	}

	if (!(caps & CEC_CAP_RC))
		return adap;

#if IS_REACHABLE(CONFIG_RC_CORE)
	/* Prepare the RC input device */
	adap->rc = rc_allocate_device();
	if (!adap->rc) {
		pr_err("cec-%s: failed to allocate memory for rc_dev\n",
		       name);
		kthread_stop(adap->kthread);
		kfree(adap);
		return ERR_PTR(-ENOMEM);
	}

	snprintf(adap->input_name, sizeof(adap->input_name),
		 "RC for %s", name);
	snprintf(adap->input_phys, sizeof(adap->input_phys),
		 "%s/input0", name);

	adap->rc->input_name = adap->input_name;
	adap->rc->input_phys = adap->input_phys;
	adap->rc->input_id.bustype = BUS_CEC;
	adap->rc->input_id.vendor = 0;
	adap->rc->input_id.product = 0;
	adap->rc->input_id.version = 1;
	adap->rc->dev.parent = parent;
	adap->rc->driver_type = RC_DRIVER_SCANCODE;
	adap->rc->driver_name = CEC_NAME;
	adap->rc->allowed_protocols = RC_BIT_CEC;
	adap->rc->priv = adap;
	adap->rc->map_name = RC_MAP_CEC;
	adap->rc->timeout = MS_TO_NS(100);
#else
	adap->capabilities &= ~CEC_CAP_RC;
#endif
	return adap;
}
EXPORT_SYMBOL_GPL(cec_allocate_adapter);

int cec_register_adapter(struct cec_adapter *adap)
{
	int res;

	if (IS_ERR_OR_NULL(adap))
		return 0;

#if IS_REACHABLE(CONFIG_RC_CORE)
	if (adap->capabilities & CEC_CAP_RC) {
		res = rc_register_device(adap->rc);

		if (res) {
			pr_err("cec-%s: failed to prepare input device\n",
			       adap->name);
			rc_free_device(adap->rc);
			adap->rc = NULL;
			return res;
		}
	}
#endif

	res = cec_devnode_register(&adap->devnode, adap->owner);
	if (res) {
#if IS_REACHABLE(CONFIG_RC_CORE)
		/* Note: rc_unregister also calls rc_free */
		rc_unregister_device(adap->rc);
		adap->rc = NULL;
#endif
		return res;
	}

	dev_set_drvdata(&adap->devnode.dev, adap);
#ifdef CONFIG_MEDIA_CEC_DEBUG
	if (!top_cec_dir)
		return 0;

	adap->cec_dir = debugfs_create_dir(dev_name(&adap->devnode.dev), top_cec_dir);
	if (IS_ERR_OR_NULL(adap->cec_dir)) {
		pr_warn("cec-%s: Failed to create debugfs dir\n", adap->name);
		return 0;
	}
	adap->status_file = debugfs_create_devm_seqfile(&adap->devnode.dev,
		"status", adap->cec_dir, cec_adap_status);
	if (IS_ERR_OR_NULL(adap->status_file)) {
		pr_warn("cec-%s: Failed to create status file\n", adap->name);
		debugfs_remove_recursive(adap->cec_dir);
		adap->cec_dir = NULL;
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(cec_register_adapter);

void cec_unregister_adapter(struct cec_adapter *adap)
{
	if (IS_ERR_OR_NULL(adap))
		return;

#if IS_REACHABLE(CONFIG_RC_CORE)
	/* Note: rc_unregister also calls rc_free */
	rc_unregister_device(adap->rc);
	adap->rc = NULL;
#endif
	debugfs_remove_recursive(adap->cec_dir);
	cec_devnode_unregister(&adap->devnode);
}
EXPORT_SYMBOL_GPL(cec_unregister_adapter);

void cec_delete_adapter(struct cec_adapter *adap)
{
	if (IS_ERR_OR_NULL(adap))
		return;
	mutex_lock(&adap->lock);
	__cec_s_phys_addr(adap, CEC_PHYS_ADDR_INVALID, false);
	mutex_unlock(&adap->lock);
	kthread_stop(adap->kthread);
	if (adap->kthread_config)
		kthread_stop(adap->kthread_config);
#if IS_REACHABLE(CONFIG_RC_CORE)
	rc_free_device(adap->rc);
#endif
	kfree(adap);
}
EXPORT_SYMBOL_GPL(cec_delete_adapter);

/*
 *	Initialise cec for linux
 */
static int __init cec_devnode_init(void)
{
	int ret;

	pr_info("Linux cec interface: v0.10\n");
	ret = alloc_chrdev_region(&cec_dev_t, 0, CEC_NUM_DEVICES,
				  CEC_NAME);
	if (ret < 0) {
		pr_warn("cec: unable to allocate major\n");
		return ret;
	}

#ifdef CONFIG_MEDIA_CEC_DEBUG
	top_cec_dir = debugfs_create_dir("cec", NULL);
	if (IS_ERR_OR_NULL(top_cec_dir)) {
		pr_warn("cec: Failed to create debugfs cec dir\n");
		top_cec_dir = NULL;
	}
#endif

	ret = bus_register(&cec_bus_type);
	if (ret < 0) {
		unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
		pr_warn("cec: bus_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit cec_devnode_exit(void)
{
	debugfs_remove_recursive(top_cec_dir);
	bus_unregister(&cec_bus_type);
	unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
}

subsys_initcall(cec_devnode_init);
module_exit(cec_devnode_exit)

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_DESCRIPTION("Device node registration for cec drivers");
MODULE_LICENSE("GPL");
