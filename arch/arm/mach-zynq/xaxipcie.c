/*
 * Xilinx AXI PCIe IP hardware initialation, setup and
 * configuration spaces access file.
 *
 * Copyright (c) 2012 Xilinx, Inc.
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
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/irqdomain.h>
#include <linux/pci.h>
#include <asm/mach/pci.h>

/* Register definitions */
#define PCIE_CFG_CMD			0x00000004
#define PCIE_CFG_CLS			0x00000008
#define PCIE_CFG_HDR			0x0000000C
#define PCIE_CFG_AD1			0x00000010
#define PCIE_CFG_AD2			0x00000014
#define PCIE_CFG_BUS			0x00000018
#define PCIE_CFG_IO			0x0000001C
#define PCIE_CFG_MEM			0x00000020
#define PCIE_CFG_PREF_MEM		0x00000024
#define PCIE_CFG_PREF_BASE_UPPER	0x00000028
#define PCIE_CFG_PREF_LIMIT_UPPER	0x0000002c
#define PCIE_CFG_IO_UPPER		0x00000030

#define XAXIPCIE_REG_VSECC		0x00000128
#define XAXIPCIE_REG_VSECH		0x0000012c
#define XAXIPCIE_REG_BIR		0x00000130
#define XAXIPCIE_REG_BSCR		0x00000134
#define XAXIPCIE_REG_IDR		0x00000138
#define XAXIPCIE_REG_IMR		0x0000013c
#define XAXIPCIE_REG_BLR		0x00000140
#define XAXIPCIE_REG_PSCR		0x00000144
#define XAXIPCIE_REG_RPSC		0x00000148
#define XAXIPCIE_REG_MSIBASE1		0x0000014c
#define XAXIPCIE_REG_MSIBASE2		0x00000150
#define XAXIPCIE_REG_RPEFR		0x00000154
#define XAXIPCIE_REG_RPIFR1		0x00000158
#define XAXIPCIE_REG_RPIFR2		0x0000015c
#define XAXIPCIE_REG_VSECC2		0x00000200
#define XAXIPCIE_REG_VSECH2		0x00000204

/* Interrupt register defines */
#define XAXIPCIE_INTR_LINK_DOWN		(1 << 0)
#define XAXIPCIE_INTR_ECRC_ERR		(1 << 1)
#define XAXIPCIE_INTR_STR_ERR		(1 << 2)
#define XAXIPCIE_INTR_HOT_RESET		(1 << 3)
#define XAXIPCIE_INTR_CFG_COMPL		(7 << 5)
#define XAXIPCIE_INTR_CFG_TIMEOUT	(1 << 8)
#define XAXIPCIE_INTR_CORRECTABLE	(1 << 9)
#define XAXIPCIE_INTR_NONFATAL		(1 << 10)
#define XAXIPCIE_INTR_FATAL		(1 << 11)
#define XAXIPCIE_INTR_INTX		(1 << 16)
#define XAXIPCIE_INTR_MSI		(1 << 17)
#define XAXIPCIE_INTR_SLV_UNSUPP	(1 << 20)
#define XAXIPCIE_INTR_SLV_UNEXP		(1 << 21)
#define XAXIPCIE_INTR_SLV_COMPL		(1 << 22)
#define XAXIPCIE_INTR_SLV_ERRP		(1 << 23)
#define XAXIPCIE_INTR_SLV_CMPABT	(1 << 24)
#define XAXIPCIE_INTR_SLV_ILLBUR	(1 << 25)
#define XAXIPCIE_INTR_MST_DECERR	(1 << 26)
#define XAXIPCIE_INTR_MST_SLVERR	(1 << 27)
#define XAXIPCIE_INTR_MST_ERRP		(1 << 28)

#define BUS_LOC_SHIFT			20
#define DEV_LOC_SHIFT			12
#define PRIMARY_BUS			1
#define PORT_REG_SIZE			0x1000
#define PORT_HEADER_SIZE		0x128

#define XAXIPCIE_LOCAL_CNFG_BASE	0x00000000
#define XAXIPCIE_REG_BASE		0x00000128
#define XAXIPCIE_REG_PSCR_LNKUP		0x00000800
#define XAXIPCIE_REG_IMR_MASKALL	0x1FF30FED
#define XAXIPCIE_REG_IDR_MASKALL	0xFFFFFFFF
#define XAXIPCIE_REG_RPSC_BEN		0x00000001
#define BUS_MASTER_ENABLE		0x00000004

#define XAXIPCIE_ACCESS8	1
#define XAXIPCIE_ACCESS16	2

#define XAXIPCIE_MEM_SPACE	2
#define XAXIPCIE_MEM_SPACE64	3

/* Config structure for PCIe */
struct xaxi_pcie_of_config {
	u32 num_instances;
	u32 device_id;
	u32 device_type;
	u32 ecam_base;
	u32 ecam_high;
	u32 baseaddr;
	u32 highaddr;
	u32 bars_num;
	u32 irq_num;
	u32 reg_base;
	u32 reg_len;
	u32 pcie2axibar_0;
	u32 pcie2axibar_1;
	const __be32 *ranges;
	int range_len;
	u32 address_cells;
};

/* PCIe Root Port Structure */
struct xaxi_pcie_port {
	struct device_node *node;
	u32 reg_base;
	u32 reg_len;
	u32 ecam_base;
	u32 ecam_high;
	u32 baseaddr;
	u32 highaddr;
	u32 header_addr;
	u8 index;
	u8 type;
	u8 link_up;
	u8 bars_num;
	u32 irq_num;
	const __be32 *ranges;
	int range_len;
	u32 pna;
	u8 __iomem *base_addr_remap;
	u8 __iomem *header_remap;
	u8 __iomem *ecam_remap;
	u32 pcie2axibar_0;
	u32 pcie2axibar_1;
	u32 root_bus_nr;
	u32 first_busno;
	u32 last_busno;
	resource_size_t isa_mem_phys;
	resource_size_t isa_mem_size;
	resource_size_t pci_mem_offset;
	struct resource io_resource;
	struct resource mem_resources[3];
	char mem_space_name[16];
};

static struct xaxi_pcie_port *xaxi_pcie_ports;
static int xaxi_pcie_port_cnt;
static int last_bus_on_record;

/* ISA Memory physical address */
static resource_size_t isa_mem_base;

#ifdef CONFIG_PCI_MSI
static int xaxipcie_msi_irq_base;

int xaxipcie_alloc_msi_irqdescs(struct device_node *node,
				unsigned long msg_addr);
#endif

/* Macros */
#define is_link_up(base_address)	\
	((readl(base_address + XAXIPCIE_REG_PSCR) &	\
	XAXIPCIE_REG_PSCR_LNKUP) ? 1 : 0)

#define bridge_enable(base_address)	\
	writel((readl(base_address + XAXIPCIE_REG_RPSC) |	\
		XAXIPCIE_REG_RPSC_BEN), \
		(base_address + XAXIPCIE_REG_RPSC))

/**
 * xaxi_pcie_verify_config
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: device/function
 *
 * @return: Error / no error
 *
 * @note: Make sure we can handle this configuration call on our
 *        device.
 */
static int xaxi_pcie_verify_config(struct xaxi_pcie_port *port,
				struct pci_bus *bus,
				unsigned int devfn)
{
	static int message;

	/* Endpoint can not generate upstream(remote) config cycles */
	if ((!port->type) && bus->number != port->first_busno)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check we are within the mapped range */
	if (bus->number > port->last_busno) {
		if (!message) {
			pr_warn("Warning! Probing bus %u out of range !\n",
				bus->number);
			message++;
		}
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* The other side of the RC has only one device as well */
	if (bus->number == (port->first_busno + 1) &&
		PCI_SLOT(devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check if we have a link */
	if (!port->link_up)
		port->link_up = is_link_up(port->base_addr_remap);

	if ((bus->number != port->first_busno) && !port->link_up)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return 0;
}

/**
 * xaxi_pcie_get_config_base
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 * @where: Offset from base
 *
 * @return: Base address of the configuration space needed to be
 *          accessed.
 *
 * @note: Get the base address of the configuration space for this
 *        pcie device.
 */
static void __iomem *xaxi_pcie_get_config_base(
				struct xaxi_pcie_port *port,
				struct pci_bus *bus,
				unsigned int devfn, int where)
{
	int relbus;

	relbus = ((bus->number << BUS_LOC_SHIFT) | (devfn << DEV_LOC_SHIFT));

	return port->header_remap + relbus + where;
}

/**
 * xaxi_pcie_read_config - Read config reg.
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: A pointer to value read
 *
 * @return: Error / no error
 *
 *
 * @note: Read byte/word/dword from pcie device config reg.
 */
static int xaxi_pcie_read_config(struct pci_bus *bus,
				unsigned int devfn,
				int where,
				int size,
				u32 *val)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct xaxi_pcie_port *port = sys->private_data;
	void __iomem *addr;

	if (xaxi_pcie_verify_config(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = xaxi_pcie_get_config_base(port, bus, devfn, where);

	if ((bus->number == 0) && devfn > 0) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_SUCCESSFUL;
	}

	switch (size) {
	case XAXIPCIE_ACCESS8:
		*val = readb(addr);
		break;
	case XAXIPCIE_ACCESS16:
		*val = readw(addr);
		break;
	default:
		*val = readl(addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

/**
 * xaxi_pcie_write_config - Write config reg.
 * @port: A pointer to a pcie port that needs to be handled
 * @bus: Bus structure of current bus
 * @devfun: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be written to device
 *
 * @return: Error / no error
 *
 *
 * @note: Write byte/word/dword to pcie device config reg.
 */
static int xaxi_pcie_write_config(struct pci_bus *bus,
				unsigned int devfn,
				int where,
				int size,
				u32 val)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct xaxi_pcie_port *port = sys->private_data;
	void __iomem *addr;

	if (xaxi_pcie_verify_config(port, bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = xaxi_pcie_get_config_base(port, bus, devfn, where);

	if ((bus->number == 0) && devfn > 0)
		return PCIBIOS_SUCCESSFUL;

	switch (size) {
	case XAXIPCIE_ACCESS8:
		writeb(val, addr);
		break;
	case XAXIPCIE_ACCESS16:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
		break;
	}

	wmb();

	return PCIBIOS_SUCCESSFUL;
}

/**
 * xaxi_pcie_set_bridge_resource - Setup base & limit registers of config space.
 * @port: Pointer to a root port
 *
 * @return: None
 *
 * @note: None
 */
static void xaxi_pcie_set_bridge_resource(struct xaxi_pcie_port *port)
{
	const __be32 *ranges = port->ranges;
	int rlen = port->range_len;
	int np = port->pna + 5;
	u32 pci_space;
	unsigned long long pci_addr, size;
	u32 val = 0;

	while ((rlen -= np * 4) >= 0) {
		pci_space = be32_to_cpup(ranges);
		pci_addr = of_read_number(ranges + 1, 2);
		size = of_read_number(ranges + port->pna + 3, 2);

		pr_info("%s:pci_space: 0x%08x pci_addr:0x%016llx size: 0x%016llx\n",
			__func__, pci_space, pci_addr, size);

		ranges += np;

		switch ((pci_space >> 24) & 0x3) {
		case XAXIPCIE_MEM_SPACE:	/* PCI Memory space */
			pr_info("%s:Setting resource in Memory Space\n",
								__func__);
			writel(port->pcie2axibar_0,
					port->header_remap +
						PCIE_CFG_AD1);
			writel(port->pcie2axibar_1,
					port->header_remap +
						PCIE_CFG_AD2);
			break;
		case XAXIPCIE_MEM_SPACE64:	/* PCI 64 bits Memory space */
			pr_info("%s:Setting resource in Prefetchable Memory Space\n",
				__func__);

			val = ((pci_addr >> 16) & 0xfff0) |
					((pci_addr + size - 1) & 0xfff00000);

			writel(val, port->header_remap +
						PCIE_CFG_PREF_MEM);

			val = ((pci_addr >> 32) & 0xffffffff);
			writel(val, port->header_remap +
						PCIE_CFG_PREF_BASE_UPPER);

			val = (((pci_addr + size - 1) >> 32) & 0xffffffff);
			writel(val, port->header_remap +
						PCIE_CFG_PREF_LIMIT_UPPER);
			break;
		}
	}
}

static int xaxi_pcie_hookup_resources(struct xaxi_pcie_port *port,
					struct pci_sys_data *sys)
{
	struct resource *res;
	int i;

	/* Hookup Memory resources */
	for (i = 0; i < 3; ++i) {
		res = &port->mem_resources[i];
		snprintf(port->mem_space_name, sizeof(port->mem_space_name),
			"PCIe %d MEM", port->index);
		port->mem_space_name[sizeof(port->mem_space_name) - 1] = 0;
		res->name = port->mem_space_name;

		if (!res->flags) {
			if (i > 0)
				continue;
			/* Workaround for lack of MEM resource only on 32-bit */
			res->start = port->pci_mem_offset;
			res->end = (resource_size_t)-1LL;
			res->flags = IORESOURCE_MEM;
		}
		if (request_resource(&iomem_resource, res))
			panic("Request PCIe%d Memory resource failed\n",
					port->index);
		pci_add_resource_offset(&sys->resources,
				res, port->pci_mem_offset);

		pr_info("PCI: PHB MEM resource %d = %016llx-%016llx [%lx]\n",
			i, (unsigned long long)res->start,
			(unsigned long long)res->end,
			(unsigned long)res->flags);
	}

	return 0;
}

static void xaxi_pcie_process_bridge_OF_ranges(struct xaxi_pcie_port *port,
					int primary)
{
	/* The address cells of PCIe node */
	int pna = port->pna;
	int np = pna + 5;
	int memno = 0, isa_hole = -1;
	u32 pci_space;
	unsigned long long pci_addr, cpu_addr, pci_next, cpu_next, size;
	unsigned long long isa_mb = 0;
	struct resource *res;
	const __be32 *ranges = port->ranges;
	int rlen = port->range_len;
	struct device_node *node = port->node;

	pr_info("PCI host bridge %s %s ranges:\n",
		node->full_name, primary ? "(primary)" : "");

	/* Parse it */
	pr_debug("Parsing ranges property...\n");
	while ((rlen -= np * 4) >= 0) {
		/* Read next ranges element */
		pci_space = be32_to_cpup(ranges);
		pci_addr = of_read_number(ranges + 1, 2);
		cpu_addr = of_translate_address(node, ranges + 3);
		size = of_read_number(ranges + pna + 3, 2);

		pr_debug("pci_space: 0x%08x pci_addr:0x%016llx\n",
				pci_space, pci_addr);
		pr_debug("cpu_addr:0x%016llx size:0x%016llx\n", cpu_addr, size);

		ranges += np;

		/* If we failed translation or got a zero-sized region
		 * (some FW try to feed us with non sensical zero sized regions
		 * such as power3 which look like some kind of attempt
		 * at exposing the VGA memory hole)
		 */
		if (cpu_addr == OF_BAD_ADDR || size == 0)
			continue;

		/* Now consume following elements while they are contiguous */
		for (; rlen >= np * sizeof(u32);
			ranges += np, rlen -= np * 4) {
			if (be32_to_cpup(ranges) != pci_space)
				break;
			pci_next = of_read_number(ranges + 1, 2);
			cpu_next = of_translate_address(node, ranges + 3);
			if (pci_next != pci_addr + size ||
				cpu_next != cpu_addr + size)
				break;
			size += of_read_number(ranges + pna + 3, 2);
		}

		/* Act based on address space type */
		res = NULL;
		switch ((pci_space >> 24) & 0x3) {
		case XAXIPCIE_MEM_SPACE:	/* PCI Memory space */
		case XAXIPCIE_MEM_SPACE64:	/* PCI 64 bits Memory space */
			pr_info("MEM 0x%016llx..0x%016llx -> 0x%016llx %s\n",
				cpu_addr, cpu_addr + size - 1, pci_addr,
				(pci_space & 0x40000000) ? "Prefetch" : "");

			/* We support only 3 memory ranges */
			if (memno >= 3) {
				pr_info("\\--> Skipped (too many) !\n");
				continue;
			}
			/* Handles ISA memory hole space here */
			if (pci_addr == 0) {
				isa_mb = cpu_addr;
				isa_hole = memno;
				if (primary || isa_mem_base == 0)
					isa_mem_base = cpu_addr;
				port->isa_mem_phys = cpu_addr;
				port->isa_mem_size = size;
			}

			/* We get the PCI/Mem offset from the first range or
			 * the, current one if the offset came from an ISA
			 * hole. If they don't match, bugger.
			 */
			if (memno == 0 ||
				(isa_hole >= 0 && pci_addr != 0 &&
					port->pci_mem_offset == isa_mb))
				port->pci_mem_offset = cpu_addr - pci_addr;
			else if (pci_addr != 0 &&
				port->pci_mem_offset != cpu_addr - pci_addr) {
				pr_info("\\--> Skipped (offset mismatch) !\n");
				continue;
			}

			/* Build resource */
			res = &port->mem_resources[memno++];
			res->flags = IORESOURCE_MEM;
			if (pci_space & 0x40000000)
				res->flags |= IORESOURCE_PREFETCH;
			res->start = cpu_addr;
			break;
		}
		if (res != NULL) {
			res->name = node->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
	}

	/* If there's an ISA hole and the pci_mem_offset is -not- matching
	 * the ISA hole offset, then we need to remove the ISA hole from
	 * the resource list for that brige
	 */
	if (isa_hole >= 0 && port->pci_mem_offset != isa_mb) {
		unsigned int next = isa_hole + 1;
		pr_info("Removing ISA hole at 0x%016llx\n", isa_mb);
		if (next < memno)
			memmove(&port->mem_resources[isa_hole],
				&port->mem_resources[next],
				sizeof(struct resource) * (memno - next));
		port->mem_resources[--memno].flags = 0;
	}
}

static struct pci_ops xaxi_pcie_ops = {
	.read  = xaxi_pcie_read_config,
	.write = xaxi_pcie_write_config,
};

static int xaxi_pcie_setup(int nr, struct pci_sys_data *sys)
{
	u32 val;
	struct xaxi_pcie_port *port = &xaxi_pcie_ports[nr];

	sys->private_data = port;

	/* Get bus range */
	port->first_busno = last_bus_on_record;

	val = readl(port->base_addr_remap + XAXIPCIE_REG_PSCR);
	val = readl(port->header_remap + XAXIPCIE_REG_BIR);
	val = (val >> 16) & 0x7;
	port->last_busno = (((port->reg_base - port->reg_len - 1) >> 20)
						& 0xFF) & val;

	/* Write primary, secondary and subordinate bus numbers */
	val = port->first_busno;
	val |= ((port->first_busno + 1) << 8);
	val |= (port->last_busno << 16);

	writel(val, (port->header_remap + PCIE_CFG_BUS));
	last_bus_on_record = port->last_busno + 1;

	xaxi_pcie_set_bridge_resource(port);

	/* Parse outbound mapping resources */
	xaxi_pcie_process_bridge_OF_ranges(port, PRIMARY_BUS);
	xaxi_pcie_hookup_resources(port, sys);

	return 1;
}

static struct pci_bus __init *xaxi_pcie_scan_bus(int nr,
				struct pci_sys_data *sys)
{
	struct xaxi_pcie_port *port;

	if (nr >= xaxi_pcie_port_cnt)
		return NULL;

	port = &xaxi_pcie_ports[nr];
	port->root_bus_nr = sys->busnr;

	return pci_scan_root_bus(NULL, sys->busnr, &xaxi_pcie_ops, sys,
			&sys->resources);
}

static int xaxi_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_sys_data *sys = dev->sysdata;
	struct xaxi_pcie_port *port = sys->private_data;

	return port->irq_num;
}

/* Interrupt handler */
static irqreturn_t xaxi_pcie_intr_handler(int irq, void *data)
{
	struct xaxi_pcie_port *port = (struct xaxi_pcie_port *)data;
	u32 val = 0, mask = 0;
	u32 status;
	u32 msi_addr = 0;
	u32 msi_data = 0;

	/* Read interrupt decode and mask registers */
	val = readl(port->header_remap + XAXIPCIE_REG_IDR);
	mask = readl(port->header_remap + XAXIPCIE_REG_IMR);

	status = val & mask;
	if (!status)
		return IRQ_NONE;

	if (status & XAXIPCIE_INTR_LINK_DOWN)
		pr_err("Link Down\n");

	if (status & XAXIPCIE_INTR_ECRC_ERR)
		pr_warn("ECRC failed\n");

	if (status & XAXIPCIE_INTR_STR_ERR)
		pr_warn("Streaming error\n");

	if (status & XAXIPCIE_INTR_HOT_RESET)
		pr_info("Hot reset\n");

	if (status & XAXIPCIE_INTR_CFG_TIMEOUT)
		pr_warn("ECAM access timeout\n");

	if (status & XAXIPCIE_INTR_CORRECTABLE) {
		pr_warn("Correctable error message\n");
		val = readl(port->header_remap +
				XAXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			writel(0xFFFFFFFF,
				port->base_addr_remap +
				XAXIPCIE_REG_RPEFR);
			pr_debug("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & XAXIPCIE_INTR_NONFATAL) {
		pr_warn("Non fatal error message\n");
		val = readl((port->header_remap) +
				XAXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			writel(0xFFFFFFFF,
				port->base_addr_remap +
				XAXIPCIE_REG_RPEFR);
			pr_debug("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & XAXIPCIE_INTR_FATAL) {
		pr_warn("Fatal error message\n");
		val = readl(port->header_remap +
				XAXIPCIE_REG_RPEFR);
		if (val & (1 << 18)) {
			writel(0xFFFFFFFF,
				port->base_addr_remap +
				XAXIPCIE_REG_RPEFR);
			pr_debug("Requester ID %d\n", (val & 0xffff));
		}
	}

	if (status & XAXIPCIE_INTR_INTX) {
		/* INTx interrupt received */
		val = readl(port->header_remap + XAXIPCIE_REG_RPIFR1);

		/* Check whether interrupt valid */
		if (!(val & (1 << 31))) {
			pr_warn("RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
		}

		/* Check MSI or INTX */
		if (!(val & (1 << 30))) {
			if (val & (1 << 29))
				pr_debug("INTx assert\n");
			else
				pr_debug("INTx deassert\n");
		}

		/* Clear interrupt FIFO register 1 */
		writel(0xFFFFFFFF,
			port->base_addr_remap + XAXIPCIE_REG_RPIFR1);
	}

	if (status & XAXIPCIE_INTR_MSI) {
		/* MSI Interrupt */
		val = readl(port->header_remap + XAXIPCIE_REG_RPIFR1);

		if (!(val & (1 << 31))) {
			pr_warn("RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
		}

		if (val & (1 << 30)) {
			msi_addr = (val >> 16) & 0x7FF;
			msi_data = readl(port->header_remap +
					XAXIPCIE_REG_RPIFR2) & 0xFFFF;
			pr_debug("%s: msi_addr %08x msi_data %08x\n",
					__func__, msi_addr, msi_data);
		}

		/* Clear interrupt FIFO register 1 */
		writel(0xFFFFFFFF,
			port->base_addr_remap + XAXIPCIE_REG_RPIFR1);
#ifdef CONFIG_PCI_MSI
		/* Handle MSI Interrupt */
		if (msi_data >= xaxipcie_msi_irq_base)
			generic_handle_irq(msi_data);
#endif
	}

	if (status & XAXIPCIE_INTR_SLV_UNSUPP)
		pr_warn("Slave unsupported request\n");

	if (status & XAXIPCIE_INTR_SLV_UNEXP)
		pr_warn("Slave unexpected completion\n");

	if (status & XAXIPCIE_INTR_SLV_COMPL)
		pr_warn("Slave completion timeout\n");

	if (status & XAXIPCIE_INTR_SLV_ERRP)
		pr_warn("Slave Error Poison\n");

	if (status & XAXIPCIE_INTR_SLV_CMPABT)
		pr_warn("Slave Completer Abort\n");

	if (status & XAXIPCIE_INTR_SLV_ILLBUR)
		pr_warn("Slave Illegal Burst\n");

	if (status & XAXIPCIE_INTR_MST_DECERR)
		pr_warn("Master decode error\n");

	if (status & XAXIPCIE_INTR_MST_SLVERR)
		pr_warn("Master slave error\n");

	if (status & XAXIPCIE_INTR_MST_ERRP)
		pr_warn("Master error poison\n");

	/* Clear the Interrupt Decode register */
	writel(status, port->base_addr_remap + XAXIPCIE_REG_IDR);

	return IRQ_HANDLED;
}

/**
 * xaxi_pcie_init_port - Initialize hardware
 * @port: A pointer to a pcie port that needs to be initialized
 *
 * @return: Error / no error
 *
 * @note: None
 */
static int xaxi_pcie_init_port(struct xaxi_pcie_port *port)
{
	void __iomem *base_addr_remap = NULL;
	int err = 0;
#ifdef CONFIG_PCI_MSI
	unsigned long xaxipcie_msg_addr;
#endif

	base_addr_remap = ioremap(port->reg_base, port->reg_len);
	if (!base_addr_remap)
		return -ENOMEM;

	port->base_addr_remap = base_addr_remap;

	/* make sure it is root port before touching header */
	if (port->type) {
		port->header_remap = base_addr_remap;
		writel(BUS_MASTER_ENABLE,
			port->base_addr_remap + PCIE_CFG_CMD);
	}

#ifdef CONFIG_PCI_MSI
	xaxipcie_msg_addr = port->reg_base & ~0xFFF;	/* 4KB aligned */
	writel(0x0, port->base_addr_remap +
				XAXIPCIE_REG_MSIBASE1);

	writel(xaxipcie_msg_addr, port->base_addr_remap +
				XAXIPCIE_REG_MSIBASE2);

	xaxipcie_msi_irq_base = xaxipcie_alloc_msi_irqdescs(port->node,
					xaxipcie_msg_addr);
	if (xaxipcie_msi_irq_base < 0) {
		pr_err("%s: Couldn't allocate MSI IRQ numbers\n",
					 __func__);
		return -ENODEV;
	}
#endif

	port->link_up = is_link_up(port->base_addr_remap);
	if (!port->link_up)
		pr_info("%s: LINK IS DOWN\n", __func__);
	else
		pr_info("%s: LINK IS UP\n", __func__);

	/* Disable all interrupts*/
	writel(~XAXIPCIE_REG_IDR_MASKALL,
		port->base_addr_remap + XAXIPCIE_REG_IMR);

	/* Clear pending interrupts*/
	writel(readl(port->base_addr_remap + XAXIPCIE_REG_IDR) &
			XAXIPCIE_REG_IMR_MASKALL,
			port->base_addr_remap + XAXIPCIE_REG_IDR);

	/* Enable all interrupts*/
	writel(XAXIPCIE_REG_IMR_MASKALL,
			port->base_addr_remap + XAXIPCIE_REG_IMR);

	/*
	 * Bridge enable must be done after enumeration,
	 * but there is no callback defined
	 */
	bridge_enable(port->base_addr_remap);

	/* Register Interrupt Handler */
	err = request_irq(port->irq_num, xaxi_pcie_intr_handler,
					IRQF_SHARED, "zynqpcie", port);
	if (err) {
		pr_err("%s: Could not allocate interrupt\n", __func__);
		return err;
	}

	return 0;
}

static struct xaxi_pcie_port *
xaxi_pcie_instantiate_port_info(struct xaxi_pcie_of_config *config,
					struct device_node *node)
{
	struct xaxi_pcie_port *port;
	int port_num;

	port_num = config->device_id;
	port = &xaxi_pcie_ports[port_num];
	port->node = of_node_get(node);
	port->index = port_num;
	port->type = config->device_type;
	port->reg_base = config->reg_base;
	port->reg_len = config->reg_len;
	port->bars_num  = config->bars_num;
	port->irq_num   = config->irq_num;
	port->header_addr = port->reg_base + XAXIPCIE_LOCAL_CNFG_BASE;
	port->pcie2axibar_0 = config->pcie2axibar_0;
	port->pcie2axibar_1 = config->pcie2axibar_1;
	port->ranges = config->ranges;
	port->range_len = config->range_len;
	port->pna = config->address_cells;

	return port;
}

/**
 * xaxi_get_pcie_of_config - Read info from device tree
 * @node: A pointer to device node to read from
 * @info: A pointer to xilinx_pcie_node struct to write device tree
 *	info into to.
 *
 * @return: Error / no error
 *
 * @note: Read related info from device tree
 */
static int xaxi_pcie_get_of_config(struct device_node *node,
		struct xaxi_pcie_of_config *info)
{
	const __be32 *value;
	int rlen;

	info->num_instances = 1;

	value = of_get_property(node, "xlnx,device-num", &rlen);

	info->device_id = 0;

	value = of_get_property(node, "xlnx,include-rc", &rlen);
	if (value)
		info->device_type = be32_to_cpup(value);
	else
		return -ENODEV;

	value = of_get_property(node, "reg", &rlen);
	if (value) {
		info->reg_base =
			be32_to_cpup(value);
		info->reg_len =
			be32_to_cpup(value + 1);
	} else
		return -ENODEV;

	value = of_get_property(node, "xlnx,pciebar-num", &rlen);
	if (value)
		info->bars_num = be32_to_cpup(value);
	else
		return -ENODEV;

	info->irq_num = irq_of_parse_and_map(node, 0);

	/* Get address translation parameters */
	value = of_get_property(node, "xlnx,pciebar2axibar-0", &rlen);
	if (value) {
		info->pcie2axibar_0 =
			be32_to_cpup(value);
	} else
		return -ENODEV;

	value = of_get_property(node, "xlnx,pciebar2axibar-1", &rlen);
	if (value) {
		info->pcie2axibar_1 =
			be32_to_cpup(value);
	} else
		return -ENODEV;

	/* The address cells of PCIe node */
	info->address_cells = of_n_addr_cells(node);

	/* Get ranges property */
	value = of_get_property(node, "ranges", &rlen);
	if (value) {
		info->ranges = value;
		info->range_len = rlen;
	} else
		return -ENODEV;

	return 0;
}

static int __init xaxi_pcie_of_probe(struct device_node *node)
{
	int err = 0;
	struct xaxi_pcie_of_config config;
	struct xaxi_pcie_port *port;

	err = xaxi_pcie_get_of_config(node, &config);
	if (err) {
		pr_err("%s: Invalid Configuration\n", __func__);
		return err;
	}

	if (!xaxi_pcie_port_cnt) {
		xaxi_pcie_port_cnt = config.num_instances;

		if (xaxi_pcie_port_cnt) {
			xaxi_pcie_ports = (struct xaxi_pcie_port *)
				kzalloc(xaxi_pcie_port_cnt *
				sizeof(struct xaxi_pcie_port), GFP_KERNEL);

			if (!xaxi_pcie_ports) {
				pr_err("%s: Memory allocation failed\n",
					__func__);
				return -ENOMEM;
			}
		} else /* not suppose to be here
			* when we don't have pcie ports */
			return -ENODEV;
	}

	port = xaxi_pcie_instantiate_port_info(&config, node);
	err = xaxi_pcie_init_port(port);
	if (err) {
		pr_err("%s: Port Initalization failed\n", __func__);
		return err;
	}

	return err;
}

static struct of_device_id xaxi_pcie_match[] = {
	{ .compatible = "xlnx,axi-pcie-1.05.a" ,},
	{}
};

static struct hw_pci xaxi_pcie_hw __initdata = {
	.nr_controllers = 1,
	.setup          = xaxi_pcie_setup,
	.scan           = xaxi_pcie_scan_bus,
	.map_irq        = xaxi_pcie_map_irq,
};

static int __init xaxi_pcie_init(void)
{
	int err;
	int init = 0;
	struct device_node *node;

	for_each_matching_node(node, xaxi_pcie_match) {
		err = xaxi_pcie_of_probe(node);
		if (err) {
			pr_err("%s: Root Port Probe failed\n", __func__);

			return err;
		}
		pr_info("AXI PCIe Root Port Probe Successful\n");
		init++;
	}

	if (init)
		pci_common_init(&xaxi_pcie_hw);

	return 0;
}

subsys_initcall(xaxi_pcie_init);
