/**
 * dwc3-of-simple.c - OF glue layer for simple integrations
 *
 * Copyright (c) 2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This is a combination of the old dwc3-qcom.c by Ivan T. Ivanov
 * <iivanov@mm-sol.com> and the original patch adding support for Xilinx' SoC
 * by Subbaraya Sundeep Bhatta <subbaraya.sundeep.bhatta@xilinx.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/soc/xilinx/zynqmp/fw.h>
#include <linux/slab.h>

#include <linux/phy/phy-zynqmp.h>
#include <linux/of_address.h>

#include "core.h"

/* Xilinx USB 3.0 IP Register */
#define XLNX_USB_COHERENCY		0x005C
#define XLNX_USB_COHERENCY_ENABLE	0x1

/* ULPI control registers */
#define ULPI_OTG_CTRL_SET		0xB
#define ULPI_OTG_CTRL_CLEAR		0XC
#define OTG_CTRL_DRVVBUS_OFFSET		5

#define DWC3_OF_ADDRESS(ADDR)		((ADDR) - DWC3_GLOBALS_REGS_START)

struct dwc3_of_simple {
	struct device		*dev;
	struct clk		**clks;
	int			num_clocks;
	void __iomem		*regs;
	struct dwc3		*dwc;
	bool			wakeup_capable;
	bool			dis_u3_susphy_quirk;
};

void dwc3_set_phydata(struct device *dev, struct phy *phy)
{
	struct device_node *node = of_get_parent(dev->of_node);
	int ret;

	if ((node != NULL) &&
		of_device_is_compatible(node, "xlnx,zynqmp-dwc3")) {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple   *simple;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		/* assign USB vendor regs to phy lane */
		ret = xpsgtr_set_protregs(phy, simple->regs);
		if (ret) {
			dev_err(&pdev_parent->dev,
				"Not able to set PHY data\n");
		}
	}
}
EXPORT_SYMBOL(dwc3_set_phydata);

int dwc3_enable_hw_coherency(struct device *dev)
{
	struct device_node *node = of_get_parent(dev->of_node);

	if (of_device_is_compatible(node, "xlnx,zynqmp-dwc3")) {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple *simple;
		void __iomem *regs;
		u32 reg;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);
		regs = simple->regs;

		reg = readl(regs + XLNX_USB_COHERENCY);
		reg |= XLNX_USB_COHERENCY_ENABLE;
		writel(reg, regs + XLNX_USB_COHERENCY);
	}

	return 0;
}
EXPORT_SYMBOL(dwc3_enable_hw_coherency);

void dwc3_set_simple_data(struct dwc3 *dwc)
{
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	if (node && of_device_is_compatible(node, "xlnx,zynqmp-dwc3")) {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple   *simple;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		/* Set (struct dwc3 *) to simple->dwc for future use */
		simple->dwc =  dwc;
	}
}
EXPORT_SYMBOL(dwc3_set_simple_data);

void dwc3_simple_check_quirks(struct dwc3 *dwc)
{
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	if (node && of_device_is_compatible(node, "xlnx,zynqmp-dwc3")) {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple   *simple;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		/* Add snps,dis_u3_susphy_quirk */
		dwc->dis_u3_susphy_quirk = simple->dis_u3_susphy_quirk;
	}
}
EXPORT_SYMBOL(dwc3_simple_check_quirks);

void dwc3_simple_wakeup_capable(struct device *dev, bool wakeup)
{
	struct device_node *node =
		of_find_compatible_node(dev->of_node, NULL, "xlnx,zynqmp-dwc3");

	if (node)  {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple   *simple;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		/* Set wakeup capable as true or false */
		simple->wakeup_capable = wakeup;
	}
}
EXPORT_SYMBOL(dwc3_simple_wakeup_capable);

static int dwc3_of_simple_clk_init(struct dwc3_of_simple *simple, int count)
{
	struct device		*dev = simple->dev;
	struct device_node	*np = dev->of_node;
	int			i;

	simple->num_clocks = count;

	if (!count)
		return 0;

	simple->clks = devm_kcalloc(dev, simple->num_clocks,
			sizeof(struct clk *), GFP_KERNEL);
	if (!simple->clks)
		return -ENOMEM;

	for (i = 0; i < simple->num_clocks; i++) {
		struct clk	*clk;
		int		ret;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			while (--i >= 0)
				clk_put(simple->clks[i]);
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			while (--i >= 0) {
				clk_disable_unprepare(simple->clks[i]);
				clk_put(simple->clks[i]);
			}
			clk_put(clk);

			return ret;
		}

		simple->clks[i] = clk;
	}

	return 0;
}

static int dwc3_of_simple_probe(struct platform_device *pdev)
{
	struct dwc3_of_simple	*simple;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;

	int			ret;
	int			i;

	simple = devm_kzalloc(dev, sizeof(*simple), GFP_KERNEL);
	if (!simple)
		return -ENOMEM;

	platform_set_drvdata(pdev, simple);
	simple->dev = dev;

	if (of_device_is_compatible(pdev->dev.of_node,
				    "xlnx,zynqmp-dwc3")) {

		char			*soc_rev;
		struct resource		*res;
		void __iomem		*regs;

		res = platform_get_resource(pdev,
					    IORESOURCE_MEM, 0);

		regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		/* Store the usb control regs into simple for further usage */
		simple->regs = regs;

		/* read Silicon version using nvmem driver */
		soc_rev = zynqmp_nvmem_get_silicon_version(&pdev->dev,
						   "soc_revision");

		if (PTR_ERR(soc_rev) == -EPROBE_DEFER) {
			/* Do a deferred probe */
			return -EPROBE_DEFER;

		} else if (!IS_ERR(soc_rev) &&
					(*soc_rev < ZYNQMP_SILICON_V4)) {
			/* Add snps,dis_u3_susphy_quirk
			 * for SOC revison less than v4
			 */
			simple->dis_u3_susphy_quirk = true;
		}

		/* Clean soc_rev if got a valid pointer from nvmem driver
		 * else we may end up in kernel panic
		 */
		if (!IS_ERR(soc_rev))
			kfree(soc_rev);
	}

	ret = dwc3_of_simple_clk_init(simple, of_clk_get_parent_count(np));
	if (ret)
		return ret;

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		for (i = 0; i < simple->num_clocks; i++) {
			clk_disable_unprepare(simple->clks[i]);
			clk_put(simple->clks[i]);
		}

		return ret;
	}

	platform_set_drvdata(pdev, simple);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;
}

static int dwc3_of_simple_remove(struct platform_device *pdev)
{
	struct dwc3_of_simple	*simple = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	int			i;

	of_platform_depopulate(dev);

	for (i = 0; i < simple->num_clocks; i++) {
		clk_disable_unprepare(simple->clks[i]);
		clk_put(simple->clks[i]);
	}

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM

static void dwc3_simple_vbus(struct dwc3 *dwc, bool vbus_off)
{
	u32 reg, addr;
	u8  val;

	if (vbus_off)
		addr = ULPI_OTG_CTRL_CLEAR;
	else
		addr = ULPI_OTG_CTRL_SET;

	val = (1 << OTG_CTRL_DRVVBUS_OFFSET);

	reg = DWC3_GUSB2PHYACC_NEWREGREQ | DWC3_GUSB2PHYACC_ADDR(addr);
	reg |= DWC3_GUSB2PHYACC_WRITE | val;

	addr = DWC3_OF_ADDRESS(DWC3_GUSB2PHYACC(0));
	writel(reg, dwc->regs + addr);
}

static int dwc3_of_simple_suspend(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			i;

	if (!simple->wakeup_capable) {
		/* Ask ULPI to turn OFF Vbus */
		dwc3_simple_vbus(simple->dwc, true);

		/* Disable the clocks */
		for (i = 0; i < simple->num_clocks; i++)
			clk_disable(simple->clks[i]);
	}

	return 0;
}

static int dwc3_of_simple_resume(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			ret;
	int			i;

	if (simple->wakeup_capable)
		return 0;

	for (i = 0; i < simple->num_clocks; i++) {
		ret = clk_enable(simple->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable(simple->clks[i]);
			return ret;
		}

		/* Ask ULPI to turn ON Vbus */
		dwc3_simple_vbus(simple->dwc, false);
	}

	return 0;
}

static int dwc3_of_simple_runtime_suspend(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < simple->num_clocks; i++)
		clk_disable(simple->clks[i]);

	return 0;
}

static int dwc3_of_simple_runtime_resume(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			ret;
	int			i;

	for (i = 0; i < simple->num_clocks; i++) {
		ret = clk_enable(simple->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable(simple->clks[i]);
			return ret;
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops dwc3_of_simple_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_of_simple_suspend,
				dwc3_of_simple_resume)
	SET_RUNTIME_PM_OPS(dwc3_of_simple_runtime_suspend,
			dwc3_of_simple_runtime_resume, NULL)
};

static const struct of_device_id of_dwc3_simple_match[] = {
	{ .compatible = "qcom,dwc3" },
	{ .compatible = "rockchip,rk3399-dwc3" },
	{ .compatible = "xlnx,zynqmp-dwc3" },
	{ .compatible = "cavium,octeon-7130-usb-uctl" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_dwc3_simple_match);

static struct platform_driver dwc3_of_simple_driver = {
	.probe		= dwc3_of_simple_probe,
	.remove		= dwc3_of_simple_remove,
	.driver		= {
		.name	= "dwc3-of-simple",
		.of_match_table = of_dwc3_simple_match,
		.pm = &dwc3_of_simple_dev_pm_ops,
	},
};

module_platform_driver(dwc3_of_simple_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 OF Simple Glue Layer");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
