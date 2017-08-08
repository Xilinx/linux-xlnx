/*
 * Zynq R5 Remote Processor driver
 *
 * Copyright (C) 2015 Jason Wu <j.wu@xilinx.com>
 * Copyright (C) 2015 Xilinx, Inc.
 *
 * Based on origin OMAP and Zynq Remote Processor driver
 *
 * Copyright (C) 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012 PetaLogix
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
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/genalloc.h>
#include <linux/pfn.h>
#include <linux/idr.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

#include "remoteproc_internal.h"

/* Register offset definitions for RPU. */
#define RPU_GLBL_CNTL_OFFSET	0x00000000 /* RPU control */

#define RPU_CFG_OFFSET	0x00000000 /* RPU configuration */

/* Boot memory bit. high for OCM, low for TCM */
#define VINITHI_BIT		BIT(2)
/* CPU halt bit, high: processor is running. low: processor is halt */
#define nCPUHALT_BIT		BIT(0)
/* RPU mode, high: split mode. low: lock step mode */
#define SLSPLIT_BIT		BIT(3)
/* Clamp mode. high: split mode. low: lock step mode */
#define SLCLAMP_BIT		BIT(4)
/* TCM mode. high: combine RPU TCMs. low: split TCM for RPU1 and RPU0 */
#define TCM_COMB_BIT		BIT(6)

/* IPI reg offsets */
#define TRIG_OFFSET		0x00000000
#define OBS_OFFSET		0x00000004
#define ISR_OFFSET		0x00000010
#define IMR_OFFSET		0x00000014
#define IER_OFFSET		0x00000018
#define IDR_OFFSET		0x0000001C
#define IPI_ALL_MASK		0x0F0F0301

#define MAX_INSTANCES		2 /* Support upto 2 RPU */

/* RPU IPI mask */
#define RPU_IPI_INIT_MASK	0x00000100
#define RPU_IPI_MASK(n)		(RPU_IPI_INIT_MASK << (n))
#define RPU_0_IPI_MASK		RPU_IPI_MASK(0)
#define RPU_1_IPI_MASK		RPU_IPI_MASK(1)

/* PM proc states */
#define PM_PROC_STATE_ACTIVE 1u

/* Register access macros */
#define reg_read(base, reg) \
	readl(((void __iomem *)(base)) + (reg))
#define reg_write(base, reg, val) \
	writel((val), ((void __iomem *)(base)) + (reg))

#define DEFAULT_FIRMWARE_NAME	"rproc-rpu-fw"

static bool autoboot __read_mostly;

struct zynqmp_r5_rproc_pdata;

/* enumerations for R5 boot device */
enum rpu_bootmem {
	TCM = 0,
	OCM,
};

/* enumerations for R5 core configurations */
enum rpu_core_conf {
	LOCK_STEP = 0,
	SPLIT,
};

/* Power domain id list element */
struct pd_id_st {
	struct list_head node;
	u32 id;
};

/* On-chip memory pool element */
struct mem_pool_st {
	struct list_head node;
	struct gen_pool *pool;
	struct list_head pd_ids;
};

/**
 * struct zynqmp_r5_rproc_pdata - zynqmp rpu remote processor instance state
 * @rproc: rproc handle
 * @fw_ops: local firmware operations
 * @default_fw_ops: default rproc firmware operations
 * @workqueue: workqueue for the RPU remoteproc
 * @rpu_base: virt ptr to RPU control address registers
 * @rpu_glbl_base: virt ptr to RPU global control address registers
 * @ipi_base: virt ptr to IPI channel address registers for APU
 * @rpu_mode: RPU core configuration
 * @rpu_id: RPU CPU id
 * @rpu_pd_id: RPU CPU power domain id
 * @bootmem: RPU boot memory device used
 * @mem_pools: list of gen_pool for firmware mmio_sram memory and their
 *             power domain IDs
 * @mems: list of rproc_mem_entries for firmware
 * @vring0: IRQ number used for vring0
 * @ipi_dest_mask: IPI destination mask for the IPI channel
 */
struct zynqmp_r5_rproc_pdata {
	struct rproc *rproc;
	struct rproc_fw_ops fw_ops;
	const struct rproc_fw_ops *default_fw_ops;
	struct work_struct workqueue;
	void __iomem *rpu_base;
	void __iomem *rpu_glbl_base;
	void __iomem *ipi_base;
	enum rpu_core_conf rpu_mode;
	enum rpu_bootmem bootmem;
	struct list_head mem_pools;
	struct list_head mems;
	u32 ipi_dest_mask;
	u32 rpu_id;
	u32 rpu_pd_id;
	int vring0;
};

/**
 * r5_boot_addr_config - configure the boot address of R5
 * @pdata: platform data
 *
 * This function will set the boot address based on if the
 * boot memory in the ELF file is TCM or OCM
 */
static void r5_boot_addr_config(struct zynqmp_r5_rproc_pdata *pdata)
{
	u32 tmp;
	u32 offset = RPU_CFG_OFFSET;

	pr_debug("%s: R5 ID: %d, boot_dev %d\n",
		 __func__, pdata->rpu_id, pdata->bootmem);

	tmp = reg_read(pdata->rpu_base, offset);
	if (pdata->bootmem == OCM)
		tmp |= VINITHI_BIT;
	else
		tmp &= ~VINITHI_BIT;
	reg_write(pdata->rpu_base, offset, tmp);
}

/**
 * r5_mode_config - configure R5 operation mode
 * @pdata: platform data
 *
 * configure R5 to split mode or lockstep mode
 * based on the platform data.
 */
static void r5_mode_config(struct zynqmp_r5_rproc_pdata *pdata)
{
	u32 tmp;

	pr_debug("%s: mode: %d\n", __func__, pdata->rpu_mode);
	tmp = reg_read(pdata->rpu_glbl_base, 0);
	if (pdata->rpu_mode == SPLIT) {
		tmp |= SLSPLIT_BIT;
		tmp &= ~TCM_COMB_BIT;
		tmp &= ~SLCLAMP_BIT;
	} else {
		tmp &= ~SLSPLIT_BIT;
		tmp |= TCM_COMB_BIT;
		tmp |= SLCLAMP_BIT;
	}
	reg_write(pdata->rpu_glbl_base, 0, tmp);
}

/*
 * r5_is_running - check if r5 is running
 * @pdata: platform data
 *
 * check if R5 is running
 * @retrun: true if r5 is running, false otherwise
 */
static bool r5_is_running(struct zynqmp_r5_rproc_pdata *pdata)
{
	u32 status, requirements, usage;

	pr_debug("%s: rpu id: %d\n", __func__, pdata->rpu_id);
	if (zynqmp_pm_get_node_status(pdata->rpu_pd_id,
				      &status, &requirements, &usage)) {
		pr_err("Failed to get RPU node status.\n");
		return false;
	} else if (status != PM_PROC_STATE_ACTIVE) {
		pr_debug("RPU %d is not running.\n", pdata->rpu_id);
		return false;
	}

	pr_debug("RPU %d is running.\n", pdata->rpu_id);
	return true;
}

/**
 * r5_request_tcm - request access to TCM
 * @pdata: platform data
 *
 * Request access to TCM
 *
 * @return: 0 if succeeded, error code otherwise
 */
static int r5_request_tcm(struct zynqmp_r5_rproc_pdata *pdata)
{
	struct mem_pool_st *mem_node;

	r5_mode_config(pdata);

	list_for_each_entry(mem_node, &pdata->mem_pools, node) {
		struct pd_id_st *pd_id;

		list_for_each_entry(pd_id, &mem_node->pd_ids, node)
			zynqmp_pm_request_node(pd_id->id,
					       ZYNQMP_PM_CAPABILITY_ACCESS,
				0, ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	}

	return 0;
}

/**
 * r5_release_tcm - release TCM
 * @pdata: platform data
 *
 * Release TCM
 */

static void r5_release_tcm(struct zynqmp_r5_rproc_pdata *pdata)
{
	struct mem_pool_st *mem_node;

	list_for_each_entry(mem_node, &pdata->mem_pools, node) {
		struct pd_id_st *pd_id;

		list_for_each_entry(pd_id, &mem_node->pd_ids, node)
			zynqmp_pm_release_node(pd_id->id);
	}
}

/**
 * disable_ipi - disable IPI
 * @pdata: platform data
 *
 * Disable IPI interrupt
 */
static inline void disable_ipi(struct zynqmp_r5_rproc_pdata *pdata)
{
	/* Disable R5 IPI interrupt */
	if (pdata->ipi_base)
		reg_write(pdata->ipi_base, IDR_OFFSET, pdata->ipi_dest_mask);
}

/**
 * enable_ipi - enable IPI
 * @pdata: platform data
 *
 * Enable IPI interrupt
 */
static inline void enable_ipi(struct zynqmp_r5_rproc_pdata *pdata)
{
	/* Enable R5 IPI interrupt */
	if (pdata->ipi_base)
		reg_write(pdata->ipi_base, IER_OFFSET, pdata->ipi_dest_mask);
}

/**
 * event_notified_idr_cb - event notified idr callback
 * @id: idr id
 * @ptr: pointer to idr private data
 * @data: data passed to idr_for_each callback
 *
 * Pass notification to remtoeproc virtio
 *
 * @return: 0. having return is to satisfy the idr_for_each() function
 *          pointer input argument requirement.
 */
static int event_notified_idr_cb(int id, void *ptr, void *data)
{
	struct rproc *rproc = data;
	(void)rproc_virtio_interrupt(rproc, id);
	return 0;
}

static void handle_event_notified(struct work_struct *work)
{
	struct rproc *rproc;
	struct zynqmp_r5_rproc_pdata *local = container_of(
				work, struct zynqmp_r5_rproc_pdata,
				workqueue);

	rproc = local->rproc;
	idr_for_each(&rproc->notifyids, event_notified_idr_cb, rproc);
}

static int zynqmp_r5_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;

	dev_dbg(dev, "%s\n", __func__);

	/*
	 * Use memory barrier to make sure all write memory operations
	 * complemeted.
	 */
	wmb();
	/* Set up R5 */
	if ((rproc->bootaddr & 0xF0000000) == 0xF0000000)
		local->bootmem = OCM;
	else
		local->bootmem = TCM;
	dev_info(dev, "RPU boot from %s.",
		 local->bootmem == OCM ? "OCM" : "TCM");

	r5_mode_config(local);
	zynqmp_pm_force_powerdown(local->rpu_pd_id,
				  ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	r5_boot_addr_config(local);
	/* Add delay before release from halt and reset */
	udelay(500);
	zynqmp_pm_request_wakeup(local->rpu_pd_id,
				 1, local->bootmem,
		ZYNQMP_PM_REQUEST_ACK_NO);

	/* Make sure IPI is enabled */
	enable_ipi(local);

	return 0;
}

/* kick a firmware */
static void zynqmp_r5_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);

	/*
	 * Use memory barrier to make sure write memory operations
	 * completed.
	 */
	wmb();
	/*
	 * send irq to R5 firmware
	 * Currently vqid is not used because we only got one.
	 */
	if (local->ipi_base)
		reg_write(local->ipi_base, TRIG_OFFSET, local->ipi_dest_mask);
}

/* power off the remote processor */
static int zynqmp_r5_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;
	struct rproc_mem_entry *mem, *nmem;

	dev_dbg(dev, "%s\n", __func__);

	disable_ipi(local);
	zynqmp_pm_force_powerdown(local->rpu_pd_id,
				  ZYNQMP_PM_REQUEST_ACK_BLOCKING);

	/* After it reset was once asserted, TCM will be initialized
	 * before it can be read. E.g. remoteproc virtio will access
	 * TCM if vdev rsc entry is in TCM after RPU stop.
	 * The following is to initialize the TCM.
	 */
	list_for_each_entry_safe(mem, nmem, &local->mems, node) {
		if ((mem->dma & 0xFFF00000) == 0xFFE00000)
			memset(mem->va, 0, mem->len);
	}

	return 0;
}

/* check if ZynqMP r5 is running */
static bool zynqmp_r5_rproc_is_running(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;

	dev_dbg(dev, "%s\n", __func__);

	return r5_is_running(local);
}

static void *zynqmp_r5_rproc_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct rproc_mem_entry *mem;
	void *va = NULL;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;

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

static struct rproc_ops zynqmp_r5_rproc_ops = {
	.start		= zynqmp_r5_rproc_start,
	.stop		= zynqmp_r5_rproc_stop,
	.is_running     = zynqmp_r5_rproc_is_running,
	.kick		= zynqmp_r5_rproc_kick,
	.da_to_va       = zynqmp_r5_rproc_da_to_va,
};

static int zynqmp_r5_rproc_add_mems(struct zynqmp_r5_rproc_pdata *pdata)
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
		/* TCM memory:
		 *   TCM_0: da 0 <-> global addr 0xFFE00000
		 *   TCM_1: da 0 <-> global addr 0xFFE90000
		 */
		if ((dma & 0xFFF00000) == 0xFFE00000) {
			mem->da = (dma & 0x000FFFFF);
			if ((dma & 0xFFF80000) == 0xFFE80000)
				mem->da -= 0x90000;
		} else {
			mem->da = dma;
		}
		dev_dbg(dev, "%s: va = %p, da = 0x%x dma = 0x%llx\n",
			__func__, va, mem->da, mem->dma);
		list_add_tail(&mem->node, &pdata->mems);
	}
	return 0;
}

/* Release R5 from reset and make it halted.
 * In case the firmware uses TCM, in order to load firmware to TCM,
 * will need to release R5 from reset and stay in halted state.
 */
static int zynqmp_r5_rproc_init(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	ret = r5_request_tcm(local);
	if (ret)
		return ret;

	enable_ipi(local);
	return zynqmp_r5_rproc_add_mems(local);
}

static irqreturn_t r5_remoteproc_interrupt(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct platform_device *pdev = to_platform_device(dev);
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;
	u32 ipi_reg;

	/* Check if there is a kick from R5 */
	ipi_reg = reg_read(local->ipi_base, ISR_OFFSET);
	if (!(ipi_reg & local->ipi_dest_mask))
		return IRQ_NONE;

	dev_dbg(dev, "KICK Linux because of pending message(irq%d)\n", irq);
	reg_write(local->ipi_base, ISR_OFFSET, local->ipi_dest_mask);
	schedule_work(&local->workqueue);

	return IRQ_HANDLED;
}

/*
 * Empty RSC table
 */
static struct resource_table r5_rproc_default_rsc_table = {
	.ver = 1,
	.num = 0,
};

/* Redefine r5 resource table to allow empty resource table */
static struct resource_table *r5_rproc_find_rsc_table(
			struct rproc *rproc,
			const struct firmware *fw,
			int *tablesz)
{
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;
	struct resource_table *rsc;

	rsc = local->default_fw_ops->find_rsc_table(rproc, fw, tablesz);
	if (!rsc) {
		*tablesz = sizeof(r5_rproc_default_rsc_table);
		return &r5_rproc_default_rsc_table;
	} else {
		return rsc;
	}
}

static int zynqmp_r5_remoteproc_probe(struct platform_device *pdev)
{
	const unsigned char *prop;
	struct resource *res;
	int ret = 0;
	struct zynqmp_r5_rproc_pdata *local;
	struct rproc *rproc;
	struct gen_pool *mem_pool = NULL;
	struct mem_pool_st *mem_node = NULL;
	int i;
	struct device_node *tmp_node;

	rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev),
			    &zynqmp_r5_rproc_ops, NULL,
		sizeof(struct zynqmp_r5_rproc_pdata));
	if (!rproc) {
		dev_err(&pdev->dev, "rproc allocation failed\n");
		return -ENOMEM;
	}
	local = rproc->priv;
	local->rproc = rproc;

	platform_set_drvdata(pdev, rproc);

	/* FIXME: it may need to extend to 64/48 bit */
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto rproc_fault;
	}

	/* Get the RPU power domain id */
	tmp_node = of_parse_phandle(pdev->dev.of_node, "pd-handle", 0);
	if (tmp_node) {
		of_property_read_u32(tmp_node, "pd-id", &local->rpu_pd_id);
	} else {
		dev_err(&pdev->dev, "No power domain ID is specified.\n");
		ret = -EINVAL;
		goto rproc_fault;
	}
	dev_dbg(&pdev->dev, "RPU[%d] pd_id = %d.\n",
		local->rpu_id, local->rpu_pd_id);

	prop = of_get_property(pdev->dev.of_node, "core_conf", NULL);
	if (!prop) {
		dev_warn(&pdev->dev, "default core_conf used: lock-step\n");
		prop = "lock-step";
	}

	dev_info(&pdev->dev, "RPU core_conf: %s\n", prop);
	if (!strcmp(prop, "split0")) {
		local->rpu_mode = SPLIT;
		local->rpu_id = 0;
		local->ipi_dest_mask = RPU_0_IPI_MASK;
	} else if (!strcmp(prop, "split1")) {
		local->rpu_mode = SPLIT;
		local->rpu_id = 1;
		local->ipi_dest_mask = RPU_1_IPI_MASK;
	} else if (!strcmp(prop, "lock-step")) {
		local->rpu_mode = LOCK_STEP;
		local->rpu_id = 0;
		local->ipi_dest_mask = RPU_0_IPI_MASK;
	} else {
		dev_err(&pdev->dev, "Invalid core_conf mode provided - %s , %d\n",
			prop, local->rpu_mode);
		ret = -EINVAL;
		goto rproc_fault;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "rpu_base");
	local->rpu_base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
	if (IS_ERR(local->rpu_base)) {
		dev_err(&pdev->dev, "Unable to map RPU I/O memory\n");
		ret = PTR_ERR(local->rpu_base);
		goto rproc_fault;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "rpu_glbl_base");
	local->rpu_glbl_base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
	if (IS_ERR(local->rpu_glbl_base)) {
		dev_err(&pdev->dev, "Unable to map RPU Global I/O memory\n");
		ret = PTR_ERR(local->rpu_glbl_base);
		goto rproc_fault;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ipi");
	if (res) {
		local->ipi_base = devm_ioremap(&pdev->dev, res->start,
				resource_size(res));
		if (IS_ERR(local->ipi_base)) {
			pr_err("%s: Unable to map IPI\n", __func__);
			ret = PTR_ERR(local->ipi_base);
			goto rproc_fault;
		}
	} else {
		dev_info(&pdev->dev, "IPI resource is not specified.\n");
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
				goto rproc_fault;
			mem_node->pool = mem_pool;
			/* Get the memory node power domain id */
			tmp_node = of_parse_phandle(pdev->dev.of_node,
						    srams_name, i);
			if (tmp_node) {
				struct device_node *pd_node;
				struct pd_id_st *pd_id;
				int j;

				INIT_LIST_HEAD(&mem_node->pd_ids);
				for (j = 0; ; j++) {
					pd_node = of_parse_phandle(tmp_node,
								   "pd-handle",
								   j);
					if (!pd_node)
						break;
					pd_id = devm_kzalloc(&pdev->dev,
							     sizeof(*pd_id),
							GFP_KERNEL);
					if (!pd_id) {
						ret = -ENOMEM;
						goto rproc_fault;
					}
					of_property_read_u32(pd_node,
							     "pd-id",
							     &pd_id->id);
					list_add_tail(&pd_id->node,
						      &mem_node->pd_ids);
					dev_dbg(&pdev->dev,
						"mem[%d] pd_id = %d.\n",
						i, pd_id->id);
				}
			}
			list_add_tail(&mem_node->node, &local->mem_pools);
		} else {
			break;
		}
	}

	/* Disable IPI before requesting IPI IRQ */
	disable_ipi(local);
	INIT_WORK(&local->workqueue, handle_event_notified);

	/* IPI IRQ */
	if (local->ipi_base) {
		ret = platform_get_irq(pdev, 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "unable to find IPI IRQ\n");
			goto rproc_fault;
		}
		local->vring0 = ret;
		ret = devm_request_irq(&pdev->dev, local->vring0,
				       r5_remoteproc_interrupt, IRQF_SHARED,
				       dev_name(&pdev->dev), &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "IRQ %d already allocated\n",
				local->vring0);
			goto rproc_fault;
		}
		dev_dbg(&pdev->dev, "vring0 irq: %d\n", local->vring0);
	}

	ret = zynqmp_r5_rproc_init(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "failed to init ZynqMP R5 rproc\n");
		goto rproc_fault;
	}

	rproc->auto_boot = autoboot;

	/* Set local firmware operations */
	memcpy(&local->fw_ops, rproc->fw_ops, sizeof(local->fw_ops));
	local->fw_ops.find_rsc_table = r5_rproc_find_rsc_table;
	local->default_fw_ops = rproc->fw_ops;
	rproc->fw_ops = &local->fw_ops;

	ret = rproc_add(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "rproc registration failed\n");
		goto rproc_fault;
	}

	return ret;

rproc_fault:
	rproc_free(local->rproc);

	return ret;
}

static int zynqmp_r5_remoteproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct zynqmp_r5_rproc_pdata *local = rproc->priv;
	struct rproc_mem_entry *mem;

	dev_info(&pdev->dev, "%s\n", __func__);

	rproc_del(rproc);

	list_for_each_entry(mem, &local->mems, node) {
		if (mem->priv)
			gen_pool_free((struct gen_pool *)mem->priv,
				      (unsigned long)mem->va, mem->len);
	}

	r5_release_tcm(local);

	rproc_free(rproc);

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id zynqmp_r5_remoteproc_match[] = {
	{ .compatible = "xlnx,zynqmp-r5-remoteproc-1.0", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_r5_remoteproc_match);

static struct platform_driver zynqmp_r5_remoteproc_driver = {
	.probe = zynqmp_r5_remoteproc_probe,
	.remove = zynqmp_r5_remoteproc_remove,
	.driver = {
		.name = "zynqmp_r5_remoteproc",
		.of_match_table = zynqmp_r5_remoteproc_match,
	},
};
module_platform_driver(zynqmp_r5_remoteproc_driver);

module_param_named(autoboot,  autoboot, bool, 0444);
MODULE_PARM_DESC(autoboot,
		 "enable | disable autoboot. (default: true)");

MODULE_AUTHOR("Jason Wu <j.wu@xilinx.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ZynqMP R5 remote processor control driver");
