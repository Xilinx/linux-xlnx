// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx GT Quad Base driver
 *
 * Copyright (C) 2021 Xilinx, Inc.
 *
 * Author: Jagadeesh Banisetti <jagadeesh.banisetti@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Gt Quad Base Registers */
#define GT_QUAD_BASE_CTL				0xc
#define GT_QUAD_BASE_CTL_VALUE				0xf9e8d7c6
#define GT_QUAD_BASE_CH1_CLK_DIV_REG			0x3694
#define GT_QUAD_BASE_CH1_CLK_DIV_MASK			GENMASK(9, 0)
#define GT_QUAD_BASE_CH1_CLK_DIV_VALUE			0x260
#define GT_QUAD_BASE_DEFAULT_VAL			0

struct dpgtquadphy_dev {
	struct phy *phy;
	struct clk *axi_clk;
	void __iomem *base;
};

static int xdpgtquad_init(struct phy *phy)
{
	struct dpgtquadphy_dev *gtquad = phy_get_drvdata(phy);
	u32 data;

	/*
	 * Unlocking the NPI space so that GT CH1 divider value can be
	 * programmed. This will generate a /20 clk
	 */
	writel(GT_QUAD_BASE_CTL_VALUE,
	       gtquad->base + GT_QUAD_BASE_CTL);

	data = readl(gtquad->base + GT_QUAD_BASE_CH1_CLK_DIV_REG);
	data &= ~GT_QUAD_BASE_CH1_CLK_DIV_MASK;
	data |= FIELD_PREP(GT_QUAD_BASE_CH1_CLK_DIV_MASK,
			   GT_QUAD_BASE_CH1_CLK_DIV_VALUE);
	writel(data, gtquad->base + GT_QUAD_BASE_CH1_CLK_DIV_REG);

	return 0;
}

static int xdpgtquad_reset(struct phy *phy)
{
	struct dpgtquadphy_dev *gtquad = phy_get_drvdata(phy);

	writel(GT_QUAD_BASE_DEFAULT_VAL,
	       gtquad->base + GT_QUAD_BASE_CTL);
	writel(GT_QUAD_BASE_DEFAULT_VAL,
	       gtquad->base + GT_QUAD_BASE_CH1_CLK_DIV_REG);

	return 0;
}

static struct phy *xdpgtquadphy_xlate(struct device *dev,
				      struct of_phandle_args *args)
{
	struct dpgtquadphy_dev *priv = dev_get_drvdata(dev);
	struct device_node *gtquadphynode = args->np;

	if (!of_device_is_available(gtquadphynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}

	return priv->phy;
}

static const struct phy_ops xdpgtquad_phyops = {
	.reset		= xdpgtquad_reset,
	.init		= xdpgtquad_init,
	.owner		= THIS_MODULE,
};

/**
 * gt_quad_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int gt_quad_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dpgtquadphy_dev *gtquad;
	struct phy *phy;
	struct phy_provider *provider;
	struct resource *res;
	int ret;

	gtquad = devm_kzalloc(&pdev->dev, sizeof(*gtquad), GFP_KERNEL);
	if (!gtquad)
		return -ENOMEM;

	gtquad->axi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(gtquad->axi_clk)) {
		dev_err(&pdev->dev, "failed to get s_axi_clk\n");
		return PTR_ERR(gtquad->axi_clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gtquad->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gtquad->base)) {
		dev_err(&pdev->dev, "Couldn't map GT Quad Base IP registers\n");
		return PTR_ERR(gtquad->base);
	}

	ret = clk_prepare_enable(gtquad->axi_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)\n", ret);
		return ret;
	}

	phy = devm_phy_create(&pdev->dev, np, &xdpgtquad_phyops);
	if (IS_ERR(phy)) {
		dev_err(&pdev->dev, "failed to create DP GT_QUAD PHY\n");
		ret = PTR_ERR(phy);
		goto error_clk;
	}
	gtquad->phy = phy;
	phy_set_drvdata(phy, gtquad);

	provider = devm_of_phy_provider_register(&pdev->dev, xdpgtquadphy_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
		ret = PTR_ERR(provider);
		goto error_clk;
	}

	platform_set_drvdata(pdev, gtquad);

	return 0;

error_clk:
	clk_disable_unprepare(gtquad->axi_clk);

	return ret;
}

static int gt_quad_remove(struct platform_device *pdev)
{
	struct dpgtquadphy_dev *gtquad = platform_get_drvdata(pdev);

	clk_disable_unprepare(gtquad->axi_clk);

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id dpgtquadphy_of_match[] = {
	{ .compatible = "xlnx,gt-quad-base-1.1" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, dpgtquadphy_of_match);

static struct platform_driver dpgtquadphy_driver = {
	.probe = gt_quad_probe,
	.remove = gt_quad_remove,
	.driver = {
		.name = "xilinx-dpgtquadphy",
		.of_match_table	= dpgtquadphy_of_match,
	},
};
module_platform_driver(dpgtquadphy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jagadeesh Banisetti <jbaniset@xilinx.com>");
MODULE_DESCRIPTION("Xilinx driver for GT Quad Base");
