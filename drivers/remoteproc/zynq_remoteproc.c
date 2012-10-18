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
#include <linux/genalloc.h>
#include <../../arch/arm/mach-zynq/common.h>

#include "remoteproc_internal.h"

#define MAX_NUM_VRINGS 2
#define NOTIFYID_ANY (-1)
/* Maximum on chip memories used by the driver*/
#define MAX_ON_CHIP_MEMS        32

/* Structure for storing IRQs */
struct irq_list {
	int irq;
	struct list_head list;
};

/* Structure for IPIs */
struct ipi_info {
	u32 irq;
	u32 notifyid;
	bool pending;
};

/* On-chip memory pool element */
struct mem_pool_st {
	struct list_head node;
	struct gen_pool *pool;
};

/* Private data */
struct zynq_rproc_pdata {
	struct irq_list irqs;
	struct rproc *rproc;
	struct ipi_info ipis[MAX_NUM_VRINGS];
	struct list_head mem_pools;
	struct list_head mems;
	u32 mem_start;
	u32 mem_end;
};

/* Store rproc for IPI handler */
static struct rproc *rproc;
static struct work_struct workqueue;

static void handle_event(struct work_struct *work)
{
	struct zynq_rproc_pdata *local = rproc->priv;

	if (rproc_vq_interrupt(local->rproc, local->ipis[0].notifyid) ==
				IRQ_NONE)
		dev_dbg(rproc->dev.parent, "no message found in vqid 0\n");
}

static void ipi_kick(void)
{
	dev_dbg(rproc->dev.parent, "KICK Linux because of pending message\n");
	schedule_work(&workqueue);
}

static void kick_pending_ipi(struct rproc *rproc)
{
	struct zynq_rproc_pdata *local = rproc->priv;
	int i;

	for (i = 0; i < MAX_NUM_VRINGS; i++) {

		/* Send swirq to firmware */
		if (local->ipis[i].pending == true) {
			gic_raise_softirq(cpumask_of(1),
					local->ipis[i].irq);
			local->ipis[i].pending = false;
		}
	}
}

static int zynq_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	int ret;

	dev_dbg(dev, "%s\n", __func__);
	INIT_WORK(&workqueue, handle_event);

	ret = cpu_down(1);
	/* EBUSY means CPU is already released */
	if (ret && (ret != -EBUSY)) {
		dev_err(dev, "Can't release cpu1\n");
		return ret;
	}

	ret = zynq_cpun_start(rproc->bootaddr, 1);
	/* Trigger pending kicks */
	kick_pending_ipi(rproc);

	return ret;
}

/* kick a firmware */
static void zynq_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct zynq_rproc_pdata *local = rproc->priv;
	struct rproc_vdev *rvdev, *rvtmp;
	struct fw_rsc_vdev *rsc;
	int i;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	list_for_each_entry_safe(rvdev, rvtmp, &rproc->rvdevs, node) {
		rsc = (void *)rproc->table_ptr + rvdev->rsc_offset;
		for (i = 0; i < MAX_NUM_VRINGS; i++) {
			struct rproc_vring *rvring = &rvdev->vring[i];

			/* Send swirq to firmware */
			if (rvring->notifyid == vqid) {
				local->ipis[i].notifyid = vqid;
				/* As we do not turn off CPU1 until start,
				 * we delay firmware kick
				 */
				if (rproc->state == RPROC_RUNNING)
					gic_raise_softirq(cpumask_of(1),
						local->ipis[i].irq);
				else
					local->ipis[i].pending = true;
			}
		}

	}
}

/* power off the remote processor */
static int zynq_rproc_stop(struct rproc *rproc)
{
	int ret;
	struct device *dev = rproc->dev.parent;

	dev_dbg(rproc->dev.parent, "%s\n", __func__);

	/* Cpu can't be power on - for example in nosmp mode */
	ret = cpu_up(1);
	if (ret)
		dev_err(dev, "Can't power on cpu1 %d\n", ret);

	return 0;
}

static void *zynq_rproc_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct rproc_mem_entry *mem;
	void *va = 0;
	struct zynq_rproc_pdata *local = rproc->priv;

	list_for_each_entry(mem, &local->mems, node) {
		int offset = da - mem->da;

		/* try next carveout if da is too small */
		if (offset < 0)
			continue;

		/* try next carveout if da is too large */
		if (offset + len > mem->len)
			continue;

		va = mem->va + offset;

		break;
	}
	return va;
}

static struct rproc_ops zynq_rproc_ops = {
	.start		= zynq_rproc_start,
	.stop		= zynq_rproc_stop,
	.kick		= zynq_rproc_kick,
	.da_to_va	= zynq_rproc_da_to_va,
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

static void clear_irq(struct rproc *rproc)
{
	struct list_head *pos, *q;
	struct irq_list *tmp;
	struct zynq_rproc_pdata *local = rproc->priv;

	dev_info(rproc->dev.parent, "Deleting the irq_list\n");
	list_for_each_safe(pos, q, &local->irqs.list) {
		tmp = list_entry(pos, struct irq_list, list);
		free_irq(tmp->irq, rproc->dev.parent);
		gic_set_cpu(0, tmp->irq);
		list_del(pos);
		kfree(tmp);
	}
}

static int zynq_rproc_add_mems(struct zynq_rproc_pdata *pdata)
{
	struct mem_pool_st *mem_node;
	size_t mem_size;
	struct gen_pool *mem_pool;
	struct rproc_mem_entry *mem;
	dma_addr_t dma;
	void *va;
	struct device *dev = pdata->rproc->dev.parent;

	list_for_each_entry(mem_node, &pdata->mem_pools, node) {
		mem_pool = mem_node->pool;
		mem_size = gen_pool_size(mem_pool);
		mem  = devm_kzalloc(dev, sizeof(struct rproc_mem_entry),
				GFP_KERNEL);
		if (!mem)
			return -ENOMEM;

		va = gen_pool_dma_alloc(mem_pool, mem_size, &dma);
		if (!va) {
			dev_err(dev, "Failed to allocate dma carveout mem.\n");
			return -ENOMEM;
		}
		mem->priv = (void *)mem_pool;
		mem->va = va;
		mem->len = mem_size;
		mem->dma = dma;
		mem->da = dma;
		dev_dbg(dev, "%s: va = %p, da = 0x%x dma = 0x%x\n",
			__func__, va, mem->da, mem->dma);
		list_add_tail(&mem->node, &pdata->mems);
	}
	return 0;
}

static int zynq_remoteproc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct irq_list *tmp;
	int count = 0;
	struct zynq_rproc_pdata *local;
	struct gen_pool *mem_pool = NULL;
	struct mem_pool_st *mem_node = NULL;
	int i;

	rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev),
		&zynq_rproc_ops, NULL,
		sizeof(struct zynq_rproc_pdata));
	if (!rproc) {
		dev_err(&pdev->dev, "rproc allocation failed\n");
		ret = -ENOMEM;
		return ret;
	}
	local = rproc->priv;
	local->rproc = rproc;

	platform_set_drvdata(pdev, rproc);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto dma_mask_fault;
	}

	/* Init list for IRQs - it can be long list */
	INIT_LIST_HEAD(&local->irqs.list);

	/* Alloc IRQ based on DTS to be sure that no other driver will use it */
	while (1) {
		int irq;

		irq = platform_get_irq(pdev, count++);
		if (irq == -ENXIO || irq == -EINVAL)
			break;

		tmp = kzalloc(sizeof(struct irq_list), GFP_KERNEL);
		if (!tmp) {
			ret = -ENOMEM;
			goto irq_fault;
		}

		tmp->irq = irq;

		dev_dbg(&pdev->dev, "%d: Alloc irq: %d\n", count, tmp->irq);

		/* Allocating shared IRQs will ensure that any module will
		 * use these IRQs
		 */
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
		list_add(&(tmp->list), &(local->irqs.list));
	}

	/* Allocate free IPI number */
	/* Read vring0 ipi number */
	ret = of_property_read_u32(pdev->dev.of_node, "vring0",
				&local->ipis[0].irq);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to read property");
		goto irq_fault;
	}

	ret = set_ipi_handler(local->ipis[0].irq, ipi_kick,
			"Firmware kick");
	if (ret) {
		dev_err(&pdev->dev, "IPI handler already registered\n");
		goto irq_fault;
	}

	/* Read vring1 ipi number */
	ret = of_property_read_u32(pdev->dev.of_node, "vring1",
				&local->ipis[1].irq);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to read property");
		goto ipi_fault;
	}

	/* Find on-chip memory */
	INIT_LIST_HEAD(&local->mem_pools);
	INIT_LIST_HEAD(&local->mems);
	for (i = 0; ; i++) {
		char *srams_name = "srams";

		mem_pool = of_gen_pool_get(pdev->dev.of_node,
					   srams_name, i);
		if (mem_pool) {
			mem_node = devm_kzalloc(&pdev->dev,
					sizeof(struct mem_pool_st),
					GFP_KERNEL);
			if (!mem_node)
				goto ipi_fault;
			mem_node->pool = mem_pool;
			list_add_tail(&mem_node->node, &local->mem_pools);
		} else {
			break;
		}
	}
	ret = zynq_rproc_add_mems(local);
	if (ret) {
		dev_err(&pdev->dev, "rproc failed to add mems\n");
		goto ipi_fault;
	}

	ret = rproc_add(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "rproc registration failed\n");
		goto ipi_fault;
	}

	return 0;

ipi_fault:
	clear_ipi_handler(local->ipis[0].irq);

irq_fault:
	clear_irq(rproc);

dma_mask_fault:
	rproc_free(rproc);

	return ret;
}

static int zynq_remoteproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct zynq_rproc_pdata *local = rproc->priv;
	struct rproc_mem_entry *mem;

	dev_info(&pdev->dev, "%s\n", __func__);

	rproc_del(rproc);

	clear_ipi_handler(local->ipis[0].irq);
	clear_irq(rproc);

	list_for_each_entry(mem, &local->mems, node) {
		if (mem->priv)
			gen_pool_free((struct gen_pool *)mem->priv,
				      (unsigned long)mem->va, mem->len);
	}

	rproc_free(rproc);

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

MODULE_AUTHOR("Michal Simek <monstr@monstr.eu");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Zynq remote processor control driver");
