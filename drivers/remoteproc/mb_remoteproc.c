/*
 * Microblaze Remote Processor driver
 *
 * Copyright (C) 2012 - 2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2013 Xilinx, Inc.
 * Copyright (C) 2012 PetaLogix
 *
 * Based on origin OMAP Remote Processor driver
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/smp.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/outercache.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/cpu.h>

#include "remoteproc_internal.h"

/* Module parameter */
static char *firmware;

/* Private data */
struct mb_rproc_pdata {
	struct rproc *rproc;
	u32 mem_start;
	u32 mem_end;
	u32 *gpio_reset_addr;
	u32 reset_gpio_pin;
};

static int mb_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct mb_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_info(dev, "%s\n", __func__);

	flush_cache_all();
	outer_flush_range(local->mem_start, local->mem_end);

	*local->gpio_reset_addr &= ~(1 << local->reset_gpio_pin);

	return 0;
}

/* kick a firmware */
static void mb_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;

	dev_info(dev, "KICK Firmware to start send messages vqid %d\n",
									vqid);
}

/* power off the remote processor */
static int mb_rproc_stop(struct rproc *rproc)
{
  	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct mb_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_info(dev, "%s\n", __func__);

	*local->gpio_reset_addr |= 1 << local->reset_gpio_pin;

	return 0;
}

static struct rproc_ops mb_rproc_ops = {
	.start		= mb_rproc_start,
	.stop		= mb_rproc_stop,
	.kick		= mb_rproc_kick,
};

/* Just to detect bug if interrupt forwarding is broken */
static irqreturn_t mb_remoteproc_interrupt(int irq, void *dev_id)
{
	struct device *dev = dev_id;

	dev_err(dev, "GIC IRQ %d is not forwarded correctly\n", irq);

	return IRQ_HANDLED;
}

static int mb_remoteproc_probe(struct platform_device *pdev)
{
	const unsigned char *prop;
	const void *of_prop;
	struct resource *res; /* IO mem resources */
	int ret = 0;
	int count = 0;
	struct mb_rproc_pdata *local;

	local = devm_kzalloc(&pdev->dev, sizeof(*local), GFP_KERNEL);
	if (!local) {
		dev_err(&pdev->dev, "Unable to alloc private data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, local);

	/* Declare memory for firmware */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "invalid address\n");
		return -ENODEV;
	}

	local->mem_start = res->start;
	local->mem_end = res->end;

	/* Alloc phys addr from 0 to max_addr for firmware */
	ret = dma_declare_coherent_memory(&pdev->dev, local->mem_start,
		local->mem_start, local->mem_end - local->mem_start + 1,
		DMA_MEMORY_IO);
	if (!ret) {
		dev_err(&pdev->dev, "dma_declare_coherent_memory failed\n");
		return ret;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	/* Alloc IRQ based on DTS to be sure that no other driver will use it */
	while (1) {
		int irq;
		/* Allocating shared IRQs will ensure that any module will
		 * use these IRQs */
		irq = platform_get_irq(pdev, count++);
		if (irq == -ENXIO)
			break;
		ret = devm_request_irq(&pdev->dev, irq, mb_remoteproc_interrupt,
				       0, dev_name(&pdev->dev), &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "IRQ %d already allocated\n", irq);
			return ret;
		}

		dev_info(&pdev->dev, "%d: Alloc irq: %d\n", count, irq);
	}

	of_prop = of_get_property(pdev->dev.of_node, "reset-gpio", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify gpio reset addr\n");
		return of_prop;
	}

	local->gpio_reset_addr = ioremap(be32_to_cpup(of_prop), 0x1000);
	if (!local->gpio_reset_addr) {
		dev_err(&pdev->dev, "Reset GPIO ioremap failed\n");
		return local->gpio_reset_addr;
	}

	of_prop = of_get_property(pdev->dev.of_node, "reset-gpio-pin", NULL);
	if (!of_prop) {
		dev_err(&pdev->dev, "Please specify cpu number\n");
		return of_prop;
	}
	local->reset_gpio_pin = be32_to_cpup(of_prop);

	/* Keep mb in reset */
	*local->gpio_reset_addr |= 1 << local->reset_gpio_pin;

	/* Module param firmware first */
	if (firmware)
		prop = firmware;
	else
		prop = of_get_property(pdev->dev.of_node, "firmware", NULL);

	if (prop) {
		dev_info(&pdev->dev, "Using firmware: %s\n", prop);
		local->rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev),
				&mb_rproc_ops, prop, sizeof(struct rproc));
		if (!local->rproc) {
			dev_err(&pdev->dev, "rproc allocation failed\n");
			return -ENOMEM;
		}

		ret = rproc_add(local->rproc);
		if (ret) {
			dev_err(&pdev->dev, "rproc registration failed\n");
			rproc_put(local->rproc);
			return ret;
		}
		return 0;
	}

	return -ENODEV;
}

static int mb_remoteproc_remove(struct platform_device *pdev)
{
	struct mb_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s\n", __func__);

	dma_release_declared_memory(&pdev->dev);

	rproc_del(local->rproc);
	rproc_put(local->rproc);

	return 0;
}

/* Match table for OF platform binding */
static struct of_device_id mb_remoteproc_match[] = {
	{ .compatible = "xlnx,mb_remoteproc", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, mb_remoteproc_match);

static struct platform_driver mb_remoteproc_driver = {
	.probe = mb_remoteproc_probe,
	.remove = mb_remoteproc_remove,
	.driver = {
		.name = "mb_remoteproc",
		.owner = THIS_MODULE,
		.of_match_table = mb_remoteproc_match,
	},
};
module_platform_driver(mb_remoteproc_driver);

module_param(firmware, charp, 0);
MODULE_PARM_DESC(firmware, "Override the firmware image name. Default value in DTS.");

MODULE_AUTHOR("Michal Simek <monstr@monstr.eu");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Microblaze remote processor control driver");
