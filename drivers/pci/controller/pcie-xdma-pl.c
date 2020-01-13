// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe host controller driver for Xilinx XDMA PCIe Bridge
 *
 * Copyright (C) 2017 Xilinx, Inc. All rights reserved.
 */

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
#include <linux/irqchip/chained_irq.h>

#include "../pci.h"

/* Register definitions */
#define XILINX_PCIE_REG_VSEC		0x0000012c
#define XILINX_PCIE_REG_BIR		0x00000130
#define XILINX_PCIE_REG_IDR		0x00000138
#define XILINX_PCIE_REG_IMR		0x0000013c
#define XILINX_PCIE_REG_PSCR		0x00000144
#define XILINX_PCIE_REG_RPSC		0x00000148
#define XILINX_PCIE_REG_MSIBASE1	0x0000014c
#define XILINX_PCIE_REG_MSIBASE2	0x00000150
#define XILINX_PCIE_REG_RPEFR		0x00000154
#define XILINX_PCIE_REG_RPIFR1		0x00000158
#define XILINX_PCIE_REG_RPIFR2		0x0000015c
#define XILINX_PCIE_REG_IDRN            0x00000160
#define XILINX_PCIE_REG_IDRN_MASK       0x00000164
#define XILINX_PCIE_REG_MSI_LOW		0x00000170
#define XILINX_PCIE_REG_MSI_HI		0x00000174
#define XILINX_PCIE_REG_MSI_LOW_MASK	0x00000178
#define XILINX_PCIE_REG_MSI_HI_MASK	0x0000017c

/* Interrupt registers definitions */
#define XILINX_PCIE_INTR_LINK_DOWN	BIT(0)
#define XILINX_PCIE_INTR_HOT_RESET	BIT(3)
#define XILINX_PCIE_INTR_CFG_TIMEOUT	BIT(8)
#define XILINX_PCIE_INTR_CORRECTABLE	BIT(9)
#define XILINX_PCIE_INTR_NONFATAL	BIT(10)
#define XILINX_PCIE_INTR_FATAL		BIT(11)
#define XILINX_PCIE_INTR_INTX		BIT(16)
#define XILINX_PCIE_INTR_MSI		BIT(17)
#define XILINX_PCIE_INTR_SLV_UNSUPP	BIT(20)
#define XILINX_PCIE_INTR_SLV_UNEXP	BIT(21)
#define XILINX_PCIE_INTR_SLV_COMPL	BIT(22)
#define XILINX_PCIE_INTR_SLV_ERRP	BIT(23)
#define XILINX_PCIE_INTR_SLV_CMPABT	BIT(24)
#define XILINX_PCIE_INTR_SLV_ILLBUR	BIT(25)
#define XILINX_PCIE_INTR_MST_DECERR	BIT(26)
#define XILINX_PCIE_INTR_MST_SLVERR	BIT(27)
#define XILINX_PCIE_IMR_ALL_MASK	0x0FF30FE9
#define XILINX_PCIE_IDR_ALL_MASK	0xFFFFFFFF
#define XILINX_PCIE_IDRN_MASK           GENMASK(19, 16)

/* Root Port Error FIFO Read Register definitions */
#define XILINX_PCIE_RPEFR_ERR_VALID	BIT(18)
#define XILINX_PCIE_RPEFR_REQ_ID	GENMASK(15, 0)
#define XILINX_PCIE_RPEFR_ALL_MASK	0xFFFFFFFF

/* Root Port Interrupt FIFO Read Register 1 definitions */
#define XILINX_PCIE_RPIFR1_INTR_VALID	BIT(31)
#define XILINX_PCIE_RPIFR1_MSI_INTR	BIT(30)
#define XILINX_PCIE_RPIFR1_INTR_MASK	GENMASK(28, 27)
#define XILINX_PCIE_RPIFR1_ALL_MASK	0xFFFFFFFF
#define XILINX_PCIE_RPIFR1_INTR_SHIFT	27
#define XILINX_PCIE_IDRN_SHIFT          16
#define XILINX_PCIE_VSEC_REV_MASK	GENMASK(19, 16)
#define XILINX_PCIE_VSEC_REV_SHIFT	16
#define XILINX_PCIE_FIFO_SHIFT		5

/* Bridge Info Register definitions */
#define XILINX_PCIE_BIR_ECAM_SZ_MASK	GENMASK(18, 16)
#define XILINX_PCIE_BIR_ECAM_SZ_SHIFT	16

/* Root Port Interrupt FIFO Read Register 2 definitions */
#define XILINX_PCIE_RPIFR2_MSG_DATA	GENMASK(15, 0)

/* Root Port Status/control Register definitions */
#define XILINX_PCIE_REG_RPSC_BEN	BIT(0)

/* Phy Status/Control Register definitions */
#define XILINX_PCIE_REG_PSCR_LNKUP	BIT(11)

/* ECAM definitions */
#define ECAM_BUS_NUM_SHIFT		20
#define ECAM_DEV_NUM_SHIFT		12

/* Number of MSI IRQs */
#define XILINX_NUM_MSI_IRQS		64
#define INTX_NUM                        4

#define DMA_BRIDGE_BASE_OFF		0xCD8

enum msi_mode {
	MSI_DECD_MODE = 1,
	MSI_FIFO_MODE,
};

enum xdma_config {
	XDMA_ZYNQMP_PL = 1,
	XDMA_VERSAL_PL,
};

struct xilinx_msi {
	struct irq_domain *msi_domain;
	unsigned long *bitmap;
	struct irq_domain *dev_domain;
	struct mutex lock;		/* protect bitmap variable */
	unsigned long msi_pages;
	int irq_msi0;
	int irq_msi1;
};

/**
 * struct xilinx_pcie_port - PCIe port information
 * @reg_base: IO Mapped Register Base
 * @irq: Interrupt number
 * @root_busno: Root Bus number
 * @dev: Device pointer
 * @leg_domain: Legacy IRQ domain pointer
 * @resources: Bus Resources
 * @msi: MSI information
 * @irq_misc: Legacy and error interrupt number
 * @msi_mode: MSI mode
 * @xdma_config: XDMA IP configuration
 */
struct xilinx_pcie_port {
	void __iomem *reg_base;
	u32 irq;
	u8 root_busno;
	struct device *dev;
	struct irq_domain *leg_domain;
	struct list_head resources;
	struct xilinx_msi msi;
	int irq_misc;
	u8 msi_mode;
	u8 xdma_config;
};

static inline u32 pcie_read(struct xilinx_pcie_port *port, u32 reg)
{
	if (port->xdma_config == XDMA_ZYNQMP_PL)
		return readl(port->reg_base + reg);
	else
		return readl(port->reg_base + reg + DMA_BRIDGE_BASE_OFF);
}

static inline void pcie_write(struct xilinx_pcie_port *port, u32 val, u32 reg)
{
	if (port->xdma_config == XDMA_ZYNQMP_PL)
		writel(val, port->reg_base + reg);
	else
		writel(val, port->reg_base + reg + DMA_BRIDGE_BASE_OFF);
}

static inline bool xilinx_pcie_link_is_up(struct xilinx_pcie_port *port)
{
	return (pcie_read(port, XILINX_PCIE_REG_PSCR) &
		XILINX_PCIE_REG_PSCR_LNKUP) ? 1 : 0;
}

/**
 * xilinx_pcie_clear_err_interrupts - Clear Error Interrupts
 * @port: PCIe port information
 */
static void xilinx_pcie_clear_err_interrupts(struct xilinx_pcie_port *port)
{
	unsigned long val = pcie_read(port, XILINX_PCIE_REG_RPEFR);

	if (val & XILINX_PCIE_RPEFR_ERR_VALID) {
		dev_dbg(port->dev, "Requester ID %lu\n",
			val & XILINX_PCIE_RPEFR_REQ_ID);
		pcie_write(port, XILINX_PCIE_RPEFR_ALL_MASK,
			   XILINX_PCIE_REG_RPEFR);
	}
}

/**
 * xilinx_pcie_valid_device - Check if a valid device is present on bus
 * @bus: PCI Bus structure
 * @devfn: device/function
 *
 * Return: 'true' on success and 'false' if invalid device is found
 */
static bool xilinx_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct xilinx_pcie_port *port = bus->sysdata;

	/* Check if link is up when trying to access downstream ports */
	if (bus->number != port->root_busno)
		if (!xilinx_pcie_link_is_up(port))
			return false;

	/* Only one device down on each root port */
	if (bus->number == port->root_busno && devfn > 0)
		return false;

	return true;
}

/**
 * xilinx_pcie_map_bus - Get configuration base
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 *
 * Return: Base address of the configuration space needed to be
 *	   accessed.
 */
static void __iomem *xilinx_pcie_map_bus(struct pci_bus *bus,
					 unsigned int devfn, int where)
{
	struct xilinx_pcie_port *port = bus->sysdata;
	int relbus;

	if (!xilinx_pcie_valid_device(bus, devfn))
		return NULL;

	relbus = (bus->number << ECAM_BUS_NUM_SHIFT) |
		 (devfn << ECAM_DEV_NUM_SHIFT);

	return port->reg_base + relbus + where;
}

/* PCIe operations */
static struct pci_ops xilinx_pcie_ops = {
	.map_bus = xilinx_pcie_map_bus,
	.read	= pci_generic_config_read,
	.write	= pci_generic_config_write,
};

/**
 * xilinx_pcie_enable_msi - Enable MSI support
 * @port: PCIe port information
 */
static void xilinx_pcie_enable_msi(struct xilinx_pcie_port *port)
{
	struct xilinx_msi *msi = &port->msi;
	phys_addr_t msg_addr;

	msi->msi_pages = __get_free_pages(GFP_KERNEL, 0);
	msg_addr = virt_to_phys((void *)msi->msi_pages);
	pcie_write(port, upper_32_bits(msg_addr), XILINX_PCIE_REG_MSIBASE1);
	pcie_write(port, lower_32_bits(msg_addr), XILINX_PCIE_REG_MSIBASE2);
}

/**
 * xilinx_pcie_intx_map - Set the handler for the INTx and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: HW interrupt number
 *
 * Return: Always returns 0.
 */
static int xilinx_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

/* INTx IRQ Domain operations */
static const struct irq_domain_ops intx_domain_ops = {
	.map = xilinx_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,
};

static void xilinx_pcie_handle_msi_irq(struct xilinx_pcie_port *port,
				       u32 status_reg)
{
	struct xilinx_msi *msi;
	unsigned long status;
	u32 bit;
	u32 virq;

	msi = &port->msi;

	while ((status = pcie_read(port, status_reg)) != 0) {
		for_each_set_bit(bit, &status, 32) {
			pcie_write(port, 1 << bit, status_reg);
			if (status_reg == XILINX_PCIE_REG_MSI_HI)
				bit = bit + 32;
			virq = irq_find_mapping(msi->dev_domain, bit);
			if (virq)
				generic_handle_irq(virq);
		}
	}
}

static void xilinx_pcie_msi_handler_high(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct xilinx_pcie_port *port = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);
	xilinx_pcie_handle_msi_irq(port, XILINX_PCIE_REG_MSI_HI);
	chained_irq_exit(chip, desc);
}

static void xilinx_pcie_msi_handler_low(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct xilinx_pcie_port *port = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);
	xilinx_pcie_handle_msi_irq(port, XILINX_PCIE_REG_MSI_LOW);
	chained_irq_exit(chip, desc);
}

/**
 * xilinx_pcie_intr_handler - Interrupt Service Handler
 * @irq: IRQ number
 * @data: PCIe port information
 *
 * Return: IRQ_HANDLED on success and IRQ_NONE on failure
 */
static irqreturn_t xilinx_pcie_intr_handler(int irq, void *data)
{
	struct xilinx_pcie_port *port = (struct xilinx_pcie_port *)data;
	u32 val, mask, status, msi_data, bit;
	unsigned long intr_val;

	/* Read interrupt decode and mask registers */
	val = pcie_read(port, XILINX_PCIE_REG_IDR);
	mask = pcie_read(port, XILINX_PCIE_REG_IMR);

	status = val & mask;
	if (!status)
		return IRQ_NONE;

	if (status & XILINX_PCIE_INTR_LINK_DOWN)
		dev_warn(port->dev, "Link Down\n");

	if (status & XILINX_PCIE_INTR_HOT_RESET)
		dev_info(port->dev, "Hot reset\n");

	if (status & XILINX_PCIE_INTR_CFG_TIMEOUT)
		dev_warn(port->dev, "ECAM access timeout\n");

	if (status & XILINX_PCIE_INTR_CORRECTABLE) {
		dev_warn(port->dev, "Correctable error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_NONFATAL) {
		dev_warn(port->dev, "Non fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_FATAL) {
		dev_warn(port->dev, "Fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_INTX) {
		/* Handle INTx Interrupt */
		intr_val = pcie_read(port, XILINX_PCIE_REG_IDRN);
		intr_val = intr_val >> XILINX_PCIE_IDRN_SHIFT;

		for_each_set_bit(bit, &intr_val, INTX_NUM)
			generic_handle_irq(irq_find_mapping(port->leg_domain,
							    bit));
	}

	if (port->msi_mode == MSI_FIFO_MODE &&
	    (status & XILINX_PCIE_INTR_MSI)) {
		/* MSI Interrupt */
		val = pcie_read(port, XILINX_PCIE_REG_RPIFR1);

		if (!(val & XILINX_PCIE_RPIFR1_INTR_VALID)) {
			dev_warn(port->dev, "RP Intr FIFO1 read error\n");
			goto error;
		}

		if (val & XILINX_PCIE_RPIFR1_MSI_INTR) {
			msi_data = pcie_read(port, XILINX_PCIE_REG_RPIFR2) &
				   XILINX_PCIE_RPIFR2_MSG_DATA;

			/* Clear interrupt FIFO register 1 */
			pcie_write(port, XILINX_PCIE_RPIFR1_ALL_MASK,
				   XILINX_PCIE_REG_RPIFR1);

			if (IS_ENABLED(CONFIG_PCI_MSI)) {
				/* Handle MSI Interrupt */
				val = irq_find_mapping(port->msi.dev_domain,
						       msi_data);
				if (val)
					generic_handle_irq(val);
			}
		}
	}

	if (status & XILINX_PCIE_INTR_SLV_UNSUPP)
		dev_warn(port->dev, "Slave unsupported request\n");

	if (status & XILINX_PCIE_INTR_SLV_UNEXP)
		dev_warn(port->dev, "Slave unexpected completion\n");

	if (status & XILINX_PCIE_INTR_SLV_COMPL)
		dev_warn(port->dev, "Slave completion timeout\n");

	if (status & XILINX_PCIE_INTR_SLV_ERRP)
		dev_warn(port->dev, "Slave Error Poison\n");

	if (status & XILINX_PCIE_INTR_SLV_CMPABT)
		dev_warn(port->dev, "Slave Completer Abort\n");

	if (status & XILINX_PCIE_INTR_SLV_ILLBUR)
		dev_warn(port->dev, "Slave Illegal Burst\n");

	if (status & XILINX_PCIE_INTR_MST_DECERR)
		dev_warn(port->dev, "Master decode error\n");

	if (status & XILINX_PCIE_INTR_MST_SLVERR)
		dev_warn(port->dev, "Master slave error\n");

error:
	/* Clear the Interrupt Decode register */
	pcie_write(port, status, XILINX_PCIE_REG_IDR);

	return IRQ_HANDLED;
}

static struct irq_chip xilinx_msi_irq_chip = {
	.name = "xilinx_pcie:msi",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info xilinx_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		MSI_FLAG_MULTI_PCI_MSI),
	.chip = &xilinx_msi_irq_chip,
};

static void xilinx_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct xilinx_pcie_port *pcie = irq_data_get_irq_chip_data(data);
	struct xilinx_msi *msi = &pcie->msi;
	phys_addr_t msi_addr;

	msi_addr = virt_to_phys((void *)msi->msi_pages);
	msg->address_lo = lower_32_bits(msi_addr);
	msg->address_hi = upper_32_bits(msi_addr);
	msg->data = data->hwirq;
}

static int xilinx_msi_set_affinity(struct irq_data *irq_data,
				   const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip xilinx_irq_chip = {
	.name = "Xilinx MSI",
	.irq_compose_msi_msg = xilinx_compose_msi_msg,
	.irq_set_affinity = xilinx_msi_set_affinity,
};

static int xilinx_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	struct xilinx_pcie_port *pcie = domain->host_data;
	struct xilinx_msi *msi = &pcie->msi;
	int bit;
	int i;

	mutex_lock(&msi->lock);
	bit = bitmap_find_free_region(msi->bitmap, XILINX_NUM_MSI_IRQS,
				      get_count_order(nr_irqs));
	if (bit < 0) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, bit + i, &xilinx_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}
	mutex_unlock(&msi->lock);
	return 0;
}

static void xilinx_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct xilinx_pcie_port *pcie = irq_data_get_irq_chip_data(data);
	struct xilinx_msi *msi = &pcie->msi;

	mutex_lock(&msi->lock);
	bitmap_release_region(msi->bitmap, data->hwirq,
			      get_count_order(nr_irqs));
	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops dev_msi_domain_ops = {
	.alloc  = xilinx_irq_domain_alloc,
	.free   = xilinx_irq_domain_free,
};

static int xilinx_pcie_init_msi_irq_domain(struct xilinx_pcie_port *port)
{
	struct fwnode_handle *fwnode = of_node_to_fwnode(port->dev->of_node);
	struct xilinx_msi *msi = &port->msi;
	int size = BITS_TO_LONGS(XILINX_NUM_MSI_IRQS) * sizeof(long);

	msi->dev_domain = irq_domain_add_linear(NULL, XILINX_NUM_MSI_IRQS,
						&dev_msi_domain_ops, port);
	if (!msi->dev_domain) {
		dev_err(port->dev, "failed to create dev IRQ domain\n");
		return -ENOMEM;
	}
	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
						    &xilinx_msi_domain_info,
						    msi->dev_domain);
	if (!msi->msi_domain) {
		dev_err(port->dev, "failed to create msi IRQ domain\n");
		irq_domain_remove(msi->dev_domain);
		return -ENOMEM;
	}

	mutex_init(&msi->lock);
	msi->bitmap = kzalloc(size, GFP_KERNEL);
	if (!msi->bitmap)
		return -ENOMEM;

	xilinx_pcie_enable_msi(port);

	return 0;
}

/**
 * xilinx_pcie_init_irq_domain - Initialize IRQ domain
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_init_irq_domain(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return PTR_ERR(pcie_intc_node);
	}

	port->leg_domain = irq_domain_add_linear(pcie_intc_node, INTX_NUM,
						 &intx_domain_ops,
						 port);
	if (!port->leg_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return PTR_ERR(port->leg_domain);
	}

	xilinx_pcie_init_msi_irq_domain(port);

	return 0;
}

/**
 * xilinx_pcie_init_port - Initialize hardware
 * @port: PCIe port information
 */
static void xilinx_pcie_init_port(struct xilinx_pcie_port *port)
{
	if (xilinx_pcie_link_is_up(port))
		dev_info(port->dev, "PCIe Link is UP\n");
	else
		dev_info(port->dev, "PCIe Link is DOWN\n");

	/* Disable all interrupts */
	pcie_write(port, ~XILINX_PCIE_IDR_ALL_MASK,
		   XILINX_PCIE_REG_IMR);

	/* Clear pending interrupts */
	pcie_write(port, pcie_read(port, XILINX_PCIE_REG_IDR) &
			 XILINX_PCIE_IMR_ALL_MASK,
		   XILINX_PCIE_REG_IDR);

	/* Enable all interrupts */
	pcie_write(port, XILINX_PCIE_IMR_ALL_MASK, XILINX_PCIE_REG_IMR);
	pcie_write(port, XILINX_PCIE_IDRN_MASK, XILINX_PCIE_REG_IDRN_MASK);
	if (port->msi_mode == MSI_DECD_MODE) {
		pcie_write(port, XILINX_PCIE_IDR_ALL_MASK,
			   XILINX_PCIE_REG_MSI_LOW_MASK);
		pcie_write(port, XILINX_PCIE_IDR_ALL_MASK,
			   XILINX_PCIE_REG_MSI_HI_MASK);
	}
	/* Enable the Bridge enable bit */
	pcie_write(port, pcie_read(port, XILINX_PCIE_REG_RPSC) |
			 XILINX_PCIE_REG_RPSC_BEN,
		   XILINX_PCIE_REG_RPSC);

}

static int xilinx_request_misc_irq(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err;

	port->irq_misc = platform_get_irq_byname(pdev, "misc");
	if (port->irq_misc <= 0) {
		dev_err(dev, "Unable to find misc IRQ line\n");
		return port->irq_misc;
	}
	err = devm_request_irq(dev, port->irq_misc,
			       xilinx_pcie_intr_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "xilinx-pcie", port);
	if (err) {
		dev_err(dev, "unable to request misc IRQ line %d\n",
			port->irq_misc);
		return err;
	}

	return 0;
}

static int xilinx_request_msi_irq(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct platform_device *pdev = to_platform_device(dev);

	port->msi.irq_msi0 = platform_get_irq_byname(pdev, "msi0");
	if (port->msi.irq_msi0 <= 0) {
		dev_err(dev, "Unable to find msi0 IRQ line\n");
		return port->msi.irq_msi0;
	}

	irq_set_chained_handler_and_data(port->msi.irq_msi0,
					 xilinx_pcie_msi_handler_low,
					 port);

	port->msi.irq_msi1 = platform_get_irq_byname(pdev, "msi1");
	if (port->msi.irq_msi1 <= 0) {
		dev_err(dev, "Unable to find msi1 IRQ line\n");
		return port->msi.irq_msi1;
	}

	irq_set_chained_handler_and_data(port->msi.irq_msi1,
					 xilinx_pcie_msi_handler_high,
					 port);

	return 0;
}

/**
 * xilinx_pcie_parse_dt - Parse Device tree
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_parse_dt(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct resource regs;
	const char *type;
	int err, mode_val, val;

	if (of_device_is_compatible(node, "xlnx,xdma-host-3.00"))
		port->xdma_config = XDMA_ZYNQMP_PL;
	else if (of_device_is_compatible(node, "xlnx,pcie-dma-versal-2.0"))
		port->xdma_config = XDMA_VERSAL_PL;

	if (port->xdma_config == XDMA_ZYNQMP_PL ||
	    port->xdma_config == XDMA_VERSAL_PL) {
		type = of_get_property(node, "device_type", NULL);
		if (!type || strcmp(type, "pci")) {
			dev_err(dev, "invalid \"device_type\" %s\n", type);
			return -EINVAL;
		}

		err = of_address_to_resource(node, 0, &regs);
		if (err) {
			dev_err(dev, "missing \"reg\" property\n");
			return err;
		}

		port->reg_base = devm_ioremap_resource(dev, &regs);
		if (IS_ERR(port->reg_base))
			return PTR_ERR(port->reg_base);

		if (port->xdma_config == XDMA_ZYNQMP_PL) {
			val = pcie_read(port, XILINX_PCIE_REG_BIR);
			val = (val >> XILINX_PCIE_FIFO_SHIFT) & MSI_DECD_MODE;
			mode_val = pcie_read(port, XILINX_PCIE_REG_VSEC) &
					XILINX_PCIE_VSEC_REV_MASK;
			mode_val = mode_val >> XILINX_PCIE_VSEC_REV_SHIFT;
			if (mode_val && !val) {
				port->msi_mode = MSI_DECD_MODE;
				dev_info(dev, "Using MSI Decode mode\n");
			} else {
				port->msi_mode = MSI_FIFO_MODE;
				dev_info(dev, "Using MSI FIFO mode\n");
			}
		}

		if (port->xdma_config == XDMA_VERSAL_PL)
			port->msi_mode = MSI_DECD_MODE;

		if (port->msi_mode == MSI_DECD_MODE) {
			err = xilinx_request_misc_irq(port);
			if (err)
				return err;

			err = xilinx_request_msi_irq(port);
			if (err)
				return err;

		} else if (port->msi_mode == MSI_FIFO_MODE) {
			port->irq = irq_of_parse_and_map(node, 0);
			if (!port->irq) {
				dev_err(dev, "Unable to find IRQ line\n");
				return -ENXIO;
			}

			err = devm_request_irq(dev, port->irq,
					       xilinx_pcie_intr_handler,
					       IRQF_SHARED | IRQF_NO_THREAD,
					       "xilinx-pcie", port);
			if (err) {
				dev_err(dev, "unable to request irq %d\n",
					port->irq);
				return err;
			}
		}
	}

	return 0;
}

/**
 * xilinx_pcie_probe - Probe function
 * @pdev: Platform device pointer
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_probe(struct platform_device *pdev)
{
	struct xilinx_pcie_port *port;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	int err;
	LIST_HEAD(res);

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*port));
	if (!bridge)
		return -ENODEV;

	port = pci_host_bridge_priv(bridge);

	port->dev = dev;

	err = xilinx_pcie_parse_dt(port);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	xilinx_pcie_init_port(port);

	err = xilinx_pcie_init_irq_domain(port);
	if (err) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return err;
	}

	list_splice_init(&res, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = port;
	bridge->busnr = port->root_busno;
	bridge->ops = &xilinx_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(bridge);
	if (err)
		return err;

	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);
	pci_bus_add_devices(bus);
	return 0;
}

static const struct of_device_id xilinx_pcie_of_match[] = {
	{ .compatible = "xlnx,xdma-host-3.00", },
	{ .compatible = "xlnx,pcie-dma-versal-2.0", },
	{}
};

static struct platform_driver xilinx_pcie_driver = {
	.driver = {
		.name = "xilinx-xdma-pcie",
		.of_match_table = xilinx_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = xilinx_pcie_probe,
};

builtin_platform_driver(xilinx_pcie_driver);
