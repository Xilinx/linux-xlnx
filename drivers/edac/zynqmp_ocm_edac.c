/*
 * Xilinx ZynqMP OCM ECC Driver
 * This driver is based on mpc85xx_edac.c drivers
 *
 * Copyright (C) 2016 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include "edac_core.h"

#define ZYNQMP_OCM_EDAC_MSG_SIZE	256

#define ZYNQMP_OCM_EDAC_STRING	"zynqmp_ocm"
#define ZYNQMP_OCM_EDAC_MOD_VER	"1"

/* Controller registers */
#define CTRL_OFST		0x0
#define OCM_ISR_OFST		0x04
#define OCM_IMR_OFST		0x08
#define OCM_IEN_OFST		0x0C
#define OCM_IDS_OFST		0x10

/* ECC control register */
#define ECC_CTRL_OFST		0x14

/* Correctable error info registers */
#define CE_FFA_OFST		0x1C
#define CE_FFD0_OFST		0x20
#define CE_FFD1_OFST		0x24
#define CE_FFD2_OFST		0x28
#define CE_FFD3_OFST		0x2C
#define CE_FFE_OFST		0x30

/* Uncorrectable error info registers */
#define UE_FFA_OFST		0x34
#define UE_FFD0_OFST		0x38
#define UE_FFD1_OFST		0x3C
#define UE_FFD2_OFST		0x40
#define UE_FFD3_OFST		0x44
#define UE_FFE_OFST		0x48

/* ECC control register bit field definitions */
#define ECC_CTRL_CLR_CE_ERR	0x40
#define ECC_CTRL_CLR_UE_ERR	0x80

/* Fault injection data and count registers */
#define OCM_FID0_OFST		0x4C
#define OCM_FID1_OFST		0x50
#define OCM_FID2_OFST		0x54
#define OCM_FID3_OFST		0x58
#define OCM_FIC_OFST		0x74

/* Interrupt masks */
#define OCM_CEINTR_MASK	0x40
#define OCM_UEINTR_MASK	0x80
#define OCM_ECC_ENABLE_MASK	0x1
#define OCM_FICOUNT_MASK	0x0FFFFFFF
#define OCM_BASEVAL	0xFFFC0000
#define EDAC_DEVICE "ZynqMP-OCM"
#define OCM_CEUE_MASK	0xC0

/**
 * struct ecc_error_info - ECC error log information
 * @addr:	Fault generated at this address
 * @data0:	Generated fault data
 * @data1:	Generated fault data
 */
struct ecc_error_info {
	u32 addr;
	u32 data0;
	u32 data1;
};

/**
 * struct zynqmp_ocm_ecc_status - ECC status information to report
 * @ce_cnt:	Correctable error count
 * @ue_cnt:	Uncorrectable error count
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct zynqmp_ocm_ecc_status {
	u32 ce_cnt;
	u32 ue_cnt;
	struct ecc_error_info ceinfo;
	struct ecc_error_info ueinfo;
};

/**
 * struct zynqmp_ocm_edac_priv - DDR memory controller private instance data
 * @baseaddr:	Base address of the DDR controller
 * @message:	Buffer for framing the event specific info
 * @stat:	ECC status information
 * @p_data:	Pointer to platform data
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 * @ce_bitpos:	Bit position for Correctable Error
 * @ue_bitpos0:	First bit position for Uncorrectable Error
 * @ue_bitpos1:	Second bit position for Uncorrectable Error
 */
struct zynqmp_ocm_edac_priv {
	void __iomem *baseaddr;
	char message[ZYNQMP_OCM_EDAC_MSG_SIZE];
	struct zynqmp_ocm_ecc_status stat;
	const struct zynqmp_ocm_platform_data *p_data;
	u32 ce_cnt;
	u32 ue_cnt;
	u8 ce_bitpos;
	u8 ue_bitpos0;
	u8 ue_bitpos1;
};

/**
 * zynqmp_ocm_edac_geterror_info - Get the current ecc error info
 * @base:	Pointer to the base address of the ddr memory controller
 * @p:		Pointer to the ocm ecc status structure
 * @mask:	Status register mask value
 *
 * Determines there is any ecc error or not
 *
 */
static void zynqmp_ocm_edac_geterror_info(void __iomem *base,
				    struct zynqmp_ocm_ecc_status *p, int mask)
{
	if (mask & OCM_CEINTR_MASK) {
		p->ce_cnt++;
		p->ceinfo.data0 = readl(base + CE_FFD0_OFST);
		p->ceinfo.data1 = readl(base + CE_FFD1_OFST);
		p->ceinfo.addr = (OCM_BASEVAL | readl(base + CE_FFA_OFST));
		writel(ECC_CTRL_CLR_CE_ERR, base + OCM_ISR_OFST);
	} else if (mask & OCM_UEINTR_MASK) {
		p->ue_cnt++;
		p->ueinfo.data0 = readl(base + UE_FFD0_OFST);
		p->ueinfo.data1 = readl(base + UE_FFD1_OFST);
		p->ueinfo.addr = (OCM_BASEVAL | readl(base + UE_FFA_OFST));
		writel(ECC_CTRL_CLR_UE_ERR, base + OCM_ISR_OFST);
	}
}

/**
 * zynqmp_ocm_edac_handle_error - Handle controller error types CE and UE
 * @dci:	Pointer to the edac device controller instance
 * @p:		Pointer to the ocm ecc status structure
 *
 * Handles the controller ECC correctable and un correctable error.
 */
static void zynqmp_ocm_edac_handle_error(struct edac_device_ctl_info *dci,
				    struct zynqmp_ocm_ecc_status *p)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;
	struct ecc_error_info *pinf;

	if (p->ce_cnt) {
		pinf = &p->ceinfo;
		snprintf(priv->message, ZYNQMP_OCM_EDAC_MSG_SIZE,
			 "\n\rOCM ECC error type :%s\n\r"
			 "Addr: [0x%X]\n\rFault Data[31:0]: [0x%X]\n\r"
			 "Fault Data[63:32]: [0x%X]",
			 "CE", pinf->addr, pinf->data0, pinf->data1);
		edac_device_handle_ce(dci, 0, 0, priv->message);
	}

	if (p->ue_cnt) {
		pinf = &p->ueinfo;
		snprintf(priv->message, ZYNQMP_OCM_EDAC_MSG_SIZE,
			 "\n\rOCM ECC error type :%s\n\r"
			 "Addr: [0x%X]\n\rFault Data[31:0]: [0x%X]\n\r"
			 "Fault Data[63:32]: [0x%X]",
			 "UE", pinf->addr, pinf->data0, pinf->data1);
		edac_device_handle_ue(dci, 0, 0, priv->message);
	}

	memset(p, 0, sizeof(*p));
}

/**
 * zynqmp_ocm_edac_intr_handler - isr routine
 * @irq:        irq number
 * @dev_id:     device id poniter
 *
 * This is the Isr routine called by edac core interrupt thread.
 * Used to check and post ECC errors.
 *
 * Return: IRQ_NONE, if interrupt not set or IRQ_HANDLED otherwise
 */
static irqreturn_t zynqmp_ocm_edac_intr_handler(int irq, void *dev_id)
{
	struct edac_device_ctl_info *dci = dev_id;
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;
	int regval;

	regval = readl(priv->baseaddr + OCM_ISR_OFST);
	if (!(regval & OCM_CEUE_MASK))
		return IRQ_NONE;

	zynqmp_ocm_edac_geterror_info(priv->baseaddr,
				&priv->stat, regval);

	priv->ce_cnt += priv->stat.ce_cnt;
	priv->ue_cnt += priv->stat.ue_cnt;
	zynqmp_ocm_edac_handle_error(dci, &priv->stat);

	return IRQ_HANDLED;
}

/**
 * zynqmp_ocm_edac_get_eccstate - Return the controller ecc status
 * @base:	Pointer to the ddr memory controller base address
 *
 * Get the ECC enable/disable status for the controller
 *
 * Return: ecc status 0/1.
 */
static bool zynqmp_ocm_edac_get_eccstate(void __iomem *base)
{
	return readl(base + ECC_CTRL_OFST) & OCM_ECC_ENABLE_MASK;
}

static const struct of_device_id zynqmp_ocm_edac_match[] = {
	{ .compatible = "xlnx,zynqmp-ocmc-1.0"},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, zynqmp_ocm_edac_match);

/**
 * zynqmp_ocm_edac_inject_fault_count_show - Shows fault injection count
 * @dci:        Pointer to the edac device struct
 * @data:       Pointer to user data
 *
 * Shows the fault injection count, once the counter reaches
 * zero, it injects errors
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_fault_count_show(
		struct edac_device_ctl_info *dci, char *data)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "FIC: 0x%x\n\r",
			readl(priv->baseaddr + OCM_FIC_OFST));
}

/**
 * zynqmp_ocm_edac_inject_fault_count_store - write fi count
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Update the fault injection count register, once the counter reaches
 * zero, it injects errors
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_fault_count_store(
		struct edac_device_ctl_info *dci, const char *data,
		size_t count)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;
	u32 ficount;

	if (!data)
		return -EFAULT;

	if (kstrtoint(data, 0, &ficount))
		return -EINVAL;

	ficount &= OCM_FICOUNT_MASK;
	writel(ficount, priv->baseaddr + OCM_FIC_OFST);

	return count;
}

/**
 * zynqmp_ocm_edac_inject_cebitpos_show - Shows CE bit position
 * @dci:        Pointer to the edac device struct
 * @data:       Pointer to user data
 *
 * Shows the Correctable error bit position,
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_cebitpos_show(struct edac_device_ctl_info
							*dci, char *data)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	if (priv->ce_bitpos <= 31)
		return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID0_OFST))));

	return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID1_OFST))));
}

/**
 * zynqmp_ocm_edac_inject_cebitpos_store - Set CE bit postion
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Set any one bit to inject CE error
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_cebitpos_store(
		struct edac_device_ctl_info *dci, const char *data,
		size_t count)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	if (!data)
		return -EFAULT;

	if (kstrtou8(data, 0, &priv->ce_bitpos))
		return -EINVAL;

	if (priv->ce_bitpos <= 31) {
		writel(1 << priv->ce_bitpos, priv->baseaddr + OCM_FID0_OFST);
		writel(0, priv->baseaddr + OCM_FID1_OFST);
	} else if (priv->ce_bitpos >= 32 && priv->ce_bitpos <= 63) {
		writel(1 << (priv->ce_bitpos - 32),
				priv->baseaddr + OCM_FID1_OFST);
		writel(0, priv->baseaddr + OCM_FID0_OFST);
	} else {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			"Bit number > 64 is not valid\n");
	}

	return count;
}

/**
 * zynqmp_ocm_edac_inject_uebitpos0_show - Shows UE bit postion0
 * @dci:        Pointer to the edac device struct
 * @data:       Pointer to user data
 *
 * Shows the one of bit position for UE error
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_uebitpos0_show(
		struct edac_device_ctl_info *dci, char *data)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	if (priv->ue_bitpos0 <= 31)
		return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID0_OFST))));

	return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID1_OFST))));
}

/**
 * zynqmp_ocm_edac_inject_uebitpos0_store - set UE bit position0
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Set the first bit postion for UE Error generation,we need to configure
 * any two bitpositions to inject UE Error
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_uebitpos0_store(
		struct edac_device_ctl_info *dci,
		const char *data, size_t count)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	if (!data)
		return -EFAULT;

	if (kstrtou8(data, 0, &priv->ue_bitpos0))
		return -EINVAL;

	if (priv->ue_bitpos0 <= 31)
		writel(1 << priv->ue_bitpos0, priv->baseaddr + OCM_FID0_OFST);
	else if (priv->ue_bitpos0 >= 32 && priv->ue_bitpos0 <= 63)
		writel(1 << (priv->ue_bitpos0 - 32),
				priv->baseaddr + OCM_FID1_OFST);
	else
		edac_printk(KERN_ERR, EDAC_DEVICE,
			"Bit position > 64 is not valid\n");
	edac_printk(KERN_INFO, EDAC_DEVICE,
			"Set another bit position for UE\n");
	return count;
}

/**
 * zynqmp_ocm_edac_inject_uebitpos1_show - Shows UE bit postion1
 * @dci:        Pointer to the edac device struct
 * @data:       Pointer to user data
 *
 * Shows the second bit postion configured for UE error
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_uebitpos1_show(
		struct edac_device_ctl_info *dci, char *data)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	if (priv->ue_bitpos1 <= 31)
		return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID0_OFST))));

	return sprintf(data, "Fault Injection Data Reg: [0x%x]\n\r",
			((readl(priv->baseaddr + OCM_FID1_OFST))));

}

/**
 * zynqmp_ocm_edac_inject_uebitposition1_store - Set UE second bit postion
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Set the second bit postion for UE Error generation,we need to configure
 * any two bitpositions to inject UE Error
 * Return: Number of bytes copied.
 */
static ssize_t zynqmp_ocm_edac_inject_uebitpos1_store(
		struct edac_device_ctl_info *dci, const char *data,
		size_t count)
{
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;
	u32 mask;

	if (!data)
		return -EFAULT;

	if (kstrtou8(data, 0, &priv->ue_bitpos1))
		return -EINVAL;

	if (priv->ue_bitpos0 == priv->ue_bitpos1) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
				"Bit positions should not be equal\n");
		return -EINVAL;
	}

	/* If both bit postions are referring to 32 bit data, then configure
	 * only FID0 register or if it is 64 bit data, then configure only
	 * FID1 register.
	 */
	if (priv->ue_bitpos0 <= 31 &&
	    priv->ue_bitpos1 <= 31) {
		mask = (1 << priv->ue_bitpos0);
		mask |= (1 << priv->ue_bitpos1);
		writel(mask, priv->baseaddr + OCM_FID0_OFST);
		writel(0, priv->baseaddr + OCM_FID1_OFST);
	} else if ((priv->ue_bitpos0 >= 32 && priv->ue_bitpos0 <= 63) &&
			(priv->ue_bitpos1 >= 32 && priv->ue_bitpos1 <= 63)) {
		mask = (1 << (priv->ue_bitpos0 - 32));
		mask |= (1 << (priv->ue_bitpos1 - 32));
		writel(mask, priv->baseaddr + OCM_FID1_OFST);
		writel(0, priv->baseaddr + OCM_FID0_OFST);
	}

	/* If one bit position is referring a bit in 32 bit data and other in
	 * 64 bit data, just configure FID0/FID1 based on uebitpos1.
	 */
	if ((priv->ue_bitpos0 <= 31) &&
	    (priv->ue_bitpos1 >= 32 && priv->ue_bitpos1 <= 63)) {
		writel(1 << (priv->ue_bitpos1 - 32),
				priv->baseaddr + OCM_FID1_OFST);
	} else if ((priv->ue_bitpos0 >= 32 && priv->ue_bitpos0 <= 63) &&
			(priv->ue_bitpos1 <= 31)) {
		writel(1 << priv->ue_bitpos1,
				priv->baseaddr + OCM_FID0_OFST);
	} else {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			"Bit position > 64 is not valid, Valid bits:[63:0]\n");
	}

	edac_printk(KERN_INFO, EDAC_DEVICE,
			"UE at Bit Position0: %d Bit Position1: %d\n",
			priv->ue_bitpos0, priv->ue_bitpos1);
	return count;
}

static struct edac_dev_sysfs_attribute zynqmp_ocm_edac_sysfs_attributes[] = {
	{
		.attr = {
			.name = "inject_cebitpos",
			.mode = (0644)
		},
		.show = zynqmp_ocm_edac_inject_cebitpos_show,
		.store = zynqmp_ocm_edac_inject_cebitpos_store},
	{
		.attr = {
			.name = "inject_uebitpos0",
			.mode = (0644)
		},
		.show = zynqmp_ocm_edac_inject_uebitpos0_show,
		.store = zynqmp_ocm_edac_inject_uebitpos0_store},
	{
		.attr = {
			.name = "inject_uebitpos1",
			.mode = (0644)
		},
		.show = zynqmp_ocm_edac_inject_uebitpos1_show,
		.store = zynqmp_ocm_edac_inject_uebitpos1_store},
	{
		.attr = {
			.name = "inject_fault_count",
			.mode = (0644)
		},
		.show = zynqmp_ocm_edac_inject_fault_count_show,
		.store = zynqmp_ocm_edac_inject_fault_count_store},
	/* End of list */
	{
		.attr = {.name = NULL}
	}
};

/**
 * zynqmp_set_ocm_edac_sysfs_attributes - create sysfs attributes
 * @edac_dev:	Pointer to the edac device struct
 *
 * Creates sysfs entires for error injection
 * Return: None.
 */
static void zynqmp_set_ocm_edac_sysfs_attributes(struct edac_device_ctl_info
						*edac_dev)
{
	edac_dev->sysfs_attributes = zynqmp_ocm_edac_sysfs_attributes;
}

/**
 * zynqmp_ocm_edac_probe - Check controller and bind driver
 * @pdev:	Pointer to the platform_device struct
 *
 * Probes a specific controller instance for binding with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int zynqmp_ocm_edac_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci;
	struct zynqmp_ocm_edac_priv *priv;
	int irq, status;
	struct resource *res;
	void __iomem *baseaddr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(baseaddr))
		return PTR_ERR(baseaddr);

	if (!zynqmp_ocm_edac_get_eccstate(baseaddr)) {
		edac_printk(KERN_INFO, EDAC_DEVICE,
				"ECC not enabled - Disabling EDAC driver\n");
		return -ENXIO;
	}

	dci = edac_device_alloc_ctl_info(sizeof(*priv), ZYNQMP_OCM_EDAC_STRING,
			1, ZYNQMP_OCM_EDAC_STRING, 1, 0, NULL, 0,
			edac_device_alloc_index());
	if (!dci) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
				"Unable to allocate EDAC device\n");
		return -ENOMEM;
	}

	priv = dci->pvt_info;
	platform_set_drvdata(pdev, dci);
	dci->dev = &pdev->dev;
	priv->baseaddr = baseaddr;
	dci->mod_name = pdev->dev.driver->name;
	dci->ctl_name = ZYNQMP_OCM_EDAC_STRING;
	dci->dev_name = dev_name(&pdev->dev);

	zynqmp_set_ocm_edac_sysfs_attributes(dci);
	if (edac_device_add_device(dci))
		goto free_dev_ctl;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
				"No irq %d in DT\n", irq);
		return irq;
	}

	status = devm_request_irq(&pdev->dev, irq,
		zynqmp_ocm_edac_intr_handler,
		0, dev_name(&pdev->dev), dci);
	if (status < 0) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Failed to request Irq\n");
		goto free_edac_dev;
	}

	writel(OCM_CEUE_MASK, priv->baseaddr + OCM_IEN_OFST);

	return 0;

free_edac_dev:
	edac_device_del_device(&pdev->dev);
free_dev_ctl:
	edac_device_free_ctl_info(dci);

	return -1;
}

/**
 * zynqmp_ocm_edac_remove - Unbind driver from controller
 * @pdev:	Pointer to the platform_device struct
 *
 * Return: Unconditionally 0
 */
static int zynqmp_ocm_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);
	struct zynqmp_ocm_edac_priv *priv = dci->pvt_info;

	writel(OCM_CEUE_MASK, priv->baseaddr + OCM_IDS_OFST);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

static struct platform_driver zynqmp_ocm_edac_driver = {
	.driver = {
		   .name = "zynqmp-ocm-edac",
		   .of_match_table = zynqmp_ocm_edac_match,
		   },
	.probe = zynqmp_ocm_edac_probe,
	.remove = zynqmp_ocm_edac_remove,
};

module_platform_driver(zynqmp_ocm_edac_driver);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("ZynqMP OCM ECC driver");
MODULE_LICENSE("GPL v2");
