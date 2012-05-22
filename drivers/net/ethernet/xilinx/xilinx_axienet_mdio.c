/*
 * MDIO bus driver for the Xilinx Axi Ethernet device
 *
 * Copyright (c) 2009 Secret Lab Technologies, Ltd.
 * Copyright (c) 2010 Xilinx, Inc. All rights reserved.
 *
 */

#include <linux/of_address.h>
#include <linux/of_mdio.h>

#include "xilinx_axienet.h"

#define MAX_MDIO_FREQ	   2500000 /* 2.5 MHz */
#define CPU_NAME		"cpu"
#define CLOCK_FREQ_PROP_NAME    "clock-frequency"
#define DEFAULT_CLOCK_DIVISOR   29 /* If all else fails, fallback to this */

/* ----------------------------------------------------------------------------
 * MDIO Bus functions
 */

/**
 * axienet_mdio_read - MDIO interface read function
 * @bus:	Pointer to mii bus structure
 * @phy_id:	Address of the PHY device
 * @reg:	PHY register to read
 *
 * returns:	The register contents on success, -ETIMEDOUT on a timeout
 *
 * Reads the contents of the requested register from the requested PHY
 * address by first writing the details into MCR register. After a while
 * the register MRD is read to obtain the PHY register content.
 **/
static int axienet_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct axienet_local *lp = bus->priv;
	u32 rc;
	long end = jiffies + 2;


	/* Wait till MDIO interface is ready to accept a new transaction.*/
	while (!(axienet_ior(lp, XAE_MDIO_MCR_OFFSET) &
						XAE_MDIO_MCR_READY_MASK)) {

		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	axienet_iow(lp, XAE_MDIO_MCR_OFFSET,
	(((phy_id << XAE_MDIO_MCR_PHYAD_SHIFT) & XAE_MDIO_MCR_PHYAD_MASK) |
	((reg << XAE_MDIO_MCR_REGAD_SHIFT) & XAE_MDIO_MCR_REGAD_MASK)	|
	XAE_MDIO_MCR_INITIATE_MASK |
	XAE_MDIO_MCR_OP_READ_MASK));

	end = jiffies + 2;
	while (!(axienet_ior(lp, XAE_MDIO_MCR_OFFSET) &
						XAE_MDIO_MCR_READY_MASK)) {
		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	/* Read data */
	rc = axienet_ior(lp, XAE_MDIO_MRD_OFFSET) & 0x0000FFFF;
	dev_dbg(lp->dev, "axienet_mdio_read(phy_id=%i, reg=%x) == %x\n",
		phy_id, reg, rc);

	return rc;
}

/**
 * axienet_mdio_write - MDIO interface write function
 * @bus:	Pointer to mii bus structure
 * @phy_id:	Address of the PHY device
 * @reg:	PHY register to write to
 * @val:	Value to be written into the register
 *
 * returns:	0 on success, -ETIMEDOUT on a timeout
 *
 * Writes the value to the requested register by first writing the value
 * into MWD register. The the MCR register is then appropriately setup
 * to finish the write operation.
 **/
static int axienet_mdio_write(struct mii_bus *bus, int phy_id, int reg,
								u16 val)
{
	struct axienet_local *lp = bus->priv;
	long end = jiffies + 2;

	dev_dbg(lp->dev, "axienet_mdio_write(phy_id=%i, reg=%x, val=%x)\n",
		phy_id, reg, val);

	/* Wait till MDIO interface is ready to accept a new transaction.*/
	while (!(axienet_ior(lp, XAE_MDIO_MCR_OFFSET) &
						XAE_MDIO_MCR_READY_MASK)) {
		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	axienet_iow(lp, XAE_MDIO_MWD_OFFSET, (u32)val);
	axienet_iow(lp, XAE_MDIO_MCR_OFFSET,
	(((phy_id << XAE_MDIO_MCR_PHYAD_SHIFT) & XAE_MDIO_MCR_PHYAD_MASK) |
	((reg << XAE_MDIO_MCR_REGAD_SHIFT) & XAE_MDIO_MCR_REGAD_MASK)	|
	XAE_MDIO_MCR_INITIATE_MASK |
	XAE_MDIO_MCR_OP_WRITE_MASK));

	end = jiffies + 2;
	while (!(axienet_ior(lp, XAE_MDIO_MCR_OFFSET) &
						XAE_MDIO_MCR_READY_MASK)) {
		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	return 0;
}

/**
 * axienet_mdio_setup - MDIO setup function
 * @lp:		Pointer to axienet local data structure.
 * @np:		Pointer to device node
 *
 * returns:	0 on success, -ETIMEDOUT on a timeout, -ENOMEM when
 *		mdiobus_alloc (to allocate memory for mii bus structure) fails.
 *
 * Sets up the MDIO interface by initializing the MDIO clock and enabling the
 * MDIO interface in hardware. Register the MDIO interface.
 **/
int axienet_mdio_setup(struct axienet_local *lp, struct device_node *np)
{
	struct mii_bus *bus;
	int rc;
	struct resource res;
	u32 clk_div;
	long end = jiffies + 2;
	struct device_node *np1;

	/* clk_div can be calculated by deriving it from the equation:
	 * fMDIO = fHOST / ((1 + clk_div) * 2)
	 *
	 * Where fMDIO <= 2500000, so we get:
	 * fHOST / ((1 + clk_div) * 2) <= 2500000
	 *
	 * Then we get:
	 * 1 / ((1 + clk_div) * 2) <= (2500000 / fHOST)
	 *
	 * Then we get:
	 * 1 / (1 + clk_div) <= ((2500000 * 2) / fHOST)
	 *
	 * Then we get:
	 * 1 / (1 + clk_div) <= (5000000 / fHOST)
	 *
	 * So:
	 * (1 + clk_div) >= (fHOST / 5000000)
	 *
	 * And finally:
	 * clk_div >= (fHOST / 5000000) - 1
	 *
	 * fHOST can be read from the flattened device tree as property "clock-frequency"
	 * from the CPU
	 */
	np1 = of_find_node_by_name(NULL, CPU_NAME);
	if(NULL == np1)
	{
		printk(KERN_WARNING "%s(): Could not find CPU device node.", __FUNCTION__);
		printk(KERN_WARNING "Setting MDIO clock divisor to default %d\n",
		       DEFAULT_CLOCK_DIVISOR);
		clk_div = DEFAULT_CLOCK_DIVISOR;
	}
	else
	{
		u32 *property_p;

		property_p = (uint32_t *)of_get_property(np1, CLOCK_FREQ_PROP_NAME, NULL);
		if(NULL == property_p)
		{
			printk(KERN_WARNING "%s(): Could not find CPU property %s.",
			       __FUNCTION__, CLOCK_FREQ_PROP_NAME);
			printk(KERN_WARNING "Setting MDIO clock divisor to default %d\n",
			       DEFAULT_CLOCK_DIVISOR);
			clk_div = DEFAULT_CLOCK_DIVISOR;
		}
		else
		{
			u32 host_clock = be32_to_cpup(property_p);

			clk_div = (host_clock / (MAX_MDIO_FREQ * 2)) - 1;

			/* If there is any remainder from the division of fHOST / (MAX_MDIO_FREQ * 2),
			 * then we need to add 1 to the clock divisor or we will surely be
			 * above 2.5 MHz */
			if(host_clock % (MAX_MDIO_FREQ * 2))
				clk_div++;
			printk(KERN_DEBUG "%s(): Setting MDIO clock divisor to %u based on %u Hz host clock.\n",
			       __FUNCTION__, clk_div, host_clock);
		}
		of_node_put(np1);
	}

	axienet_iow(lp, XAE_MDIO_MC_OFFSET, (((u32)clk_div) |
						XAE_MDIO_MC_MDIOEN_MASK));
	while (!(axienet_ior(lp, XAE_MDIO_MCR_OFFSET) &
						XAE_MDIO_MCR_READY_MASK)) {
		if (end - jiffies <= 0) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	np1 = of_get_parent(lp->phy_node);
	of_address_to_resource(np1, 0, &res);
	snprintf(bus->id, MII_BUS_ID_SIZE, "%.8llx",
		 (unsigned long long)res.start);
	bus->priv = lp;
	bus->name = "Xilinx Axi Ethernet MDIO";
	bus->read = axienet_mdio_read;
	bus->write = axienet_mdio_write;
	bus->parent = lp->dev;
	bus->irq = lp->mdio_irqs; /* preallocated IRQ table */

	lp->mii_bus = bus;

	rc = of_mdiobus_register(bus, np1);
	if (rc) {
		mdiobus_free(bus);
		return rc;
	}

	return 0;
}

/**
 * axienet_mdio_teardown - MDIO remove function
 * @lp:		Pointer to axienet local data structure.
 *
 * Unregisters the MDIO and frees any associate memory for mii bus.
 **/
void axienet_mdio_teardown(struct axienet_local *lp)
{
	mdiobus_unregister(lp->mii_bus);
	kfree(lp->mii_bus->irq);
	mdiobus_free(lp->mii_bus);
	lp->mii_bus = NULL;
}

