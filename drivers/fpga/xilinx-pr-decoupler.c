/*
 * Copyright (c) 2017 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/fpga/fpga-bridge.h>

struct pr_decoupler_priv {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
};

/* 1 - decouple, 0 - normal */
#define DECOUPLE	1
#define NORMAL		0

static int pr_decoupler_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct pr_decoupler_priv *priv = bridge->priv;
	struct device *dev = priv->dev;

	if (enable) {
		dev_dbg(dev, "Normal mode - traffic can go through\n");
		writel(NORMAL, priv->base);
	} else {
		dev_dbg(dev, "Decouple mode - traffic can't go through\n");
		writel(DECOUPLE, priv->base);
	}

	return 0;
}

static int pr_decoupler_enable_show(struct fpga_bridge *bridge)
{
	struct pr_decoupler_priv *priv = bridge->priv;

	return !readl(priv->base);
}

static struct fpga_bridge_ops pr_decoupler_ops = {
	.enable_set = pr_decoupler_enable_set,
	.enable_show = pr_decoupler_enable_show,
};

static int pr_decoupler_probe(struct platform_device *pdev)
{
	struct pr_decoupler_priv *priv;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->dev = dev;

	priv->clk = devm_clk_get(dev, "aclk");
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "Input clock not found\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clk\n");
		return ret;
	}

	return fpga_bridge_register(dev, "pr_decoupler",
				    &pr_decoupler_ops, priv);
}

static int pr_decoupler_remove(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);
	struct pr_decoupler_priv *priv = bridge->priv;

	fpga_bridge_unregister(&pdev->dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id pr_decoupler_of_match[] = {
	{ .compatible = "xlnx,pr_decoupler", },
	{},
};
MODULE_DEVICE_TABLE(of, pr_decoupler_of_match);

static struct platform_driver pr_decoupler_driver = {
	.probe = pr_decoupler_probe,
	.remove = pr_decoupler_remove,
	.driver = {
		.name	= "pr_decoupler",
		.of_match_table = of_match_ptr(pr_decoupler_of_match),
	},
};

module_platform_driver(pr_decoupler_driver);

MODULE_DESCRIPTION("Xilinx PR Decoupler");
MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>");
MODULE_LICENSE("GPL v2");
