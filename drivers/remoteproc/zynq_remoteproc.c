// SPDX-License-Identifier: GPL-2.0
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_reserved_mem.h>
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

/**
 * struct zynq_mem_res - zynq memory resource for firmware memory
 * @res: memory resource
 * @node: list node
 */
struct zynq_mem_res {
	struct resource res;
	struct list_head node;
};

/**
 * struct zynq_rproc_data - zynq rproc private data
 * @irqs: inter processor soft IRQs
 * @rproc: pointer to remoteproc instance
 * @ipis: interrupt processor interrupts statistics
 * @fw_mems: list of firmware memories
 */
struct zynq_rproc_pdata {
	struct irq_list irqs;
	struct rproc *rproc;
	struct ipi_info ipis[MAX_NUM_VRINGS];
	struct list_head fw_mems;
};

static bool autoboot __read_mostly;

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
		if (local->ipis[i].pending) {
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
	int i;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	list_for_each_entry_safe(rvdev, rvtmp, &rproc->rvdevs, node) {
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
	struct zynq_rproc_pdata *local = rproc->priv;
	struct zynq_mem_res *mem_res;
	struct device *dev;

	dev = rproc->dev.parent;
	list_for_each_entry(mem_res, &local->fw_mems, node) {
		/* This function will only get called if
		 * the reserved firmware memory is not yet
		 * added to the carved out list.
		 */
		struct rproc_mem_entry *mem;
		struct resource *res = &mem_res->res;
		u64 res_da = (u64)res->start;
		resource_size_t size;
		int offset;
		void *va;
		dma_addr_t dma;

		size = resource_size(res);
		offset = (int)(da - res_da);
		if (offset < 0)
			continue;
		if (da > (res_da + size))
			continue;
		va = devm_ioremap_wc(dev, res->start, size);
		dma = (dma_addr_t)res->start;
		mem = rproc_mem_entry_init(dev, va, dma, (int)size,
					   (u32)res->start,
					   NULL, NULL, res->name);
		if (!mem)
			return NULL;
		dev_dbg(dev, "%s: %s, va = %p, da = 0x%x dma = 0x%x\n",
			__func__, mem->name, mem->va,
			mem->da, mem->dma);
		rproc_add_carveout(rproc, mem);
		return (char *)va + offset;
	}
	return NULL;
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
	int num_mems, i;
	struct device *dev = pdata->rproc->dev.parent;
	struct device_node *np = dev->of_node;

	INIT_LIST_HEAD(&pdata->fw_mems);
	num_mems = of_count_phandle_with_args(np, "memory-region", NULL);
	if (num_mems <= 0)
		return 0;
	for (i = 0; i < num_mems; i++) {
		struct device_node *node;
		struct zynq_mem_res *mem_res;
		int ret;

		node = of_parse_phandle(np, "memory-region", i);
		ret = of_device_is_compatible(node, "shared-dma-pool");
		if (ret) {
			/* it is DMA memory. */
			ret = of_reserved_mem_device_init_by_idx(dev, np, i);
			if (ret) {
				dev_err(dev, "unable to reserve DMA mem.\n");
				return ret;
			}
			dev_dbg(dev, "%s, dma memory %s.\n",
				__func__, of_node_full_name(node));
			continue;
		}
		/*
		 * It is non-DMA memory, used for firmware loading.
		 * It will be added to the remoteproc carveouts later while
		 * loading the firmware.
		 * It needs to keep track of the memory resource here and
		 * add it to carveouts when loading firmware because
		 * the carveouts will be removed when rproc stops the processor.
		 */
		mem_res = devm_kzalloc(dev, sizeof(*mem_res), GFP_KERNEL);
		if (!mem_res)
			return -ENOMEM;
		ret = of_address_to_resource(node, 0, &mem_res->res);
		if (ret) {
			dev_err(dev, "unable to resolve memory region.\n");
			return ret;
		}
		list_add_tail(&mem_res->node, &pdata->fw_mems);
		dev_dbg(dev, "%s, non-dma mem %s\n",
			__func__, of_node_full_name(node));
	}
	return 0;
}

static int zynq_remoteproc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct irq_list *tmp;
	int count = 0;
	struct zynq_rproc_pdata *local;

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

		tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
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
		list_add(&tmp->list, &local->irqs.list);
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
	ret = zynq_rproc_add_mems(local);
	if (ret) {
		dev_err(&pdev->dev, "rproc failed to add mems\n");
		goto ipi_fault;
	}

	rproc->auto_boot = autoboot;

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

	dev_info(&pdev->dev, "%s\n", __func__);

	rproc_del(rproc);

	clear_ipi_handler(local->ipis[0].irq);
	clear_irq(rproc);

	of_reserved_mem_device_release(&pdev->dev);
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

module_param_named(autoboot,  autoboot, bool, 0444);
MODULE_PARM_DESC(autoboot,
		 "enable | disable autoboot. (default: false)");

MODULE_AUTHOR("Michal Simek <monstr@monstr.eu");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Zynq remote processor control driver");
