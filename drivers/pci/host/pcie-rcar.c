/*
 * PCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2014 Renesas Electronics Europe Ltd
 *
 * Based on:
 *  arch/sh/drivers/pci/pcie-sh7786.c
 *  arch/sh/drivers/pci/ops-sh7786.c
 *  Copyright (C) 2009 - 2011  Paul Mundt
 *
 * Author: Phil Edworthy <phil.edworthy@renesas.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#define PCIECAR			0x000010
#define PCIECCTLR		0x000018
#define  CONFIG_SEND_ENABLE	(1 << 31)
#define  TYPE0			(0 << 8)
#define  TYPE1			(1 << 8)
#define PCIECDR			0x000020
#define PCIEMSR			0x000028
#define PCIEINTXR		0x000400
#define PCIEMSITXR		0x000840

/* Transfer control */
#define PCIETCTLR		0x02000
#define  CFINIT			1
#define PCIETSTR		0x02004
#define  DATA_LINK_ACTIVE	1
#define PCIEERRFR		0x02020
#define  UNSUPPORTED_REQUEST	(1 << 4)
#define PCIEMSIFR		0x02044
#define PCIEMSIALR		0x02048
#define  MSIFE			1
#define PCIEMSIAUR		0x0204c
#define PCIEMSIIER		0x02050

/* root port address */
#define PCIEPRAR(x)		(0x02080 + ((x) * 0x4))

/* local address reg & mask */
#define PCIELAR(x)		(0x02200 + ((x) * 0x20))
#define PCIELAMR(x)		(0x02208 + ((x) * 0x20))
#define  LAM_PREFETCH		(1 << 3)
#define  LAM_64BIT		(1 << 2)
#define  LAR_ENABLE		(1 << 1)

/* PCIe address reg & mask */
#define PCIEPALR(x)		(0x03400 + ((x) * 0x20))
#define PCIEPAUR(x)		(0x03404 + ((x) * 0x20))
#define PCIEPAMR(x)		(0x03408 + ((x) * 0x20))
#define PCIEPTCTLR(x)		(0x0340c + ((x) * 0x20))
#define  PAR_ENABLE		(1 << 31)
#define  IO_SPACE		(1 << 8)

/* Configuration */
#define PCICONF(x)		(0x010000 + ((x) * 0x4))
#define PMCAP(x)		(0x010040 + ((x) * 0x4))
#define EXPCAP(x)		(0x010070 + ((x) * 0x4))
#define VCCAP(x)		(0x010100 + ((x) * 0x4))

/* link layer */
#define IDSETR1			0x011004
#define TLCTLR			0x011048
#define MACSR			0x011054
#define  SPCHGFIN		(1 << 4)
#define  SPCHGFAIL		(1 << 6)
#define  SPCHGSUC		(1 << 7)
#define  LINK_SPEED		(0xf << 16)
#define  LINK_SPEED_2_5GTS	(1 << 16)
#define  LINK_SPEED_5_0GTS	(2 << 16)
#define MACCTLR			0x011058
#define  SPEED_CHANGE		(1 << 24)
#define  SCRAMBLE_DISABLE	(1 << 27)
#define MACS2R			0x011078
#define MACCGSPSETR		0x011084
#define  SPCNGRSN		(1 << 31)

/* R-Car H1 PHY */
#define H1_PCIEPHYADRR		0x04000c
#define  WRITE_CMD		(1 << 16)
#define  PHY_ACK		(1 << 24)
#define  RATE_POS		12
#define  LANE_POS		8
#define  ADR_POS		0
#define H1_PCIEPHYDOUTR		0x040014
#define H1_PCIEPHYSR		0x040018

/* R-Car Gen2 PHY */
#define GEN2_PCIEPHYADDR	0x780
#define GEN2_PCIEPHYDATA	0x784
#define GEN2_PCIEPHYCTRL	0x78c

#define INT_PCI_MSI_NR	32

#define RCONF(x)	(PCICONF(0)+(x))
#define RPMCAP(x)	(PMCAP(0)+(x))
#define REXPCAP(x)	(EXPCAP(0)+(x))
#define RVCCAP(x)	(VCCAP(0)+(x))

#define  PCIE_CONF_BUS(b)	(((b) & 0xff) << 24)
#define  PCIE_CONF_DEV(d)	(((d) & 0x1f) << 19)
#define  PCIE_CONF_FUNC(f)	(((f) & 0x7) << 16)

#define RCAR_PCI_MAX_RESOURCES 4
#define MAX_NR_INBOUND_MAPS 6

struct rcar_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct msi_controller chip;
	unsigned long pages;
	struct mutex lock;
	int irq1;
	int irq2;
};

static inline struct rcar_msi *to_rcar_msi(struct msi_controller *chip)
{
	return container_of(chip, struct rcar_msi, chip);
}

/* Structure representing the PCIe interface */
struct rcar_pcie {
	struct device		*dev;
	void __iomem		*base;
	struct list_head	resources;
	int			root_bus_nr;
	struct clk		*clk;
	struct clk		*bus_clk;
	struct			rcar_msi msi;
};

static void rcar_pci_write_reg(struct rcar_pcie *pcie, unsigned long val,
			       unsigned long reg)
{
	writel(val, pcie->base + reg);
}

static unsigned long rcar_pci_read_reg(struct rcar_pcie *pcie,
				       unsigned long reg)
{
	return readl(pcie->base + reg);
}

enum {
	RCAR_PCI_ACCESS_READ,
	RCAR_PCI_ACCESS_WRITE,
};

static void rcar_rmw32(struct rcar_pcie *pcie, int where, u32 mask, u32 data)
{
	int shift = 8 * (where & 3);
	u32 val = rcar_pci_read_reg(pcie, where & ~3);

	val &= ~(mask << shift);
	val |= data << shift;
	rcar_pci_write_reg(pcie, val, where & ~3);
}

static u32 rcar_read_conf(struct rcar_pcie *pcie, int where)
{
	int shift = 8 * (where & 3);
	u32 val = rcar_pci_read_reg(pcie, where & ~3);

	return val >> shift;
}

/* Serialization is provided by 'pci_lock' in drivers/pci/access.c */
static int rcar_pcie_config_access(struct rcar_pcie *pcie,
		unsigned char access_type, struct pci_bus *bus,
		unsigned int devfn, int where, u32 *data)
{
	int dev, func, reg, index;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);
	reg = where & ~3;
	index = reg / 4;

	/*
	 * While each channel has its own memory-mapped extended config
	 * space, it's generally only accessible when in endpoint mode.
	 * When in root complex mode, the controller is unable to target
	 * itself with either type 0 or type 1 accesses, and indeed, any
	 * controller initiated target transfer to its own config space
	 * result in a completer abort.
	 *
	 * Each channel effectively only supports a single device, but as
	 * the same channel <-> device access works for any PCI_SLOT()
	 * value, we cheat a bit here and bind the controller's config
	 * space to devfn 0 in order to enable self-enumeration. In this
	 * case the regular ECAR/ECDR path is sidelined and the mangled
	 * config access itself is initiated as an internal bus transaction.
	 */
	if (pci_is_root_bus(bus)) {
		if (dev != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (access_type == RCAR_PCI_ACCESS_READ) {
			*data = rcar_pci_read_reg(pcie, PCICONF(index));
		} else {
			/* Keep an eye out for changes to the root bus number */
			if (pci_is_root_bus(bus) && (reg == PCI_PRIMARY_BUS))
				pcie->root_bus_nr = *data & 0xff;

			rcar_pci_write_reg(pcie, *data, PCICONF(index));
		}

		return PCIBIOS_SUCCESSFUL;
	}

	if (pcie->root_bus_nr < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Clear errors */
	rcar_pci_write_reg(pcie, rcar_pci_read_reg(pcie, PCIEERRFR), PCIEERRFR);

	/* Set the PIO address */
	rcar_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
		PCIE_CONF_DEV(dev) | PCIE_CONF_FUNC(func) | reg, PCIECAR);

	/* Enable the configuration access */
	if (bus->parent->number == pcie->root_bus_nr)
		rcar_pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE0, PCIECCTLR);
	else
		rcar_pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE1, PCIECCTLR);

	/* Check for errors */
	if (rcar_pci_read_reg(pcie, PCIEERRFR) & UNSUPPORTED_REQUEST)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check for master and target aborts */
	if (rcar_read_conf(pcie, RCONF(PCI_STATUS)) &
		(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (access_type == RCAR_PCI_ACCESS_READ)
		*data = rcar_pci_read_reg(pcie, PCIECDR);
	else
		rcar_pci_write_reg(pcie, *data, PCIECDR);

	/* Disable the configuration access */
	rcar_pci_write_reg(pcie, 0, PCIECCTLR);

	return PCIBIOS_SUCCESSFUL;
}

static int rcar_pcie_read_conf(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct rcar_pcie *pcie = bus->sysdata;
	int ret;

	ret = rcar_pcie_config_access(pcie, RCAR_PCI_ACCESS_READ,
				      bus, devfn, where, val);
	if (ret != PCIBIOS_SUCCESSFUL) {
		*val = 0xffffffff;
		return ret;
	}

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 2))) & 0xffff;

	dev_dbg(&bus->dev, "pcie-config-read: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08lx\n",
		bus->number, devfn, where, size, (unsigned long)*val);

	return ret;
}

/* Serialization is provided by 'pci_lock' in drivers/pci/access.c */
static int rcar_pcie_write_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	struct rcar_pcie *pcie = bus->sysdata;
	int shift, ret;
	u32 data;

	ret = rcar_pcie_config_access(pcie, RCAR_PCI_ACCESS_READ,
				      bus, devfn, where, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	dev_dbg(&bus->dev, "pcie-config-write: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08lx\n",
		bus->number, devfn, where, size, (unsigned long)val);

	if (size == 1) {
		shift = 8 * (where & 3);
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
	} else if (size == 2) {
		shift = 8 * (where & 2);
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
	} else
		data = val;

	ret = rcar_pcie_config_access(pcie, RCAR_PCI_ACCESS_WRITE,
				      bus, devfn, where, &data);

	return ret;
}

static struct pci_ops rcar_pcie_ops = {
	.read	= rcar_pcie_read_conf,
	.write	= rcar_pcie_write_conf,
};

static void rcar_pcie_setup_window(int win, struct rcar_pcie *pcie,
				   struct resource *res)
{
	/* Setup PCIe address space mappings for each resource */
	resource_size_t size;
	resource_size_t res_start;
	u32 mask;

	rcar_pci_write_reg(pcie, 0x00000000, PCIEPTCTLR(win));

	/*
	 * The PAMR mask is calculated in units of 128Bytes, which
	 * keeps things pretty simple.
	 */
	size = resource_size(res);
	mask = (roundup_pow_of_two(size) / SZ_128) - 1;
	rcar_pci_write_reg(pcie, mask << 7, PCIEPAMR(win));

	if (res->flags & IORESOURCE_IO)
		res_start = pci_pio_to_address(res->start);
	else
		res_start = res->start;

	rcar_pci_write_reg(pcie, upper_32_bits(res_start), PCIEPAUR(win));
	rcar_pci_write_reg(pcie, lower_32_bits(res_start) & ~0x7F,
			   PCIEPALR(win));

	/* First resource is for IO */
	mask = PAR_ENABLE;
	if (res->flags & IORESOURCE_IO)
		mask |= IO_SPACE;

	rcar_pci_write_reg(pcie, mask, PCIEPTCTLR(win));
}

static int rcar_pcie_setup(struct list_head *resource, struct rcar_pcie *pci)
{
	struct resource_entry *win;
	int i = 0;

	/* Setup PCI resources */
	resource_list_for_each_entry(win, &pci->resources) {
		struct resource *res = win->res;

		if (!res->flags)
			continue;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
		case IORESOURCE_MEM:
			rcar_pcie_setup_window(i, pci, res);
			i++;
			break;
		case IORESOURCE_BUS:
			pci->root_bus_nr = res->start;
			break;
		default:
			continue;
		}

		pci_add_resource(resource, res);
	}

	return 1;
}

static void rcar_pcie_force_speedup(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	unsigned int timeout = 1000;
	u32 macsr;

	if ((rcar_pci_read_reg(pcie, MACS2R) & LINK_SPEED) != LINK_SPEED_5_0GTS)
		return;

	if (rcar_pci_read_reg(pcie, MACCTLR) & SPEED_CHANGE) {
		dev_err(dev, "Speed change already in progress\n");
		return;
	}

	macsr = rcar_pci_read_reg(pcie, MACSR);
	if ((macsr & LINK_SPEED) == LINK_SPEED_5_0GTS)
		goto done;

	/* Set target link speed to 5.0 GT/s */
	rcar_rmw32(pcie, EXPCAP(12), PCI_EXP_LNKSTA_CLS,
		   PCI_EXP_LNKSTA_CLS_5_0GB);

	/* Set speed change reason as intentional factor */
	rcar_rmw32(pcie, MACCGSPSETR, SPCNGRSN, 0);

	/* Clear SPCHGFIN, SPCHGSUC, and SPCHGFAIL */
	if (macsr & (SPCHGFIN | SPCHGSUC | SPCHGFAIL))
		rcar_pci_write_reg(pcie, macsr, MACSR);

	/* Start link speed change */
	rcar_rmw32(pcie, MACCTLR, SPEED_CHANGE, SPEED_CHANGE);

	while (timeout--) {
		macsr = rcar_pci_read_reg(pcie, MACSR);
		if (macsr & SPCHGFIN) {
			/* Clear the interrupt bits */
			rcar_pci_write_reg(pcie, macsr, MACSR);

			if (macsr & SPCHGFAIL)
				dev_err(dev, "Speed change failed\n");

			goto done;
		}

		msleep(1);
	};

	dev_err(dev, "Speed change timed out\n");

done:
	dev_info(dev, "Current link speed is %s GT/s\n",
		 (macsr & LINK_SPEED) == LINK_SPEED_5_0GTS ? "5" : "2.5");
}

static int rcar_pcie_enable(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct pci_bus *bus, *child;
	LIST_HEAD(res);

	/* Try setting 5 GT/s link speed */
	rcar_pcie_force_speedup(pcie);

	rcar_pcie_setup(&res, pcie);

	pci_add_flags(PCI_REASSIGN_ALL_RSRC | PCI_REASSIGN_ALL_BUS);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		bus = pci_scan_root_bus_msi(dev, pcie->root_bus_nr,
				&rcar_pcie_ops, pcie, &res, &pcie->msi.chip);
	else
		bus = pci_scan_root_bus(dev, pcie->root_bus_nr,
				&rcar_pcie_ops, pcie, &res);

	if (!bus) {
		dev_err(dev, "Scanning rootbus failed");
		return -ENODEV;
	}

	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);

	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);

	return 0;
}

static int phy_wait_for_ack(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	unsigned int timeout = 100;

	while (timeout--) {
		if (rcar_pci_read_reg(pcie, H1_PCIEPHYADRR) & PHY_ACK)
			return 0;

		udelay(100);
	}

	dev_err(dev, "Access to PCIe phy timed out\n");

	return -ETIMEDOUT;
}

static void phy_write_reg(struct rcar_pcie *pcie,
				 unsigned int rate, unsigned int addr,
				 unsigned int lane, unsigned int data)
{
	unsigned long phyaddr;

	phyaddr = WRITE_CMD |
		((rate & 1) << RATE_POS) |
		((lane & 0xf) << LANE_POS) |
		((addr & 0xff) << ADR_POS);

	/* Set write data */
	rcar_pci_write_reg(pcie, data, H1_PCIEPHYDOUTR);
	rcar_pci_write_reg(pcie, phyaddr, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);

	/* Clear command */
	rcar_pci_write_reg(pcie, 0, H1_PCIEPHYDOUTR);
	rcar_pci_write_reg(pcie, 0, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);
}

static int rcar_pcie_wait_for_dl(struct rcar_pcie *pcie)
{
	unsigned int timeout = 10;

	while (timeout--) {
		if ((rcar_pci_read_reg(pcie, PCIETSTR) & DATA_LINK_ACTIVE))
			return 0;

		msleep(5);
	}

	return -ETIMEDOUT;
}

static int rcar_pcie_hw_init(struct rcar_pcie *pcie)
{
	int err;

	/* Begin initialization */
	rcar_pci_write_reg(pcie, 0, PCIETCTLR);

	/* Set mode */
	rcar_pci_write_reg(pcie, 1, PCIEMSR);

	/*
	 * Initial header for port config space is type 1, set the device
	 * class to match. Hardware takes care of propagating the IDSETR
	 * settings, so there is no need to bother with a quirk.
	 */
	rcar_pci_write_reg(pcie, PCI_CLASS_BRIDGE_PCI << 16, IDSETR1);

	/*
	 * Setup Secondary Bus Number & Subordinate Bus Number, even though
	 * they aren't used, to avoid bridge being detected as broken.
	 */
	rcar_rmw32(pcie, RCONF(PCI_SECONDARY_BUS), 0xff, 1);
	rcar_rmw32(pcie, RCONF(PCI_SUBORDINATE_BUS), 0xff, 1);

	/* Initialize default capabilities. */
	rcar_rmw32(pcie, REXPCAP(0), 0xff, PCI_CAP_ID_EXP);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_FLAGS),
		PCI_EXP_FLAGS_TYPE, PCI_EXP_TYPE_ROOT_PORT << 4);
	rcar_rmw32(pcie, RCONF(PCI_HEADER_TYPE), 0x7f,
		PCI_HEADER_TYPE_BRIDGE);

	/* Enable data link layer active state reporting */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_LNKCAP), PCI_EXP_LNKCAP_DLLLARC,
		PCI_EXP_LNKCAP_DLLLARC);

	/* Write out the physical slot number = 0 */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_SLTCAP), PCI_EXP_SLTCAP_PSN, 0);

	/* Set the completion timer timeout to the maximum 50ms. */
	rcar_rmw32(pcie, TLCTLR + 1, 0x3f, 50);

	/* Terminate list of capabilities (Next Capability Offset=0) */
	rcar_rmw32(pcie, RVCCAP(0), 0xfff00000, 0);

	/* Enable MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI))
		rcar_pci_write_reg(pcie, 0x801f0000, PCIEMSITXR);

	/* Finish initialization - establish a PCI Express link */
	rcar_pci_write_reg(pcie, CFINIT, PCIETCTLR);

	/* This will timeout if we don't have a link. */
	err = rcar_pcie_wait_for_dl(pcie);
	if (err)
		return err;

	/* Enable INTx interrupts */
	rcar_rmw32(pcie, PCIEINTXR, 0, 0xF << 8);

	wmb();

	return 0;
}

static int rcar_pcie_hw_init_h1(struct rcar_pcie *pcie)
{
	unsigned int timeout = 10;

	/* Initialize the phy */
	phy_write_reg(pcie, 0, 0x42, 0x1, 0x0EC34191);
	phy_write_reg(pcie, 1, 0x42, 0x1, 0x0EC34180);
	phy_write_reg(pcie, 0, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 1, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 0, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x4C, 0x1, 0x786174A0);
	phy_write_reg(pcie, 1, 0x4D, 0x1, 0x048000BB);
	phy_write_reg(pcie, 0, 0x51, 0x1, 0x079EC062);
	phy_write_reg(pcie, 0, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x56, 0x1, 0x00003806);

	phy_write_reg(pcie, 0, 0x60, 0x1, 0x004B03A5);
	phy_write_reg(pcie, 0, 0x64, 0x1, 0x3F0F1F0F);
	phy_write_reg(pcie, 0, 0x66, 0x1, 0x00008000);

	while (timeout--) {
		if (rcar_pci_read_reg(pcie, H1_PCIEPHYSR))
			return rcar_pcie_hw_init(pcie);

		msleep(5);
	}

	return -ETIMEDOUT;
}

static int rcar_pcie_hw_init_gen2(struct rcar_pcie *pcie)
{
	/*
	 * These settings come from the R-Car Series, 2nd Generation User's
	 * Manual, section 50.3.1 (2) Initialization of the physical layer.
	 */
	rcar_pci_write_reg(pcie, 0x000f0030, GEN2_PCIEPHYADDR);
	rcar_pci_write_reg(pcie, 0x00381203, GEN2_PCIEPHYDATA);
	rcar_pci_write_reg(pcie, 0x00000001, GEN2_PCIEPHYCTRL);
	rcar_pci_write_reg(pcie, 0x00000006, GEN2_PCIEPHYCTRL);

	rcar_pci_write_reg(pcie, 0x000f0054, GEN2_PCIEPHYADDR);
	/* The following value is for DC connection, no termination resistor */
	rcar_pci_write_reg(pcie, 0x13802007, GEN2_PCIEPHYDATA);
	rcar_pci_write_reg(pcie, 0x00000001, GEN2_PCIEPHYCTRL);
	rcar_pci_write_reg(pcie, 0x00000006, GEN2_PCIEPHYCTRL);

	return rcar_pcie_hw_init(pcie);
}

static int rcar_msi_alloc(struct rcar_msi *chip)
{
	int msi;

	mutex_lock(&chip->lock);

	msi = find_first_zero_bit(chip->used, INT_PCI_MSI_NR);
	if (msi < INT_PCI_MSI_NR)
		set_bit(msi, chip->used);
	else
		msi = -ENOSPC;

	mutex_unlock(&chip->lock);

	return msi;
}

static int rcar_msi_alloc_region(struct rcar_msi *chip, int no_irqs)
{
	int msi;

	mutex_lock(&chip->lock);
	msi = bitmap_find_free_region(chip->used, INT_PCI_MSI_NR,
				      order_base_2(no_irqs));
	mutex_unlock(&chip->lock);

	return msi;
}

static void rcar_msi_free(struct rcar_msi *chip, unsigned long irq)
{
	mutex_lock(&chip->lock);
	clear_bit(irq, chip->used);
	mutex_unlock(&chip->lock);
}

static irqreturn_t rcar_pcie_msi_irq(int irq, void *data)
{
	struct rcar_pcie *pcie = data;
	struct rcar_msi *msi = &pcie->msi;
	struct device *dev = pcie->dev;
	unsigned long reg;

	reg = rcar_pci_read_reg(pcie, PCIEMSIFR);

	/* MSI & INTx share an interrupt - we only handle MSI here */
	if (!reg)
		return IRQ_NONE;

	while (reg) {
		unsigned int index = find_first_bit(&reg, 32);
		unsigned int irq;

		/* clear the interrupt */
		rcar_pci_write_reg(pcie, 1 << index, PCIEMSIFR);

		irq = irq_find_mapping(msi->domain, index);
		if (irq) {
			if (test_bit(index, msi->used))
				generic_handle_irq(irq);
			else
				dev_info(dev, "unhandled MSI\n");
		} else {
			/* Unknown MSI, just clear it */
			dev_dbg(dev, "unexpected MSI\n");
		}

		/* see if there's any more pending in this vector */
		reg = rcar_pci_read_reg(pcie, PCIEMSIFR);
	}

	return IRQ_HANDLED;
}

static int rcar_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			      struct msi_desc *desc)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct rcar_pcie *pcie = container_of(chip, struct rcar_pcie, msi.chip);
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;

	hwirq = rcar_msi_alloc(msi);
	if (hwirq < 0)
		return hwirq;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq) {
		rcar_msi_free(msi, hwirq);
		return -EINVAL;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_lo = rcar_pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg.address_hi = rcar_pci_read_reg(pcie, PCIEMSIAUR);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static int rcar_msi_setup_irqs(struct msi_controller *chip,
			       struct pci_dev *pdev, int nvec, int type)
{
	struct rcar_pcie *pcie = container_of(chip, struct rcar_pcie, msi.chip);
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct msi_desc *desc;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;
	int i;

	/* MSI-X interrupts are not supported */
	if (type == PCI_CAP_ID_MSIX)
		return -EINVAL;

	WARN_ON(!list_is_singular(&pdev->dev.msi_list));
	desc = list_entry(pdev->dev.msi_list.next, struct msi_desc, list);

	hwirq = rcar_msi_alloc_region(msi, nvec);
	if (hwirq < 0)
		return -ENOSPC;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq)
		return -ENOSPC;

	for (i = 0; i < nvec; i++) {
		/*
		 * irq_create_mapping() called from rcar_pcie_probe() pre-
		 * allocates descs,  so there is no need to allocate descs here.
		 * We can therefore assume that if irq_find_mapping() above
		 * returns non-zero, then the descs are also successfully
		 * allocated.
		 */
		if (irq_set_msi_desc_off(irq, i, desc)) {
			/* TODO: clear */
			return -EINVAL;
		}
	}

	desc->nvec_used = nvec;
	desc->msi_attrib.multiple = order_base_2(nvec);

	msg.address_lo = rcar_pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg.address_hi = rcar_pci_read_reg(pcie, PCIEMSIAUR);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static void rcar_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct irq_data *d = irq_get_irq_data(irq);

	rcar_msi_free(msi, d->hwirq);
}

static struct irq_chip rcar_msi_irq_chip = {
	.name = "R-Car PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static int rcar_msi_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &rcar_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = rcar_msi_map,
};

static int rcar_pcie_enable_msi(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct rcar_msi *msi = &pcie->msi;
	unsigned long base;
	int err, i;

	mutex_init(&msi->lock);

	msi->chip.dev = dev;
	msi->chip.setup_irq = rcar_msi_setup_irq;
	msi->chip.setup_irqs = rcar_msi_setup_irqs;
	msi->chip.teardown_irq = rcar_msi_teardown_irq;

	msi->domain = irq_domain_add_linear(dev->of_node, INT_PCI_MSI_NR,
					    &msi_domain_ops, &msi->chip);
	if (!msi->domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < INT_PCI_MSI_NR; i++)
		irq_create_mapping(msi->domain, i);

	/* Two irqs are for MSI, but they are also used for non-MSI irqs */
	err = devm_request_irq(dev, msi->irq1, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_irq_chip.name, pcie);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	err = devm_request_irq(dev, msi->irq2, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_irq_chip.name, pcie);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	/* setup MSI data target */
	msi->pages = __get_free_pages(GFP_KERNEL, 0);
	base = virt_to_phys((void *)msi->pages);

	rcar_pci_write_reg(pcie, base | MSIFE, PCIEMSIALR);
	rcar_pci_write_reg(pcie, 0, PCIEMSIAUR);

	/* enable all MSI interrupts */
	rcar_pci_write_reg(pcie, 0xffffffff, PCIEMSIIER);

	return 0;

err:
	irq_domain_remove(msi->domain);
	return err;
}

static int rcar_pcie_get_resources(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct resource res;
	int err, i;

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	pcie->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	pcie->clk = devm_clk_get(dev, "pcie");
	if (IS_ERR(pcie->clk)) {
		dev_err(dev, "cannot get platform clock\n");
		return PTR_ERR(pcie->clk);
	}
	err = clk_prepare_enable(pcie->clk);
	if (err)
		return err;

	pcie->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(pcie->bus_clk)) {
		dev_err(dev, "cannot get pcie bus clock\n");
		err = PTR_ERR(pcie->bus_clk);
		goto fail_clk;
	}
	err = clk_prepare_enable(pcie->bus_clk);
	if (err)
		goto fail_clk;

	i = irq_of_parse_and_map(dev->of_node, 0);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
		goto err_map_reg;
	}
	pcie->msi.irq1 = i;

	i = irq_of_parse_and_map(dev->of_node, 1);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
		goto err_map_reg;
	}
	pcie->msi.irq2 = i;

	return 0;

err_map_reg:
	clk_disable_unprepare(pcie->bus_clk);
fail_clk:
	clk_disable_unprepare(pcie->clk);

	return err;
}

static int rcar_pcie_inbound_ranges(struct rcar_pcie *pcie,
				    struct of_pci_range *range,
				    int *index)
{
	u64 restype = range->flags;
	u64 cpu_addr = range->cpu_addr;
	u64 cpu_end = range->cpu_addr + range->size;
	u64 pci_addr = range->pci_addr;
	u32 flags = LAM_64BIT | LAR_ENABLE;
	u64 mask;
	u64 size;
	int idx = *index;

	if (restype & IORESOURCE_PREFETCH)
		flags |= LAM_PREFETCH;

	/*
	 * If the size of the range is larger than the alignment of the start
	 * address, we have to use multiple entries to perform the mapping.
	 */
	if (cpu_addr > 0) {
		unsigned long nr_zeros = __ffs64(cpu_addr);
		u64 alignment = 1ULL << nr_zeros;

		size = min(range->size, alignment);
	} else {
		size = range->size;
	}
	/* Hardware supports max 4GiB inbound region */
	size = min(size, 1ULL << 32);

	mask = roundup_pow_of_two(size) - 1;
	mask &= ~0xf;

	while (cpu_addr < cpu_end) {
		/*
		 * Set up 64-bit inbound regions as the range parser doesn't
		 * distinguish between 32 and 64-bit types.
		 */
		rcar_pci_write_reg(pcie, lower_32_bits(pci_addr),
				   PCIEPRAR(idx));
		rcar_pci_write_reg(pcie, lower_32_bits(cpu_addr), PCIELAR(idx));
		rcar_pci_write_reg(pcie, lower_32_bits(mask) | flags,
				   PCIELAMR(idx));

		rcar_pci_write_reg(pcie, upper_32_bits(pci_addr),
				   PCIEPRAR(idx + 1));
		rcar_pci_write_reg(pcie, upper_32_bits(cpu_addr),
				   PCIELAR(idx + 1));
		rcar_pci_write_reg(pcie, 0, PCIELAMR(idx + 1));

		pci_addr += size;
		cpu_addr += size;
		idx += 2;

		if (idx > MAX_NR_INBOUND_MAPS) {
			dev_err(pcie->dev, "Failed to map inbound regions!\n");
			return -EINVAL;
		}
	}
	*index = idx;

	return 0;
}

static int pci_dma_range_parser_init(struct of_pci_range_parser *parser,
				     struct device_node *node)
{
	const int na = 3, ns = 2;
	int rlen;

	parser->node = node;
	parser->pna = of_n_addr_cells(node);
	parser->np = parser->pna + na + ns;

	parser->range = of_get_property(node, "dma-ranges", &rlen);
	if (!parser->range)
		return -ENOENT;

	parser->end = parser->range + rlen / sizeof(__be32);
	return 0;
}

static int rcar_pcie_parse_map_dma_ranges(struct rcar_pcie *pcie,
					  struct device_node *np)
{
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	int index = 0;
	int err;

	if (pci_dma_range_parser_init(&parser, np))
		return -EINVAL;

	/* Get the dma-ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		u64 end = range.cpu_addr + range.size - 1;

		dev_dbg(pcie->dev, "0x%08x 0x%016llx..0x%016llx -> 0x%016llx\n",
			range.flags, range.cpu_addr, end, range.pci_addr);

		err = rcar_pcie_inbound_ranges(pcie, &range, &index);
		if (err)
			return err;
	}

	return 0;
}

static const struct of_device_id rcar_pcie_of_match[] = {
	{ .compatible = "renesas,pcie-r8a7779", .data = rcar_pcie_hw_init_h1 },
	{ .compatible = "renesas,pcie-rcar-gen2",
	  .data = rcar_pcie_hw_init_gen2 },
	{ .compatible = "renesas,pcie-r8a7790",
	  .data = rcar_pcie_hw_init_gen2 },
	{ .compatible = "renesas,pcie-r8a7791",
	  .data = rcar_pcie_hw_init_gen2 },
	{ .compatible = "renesas,pcie-r8a7795", .data = rcar_pcie_hw_init },
	{},
};

static int rcar_pcie_parse_request_of_pci_ranges(struct rcar_pcie *pci)
{
	int err;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	resource_size_t iobase;
	struct resource_entry *win, *tmp;

	err = of_pci_get_host_bridge_resources(np, 0, 0xff, &pci->resources,
					       &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, &pci->resources);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry_safe(win, tmp, &pci->resources) {
		struct resource *res = win->res;

		if (resource_type(res) == IORESOURCE_IO) {
			err = pci_remap_iospace(res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);

				resource_list_destroy_entry(win);
			}
		}
	}

	return 0;

out_release_res:
	pci_free_resource_list(&pci->resources);
	return err;
}

static int rcar_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_pcie *pcie;
	unsigned int data;
	const struct of_device_id *of_id;
	int err;
	int (*hw_init_fn)(struct rcar_pcie *);

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = dev;

	INIT_LIST_HEAD(&pcie->resources);

	rcar_pcie_parse_request_of_pci_ranges(pcie);

	err = rcar_pcie_get_resources(pcie);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		return err;
	}

	err = rcar_pcie_parse_map_dma_ranges(pcie, dev->of_node);
	if (err)
		return err;

	of_id = of_match_device(rcar_pcie_of_match, dev);
	if (!of_id || !of_id->data)
		return -EINVAL;
	hw_init_fn = of_id->data;

	pm_runtime_enable(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_pm_disable;
	}

	/* Failure to get a link might just be that no cards are inserted */
	err = hw_init_fn(pcie);
	if (err) {
		dev_info(dev, "PCIe link down\n");
		err = 0;
		goto err_pm_put;
	}

	data = rcar_pci_read_reg(pcie, MACSR);
	dev_info(dev, "PCIe x%d: link up\n", (data >> 20) & 0x3f);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = rcar_pcie_enable_msi(pcie);
		if (err < 0) {
			dev_err(dev,
				"failed to enable MSI support: %d\n",
				err);
			goto err_pm_put;
		}
	}

	err = rcar_pcie_enable(pcie);
	if (err)
		goto err_pm_put;

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);
	return err;
}

static struct platform_driver rcar_pcie_driver = {
	.driver = {
		.name = "rcar-pcie",
		.of_match_table = rcar_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rcar_pcie_probe,
};
builtin_platform_driver(rcar_pcie_driver);
