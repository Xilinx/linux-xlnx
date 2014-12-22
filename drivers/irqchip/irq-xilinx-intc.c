/*
 * Copyright (C) 2007-2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012-2014 Xilinx, Inc.
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/slab.h>
#include "irqchip.h"

/**
 * struct intc - Interrupt controller private data structure
 * @baseaddr:	Base address of the interrupt controller device
 * @nr_irq:	Number of interrrupts supported by the device
 * @intr_mask:	Type of each interrupt, level or edge
 * @domain:	The interrupt domain for the device
 * @read_fn:	The read function for device registers
 * @write_fn:	The write function for device registers
 */
struct intc {
	void __iomem *baseaddr;
	u32 nr_irq;
	u32 intr_mask;
	struct irq_domain *domain;
	unsigned int (*read_fn)(void __iomem *);
	void (*write_fn)(u32, void __iomem *);
};

/* No one else should require these constants, so define them locally here. */
#define ISR 0x00			/* Interrupt Status Register */
#define IPR 0x04			/* Interrupt Pending Register */
#define IER 0x08			/* Interrupt Enable Register */
#define IAR 0x0c			/* Interrupt Acknowledge Register */
#define SIE 0x10			/* Set Interrupt Enable bits */
#define CIE 0x14			/* Clear Interrupt Enable bits */
#define IVR 0x18			/* Interrupt Vector Register */
#define MER 0x1c			/* Master Enable Register */

#define MER_ME (1<<0)
#define MER_HIE (1<<1)

static void intc_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int intc_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void intc_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int intc_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

static void intc_enable_or_unmask(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;
	struct intc *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("enable_or_unmask: %ld\n", d->hwirq);

	/*
	 * ack level irqs because they can't be acked during
	 * ack function since the handle_level_irq function
	 * acks the irq before calling the interrupt handler
	 */
	if (irqd_is_level_type(d))
		local_intc->write_fn(mask, local_intc->baseaddr + IAR);

	local_intc->write_fn(mask, local_intc->baseaddr + SIE);
}

static void intc_disable_or_mask(struct irq_data *d)
{
	struct intc *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("disable: %ld\n", d->hwirq);
	local_intc->write_fn(1 << d->hwirq, local_intc->baseaddr + CIE);
}

static void intc_ack(struct irq_data *d)
{
	struct intc *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("ack: %ld\n", d->hwirq);
	local_intc->write_fn(1 << d->hwirq, local_intc->baseaddr + IAR);
}

static void intc_mask_ack(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;
	struct intc *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("disable_and_ack: %ld\n", d->hwirq);
	local_intc->write_fn(mask, local_intc->baseaddr + CIE);
	local_intc->write_fn(mask, local_intc->baseaddr + IAR);
}

static struct irq_chip intc_dev = {
	.name = "Xilinx INTC",
	.irq_unmask = intc_enable_or_unmask,
	.irq_mask = intc_disable_or_mask,
	.irq_ack = intc_ack,
	.irq_mask_ack = intc_mask_ack,
};

static unsigned int get_irq(struct intc *local_intc)
{
	unsigned int hwirq, irq = -1;

	hwirq = local_intc->read_fn(local_intc->baseaddr + IVR);
	if (hwirq != -1U)
		irq = irq_find_mapping(local_intc->domain, hwirq);

	pr_debug("get_irq: hwirq=%d, irq=%d\n", hwirq, irq);

	return irq;
}

static int xintc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	struct intc *local_intc = d->host_data;

	if (local_intc->intr_mask & (1 << hw)) {
		irq_set_chip_and_handler_name(irq, &intc_dev,
						handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else {
		irq_set_chip_and_handler_name(irq, &intc_dev,
						handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	}

	irq_set_chip_data(irq, local_intc);
	return 0;
}

static const struct irq_domain_ops xintc_irq_domain_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = xintc_map,
};

static void intc_handler(u32 irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct intc *local_intc =
		irq_data_get_irq_handler_data(&desc->irq_data);

	pr_debug("intc_handler: input irq = %d\n", desc->irq_data.irq);
	chained_irq_enter(chip, desc);

	/*
	 * ignore the irq input as we now need to query the AXI interrupt
	 * controller to see which interrupt is active
	 */
	irq = get_irq(local_intc);
	while (irq != -1U) {
		generic_handle_irq(irq);
		irq = get_irq(local_intc);
	}

	chained_irq_exit(chip, desc);
}

static int __init xilinx_intc_of_init(struct device_node *node,
					     struct device_node *parent)
{
	u32 irq;
	int ret;
	struct intc *intc;

	intc = kzalloc(sizeof(struct intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	intc->baseaddr = of_iomap(node, 0);
	if (!intc->baseaddr) {
		pr_err("%s: unable to map memory\n", node->full_name);
		ret = -ENOMEM;
		goto error1;
	}

	ret = of_property_read_u32(node, "xlnx,num-intr-inputs", &intc->nr_irq);
	if (ret < 0) {
		pr_err("%s: unable to read xlnx,num-intr-inputs\n",
			node->full_name);
		goto error2;
	}

	ret = of_property_read_u32(node, "xlnx,kind-of-intr", &intc->intr_mask);
	if (ret < 0) {
		pr_err("%s: unable to read xlnx,kind-of-intr\n",
			node->full_name);
		goto error2;
	}

	if (intc->intr_mask >> intc->nr_irq)
		pr_info(" ERROR: Mismatch in kind-of-intr param\n");

	pr_info("%s: num_irq=%d, edge=0x%x\n",
		node->full_name, intc->nr_irq, intc->intr_mask);

	intc->write_fn = intc_write32;
	intc->read_fn = intc_read32;

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	intc->write_fn(0, intc->baseaddr + IER);

	/* Acknowledge any pending interrupts just in case. */
	intc->write_fn(0xffffffff, intc->baseaddr + IAR);

	/*
	 * Turn on the Master Enable and then set up the driver for
	 * a big-endian processor if big-endian is detected.
	 */
	intc->write_fn(MER_HIE | MER_ME, intc->baseaddr + MER);
	if (!(intc->read_fn(intc->baseaddr + MER) & (MER_HIE | MER_ME))) {
		intc->write_fn = intc_write32_be;
		intc->read_fn = intc_read32_be;
		intc->write_fn(MER_HIE | MER_ME, intc->baseaddr + MER);
	}

	intc->domain = irq_domain_add_linear(node, intc->nr_irq,
				&xintc_irq_domain_ops, intc);
	irq_set_default_host(intc->domain);

	/*
	 * Check if this interrupt controller is a chained interrupt controller
	 * and if so then set up the handler and enable it.
	 */
	irq = irq_of_parse_and_map(node, 0);
	if (irq > 0) {
		pr_info("%s: chained intc connected to irq %d\n",
			 node->full_name, irq);
		irq_set_handler(irq, intc_handler);
		irq_set_handler_data(irq, intc);
		enable_irq(irq);
	};

	return 0;

error2:
	iounmap(intc->baseaddr);

error1:
	kfree(intc);
	return ret;
}

IRQCHIP_DECLARE(xilinx_intc, "xlnx,xps-intc-1.00.a", xilinx_intc_of_init);
