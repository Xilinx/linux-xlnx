/*
 * Xilinx PCIe IP hardware MSI initialisation
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

#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include "xilinx_axipcie.h"

static DECLARE_BITMAP(msi_irq_in_use, XILINX_NUM_MSI_IRQS);

/*
 * Dynamic irq allocate and deallocation
 */

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

	irq = IRQ_XILINX_MSI_0 + pos;
	if (irq > NR_IRQS)
		return -ENOSPC;

	/* test_and_set_bit operates on 32-bits at a time */
	if (test_and_set_bit(pos, msi_irq_in_use))
		goto again;

	dynamic_irq_init(irq);

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
	int pos = irq - IRQ_XILINX_MSI_0;

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
		.name = "PCI-MSI",
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
	msg.address_lo = msg_addr;
	msg.data = irq;

	DBG("irq %d addr_hi %08x low %08x data %08x\n",
			irq, msg.address_hi, msg.address_lo, msg.data);

	write_msi_msg(irq, &msg);

	irq_set_chip_and_handler(irq, &xilinx_msi_chip, handle_simple_irq);

	return 0;
}
