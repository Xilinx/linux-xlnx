/*
 * Copyright (C) 2015 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * Based on vendor driver:
 * Copyright (C) 2013 Marvell Inc.
 * Author: Chao Xie <xiechao.mail@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>

#define PHY_28NM_HSIC_CTRL			0x08
#define PHY_28NM_HSIC_IMPCAL_CAL		0x18
#define PHY_28NM_HSIC_PLL_CTRL01		0x1c
#define PHY_28NM_HSIC_PLL_CTRL2			0x20
#define PHY_28NM_HSIC_INT			0x28

#define PHY_28NM_HSIC_PLL_SELLPFR_SHIFT		26
#define PHY_28NM_HSIC_PLL_FBDIV_SHIFT		0
#define PHY_28NM_HSIC_PLL_REFDIV_SHIFT		9

#define PHY_28NM_HSIC_S2H_PU_PLL		BIT(10)
#define PHY_28NM_HSIC_H2S_PLL_LOCK		BIT(15)
#define PHY_28NM_HSIC_S2H_HSIC_EN		BIT(7)
#define S2H_DRV_SE0_4RESUME			BIT(14)
#define PHY_28NM_HSIC_H2S_IMPCAL_DONE		BIT(27)

#define PHY_28NM_HSIC_CONNECT_INT		BIT(1)
#define PHY_28NM_HSIC_HS_READY_INT		BIT(2)

struct mv_hsic_phy {
	struct phy		*phy;
	struct platform_device	*pdev;
	void __iomem		*base;
	struct clk		*clk;
};

static bool wait_for_reg(void __iomem *reg, u32 mask, unsigned long timeout)
{
	timeout += jiffies;
	while (time_is_after_eq_jiffies(timeout)) {
		if ((readl(reg) & mask) == mask)
			return true;
		msleep(1);
	}
	return false;
}

static int mv_hsic_phy_init(struct phy *phy)
{
	struct mv_hsic_phy *mv_phy = phy_get_drvdata(phy);
	struct platform_device *pdev = mv_phy->pdev;
	void __iomem *base = mv_phy->base;

	clk_prepare_enable(mv_phy->clk);

	/* Set reference clock */
	writel(0x1 << PHY_28NM_HSIC_PLL_SELLPFR_SHIFT |
		0xf0 << PHY_28NM_HSIC_PLL_FBDIV_SHIFT |
		0xd << PHY_28NM_HSIC_PLL_REFDIV_SHIFT,
		base + PHY_28NM_HSIC_PLL_CTRL01);

	/* Turn on PLL */
	writel(readl(base + PHY_28NM_HSIC_PLL_CTRL2) |
		PHY_28NM_HSIC_S2H_PU_PLL,
		base + PHY_28NM_HSIC_PLL_CTRL2);

	/* Make sure PHY PLL is locked */
	if (!wait_for_reg(base + PHY_28NM_HSIC_PLL_CTRL2,
	    PHY_28NM_HSIC_H2S_PLL_LOCK, HZ / 10)) {
		dev_err(&pdev->dev, "HSIC PHY PLL not locked after 100mS.");
		clk_disable_unprepare(mv_phy->clk);
		return -ETIMEDOUT;
	}

	return 0;
}

static int mv_hsic_phy_power_on(struct phy *phy)
{
	struct mv_hsic_phy *mv_phy = phy_get_drvdata(phy);
	struct platform_device *pdev = mv_phy->pdev;
	void __iomem *base = mv_phy->base;
	u32 reg;

	reg = readl(base + PHY_28NM_HSIC_CTRL);
	/* Avoid SE0 state when resume for some device will take it as reset */
	reg &= ~S2H_DRV_SE0_4RESUME;
	reg |= PHY_28NM_HSIC_S2H_HSIC_EN;	/* Enable HSIC PHY */
	writel(reg, base + PHY_28NM_HSIC_CTRL);

	/*
	 *  Calibration Timing
	 *		   ____________________________
	 *  CAL START   ___|
	 *			   ____________________
	 *  CAL_DONE    ___________|
	 *		   | 400us |
	 */

	/* Make sure PHY Calibration is ready */
	if (!wait_for_reg(base + PHY_28NM_HSIC_IMPCAL_CAL,
	    PHY_28NM_HSIC_H2S_IMPCAL_DONE, HZ / 10)) {
		dev_warn(&pdev->dev, "HSIC PHY READY not set after 100mS.");
		return -ETIMEDOUT;
	}

	/* Waiting for HSIC connect int*/
	if (!wait_for_reg(base + PHY_28NM_HSIC_INT,
	    PHY_28NM_HSIC_CONNECT_INT, HZ / 5)) {
		dev_warn(&pdev->dev, "HSIC wait for connect interrupt timeout.");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mv_hsic_phy_power_off(struct phy *phy)
{
	struct mv_hsic_phy *mv_phy = phy_get_drvdata(phy);
	void __iomem *base = mv_phy->base;

	writel(readl(base + PHY_28NM_HSIC_CTRL) & ~PHY_28NM_HSIC_S2H_HSIC_EN,
		base + PHY_28NM_HSIC_CTRL);

	return 0;
}

static int mv_hsic_phy_exit(struct phy *phy)
{
	struct mv_hsic_phy *mv_phy = phy_get_drvdata(phy);
	void __iomem *base = mv_phy->base;

	/* Turn off PLL */
	writel(readl(base + PHY_28NM_HSIC_PLL_CTRL2) &
		~PHY_28NM_HSIC_S2H_PU_PLL,
		base + PHY_28NM_HSIC_PLL_CTRL2);

	clk_disable_unprepare(mv_phy->clk);
	return 0;
}


static const struct phy_ops hsic_ops = {
	.init		= mv_hsic_phy_init,
	.power_on	= mv_hsic_phy_power_on,
	.power_off	= mv_hsic_phy_power_off,
	.exit		= mv_hsic_phy_exit,
	.owner		= THIS_MODULE,
};

static int mv_hsic_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct mv_hsic_phy *mv_phy;
	struct resource *r;

	mv_phy = devm_kzalloc(&pdev->dev, sizeof(*mv_phy), GFP_KERNEL);
	if (!mv_phy)
		return -ENOMEM;

	mv_phy->pdev = pdev;

	mv_phy->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mv_phy->clk)) {
		dev_err(&pdev->dev, "failed to get clock.\n");
		return PTR_ERR(mv_phy->clk);
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mv_phy->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(mv_phy->base))
		return PTR_ERR(mv_phy->base);

	mv_phy->phy = devm_phy_create(&pdev->dev, pdev->dev.of_node, &hsic_ops);
	if (IS_ERR(mv_phy->phy))
		return PTR_ERR(mv_phy->phy);

	phy_set_drvdata(mv_phy->phy, mv_phy);

	phy_provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id mv_hsic_phy_dt_match[] = {
	{ .compatible = "marvell,pxa1928-hsic-phy", },
	{},
};
MODULE_DEVICE_TABLE(of, mv_hsic_phy_dt_match);

static struct platform_driver mv_hsic_phy_driver = {
	.probe	= mv_hsic_phy_probe,
	.driver = {
		.name   = "mv-hsic-phy",
		.of_match_table = of_match_ptr(mv_hsic_phy_dt_match),
	},
};
module_platform_driver(mv_hsic_phy_driver);

MODULE_AUTHOR("Rob Herring <robh@kernel.org>");
MODULE_DESCRIPTION("Marvell HSIC phy driver");
MODULE_LICENSE("GPL v2");
