/*
 * c67x00-drv.c: Cypress C67X00 USB Common infrastructure
 *
 * Copyright (C) 2006-2008 Barco N.V.
 *    Derived from the Cypress cy7c67200/300 ezusb linux driver and
 *    based on multiple host controller drivers inside the linux kernel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA.
 */

/*
 * This file implements the common infrastructure for using the c67x00.
 * It is both the link between the platform configuration and subdrivers and
 * the link between the common hardware parts and the subdrivers (e.g.
 * interrupt handling).
 *
 * The c67x00 has 2 SIE's (serial interface engine) which can be configured
 * to be host, device or OTG (with some limitations, E.G. only SIE1 can be OTG).
 *
 * Depending on the platform configuration, the SIE's are created and
 * the corresponding subdriver is initialized (c67x00_probe_sie).
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/c67x00.h>
#include <linux/of_platform.h>

#include "c67x00.h"
#include "c67x00-hcd.h"

static void c67x00_probe_sie(struct c67x00_sie *sie,
			     struct c67x00_device *dev, int sie_num)
{
	spin_lock_init(&sie->lock);
	sie->dev = dev;
	sie->sie_num = sie_num;
	sie->mode = c67x00_sie_config(dev->pdata->sie_config, sie_num);

	switch (sie->mode) {
	case C67X00_SIE_HOST:
		c67x00_hcd_probe(sie);
		break;

	case C67X00_SIE_UNUSED:
		dev_info(sie_dev(sie),
			 "Not using SIE %d as requested\n", sie->sie_num);
		break;

	default:
		dev_err(sie_dev(sie),
			"Unsupported configuration: 0x%x for SIE %d\n",
			sie->mode, sie->sie_num);
		break;
	}
}

static void c67x00_remove_sie(struct c67x00_sie *sie)
{
	switch (sie->mode) {
	case C67X00_SIE_HOST:
		c67x00_hcd_remove(sie);
		break;

	default:
		break;
	}
}

static irqreturn_t c67x00_irq(int irq, void *__dev)
{
	struct c67x00_device *c67x00 = __dev;
	struct c67x00_sie *sie;
	u16 msg, int_status;
	int i, count = 8;

	int_status = c67x00_ll_hpi_status(c67x00);
	if (!int_status)
		return IRQ_NONE;

	while (int_status != 0 && (count-- >= 0)) {
		c67x00_ll_irq(c67x00, int_status);
		for (i = 0; i < C67X00_SIES; i++) {
			sie = &c67x00->sie[i];
			msg = 0;
			if (int_status & SIEMSG_FLG(i))
				msg = c67x00_ll_fetch_siemsg(c67x00, i);
			if (sie->irq)
				sie->irq(sie, int_status, msg);
		}
		int_status = c67x00_ll_hpi_status(c67x00);
	}

	if (int_status)
		dev_warn(&c67x00->pdev->dev, "Not all interrupts handled! "
			 "status = 0x%04x\n", int_status);

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------------- */

static int c67x00_drv_probe(struct platform_device *pdev)
{
	struct c67x00_device *c67x00;
	struct c67x00_platform_data *pdata;
	struct resource *res;
	int ret, i, irq;

	c67x00 = devm_kzalloc(&pdev->dev, sizeof(*c67x00), GFP_KERNEL);
	if (!c67x00)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	c67x00->hpi.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(c67x00->hpi.base))
		return PTR_ERR(c67x00->hpi.base);

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		if (!pdev->dev.of_node)
			return -ENODEV;
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = of_property_read_u32(pdev->dev.of_node, "hpi-regstep",
					   &pdata->hpi_regstep);
		if (!ret)
			return ret;

		ret = of_property_read_u32(pdev->dev.of_node, "sie-config",
					   &pdata->sie_config);
		if (!ret)
			return ret;
	}

	spin_lock_init(&c67x00->hpi.lock);
	c67x00->hpi.regstep = pdata->hpi_regstep;
	c67x00->pdata = dev_get_platdata(&pdev->dev);
	c67x00->pdev = pdev;

	c67x00_ll_init(c67x00);
	c67x00_ll_hpi_reg_init(c67x00);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "irq resource not found\n");
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, c67x00_irq, 0, pdev->name,
			       c67x00);
	if (ret) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return ret;
	}

	ret = c67x00_ll_reset(c67x00);
	if (ret) {
		dev_err(&pdev->dev, "Device reset failed\n");
		return ret;
	}

	for (i = 0; i < C67X00_SIES; i++)
		c67x00_probe_sie(&c67x00->sie[i], c67x00, i);

	platform_set_drvdata(pdev, c67x00);

	return 0;
}

static int c67x00_drv_remove(struct platform_device *pdev)
{
	struct c67x00_device *c67x00 = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < C67X00_SIES; i++)
		c67x00_remove_sie(&c67x00->sie[i]);

	c67x00_ll_release(c67x00);

	return 0;
}

#ifdef CONFIG_OF
/* Match table for OF platform binding - from xilinx_emaclite */
static struct of_device_id c67x00_of_match[] = {
	{ .compatible = "cypress,c67x00", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, c67x00_of_match);
#endif

static struct platform_driver c67x00_driver = {
	.probe	= c67x00_drv_probe,
	.remove	= c67x00_drv_remove,
	.driver	= {
		.owner = THIS_MODULE,
		.name = "c67x00",
		.of_match_table = of_match_ptr(c67x00_of_match),
	},
};

module_platform_driver(c67x00_driver);

MODULE_AUTHOR("Peter Korsgaard, Jan Veldeman, Grant Likely");
MODULE_DESCRIPTION("Cypress C67X00 USB Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:c67x00");
