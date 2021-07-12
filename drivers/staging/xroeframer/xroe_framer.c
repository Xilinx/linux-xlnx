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

/*
 * TODO: to be made static as well, so that multiple instances can be used. As
 * of now, the "xroe_lp" structure is shared among the multiple source files
 */
struct framer_local *xroe_lp;
static struct platform_driver framer_driver;
/*
 * TODO: placeholder for the IRQ once it's been implemented
 * in the framer block
 */
static irqreturn_t framer_irq(int irq, void *xroe_lp)
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
	int rc = 0;

	dev_dbg(dev, "Device Tree Probing\n");
	xroe_lp = devm_kzalloc(&pdev->dev, sizeof(*xroe_lp), GFP_KERNEL);
	if (!xroe_lp)
		return -ENOMEM;

	/* Get iospace for the device */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xroe_lp->base_addr = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(xroe_lp->base_addr))
		return PTR_ERR(xroe_lp->base_addr);

	dev_set_drvdata(dev, xroe_lp);
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
	rc = devm_request_irq(dev, xroe_lp->irq, &framer_irq, 0, DRIVER_NAME, xroe_lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			xroe_lp->irq);
		/*
		 * TODO: Return non-zero (error) code on no IRQ found.
		 * To be implemented once the IRQ is in the block
		 */
		return 0;
	}

	return rc;
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
	platform_driver_unregister(&framer_driver);
	pr_info("XROE Framer exit\n");
}

module_init(framer_init);
module_exit(framer_exit);

static const struct of_device_id framer_of_match[] = {
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

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("framer - Xilinx Radio over Ethernet Framer driver");
