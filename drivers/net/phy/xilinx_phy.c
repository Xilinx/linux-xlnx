/* Xilinx PCS/PMA Core phy driver
 *
 * Copyright (C) 2015 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for PCS/PMA Core.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/xilinx_phy.h>

#define MII_PHY_STATUS_SPD_MASK		0x0C00
#define MII_PHY_STATUS_FULLDUPLEX	0x1000
#define MII_PHY_STATUS_1000		0x0800
#define MII_PHY_STATUS_100		0x0400
#define XPCSPMA_PHY_CTRL_ISOLATE_DISABLE 0xFBFF

static int xilinxphy_read_status(struct phy_device *phydev)
{
	int err;
	int status = 0;

	/* Update the link, but return if there
	 * was an error
	 */
	err = genphy_update_link(phydev);
	if (err)
		return err;

	if (AUTONEG_ENABLE == phydev->autoneg) {
		status = phy_read(phydev, MII_LPA);

		if (status & MII_PHY_STATUS_FULLDUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		switch (status & MII_PHY_STATUS_SPD_MASK) {
		case MII_PHY_STATUS_1000:
			phydev->speed = SPEED_1000;
			break;

		case MII_PHY_STATUS_100:
			phydev->speed = SPEED_100;
			break;

		default:
			phydev->speed = SPEED_10;
			break;
		}
	} else {
		int bmcr = phy_read(phydev, MII_BMCR);

		if (bmcr < 0)
			return bmcr;

		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		if (bmcr & BMCR_SPEED1000)
			phydev->speed = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			phydev->speed = SPEED_100;
		else
			phydev->speed = SPEED_10;
	}

	/* For 1000BASE-X Phy Mode the speed/duplex will always be
	 * 1000Mbps/fullduplex
	 */
	if (phydev->dev_flags == XAE_PHY_TYPE_1000BASE_X) {
		phydev->duplex = DUPLEX_FULL;
		phydev->speed = SPEED_1000;
	}

	return 0;
}

static int xilinxphy_of_init(struct phy_device *phydev)
{
	struct device *dev = &phydev->dev;
	struct device_node *of_node = dev->of_node;
	u32 phytype;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return 0;

	if (!of_node)
		return -ENODEV;

	if (!of_property_read_u32(of_node, "xlnx,phy-type", &phytype))
		if (phytype == XAE_PHY_TYPE_1000BASE_X)
			phydev->dev_flags |= XAE_PHY_TYPE_1000BASE_X;

	return 0;
}

static int xilinxphy_config_init(struct phy_device *phydev)
{
	int temp;

	xilinxphy_of_init(phydev);
	temp = phy_read(phydev, MII_BMCR);
	temp &= XPCSPMA_PHY_CTRL_ISOLATE_DISABLE;
	phy_write(phydev, MII_BMCR, temp);

	return 0;
}

static struct phy_driver xilinx_drivers[] = {
	{
		.phy_id = XILINX_PHY_ID,
		.phy_id_mask = XILINX_PHY_ID_MASK,
		.name = "Xilinx PCS/PMA PHY",
		.features = PHY_GBIT_FEATURES,
		.config_init = &xilinxphy_config_init,
		.config_aneg = &genphy_config_aneg,
		.read_status = &xilinxphy_read_status,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.driver = { .owner = THIS_MODULE },
	},
};

module_phy_driver(xilinx_drivers);

static struct mdio_device_id __maybe_unused xilinx_tbl[] = {
	{ XILINX_PHY_ID, XILINX_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, xilinx_tbl);
MODULE_DESCRIPTION("Xilinx PCS/PMA PHY driver");
MODULE_LICENSE("GPL");
