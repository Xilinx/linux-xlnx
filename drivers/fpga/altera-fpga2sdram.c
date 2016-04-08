/*
 * FPGA to SDRAM Bridge Driver for Altera SoCFPGA Devices
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
 * This driver manages a bridge between an FPGA and the SDRAM used by the ARM
 * host processor system (HPS).
 *
 * The bridge contains 4 read ports, 4 write ports, and 6 command ports.
 * Reconfiguring these ports requires that no SDRAM transactions occur during
 * reconfiguration. The code reconfiguring the ports cannot run out of SDRAM
 * nor can the FPGA access the SDRAM during reconfiguration. This driver does
 * not support reconfiguring the ports. The ports are configured by code
 * running out of on chip ram before Linux is started and the configuration
 * is passed in a handoff register in the system manager.
 *
 * This driver supports enabling and disabling of the configured ports, which
 * allows for safe reprogramming of the FPGA, assuming that the new FPGA image
 * uses the same port configuration. Bridges must be disabled before
 * reprogramming the FPGA and re-enabled after the FPGA has been programmed.
 */

#include <linux/fpga/fpga-bridge.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

#define ALT_SDR_CTL_FPGAPORTRST_OFST	0x80
#define ALT_SDR_CTL_FPGAPORTRST_PORTRSTN_MSK	0x00003fff
#define ALT_SDR_CTL_FPGAPORTRST_RD_SHIFT	0
#define ALT_SDR_CTL_FPGAPORTRST_WR_SHIFT	4
#define ALT_SDR_CTL_FPGAPORTRST_CTRL_SHIFT	8

#define SYSMGR_ISWGRP_HANDOFF3 (0x8C)
#define ISWGRP_HANDOFF_FPGA2SDR SYSMGR_ISWGRP_HANDOFF3

#define F2S_BRIDGE_NAME "fpga2sdram"

struct alt_fpga2sdram_data {
	struct device *dev;
	struct regmap *sdrctl;
	int mask;
};

static int alt_fpga2sdram_enable_show(struct fpga_bridge *bridge)
{
	struct alt_fpga2sdram_data *priv = bridge->priv;
	int value;

	regmap_read(priv->sdrctl, ALT_SDR_CTL_FPGAPORTRST_OFST, &value);

	return (value & priv->mask) == priv->mask;
}

static inline int _alt_fpga2sdram_enable_set(struct alt_fpga2sdram_data *priv,
	bool enable)
{
	return regmap_update_bits(priv->sdrctl, ALT_SDR_CTL_FPGAPORTRST_OFST,
	priv->mask, enable ? priv->mask : 0);
}

static int alt_fpga2sdram_enable_set(struct fpga_bridge *bridge, bool enable)
{
	return _alt_fpga2sdram_enable_set(bridge->priv, enable);
}

struct prop_map {
	char *prop_name;
	u32 *prop_value;
	u32 prop_max;
};

static const struct fpga_bridge_ops altera_fpga2sdram_br_ops = {
	.enable_set = alt_fpga2sdram_enable_set,
	.enable_show = alt_fpga2sdram_enable_show,
};

static const struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-fpga2sdram-bridge" },
	{},
};

static int alt_fpga_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct alt_fpga2sdram_data *priv;
	u32 enable;
	struct regmap *sysmgr;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
	return -ENOMEM;

	priv->dev = dev;

	priv->sdrctl = syscon_regmap_lookup_by_compatible("altr,sdr-ctl");
	if (IS_ERR(priv->sdrctl)) {
	dev_err(dev, "regmap for altr,sdr-ctl lookup failed.\n");
	return PTR_ERR(priv->sdrctl);
	}

	sysmgr = syscon_regmap_lookup_by_compatible("altr,sys-mgr");
	if (IS_ERR(priv->sdrctl)) {
	dev_err(dev, "regmap for altr,sys-mgr lookup failed.\n");
	return PTR_ERR(sysmgr);
	}

	/* Get f2s bridge configuration saved in handoff register */
	regmap_read(sysmgr, SYSMGR_ISWGRP_HANDOFF3, &priv->mask);

	ret = fpga_bridge_register(dev, F2S_BRIDGE_NAME,
	&altera_fpga2sdram_br_ops, priv);
	if (ret)
	return ret;

	dev_info(dev, "driver initialized with handoff %08x\n", priv->mask);

	if (!of_property_read_u32(dev->of_node, "bridge-enable", &enable)) {
	if (enable > 1) {
	dev_warn(dev, "invalid bridge-enable %u > 1\n", enable);
	} else {
	dev_info(dev, "%s bridge\n",
	(enable ? "enabling" : "disabling"));
	ret = _alt_fpga2sdram_enable_set(priv, enable);
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
	fpga_bridge_unregister(&pdev->dev);

	return 0;
}

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);

static struct platform_driver altera_fpga_driver = {
	.probe = alt_fpga_bridge_probe,
	.remove = alt_fpga_bridge_remove,
	.driver = {
	.name	= "altera_fpga2sdram_bridge",
	.of_match_table = of_match_ptr(altera_fpga_of_match),
	},
};

module_platform_driver(altera_fpga_driver);

MODULE_DESCRIPTION("Altera SoCFPGA FPGA to SDRAM Bridge");
MODULE_AUTHOR("Alan Tull <atull@xxxxxxxxxxxxxxxxxxxxx>");
MODULE_LICENSE("GPL v2");
