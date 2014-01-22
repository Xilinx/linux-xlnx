/*
 * Xilinx AXI PCIe IP hardware initialation, setup and
 * configuration spaces access file.
 *
 * Copyright (c) 2010-2011 Xilinx, Inc.
 *
 * This program has adopted some work from PCI/PCIE support for AMCC
 * PowerPC boards written by Benjamin Herrenschmidt.
 * Copyright 2007 Ben. Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/compiler.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/pci-bridge.h>
#include <linux/interrupt.h>
#include "xilinx_axipcie.h"

static struct xilinx_axipcie_port *xilinx_axipcie_ports;
static unsigned int xilinx_axipcie_port_count;

static const struct of_device_id xilinx_axipcie_match[] = {
	{ .compatible = "xlnx,axi-pcie-1.05.a" ,},
	{}
};

static int last_bus_on_record;

#ifdef CONFIG_PCI_MSI
unsigned long msg_addr;
#endif

/* Macros */
#define is_link_up(base_address)	\
	((in_le32(((u8 *)base_address) + AXIPCIE_REG_PSCR) &	\
	AXIPCIE_REG_PSCR_LNKUP) ? 1 : 0)

#define bridge_enable(base_address)	\
	out_le32((((u8 *)base_address) + AXIPCIE_REG_RPSC),	\
		(in_le32(((u8 *)base_address) + AXIPCIE_REG_RPSC) |	\
		AXIPCIE_REG_RPSC_BEN))

/**
 * xilinx_get_axipcie_ip_config_info - Read info from device tree
 * @dev: A pointer to device node to read from
 * @ip_config_info: A pointer to xilinx_pcie_node struct to write device tree
 *			info into to.
 *
 * @return: Error / no error
 *
 * @note: Read related info from device tree
 */
int xilinx_get_axipcie_ip_config_info(struct device_node *dev,
		struct xilinx_axipcie_node *ip_config_info)
{
	u32 *ip_setup_parameter;
	u32 rlen;

	ip_config_info->number_of_instances = 1;

	ip_setup_parameter = (u32 *) of_get_property(dev,
						"xlnx,device-num", &rlen);
	ip_config_info->device_id = 0;

	ip_setup_parameter = (u32 *) of_get_property(dev,
						"xlnx,include-rc", &rlen);

	if (ip_setup_parameter)
		ip_config_info->device_type = be32_to_cpup(ip_setup_parameter);
	else
		return -ENODEV;

	ip_setup_parameter = (u32 *) of_get_property(dev,
						"reg", &rlen);

	if (ip_setup_parameter) {
		ip_config_info->reg_base =
					be32_to_cpup(ip_setup_parameter);
		ip_config_info->reg_len =
					be32_to_cpup(ip_setup_parameter + 1);
	} else
		return -ENODEV;

	ip_setup_parameter = (u32 *) of_get_property(dev,
						"xlnx,pciebar-num", &rlen);

	if (ip_setup_parameter)
		ip_config_info->bars_num = be32_to_cpup(ip_setup_parameter);
	else
		return -ENODEV;

	ip_config_info->irq_num = irq_of_parse_and_map(dev, 0);

	/* Get address translation parameters */
	ip_setup_parameter = (u32 *) of_get_property(dev,
					"xlnx,pciebar2axibar-0", &rlen);

	if (ip_setup_parameter)
		ip_config_info->pcie2axibar_0 =
					be32_to_cpup(ip_setup_parameter);
	else
		return -ENODEV;

	ip_setup_parameter = (u32 *) of_get_property(dev,
					"xlnx,pciebar2axibar-1", &rlen);

	if (ip_setup_parameter)
		ip_config_info->pcie2axibar_1 =
					be32_to_cpup(ip_setup_parameter);
	else
		ip_config_info->pcie2axibar_1 = 0x0;

	return 0;
}

/**
 * fixup_xilinx_axipcie_bridge
 * @dev: A pointer to device pcie device struct
 *
 * @return: None
 *
 * @note: A fix up routine to be called by kernel during enumeration
 */
static void fixup_xilinx_axipcie_bridge(struct pci_dev *dev)
{
	struct pci_controller *hose;
	int i;

	if (dev->devfn != 0 || dev->bus->self != NULL)
		return;

	hose = pci_bus_to_host(dev->bus);
	if (hose == NULL)
		return;

	if (!of_match_node(xilinx_axipcie_match, hose->dn))
		return;

	/* Hide the PCI host BARs from the kernel as their content doesn't
	 * fit well in the resource management
	 */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		dev->resource[i].start = dev->resource[i].end = 0;
		dev->resource[i].flags = 0;
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, fixup_xilinx_axipcie_bridge);

/**
 * xilinx_init_axipcie_port - Initialize hardware
 * @port: A pointer to a pcie port that needs to be initialized
 *
 * @return: Error / no error
 *
 * @note: None
 */
static int xilinx_init_axipcie_port(struct xilinx_axipcie_port *port)
{
	void __iomem *base_addr_remap = NULL;

	/* base_addr_remap = ioremap(port->reg_base, PORT_REG_SIZE); */
	base_addr_remap = ioremap(port->reg_base, port->reg_len);
	if (!base_addr_remap)
		return -ENOMEM;

	port->base_addr_remap = base_addr_remap;

	/* make sure it is root port before touching header */
	if (port->type) {

		port->header_remap = base_addr_remap;
		out_le32((((u8 *)port->base_addr_remap) + PCIE_CFG_CMD),
							BUS_MASTER_ENABLE);
	}

#ifdef CONFIG_PCI_MSI
	msg_addr = port->reg_base & ~0xFFF;	/* 4KB aligned */
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_MSIBASE1),
							0x00000000);
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_MSIBASE2),
							msg_addr);
#endif

	port->link = is_link_up(port->base_addr_remap);
	if (!port->link)
		pr_info("LINK IS DOWN\n");
	else
		pr_info("LINK IS UP\n");

	/* Disable all interrupts*/
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_IMR),
					~AXIPCIE_REG_IDR_MASKALL);
	/* Clear pending interrupts*/
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_IDR),
		in_le32(((u8 *)port->base_addr_remap) + AXIPCIE_REG_IDR) &
					AXIPCIE_REG_IMR_MASKALL);
	/* Enable all interrupts*/
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_IMR),
					AXIPCIE_REG_IMR_MASKALL);

	/* Bridge enable must be done after enumeration,
		but there is no callback defined */
	bridge_enable(port->base_addr_remap);

	return 0;
}

/**
 * xilinx_axipcie_verify_config
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: device/function
 *
 * @return: Error / no error
 *
 * @note: Make sure we can handle this configuration call on our
 *        device.
 */
static int xilinx_axipcie_verify_config(struct xilinx_axipcie_port *port,
				struct pci_bus *bus,
				unsigned int devfn)
{
	static int message;

	/* Endpoint can not generate upstream(remote) config cycles */
	if ((!port->type) && bus->number != port->hose->first_busno)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check we are within the mapped range */
	if (bus->number > port->hose->last_busno) {
		if (!message) {
			printk(KERN_WARNING "Warning! Probing bus %u"
			       " out of range !\n", bus->number);
			message++;
		}
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* The other side of the RC has only one device as well */
	if (bus->number == (port->hose->first_busno + 1) &&
	    PCI_SLOT(devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check if we have a link */
	if (!port->link)
		port->link = is_link_up(port->base_addr_remap);

	if ((bus->number != port->hose->first_busno) && !port->link)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return 0;
}

/**
 * xilinx_axipcie_get_config_base
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 *
 * @return: Base address of the configuration space needed to be
 *          accessed.
 *
 * @note: Get the base address of the configuration space for this
 *        pcie device.
 */
static void __iomem *xilinx_axipcie_get_config_base(
					struct xilinx_axipcie_port *port,
					struct pci_bus *bus,
					unsigned int devfn)
{
	int relbus;

	relbus = ((bus->number << BUS_LOC_SHIFT) | (devfn << DEV_LOC_SHIFT));

	if (relbus == 0)
		return (void __iomem *)port->header_remap;

	return (void __iomem *)port->hose->cfg_data + relbus;
}

/**
 * xilinx_axipcie_read_config - Read config reg.
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 * @offset: Offset from base
 * @len: Byte/word/dword
 * @val: A pointer to value read
 *
 * @return: Error / no error
 *
 *
 * @note: Read byte/word/dword from pcie device config reg.
 */
static int xilinx_axipcie_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int offset,
				int len,
				u32 *val)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	struct xilinx_axipcie_port *port =
				&xilinx_axipcie_ports[hose->indirect_type];
	void __iomem *addr;

	if (xilinx_axipcie_verify_config(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = xilinx_axipcie_get_config_base(port, bus, devfn);

	if ((bus->number == 0) && devfn > 0) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_SUCCESSFUL;
	}

	switch (len) {
	case 1:
		*val = in_8((u8 *)(addr + offset));
		break;
	case 2:
		*val = in_le16((u16 *)(addr + offset));
		break;
	default:
		*val = in_le32((u32 *)(addr + offset));
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

/**
 * xilinx_axipcie_write_config - Write config reg.
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 * @offset: Offset from base
 * @len: Byte/word/dword
 * @val: Value to be written to device
 *
 * @return: Error / no error
 *
 *
 * @note: Write byte/word/dword to pcie device config reg.
 */
static int xilinx_axipcie_write_config(struct pci_bus *bus,
				unsigned int devfn,
				int offset,
				int len,
				u32 val)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	struct xilinx_axipcie_port *port =
				&xilinx_axipcie_ports[hose->indirect_type];
	void __iomem  *addr;

	if (xilinx_axipcie_verify_config(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = xilinx_axipcie_get_config_base(port, bus, devfn);

	if ((bus->number == 0) && devfn > 0)
		return PCIBIOS_SUCCESSFUL;

	switch (len) {
	case 1:
		out_8((u8 *)(addr + offset), val);
		break;
	case 2:
		out_le16((u16 *)(addr + offset), val);
		break;
	default:
		out_le32((u32 *)(addr + offset), val);
		break;
	}

	wmb();

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops xlnx_pcie_pci_ops = {
	.read  = xilinx_axipcie_read_config,
	.write = xilinx_axipcie_write_config,
};

/**
 * xilinx_set_bridge_resource - Setup base & limit registers of config space.
 * @port: Pointer to a root port
 *
 * @return: None
 *
 * @note: None
 */
void xilinx_set_bridge_resource(struct xilinx_axipcie_port *port)
{
	const u32 *ranges;
	int rlen;
	/* The address cells of PCIe parent node */
	int pna = of_n_addr_cells(port->node);
	int np = pna + 5;
	u32 pci_space;
	unsigned long long pci_addr, size;
	struct device_node *dev;
	u32 val = 0;

	dev = port->node;

	/* Get ranges property */
	ranges = of_get_property(dev, "ranges", &rlen);
	if (ranges == NULL) {
		printk(KERN_DEBUG "%s:Didnot get any ranges property\n",
								__func__);
		return;
	}

	while ((rlen -= np * 4) >= 0) {
		pci_space = be32_to_cpup(ranges);
		pci_addr = of_read_number(ranges + 1, 2);
		size = of_read_number(ranges + pna + 3, 2);

		printk(KERN_INFO "%s:pci_space: 0x%08x pci_addr:0x%016llx size:"
			"0x%016llx\n", __func__, pci_space, pci_addr, size);

		ranges += np;

		switch ((pci_space >> 24) & 0x3) {
		case 1:		/* PCI IO space */
			printk(KERN_INFO "%s:Setting resource in IO Space\n",
								__func__);

			val = ((pci_addr >> 8) & 0x000000F0) |
					((pci_addr + size - 1) & 0x0000F000);

			out_le32((((u8 *)port->header_remap) + PCIE_CFG_IO),
									val);

			val = ((pci_addr >> 16) & 0x0000FFFF) |
					((pci_addr + size - 1) & 0xFFFF0000);

			out_le32((((u8 *)port->header_remap) +
						PCIE_CFG_IO_UPPER), val);

			break;
		case 2:		/* PCI Memory space */
			printk(KERN_INFO "%s:Setting resource in Memory Space\n",
								__func__);
			val = ((pci_addr >> 16) & 0xfff0) |
					((pci_addr + size - 1) & 0xfff00000);

			/* out_le32((((u8 *)port->header_remap) + PCIE_CFG_MEM),
								val); */

			break;
		case 3:		/* PCI 64 bits Memory space */
			printk(KERN_INFO "%s:Setting resource in Prefetchable"
					" Memory Space\n", __func__);

			val = ((pci_addr >> 16) & 0xfff0) |
					((pci_addr + size - 1) & 0xfff00000);

			out_le32((((u8 *)port->header_remap) +
						PCIE_CFG_PREF_MEM), val);

			val = ((pci_addr >> 32) & 0xffffffff);
			out_le32((((u8 *)port->header_remap) +
					PCIE_CFG_PREF_BASE_UPPER), val);

			val = (((pci_addr + size - 1) >> 32) & 0xffffffff);
			out_le32((((u8 *)port->header_remap) +
					PCIE_CFG_PREF_LIMIT_UPPER), val);

			break;
		}
	}

	/* EP initiated memory access */
	out_le32((((u8 *)port->header_remap) + PCIE_CFG_AD1),
						port->pcie2axibar_0);
	out_le32((((u8 *)port->header_remap) + PCIE_CFG_AD2),
						port->pcie2axibar_1);
}

/**
 * xilinx_setup_axipcie_root_port - Setup root port
 * @port: Pointer to a root port
 *
 * @return: Error / no error
 *
 * @note: This is a root port so set it up accordingly
 */
static int __init xilinx_setup_axipcie_root_port(
					struct xilinx_axipcie_port *port)
{
	struct pci_controller *hose = NULL;
	u32 val = 0;

	/* Allocate the host controller data structure */
	hose = pcibios_alloc_controller(port->node);
	if (!hose) {
		iounmap(port->base_addr_remap);
		iounmap(port->header_remap);
		return -ENOMEM;
	}

	hose->indirect_type = port->index;

	/* Get bus range */
	hose->first_busno = last_bus_on_record;

	val = in_le32(((u8 *)port->header_remap) + AXIPCIE_REG_BIR);
	val = (val >> 16) & 0x7;
	hose->last_busno = (((port->reg_base - port->reg_len - 1) >> 20)
							& 0xFF) & val;

	/* Write primary, secondary and subordinate bus numbers */
	val = hose->first_busno;
	val |= ((hose->first_busno + 1) << 8);
	val |= (hose->last_busno << 16);

	out_le32((((u8 *)port->header_remap) + PCIE_CFG_BUS), val);
	last_bus_on_record = hose->last_busno + 1;

	port->ecam_remap = port->header_remap;

	/* Setup config space */
	hose->cfg_addr = port->header_remap;
	hose->cfg_data = port->ecam_remap;
	hose->ops = &xlnx_pcie_pci_ops;
	port->hose = hose;

	xilinx_set_bridge_resource(port);
	/* Parse outbound mapping resources */
	pci_process_bridge_OF_ranges(hose, port->node, PRIMARY_BUS);

	return 0;
}

/**
 * Interrupt handler
 */
static irqreturn_t xilinx_axipcie_intr_handler(int irq, void *data)
{
	struct xilinx_axipcie_port *port = (struct xilinx_axipcie_port *)data;
	u32 val = 0, mask = 0;
	u32 status;
	u32 msi_addr = 0;
	u32 msi_data = 0;

	/* Read interrupt decode and mask registers */
	val = in_le32(((u8 *)port->header_remap) + AXIPCIE_REG_IDR);
	mask = in_le32(((u8 *)port->header_remap) + AXIPCIE_REG_IMR);

	status = val & mask;
	if (!status)
		return IRQ_NONE;

	if (status & AXIPCIE_INTR_LINK_DOWN)
		printk(KERN_ERR "Link Down\n");

	if (status & AXIPCIE_INTR_ECRC_ERR)
		printk(KERN_WARNING "ECRC failed\n");

	if (status & AXIPCIE_INTR_STR_ERR)
		printk(KERN_WARNING "Streaming error\n");

	if (status & AXIPCIE_INTR_HOT_RESET)
		printk(KERN_INFO "Hot reset\n");

	if (status & AXIPCIE_INTR_CFG_TIMEOUT)
		printk(KERN_WARNING "ECAM access timeout\n");

	if (status & AXIPCIE_INTR_CORRECTABLE) {
		printk(KERN_WARNING "Correctable error message\n");
		val = in_le32(((u8 *)port->header_remap) +
				AXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			out_le32((((u8 *)port->base_addr_remap) +
				AXIPCIE_REG_RPEFR), 0xFFFFFFFF);
			DBG("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & AXIPCIE_INTR_NONFATAL) {
		printk(KERN_WARNING "Non fatal error message\n");
		val = in_le32(((u8 *)port->header_remap) +
				AXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			out_le32((((u8 *)port->base_addr_remap) +
				AXIPCIE_REG_RPEFR), 0xFFFFFFFF);
			DBG("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & AXIPCIE_INTR_FATAL) {
		printk(KERN_WARNING "Fatal error message\n");
		val = in_le32(((u8 *)port->header_remap) +
				AXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			out_le32((((u8 *)port->base_addr_remap) +
				AXIPCIE_REG_RPEFR), 0xFFFFFFFF);
			DBG("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & AXIPCIE_INTR_INTX) {
		/* INTx interrupt received */
		val = in_le32(((u8 *)port->header_remap) + AXIPCIE_REG_RPIFR1);

		/* Check whether interrupt valid */
		if (!(val & (1 << 31))) {
			printk(KERN_WARNING "RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
		}

		/* Check MSI or INTX */
		if (!(val & (1 << 30))) {
			if (val & (1 << 29))
				DBG("INTx assert\n");
			else
				DBG("INTx deassert\n");
		}

		/* Clear interrupt FIFO register 1 */
		out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_RPIFR1),
								0xFFFFFFFF);
	}

	if (status & AXIPCIE_INTR_MSI) {
		/* MSI Interrupt */
		val = in_le32(((u8 *)port->header_remap) + AXIPCIE_REG_RPIFR1);

		if (!(val & (1 << 31))) {
			printk(KERN_WARNING "RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
		}

		if (val & (1 << 30)) {
			msi_addr = (val >> 16) & 0x7FF;
			msi_data = in_le32(((u8 *)port->header_remap) +
						AXIPCIE_REG_RPIFR2) & 0xFFFF;
			DBG("%s: msi_addr %08x msi_data %08x\n",
					__func__, msi_addr, msi_data);
		}

		/* Clear interrupt FIFO register 1 */
		out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_RPIFR1),
								0xFFFFFFFF);
#ifdef CONFIG_PCI_MSI
		/* Handle MSI Interrupt */
		if (msi_data >= IRQ_XILINX_MSI_0)
			generic_handle_irq(msi_data);
#endif
	}

	if (status & AXIPCIE_INTR_SLV_UNSUPP)
		printk(KERN_WARNING "Slave unsupported request\n");

	if (status & AXIPCIE_INTR_SLV_UNEXP)
		printk(KERN_WARNING "Slave unexpected completion\n");

	if (status & AXIPCIE_INTR_SLV_COMPL)
		printk(KERN_WARNING "Slave completion timeout\n");

	if (status & AXIPCIE_INTR_SLV_ERRP)
		printk(KERN_WARNING "Slave Error Poison\n");

	if (status & AXIPCIE_INTR_SLV_CMPABT)
		printk(KERN_WARNING "Slave Completer Abort\n");

	if (status & AXIPCIE_INTR_SLV_ILLBUR)
		printk(KERN_WARNING "Slave Illegal Burst\n");

	if (status & AXIPCIE_INTR_MST_DECERR)
		printk(KERN_WARNING "Master decode error\n");

	if (status & AXIPCIE_INTR_MST_SLVERR)
		printk(KERN_WARNING "Master slave error\n");

	if (status & AXIPCIE_INTR_MST_ERRP)
		printk(KERN_WARNING "Master error poison\n");

	/* Clear the Interrupt Decode register */
	out_le32((((u8 *)port->base_addr_remap) + AXIPCIE_REG_IDR), status);

	return IRQ_HANDLED;
}

/**
 * xilinx_probe_axipcie_node
 * @np: Pointer to device node to be probed
 *
 * @return: Error / no error
 *
 * @note: Find out how this pcie node is configured
 */
static int __init xilinx_probe_axipcie_node(struct device_node *np)
{
	struct xilinx_axipcie_port *port;
	struct xilinx_axipcie_node ip_setup_info;
	int portno;
	int error;
	int ret;

	printk(KERN_INFO "Probing Xilinx PCI Express root complex device\n");

	error = xilinx_get_axipcie_ip_config_info(np , &ip_setup_info);

	if (error) {
		printk(KERN_INFO "Error while getting pcie config info\n");
		return error;
	}

	if (!xilinx_axipcie_port_count) {
		xilinx_axipcie_port_count = ip_setup_info.number_of_instances;

		if (xilinx_axipcie_port_count) {

			xilinx_axipcie_ports =
					kzalloc(xilinx_axipcie_port_count *
			sizeof(struct xilinx_axipcie_port), GFP_KERNEL);

			if (!xilinx_axipcie_ports) {
				printk(KERN_INFO "Memory allocation failed\n");
				return -ENOMEM;
			}
		} else /* not suppose to be here
			* when we don't have pcie ports */
			return -ENODEV;
	}

	/* Initialize this port vital info. struct */
	portno = ip_setup_info.device_id;

	port = &xilinx_axipcie_ports[portno];
	port->node = of_node_get(np);
	port->index = portno;
	port->type = ip_setup_info.device_type;
	port->reg_base = ip_setup_info.reg_base;
	port->reg_len = ip_setup_info.reg_len;
	port->bars_num  = ip_setup_info.bars_num;
	port->irq_num	= ip_setup_info.irq_num;
	port->header_addr = port->reg_base + AXIPCIE_LOCAL_CNFG_BASE;
	port->pcie2axibar_0 = ip_setup_info.pcie2axibar_0;
	port->pcie2axibar_1 = ip_setup_info.pcie2axibar_1;

	irq_set_chip_data(port->irq_num, port);

	/* initialize hardware */
	error = xilinx_init_axipcie_port(port);
	if (error) {
		printk(KERN_INFO "Error while initialize pcie port\n");
		return error;
	}

	/* Register interrupt handler */
	ret = request_irq(port->irq_num, xilinx_axipcie_intr_handler,
						IRQF_SHARED, "xaxipcie", port);
	if (ret) {
		printk(KERN_ERR "%s: Could not allocate interrupt\n", __func__);
		return ret;
	}

	/* Setup hose data structure */
	if (port->type) {
		error = xilinx_setup_axipcie_root_port(port);
		if (error) {
			printk(KERN_INFO "Error while initialize "
							"pcie root port\n");
			return error;
		}
	}

	return 0;
}

/**
 * pcibios_set_master - Architecture specific function
 * @dev: A pointer to device pcie device struct
 *
 * @return: Error / no error
 * @note: Enables Bridge Enable bit during the rescan process
 */
void pcibios_set_master(struct pci_dev *dev)
{
	struct pci_controller *hose =
			(struct pci_controller *) dev->bus->sysdata;
	struct xilinx_axipcie_port *port =
			&xilinx_axipcie_ports[hose->indirect_type];

	if (port->link)
		bridge_enable(port->base_addr_remap);
}

/**
 * xilinx_find_axipcie_nodes - Entry function
 * void
 *
 * @return: Error / no error
 * @note: Find pcie nodes in device tree
 */
static int __init xilinx_find_axipcie_nodes(void)
{
	struct device_node *np;
	const struct of_device_id *matches = xilinx_axipcie_match;
	int error = 0;

	printk(KERN_INFO "Initialising Xilinx PCI Express root"
						" complex device\n");
	for_each_matching_node(np, matches) {
		error = xilinx_probe_axipcie_node(np);
		if (error)
			return error;
	}
	return 0;
}

arch_initcall(xilinx_find_axipcie_nodes);
