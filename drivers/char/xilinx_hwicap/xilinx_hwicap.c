/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     Xilinx products are not intended for use in life support appliances,
 *     devices, or systems. Use in such applications is expressly prohibited.
 *
 *     (c) Copyright 2002 Xilinx Inc., Systems Engineering Group
 *     (c) Copyright 2004 Xilinx Inc., Systems Engineering Group
 *     (c) Copyright 2007 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

/*
 * xilinx_hwicap.c
 *
 * This is the code behind /dev/xilinx_icap/'x' -- it allows a user-space
 * application to use the Xilinx ICAP subsystem.
 *
 * A /dev/xilinx_icap/'x' device node represents an arbitrary device
 * on port 'x'.  The following operations are possible:
 *
 * open		do nothing, set up default IEEE 1284 protocol to be COMPAT
 * release	release port and unregister device (if necessary)
 * write        Write a bitstream to the configuration processor.
 * read         Read a data stream from the configuration processor.
 *
 * Note that in order to use the read interface, it is first necessary
 * to write a request packet to the write interface.  i.e., it is not
 * possible to simply readback the bitstream (or any configuration
 * bits) from a device without specifically requesting them first.
 * The code to craft such packets is intended to be part of the
 * user-space application code that uses this device.  The simplest
 * way to use this interface is simply:
 *
 * cp foo.bit /dev/xilinx_icap
 *
 * Note that unless foo.bit is an appropriately constructed partial
 * bitstream, this has a high likelyhood of overwriting the design
 * currently programmed in the FPGA.
 */

#define DEBUG

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#ifdef CONFIG_OF
/* For open firmware. */
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

#include "xilinx_hwicap.h"

#define DRIVER_NAME "xilinx_icap"

#define HWICAP_REGS   (0x10000)

#define HWICAP_MAJOR 254
#define HWICAP_MINOR 0
#define HWICAP_DEVICES 1

/* An array, which is set to true when the device is registered. */
static bool probed_devices[HWICAP_DEVICES];

static struct class *icap_class;

int hwicap_initialize_hwicap(struct hwicap_drvdata *drvdata)
{

	u32 DeviceIdCode;
	u32 Packet;
	int Status;

	dev_dbg(drvdata->dev, "Reset...\n");

	/* Abort any current transaction, to make sure we have the ICAP in */
	/* a good state. */
	XHwIcap_mReset(drvdata->baseAddress);

	/* Attempt to read the IDCODE from ICAP if specified. */
	dev_dbg(drvdata->dev, "Reading IDCODE...\n");
	DeviceIdCode = XHwIcap_GetConfigReg(drvdata, XHI_IDCODE);
	dev_info(drvdata->dev, "Device IDCODE = %x\n", DeviceIdCode);

	/* Mask out the version section of the DeviceIdCode */
	DeviceIdCode = DeviceIdCode & 0x0FFFFFFF;

	dev_dbg(drvdata->dev, "Desync...\n");
	Status = XHwIcap_CommandDesync(drvdata);

	if (Status) {
		return Status;
	}

	/* Abort any current transaction, to make sure we have the ICAP in */
	/* a good state. */
	XHwIcap_mReset(drvdata->baseAddress);

	DeviceIdCode = XHwIcap_GetConfigReg(drvdata, XHI_IDCODE);

	dev_info(drvdata->dev, "Device IDCODE = %x\n", DeviceIdCode);

	return 0;
}

static ssize_t
hwicap_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	ssize_t bytes_to_read = 0;
	u32 *kbuf;
	u32 words;
	u32 bytes_remaining;
	int Status;

	if (drvdata->read_buffer_in_use) {
		/* If there are leftover bytes in the buffer, just */
		/* return them and don't try to read more from the */
		/* ICAP device. */
		bytes_to_read =
		    (count <
		     drvdata->read_buffer_in_use) ? count : drvdata->
		    read_buffer_in_use;

		/* Return the data currently in the read buffer. */
		if (copy_to_user(buf, drvdata->read_buffer, bytes_to_read)) {
			return -EFAULT;
		}
		drvdata->read_buffer_in_use -= bytes_to_read;
		memcpy(drvdata->read_buffer + bytes_to_read,
		       drvdata->read_buffer, 4 - bytes_to_read);
	} else {
		/* Get new data from the ICAP, and return was was requested. */
		kbuf = (u32 *) get_zeroed_page(GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		/* The ICAP device is only able to read complete */
		/* words.  If a number of bytes that do not correspond */
		/* to complete words is requested, then we read enough */
		/* words to get the required number of bytes, and then */
		/* save the remaining bytes for the next read. */

		/* Determine the number of words to read, rounding up */
		/* if necessary. */
		words = ((count + 3) >> 2);
		bytes_to_read = words << 2;

		if (bytes_to_read > PAGE_SIZE) {
			bytes_to_read = PAGE_SIZE;
		}

		/* Ensure we only read a complete number of words. */
		bytes_remaining = bytes_to_read & 3;
		bytes_to_read &= ~3;
		words = bytes_to_read >> 2;

		Status = XHwIcap_GetConfiguration(drvdata, kbuf, words);

		/* If we didn't read correctly, then bail out. */
		if (Status) {
			free_page((unsigned long)kbuf);
			return -EFAULT;
		}

		/* If we fail to return the data to the user, then bail out. */
		if (copy_to_user(buf, kbuf, bytes_to_read)) {
			free_page((unsigned long)kbuf);
			return -EFAULT;
		}
		memcpy(kbuf, drvdata->read_buffer, bytes_remaining);
		drvdata->read_buffer_in_use = bytes_remaining;
		free_page((unsigned long)kbuf);
	}
	return bytes_to_read;
}

static ssize_t hwicap_write(struct file *file, const char *buf,
			     size_t count, loff_t *ppos)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	ssize_t written = 0;
	ssize_t left = count;
	u32 *kbuf;
	int len;
	int Status;

	left += drvdata->write_buffer_in_use;

	/* only write multiples of 4 bytes. */
	if (left < 4)
		return 0;

	kbuf = (u32 *) __get_free_page(GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	while (left > 3) {
		/* only write multiples of 4 bytes, so there might */
		/* be as many as 3 bytes left (at the end). */
		len = left;

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		len &= ~3;

		if (drvdata->write_buffer_in_use) {
			memcpy(kbuf, drvdata->write_buffer,
			       drvdata->write_buffer_in_use);
			if (copy_from_user
			    ((((char *)kbuf) + (drvdata->write_buffer_in_use)),
			     buf + written,
			     len - (drvdata->write_buffer_in_use))) {
				free_page((unsigned long)kbuf);
				return -EFAULT;
			}
		} else {
			if (copy_from_user(kbuf, buf + written, len)) {
				free_page((unsigned long)kbuf);
				return -EFAULT;
			}
		}

		Status = XHwIcap_SetConfiguration(drvdata, kbuf, len >> 2);

		if (Status) {
			free_page((unsigned long)kbuf);
			return -EFAULT;
		}
		if (drvdata->write_buffer_in_use) {
			len -= drvdata->write_buffer_in_use;
			left -= drvdata->write_buffer_in_use;
			drvdata->write_buffer_in_use = 0;
		}
		written += len;
		left -= len;
	}
	if ((left > 0) && (left < 4)) {
		if (!copy_from_user(drvdata->write_buffer, buf + written, left)) {
			drvdata->write_buffer_in_use = left;
			written += left;
			left = 0;
		}
	}

	free_page((unsigned long)kbuf);
	return written;
}

static int hwicap_open(struct inode *inode, struct file *file)
{
	struct hwicap_drvdata *drvdata;
	int status;

	drvdata = container_of(inode->i_cdev, struct hwicap_drvdata, cdev);

	status = hwicap_initialize_hwicap(drvdata);
	if (status) {
		dev_err(drvdata->dev, "Failed to open file");
		return -status;
	}

	drvdata->flags = 0;
	file->private_data = drvdata;
	drvdata->write_buffer_in_use = 0;
	drvdata->read_buffer_in_use = 0;

	return 0;
}

static int hwicap_release(struct inode *inode, struct file *file)
{
	struct hwicap_drvdata *drvdata = file->private_data;
	int i;
	int Status;

	if (drvdata->write_buffer_in_use) {
		/* Flush write buffer. */
		for (i = drvdata->write_buffer_in_use; i < 4; i++) {
			drvdata->write_buffer[i] = 0;
		}
		Status =
		    XHwIcap_SetConfiguration(drvdata,
					     (u32 *) drvdata->write_buffer, 1);
		if (Status) {
			return Status;
		}
	}

	Status = XHwIcap_CommandDesync(drvdata);
	if (Status) {
		return Status;
	}

	return 0;
}

static struct file_operations hwicap_fops = {
	.owner = THIS_MODULE,
	.write = hwicap_write,
	.read = hwicap_read,
	.open = hwicap_open,
	.release = hwicap_release,
};

static int __devinit hwicap_setup(struct device *dev, int id, struct resource *regs_res)
{
	dev_t devt;
	struct hwicap_drvdata *drvdata = NULL;
	int retval = 0;

	dev_info(dev, "Xilinx icap port driver\n");

	if (id < 0) {
		for (id = 0; id < HWICAP_DEVICES; id++)
			if (!probed_devices[id])
				break;
	}
	if (id < 0 || id >= HWICAP_DEVICES) {
		dev_err(dev, "%s%i too large\n", DRIVER_NAME, id);
		return -EINVAL;
	}
	if (probed_devices[id]) {
		dev_err(dev, "cannot assign to %s%i; it is already in use\n",
			DRIVER_NAME, id);
		return -EBUSY;
	}

	probed_devices[id] = 1;

	devt = MKDEV(HWICAP_MAJOR, HWICAP_MINOR + id);

	drvdata = kmalloc(sizeof(struct hwicap_drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "Couldn't allocate device private record\n");
		return -ENOMEM;
	}
	memset((void *)drvdata, 0, sizeof(struct hwicap_drvdata));
	dev_set_drvdata(dev, (void *)drvdata);

	if (!regs_res) {
		dev_err(dev, "Couldn't get registers resource\n");
		retval = -EFAULT;
		goto failed1;
	}

	drvdata->mem_start = regs_res->start;
	drvdata->mem_end = regs_res->end;
	drvdata->mem_size = regs_res->end - regs_res->start + 1;

	if (!request_mem_region(drvdata->mem_start, drvdata->mem_size, DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	drvdata->devt = devt;
	drvdata->dev = dev;
	drvdata->baseAddress = ioremap(drvdata->mem_start, drvdata->mem_size);
	if (!drvdata->baseAddress) {
		dev_err(dev, "ioremap() failed\n");
		goto failed2;
	}

	dev_info(dev, "ioremap %lx to %p with size %x\n",
		 (unsigned long int)drvdata->mem_start,
			drvdata->baseAddress, drvdata->mem_size);

	cdev_init(&drvdata->cdev, &hwicap_fops);
	drvdata->cdev.owner = THIS_MODULE;
	retval = cdev_add(&drvdata->cdev, devt, 1);
	if (retval) {
		dev_err(dev, "cdev_add() failed\n");
		goto failed3;
	}
	/*  devfs_mk_cdev(devt, S_IFCHR|S_IRUGO|S_IWUGO, DRIVER_NAME); */
	class_device_create(icap_class, NULL, devt, NULL, DRIVER_NAME);
	return 0;		/* success */

 failed3:
	iounmap(drvdata->baseAddress);

 failed2:
	release_mem_region(regs_res->start, drvdata->mem_size);

 failed1:
	kfree(drvdata);

	return retval;
}

static int __devexit hwicap_remove(struct device *dev)
{
	struct hwicap_drvdata *drvdata;

	drvdata = (struct hwicap_drvdata *)dev_get_drvdata(dev);

	if(drvdata) {
		class_device_destroy(icap_class, drvdata->devt);
		cdev_del(&drvdata->cdev);
		iounmap(drvdata->baseAddress);
		release_mem_region(drvdata->mem_start, drvdata->mem_size);
		kfree(drvdata);
		dev_set_drvdata(dev, NULL);
		probed_devices[MINOR(dev->devt)-HWICAP_MINOR] = 0;
	}

	return 0;		/* success */
}

static int __devinit hwicap_drv_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

        return hwicap_setup(&pdev->dev, pdev->id, res);
}

static int __devexit hwicap_drv_remove(struct platform_device *pdev)
{
	return hwicap_remove(&pdev->dev);
}

static struct platform_driver hwicap_platform_driver = {
	.probe = hwicap_drv_probe,
	.remove = hwicap_drv_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
};

/* ---------------------------------------------------------------------
 * OF bus binding
 */

#if defined(CONFIG_OF)
static int __devinit
hwicap_of_probe(struct of_device *op, const struct of_device_id *match)
{
	struct resource res;
	const unsigned int *id;
	int rc;

	dev_dbg(&op->dev, "hwicap_of_probe(%p, %p)\n", op, match);

	rc = of_address_to_resource(op->node, 0, &res);
	if (rc) {
		dev_err(&op->dev, "invalid address\n");
		return rc;
	}

	id = of_get_property(op->node, "port-number", NULL);

	return hwicap_setup(&op->dev, id ? *id : -1, &res);
}

static int __devexit hwicap_of_remove(struct of_device *op)
{
	return hwicap_remove(&op->dev);
}

/* Match table for of_platform binding */
static struct of_device_id __devinit hwicap_of_match[] = {
	{ .compatible = "xlnx,opb-hwicap-1.00.b", },
	{},
};
MODULE_DEVICE_TABLE(of, hwicap_of_match);

static struct of_platform_driver hwicap_of_driver = {
	.owner = THIS_MODULE,
	.name = DRIVER_NAME,
	.match_table = hwicap_of_match,
	.probe = hwicap_of_probe,
	.remove = __devexit_p(hwicap_of_remove),
	.driver = {
		.name = DRIVER_NAME,
	},
};

/* Registration helpers to keep the number of #ifdefs to a minimum */
static inline int __devinit hwicap_of_register(void)
{
	pr_debug("hwicap: calling of_register_platform_driver()\n");
	return of_register_platform_driver(&hwicap_of_driver);
}

static inline void __devexit hwicap_of_unregister(void)
{
	of_unregister_platform_driver(&hwicap_of_driver);
}
#else /* CONFIG_OF */
/* CONFIG_OF not enabled; do nothing helpers */
static inline int __devinit hwicap_of_register(void) { return 0; }
static inline void __devexit hwicap_of_unregister(void) { }
#endif /* CONFIG_OF */

static int __devinit hwicap_module_init(void)
{
	dev_t devt;
	int retval;

	icap_class = class_create(THIS_MODULE, "xilinx_config");

	devt = MKDEV(HWICAP_MAJOR, HWICAP_MINOR);
	retval = register_chrdev_region(devt, HWICAP_DEVICES,
						DRIVER_NAME);

	if (retval) return retval;

	retval = platform_driver_register(&hwicap_platform_driver);

	if(retval) goto failed1;

	retval = hwicap_of_register();

	if (retval) goto failed2;

	return retval;

 failed2:
	platform_driver_unregister(&hwicap_platform_driver);

 failed1:
	unregister_chrdev_region(devt, HWICAP_DEVICES);

	return retval;
}

static void __devexit hwicap_module_cleanup(void)
{
	dev_t devt = MKDEV(HWICAP_MAJOR, HWICAP_MINOR);

	class_destroy(icap_class);

	platform_driver_unregister(&hwicap_platform_driver);

	hwicap_of_unregister();

	unregister_chrdev_region(devt, HWICAP_DEVICES);
}

module_init(hwicap_module_init);
module_exit(hwicap_module_cleanup);

/* #ifdef CONFIG_OF */

/* static int __devinit xilinx_hwicap_of_init(void) */
/* { */
/* 	struct device_node *np; */
/* 	unsigned int i; */
/* 	struct platform_device *pdev; */
/* 	int ret; */

/* 	for (np = NULL, i = 0; */
/* 	     (np = of_find_compatible_node(np, NULL, "xlnx,opb-hwicap")) != NULL; */
/* 	     i++) { */
/* 		struct resource r; */

/* 		memset(&r, 0, sizeof(r)); */

/* 		ret = of_address_to_resource(np, 0, &r); */
/* 		if (ret) */
/* 			goto err; */
/* 		pdev = */
/* 		    platform_device_register_simple(DRIVER_NAME, i, &r, 1); */

/* 		if (IS_ERR(pdev)) { */
/* 			ret = PTR_ERR(pdev); */
/* 			goto err; */
/* 		} */
/* 	} */

/* 	return 0; */
/* err: */
/* 	return ret; */
/* } */

/* module_init(xilinx_hwicap_of_init); */

/* #endif */

MODULE_AUTHOR("Xilinx, Inc; Xilinx Research Labs Group");
MODULE_DESCRIPTION("Xilinx ICAP Port Driver");
MODULE_LICENSE("GPL");
