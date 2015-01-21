/*
 * Xilinx PCIe IP hardware MSI initialisation
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
 *
 */

#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>

#define XILINX_NUM_MSI_IRQS	128

static unsigned long xaxipcie_msg_addr;
static struct irq_domain *xaxipcie_irq_domain;
static int xaxipcie_msi_irq_base;
int xaxipcie_alloc_msi_irqdescs(struct device_node *node,
					unsigned long msg_addr);

static DECLARE_BITMAP(xaxipcie_used_msi, XILINX_NUM_MSI_IRQS);

/**
 * arch_teardown_msi_irq-Teardown the Interrupt
 * @irq: Interrupt number to teardown
 *
 * @return: None
 *
 * @note: This function  is called when pci_disable_msi is called
 */
void arch_teardown_msi_irq(unsigned int irq)
{
	if ((irq >= xaxipcie_msi_irq_base) &&
			(irq < (xaxipcie_msi_irq_base + XILINX_NUM_MSI_IRQS))) {

		clear_bit(irq - xaxipcie_msi_irq_base, xaxipcie_used_msi);
		irq_free_desc(irq);
	} else {
		pr_err(
		"Teardown MSI irq, not in AXI PCIE irq space? irq=%u\n", irq);
	}
}

/**
 * xilinx_msi_nop-No operation handler
 * @irq: Interrupt number
 *
 * @return: None
 *
 * @note: None
 */
static void xilinx_msi_nop(struct irq_data *d)
{
	return;
}

static struct irq_chip xilinx_msi_chip = {
		.name = "PCIe-MSI",
		.irq_ack = xilinx_msi_nop,
		.irq_enable = unmask_msi_irq,
		.irq_disable = mask_msi_irq,
		.irq_mask = mask_msi_irq,
		.irq_unmask = unmask_msi_irq,
};

/**
 * arch_setup_msi_irq-Setup MSI interrupt
 * @pdev: Pointer to current pci device structure
 * @desc: Pointer to MSI description structure
 *
 * @return: Error/ no-error
 *
 * @note: This function  is called when pci_enable_msi is called
 */
int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	struct msi_msg msg;
	int virq;
	int irq = 0;

	while (irq < XILINX_NUM_MSI_IRQS) {
		if (!test_and_set_bit(irq, xaxipcie_used_msi))
			break;
		irq++;
	}

	if (irq >= XILINX_NUM_MSI_IRQS)
		return -ENOSPC;

	virq = irq_create_mapping(xaxipcie_irq_domain, irq);

	if (virq <= 0) {
		clear_bit(irq, xaxipcie_used_msi);
		return -ENOSPC;
	}

	irq_set_msi_desc(virq, desc);

	msg.address_hi = 0x00000000;
	msg.address_lo = xaxipcie_msg_addr;
	msg.data = virq;

	pr_debug("virq %d addr_hi %08x low %08x data %08x\n",
			virq, msg.address_hi, msg.address_lo, msg.data);

	write_msi_msg(virq, &msg);

	irq_set_chip_and_handler(virq, &xilinx_msi_chip, handle_simple_irq);

	return 0;
}

/**
 * xaxipcie_alloc_msi_irqdescs - allocate msi irq descs
 * @node: Pointer to device node structure
 * @msg_addr: PCIe MSI message address
 *
 * @return: Allocated MSI IRQ Base/ error
 *
 * @note: This function is called when xaxipcie_init_port() is called
 */
int xaxipcie_alloc_msi_irqdescs(struct device_node *node,
					unsigned long msg_addr)
{
	/* Store the PCIe MSI message address */
	xaxipcie_msg_addr = msg_addr;

	/* Allocate MSI IRQ descriptors */
	xaxipcie_msi_irq_base = irq_alloc_descs(-1, 0,
					XILINX_NUM_MSI_IRQS, 0);

	if (xaxipcie_msi_irq_base < 0)
		return -ENODEV;

	/* Register IRQ domain */
	xaxipcie_irq_domain = irq_domain_add_legacy(node,
				XILINX_NUM_MSI_IRQS,
				xaxipcie_msi_irq_base,
				0, &irq_domain_simple_ops, NULL);

	if (!xaxipcie_irq_domain)
		return -ENOMEM;

	return xaxipcie_msi_irq_base;
}
EXPORT_SYMBOL(xaxipcie_alloc_msi_irqdescs);
