/*
 * Pl310 L2 Cache EDAC Driver
 *
 * Copyright (C) 2013-2014 Xilinx, Inc.
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
 */
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <asm/hardware/cache-l2x0.h>
#include "edac_core.h"

/* Auxilary control register definitions */
#define L2X0_AUX_CTRL_PARITY_MASK	BIT(21)

/* Interrupt imask/status/clear register definitions */
#define L2X0_INTR_PARRD_MASK	0x4
#define L2X0_INTR_PARRT_MASK	0x2

/**
 * struct pl310_edac_l2_priv - Zynq L2 cache controller private instance data
 * @base:		Base address of the controller
 * @irq:		Interrupt number
 */
struct pl310_edac_l2_priv {
	void __iomem *base;
	int irq;
};

/**
 * pl310_edac_l2_parityerr_check - Check controller staus for parity errors
 * @dci:	Pointer to the edac device controller instance
 *
 * This routine is used to check and post parity errors
 */
static void pl310_edac_l2_parityerr_check(struct edac_device_ctl_info *dci)
{
	struct pl310_edac_l2_priv *priv = dci->pvt_info;
	u32 regval;

	regval = readl(priv->base + L2X0_RAW_INTR_STAT);
	if (regval & L2X0_INTR_PARRD_MASK) {
		/* Data parity error will be reported as correctable error */
		writel(L2X0_INTR_PARRD_MASK, priv->base + L2X0_INTR_CLEAR);
		edac_device_handle_ce(dci, 0, 0, dci->ctl_name);
	}
	if (regval & L2X0_INTR_PARRT_MASK) {
		/* tag parity error will be reported as uncorrectable error */
		writel(L2X0_INTR_PARRT_MASK, priv->base + L2X0_INTR_CLEAR);
		edac_device_handle_ue(dci, 0, 0, dci->ctl_name);
	}
}

/**
 * pl310_edac_l2_int_handler - ISR fucntion for l2cahe controller
 * @irq:	Irq Number
 * @device:	Pointer to the edac device controller instance
 *
 * This routine is triggered whenever there is parity error detected
 *
 * Return: Always returns IRQ_HANDLED
 */
static irqreturn_t pl310_edac_l2_int_handler(int irq, void *device)
{
	pl310_edac_l2_parityerr_check((struct edac_device_ctl_info *)device);
	return IRQ_HANDLED;
}

/**
 * pl310_edac_l2_poll_handler - Poll the status reg for parity errors
 * @dci:	Pointer to the edac device controller instance
 *
 * This routine is used to check and post parity errors and is called by
 * the EDAC polling thread
 */
static void pl310_edac_l2_poll_handler(struct edac_device_ctl_info *dci)
{
	pl310_edac_l2_parityerr_check(dci);
}

/**
 * pl310_edac_l2_get_paritystate - check the parity enable/disable status
 * @base:	Pointer to the contoller base address
 *
 * This routine returns the parity enable/diable status for the controller
 *
 * Return: true/false -  parity enabled/disabled.
 */
static bool pl310_edac_l2_get_paritystate(void __iomem *base)
{
	u32 regval;

	regval = readl(base + L2X0_AUX_CTRL);
	if (regval & L2X0_AUX_CTRL_PARITY_MASK)
		return true;

	return false;
}

/**
 * pl310_edac_l2_probe - Check controller and bind driver
 * @pdev:	Pointer to the platform_device struct
 *
 * This routine probes a specific arm,pl310-cache instance for binding
 * with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int pl310_edac_l2_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci;
	struct pl310_edac_l2_priv *priv;
	int rc;
	struct resource *res;
	void __iomem *baseaddr;
	u32 regval;

	/* Get the data from the platform device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(baseaddr))
		return PTR_ERR(baseaddr);

	/* Check for the ecc enable status */
	if (pl310_edac_l2_get_paritystate(baseaddr) == false) {
		dev_err(&pdev->dev, "parity check not enabled\n");
		return -ENXIO;
	}

	dci = edac_device_alloc_ctl_info(sizeof(*priv), "l2cache",
					 1, "L", 1, 1, NULL, 0, 0);
	if (IS_ERR(dci))
		return PTR_ERR(dci);

	priv = dci->pvt_info;
	priv->base = baseaddr;
	dci->dev = &pdev->dev;
	dci->mod_name = "pl310_edac_l2";
	dci->ctl_name = "pl310_l2_controller";
	dci->dev_name = dev_name(&pdev->dev);

	priv->irq = platform_get_irq(pdev, 0);
	rc = devm_request_irq(&pdev->dev, priv->irq,
				pl310_edac_l2_int_handler,
				0, dev_name(&pdev->dev), (void *)dci);
	if (rc < 0) {
		dci->edac_check = pl310_edac_l2_poll_handler;
		edac_op_state = EDAC_OPSTATE_POLL;
	}

	rc = edac_device_add_device(dci);
	if (rc) {
		dev_err(&pdev->dev, "failed to register with EDAC core\n");
		goto del_edac_device;
	}

	if (edac_op_state != EDAC_OPSTATE_POLL) {
		regval = readl(priv->base+L2X0_INTR_MASK);
		regval |= (L2X0_INTR_PARRD_MASK | L2X0_INTR_PARRT_MASK);
		writel(regval, priv->base+L2X0_INTR_MASK);
	}

	return rc;

del_edac_device:
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return rc;
}

/**
 * pl310_edac_l2_remove - Unbind driver from controller
 * @pdev:	Pointer to the platform_device struct
 *
 * This routine unbinds the EDAC device controller instance associated
 * with the specified arm,pl310-cache controller described by the
 * OpenFirmware device tree node passed as a parameter.
 *
 * Return: Always returns 0
 */
static int pl310_edac_l2_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);
	struct pl310_edac_l2_priv *priv = dci->pvt_info;
	u32 regval;

	if (edac_op_state != EDAC_OPSTATE_POLL) {
		regval = readl(priv->base+L2X0_INTR_MASK);
		regval &= ~(L2X0_INTR_PARRD_MASK | L2X0_INTR_PARRT_MASK);
		writel(regval, priv->base+L2X0_INTR_MASK);
	}

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

/* Device tree node type and compatible tuples this driver can match on */
static struct of_device_id pl310_edac_l2_match[] = {
	{ .compatible = "arm,pl310-cache", },
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, pl310_edac_l2_match);

static struct platform_driver pl310_edac_l2_driver = {
	.driver = {
		 .name = "pl310-edac-l2",
		 .owner = THIS_MODULE,
		 .of_match_table = pl310_edac_l2_match,
	},
	.probe = pl310_edac_l2_probe,
	.remove = pl310_edac_l2_remove,
};

module_platform_driver(pl310_edac_l2_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("pl310 L2 EDAC driver");
MODULE_LICENSE("GPL v2");
