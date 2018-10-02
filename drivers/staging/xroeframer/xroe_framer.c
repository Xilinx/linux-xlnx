// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "xroe_framer.h"

#define DRIVER_NAME "framer"
/* IOCTL commands */
/* Use 0xF5 as magic number */
#define XROE_FRAMER_MAGIC_NUMBER	0xF5
#define XROE_FRAMER_IOSET		_IOW(XROE_FRAMER_MAGIC_NUMBER, 0, u32)
#define XROE_FRAMER_IOGET		_IOR(XROE_FRAMER_MAGIC_NUMBER, 1, u32)

static dev_t first, second, third;
static struct cdev c_dev, stats_dev, radio_ctrl_dev;
static struct class *cl;
/*
 * TODO: to be made static as well, so that multiple instances can be used. As
 * of now, the "lp" structure is shared among the multiple source files
 */
struct framer_local *lp;
static void __iomem *radio_ctrl;
static struct platform_driver framer_driver;
/* TODO: to be removed from the header file and resort that main file
 * not to need them. Also swap const and static.
 */
const static struct file_operations framer_fops;
const static struct file_operations stats_ops;
const static struct file_operations radio_ctrl_fops;
/*
 * TODO: placeholder for the IRQ once it's been implemented
 * in the framer block
 */
static irqreturn_t framer_irq(int irq, void *lp)
{
	return IRQ_HANDLED;
}

/**
 * framer_probe - Probes the device tree to locate the framer block
 * @pdev:	The structure containing the device's details
 *
 * Probes the device tree to locate the framer block and maps it to
 * the kernel virtual memory space
 *
 * Return: 0 on success or a negative errno on error.
 */
static int framer_probe(struct platform_device *pdev)
{
	struct resource *r_mem; /* IO mem resources */
	struct resource *r_irq;
	struct device *dev = &pdev->dev;
	struct device *parent_xroe_device = NULL;
	struct device *stats_device = NULL;
	struct device *radio_ctrl_device = NULL;
	int rc = 0;

	dev_dbg(dev, "Device Tree Probing\n");
	/* Get iospace for the device */
	/*
	 * TODO: Use platform_get_resource_byname() instead when the DT entry
	 * of the framer block has been finalised (when framer gets out of
	 * the development stage).
	 */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}

	lp = devm_kzalloc(&pdev->dev, sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	dev_set_drvdata(dev, lp);
	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;

	if (!devm_request_mem_region(dev, lp->mem_start,
				     lp->mem_end - lp->mem_start + 1,
				     DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)lp->mem_start);
		return -EBUSY;
	}

	lp->base_addr = devm_ioremap(dev,
				     lp->mem_start,
				     lp->mem_end - lp->mem_start + 1);
	if (!lp->base_addr) {
		dev_err(dev, "framer: Could not allocate iomem\n");
		return -EIO;
	}
	rc = alloc_chrdev_region(&first, 0, 1, "xroe");
	if (rc < 0) {
		pr_err("Allocating XROE framer failed\n");
		return rc;
	}
	cl = class_create(THIS_MODULE, "chardrv");
	if (IS_ERR(cl)) {
		pr_err("Class create failed\n");
		unregister_chrdev_region(first, 1);
		return PTR_ERR(cl);
	}
	parent_xroe_device = device_create(cl, NULL, first, NULL, "xroe!ip");
	if (IS_ERR(parent_xroe_device)) {
		pr_err("Device create failed\n");
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return PTR_ERR(parent_xroe_device);
	}
	cdev_init(&c_dev, &framer_fops);
	rc = cdev_add(&c_dev, first, 1);
	if (rc < 0) {
		pr_err("Device add failed\n");
		device_destroy(cl, first);
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return rc;
	}

	/* Register "/dev/xroefram/stats" device */
	rc = alloc_chrdev_region(&second, 0, 1, "xroe");
	if (rc < 0) {
		pr_err("Allocating xroe stats failed\n");
		return rc;
	}
	stats_device = device_create(cl, parent_xroe_device, second, NULL,
				     "xroe!stats");
	if (IS_ERR(stats_device)) {
		pr_err("Stats device create failed\n");
		class_destroy(cl);
		unregister_chrdev_region(second, 1);
		return PTR_ERR(stats_device);
	}
	cdev_init(&stats_dev, &stats_ops);
	rc = cdev_add(&stats_dev, second, 1);
	if (rc < 0) {
		pr_err("Stats device add failed\n");
		device_destroy(cl, second);
		class_destroy(cl);
		unregister_chrdev_region(second, 1);
		return rc;
	}
	/* Register "/dev/xroefram/radio_ctrl" device */
	/*
	 * TODO: Remove hardcoded address & size and read them from DT
	 * once the radio_ctrl device has been properly implemented in the DT
	 */
	radio_ctrl = ioremap(RADIO_CTRL_BASE, RADIO_CTRL_SIZE);
	if (IS_ERR(radio_ctrl)) {
		pr_err("Mapping Radio Control failed\n");
		return PTR_ERR(radio_ctrl);
	}
	rc = alloc_chrdev_region(&third, 0, 1, "xroe");
	if (rc < 0) {
		pr_err("Allocating xroe radio_ctrl failed\n");
		return rc;
	}
	radio_ctrl_device = device_create(cl, parent_xroe_device, third,
					  NULL, "xroe!radio_ctrl");
	if (IS_ERR(radio_ctrl_device)) {
		pr_err("radio_ctrl device create failed\n");
		class_destroy(cl);
		unregister_chrdev_region(third, 1);
		return PTR_ERR(radio_ctrl_device);
	}
	cdev_init(&radio_ctrl_dev, &radio_ctrl_fops);
	rc = cdev_add(&radio_ctrl_dev, third, 1);
	if (rc < 0) {
		pr_err("radio_ctrl device add failed\n");
		device_destroy(cl, third);
		class_destroy(cl);
		unregister_chrdev_region(third, 1);
		return rc;
	}
	xroe_sysfs_init();
	/* Get IRQ for the device */
	/*
	 * TODO: No IRQ *yet* in the DT from the framer block, as it's still
	 * under development. To be added once it's in the block, and also
	 * replace with platform_get_irq_byname()
	 */
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (IS_ERR(r_irq)) {
		dev_info(dev, "no IRQ found\n");
		/*
		 * TODO: Return non-zero (error) code on no IRQ found.
		 * To be implemented once the IRQ is in the block
		 */
		return 0;
	}
	rc = devm_request_irq(dev, lp->irq, &framer_irq, 0, DRIVER_NAME, lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			lp->irq);

		return rc;
	}

	return rc;
}

/**
 * framer_read - Reads from the framer block and copies to user
 * @f:		The file opened by the user
 * @len:	The number of bytes to be read
 * @off:	The offset from the framer's base address
 * @buf:	The buffer containing the bytes read from the framer
 *
 * Returns a byte-by-byte read from the framer block for the provided
 * length and offset
 *
 * Return: The number of bytes read on success, 0 if the input offset
 * is off memory limits or -EFAULT if the copy_to_user() fails
 */
static ssize_t framer_read(struct file *f,
			   char __user *buf, size_t len, loff_t *off)
{
	int i;
	u8 byte;
	size_t framer_size = (size_t)(lp->mem_end - lp->mem_start);

	if (*off >= framer_size)
		return 0;
	if (*off + len > framer_size)
		len = framer_size - *off;

	for (i = 0; i < len; i++) {
		byte = ioread8((u8 *)lp->base_addr + *off + i);
		if (copy_to_user(buf + i, &byte, 1))
			return -EFAULT;
	}
	*off += len;

	return len;
}

/**
 * framer_write - Copies from the user and writes to the framer block
 * @f:		The file opened by the user
 * @len:	The number of bytes to be read
 * @off:	The offset from the framer's base address
 * @buf:	The buffer containing the bytes to be written to the framer
 *
 * Returns a byte-by-byte read from the framer block for the provided
 * length and offset
 *
 * Return: The number of bytes read on success, 0 if the input offset
 * is off memory limits or -EFAULT if the copy_from_user() fails
 */
static ssize_t framer_write(struct file *f, const char __user *buf,
			    size_t len, loff_t *off)
{
	int i;
	u8 byte;
	size_t framer_size = (size_t)(lp->mem_end - (lp->mem_start));

	if (*off >= framer_size)
		return 0;
	if (*off + len > framer_size)
		len = framer_size - *off;
	for (i = 0; i < len; i++) {
		if (copy_from_user(&byte, buf + i, 1))
			return -EFAULT;

		iowrite8(byte, (u8 *)lp->base_addr + *off + i);
	}
	*off += len;

	return len;
}

/**
 * framer_ioctl - Provides ioctl access to the XROE framer
 * @f:				The file opened by the user
 * @cmd:			The ioctl command passed from the user
 * @ioctl_param:	The parameter(s) passed from the user.
 *					Here, a structure of 2 uint32_t
 *					pointers to offset from the base addess
 *					and a value
 *
 * Copies two pointers from the user, pointing to an address offset and a value.
 * The command passed also from the user is switched, and after performing a
 * range check on the offset, the value is either being read or written
 *
 * Return: 0 on success or a negative errno on error.
 */
static long framer_ioctl(struct file *f, unsigned int cmd,
			 unsigned long ioctl_param)
{
	struct ioctl_arguments *args = kmalloc(sizeof(*args), GFP_KERNEL);
	int ret = 0;
	u32 offset;
	size_t framer_size = (size_t)(lp->mem_end - (lp->mem_start));

	switch (cmd) {
	case XROE_FRAMER_IOSET: /* Write */
		if (copy_from_user(args, (void *)ioctl_param,
				   sizeof(struct ioctl_arguments))) {
			ret = -EFAULT;
			break;
		}
		offset = *args->offset;
		ret = utils_check_address_offset(offset, framer_size);
		if (ret)
			break;

		iowrite32(*args->value, (u8 *)lp->base_addr + (loff_t)offset);
		break;

	case XROE_FRAMER_IOGET: /* Read */
		if (copy_from_user(args, (void *)ioctl_param,
				   sizeof(struct ioctl_arguments))) {
			ret = EFAULT;
			break;
		}
		offset = *args->offset;
		ret = utils_check_address_offset(offset, framer_size);
		if (ret)
			break;

		*args->value = ioread32((u8 *)lp->base_addr + (loff_t)offset);
		break;

	default:
		ret = -EPERM; /* Operation not permitted */
		break;
	}

	kfree(args);
	return ret;
}

/**
 * stats_read - Reads from the stats block and copies to user
 * @f:		The file opened by the user
 * @len:	The number of bytes to be read
 * @off:	The offset from the stats' base address
 * @buf:	The buffer containing the bytes read from the stats
 *
 * Returns a byte-by-byte read from the stats block for the provided
 * length and offset
 *
 * Return: The number of bytes read on success, 0 if the input offset
 * is off memory limits or -EFAULT if the copy_to_user() fails
 */
static ssize_t stats_read(struct file *f, char __user *buf, size_t len,
			  loff_t *off)
{
	int i;
	u8 byte;

	if (*off >= STATS_SIZE)
		return 0;
	if (*off + len > STATS_SIZE)
		len = STATS_SIZE - *off;

	for (i = 0; i < len; i++) {
		byte = ioread8((u8 *)lp->base_addr + *off + i + STATS_BASE);
		if (copy_to_user(buf + i, &byte, 1))
			return -EFAULT;
	}
	*off += len;

	return len;
}

/**
 * stats_ioctl - Provides ioctl access to the XROE stats
 * @f:				The file opened by the user
 * @cmd:			The ioctl command passed from the user
 * @ioctl_param:	The parameter(s) passed from the user.
 *					Here, a structure of 2 uint32_t pointers
 *					to offset from the base addess
 *					and a value
 *
 * Copies two pointers from the user, pointing to an address offset and a value.
 * The command passed also from the user is switched, and after performing a
 * range check on the offset, the value is either being read or written
 *
 * Return: 0 on success or a negative errno on error.
 */
static long stats_ioctl(struct file *f, unsigned int cmd,
			unsigned long ioctl_param)
{
	struct ioctl_arguments *args = kmalloc(sizeof(*args), GFP_KERNEL);
	int ret = 0;
	u32 offset;
	size_t stats_size = STATS_SIZE;

	switch (cmd) {
	case XROE_FRAMER_IOGET: /* Read */
		if (copy_from_user(args, (void *)ioctl_param,
				   sizeof(struct ioctl_arguments))) {
			ret = -EFAULT;
			break;
		}
		offset = *args->offset - STATS_BASE;
		ret = utils_check_address_offset(offset, stats_size);
		if (ret)
			break;
		*args->value = ioread32((u8 *)lp->base_addr + (loff_t)offset +
		STATS_BASE);
		break;
	case XROE_FRAMER_IOSET: /* Write - not permitted on the stats device */
	default:
		ret = -EPERM; /* Operation not permitted */
		break;
	}

	kfree(args);
	return ret;
}

/**
 * radio_ctrl_read - Reads from the radio control block and copies to user
 * @f:		The file opened by the user
 * @len:	The number of bytes to be read
 * @off:	The offset from the radio control's base address
 * @buf:	The buffer containing the bytes read from
 *			the radio control device
 *
 * Returns a byte-by-byte read from the radio control device block
 * for the provided length and offset
 *
 * Return: The number of bytes read on success, 0 if the input offset
 * is off memory limits or -EFAULT if the copy_to_user() fails
 */
static ssize_t radio_ctrl_read(struct file *f, char __user *buf, size_t len,
			       loff_t *off)
{
	int i;
	u8 byte;

	if (*off >= RADIO_CTRL_SIZE)
		return 0;
	if (*off + len > RADIO_CTRL_SIZE)
		len = RADIO_CTRL_SIZE - *off;

	for (i = 0; i < len; i++) {
		byte = ioread8((u8 *)radio_ctrl + *off + i);
		if (copy_to_user(buf + i, &byte, 1))
			return -EFAULT;
	}
	*off += len;

	return len;
}

/**
 * radio_ctrl_read - Copies from the user and writes to the radio control
 * @f:		The file opened by the user
 * @len:	The number of bytes to be read
 * @off:	The offset from the radio control's base address
 * @buf:	The buffer containing the bytes read
 *			from the radio control device
 *
 * Performs a byte-by-byte write to the radio control device for the
 * provided length and offset
 *
 * Return: The number of bytes read on success, 0 if the input offset
 * is off memory limits or -EFAULT if the copy_from_user() fails
 */
static ssize_t radio_ctrl_write(struct file *f, const char __user *buf,
				size_t len, loff_t *off)
{
	int i;
	u8 byte;

	if (*off >= RADIO_CTRL_SIZE)
		return 0;

	if (*off + len > RADIO_CTRL_SIZE)
		len = RADIO_CTRL_SIZE - *off;

	for (i = 0; i < len; i++) {
		if (copy_from_user(&byte, buf + i, 1))
			return -EFAULT;
		iowrite8(byte, (u8 *)radio_ctrl + *off + i);
	}
	*off += len;

	return len;
}

/**
 * utils_check_address_offset - Offset range check
 * @offset:			The address offset to be checked
 * @device_size:	The device's address range
 *
 * Checks a given offset against the given device's range and then checks
 * if it's even or not
 *
 * Return: 0 on success or a negative errno on error.
 */
int utils_check_address_offset(u32 offset, size_t device_size)
{
	if (offset >= device_size)
		return -ENXIO; /* No such device or address */
	else if (offset % 2 != 0)
		return -EINVAL; /* Invalid argument */

	return 0;
}

/**
 * framer_init - Registers the driver
 *
 * Return: 0 on success, -1 on allocation error
 *
 * Registers the framer driver and creates character device drivers
 * for the whole block, as well as separate ones for stats and
 * radio control.
 */
static int __init framer_init(void)
{
	int ret;

	pr_debug("XROE framer driver init\n");

	ret = platform_driver_register(&framer_driver);

	return ret;
}

/* TODO: Fix kernel-doc warning for the documentation of this function */
/**
 * framer_exit - Destroys the driver
 *
 * Unregisters the framer driver and destroys the character
 * device driver for the whole block, as well as the separate ones
 * for stats and radio control. Returns 0 upon successful execution
 */
static void __exit framer_exit(void)
{
	xroe_sysfs_exit();
	cdev_del(&radio_ctrl_dev);
	device_destroy(cl, third);
	cdev_del(&stats_dev);
	device_destroy(cl, second);
	cdev_del(&c_dev);
	device_destroy(cl, first);
	class_destroy(cl);
	unregister_chrdev_region(third, 1);
	unregister_chrdev_region(second, 1);
	unregister_chrdev_region(first, 2);
	iounmap(radio_ctrl);
	platform_driver_unregister(&framer_driver);
	pr_info("XROE Framer exit\n");
}

module_init(framer_init);
module_exit(framer_exit);

/* TODO: Document DT binding */
const static struct of_device_id framer_of_match[] = {
	{ .compatible = "xlnx,roe-framer-1.0", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, framer_of_match);

static struct platform_driver framer_driver = {
	.driver = {
		/*
		 * TODO: .name shouldn't be necessary, though removing
		 * it results in kernel panic. To investigate further
		 */
		.name = DRIVER_NAME,
		.of_match_table = framer_of_match,
	},
	.probe = framer_probe,
};

const static struct file_operations framer_fops = {
	.owner = THIS_MODULE,
	.read = framer_read,
	.write = framer_write,
	.unlocked_ioctl = framer_ioctl
};

const static struct file_operations stats_ops = {
	.owner = THIS_MODULE,
	.read = stats_read,
	.unlocked_ioctl = stats_ioctl
};

const static struct file_operations radio_ctrl_fops = {
	.owner = THIS_MODULE,
	.read = radio_ctrl_read,
	.write = radio_ctrl_write
};

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("framer - Xilinx Radio over Ethernet Framer driver");
