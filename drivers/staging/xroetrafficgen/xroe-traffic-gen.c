// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include "xroe-traffic-gen.h"

#define DRIVER_NAME "xroe_traffic_gen"

static struct platform_driver xroe_traffic_gen_driver;

/**
 * xroe_traffic_gen_probe - Probes the device tree to locate the traffic gen
 * block
 * @pdev:	The structure containing the device's details
 *
 * Probes the device tree to locate the traffic gen block and maps it to
 * the kernel virtual memory space
 *
 * Return: 0 on success or a negative errno on error.
 */
static int xroe_traffic_gen_probe(struct platform_device *pdev)
{
	struct xroe_traffic_gen_local *lp;
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;

	lp = devm_kzalloc(&pdev->dev, sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	/* Get iospace for the device */
	/*
	 * TODO: Use platform_get_resource_byname() instead when the DT entry
	 * of the traffic gen block has been finalised (when it gets out of
	 * the development stage).
	 */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->base_addr = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(lp->base_addr))
		return PTR_ERR(lp->base_addr);

	dev_set_drvdata(dev, lp);
	xroe_traffic_gen_sysfs_init(dev);
	return 0;
}

/**
 * xroe_traffic_gen_remove - Removes the sysfs entries created by the driver
 * @pdev:	The structure containing the device's details
 *
 * Removes the sysfs entries created by the driver
 *
 * Return: 0
 */
static int xroe_traffic_gen_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	xroe_traffic_gen_sysfs_exit(dev);
	return 0;
}

/**
 * xroe_traffic_gen_init - Registers the driver
 *
 * Return: 0 on success, -1 on allocation error
 *
 * Registers the traffic gen driver and creates the sysfs entries related
 * to it
 */
static int __init xroe_traffic_gen_init(void)
{
	int ret;

	pr_info("XROE traffic generator driver init\n");
	ret = platform_driver_register(&xroe_traffic_gen_driver);
	return ret;
}

/**
 * xroe_traffic_gen_exit - Destroys the driver
 *
 * Unregisters the traffic gen driver
 */
static void __exit xroe_traffic_gen_exit(void)
{
	platform_driver_unregister(&xroe_traffic_gen_driver);
	pr_debug("XROE traffic generator driver exit\n");
}

static const struct of_device_id xroe_traffic_gen_of_match[] = {
	{ .compatible = "xlnx,roe-traffic-gen-1.0", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, xroe_traffic_gen_of_match);

static struct platform_driver xroe_traffic_gen_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table	= xroe_traffic_gen_of_match,
	},
	.probe = xroe_traffic_gen_probe,
	.remove = xroe_traffic_gen_remove,
};

module_init(xroe_traffic_gen_init);
module_exit(xroe_traffic_gen_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx Radio over Ethernet Traffic Generator driver");
