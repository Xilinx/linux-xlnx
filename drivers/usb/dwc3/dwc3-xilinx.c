// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-xilinx.c - Xilinx DWC3 controller specific glue driver
 *
 * Authors: Manish Narani <manish.narani@xilinx.com>
 *          Anurag Kumar Vulisha <anurag.kumar.vulisha@xilinx.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/of.h>
#include <linux/io.h>

#include <linux/phy/phy.h>

#include "core.h"

/* USB phy reset mask register */
#define XLNX_USB_PHY_RST_EN			0x001C
#define XLNX_PHY_RST_MASK			0x1

/* Xilinx USB 3.0 IP Register */
#define XLNX_USB_TRAFFIC_ROUTE_CONFIG		0x005C
#define XLNX_USB_TRAFFIC_ROUTE_FPD		0x1

#define XLNX_USB_CUR_PWR_STATE			0x0000
#define XLNX_CUR_PWR_STATE_D0			0x00
#define XLNX_CUR_PWR_STATE_D3			0x0F
#define XLNX_CUR_PWR_STATE_BITMASK		0x0F

#define XLNX_USB_PME_ENABLE			0x0034
#define XLNX_PME_ENABLE_SIG_GEN			0x01

#define XLNX_USB_REQ_PWR_STATE			0x003c
#define XLNX_REQ_PWR_STATE_D0			0x00
#define XLNX_REQ_PWR_STATE_D3			0x03

/* Number of retries for USB operations */
#define DWC3_PWR_STATE_RETRIES			1000
#define DWC3_PWR_TIMEOUT			100

/* Versal USB Node ID */
#define VERSAL_USB_NODE_ID			0x18224018

/* Versal USB Reset ID */
#define VERSAL_USB_RESET_ID			0xC104036

#define XLNX_USB_FPD_PIPE_CLK			0x7c
#define PIPE_CLK_DESELECT			1
#define PIPE_CLK_SELECT				0
#define XLNX_USB_FPD_POWER_PRSNT		0x80
#define PIPE_POWER_ON				1
#define PIPE_POWER_OFF				0

enum dwc3_xlnx_core_state {
	UNKNOWN_STATE = 0,
	D0_STATE,
	D3_STATE
};

struct dwc3_xlnx {
	int				num_clocks;
	struct clk_bulk_data		*clks;
	struct device			*dev;
	void __iomem			*regs;
	int				(*pltfm_init)(struct dwc3_xlnx *data);
	struct regulator		*dwc3_pmu;
	struct regulator_dev		*dwc3_xlnx_reg_rdev;
	enum dwc3_xlnx_core_state	pmu_state;
	bool				wakeup_capable;
	struct reset_control		*crst;
	bool				enable_d3_suspend;
	enum usb_dr_mode		dr_mode;
	struct regulator_desc		dwc3_xlnx_reg_desc;
};

static const char *const usb_dr_modes[] = {
	[USB_DR_MODE_UNKNOWN]		= "",
	[USB_DR_MODE_HOST]		= "host",
	[USB_DR_MODE_PERIPHERAL]	= "peripheral",
	[USB_DR_MODE_OTG]		= "otg",
};

static enum usb_dr_mode usb_get_dr_mode_from_string(const char *str)
{
	int ret;

	ret = match_string(usb_dr_modes, ARRAY_SIZE(usb_dr_modes), str);
	return (ret < 0) ? USB_DR_MODE_UNKNOWN : ret;
}

#ifdef CONFIG_PM
static struct regulator_init_data dwc3_xlnx_reg_initdata = {
	.constraints = {
		.always_on = 0,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static int dwc3_zynqmp_power_req(struct device *dev, bool on)
{
	u32 reg, retries;
	void __iomem *reg_base;
	struct dwc3_xlnx *priv_data;
	int ret;

	priv_data = dev_get_drvdata(dev);
	reg_base = priv_data->regs;

	/* Check if entering into D3 state is allowed during suspend */
	if (!priv_data->enable_d3_suspend)
		return 0;

	if (on) {
		dev_dbg(priv_data->dev,
			"trying to set power state to D0....\n");

		if (priv_data->pmu_state == D0_STATE)
			return 0;

		/* Release USB core reset , which was assert during D3 entry */
		ret = reset_control_deassert(priv_data->crst);
		if (ret < 0) {
			dev_err(dev, "Failed to release core reset\n");
			return ret;
		}

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
			dev_err(priv_data->dev,
				"Failed to set power state to D0\n");
			return -EIO;
		}

		priv_data->pmu_state = D0_STATE;
		/* disable D3 entry */
		priv_data->enable_d3_suspend = false;
	} else {
		dev_dbg(priv_data->dev, "Trying to set power state to D3...\n");

		if (priv_data->pmu_state == D3_STATE)
			return 0;

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
			dev_err(priv_data->dev,
				"Failed to set power state to D3\n");
			return -EIO;
		}

		/* Assert USB core reset after entering D3 state */
		ret = reset_control_assert(priv_data->crst);
		if (ret < 0) {
			dev_err(dev, "Failed to assert core reset\n");
			return ret;
		}

		priv_data->pmu_state = D3_STATE;
	}

	return 0;
}

static int dwc3_versal_power_req(struct device *dev, bool on)
{
	int ret;
	struct dwc3_xlnx *priv_data;

	priv_data = dev_get_drvdata(dev);

	if (on) {
		dev_dbg(dev, "%s:Trying to set power state to D0....\n",
			 __func__);

		if (priv_data->pmu_state == D0_STATE)
			return 0;

		ret = zynqmp_pm_reset_assert(VERSAL_USB_RESET_ID,
					     PM_RESET_ACTION_RELEASE);
		if (ret < 0)
			dev_err(priv_data->dev, "failed to De-assert Reset\n");

		ret = zynqmp_pm_usb_set_state(VERSAL_USB_NODE_ID,
					      XLNX_REQ_PWR_STATE_D0,
					      DWC3_PWR_STATE_RETRIES *
					      DWC3_PWR_TIMEOUT);
		if (ret < 0)
			dev_err(priv_data->dev, "failed to enter D0 state\n");

		priv_data->pmu_state = D0_STATE;
	} else {
		dev_dbg(dev, "%s:Trying to set power state to D3...\n",
			__func__);

		if (priv_data->pmu_state == D3_STATE)
			return 0;

		ret = zynqmp_pm_usb_set_state(VERSAL_USB_NODE_ID,
					      XLNX_REQ_PWR_STATE_D3,
					      DWC3_PWR_STATE_RETRIES *
					      DWC3_PWR_TIMEOUT);
		if (ret < 0)
			dev_err(priv_data->dev, "failed to enter D3 state\n");

		ret = zynqmp_pm_reset_assert(VERSAL_USB_RESET_ID,
					     PM_RESET_ACTION_ASSERT);
		if (ret < 0)
			dev_err(priv_data->dev, "failed to assert Reset\n");

		priv_data->pmu_state = D3_STATE;
	}

	return ret;
}

static int dwc3_set_usb_core_power(struct device *dev, bool on)
{
	int ret;
	struct device_node *node = dev->of_node;

	if (of_device_is_compatible(node, "xlnx,zynqmp-dwc3"))
		/* Set D3/D0 state for ZynqMP */
		ret = dwc3_zynqmp_power_req(dev, on);
	else if (of_device_is_compatible(node, "xlnx,versal-dwc3"))
		/* Set D3/D0 state for Versal */
		ret = dwc3_versal_power_req(dev, on);
	else
		/* This is only for Xilinx devices */
		return 0;

	return ret;
}

static int dwc3_xlnx_reg_enable(struct regulator_dev *rdev)
{
	return dwc3_set_usb_core_power(rdev->dev.parent, true);
}

static int dwc3_xlnx_reg_disable(struct regulator_dev *rdev)
{
	return dwc3_set_usb_core_power(rdev->dev.parent, false);
}

static int dwc3_xlnx_reg_is_enabled(struct regulator_dev *rdev)
{
	struct dwc3_xlnx	*priv_data = dev_get_drvdata(rdev->dev.parent);

	return !!(priv_data->pmu_state == D0_STATE);
}

static struct regulator_ops dwc3_xlnx_reg_ops = {
	.enable			= dwc3_xlnx_reg_enable,
	.disable		= dwc3_xlnx_reg_disable,
	.is_enabled		= dwc3_xlnx_reg_is_enabled,
};

static int dwc3_xlnx_register_regulator(struct device *dev,
					struct dwc3_xlnx *priv_data)
{
	struct regulator_config config = { };
	int ret = 0;

	config.dev = dev;
	config.driver_data = (void *)priv_data;
	config.init_data = &dwc3_xlnx_reg_initdata;

	priv_data->dwc3_xlnx_reg_desc.name = dev->of_node->full_name;
	priv_data->dwc3_xlnx_reg_desc.id = -1;
	priv_data->dwc3_xlnx_reg_desc.type = REGULATOR_VOLTAGE;
	priv_data->dwc3_xlnx_reg_desc.owner = THIS_MODULE;
	priv_data->dwc3_xlnx_reg_desc.ops = &dwc3_xlnx_reg_ops;

	/* Register the dwc3 PMU regulator */
	priv_data->dwc3_xlnx_reg_rdev =
		devm_regulator_register(dev, &priv_data->dwc3_xlnx_reg_desc,
					&config);

	if (IS_ERR(priv_data->dwc3_xlnx_reg_rdev)) {
		ret = PTR_ERR(priv_data->dwc3_xlnx_reg_rdev);
		pr_err("Failed to register regulator: %d\n", ret);
	}

	return ret;
}
#endif

static void dwc3_xlnx_mask_phy_rst(struct dwc3_xlnx *priv_data, bool mask)
{
	u32 reg;

	/*
	 * Enable or disable ULPI PHY reset from USB Controller.
	 * This does not actually reset the phy, but just controls
	 * whether USB controller can or cannot reset ULPI PHY.
	 */
	reg = readl(priv_data->regs + XLNX_USB_PHY_RST_EN);

	if (mask)
		reg &= ~XLNX_PHY_RST_MASK;
	else
		reg |= XLNX_PHY_RST_MASK;

	writel(reg, priv_data->regs + XLNX_USB_PHY_RST_EN);
}

static int dwc3_xlnx_init_versal(struct dwc3_xlnx *priv_data)
{
	struct device		*dev = priv_data->dev;
	int			ret;

	dwc3_xlnx_mask_phy_rst(priv_data, false);

	/* Assert and De-assert reset */
	ret = zynqmp_pm_reset_assert(VERSAL_USB_RESET_ID,
				     PM_RESET_ACTION_ASSERT);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to assert Reset\n");
		return ret;
	}

	ret = zynqmp_pm_reset_assert(VERSAL_USB_RESET_ID,
				     PM_RESET_ACTION_RELEASE);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to De-assert Reset\n");
		return ret;
	}

	dwc3_xlnx_mask_phy_rst(priv_data, true);

	return 0;
}

static int dwc3_xlnx_init_zynqmp(struct dwc3_xlnx *priv_data)
{
	struct device		*dev = priv_data->dev;
	struct reset_control	*crst, *hibrst, *apbrst;
	struct phy		*usb3_phy;
	int			ret;
	u32			reg;
	struct gpio_desc	*reset_gpio = NULL;

	crst = devm_reset_control_get_exclusive(dev, "usb_crst");
	if (IS_ERR(crst)) {
		ret = PTR_ERR(crst);
		dev_err_probe(dev, ret,
			      "failed to get core reset signal\n");
		goto err;
	}
	priv_data->crst = crst;

	hibrst = devm_reset_control_get_exclusive(dev, "usb_hibrst");
	if (IS_ERR(hibrst)) {
		ret = PTR_ERR(hibrst);
		dev_err_probe(dev, ret,
			      "failed to get hibernation reset signal\n");
		goto err;
	}

	apbrst = devm_reset_control_get_exclusive(dev, "usb_apbrst");
	if (IS_ERR(apbrst)) {
		ret = PTR_ERR(apbrst);
		dev_err_probe(dev, ret,
			      "failed to get APB reset signal\n");
		goto err;
	}

	usb3_phy = devm_phy_get(dev, "usb3-phy");
	if (PTR_ERR(usb3_phy) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err;
	} else if (IS_ERR(usb3_phy)) {
		ret = 0;
		goto skip_usb3_phy;
	}

	ret = reset_control_assert(crst);
	if (ret < 0) {
		dev_err(dev, "Failed to assert core reset\n");
		goto err;
	}

	ret = reset_control_assert(hibrst);
	if (ret < 0) {
		dev_err(dev, "Failed to assert hibernation reset\n");
		goto err;
	}

	ret = reset_control_assert(apbrst);
	if (ret < 0) {
		dev_err(dev, "Failed to assert APB reset\n");
		goto err;
	}

	ret = phy_init(usb3_phy);
	if (ret < 0) {
		phy_exit(usb3_phy);
		goto err;
	}

	ret = reset_control_deassert(apbrst);
	if (ret < 0) {
		dev_err(dev, "Failed to release APB reset\n");
		goto err;
	}

	/* Set PIPE Power Present signal in FPD Power Present Register*/
	writel(PIPE_POWER_ON, priv_data->regs + XLNX_USB_FPD_POWER_PRSNT);

	/* Set the PIPE Clock Select bit in FPD PIPE Clock register */
	writel(PIPE_CLK_SELECT, priv_data->regs + XLNX_USB_FPD_PIPE_CLK);

	ret = reset_control_deassert(crst);
	if (ret < 0) {
		dev_err(dev, "Failed to release core reset\n");
		goto err;
	}

	ret = reset_control_deassert(hibrst);
	if (ret < 0) {
		dev_err(dev, "Failed to release hibernation reset\n");
		goto err;
	}

	ret = phy_power_on(usb3_phy);
	if (ret < 0) {
		phy_exit(usb3_phy);
		goto err;
	}

skip_usb3_phy:
	/* ulpi reset via gpio-modepin or gpio-framework driver */
	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio)) {
		dev_err_probe(dev, PTR_ERR(reset_gpio),
			      "Failed to bind reset gpio\n");
		goto err;
	}

	if (reset_gpio) {
		/* Toggle ulpi to reset the phy. */
		gpiod_set_value(reset_gpio, 0);
		usleep_range(5000, 10000); /* delay */
		gpiod_set_value(reset_gpio, 1);
		usleep_range(5000, 10000); /* delay */
	}

	/*
	 * This routes the USB DMA traffic to go through FPD path instead
	 * of reaching DDR directly. This traffic routing is needed to
	 * make SMMU and CCI work with USB DMA.
	 */
	if (of_dma_is_coherent(dev->of_node) || device_iommu_mapped(dev)) {
		reg = readl(priv_data->regs + XLNX_USB_TRAFFIC_ROUTE_CONFIG);
		reg |= XLNX_USB_TRAFFIC_ROUTE_FPD;
		writel(reg, priv_data->regs + XLNX_USB_TRAFFIC_ROUTE_CONFIG);
	}

err:
	return ret;
}

/* xilinx feature support functions */
void dwc3_xilinx_wakeup_capable(struct device *dev, bool wakeup)
{
	struct device_node *node = of_node_get(dev->parent->of_node);

	/* check for valid parent node */
	while (node) {
		if (of_device_is_compatible(node, "xlnx,zynqmp-dwc3") ||
		    of_device_is_compatible(node, "xlnx,versal-dwc3"))
			break;

		/* get the next parent node */
		node = of_get_next_parent(node);
	}

	if (node) {
		struct platform_device *pdev_parent;
		struct dwc3_xlnx *priv_data;

		pdev_parent = of_find_device_by_node(node);
		priv_data = platform_get_drvdata(pdev_parent);

		/* Set wakeup capable as true or false */
		priv_data->wakeup_capable = wakeup;

		/* Allow D3 state if wakeup capable only */
		priv_data->enable_d3_suspend = wakeup;
	}
}

static const struct of_device_id dwc3_xlnx_of_match[] = {
	{
		.compatible = "xlnx,zynqmp-dwc3",
		.data = &dwc3_xlnx_init_zynqmp,
	},
	{
		.compatible = "xlnx,versal-dwc3",
		.data = &dwc3_xlnx_init_versal,
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc3_xlnx_of_match);

static int dwc3_xlnx_probe(struct platform_device *pdev)
{
	struct dwc3_xlnx		*priv_data;
	struct device			*dev = &pdev->dev;
	struct device_node		*np = dev->of_node;
	struct device_node		*dwc3_child_node = NULL;
	const struct of_device_id	*match;
	void __iomem			*regs;
	int				ret;
	const char                      *dr_modes;

	priv_data = devm_kzalloc(dev, sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		dev_err_probe(dev, ret, "failed to map registers\n");
		return ret;
	}

	match = of_match_node(dwc3_xlnx_of_match, pdev->dev.of_node);

	dwc3_child_node = of_get_next_child(pdev->dev.of_node, dwc3_child_node);
	if (!dwc3_child_node)
		return -ENODEV;

	priv_data->pltfm_init = match->data;
	priv_data->regs = regs;
	priv_data->dev = dev;

	/* get the dr_mode from child node */
	ret = of_property_read_string(dwc3_child_node, "dr_mode", &dr_modes);
	if (ret < 0)
		priv_data->dr_mode = USB_DR_MODE_UNKNOWN;
	else
		priv_data->dr_mode = usb_get_dr_mode_from_string(dr_modes);

	of_node_put(dwc3_child_node);

	/*
	 * TODO: This flag needs to be handled while implementing
	 *	the remote wake-up feature.
	 */
	priv_data->enable_d3_suspend = false;

	platform_set_drvdata(pdev, priv_data);

#ifdef CONFIG_PM
	ret = dwc3_xlnx_register_regulator(dev, priv_data);
	if (ret)
		return ret;
#endif
	/* Register the dwc3-xilinx wakeup function to dwc3 host */
	dwc3_host_wakeup_register(dwc3_xilinx_wakeup_capable);

	ret = devm_clk_bulk_get_all(priv_data->dev, &priv_data->clks);
	if (ret < 0)
		return ret;

	priv_data->num_clocks = ret;

	ret = clk_bulk_prepare_enable(priv_data->num_clocks, priv_data->clks);
	if (ret)
		return ret;

	ret = priv_data->pltfm_init(priv_data);
	if (ret)
		goto err_clk_put;

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		goto err_clk_put;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_suspend_ignore_children(dev, false);
	pm_runtime_get_sync(dev);

	return 0;

err_clk_put:
	clk_bulk_disable_unprepare(priv_data->num_clocks, priv_data->clks);
	clk_bulk_put_all(priv_data->num_clocks, priv_data->clks);

	return ret;
}

static int dwc3_xlnx_remove(struct platform_device *pdev)
{
	struct dwc3_xlnx	*priv_data = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;

	of_platform_depopulate(dev);

	/* Unregister the dwc3-xilinx wakeup function from dwc3 host */
	dwc3_host_wakeup_register(NULL);
	clk_bulk_disable_unprepare(priv_data->num_clocks, priv_data->clks);
	clk_bulk_put_all(priv_data->num_clocks, priv_data->clks);
	priv_data->num_clocks = 0;

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	return 0;
}

static int __maybe_unused dwc3_xlnx_runtime_suspend(struct device *dev)
{
	struct dwc3_xlnx *priv_data = dev_get_drvdata(dev);

	clk_bulk_disable(priv_data->num_clocks, priv_data->clks);

	return 0;
}

static int __maybe_unused dwc3_xlnx_runtime_resume(struct device *dev)
{
	struct dwc3_xlnx *priv_data = dev_get_drvdata(dev);

	return clk_bulk_enable(priv_data->num_clocks, priv_data->clks);
}

static int __maybe_unused dwc3_xlnx_runtime_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);

	return 0;
}

static int __maybe_unused dwc3_xlnx_suspend(struct device *dev)
{
	struct dwc3_xlnx *priv_data = dev_get_drvdata(dev);

	if (!priv_data->wakeup_capable) {
#ifdef CONFIG_PM
		if (priv_data->dr_mode == USB_DR_MODE_PERIPHERAL)
			/* Put the core into D3 */
			dwc3_set_usb_core_power(dev, false);
#endif
		/* Disable the clocks */
		clk_bulk_disable(priv_data->num_clocks, priv_data->clks);
	}
	return 0;
}

static int __maybe_unused dwc3_xlnx_resume(struct device *dev)
{
	struct dwc3_xlnx *priv_data = dev_get_drvdata(dev);
	int ret;

	if (priv_data->wakeup_capable)
		return 0;

#ifdef CONFIG_PM
	if (priv_data->dr_mode == USB_DR_MODE_PERIPHERAL)
		/* Put the core into D0 */
		dwc3_set_usb_core_power(dev, true);
#endif

	ret = clk_bulk_enable(priv_data->num_clocks, priv_data->clks);
	if (ret)
		return ret;

	return 0;
}

static const struct dev_pm_ops dwc3_xlnx_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_xlnx_suspend, dwc3_xlnx_resume)
	SET_RUNTIME_PM_OPS(dwc3_xlnx_runtime_suspend,
			dwc3_xlnx_runtime_resume, dwc3_xlnx_runtime_idle)
};

static struct platform_driver dwc3_xlnx_driver = {
	.probe		= dwc3_xlnx_probe,
	.remove		= dwc3_xlnx_remove,
	.driver		= {
		.name		= "dwc3-xilinx",
		.of_match_table	= dwc3_xlnx_of_match,
		.pm		= &dwc3_xlnx_dev_pm_ops,
	},
};

module_platform_driver(dwc3_xlnx_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx DWC3 controller specific glue driver");
MODULE_AUTHOR("Manish Narani <manish.narani@xilinx.com>");
MODULE_AUTHOR("Anurag Kumar Vulisha <anurag.kumar.vulisha@xilinx.com>");
