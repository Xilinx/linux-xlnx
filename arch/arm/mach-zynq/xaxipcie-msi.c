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

static DECLARE_BITMAP(msi_irq_in_use, XILINX_NUM_MSI_IRQS);

static unsigned long xaxipcie_msg_addr;
static struct irq_domain *xaxipcie_irq_domain;
static int xaxipcie_msi_irq_base;

/* Dynamic irq allocate and deallocation */

/**
 * create_irq- Dynamic irq allocate
 * void
 *
 * @return: Interrupt number allocated/ error
 *
 * @note: None
 */
int create_irq(void)
{
	int irq, pos;
again:
	pos = find_first_zero_bit(msi_irq_in_use, XILINX_NUM_MSI_IRQS);

	irq = irq_find_mapping(xaxipcie_irq_domain, pos);

	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, msi_irq_in_use))
		goto again;

	dynamic_irq_init(irq);
	set_irq_flags(irq, IRQF_VALID);

	return irq;
}

/**
 * destroy_irq- Dynamic irq de-allocate
 * @irq: Interrupt number to de-allocate
 *
 * @return: None
 *
 * @note: None
 */
void destroy_irq(unsigned int irq)
{
	int pos = irq - xaxipcie_msi_irq_base;

	dynamic_irq_cleanup(irq);

	clear_bit(pos, msi_irq_in_use);
}

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
	destroy_irq(irq);
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
	int irq = create_irq();
	struct msi_msg msg;

	if (irq < 0)
		return irq;

	irq_set_msi_desc(irq, desc);

	msg.address_hi = 0x00000000;
	msg.address_lo = xaxipcie_msg_addr;
	msg.data = irq;

	pr_debug("irq %d addr_hi %08x low %08x data %08x\n",
			irq, msg.address_hi, msg.address_lo, msg.data);

	write_msi_msg(irq, &msg);

	irq_set_chip_and_handler(irq, &xilinx_msi_chip, handle_simple_irq);

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
