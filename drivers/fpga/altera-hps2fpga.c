/*
 * FPGA to/from HPS Bridge Driver for Altera SoCFPGA Devices
 *
 * Copyright (C) 2013-2015 Altera Corporation, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This driver manages bridges on a Altera SOCFPGA between the ARM host
 * processor system (HPS) and the embedded FPGA.
 *
 * This driver supports enabling and disabling of the configured ports, which
 * allows for safe reprogramming of the FPGA, assuming that the new FPGA image
 * uses the same port configuration. Bridges must be disabled before
 * reprogramming the FPGA and re-enabled after the FPGA has been programmed.
 */

#include <linux/clk.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define ALT_L3_REMAP_OFST	0x0
#define ALT_L3_REMAP_MPUZERO_MSK	0x00000001
#define ALT_L3_REMAP_H2F_MSK	0x00000008
#define ALT_L3_REMAP_LWH2F_MSK	0x00000010

#define HPS2FPGA_BRIDGE_NAME	"hps2fpga"
#define LWHPS2FPGA_BRIDGE_NAME	"lwhps2fpga"
#define FPGA2HPS_BRIDGE_NAME	"fpga2hps"

struct altera_hps2fpga_data {
	const char *name;
	struct reset_control *bridge_reset;
	struct regmap *l3reg;
	/* The L3 REMAP register is write only, so keep a cached value. */
	unsigned int l3_remap_value;
	unsigned int remap_mask;
	struct clk *clk;
};

static int alt_hps2fpga_enable_show(struct fpga_bridge *bridge)
{
	struct altera_hps2fpga_data *priv = bridge->priv;

	return reset_control_status(priv->bridge_reset);
}

static int _alt_hps2fpga_enable_set(struct altera_hps2fpga_data *priv,
	bool enable)
{
	int ret;

	/* bring bridge out of reset */
	if (enable)
	ret = reset_control_deassert(priv->bridge_reset);
	else
	ret = reset_control_assert(priv->bridge_reset);
	if (ret)
	return ret;

	/* Allow bridge to be visible to L3 masters or not */
	if (priv->remap_mask) {
	priv->l3_remap_value |= ALT_L3_REMAP_MPUZERO_MSK;

	if (enable)
	priv->l3_remap_value |= priv->remap_mask;
	else
	priv->l3_remap_value &= ~priv->remap_mask;

	ret = regmap_write(priv->l3reg, ALT_L3_REMAP_OFST,
	priv->l3_remap_value);
	}

	return ret;
}

static int alt_hps2fpga_enable_set(struct fpga_bridge *bridge, bool enable)
{
	return _alt_hps2fpga_enable_set(bridge->priv, enable);
}

static const struct fpga_bridge_ops altera_hps2fpga_br_ops = {
	.enable_set = alt_hps2fpga_enable_set,
	.enable_show = alt_hps2fpga_enable_show,
};

static struct altera_hps2fpga_data hps2fpga_data = {
	.name = HPS2FPGA_BRIDGE_NAME,
	.remap_mask = ALT_L3_REMAP_H2F_MSK,
};

static struct altera_hps2fpga_data lwhps2fpga_data = {
	.name = LWHPS2FPGA_BRIDGE_NAME,
	.remap_mask = ALT_L3_REMAP_LWH2F_MSK,
};

static struct altera_hps2fpga_data fpga2hps_data = {
	.name = FPGA2HPS_BRIDGE_NAME,
};

static const struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-hps2fpga-bridge",
	.data = &hps2fpga_data },
	{ .compatible = "altr,socfpga-lwhps2fpga-bridge",
	.data = &lwhps2fpga_data },
	{ .compatible = "altr,socfpga-fpga2hps-bridge",
	.data = &fpga2hps_data },
	{},
};

static int alt_fpga_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct altera_hps2fpga_data *priv;
	const struct of_device_id *of_id;
	u32 enable;
	int ret;

	of_id = of_match_device(altera_fpga_of_match, dev);
	priv = (struct altera_hps2fpga_data *)of_id->data;

	priv->bridge_reset = devm_reset_control_get(dev, priv->name);
	if (IS_ERR(priv->bridge_reset)) {
	dev_err(dev, "Could not get %s reset control\n", priv->name);
	return PTR_ERR(priv->bridge_reset);
	}

	priv->l3reg = syscon_regmap_lookup_by_compatible("altr,l3regs");
	if (IS_ERR(priv->l3reg)) {
	dev_err(dev, "regmap for altr,l3regs lookup failed\n");
	return PTR_ERR(priv->l3reg);
	}

	priv->clk = of_clk_get(dev->of_node, 0);
	if (IS_ERR(priv->clk)) {
	dev_err(dev, "no clock specified\n");
	return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
	dev_err(dev, "could not enable clock\n");
	return -EBUSY;
	}

	ret = fpga_bridge_register(dev, priv->name, &altera_hps2fpga_br_ops,
	priv);
	if (ret)
	return ret;

	if (!of_property_read_u32(dev->of_node, "bridge-enable", &enable)) {
	if (enable > 1) {
	dev_warn(dev, "invalid bridge-enable %u > 1\n", enable);
	} else {
	dev_info(dev, "%s bridge\n",
	(enable ? "enabling" : "disabling"));

	ret = _alt_hps2fpga_enable_set(priv, enable);
	if (ret) {
	fpga_bridge_unregister(&pdev->dev);
	return ret;
	}
	}
	}

	return ret;
}

static int alt_fpga_bridge_remove(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);
	struct altera_hps2fpga_data *priv = bridge->priv;

	fpga_bridge_unregister(&pdev->dev);

	clk_disable_unprepare(priv->clk);
	clk_put(priv->clk);

	return 0;
}

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);

static struct platform_driver alt_fpga_bridge_driver = {
	.probe = alt_fpga_bridge_probe,
	.remove = alt_fpga_bridge_remove,
	.driver = {
	.name	= "altera_hps2fpga_bridge",
	.of_match_table = of_match_ptr(altera_fpga_of_match),
	},
};

module_platform_driver(alt_fpga_bridge_driver);

MODULE_DESCRIPTION("Altera SoCFPGA HPS to FPGA Bridge");
MODULE_AUTHOR("Alan Tull <atull@xxxxxxxxxxxxxxxxxxxxx>");
MODULE_LICENSE("GPL v2");
