// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-of-simple.c - OF glue layer for simple integrations
 *
 * Copyright (c) 2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
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
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/soc/xilinx/zynqmp/fw.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/slab.h>

#include <linux/phy/phy-zynqmp.h>
#include <linux/of_address.h>

#include "core.h"

/* USB phy reset mask register */
#define XLNX_USB_PHY_RST		0x001C
#define XLNX_PHY_RST_MASK		0x1

/* Xilinx USB 3.0 IP Register */
#define XLNX_USB_COHERENCY		0x005C
#define XLNX_USB_COHERENCY_ENABLE	0x1

/* ULPI control registers */
#define ULPI_OTG_CTRL_SET		0xB
#define ULPI_OTG_CTRL_CLEAR		0XC
#define OTG_CTRL_DRVVBUS_OFFSET		5

#define XLNX_USB_CUR_PWR_STATE          0x0000
#define XLNX_CUR_PWR_STATE_D0           0x00
#define XLNX_CUR_PWR_STATE_D3           0x0F
#define XLNX_CUR_PWR_STATE_BITMASK      0x0F

#define XLNX_USB_PME_ENABLE             0x0034
#define XLNX_PME_ENABLE_SIG_GEN         0x01

#define XLNX_USB_REQ_PWR_STATE          0x003c
#define XLNX_REQ_PWR_STATE_D0           0x00
#define XLNX_REQ_PWR_STATE_D3           0x03

/* Number of retries for USB operations */
#define DWC3_PWR_STATE_RETRIES          1000
#define DWC3_PWR_TIMEOUT		100

/* Versal USB Node ID */
#define VERSAL_USB_NODE_ID		0x18224018

/* Versal USB Reset ID */
#define VERSAL_USB_RESET_ID		0xC104036

#define DWC3_OF_ADDRESS(ADDR)		((ADDR) - DWC3_GLOBALS_REGS_START)

static const struct zynqmp_eemi_ops *eemi_ops;

struct dwc3_of_simple {
	struct device		*dev;
	struct clk		**clks;
	int			num_clocks;
	void __iomem		*regs;
	struct dwc3		*dwc;
	struct phy		*phy;
	bool			wakeup_capable;
	bool			dis_u3_susphy_quirk;
	bool			enable_d3_suspend;
	char			soc_rev;
	struct reset_control	*resets;
	bool			pulse_resets;
	bool			need_reset;
};

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

void dwc3_mask_phy_reset(struct device *dev, bool mask)
{
	struct device_node *node = of_get_parent(dev->of_node);

	/* This is only valid for versal platforms */
	if (of_device_is_compatible(node, "xlnx,versal-dwc3")) {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple *simple;
		u32 reg;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		reg = readl(simple->regs + XLNX_USB_PHY_RST);

		if (mask)
			/*
			 * Mask the phy reset signal from comtroller
			 * reaching ULPI phy. This can be done by
			 * writing 0 into usb2_phy_reset register
			 */
			reg &= ~XLNX_PHY_RST_MASK;
		else
			/*
			 * Allow phy reset signal from controller to
			 * reset ULPI phy. This can be done by writing
			 * 0x1 into usb2_phy_reset register
			 */
			reg |= XLNX_PHY_RST_MASK;

		writel(reg, simple->regs + XLNX_USB_PHY_RST);
	}
}
EXPORT_SYMBOL(dwc3_mask_phy_reset);

void dwc3_set_simple_data(struct dwc3 *dwc)
{
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	if (node && (of_device_is_compatible(node, "xlnx,zynqmp-dwc3") ||
		     of_device_is_compatible(node, "xlnx,versal-dwc3")))  {
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
	struct device_node *node = of_node_get(dev->parent->of_node);

	/* check for valid parent node */
	while (node) {
		if (!of_device_is_compatible(node, "xlnx,zynqmp-dwc3") ||
		    !of_device_is_compatible(node, "xlnx,versal-dwc3"))
			node = of_get_next_parent(node);
		else
			break;
	}

	if (node)  {
		struct platform_device *pdev_parent;
		struct dwc3_of_simple   *simple;

		pdev_parent = of_find_device_by_node(node);
		simple = platform_get_drvdata(pdev_parent);

		/* Set wakeup capable as true or false */
		simple->wakeup_capable = wakeup;

		/* Allow D3 state if wakeup capable only */
		simple->enable_d3_suspend = wakeup;
	}
}
EXPORT_SYMBOL(dwc3_simple_wakeup_capable);

static int dwc3_simple_set_phydata(struct dwc3_of_simple *simple)
{
	struct device		*dev = simple->dev;
	struct device_node	*np = dev->of_node;
	struct phy		*phy;

	np = of_get_next_child(np, NULL);

	if (np) {
		phy = of_phy_get(np, "usb3-phy");
		if (IS_ERR(phy)) {
			dev_err(dev, "%s: Can't find usb3-phy\n", __func__);
			return PTR_ERR(phy);
		}

		/* Store phy for future usage */
		simple->phy = phy;

		/* assign USB vendor regs addr to phy platform_data */
		phy->dev.platform_data = simple->regs;

		phy_put(phy);
	} else {
		dev_err(dev, "%s: Can't find child node\n", __func__);
		return -EINVAL;
	}

	return 0;
}

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
			while (--i >= 0) {
				clk_disable_unprepare(simple->clks[i]);
				clk_put(simple->clks[i]);
			}
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

static int dwc3_dis_u3phy_suspend(struct platform_device *pdev,
				  struct dwc3_of_simple *simple)
{
	char *soc_rev;

	/* The below is only valid for ZynqMP SOC */
	if (of_device_is_compatible(pdev->dev.of_node,
				    "xlnx,zynqmp-dwc3")) {
		/* read Silicon version using nvmem driver */
		soc_rev = zynqmp_nvmem_get_silicon_version(&pdev->dev,
							   "soc_revision");

		if (PTR_ERR(soc_rev) == -EPROBE_DEFER)
			/* Do a deferred probe */
			return -EPROBE_DEFER;
		else if (!IS_ERR(soc_rev) && *soc_rev < ZYNQMP_SILICON_V4)
			/* Add snps,dis_u3_susphy_quirk
			 * for SOC revison less than v4
			 */
			simple->dis_u3_susphy_quirk = true;

		if (!IS_ERR(soc_rev)) {
			/* Update soc_rev to simple for future use */
			simple->soc_rev = *soc_rev;

			/* Clean soc_rev if got a valid pointer from nvmem
			 * driver else we may end up in kernel panic
			 */
			kfree(soc_rev);
		} else {
			/* Return error */
			return PTR_ERR(soc_rev);
		}
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
	bool			shared_resets = false;

	simple = devm_kzalloc(dev, sizeof(*simple), GFP_KERNEL);
	if (!simple)
		return -ENOMEM;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops)) {
		dev_err(dev, "Failed to get eemi_ops\n");
		return PTR_ERR(eemi_ops);
	}

	platform_set_drvdata(pdev, simple);
	simple->dev = dev;

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-dwc3") ||
	    of_device_is_compatible(pdev->dev.of_node, "xlnx,versal-dwc3")) {

		struct resource		*res;
		void __iomem		*regs;

		res = platform_get_resource(pdev,
					    IORESOURCE_MEM, 0);

		regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		/* Store the usb control regs into simple for further usage */
		simple->regs = regs;

		/*
		 * ZynqMP silicon revision lesser than 4.0 needs to disable
		 * suspend of usb 3.0 phy.
		 */
		ret = dwc3_dis_u3phy_suspend(pdev, simple);
		if (ret)
			return ret;
	}

	/* Set phy data for future use */
	dwc3_simple_set_phydata(simple);

	/*
	 * Some controllers need to toggle the usb3-otg reset before trying to
	 * initialize the PHY, otherwise the PHY times out.
	 */
	if (of_device_is_compatible(np, "rockchip,rk3399-dwc3"))
		simple->need_reset = true;

	if (of_device_is_compatible(np, "amlogic,meson-axg-dwc3") ||
	    of_device_is_compatible(np, "amlogic,meson-gxl-dwc3")) {
		shared_resets = true;
		simple->pulse_resets = true;
	}

	simple->resets = of_reset_control_array_get(np, shared_resets, true);
	if (IS_ERR(simple->resets)) {
		ret = PTR_ERR(simple->resets);
		dev_err(dev, "failed to get device resets, err=%d\n", ret);
		return ret;
	}

	if (simple->pulse_resets) {
		ret = reset_control_reset(simple->resets);
		if (ret)
			goto err_resetc_put;
	} else {
		ret = reset_control_deassert(simple->resets);
		if (ret)
			goto err_resetc_put;
	}

	ret = dwc3_of_simple_clk_init(simple, of_count_phandle_with_args(np,
						"clocks", "#clock-cells"));
	if (ret)
		goto err_resetc_assert;

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		for (i = 0; i < simple->num_clocks; i++) {
			clk_disable_unprepare(simple->clks[i]);
			clk_put(simple->clks[i]);
		}

		goto err_resetc_assert;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;

err_resetc_assert:
	if (!simple->pulse_resets)
		reset_control_assert(simple->resets);

err_resetc_put:
	reset_control_put(simple->resets);
	return ret;
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
	simple->num_clocks = 0;

	if (!simple->pulse_resets)
		reset_control_assert(simple->resets);

	reset_control_put(simple->resets);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

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

static void dwc3_usb2phycfg(struct dwc3 *dwc, bool suspend)
{
	u32 addr, reg;

	addr = DWC3_OF_ADDRESS(DWC3_GUSB2PHYCFG(0));

	if (suspend) {
		reg = readl(dwc->regs + addr);
		if (!(reg & DWC3_GUSB2PHYCFG_SUSPHY)) {
			reg |= DWC3_GUSB2PHYCFG_SUSPHY;
			writel(reg, (dwc->regs + addr));
		}
	} else {
		reg = readl(dwc->regs + addr);
		if ((reg & DWC3_GUSB2PHYCFG_SUSPHY)) {
			reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
			writel(reg, (dwc->regs + addr));
		}
	}
}

static int dwc3_zynqmp_power_req(struct dwc3 *dwc, bool on)
{
	u32 reg, retries;
	void __iomem *reg_base;
	struct platform_device *pdev_parent;
	struct dwc3_of_simple *simple;
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	pdev_parent = of_find_device_by_node(node);
	simple = platform_get_drvdata(pdev_parent);
	reg_base = simple->regs;

	/* Check if entering into D3 state is allowed during suspend */
	if ((simple->soc_rev < ZYNQMP_SILICON_V4) || !simple->enable_d3_suspend)
		return 0;

	if (!simple->phy)
		return 0;

	if (on) {
		dev_dbg(dwc->dev, "trying to set power state to D0....\n");

		/* Release USB core reset , which was assert during D3 entry */
		xpsgtr_usb_crst_release(simple->phy);

		/* change power state to D0 */
		writel(XLNX_REQ_PWR_STATE_D0,
		       reg_base + XLNX_USB_REQ_PWR_STATE);

		/* wait till current state is changed to D0 */
		retries = DWC3_PWR_STATE_RETRIES;
		do {
			reg = readl(reg_base + XLNX_USB_CUR_PWR_STATE);
			if ((reg & XLNX_CUR_PWR_STATE_BITMASK) ==
			     XLNX_CUR_PWR_STATE_D0)
				break;

			udelay(DWC3_PWR_TIMEOUT);
		} while (--retries);

		if (!retries) {
			dev_err(dwc->dev, "Failed to set power state to D0\n");
			return -EIO;
		}

		dwc->is_d3 = false;

		/* Clear Suspend PHY bit if dis_u2_susphy_quirk is set */
		if (dwc->dis_u2_susphy_quirk)
			dwc3_usb2phycfg(dwc, false);
	} else {
		dev_dbg(dwc->dev, "Trying to set power state to D3...\n");

		/*
		 * Set Suspend PHY bit before entering D3 if
		 * dis_u2_susphy_quirk is set
		 */
		if (dwc->dis_u2_susphy_quirk)
			dwc3_usb2phycfg(dwc, true);

		/* enable PME to wakeup from hibernation */
		writel(XLNX_PME_ENABLE_SIG_GEN, reg_base + XLNX_USB_PME_ENABLE);

		/* change power state to D3 */
		writel(XLNX_REQ_PWR_STATE_D3,
		       reg_base + XLNX_USB_REQ_PWR_STATE);

		/* wait till current state is changed to D3 */
		retries = DWC3_PWR_STATE_RETRIES;
		do {
			reg = readl(reg_base + XLNX_USB_CUR_PWR_STATE);
			if ((reg & XLNX_CUR_PWR_STATE_BITMASK) ==
					XLNX_CUR_PWR_STATE_D3)
				break;

			udelay(DWC3_PWR_TIMEOUT);
		} while (--retries);

		if (!retries) {
			dev_err(dwc->dev, "Failed to set power state to D3\n");
			return -EIO;
		}

		/* Assert USB core reset after entering D3 state */
		xpsgtr_usb_crst_assert(simple->phy);

		dwc->is_d3 = true;
	}

	return 0;
}

static int dwc3_versal_power_req(struct dwc3 *dwc, bool on)
{
	int ret;
	struct platform_device *pdev_parent;
	struct dwc3_of_simple *simple;
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	pdev_parent = of_find_device_by_node(node);
	simple = platform_get_drvdata(pdev_parent);

	if (!eemi_ops->ioctl || !eemi_ops->reset_assert)
		return -ENOMEM;

	if (on) {
		dev_dbg(dwc->dev, "Trying to set power state to D0....\n");
		ret = eemi_ops->reset_assert(VERSAL_USB_RESET_ID,
					     PM_RESET_ACTION_RELEASE);
		if (ret < 0)
			dev_err(simple->dev, "failed to De-assert Reset\n");

		ret = eemi_ops->ioctl(VERSAL_USB_NODE_ID, IOCTL_USB_SET_STATE,
				      XLNX_REQ_PWR_STATE_D0,
				      DWC3_PWR_STATE_RETRIES * DWC3_PWR_TIMEOUT,
				      NULL);
		if (ret < 0)
			dev_err(simple->dev, "failed to enter D0 state\n");

		dwc->is_d3 = false;

		/* Clear Suspend PHY bit if dis_u2_susphy_quirk is set */
		if (dwc->dis_u2_susphy_quirk)
			dwc3_usb2phycfg(dwc, false);
	} else {
		dev_dbg(dwc->dev, "Trying to set power state to D3...\n");

		/*
		 * Set Suspend PHY bit before entering D3 if
		 * dis_u2_susphy_quirk is set
		 */
		if (dwc->dis_u2_susphy_quirk)
			dwc3_usb2phycfg(dwc, true);

		ret = eemi_ops->ioctl(VERSAL_USB_NODE_ID, IOCTL_USB_SET_STATE,
				      XLNX_REQ_PWR_STATE_D3,
				      DWC3_PWR_STATE_RETRIES * DWC3_PWR_TIMEOUT,
				      NULL);
		if (ret < 0)
			dev_err(simple->dev, "failed to enter D3 state\n");

		ret = eemi_ops->reset_assert(VERSAL_USB_RESET_ID,
					     PM_RESET_ACTION_ASSERT);
		if (ret < 0)
			dev_err(simple->dev, "failed to assert Reset\n");

		dwc->is_d3 = true;
	}

	return ret;
}

int dwc3_set_usb_core_power(struct dwc3 *dwc, bool on)
{
	int ret;
	struct device_node *node = of_get_parent(dwc->dev->of_node);

	if (of_device_is_compatible(node, "xlnx,zynqmp-dwc3"))
		/* Set D3/D0 state for ZynqMP */
		ret = dwc3_zynqmp_power_req(dwc, on);
	else if (of_device_is_compatible(node, "xlnx,versal-dwc3"))
		/* Set D3/D0 state for Versal */
		ret = dwc3_versal_power_req(dwc, on);
	else
		/* This is only for Xilinx devices */
		return 0;

	return ret;
}
EXPORT_SYMBOL(dwc3_set_usb_core_power);

#endif

static int __maybe_unused dwc3_of_simple_resume(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			ret;
	int			i;

	if (simple->wakeup_capable || simple->dwc->is_d3)
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

static int __maybe_unused dwc3_of_simple_runtime_resume(struct device *dev)
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

static int __maybe_unused dwc3_of_simple_suspend(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			i;

	if (!simple->wakeup_capable && !simple->dwc->is_d3) {
		/* Ask ULPI to turn OFF Vbus */
		dwc3_simple_vbus(simple->dwc, true);

		/* Disable the clocks */
		for (i = 0; i < simple->num_clocks; i++)
			clk_disable(simple->clks[i]);
	}

	if (simple->need_reset)
		reset_control_assert(simple->resets);

	return 0;
}

static int __maybe_unused dwc3_of_simple_runtime_suspend(struct device *dev)
{
	struct dwc3_of_simple	*simple = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < simple->num_clocks; i++)
		clk_disable(simple->clks[i]);

	if (simple->need_reset)
		reset_control_deassert(simple->resets);

	return 0;
}

static const struct dev_pm_ops dwc3_of_simple_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_of_simple_suspend, dwc3_of_simple_resume)
	SET_RUNTIME_PM_OPS(dwc3_of_simple_runtime_suspend,
			dwc3_of_simple_runtime_resume, NULL)
};

static const struct of_device_id of_dwc3_simple_match[] = {
	{ .compatible = "rockchip,rk3399-dwc3" },
	{ .compatible = "xlnx,zynqmp-dwc3" },
	{ .compatible = "xlnx,versal-dwc3" },
	{ .compatible = "cavium,octeon-7130-usb-uctl" },
	{ .compatible = "sprd,sc9860-dwc3" },
	{ .compatible = "amlogic,meson-axg-dwc3" },
	{ .compatible = "amlogic,meson-gxl-dwc3" },
	{ .compatible = "allwinner,sun50i-h6-dwc3" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_dwc3_simple_match);

static struct platform_driver dwc3_of_simple_driver = {
	.probe		= dwc3_of_simple_probe,
	.remove		= dwc3_of_simple_remove,
	.driver		= {
		.name	= "dwc3-of-simple",
		.of_match_table = of_dwc3_simple_match,
		.pm	= &dwc3_of_simple_dev_pm_ops,
	},
};

module_platform_driver(dwc3_of_simple_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 OF Simple Glue Layer");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
