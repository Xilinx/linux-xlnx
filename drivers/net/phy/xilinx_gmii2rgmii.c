/* Xilinx GMII2RGMII Converter driver
 *
 * Copyright (C) 2016 Xilinx, Inc.
 * Copyright (C) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * Author: Andrew Lunn <andrew@lunn.ch>
 * Author: Kedareswara rao Appana <appanad@xilinx.com>
 *
 * Description:
 * This driver is developed for Xilinx GMII2RGMII Converter
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/of_mdio.h>

#define XILINX_GMII2RGMII_REG		0x10
#define XILINX_GMII2RGMII_SPEED_MASK	(BMCR_SPEED1000 | BMCR_SPEED100)

struct gmii2rgmii {
	struct phy_device *phy_dev;
	struct phy_driver *phy_drv;
	struct phy_driver conv_phy_drv;
	int addr;
};

static int xgmiitorgmii_read_status(struct phy_device *phydev)
{
	struct gmii2rgmii *priv = phydev->priv;
	u16 val = 0;

	priv->phy_drv->read_status(phydev);

	val = mdiobus_read(phydev->mdio.bus, priv->addr, XILINX_GMII2RGMII_REG);
	val &= ~XILINX_GMII2RGMII_SPEED_MASK;

	if (phydev->speed == SPEED_1000)
		val |= BMCR_SPEED1000;
	else if (phydev->speed == SPEED_100)
		val |= BMCR_SPEED100;
	else
		val |= BMCR_SPEED10;

	mdiobus_write(phydev->mdio.bus, priv->addr, XILINX_GMII2RGMII_REG, val);

	return 0;
}

static int xgmiitorgmii_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node, *phy_node;
	struct gmii2rgmii *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phy_node = of_parse_phandle(np, "phy-handle", 0);
	if (!phy_node) {
		dev_err(dev, "Couldn't parse phy-handle\n");
		return -ENODEV;
	}

	priv->phy_dev = of_phy_find_device(phy_node);
	of_node_put(phy_node);
	if (!priv->phy_dev) {
		dev_info(dev, "Couldn't find phydev\n");
		return -EPROBE_DEFER;
	}

	priv->addr = mdiodev->addr;
	priv->phy_drv = priv->phy_dev->drv;
	memcpy(&priv->conv_phy_drv, priv->phy_dev->drv,
	       sizeof(struct phy_driver));
	priv->conv_phy_drv.read_status = xgmiitorgmii_read_status;
	priv->phy_dev->priv = priv;
	priv->phy_dev->drv = &priv->conv_phy_drv;

	return 0;
}

static const struct of_device_id xgmiitorgmii_of_match[] = {
	{ .compatible = "xlnx,gmii-to-rgmii-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, xgmiitorgmii_of_match);

static struct mdio_driver xgmiitorgmii_driver = {
	.probe	= xgmiitorgmii_probe,
	.mdiodrv.driver = {
		.name = "xgmiitorgmii",
		.of_match_table = xgmiitorgmii_of_match,
	},
};

mdio_module_driver(xgmiitorgmii_driver);

MODULE_DESCRIPTION("Xilinx GMII2RGMII converter driver");
MODULE_LICENSE("GPL");
