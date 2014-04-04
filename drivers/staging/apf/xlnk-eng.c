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

/**
 * struct xilinx_xlnk_eng_device - device structure for xilinx_xlnk_eng
 * @common:	common device info
 * @base:	base address for device
 * @lock:	lock used by device
 * @cnt:	usage count
 * @info:	info for registering and unregistering uio device
 */
struct xilinx_xlnk_eng_device {
	struct xlnk_eng_device common;
	void __iomem *base;
	spinlock_t lock;
	int cnt;
	struct uio_info *info;
};

static void xlnk_eng_release(struct device *dev)
{
	struct xilinx_xlnk_eng_device *xdev;
	struct xlnk_eng_device *xlnk_dev;

	xdev = dev_get_drvdata(dev);
	xlnk_dev = &xdev->common;
	if (!xlnk_dev)
		return;

	xlnk_dev->free(xlnk_dev);
}

#define DRIVER_NAME "xilinx-xlnk-eng"

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
	struct xilinx_xlnk_eng_device *xdev;
	struct uio_info *info;
	char *devname;

	pr_info("xlnk_eng_probe ...\n");
	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		return -ENOMEM;
	}

	/* more error handling */
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		return -ENOMEM;
	}
	xdev->info = info;
	devname = devm_kzalloc(&pdev->dev, 64, GFP_KERNEL);
	if (!devname) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		return -ENOMEM;
	}
	sprintf(devname, "%s.%d", DRIVER_NAME, pdev->id);
	pr_info("uio name %s\n", devname);
	/* iomap registers */

	/* Get the data from the platform device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->base = devm_ioremap_resource(&pdev->dev, res);

	/* %pa types should be used here */
	dev_info(&pdev->dev, "physical base : 0x%lx\n",
		(unsigned long)res->start);
	dev_info(&pdev->dev, "register range : 0x%lx\n",
		(unsigned long)resource_size(res));
	dev_info(&pdev->dev, "base remapped to: 0x%lx\n",
		(unsigned long)xdev->base);
	if (!xdev->base) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		return -ENOMEM;
	}

	info->mem[0].addr = res->start;
	info->mem[0].size = resource_size(res);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->mem[0].internal_addr = xdev->base;

	/* info->name = DRIVER_NAME; */
	info->name = devname;
	info->version = "0.0.1";

	info->irq = -1;

	xdev->common.dev = &pdev->dev;

	xdev->common.alloc = xilinx_xlnk_alloc;
	xdev->common.free = xilinx_xlnk_free;
	xdev->common.dev->release = xlnk_eng_release;

	dev_set_drvdata(&pdev->dev, xdev);

	spin_lock_init(&xdev->lock);

	xdev->cnt = 0;

	xlnk_eng_register_device(&xdev->common);

	if (uio_register_device(&pdev->dev, info)) {
		dev_err(&pdev->dev, "uio_register_device failed\n");
		return -ENODEV;
	}
	dev_info(&pdev->dev, "xilinx-xlnk-eng uio registered\n");

	return 0;
}

static int xlnk_eng_remove(struct platform_device *pdev)
{
	struct uio_info *info;
	struct xilinx_xlnk_eng_device *xdev;

	xdev = dev_get_drvdata(&pdev->dev);
	info = xdev->info;

	uio_unregister_device(info);
	dev_info(&pdev->dev, "xilinx-xlnk-eng uio unregistered\n");
	xlnk_eng_unregister_device(&xdev->common);

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

module_platform_driver(xlnk_eng_driver);

MODULE_DESCRIPTION("Xilinx xlnk engine generic driver");
MODULE_LICENSE("GPL");
