/*
 * Xilinx XLNK Engine Driver
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 *
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uio_driver.h>


#include "xlnk-eng.h"

static DEFINE_MUTEX(xlnk_eng_list_mutex);
static LIST_HEAD(xlnk_eng_list);

int xlnk_eng_register_device(struct xlnk_eng_device *xlnk_dev)
{
	mutex_lock(&xlnk_eng_list_mutex);
	/* todo: need to add more error checking */

	list_add_tail(&xlnk_dev->global_node, &xlnk_eng_list);

	mutex_unlock(&xlnk_eng_list_mutex);

	return 0;
}
EXPORT_SYMBOL(xlnk_eng_register_device);


void xlnk_eng_unregister_device(struct xlnk_eng_device *xlnk_dev)
{
	mutex_lock(&xlnk_eng_list_mutex);
	/* todo: need to add more error checking */

	list_del(&xlnk_dev->global_node);

	mutex_unlock(&xlnk_eng_list_mutex);
}
EXPORT_SYMBOL(xlnk_eng_unregister_device);

struct xlnk_eng_device *xlnk_eng_request_by_name(char *name)
{
	struct xlnk_eng_device *device, *_d;
	int found = 0;

	mutex_lock(&xlnk_eng_list_mutex);

	list_for_each_entry_safe(device, _d, &xlnk_eng_list, global_node) {
		if (!strcmp(dev_name(device->dev), name)) {
			found = 1;
			break;
		}
	}
	if (found)
		device = device->alloc(device);
	else
		device = NULL;

	mutex_unlock(&xlnk_eng_list_mutex);

	return device;
}
EXPORT_SYMBOL(xlnk_eng_request_by_name);

void xlnk_eng_release(struct xlnk_eng_device *xlnk_dev)
{
	if (!xlnk_dev)
		return;

	xlnk_dev->free(xlnk_dev);
}
EXPORT_SYMBOL(xlnk_eng_release);

#define DRIVER_NAME "xilinx-xlnk-eng"

struct xilinx_xlnk_eng_device {
	struct xlnk_eng_device common;
	void __iomem *base;
	spinlock_t lock;
	int cnt;
};


#define to_xilinx_xlnk(dev)	container_of(dev, \
					struct xilinx_xlnk_eng_device, common)

static struct xlnk_eng_device *xilinx_xlnk_alloc(
					struct xlnk_eng_device *xlnkdev)
{
	struct xilinx_xlnk_eng_device *xdev;
	struct xlnk_eng_device *retdev;

	xdev = to_xilinx_xlnk(xlnkdev);

	if (xdev->cnt == 0) {
		xdev->cnt++;
		retdev = xlnkdev;
	} else
		retdev = NULL;

	return retdev;
}

static void xilinx_xlnk_free(struct xlnk_eng_device *xlnkdev)
{
	struct xilinx_xlnk_eng_device *xdev;

	xdev = to_xilinx_xlnk(xlnkdev);

	xdev->cnt = 0;
}

static int xlnk_eng_probe(struct platform_device *pdev)
{
	struct resource *res;
	u32 reg_range;
	struct xilinx_xlnk_eng_device *xdev;
	struct uio_info *info;
	char *devname;
	int err = 0;

	pr_info("xlnk_eng_probe ...\n");
	xdev = kzalloc(sizeof(struct xilinx_xlnk_eng_device), GFP_KERNEL);
	if (!xdev) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		err = -ENOMEM;
		goto out_return;
	}

	/* more error handling */
	info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		err = -ENOMEM;
		goto out_free_xdev;
	}

	devname = (char *) kzalloc(64, GFP_KERNEL);
	if (!devname) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		err = -ENOMEM;
		goto out_free_xdev;
	}
	sprintf(devname, "%s.%d", DRIVER_NAME, pdev->id);
	pr_info("uio name %s\n", devname);
	/* iomap registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("get_resource for MEM resource for dev %d failed\n",
		       pdev->id);
		err = -ENOMEM;
		goto out_free_xdev;
	}
	reg_range = res->end - res->start + 1;
	if (!request_mem_region(res->start, reg_range, "xilin-xlnk-eng")) {
		pr_err("memory request failue for base %x\n",
		       (unsigned int)res->start);
		err = -ENOMEM;
		goto out_free_xdev;
	}

	xdev->base = ioremap(res->start, reg_range);

	pr_info("%s physical base : 0x%lx\n", DRIVER_NAME,
		(unsigned long)res->start);
	pr_info("%s register range : 0x%lx\n", DRIVER_NAME,
		(unsigned long)reg_range);
	pr_info("%s base remapped to: 0x%lx\n", DRIVER_NAME,
		(unsigned long)xdev->base);
	if (!xdev->base) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		err = -ENOMEM;
		goto out_free_xdev;
	}

	info->mem[0].addr = res->start;
	info->mem[0].size = reg_range;
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->mem[0].internal_addr = xdev->base;

	/* info->name = DRIVER_NAME; */
	info->name = devname;
	info->version = "0.0.1";

	info->irq = -1;

	xdev->common.dev = &pdev->dev;

	xdev->common.alloc = xilinx_xlnk_alloc;
	xdev->common.free = xilinx_xlnk_free;

	dev_set_drvdata(&pdev->dev, xdev);

	spin_lock_init(&xdev->lock);

	xdev->cnt = 0;

	xlnk_eng_register_device(&xdev->common);

	if (uio_register_device(&pdev->dev, info)) {
		dev_err(&pdev->dev, "uio_register_device failed\n");
		err = -ENODEV;
		goto out_unmap;
	}
	pr_info("xilinx-xlnk-eng uio registered\n");

	return 0;


out_unmap:
	iounmap(xdev->base);

	kfree(info);

out_free_xdev:
	kfree(xdev);

out_return:
	return err;
}

static int xlnk_eng_remove(struct platform_device *pdev)
{
	struct xlnk_eng_device *xlnk_dev =
		(struct xlnk_eng_device *)platform_get_drvdata(pdev);
	struct xilinx_xlnk_eng_device *xdev = to_xilinx_xlnk(xlnk_dev);

	/* xlnk_eng_device_unregister(&xdev); */

	iounmap(xdev->base);

	dev_set_drvdata(&pdev->dev, NULL);

	kfree(xdev);

	return 0;
}

static struct platform_driver xlnk_eng_driver = {
	.probe = xlnk_eng_probe,
	.remove = xlnk_eng_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
};

/*----------------------------------------------------------------------------*/
/* Module Init / Exit                                                         */
/*----------------------------------------------------------------------------*/

static __init int xlnk_eng_init(void)
{
	int status;
	status = platform_driver_register(&xlnk_eng_driver);
	return status;
}
module_init(xlnk_eng_init);

static void __exit xlnk_eng_exit(void)
{
	platform_driver_unregister(&xlnk_eng_driver);
}

module_exit(xlnk_eng_exit);

MODULE_DESCRIPTION("Xilinx xlnk engine generic driver");
MODULE_LICENSE("GPL");
