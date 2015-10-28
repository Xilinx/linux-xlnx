/*
 * Zynq Remote Processor driver
 *
 * Copyright (C) 2012 Michal Simek <monstr@monstr.eu>
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
#include <linux/slab.h>
#include <linux/cpu.h>

#include "remoteproc_internal.h"

extern int __cpuinit zynq_cpun_start(u32 address, int cpu);

/* Module parameter */
static char *firmware;

/* Structure for storing IRQs */
struct irq_list {
	int irq;
	struct list_head list;
};

/* Private data */
struct zynq_rproc_pdata {
	struct irq_list mylist;
	struct rproc *rproc;
	u32 vring0;
	u32 vring1;
	u32 mem_start;
	u32 mem_end;
};

/* Store rproc for IPI handler */
static struct platform_device *remoteprocdev;
static struct work_struct workqueue;

static void handle_event(struct work_struct *work)
{
	struct zynq_rproc_pdata *local = platform_get_drvdata(remoteprocdev);

	if (rproc_vq_interrupt(local->rproc, 0) == IRQ_NONE)
		dev_dbg(&remoteprocdev->dev, "no message found in vqid 0\n");
}

static void ipi_kick(void)
{
	dev_dbg(&remoteprocdev->dev, "KICK Linux because of pending message\n");
	schedule_work(&workqueue);
}

static int zynq_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_rproc_pdata *local = platform_get_drvdata(pdev);
	int ret;

	dev_dbg(dev, "%s\n", __func__);
	INIT_WORK(&workqueue, handle_event);


	mb();
	remoteprocdev = pdev;
	ret = zynq_cpun_start(rproc->bootaddr, 1);

	return ret;
}

/* kick a firmware */
static void zynq_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	/* Send swirq to firmware */
	if (!vqid)
		gic_raise_softirq(cpumask_of(1), local->vring0);
	else
		gic_raise_softirq(cpumask_of(1), local->vring1);
}

/* power off the remote processor */
static int zynq_rproc_stop(struct rproc *rproc)
{
	dev_dbg(rproc->dev.parent, "%s\n", __func__);

	/* FIXME missing reset option */
	return 0;
}

static struct rproc_ops zynq_rproc_ops = {
	.start		= zynq_rproc_start,
	.stop		= zynq_rproc_stop,
	.kick		= zynq_rproc_kick,
};

/* Just to detect bug if interrupt forwarding is broken */
static irqreturn_t zynq_remoteproc_interrupt(int irq, void *dev_id)
{
	struct device *dev = dev_id;

	dev_err(dev, "GIC IRQ %d is not forwarded correctly\n", irq);

	/*
	 *  MS: Calling this function doesn't need to be BUG
	 * especially for cases where firmware doesn't disable
	 * interrupts. In next probing can be som interrupts pending.
	 * The next scenario is for cases when you want to monitor
	 * non frequent interrupt through Linux kernel. Interrupt happen
	 * and it is forwarded to Linux which update own statistic
	 * in (/proc/interrupt) and forward it to firmware.
	 *
	 * gic_set_cpu(1, irq);	- setup cpu1 as destination cpu
	 * gic_raise_softirq(cpumask_of(1), irq); - forward irq to firmware
	 */

	gic_set_cpu(1, irq);
	return IRQ_HANDLED;
}

static void clear_irq(struct platform_device *pdev)
{
	struct list_head *pos, *q;
	struct irq_list *tmp;
	struct zynq_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Deleting the irq_list\n");
	list_for_each_safe(pos, q, &local->mylist.list) {
		tmp = list_entry(pos, struct irq_list, list);
		free_irq(tmp->irq, &pdev->dev);
		gic_set_cpu(0, tmp->irq);
		list_del(pos);
		kfree(tmp);
	}
}

static int zynq_remoteproc_probe(struct platform_device *pdev)
{
	const unsigned char *prop;
	struct resource *res; /* IO mem resources */
	int ret = 0;
	struct irq_list *tmp;
	int count = 0;
	struct zynq_rproc_pdata *local;

	ret = cpu_down(1);
	/* EBUSY means CPU is already released */
	if (ret && (ret != -EBUSY)) {
		dev_err(&pdev->dev, "Can't release cpu1\n");
		return -ENOMEM;
	}

	local = devm_kzalloc(&pdev->dev, sizeof(struct zynq_rproc_pdata),
			     GFP_KERNEL);
	if (!local)
		return -ENOMEM;

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
		ret = -ENOMEM;
		goto dma_fault;
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto dma_mask_fault;
	}

	/* Init list for IRQs - it can be long list */
	INIT_LIST_HEAD(&local->mylist.list);

	/* Alloc IRQ based on DTS to be sure that no other driver will use it */
	while (1) {
		int irq;

		irq = platform_get_irq(pdev, count++);
		if (irq == -ENXIO || irq == -EINVAL)
			break;

		tmp = kzalloc(sizeof(struct irq_list), GFP_KERNEL);
		if (!tmp) {
			dev_err(&pdev->dev, "Unable to alloc irq list\n");
			ret = -ENOMEM;
			goto irq_fault;
		}

		tmp->irq = irq;

		dev_dbg(&pdev->dev, "%d: Alloc irq: %d\n", count, tmp->irq);

		/* Allocating shared IRQs will ensure that any module will
		 * use these IRQs */
		ret = request_irq(tmp->irq, zynq_remoteproc_interrupt, 0,
					dev_name(&pdev->dev), &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "IRQ %d already allocated\n",
								tmp->irq);
			goto irq_fault;
		}

		/*
		 * MS: Here is place for detecting problem with firmware
		 * which doesn't work correctly with interrupts
		 *
		 * MS: Comment if you want to count IRQs on Linux
		 */
		gic_set_cpu(1, tmp->irq);
		list_add(&(tmp->list), &(local->mylist.list));
	}

	/* Allocate free IPI number */
	/* Read vring0 ipi number */
	ret = of_property_read_u32(pdev->dev.of_node, "vring0", &local->vring0);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to read property");
		goto ipi_fault;
	}

	ret = set_ipi_handler(local->vring0, ipi_kick, "Firmware kick");
	if (ret) {
		dev_err(&pdev->dev, "IPI handler already registered\n");
		goto irq_fault;
	}

	/* Read vring1 ipi number */
	ret = of_property_read_u32(pdev->dev.of_node, "vring1", &local->vring1);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to read property");
		goto ipi_fault;
	}

	/* Module param firmware first */
	if (firmware)
		prop = firmware;
	else
		prop = of_get_property(pdev->dev.of_node, "firmware", NULL);

	if (prop) {
		dev_dbg(&pdev->dev, "Using firmware: %s\n", prop);
		local->rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev),
				&zynq_rproc_ops, prop, sizeof(struct rproc));
		if (!local->rproc) {
			dev_err(&pdev->dev, "rproc allocation failed\n");
			goto ipi_fault;
		}

		ret = rproc_add(local->rproc);
		if (ret) {
			dev_err(&pdev->dev, "rproc registration failed\n");
			goto rproc_fault;
		}

		return ret;
	} else
		ret = -ENODEV;

rproc_fault:
	rproc_put(local->rproc);
ipi_fault:
	clear_ipi_handler(local->vring0);

irq_fault:
	clear_irq(pdev);

dma_mask_fault:
	dma_release_declared_memory(&pdev->dev);

dma_fault:
	/* Cpu can't be power on - for example in nosmp mode */
	ret |= cpu_up(1);
	if (ret)
		dev_err(&pdev->dev, "Can't power on cpu1 %d\n", ret);

	return ret;
}

static int zynq_remoteproc_remove(struct platform_device *pdev)
{
	struct zynq_rproc_pdata *local = platform_get_drvdata(pdev);
	u32 ret;

	dev_info(&pdev->dev, "%s\n", __func__);

	dma_release_declared_memory(&pdev->dev);

	clear_ipi_handler(local->vring0);
	clear_irq(pdev);

	rproc_del(local->rproc);
	rproc_put(local->rproc);

	/* Cpu can't be power on - for example in nosmp mode */
	ret = cpu_up(1);
	if (ret)
		dev_err(&pdev->dev, "Can't power on cpu1 %d\n", ret);

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id zynq_remoteproc_match[] = {
	{ .compatible = "xlnx,zynq_remoteproc", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, zynq_remoteproc_match);

static struct platform_driver zynq_remoteproc_driver = {
	.probe = zynq_remoteproc_probe,
	.remove = zynq_remoteproc_remove,
	.driver = {
		.name = "zynq_remoteproc",
		.of_match_table = zynq_remoteproc_match,
	},
};
module_platform_driver(zynq_remoteproc_driver);

module_param(firmware, charp, 0);
MODULE_PARM_DESC(firmware, "Override the firmware image name. Default value in DTS.");

MODULE_AUTHOR("Michal Simek <monstr@monstr.eu");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Zynq remote processor control driver");
