/*******************************************************************************
  STMMAC Ethernet Driver -- MDIO bus implementation
  Provides Bus interface for MII registers

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Carl Shaw <carl.shaw@st.com>
  Maintainer: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <asm/io.h>

#include "stmmac.h"

#define MII_BUSY 0x00000001
#define MII_WRITE 0x00000002

/* GMAC4 defines */
#define MII_GMAC4_GOC_SHIFT		2
#define MII_GMAC4_WRITE			(1 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_READ			(3 << MII_GMAC4_GOC_SHIFT)

#define MII_PHY_ADDR_GMAC4_SHIFT	21
#define MII_PHY_ADDR_GMAC4_MASK		GENMASK(25, 21)
#define MII_PHY_REG_GMAC4_SHIFT		16
#define MII_PHY_REG_GMAC4_MASK		GENMASK(20, 16)
#define MII_CSR_CLK_GMAC4_SHIFT		8
#define MII_CSR_CLK_GMAC4_MASK		GENMASK(11, 8)

static int stmmac_mdio_busy_wait(void __iomem *ioaddr, unsigned int mii_addr)
{
	unsigned long curr;
	unsigned long finish = jiffies + 3 * HZ;

	do {
		curr = jiffies;
		if (readl(ioaddr + mii_addr) & MII_BUSY)
			cpu_relax();
		else
			return 0;
	} while (!time_after_eq(curr, finish));

	return -EBUSY;
}

/**
 * stmmac_mdio_read
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 15-11
 * @phyreg: MII addr reg bits 10-6
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
static int stmmac_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;

	int data;
	u16 regValue = (((phyaddr << 11) & (0x0000F800)) |
			((phyreg << 6) & (0x000007C0)));
	regValue |= MII_BUSY | ((priv->clk_csr & 0xF) << 2);

	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	writel(regValue, priv->ioaddr + mii_address);

	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	/* Read the data from the MII data register */
	data = (int)readl(priv->ioaddr + mii_data);

	return data;
}

/**
 * stmmac_mdio_write
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 15-11
 * @phyreg: MII addr reg bits 10-6
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
static int stmmac_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg,
			     u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;

	u16 value =
	    (((phyaddr << 11) & (0x0000F800)) | ((phyreg << 6) & (0x000007C0)))
	    | MII_WRITE;

	value |= MII_BUSY | ((priv->clk_csr & 0xF) << 2);

	/* Wait until any existing MII operation is complete */
	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	/* Set the MII address register to write */
	writel(phydata, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	return stmmac_mdio_busy_wait(priv->ioaddr, mii_address);
}

/**
 * stmmac_mdio_read_gmac4
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 25-21
 * @phyreg: MII addr reg bits 20-16
 * Description: it reads data from the MII register of GMAC4 from within
 * the phy device.
 */
static int stmmac_mdio_read_gmac4(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	int data;
	u32 value = (((phyaddr << MII_PHY_ADDR_GMAC4_SHIFT) &
		     (MII_PHY_ADDR_GMAC4_MASK)) |
		     ((phyreg << MII_PHY_REG_GMAC4_SHIFT) &
		     (MII_PHY_REG_GMAC4_MASK))) | MII_GMAC4_READ;

	value |= MII_BUSY | ((priv->clk_csr & MII_CSR_CLK_GMAC4_MASK)
		 << MII_CSR_CLK_GMAC4_SHIFT);

	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	writel(value, priv->ioaddr + mii_address);

	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	/* Read the data from the MII data register */
	data = (int)readl(priv->ioaddr + mii_data);

	return data;
}

/**
 * stmmac_mdio_write_gmac4
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 25-21
 * @phyreg: MII addr reg bits 20-16
 * @phydata: phy data
 * Description: it writes the data into the MII register of GMAC4 from within
 * the device.
 */
static int stmmac_mdio_write_gmac4(struct mii_bus *bus, int phyaddr, int phyreg,
				   u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;

	u32 value = (((phyaddr << MII_PHY_ADDR_GMAC4_SHIFT) &
		     (MII_PHY_ADDR_GMAC4_MASK)) |
		     ((phyreg << MII_PHY_REG_GMAC4_SHIFT) &
		     (MII_PHY_REG_GMAC4_MASK))) | MII_GMAC4_WRITE;

	value |= MII_BUSY | ((priv->clk_csr & MII_CSR_CLK_GMAC4_MASK)
		 << MII_CSR_CLK_GMAC4_SHIFT);

	/* Wait until any existing MII operation is complete */
	if (stmmac_mdio_busy_wait(priv->ioaddr, mii_address))
		return -EBUSY;

	/* Set the MII address register to write */
	writel(phydata, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	return stmmac_mdio_busy_wait(priv->ioaddr, mii_address);
}

/**
 * stmmac_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
int stmmac_mdio_reset(struct mii_bus *bus)
{
#if defined(CONFIG_STMMAC_PLATFORM)
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	struct stmmac_mdio_bus_data *data = priv->plat->mdio_bus_data;

#ifdef CONFIG_OF
	if (priv->device->of_node) {

		if (data->reset_gpio < 0) {
			struct device_node *np = priv->device->of_node;
			if (!np)
				return 0;

			data->reset_gpio = of_get_named_gpio(np,
						"snps,reset-gpio", 0);
			if (data->reset_gpio < 0)
				return 0;

			data->active_low = of_property_read_bool(np,
						"snps,reset-active-low");
			of_property_read_u32_array(np,
				"snps,reset-delays-us", data->delays, 3);

			if (gpio_request(data->reset_gpio, "mdio-reset"))
				return 0;
		}

		gpio_direction_output(data->reset_gpio,
				      data->active_low ? 1 : 0);
		if (data->delays[0])
			msleep(DIV_ROUND_UP(data->delays[0], 1000));

		gpio_set_value(data->reset_gpio, data->active_low ? 0 : 1);
		if (data->delays[1])
			msleep(DIV_ROUND_UP(data->delays[1], 1000));

		gpio_set_value(data->reset_gpio, data->active_low ? 1 : 0);
		if (data->delays[2])
			msleep(DIV_ROUND_UP(data->delays[2], 1000));
	}
#endif

	if (data->phy_reset) {
		pr_debug("stmmac_mdio_reset: calling phy_reset\n");
		data->phy_reset(priv->plat->bsp_priv);
	}

	/* This is a workaround for problems with the STE101P PHY.
	 * It doesn't complete its reset until at least one clock cycle
	 * on MDC, so perform a dummy mdio read. To be upadted for GMAC4
	 * if needed.
	 */
	if (!priv->plat->has_gmac4)
		writel(0, priv->ioaddr + mii_address);
#endif
	return 0;
}

/**
 * stmmac_mdio_register
 * @ndev: net device structure
 * Description: it registers the MII bus
 */
int stmmac_mdio_register(struct net_device *ndev)
{
	int err = 0;
	struct mii_bus *new_bus;
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct stmmac_mdio_bus_data *mdio_bus_data = priv->plat->mdio_bus_data;
	struct device_node *mdio_node = priv->plat->mdio_node;
	int addr, found;

	if (!mdio_bus_data)
		return 0;

	new_bus = mdiobus_alloc();
	if (new_bus == NULL)
		return -ENOMEM;

	if (mdio_bus_data->irqs)
		memcpy(new_bus->irq, mdio_bus_data->irqs, sizeof(new_bus->irq));

#ifdef CONFIG_OF
	if (priv->device->of_node)
		mdio_bus_data->reset_gpio = -1;
#endif

	new_bus->name = "stmmac";
	if (priv->plat->has_gmac4) {
		new_bus->read = &stmmac_mdio_read_gmac4;
		new_bus->write = &stmmac_mdio_write_gmac4;
	} else {
		new_bus->read = &stmmac_mdio_read;
		new_bus->write = &stmmac_mdio_write;
	}

	new_bus->reset = &stmmac_mdio_reset;
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 new_bus->name, priv->plat->bus_id);
	new_bus->priv = ndev;
	new_bus->phy_mask = mdio_bus_data->phy_mask;
	new_bus->parent = priv->device;

	if (mdio_node)
		err = of_mdiobus_register(new_bus, mdio_node);
	else
		err = mdiobus_register(new_bus);
	if (err != 0) {
		pr_err("%s: Cannot register as MDIO bus\n", new_bus->name);
		goto bus_register_fail;
	}

	if (priv->plat->phy_node || mdio_node)
		goto bus_register_done;

	found = 0;
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		struct phy_device *phydev = mdiobus_get_phy(new_bus, addr);
		if (phydev) {
			int act = 0;
			char irq_num[4];
			char *irq_str;

			/*
			 * If an IRQ was provided to be assigned after
			 * the bus probe, do it here.
			 */
			if ((mdio_bus_data->irqs == NULL) &&
			    (mdio_bus_data->probed_phy_irq > 0)) {
				new_bus->irq[addr] =
					mdio_bus_data->probed_phy_irq;
				phydev->irq = mdio_bus_data->probed_phy_irq;
			}

			/*
			 * If we're going to bind the MAC to this PHY bus,
			 * and no PHY number was provided to the MAC,
			 * use the one probed here.
			 */
			if (priv->plat->phy_addr == -1)
				priv->plat->phy_addr = addr;

			act = (priv->plat->phy_addr == addr);
			switch (phydev->irq) {
			case PHY_POLL:
				irq_str = "POLL";
				break;
			case PHY_IGNORE_INTERRUPT:
				irq_str = "IGNORE";
				break;
			default:
				sprintf(irq_num, "%d", phydev->irq);
				irq_str = irq_num;
				break;
			}
			pr_info("%s: PHY ID %08x at %d IRQ %s (%s)%s\n",
				ndev->name, phydev->phy_id, addr,
				irq_str, phydev_name(phydev),
				act ? " active" : "");
			found = 1;
		}
	}

	if (!found && !mdio_node) {
		pr_warn("%s: No PHY found\n", ndev->name);
		mdiobus_unregister(new_bus);
		mdiobus_free(new_bus);
		return -ENODEV;
	}

bus_register_done:
	priv->mii = new_bus;

	return 0;

bus_register_fail:
	mdiobus_free(new_bus);
	return err;
}

/**
 * stmmac_mdio_unregister
 * @ndev: net device structure
 * Description: it unregisters the MII bus
 */
int stmmac_mdio_unregister(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (!priv->mii)
		return 0;

	mdiobus_unregister(priv->mii);
	priv->mii->priv = NULL;
	mdiobus_free(priv->mii);
	priv->mii = NULL;

	return 0;
}
