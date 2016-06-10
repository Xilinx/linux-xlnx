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

#include "remoteproc_internal.h"

/* Register offset definitions for RPU. */
#define RPU_GLBL_CNTL_OFFSET	0x00000000 /* RPU control */
#define RPU_0_CFG_OFFSET	0x00000100 /* RPU0 configuration */
#define RPU_1_CFG_OFFSET	0x00000200 /* RPU1 Configuration */
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

/* Clock controller low power domain (CRL_APB) for RPU */
#define CPU_R5_CTRL_OFFSET	0x00000090 /* RPU Global Control*/
#define RST_LPD_TOP_OFFSET	0x0000023C /* LPD block */
#define RPU0_RESET_BIT		BIT(0) /* RPU CPU0 reset bit */
#define RPU_AMBA_RST_MASK	BIT(2) /* RPU AMBA reset bit */
#define RPU_CLKACT_MASK		BIT(24) /* RPU clock active bit */

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
#define RPU_IPI_MASK(n)		(RPU_IPI_INIT_MASK << n)
#define RPU_0_IPI_MASK		RPU_IPI_MASK(0)
#define RPU_1_IPI_MASK		RPU_IPI_MASK(1)

/* Store rproc for IPI handler */
static struct platform_device *remoteprocdev[MAX_INSTANCES];

/* Register access macros */
#define reg_read(base, reg) \
	readl(((void __iomem *)(base)) + (reg))
#define reg_write(base, reg, val) \
	writel((val), ((void __iomem *)(base)) + (reg))

#define DEFAULT_FIRMWARE_NAME	"rproc-rpu-fw"

/* Module parameter */
static char *firmware = "r5_0_firmware";
static char *firmware1 = "r5_1_firmware";

struct zynqmp_r5_rproc_pdata;

/* enumerations for RPU/IPI control methods */
enum control_method {
	SMC = 0,
	HVC,
	HW,
};

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

/**
 * struct zynqmp_r5_rproc_pdata - zynqmp rpu remote processor instance state
 * @rproc: rproc handle
 * @workqueue: workqueue for the RPU remoteproc
 * @rpu_base: virt ptr to RPU control address registers
 * @crl_apb_base: virt ptr to CRL_APB address registers for RPU
 * @ipi_base: virt ptr to IPI channel address registers for APU
 * @rpu_mode: RPU core configuration
 * @rpu_id: RPU CPU id
 * @bootmem: RPU boot memory device used
 * @vring0: IRQ number used for vring0
 * @ipi_dest_mask: IPI destination mask for the IPI channel
 */
struct zynqmp_r5_rproc_pdata {
	struct rproc *rproc;
	struct work_struct workqueue;
	void __iomem *rpu_base;
	void __iomem *crl_apb_base;
	void __iomem *ipi_base;
	enum rpu_core_conf rpu_mode;
	enum rpu_bootmem bootmem;
	u32 ipi_dest_mask;
	u32 rpu_id;
	u32 vring0;
};

/* Get firmware ELF file entry address */
static int get_firmware_entry_addr(struct zynqmp_r5_rproc_pdata *pdata,
			u32 *elf_entry_p)
{
	struct elf32_hdr *ehdr = 0;
	const struct firmware *firmware_p;
	struct rproc *rproc = pdata->rproc;
	int ret;

	ret = request_firmware(&firmware_p, rproc->firmware, &rproc->dev);
	if (ret < 0) {
		dev_err(&rproc->dev, "%s: request_firmware failed: %d\n",
			__func__, ret);
		return ret;
	}
	ehdr = (struct elf32_hdr *)firmware_p->data;
	*elf_entry_p = (unsigned int)ehdr->e_entry;
	release_firmware(firmware_p);
	return 0;
}

/**
 * r5_boot_addr_config - configure the boot address of R5:w
 * @pdata: platform data
 *
 * This function will set the boot address based on if the
 * boot memory in the ELF file is TCM or OCM
 */
static void r5_boot_addr_config(struct zynqmp_r5_rproc_pdata *pdata)
{
	u32 tmp;
	u32 offset = RPU_1_CFG_OFFSET;

	pr_debug("%s: R5 ID: %d, boot_dev %d\n",
			 __func__, pdata->rpu_id, pdata->bootmem);
	if (pdata->rpu_id == 0)
		offset = RPU_0_CFG_OFFSET;

	tmp = reg_read(pdata->rpu_base, offset);
	if (pdata->bootmem == OCM)
		tmp |= VINITHI_BIT;
	else
		tmp &= ~VINITHI_BIT;
	reg_write(pdata->rpu_base, offset, tmp);
}

/**
 * r5_reset - change the R5 reset bit
 * @pdata: platform data
 * @do_reset: 1 to reset, 0 to release reset
 *
 * If the do_reset is 1, the function to set the
 * R5 reset bit. It do_reset is 0, the function
 * will clear the reset bit.
 */
static void r5_reset(struct zynqmp_r5_rproc_pdata *pdata,
			bool do_reset)
{
	u32 tmp;

	pr_debug("%s: R5 ID: %d, reset %d\n", __func__, pdata->rpu_id,
			 do_reset);
	tmp = reg_read(pdata->crl_apb_base, RST_LPD_TOP_OFFSET);
	if (do_reset)
		tmp |= (RPU0_RESET_BIT << pdata->rpu_id);
	else
		tmp &= ~((RPU0_RESET_BIT << pdata->rpu_id) | RPU_AMBA_RST_MASK);
	reg_write(pdata->crl_apb_base, RST_LPD_TOP_OFFSET, tmp);
}

/**
 * r5_halt - change the R5 halt bit
 * @pdata: platform data
 * @do_halt: 1 to halt, 0 to release halt
 *
 * If the do_halt is 1, the function to set the
 * R5 halt bit. It do_halt is 0, the function
 * will clear the halt bit.
 */
static void r5_halt(struct zynqmp_r5_rproc_pdata *pdata,
						bool do_halt)
{
	u32 tmp;
	u32 offset = RPU_1_CFG_OFFSET;

	pr_debug("%s: R5 ID: %d, halt %d\n", __func__, pdata->rpu_id,
			 do_halt);
	if (pdata->rpu_id == 0)
		offset = RPU_0_CFG_OFFSET;

	tmp = reg_read(pdata->rpu_base, offset);
	if (do_halt)
		tmp &= ~nCPUHALT_BIT;
	else
		tmp |= nCPUHALT_BIT;
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
	tmp = reg_read(pdata->rpu_base, 0);
	if (pdata->rpu_mode == SPLIT) {
		tmp |= SLSPLIT_BIT;
		tmp &= ~TCM_COMB_BIT;
		tmp &= ~SLCLAMP_BIT;
	} else {
		tmp &= ~SLSPLIT_BIT;
		tmp |= TCM_COMB_BIT;
		tmp |= SLCLAMP_BIT;
	}
	reg_write(pdata->rpu_base, 0, tmp);
}

/**
 * r5_enable_clock - enable R5 clock
 * @pdata: platform data
 *
 * enable R5 clock if it is disabled.
 */
static void r5_enable_clock(struct zynqmp_r5_rproc_pdata *pdata)
{
	u32 tmp;

	pr_debug("%s: mode: %d\n", __func__, pdata->rpu_mode);
	tmp = reg_read(pdata->crl_apb_base, CPU_R5_CTRL_OFFSET);
	if (!(tmp & RPU_CLKACT_MASK)) {
		tmp |= RPU_CLKACT_MASK;
		reg_write(pdata->crl_apb_base, CPU_R5_CTRL_OFFSET, tmp);
		/* Give some delay for clock to propogate */
		udelay(500);
	}
}

/**
 * ipi_init - Initialize R5 IPI
 * @pdata: platform data
 *
 * Clear IPI interrupt status register and then enable IPI interrupt.
 */
static void ipi_init(struct zynqmp_r5_rproc_pdata *pdata)
{
	pr_debug("%s\n", __func__);
	/* Disable R5 IPI interrupt */
	reg_write(pdata->ipi_base, IDR_OFFSET, pdata->ipi_dest_mask);
	/* Clear R5 IPI interrupt */
	reg_write(pdata->ipi_base, ISR_OFFSET, pdata->ipi_dest_mask);
	/* Enable R5 IPI interrupt */
	reg_write(pdata->ipi_base, IER_OFFSET, pdata->ipi_dest_mask);
}

static void handle_event(struct zynqmp_r5_rproc_pdata *local)
{
	if (rproc_vq_interrupt(local->rproc, 0) == IRQ_NONE)
		dev_dbg(&remoteprocdev[local->rpu_id]->dev, \
			"no message found in vqid 0\n");
}

static void handle_event0(struct work_struct *work)
{
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(remoteprocdev[0]);

	handle_event(local);
}

static void handle_event1(struct work_struct *work)
{
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(remoteprocdev[1]);

	handle_event(local);
}

static int zynqmp_r5_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);
	u32 bootaddr = 0;
	int ret;

	dev_dbg(dev, "%s\n", __func__);
	/* limit to two RPU support */
	if (local->rpu_id == 0)
		INIT_WORK(&local->workqueue, handle_event0);
	else
		INIT_WORK(&local->workqueue, handle_event1);

	remoteprocdev[local->rpu_id] = pdev;

	/*
	 * Use memory barrier to make sure all write memory operations
	 * complemeted.
	 */
	wmb();
	/* Set up R5 */
	ret = get_firmware_entry_addr(local, &bootaddr);
	if (ret < 0) {
		dev_err(dev, "%s: failed to get RPU boot addr.\n", __func__);
		return ret;
	}
	if (!bootaddr)
		local->bootmem = TCM;
	else
		local->bootmem = OCM;
	dev_info(dev, "RPU boot from %s.",
		local->bootmem == OCM ? "OCM" : "TCM");

	r5_mode_config(local);
	r5_halt(local, true);
	r5_reset(local, true);
	r5_boot_addr_config(local);
	/* Add delay before release from halt and reset */
	udelay(500);
	r5_reset(local, false);
	r5_halt(local, false);

	ipi_init(local);
	return 0;
}

/* kick a firmware */
static void zynqmp_r5_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);

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
	reg_write(local->ipi_base, TRIG_OFFSET, local->ipi_dest_mask);
}

/* power off the remote processor */
static int zynqmp_r5_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s\n", __func__);

	r5_halt(local, true);
	r5_reset(local, true);

	reg_write(local->ipi_base, IDR_OFFSET, local->ipi_dest_mask);
	reg_write(local->ipi_base, ISR_OFFSET, local->ipi_dest_mask);
	return 0;
}

static struct rproc_ops zynqmp_r5_rproc_ops = {
	.start		= zynqmp_r5_rproc_start,
	.stop		= zynqmp_r5_rproc_stop,
	.kick		= zynqmp_r5_rproc_kick,
};

/* Release R5 from reset and make it halted.
 * In case the firmware uses TCM, in order to load firmware to TCM,
 * will need to release R5 from reset and stay in halted state.
 */
static void zynqmp_r5_rproc_init(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s\n", __func__);

	r5_mode_config(local);
	r5_halt(local, true);
	r5_reset(local, false);
	r5_enable_clock(local);
}

static irqreturn_t r5_remoteproc_interrupt(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct platform_device *pdev = to_platform_device(dev);
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);
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

static int zynqmp_r5_remoteproc_probe(struct platform_device *pdev)
{
	const unsigned char *prop;
	struct resource *res;
	int ret = 0;
	int method = 0;
	char *rproc_firmware = 0;
	struct zynqmp_r5_rproc_pdata *local;

	local = devm_kzalloc(&pdev->dev, sizeof(struct zynqmp_r5_rproc_pdata),
				 GFP_KERNEL);
	if (!local)
		return -ENOMEM;

	platform_set_drvdata(pdev, local);

	/* FIXME: it may need to extend to 64/48 bit */
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto dma_mask_fault;
	}

	prop = of_get_property(pdev->dev.of_node, "core_conf", NULL);
	if (!prop) {
		dev_warn(&pdev->dev, "default core_conf used: lock-step\n");
		prop = "lock-step";
	}

	dev_info(&pdev->dev, "RPU core_conf: %s\n", prop);
	if (!strcmp(prop, "split0")) {
		local->rpu_mode = SPLIT;
		local->rpu_id = 0;
	} else if (!strcmp(prop, "split1")) {
		local->rpu_mode = SPLIT;
		local->rpu_id = 1;
	} else if (!strcmp(prop, "lock-step")) {
		local->rpu_mode = LOCK_STEP;
		local->rpu_id = 0;
	} else {
		dev_err(&pdev->dev, "Invalid core_conf mode provided - %s , %d\n",
			prop, local->rpu_mode);
		ret = -EINVAL;
		goto dma_mask_fault;
	}

	prop = of_get_property(pdev->dev.of_node, "method", NULL);
	if (!prop) {
		dev_warn(&pdev->dev, "default method used: smc\n");
		prop = "direct";
	}

	/* Handle direct hardware access */
	/* (TODO: remove once RPU and IPI drivers are ready ) */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
		"rpu_base");
	local->rpu_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(local->rpu_base)) {
		dev_err(&pdev->dev, "Unable to map RPU I/O memory\n");
		ret = PTR_ERR(local->rpu_base);
		goto dma_mask_fault;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
		"apb_base");
	local->crl_apb_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(local->crl_apb_base)) {
		dev_err(&pdev->dev, "Unable to map CRL_APB I/O memory\n");
		ret = PTR_ERR(local->crl_apb_base);
		goto dma_mask_fault;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ipi");
	local->ipi_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(local->ipi_base)) {
		pr_err("%s: Unable to map IPI\n", __func__);
		ret = PTR_ERR(local->ipi_base);
		goto dma_mask_fault;
	}

	/* IPI IRQ */
	local->vring0 = platform_get_irq(pdev, 0);
	if (local->vring0 < 0) {
		ret = local->vring0;
		dev_err(&pdev->dev, "unable to find IPI IRQ\n");
		goto dma_mask_fault;
	}
	ret = devm_request_irq(&pdev->dev, local->vring0,
		r5_remoteproc_interrupt, IRQF_SHARED, dev_name(&pdev->dev),
		&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "IRQ %d already allocated\n",
			local->vring0);
		goto dma_mask_fault;
	}
	dev_dbg(&pdev->dev, "vring0 irq: %d\n", local->vring0);

	if (local->rpu_id == 0) {
		local->ipi_dest_mask = RPU_0_IPI_MASK;
		rproc_firmware = firmware;
	} else {
		local->ipi_dest_mask = RPU_1_IPI_MASK;
		rproc_firmware = firmware1;
	}

	dev_dbg(&pdev->dev, "Using firmware: %s\n", rproc_firmware);
	local->rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev),
		&zynqmp_r5_rproc_ops, rproc_firmware, sizeof(struct rproc));
	if (!local->rproc) {
		dev_err(&pdev->dev, "rproc allocation failed\n");
		goto rproc_fault;
	}

	zynqmp_r5_rproc_init(local->rproc);
	ret = rproc_add(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "rproc registration failed\n");
		goto rproc_fault;
	}

	return ret;

rproc_fault:
	rproc_put(local->rproc);

dma_mask_fault:
	dma_release_declared_memory(&pdev->dev);

	return ret;
}

static int zynqmp_r5_remoteproc_remove(struct platform_device *pdev)
{
	struct zynqmp_r5_rproc_pdata *local = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s\n", __func__);

	rproc_del(local->rproc);
	rproc_put(local->rproc);

	dma_release_declared_memory(&pdev->dev);

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

module_param(firmware, charp, 0);
module_param(firmware1, charp, 0);
MODULE_PARM_DESC(firmware, "Override the RPU-0 firmware image name.");
MODULE_PARM_DESC(firmware1, "Override the RPU-1 firmware image name.");

MODULE_AUTHOR("Jason Wu <j.wu@xilinx.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ZynqMP R5 remote processor control driver");
