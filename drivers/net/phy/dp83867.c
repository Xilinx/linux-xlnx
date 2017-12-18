/*
 * Driver for the Texas Instruments DP83867 PHY
 *
 * Copyright (C) 2015 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>

#include <dt-bindings/net/ti-dp83867.h>

#define DP83867_PHY_ID		0x2000a231
#define DP83867_DEVADDR		0x1f

#define MII_DP83867_PHYCTRL	0x10
#define MII_DP83867_MICR	0x12
#define MII_DP83867_ISR		0x13
#define MII_DP83867_CFG2	0x14
#define MII_DP83867_BISCR	0x16
#define DP83867_CTRL		0x1f

/* Extended Registers */
#define DP83867_CFG4		0x0031
#define DP83867_RGMIICTL	0x0032
#define DP83867_RGMIIDCTL	0x0086

#define DP83867_SW_RESET	BIT(15)
#define DP83867_SW_RESTART	BIT(14)

/* MICR Interrupt bits */
#define MII_DP83867_MICR_AN_ERR_INT_EN		BIT(15)
#define MII_DP83867_MICR_SPEED_CHNG_INT_EN	BIT(14)
#define MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN	BIT(13)
#define MII_DP83867_MICR_PAGE_RXD_INT_EN	BIT(12)
#define MII_DP83867_MICR_AUTONEG_COMP_INT_EN	BIT(11)
#define MII_DP83867_MICR_LINK_STS_CHNG_INT_EN	BIT(10)
#define MII_DP83867_MICR_FALSE_CARRIER_INT_EN	BIT(8)
#define MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN	BIT(4)
#define MII_DP83867_MICR_WOL_INT_EN		BIT(3)
#define MII_DP83867_MICR_XGMII_ERR_INT_EN	BIT(2)
#define MII_DP83867_MICR_POL_CHNG_INT_EN	BIT(1)
#define MII_DP83867_MICR_JABBER_INT_EN		BIT(0)

/* RGMIICTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_EN		BIT(1)
#define DP83867_RGMII_RX_CLK_DELAY_EN		BIT(0)

/* PHY CTRL bits */
#define DP83867_PHYCR_FIFO_DEPTH_SHIFT		14
#define DP83867_PHYCR_FIFO_DEPTH_MASK		(3 << 14)
#define DP83867_MDI_CROSSOVER		5
#define DP83867_MDI_CROSSOVER_AUTO	0b10
#define DP83867_MDI_CROSSOVER_MDIX	0b01
#define DP83867_PHYCTRL_SGMIIEN			0x0800
#define DP83867_PHYCTRL_RXFIFO_SHIFT	12
#define DP83867_PHYCTRL_TXFIFO_SHIFT	14

/* RGMIIDCTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_SHIFT	4

/* CFG2 bits */
#define MII_DP83867_CFG2_SPEEDOPT_10EN		0x0040
#define MII_DP83867_CFG2_SGMII_AUTONEGEN	0x0080
#define MII_DP83867_CFG2_SPEEDOPT_ENH		0x0100
#define MII_DP83867_CFG2_SPEEDOPT_CNT		0x0800
#define MII_DP83867_CFG2_SPEEDOPT_INTLOW	0x2000
#define MII_DP83867_CFG2_MASK			0x003F

/* CFG4 bits */
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_MASK	0x60
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_16MS	0x00
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_2US	0x20
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_800US	0x40
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_11MS	0x60
#define DP83867_CFG4_RESVDBIT7	BIT(7)
#define DP83867_CFG4_RESVDBIT8	BIT(8)

struct dp83867_private {
	int rx_id_delay;
	int tx_id_delay;
	int fifo_depth;
	bool rxctrl_strap_worka;
};

static int dp83867_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DP83867_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int dp83867_config_intr(struct phy_device *phydev)
{
	int micr_status;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		micr_status = phy_read(phydev, MII_DP83867_MICR);
		if (micr_status < 0)
			return micr_status;

		micr_status |=
			(MII_DP83867_MICR_AN_ERR_INT_EN |
			MII_DP83867_MICR_SPEED_CHNG_INT_EN |
			MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN |
			MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN);

		return phy_write(phydev, MII_DP83867_MICR, micr_status);
	}

	micr_status = 0x0;
	return phy_write(phydev, MII_DP83867_MICR, micr_status);
}

#ifdef CONFIG_OF_MDIO
static int dp83867_of_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	int ret;

	if (!of_node)
		return -ENODEV;

	ret = of_property_read_u32(of_node, "ti,rx-internal-delay",
				   &dp83867->rx_id_delay);
	if (ret)
		return ret;

	ret = of_property_read_u32(of_node, "ti,tx-internal-delay",
				   &dp83867->tx_id_delay);
	if (ret)
		return ret;

	dp83867->rxctrl_strap_worka =
				of_property_read_bool(of_node,
						      "ti,rxctrl-strap-worka");

	return of_property_read_u32(of_node, "ti,fifo-depth",
				   &dp83867->fifo_depth);
}
#else
static int dp83867_of_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int dp83867_config_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867;
	int ret;
	u16 val, delay, cfg2;

	if (!phydev->priv) {
		dp83867 = devm_kzalloc(&phydev->mdio.dev, sizeof(*dp83867),
				       GFP_KERNEL);
		if (!dp83867)
			return -ENOMEM;

		phydev->priv = dp83867;
		ret = dp83867_of_init(phydev);
		if (ret)
			return ret;
	} else {
		dp83867 = (struct dp83867_private *)phydev->priv;
	}

	if (phy_interface_is_rgmii(phydev)) {
		ret = phy_write(phydev, MII_DP83867_PHYCTRL,
			(DP83867_MDI_CROSSOVER_AUTO << DP83867_MDI_CROSSOVER) |
			(dp83867->fifo_depth << DP83867_PHYCR_FIFO_DEPTH_SHIFT));
		if (ret)
			return ret;

		val = phy_read(phydev, MII_DP83867_PHYCTRL);
		if (val < 0)
			return val;
		val &= ~DP83867_PHYCR_FIFO_DEPTH_MASK;
		val |= (dp83867->fifo_depth << DP83867_PHYCR_FIFO_DEPTH_SHIFT);
		ret = phy_write(phydev, MII_DP83867_PHYCTRL, val);
		if (ret)
			return ret;

		/* This is a SW workaround for link instability if
		 * RX_CTRL is not strapped to mode 3 or 4 in HW.
		 */
		if (dp83867->rxctrl_strap_worka) {
			val = phy_read_mmd_indirect(phydev, DP83867_CFG4,
						    DP83867_DEVADDR);
			val &= ~DP83867_CFG4_RESVDBIT7;
			phy_write_mmd_indirect(phydev, DP83867_CFG4,
					       DP83867_DEVADDR, val);
		}
	} else {
		phy_write(phydev, MII_BMCR,
			  (BMCR_ANENABLE | BMCR_FULLDPLX | BMCR_SPEED1000));

		cfg2 = phy_read(phydev, MII_DP83867_CFG2);
		cfg2 &= MII_DP83867_CFG2_MASK;
		cfg2 |= (MII_DP83867_CFG2_SPEEDOPT_10EN |
			 MII_DP83867_CFG2_SGMII_AUTONEGEN |
			 MII_DP83867_CFG2_SPEEDOPT_ENH |
			 MII_DP83867_CFG2_SPEEDOPT_CNT |
			 MII_DP83867_CFG2_SPEEDOPT_INTLOW);
		phy_write(phydev, MII_DP83867_CFG2, cfg2);

		phy_write_mmd_indirect(phydev, DP83867_RGMIICTL,
				       DP83867_DEVADDR, 0x0);

		phy_write(phydev, MII_DP83867_PHYCTRL,
			  DP83867_PHYCTRL_SGMIIEN |
			  (DP83867_MDI_CROSSOVER_MDIX << DP83867_MDI_CROSSOVER) |
			  (dp83867->fifo_depth << DP83867_PHYCTRL_RXFIFO_SHIFT) |
			  (dp83867->fifo_depth  << DP83867_PHYCTRL_TXFIFO_SHIFT));
		phy_write(phydev, MII_DP83867_BISCR, 0x0);

		/* This is a SW workaround for link instability if
		 * RX_CTRL is not strapped to mode 3 or 4 in HW.
		 */
		if (dp83867->rxctrl_strap_worka) {
			val = phy_read_mmd_indirect(phydev, DP83867_CFG4,
						    DP83867_DEVADDR);
			val &= ~DP83867_CFG4_RESVDBIT7;
			val |= DP83867_CFG4_RESVDBIT8;
			val &= ~DP83867_CFG4_SGMII_AUTONEG_TIMER_MASK;
			val |= DP83867_CFG4_SGMII_AUTONEG_TIMER_11MS;
			phy_write_mmd_indirect(phydev, DP83867_CFG4,
					       DP83867_DEVADDR, val);
		}
	}

	if ((phydev->interface >= PHY_INTERFACE_MODE_RGMII_ID) &&
	    (phydev->interface <= PHY_INTERFACE_MODE_RGMII_RXID)) {
		val = phy_read_mmd_indirect(phydev, DP83867_RGMIICTL,
					    DP83867_DEVADDR);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			val |= (DP83867_RGMII_TX_CLK_DELAY_EN | DP83867_RGMII_RX_CLK_DELAY_EN);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			val |= DP83867_RGMII_TX_CLK_DELAY_EN;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			val |= DP83867_RGMII_RX_CLK_DELAY_EN;

		phy_write_mmd_indirect(phydev, DP83867_RGMIICTL,
				       DP83867_DEVADDR, val);

		delay = (dp83867->rx_id_delay |
			(dp83867->tx_id_delay << DP83867_RGMII_TX_CLK_DELAY_SHIFT));

		phy_write_mmd_indirect(phydev, DP83867_RGMIIDCTL,
				       DP83867_DEVADDR, delay);
	}

	return 0;
}

static int dp83867_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, DP83867_CTRL, DP83867_SW_RESET);
	if (err < 0)
		return err;

	return dp83867_config_init(phydev);
}

static struct phy_driver dp83867_driver[] = {
	{
		.phy_id		= DP83867_PHY_ID,
		.phy_id_mask	= 0xfffffff0,
		.name		= "TI DP83867",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,

		.config_init	= dp83867_config_init,
		.soft_reset	= dp83867_phy_reset,

		/* IRQ related */
		.ack_interrupt	= dp83867_ack_interrupt,
		.config_intr	= dp83867_config_intr,

		.config_aneg	= genphy_config_aneg,
		.read_status	= genphy_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};
module_phy_driver(dp83867_driver);

static struct mdio_device_id __maybe_unused dp83867_tbl[] = {
	{ DP83867_PHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, dp83867_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83867 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL");
