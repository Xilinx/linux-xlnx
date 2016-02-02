/*
 * PCIe host controller driver for NWL PCIe Bridge
 * Based on pci-xilinx.c, pci-tegra.c
 *
 * (C) Copyright 2014 - 2015, Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

/* Bridge core config registers */
#define BRCFG_PCIE_RX0			0x00000000
#define BRCFG_PCIE_RX1			0x00000004
#define BRCFG_AXI_MASTER		0x00000008
#define BRCFG_PCIE_TX			0x0000000C
#define BRCFG_INTERRUPT			0x00000010
#define BRCFG_RAM_DISABLE0		0x00000014
#define BRCFG_RAM_DISABLE1		0x00000018
#define BRCFG_PCIE_RELAXED_ORDER	0x0000001C
#define BRCFG_PCIE_RX_MSG_FILTER	0x00000020

/* Attribute registers */
#define NWL_ATTRIB_100			0x00000190

/* Egress - Bridge translation registers */
#define E_BREG_CAPABILITIES		0x00000200
#define E_BREG_STATUS			0x00000204
#define E_BREG_CONTROL			0x00000208
#define E_BREG_BASE_LO			0x00000210
#define E_BREG_BASE_HI			0x00000214
#define E_ECAM_CAPABILITIES		0x00000220
#define E_ECAM_STATUS			0x00000224
#define E_ECAM_CONTROL			0x00000228
#define E_ECAM_BASE_LO			0x00000230
#define E_ECAM_BASE_HI			0x00000234

/* Ingress - address translations */
#define I_MSII_CAPABILITIES		0x00000300
#define I_MSII_CONTROL			0x00000308
#define I_MSII_BASE_LO			0x00000310
#define I_MSII_BASE_HI			0x00000314

#define I_ISUB_CONTROL			0x000003E8
#define SET_ISUB_CONTROL		BIT(0)
/* Rxed msg fifo  - Interrupt status registers */
#define MSGF_MISC_STATUS		0x00000400
#define MSGF_MISC_MASK			0x00000404
#define MSGF_LEG_STATUS			0x00000420
#define MSGF_LEG_MASK			0x00000424
#define MSGF_MSI_STATUS_LO		0x00000440
#define MSGF_MSI_STATUS_HI		0x00000444
#define MSGF_MSI_MASK_LO		0x00000448
#define MSGF_MSI_MASK_HI		0x0000044C
#define MSGF_RX_FIFO_POP		0x00000484
#define MSGF_RX_FIFO_TYPE		0x00000488
#define MSGF_RX_FIFO_ADDRLO		0x00000490
#define MSGF_RX_FIFO_ADDRHI		0x00000494
#define MSGF_RX_FIFO_DATA		0x00000498

/* Msg filter mask bits */
#define CFG_ENABLE_PM_MSG_FWD		BIT(1)
#define CFG_ENABLE_INT_MSG_FWD		BIT(2)
#define CFG_ENABLE_ERR_MSG_FWD		BIT(3)
#define CFG_ENABLE_SLT_MSG_FWD		BIT(5)
#define CFG_ENABLE_VEN_MSG_FWD		BIT(7)
#define CFG_ENABLE_OTH_MSG_FWD		BIT(13)
#define CFG_ENABLE_VEN_MSG_EN		BIT(14)
#define CFG_ENABLE_VEN_MSG_VEN_INV	BIT(15)
#define CFG_ENABLE_VEN_MSG_VEN_ID	GENMASK(31, 16)
#define CFG_ENABLE_MSG_FILTER_MASK	(CFG_ENABLE_PM_MSG_FWD | \
					CFG_ENABLE_INT_MSG_FWD | \
					CFG_ENABLE_ERR_MSG_FWD | \
					CFG_ENABLE_SLT_MSG_FWD | \
					CFG_ENABLE_VEN_MSG_FWD | \
					CFG_ENABLE_OTH_MSG_FWD | \
					CFG_ENABLE_VEN_MSG_EN | \
					CFG_ENABLE_VEN_MSG_VEN_INV | \
					CFG_ENABLE_VEN_MSG_VEN_ID)

/* Misc interrupt status mask bits */
#define MSGF_MISC_SR_RXMSG_AVAIL	BIT(0)
#define MSGF_MISC_SR_RXMSG_OVER		BIT(1)
#define MSGF_MISC_SR_SLAVE_ERR		BIT(4)
#define MSGF_MISC_SR_MASTER_ERR		BIT(5)
#define MSGF_MISC_SR_I_ADDR_ERR		BIT(6)
#define MSGF_MISC_SR_E_ADDR_ERR		BIT(7)

#define MSGF_MISC_SR_PCIE_CORE		GENMASK(18, 16)
#define MSGF_MISC_SR_PCIE_CORE_ERR	GENMASK(31, 20)

#define MSGF_MISC_SR_MASKALL		(MSGF_MISC_SR_RXMSG_AVAIL | \
					MSGF_MISC_SR_RXMSG_OVER | \
					MSGF_MISC_SR_SLAVE_ERR | \
					MSGF_MISC_SR_MASTER_ERR | \
					MSGF_MISC_SR_I_ADDR_ERR | \
					MSGF_MISC_SR_E_ADDR_ERR | \
					MSGF_MISC_SR_PCIE_CORE | \
					MSGF_MISC_SR_PCIE_CORE_ERR)

/* Message rx fifo type mask bits */
#define MSGF_RX_FIFO_TYPE_MSI	(1)
#define MSGF_RX_FIFO_TYPE_TYPE	GENMASK(1, 0)

/* Legacy interrupt status mask bits */
#define MSGF_LEG_SR_INTA		BIT(0)
#define MSGF_LEG_SR_INTB		BIT(1)
#define MSGF_LEG_SR_INTC		BIT(2)
#define MSGF_LEG_SR_INTD		BIT(3)
#define MSGF_LEG_SR_MASKALL		(MSGF_LEG_SR_INTA | MSGF_LEG_SR_INTB | \
					MSGF_LEG_SR_INTC | MSGF_LEG_SR_INTD)

/* MSI interrupt status mask bits */
#define MSGF_MSI_SR_LO_MASK		BIT(0)
#define MSGF_MSI_SR_HI_MASK		BIT(0)

#define MSII_PRESENT			BIT(0)
#define MSII_ENABLE			BIT(0)
#define MSII_STATUS_ENABLE		BIT(15)

/* Bridge config interrupt mask */
#define BRCFG_INTERRUPT_MASK		BIT(0)
#define BREG_PRESENT			BIT(0)
#define BREG_ENABLE			BIT(0)
#define BREG_ENABLE_FORCE		BIT(1)

/* E_ECAM status mask bits */
#define E_ECAM_PRESENT			BIT(0)
#define E_ECAM_SR_WR_PEND		BIT(16)
#define E_ECAM_SR_RD_PEND		BIT(0)
#define E_ECAM_SR_MASKALL		(E_ECAM_SR_WR_PEND | E_ECAM_SR_RD_PEND)
#define E_ECAM_CR_ENABLE		BIT(0)
#define E_ECAM_SIZE_LOC			GENMASK(20, 16)
#define E_ECAM_SIZE_SHIFT		16
#define ECAM_BUS_LOC_SHIFT		20
#define ECAM_DEV_LOC_SHIFT		12
#define NWL_ECAM_VALUE_DEFAULT		12
#define NWL_ECAM_SIZE_MIN		4096

#define ATTR_UPSTREAM_FACING		BIT(6)
#define CFG_DMA_REG_BAR			GENMASK(2, 0)

/* msgf_rx_fifo_pop bits */
#define MSGF_RX_FIFO_POP_POP	BIT(0)

#define INT_PCI_MSI_NR			(2 * 32)

/* Readin the PS_LINKUP */
#define PS_LINKUP_OFFSET			0x00000238
#define PCIE_PHY_LINKUP_BIT			BIT(0)
#define PHY_RDY_LINKUP_BIT			BIT(1)
#define PCIE_USER_LINKUP			0
#define PHY_RDY_LINKUP				1
#define LINKUP_ITER_CHECK			5

/* PCIE Message Request */
#define TX_PCIE_MSG				0x00000620
#define TX_PCIE_MSG_CNTL			0x00000004
#define TX_PCIE_MSG_SPEC_LO			0x00000008
#define TX_PCIE_MSG_SPEC_HI			0x0000000C
#define TX_PCIE_MSG_DATA			0x00000010

#define MSG_BUSY_BIT				BIT(8)
#define MSG_EXECUTE_BIT				BIT(0)
#define MSG_DONE_BIT				BIT(16)
#define MSG_DONE_STATUS_BIT			(BIT(25) | BIT(24))
#define RANDOM_DIGIT				0x11223344
#define PATTRN_SSLP_TLP				0x01005074

/**
 * struct nwl_msi - MSI information
 *
 * @chip: MSI controller
 * @used: Declare Bitmap for MSI
 * @domain: IRQ domain pointer
 * @pages: MSI pages
 * @lock: mutex lock
 * @irq_msi0: msi0 interrupt number
 * @irq_msi1: msi1 interrupt number
 */
struct nwl_msi {
	struct msi_controller chip;
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	unsigned long pages;
	struct mutex lock;
	int irq_msi0;
	int irq_msi1;
};

/**
 * struct nwl_pcie - PCIe port information
 *
 * @dev: Device pointer
 * @breg_base: IO Mapped Bridge Register Base
 * @pcireg_base: IO Mapped PCIe controller attributes
 * @ecam_base: IO Mapped configuration space
 * @phys_breg_base: Physical Bridge Register Base
 * @phys_pcie_reg_base: Physical PCIe Controller Attributes
 * @phys_ecam_base: Physical Configuration Base
 * @breg_size: Bridge Register space
 * @pcie_reg_size: PCIe controller attributes space
 * @ecam_size: PCIe Configuration space
 * @irq_intx: Legacy interrupt number
 * @irq_misc: Misc interrupt number
 * @ecam_value: ECAM value
 * @last_busno: Last Bus number configured
 * @link_up: Link status flag
 * @enable_msi_fifo: Enable MSI FIFO mode
 * @bus: PCI bus
 * @msi: MSI interrupt info
 */
struct nwl_pcie {
	struct device *dev;
	void __iomem *breg_base;
	void __iomem *pcireg_base;
	void __iomem *ecam_base;
	u32 phys_breg_base;
	u32 phys_pcie_reg_base;
	u32 phys_ecam_base;
	u32 breg_size;
	u32 pcie_reg_size;
	u32 ecam_size;
	int irq_intx;
	int irq_misc;
	u32 ecam_value;
	u8 last_busno;
	u8 root_busno;
	u8 link_up;
	bool enable_msi_fifo;
	struct pci_bus *bus;
	struct nwl_msi msi;
};

static inline struct nwl_msi *to_nwl_msi(struct msi_controller *chip)
{
	return container_of(chip, struct nwl_msi, chip);
}

static inline u32 nwl_bridge_readl(struct nwl_pcie *pcie, u32 off)
{
	return readl(pcie->breg_base + off);
}

static inline void nwl_bridge_writel(struct nwl_pcie *pcie, u32 val, u32 off)
{
	writel(val, pcie->breg_base + off);
}

static inline bool nwl_pcie_is_link_up(struct nwl_pcie *pcie, u32 check_bit)
{
	unsigned int status = -EINVAL;

	if (check_bit == PCIE_USER_LINKUP)
		status = (readl(pcie->pcireg_base + PS_LINKUP_OFFSET) &
			  PCIE_PHY_LINKUP_BIT) ? 1 : 0;
	else if (check_bit == PHY_RDY_LINKUP)
		status = (readl(pcie->pcireg_base + PS_LINKUP_OFFSET) &
			  PHY_RDY_LINKUP_BIT) ? 1 : 0;
	return status;
}

static bool nwl_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct nwl_pcie *pcie = bus->sysdata;

	/* Check link,before accessing downstream ports */
	if (bus->number != pcie->root_busno) {
		if (!nwl_pcie_is_link_up(pcie, PCIE_USER_LINKUP))
			return false;
	}

	/* Only one device down on each root port */
	if (bus->number == pcie->root_busno && devfn > 0)
		return false;

	/*
	 * Do not read more than one device on the bus directly attached
	 * to root port.
	 */
	if (bus->primary == pcie->root_busno && devfn > 0)
		return false;

	return true;
}

/**
 * nwl_pcie_get_config_base - Get configuration base
 *
 * @bus: Bus structure of current bus
 * @devfn: Device/function
 * @where: Offset from base
 *
 * Return: Base address of the configuration space needed to be
 *	   accessed.
 */
static void __iomem *nwl_pcie_get_config_base(struct pci_bus *bus,
						 unsigned int devfn,
						 int where)
{
	struct nwl_pcie *pcie = bus->sysdata;
	int relbus;

	if (!nwl_pcie_valid_device(bus, devfn))
		return NULL;

	relbus = (bus->number << ECAM_BUS_LOC_SHIFT) |
			(devfn << ECAM_DEV_LOC_SHIFT);

	return pcie->ecam_base + relbus + where;
}

/**
 * nwl_setup_sspl - Set Slot Power limit
 *
 * @pcie: PCIe port information
 */
static int nwl_setup_sspl(struct nwl_pcie *pcie)
{
	unsigned int status;
	int check = 0;

	do {
		status = nwl_bridge_readl(pcie, TX_PCIE_MSG) & MSG_BUSY_BIT;
		if (!status) {
			/*
			 * Generate the TLP message for a single EP
			 * [TODO] Add a multi-endpoint code
			 */
			nwl_bridge_writel(pcie, 0x0,
					  TX_PCIE_MSG + TX_PCIE_MSG_CNTL);
			nwl_bridge_writel(pcie, 0x0,
					  TX_PCIE_MSG + TX_PCIE_MSG_SPEC_LO);
			nwl_bridge_writel(pcie, 0x0,
					  TX_PCIE_MSG + TX_PCIE_MSG_SPEC_HI);
			nwl_bridge_writel(pcie, 0x0,
					  TX_PCIE_MSG + TX_PCIE_MSG_DATA);
			/* Pattern to generate SSLP TLP */
			nwl_bridge_writel(pcie, PATTRN_SSLP_TLP,
					  TX_PCIE_MSG + TX_PCIE_MSG_CNTL);
			nwl_bridge_writel(pcie, RANDOM_DIGIT,
					  TX_PCIE_MSG + TX_PCIE_MSG_DATA);
			nwl_bridge_writel(pcie, nwl_bridge_readl(pcie,
					  TX_PCIE_MSG) | 0x1, TX_PCIE_MSG);
			status = 0;
			do {
				status = nwl_bridge_readl(pcie, TX_PCIE_MSG) &
							  MSG_DONE_BIT;
				if (!status && (check < 1)) {
					mdelay(1);
					check++;
				} else {
					return false;
				}

			} while (!status);
			status = nwl_bridge_readl(pcie, TX_PCIE_MSG)
						  & MSG_DONE_STATUS_BIT;
		}
	} while (status);

	return true;
}

/**
 * nwl_nwl_readl_config - Read configuration space
 *
 * @bus: Bus structure of current bus
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be read
 *
 * Return: PCIBIOS_SUCCESSFUL on success
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure.
 */
static int nwl_nwl_readl_config(struct pci_bus *bus,
				unsigned int devfn,
				int where,
				int size,
				u32 *val)
{
	void __iomem *addr;

	addr = nwl_pcie_get_config_base(bus, devfn, where);
	if (!addr) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	switch (size) {
	case 1:
		*val = readb(addr);
		break;
	case 2:
		*val = readw(addr);
		break;
	default:
		*val = readl(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

/**
 * nwl_nwl_writel_config - Write configuration space
 *
 * @bus: Bus structure of current bus
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be written to device
 *
 * Return: PCIBIOS_SUCCESSFUL on success,
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure.
 */
static int nwl_nwl_writel_config(struct pci_bus *bus,
				unsigned int devfn,
				int where,
				int size,
				u32 val)
{
	void __iomem *addr;
	int err = 0;
	struct nwl_pcie *pcie = bus->sysdata;

	addr = nwl_pcie_get_config_base(bus, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
		break;
	}
	if (addr == (pcie->ecam_base + PCI_EXP_SLTCAP)) {
		err = nwl_setup_sspl(pcie);
		if (!err)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

/* PCIe operations */
static struct pci_ops nwl_pcie_ops = {
	.read  = nwl_nwl_readl_config,
	.write = nwl_nwl_writel_config,
};

static irqreturn_t nwl_pcie_misc_handler(int irq, void *data)
{
	struct nwl_pcie *pcie = (struct nwl_pcie *)data;
	u32 misc_stat;

	/* Checking for misc interrupts */
	misc_stat = nwl_bridge_readl(pcie, MSGF_MISC_STATUS) &
				     MSGF_MISC_SR_MASKALL;
	if (!misc_stat)
		return IRQ_NONE;

	if (misc_stat & MSGF_MISC_SR_RXMSG_OVER)
		dev_err(pcie->dev, "Received Message FIFO Overflow\n");

	if (misc_stat & MSGF_MISC_SR_SLAVE_ERR)
		dev_err(pcie->dev, "Slave error\n");

	if (misc_stat & MSGF_MISC_SR_MASTER_ERR)
		dev_err(pcie->dev, "Master error\n");

	if (misc_stat & MSGF_MISC_SR_I_ADDR_ERR)
		dev_err(pcie->dev,
			"In Misc Ingress address translation error\n");

	if (misc_stat & MSGF_MISC_SR_E_ADDR_ERR)
		dev_err(pcie->dev,
			"In Misc Egress address translation error\n");

	if (misc_stat & MSGF_MISC_SR_PCIE_CORE_ERR)
		dev_err(pcie->dev, "PCIe Core error\n");

	if (pcie->enable_msi_fifo) {
		if (misc_stat & MSGF_MISC_SR_RXMSG_AVAIL) {
			u32 msg_type = nwl_bridge_readl(pcie,
							MSGF_RX_FIFO_TYPE) &
							MSGF_RX_FIFO_TYPE_TYPE;

			if (msg_type == MSGF_RX_FIFO_TYPE_MSI) {
				u32 irq_msi;
				struct nwl_msi *msi = &pcie->msi;
				u32 msi_data = nwl_bridge_readl(pcie,
							MSGF_RX_FIFO_DATA);
				/* Let all ready be completed before write */
				rmb();
				/* POP the FIFO */
				nwl_bridge_writel(pcie, MSGF_RX_FIFO_POP_POP,
						  MSGF_RX_FIFO_POP);

				/* Handle the msi virtual interrupt */
				irq_msi = irq_find_mapping(msi->domain,
							   msi_data);

				if (irq_msi) {
					if (test_bit(msi_data, msi->used))
						generic_handle_irq(irq_msi);
					else
						dev_info(pcie->dev,
							 "unhandled MSI %d\n",
							 irq_msi);
				} else {
					dev_info(pcie->dev, "unexpected MSI\n");
				}
			}
		}
	}
	/* Clear misc interrupt status */
	nwl_bridge_writel(pcie, misc_stat, MSGF_MISC_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t nwl_pcie_leg_handler(int irq, void *data)
{
	struct nwl_pcie *pcie = (struct nwl_pcie *)data;
	u32 leg_stat;

	/* Checking for legacy interrupts */
	leg_stat = nwl_bridge_readl(pcie, MSGF_LEG_STATUS) &
				MSGF_LEG_SR_MASKALL;
	if (!leg_stat)
		return IRQ_NONE;

	if (leg_stat & MSGF_LEG_SR_INTA)
		dev_dbg(pcie->dev, "legacy interruptA\n");

	if (leg_stat & MSGF_LEG_SR_INTB)
		dev_dbg(pcie->dev, "legacy interruptB\n");

	if (leg_stat & MSGF_LEG_SR_INTC)
		dev_dbg(pcie->dev, "legacy interruptC\n");

	if (leg_stat & MSGF_LEG_SR_INTD)
		dev_dbg(pcie->dev, "legacy interruptD\n");

	return IRQ_HANDLED;
}

static void __nwl_pcie_msi_handler(struct nwl_pcie *pcie,
					unsigned long reg, u32 val)
{
	struct nwl_msi *msi = &pcie->msi;
	unsigned int offset, index;
	int irq_msi;

	offset = find_first_bit(&reg, 32);
	index = offset;

	/* Clear the interrupt */
	nwl_bridge_writel(pcie, 1 << offset, val);

	/* Handle the msi virtual interrupt */
	irq_msi = irq_find_mapping(msi->domain, index);
	if (irq_msi) {
		if (test_bit(index, msi->used))
			generic_handle_irq(irq_msi);
		else
			dev_info(pcie->dev, "unhandled MSI\n");
	} else {
		/* that's weird who triggered this? just clear it */
		dev_info(pcie->dev, "unexpected MSI\n");
	}
}

static irqreturn_t nwl_pcie_msi_handler(int irq, void *data)
{
	struct nwl_pcie *pcie = data;
	unsigned long reg;
	int processed = 0;

	reg = nwl_bridge_readl(pcie, MSGF_MSI_STATUS_LO);
	if (reg) {
		__nwl_pcie_msi_handler(pcie, reg, MSGF_MSI_STATUS_LO);
		processed++;
	}

	reg = nwl_bridge_readl(pcie, MSGF_MSI_STATUS_HI);
	if (reg) {
		__nwl_pcie_msi_handler(pcie, reg, MSGF_MSI_STATUS_HI);
		processed++;
	}

	return processed > 0 ? IRQ_HANDLED : IRQ_NONE;
}

static int nwl_msi_alloc(struct nwl_msi *chip)
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

static void nwl_msi_free(struct nwl_msi *chip, unsigned long irq)
{
	struct device *dev = chip->chip.dev;

	mutex_lock(&chip->lock);

	if (!test_bit(irq, chip->used))
		dev_err(dev, "trying to free unused MSI#%lu\n", irq);
	else
		clear_bit(irq, chip->used);

	mutex_unlock(&chip->lock);
}

static int nwl_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			       struct msi_desc *desc)
{
	struct nwl_msi *msi = to_nwl_msi(chip);
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;

	if (desc->msi_attrib.is_msix) {
		/* currently we are not supporting MSIx */
		return -ENOSPC;
	}

	hwirq = nwl_msi_alloc(msi);
	if (hwirq < 0)
		return hwirq;

	irq = irq_create_mapping(msi->domain, hwirq);
	if (!irq)
		return -EINVAL;

	irq_set_msi_desc(irq, desc);

	msg.address_lo = virt_to_phys((void *)msi->pages);
	/* 32 bit address only */
	msg.address_hi = 0;
	msg.data = hwirq;

	write_msi_msg(irq, &msg);

	return 0;
}

static void nwl_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct nwl_msi *msi = to_nwl_msi(chip);
	struct irq_data *d = irq_get_irq_data(irq);

	nwl_msi_free(msi, d->hwirq);
}

static struct irq_chip nwl_msi_irq_chip = {
	.name = "nwl_pcie:msi",
	.irq_enable = unmask_msi_irq,
	.irq_disable = mask_msi_irq,
	.irq_mask = mask_msi_irq,
	.irq_unmask = unmask_msi_irq,
};

static int nwl_msi_map(struct irq_domain *domain, unsigned int irq,
			 irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &nwl_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = nwl_msi_map,
};

static int nwl_pcie_enable_msi(struct nwl_pcie *pcie, struct pci_bus *bus)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	struct nwl_msi *msi = &pcie->msi;
	unsigned long base;
	int ret;

	mutex_init(&msi->lock);

	/* Assign msi chip hooks */
	msi->chip.dev = pcie->dev;
	msi->chip.setup_irq = nwl_msi_setup_irq;
	msi->chip.teardown_irq = nwl_msi_teardown_irq;

	bus->msi = &msi->chip;
	/* Allocate linear irq domain */
	msi->domain = irq_domain_add_linear(pcie->dev->of_node, INT_PCI_MSI_NR,
					    &msi_domain_ops, &msi->chip);
	if (!msi->domain) {
		dev_err(&pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	/* Check for msii_present bit */
	ret = nwl_bridge_readl(pcie, I_MSII_CAPABILITIES) & MSII_PRESENT;
	if (!ret) {
		dev_err(pcie->dev, "MSI not present\n");
		ret = -EIO;
		goto err;
	}

	/* Enable MSII */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, I_MSII_CONTROL) |
			  MSII_ENABLE, I_MSII_CONTROL);
	if (!pcie->enable_msi_fifo)
		/* Enable MSII status */
		nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, I_MSII_CONTROL) |
				  MSII_STATUS_ENABLE, I_MSII_CONTROL);

	/* setup AFI/FPCI range */
	msi->pages = __get_free_pages(GFP_KERNEL, 0);
	base = virt_to_phys((void *)msi->pages);
	/* Write base to MSII_BASE_LO */
	nwl_bridge_writel(pcie, base, I_MSII_BASE_LO);

	/* Write 0x0 to MSII_BASE_HI */
	nwl_bridge_writel(pcie, 0x0, I_MSII_BASE_HI);

	/* Disable high range msi interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_MSI_SR_HI_MASK, MSGF_MSI_MASK_HI);

	/* Clear pending high range msi interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie,  MSGF_MSI_STATUS_HI) &
			  MSGF_MSI_SR_HI_MASK, MSGF_MSI_STATUS_HI);
	/* Get msi_1 IRQ number */
	msi->irq_msi1 = platform_get_irq_byname(pdev, "msi_1");
	if (msi->irq_msi1 < 0) {
		dev_err(&pdev->dev, "failed to get IRQ#%d\n", msi->irq_msi1);
		goto err;
	}
	/* Register msi handler */
	ret = devm_request_irq(pcie->dev, msi->irq_msi1, nwl_pcie_msi_handler,
			       0, nwl_msi_irq_chip.name, pcie);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%d\n",
					msi->irq_msi1);
		goto err;
	}

	/* Enable all high range msi interrupts */
	nwl_bridge_writel(pcie, MSGF_MSI_SR_HI_MASK, MSGF_MSI_MASK_HI);

	/* Disable low range msi interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_MSI_SR_LO_MASK, MSGF_MSI_MASK_LO);

	/* Clear pending low range msi interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, MSGF_MSI_STATUS_LO) &
			  MSGF_MSI_SR_LO_MASK, MSGF_MSI_STATUS_LO);
	/* Get msi_0 IRQ number */
	msi->irq_msi0 = platform_get_irq_byname(pdev, "msi_0");
	if (msi->irq_msi0 < 0) {
		dev_err(&pdev->dev, "failed to get IRQ#%d\n", msi->irq_msi0);
		goto err;
	}
	/* Register msi handler */
	ret = devm_request_irq(pcie->dev, msi->irq_msi0, nwl_pcie_msi_handler,
			       0, nwl_msi_irq_chip.name, pcie);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%d\n",
					msi->irq_msi0);
		goto err;
	}
	/* Enable all low range msi interrupts */
	nwl_bridge_writel(pcie, MSGF_MSI_SR_LO_MASK, MSGF_MSI_MASK_LO);

	return 0;
err:
	irq_domain_remove(msi->domain);
	return ret;
}

static int nwl_pcie_bridge_init(struct nwl_pcie *pcie)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	u32 breg_val, ecam_val, first_busno = 0;
	int err;
	int check_link_up = 0;

	/* Check for BREG present bit */
	breg_val = nwl_bridge_readl(pcie, E_BREG_CAPABILITIES) & BREG_PRESENT;
	if (!breg_val) {
		dev_err(pcie->dev, "BREG is not present\n");
		return breg_val;
	}
	/* Write bridge_off to breg base */
	nwl_bridge_writel(pcie, (u32)(pcie->phys_breg_base),
			  E_BREG_BASE_LO);

	/* Enable BREG */
	nwl_bridge_writel(pcie, ~BREG_ENABLE_FORCE & BREG_ENABLE,
			  E_BREG_CONTROL);

	/* Disable DMA channel registers */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, BRCFG_PCIE_RX0) |
			  CFG_DMA_REG_BAR, BRCFG_PCIE_RX0);

	/* Enable the bridge config interrupt */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, BRCFG_INTERRUPT) |
			  BRCFG_INTERRUPT_MASK, BRCFG_INTERRUPT);
	/* Enable Ingress subtractive decode translation */
	nwl_bridge_writel(pcie, SET_ISUB_CONTROL, I_ISUB_CONTROL);

	/* Enable msg filtering details */
	nwl_bridge_writel(pcie, CFG_ENABLE_MSG_FILTER_MASK,
			  BRCFG_PCIE_RX_MSG_FILTER);
	do {
		err = nwl_pcie_is_link_up(pcie, PHY_RDY_LINKUP);
		if (err != 1) {
			check_link_up++;
			if (check_link_up > LINKUP_ITER_CHECK)
				return -ENODEV;
		}
	} while (!err);

	/* Check for ECAM present bit */
	ecam_val = nwl_bridge_readl(pcie, E_ECAM_CAPABILITIES) & E_ECAM_PRESENT;
	if (!ecam_val) {
		dev_err(pcie->dev, "ECAM is not present\n");
		return ecam_val;
	}

	/* Enable ECAM */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, E_ECAM_CONTROL) |
			  E_ECAM_CR_ENABLE, E_ECAM_CONTROL);
	/* Write ecam_value on ecam_control */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, E_ECAM_CONTROL) |
			  (pcie->ecam_value << E_ECAM_SIZE_SHIFT),
			  E_ECAM_CONTROL);
	/* Write phy_reg_base to ecam base */
	nwl_bridge_writel(pcie, (u32)pcie->phys_ecam_base, E_ECAM_BASE_LO);

	/* Get bus range */
	ecam_val = nwl_bridge_readl(pcie, E_ECAM_CONTROL);
	pcie->last_busno = (ecam_val & E_ECAM_SIZE_LOC) >> E_ECAM_SIZE_SHIFT;
	/* Write primary, secondary and subordinate bus numbers */
	ecam_val = first_busno;
	ecam_val |= (first_busno + 1) << 8;
	ecam_val |= (pcie->last_busno << E_ECAM_SIZE_SHIFT);
	writel(ecam_val, (pcie->ecam_base + PCI_PRIMARY_BUS));

	/* Check if PCIe link is up? */
	pcie->link_up = nwl_pcie_is_link_up(pcie, PCIE_USER_LINKUP);
	if (!pcie->link_up)
		dev_info(pcie->dev, "Link is DOWN\n");
	else
		dev_info(pcie->dev, "Link is UP\n");

	/* Disable all misc interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_MISC_SR_MASKALL, MSGF_MISC_MASK);

	/* Clear pending misc interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, MSGF_MISC_STATUS) &
			  MSGF_MISC_SR_MASKALL, MSGF_MISC_STATUS);

	/* Get misc IRQ number */
	pcie->irq_misc = platform_get_irq_byname(pdev, "misc");
	if (pcie->irq_misc < 0) {
		dev_err(&pdev->dev, "failed to get misc IRQ#%d\n",
			pcie->irq_misc);
		return pcie->irq_misc;
	}
	/* Register misc handler */
	err = devm_request_irq(pcie->dev, pcie->irq_misc,
			       nwl_pcie_misc_handler, IRQF_SHARED,
			       "nwl_pcie:misc", pcie);
	if (err) {
		dev_err(pcie->dev, "fail to register misc IRQ#%d\n",
			pcie->irq_misc);
		return err;
	}
	/* Enable all misc interrupts */
	nwl_bridge_writel(pcie, MSGF_MISC_SR_MASKALL, MSGF_MISC_MASK);

	/* Disable all legacy interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_LEG_SR_MASKALL, MSGF_LEG_MASK);

	/* Clear pending legacy interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, MSGF_LEG_STATUS) &
			  MSGF_LEG_SR_MASKALL, MSGF_LEG_STATUS);
	/* Get intx IRQ number */
	pcie->irq_intx = platform_get_irq_byname(pdev, "intx");
	if (pcie->irq_intx < 0) {
		dev_err(&pdev->dev, "failed to get intx IRQ#%d\n",
			pcie->irq_intx);
		return pcie->irq_intx;
	}

	/* Register intx handler */
	err = devm_request_irq(pcie->dev, pcie->irq_intx,
			       nwl_pcie_leg_handler, IRQF_SHARED,
			       "nwl_pcie:intx", pcie);
	if (err) {
		dev_err(pcie->dev, "fail to register intx IRQ#%d\n",
			pcie->irq_intx);
		return err;
	}
	/* Enable all legacy interrupts */
	nwl_bridge_writel(pcie, MSGF_LEG_SR_MASKALL, MSGF_LEG_MASK);

	return 0;
}

static int nwl_pcie_parse_dt(struct nwl_pcie *pcie,
					struct platform_device *pdev)
{
	struct device_node *node = pcie->dev->of_node;
	struct resource *res;
	const char *type;

	/* Check for device type */
	type = of_get_property(node, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(pcie->dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "breg");
	pcie->breg_base = devm_ioremap_resource(pcie->dev, res);
	if (IS_ERR(pcie->breg_base))
		return PTR_ERR(pcie->breg_base);
	pcie->phys_breg_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcireg");
	pcie->pcireg_base = devm_ioremap_resource(pcie->dev, res);
	if (IS_ERR(pcie->pcireg_base))
		return PTR_ERR(pcie->pcireg_base);
	pcie->phys_pcie_reg_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	pcie->ecam_base = devm_ioremap_resource(pcie->dev, res);
	if (IS_ERR(pcie->ecam_base))
		return PTR_ERR(pcie->ecam_base);
	pcie->phys_ecam_base = res->start;

	pcie->enable_msi_fifo = of_property_read_bool(node, "xlnx,msi-fifo");

	return 0;
}

static const struct of_device_id nwl_pcie_of_match[] = {
	{ .compatible = "xlnx,nwl-pcie-2.11", },
	{}
};

static int nwl_pcie_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct nwl_pcie *pcie;
	struct pci_bus *bus;
	int err;

	resource_size_t iobase = 0;
	LIST_HEAD(res);

	/* Allocate private nwl_pcie struct */
	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	/* Set ecam value */
	pcie->ecam_value = NWL_ECAM_VALUE_DEFAULT;

	pcie->dev = &pdev->dev;

	/* Parse the device tree */
	err = nwl_pcie_parse_dt(pcie, pdev);
	if (err) {
		dev_err(pcie->dev, "Parsing DT failed\n");
		return err;
	}
	/* Bridge initialization */
	err = nwl_pcie_bridge_init(pcie);
	if (err) {
		dev_err(pcie->dev, "HW Initalization failed\n");
		return err;
	}

	err = of_pci_get_host_bridge_resources(node, 0, 0xff, &res, &iobase);
	if (err) {
		pr_err("Getting bridge resources failed\n");
		return err;
	}

	bus = pci_create_root_bus(&pdev->dev, pcie->root_busno,
				  &nwl_pcie_ops, pcie, &res);
	if (!bus)
		return -ENOMEM;

	/* Enable MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = nwl_pcie_enable_msi(pcie, bus);
		if (err < 0) {
			dev_err(&pdev->dev,
				"failed to enable MSI support: %d\n",
				err);
			return err;
		}
	}
	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
	pci_bus_add_devices(bus);
	platform_set_drvdata(pdev, pcie);

	return 0;
}

static int nwl_pcie_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver nwl_pcie_driver = {
	.driver = {
		.name = "nwl-pcie",
		.of_match_table = nwl_pcie_of_match,
	},
	.probe = nwl_pcie_probe,
	.remove = nwl_pcie_remove,
};
module_platform_driver(nwl_pcie_driver);

MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("NWL PCIe driver");
MODULE_LICENSE("GPL");
