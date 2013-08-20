/*
 * Remote processor messaging transport - sample server driver
 *
 * Copyright (C) 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012 PetaLogix
 *
 * Based on original OMX driver made by:
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/scatterlist.h>
#include <linux/idr.h>
#include <linux/poll.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/skbuff.h>

struct rpmsg_service {
	struct cdev cdev;
	struct rpmsg_channel *rpdev;
	struct device *dev;
	int major;
	int minor;
};

struct rpmsg_instance {
	struct rpmsg_endpoint *ept;
	struct rpmsg_service *service;
	struct sk_buff_head queue;
	struct mutex lock;
	wait_queue_head_t readq;
};

static void rpmsg_cb(struct rpmsg_channel *rpdev, void *data, int len,
						void *priv, u32 src)
{
	static int rx_count;
	struct sk_buff *skb;
	char *skbdata;
	/* priv is setup only for new endpoints */
	struct rpmsg_instance *instance = priv;

	dev_dbg(&rpdev->dev, "incoming msg %d (src: 0x%x) len %d\n",
						++rx_count, src, len);

	print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 32, 1,
		       data, len,  true);

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb) {
		dev_err(&rpdev->dev, "alloc_skb err: %u\n", len);
		return;
	}

	skbdata = skb_put(skb, len);
	memcpy(skbdata, data, len);
	mutex_lock(&instance->lock);
	skb_queue_tail(&instance->queue, skb);
	mutex_unlock(&instance->lock);
	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&instance->readq);
}

static ssize_t rpmsg_read(struct file *filp, char __user *buf,
						size_t len, loff_t *offp)
{
	struct rpmsg_instance *instance = filp->private_data;
	struct sk_buff *skb;
	int use;

	if (mutex_lock_interruptible(&instance->lock))
		return -ERESTARTSYS;

	/* nothing to read ? */
	if (skb_queue_empty(&instance->queue)) {
		mutex_unlock(&instance->lock);
		/* non-blocking requested ? return now */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		/* otherwise block, and wait for data */
		if (wait_event_interruptible(instance->readq,
				!skb_queue_empty(&instance->queue)))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&instance->lock))
			return -ERESTARTSYS;
	}

	skb = skb_dequeue(&instance->queue);
	if (!skb) {
		printk("err is rmpsg_omx racy ?\n");
		return -EFAULT;
	}

	mutex_unlock(&instance->lock);

	use = min(len, skb->len);
	if (copy_to_user(buf, skb->data, use))
		use = -EFAULT;

	kfree_skb(skb);
	return use;
}

/* MAX number of bytes in one packet - depend on resource table */
#define CHANNEL_SIZE	512

static ssize_t rpmsg_write(struct file *filp, const char __user *ubuf,
						size_t len, loff_t *offp)
{
	struct rpmsg_instance *instance = filp->private_data;
	struct rpmsg_service *service = instance->service;
	int err;
	char kbuf[CHANNEL_SIZE];

	len = len < CHANNEL_SIZE ? len : CHANNEL_SIZE;
	if (copy_from_user(kbuf, ubuf, len))
		return -EMSGSIZE;

	err = rpmsg_send_offchannel(service->rpdev, instance->ept->addr,
					service->rpdev->dst, kbuf, len);
	if (err) {
		dev_err(service->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	return len;
}

static int rpmsg_open(struct inode *inode, struct file *filp)
{
	struct rpmsg_instance *instance;
	struct rpmsg_service *service;

	service = container_of(inode->i_cdev, struct rpmsg_service, cdev);

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	instance->service = service;

	mutex_init(&instance->lock);
	skb_queue_head_init(&instance->queue);
	init_waitqueue_head(&instance->readq);

	filp->private_data = instance;

	/* assign a new, unique, local address and associate omx with it */
	instance->ept = rpmsg_create_ept(service->rpdev, rpmsg_cb, instance,
							RPMSG_ADDR_ANY);
	if (!instance->ept) {
		dev_err(service->dev, "create ept failed\n");
		kfree(instance);
		return -ENOMEM;
	}

	dev_dbg(service->dev, "New endpoint at %d\n", instance->ept->addr);
	return 0;
}

typedef enum {
	CLEAR = 0,
	START,
	STOP,
	CLONE,
	GET,
	QUIT,
	STATE_MASK = 0xF,
} message_state;

static int rpmsg_release(struct inode *inode, struct file *filp)
{
	int err, temp;
	struct rpmsg_instance *instance = filp->private_data;
	struct rpmsg_service *service = instance->service;

	/* Place for sending any STOP message or announce driver
	 * that connection from user-space is lost */
	temp = QUIT;
	err = rpmsg_send_offchannel(service->rpdev, instance->ept->addr,
				service->rpdev->dst, &temp, sizeof(temp));
	if (err) {
		dev_err(service->dev, "rpmsg_send failed: %d\n", err);
		/* No return here because driver wants to quit */
	}

	/* Discard all SKBs */
	while (!skb_queue_empty(&instance->queue)) {
		struct sk_buff *skb;
		skb = skb_dequeue(&instance->queue);
		kfree_skb(skb);
	}

	rpmsg_destroy_ept(instance->ept); /* Also endpoint */
	kfree(instance);
	return 0;
}


static const struct file_operations rpmsg_fops = {
	.open		= rpmsg_open,
	.release	= rpmsg_release,
	.read		= rpmsg_read,
	.write		= rpmsg_write,
	.owner		= THIS_MODULE,
};

static struct class *rpmsg_class;
static dev_t rpmsg_dev;
static u32 minor;

static int rpmsg_sample_probe(struct rpmsg_channel *rpdev)
{
	int ret;
	struct rpmsg_service *service;

	service = kzalloc(sizeof(*service), GFP_KERNEL);
	if (!service) {
		dev_err(&rpdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	service->rpdev = rpdev;
	service->major = MAJOR(rpmsg_dev);
	service->minor = minor++;

	cdev_init(&service->cdev, &rpmsg_fops);
	service->cdev.owner = THIS_MODULE;
	ret = cdev_add(&service->cdev,
				MKDEV(service->major, service->minor), 1);
	if (ret) {
		dev_err(&rpdev->dev, "cdev_add failed: %d\n", ret);
		goto free;
	}

	service->dev = device_create(rpmsg_class, &rpdev->dev,
			MKDEV(service->major, service->minor), NULL,
			"rpmsg%d", service->minor);
	if (IS_ERR(service->dev)) {
		dev_err(&rpdev->dev, "device_create failed: %d\n", ret);
		goto clean_cdev;
	}
	dev_set_drvdata(&rpdev->dev, service);

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
					rpdev->src, rpdev->dst);
	return 0;

clean_cdev:
	cdev_del(&service->cdev);
free:
	kfree(service);
	return ret;
}

static void rpmsg_sample_remove(struct rpmsg_channel *rpdev)
{
	struct rpmsg_service *service = dev_get_drvdata(&rpdev->dev);
	int major = MAJOR(rpmsg_dev);

	device_destroy(rpmsg_class, MKDEV(major, service->minor));
	cdev_del(&service->cdev);
	kfree(service);
}

static void rpmsg_sample_cb(struct rpmsg_channel *rpdev, void *data,
					int len, void *priv, u32 src)
{
	dev_info(&rpdev->dev, "ORIGIN callback function without priv\n");
}

static struct rpmsg_device_id rpmsg_driver_sample_id_table[] = {
	{ .name	= "rpmsg-timer-statistic" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_sample_id_table);

static struct rpmsg_driver rpmsg_sample_server = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_sample_id_table,
	.probe		= rpmsg_sample_probe,
	.callback	= rpmsg_sample_cb,
	.remove		= rpmsg_sample_remove,
};

#define MAX_DEVICES	8

static int __init init(void)
{
	int ret;

	/* Allocate 0-8 char devices */
	ret = alloc_chrdev_region(&rpmsg_dev, 0, MAX_DEVICES,
						KBUILD_MODNAME);
	if (ret) {
		pr_err("alloc_chrdev_region failed: %d\n", ret);
		goto out;
	}

	/* Create class for this device */
	rpmsg_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(rpmsg_class)) {
		ret = PTR_ERR(rpmsg_class);
		pr_err("class_create failed: %d\n", ret);
		goto unreg_region;
	}

	return register_rpmsg_driver(&rpmsg_sample_server);

unreg_region:
	unregister_chrdev_region(rpmsg_dev, MAX_DEVICES);
out:
	return ret;
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rpmsg_sample_server);
	class_destroy(rpmsg_class);
	unregister_chrdev_region(rpmsg_dev, MAX_DEVICES);
}
module_init(init);
module_exit(fini);

MODULE_DESCRIPTION("Virtio remote processor messaging sample driver");
MODULE_AUTHOR("Michal Simek <monstr@monstr.eu");
MODULE_LICENSE("GPL v2");
