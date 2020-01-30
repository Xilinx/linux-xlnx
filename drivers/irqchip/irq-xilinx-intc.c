/*
 * Copyright (C) 2007-2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012-2013 Xilinx, Inc.
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/jump_label.h>
#include <linux/bug.h>
#include <linux/of_irq.h>
#include <linux/cpuhotplug.h>

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

struct xintc_irq_chip {
	void		__iomem *base;
	struct		irq_domain *domain;
	u32		intr_mask;
	struct			irq_chip *intc_dev;
	u32				nr_irq;
	u32				sw_irq;
};

static DEFINE_STATIC_KEY_FALSE(xintc_is_be);

static DEFINE_PER_CPU(struct xintc_irq_chip, primary_intc);

static void xintc_write(struct xintc_irq_chip *irqc, int reg, u32 data)
{
	if (static_branch_unlikely(&xintc_is_be))
		iowrite32be(data, irqc->base + reg);
	else
		iowrite32(data, irqc->base + reg);
}

static unsigned int xintc_read(struct xintc_irq_chip *irqc, int reg)
{
	if (static_branch_unlikely(&xintc_is_be))
		return ioread32be(irqc->base + reg);
	else
		return ioread32(irqc->base + reg);
}

static void intc_enable_or_unmask(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;
	struct xintc_irq_chip *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("irq-xilinx: enable_or_unmask: %ld\n", d->hwirq);

	/* ack level irqs because they can't be acked during
	 * ack function since the handle_level_irq function
	 * acks the irq before calling the interrupt handler
	 */
	if (irqd_is_level_type(d))
		xintc_write(local_intc, IAR, mask);

	xintc_write(local_intc, SIE, mask);
}

static void intc_disable_or_mask(struct irq_data *d)
{
	struct xintc_irq_chip *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("irq-xilinx: disable: %ld\n", d->hwirq);
	xintc_write(local_intc, CIE, 1 << d->hwirq);
}

static void intc_ack(struct irq_data *d)
{
	struct xintc_irq_chip *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("irq-xilinx: ack: %ld\n", d->hwirq);
	xintc_write(local_intc, IAR, 1 << d->hwirq);
}

static void intc_mask_ack(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;
	struct xintc_irq_chip *local_intc = irq_data_get_irq_chip_data(d);

	pr_debug("irq-xilinx: disable_and_ack: %ld\n", d->hwirq);
	xintc_write(local_intc, CIE, mask);
	xintc_write(local_intc, IAR, mask);
}

static int xintc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	struct xintc_irq_chip *local_intc = d->host_data;

	if (local_intc->intr_mask & (1 << hw)) {
		irq_set_chip_and_handler_name(irq, local_intc->intc_dev,
						handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else {
		irq_set_chip_and_handler_name(irq, local_intc->intc_dev,
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

static void xil_intc_initial_setup(struct xintc_irq_chip *irqc)
{
	int i;
	u32 mask;

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	xintc_write(irqc, IER, 0);

	/* Acknowledge any pending interrupts just in case. */
	xintc_write(irqc, IAR, 0xffffffff);

	/* Turn on the Master Enable. */
	xintc_write(irqc, MER, MER_HIE | MER_ME);
	if (!(xintc_read(irqc, MER) & (MER_HIE | MER_ME))) {
		static_branch_enable(&xintc_is_be);
		xintc_write(irqc, MER, MER_HIE | MER_ME);
	}

	/* Enable all SW IRQs */
	for (i = 0; i < irqc->sw_irq; i++) {
		mask = 1 << (i + irqc->nr_irq);
		xintc_write(irqc, IAR, mask);
		xintc_write(irqc, SIE, mask);
	}
}

static void xil_intc_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct xintc_irq_chip *irqc =
		irq_data_get_irq_handler_data(&desc->irq_data);

	chained_irq_enter(chip, desc);

	do {
		u32 hwirq = xintc_read(irqc, IVR);

		if (hwirq == -1U)
			break;

		generic_handle_domain_irq(irqc->domain, hwirq);
	} while (true);
	chained_irq_exit(chip, desc);
}

static int xil_intc_start(unsigned int cpu)
{
	struct xintc_irq_chip *irqc = per_cpu_ptr(&primary_intc, cpu);

	pr_debug("%s: intc cpu %d\n", __func__, cpu);

	xil_intc_initial_setup(irqc);

	return 0;
}

static int xil_intc_stop(unsigned int cpu)
{
	pr_debug("%s: intc cpu %d\n", __func__, cpu);

	return 0;
}

static void xil_intc_handle_irq(struct pt_regs *regs)
{
	int ret;
	unsigned int hwirq, cpu_id = smp_processor_id();
	struct xintc_irq_chip *irqc = per_cpu_ptr(&primary_intc, cpu_id);

	do {
		hwirq = xintc_read(irqc, IVR);
		if (hwirq != -1U) {
			if (hwirq >= irqc->nr_irq) {
				WARN_ONCE(1, "SW interrupt not handled\n");
				/* ACK is necessary */
				xintc_write(irqc, IAR, 1 << hwirq);
				continue;
			} else {
				ret = handle_domain_irq(irqc->domain,
							hwirq, regs);
				WARN_ONCE(ret, "cpu %d: Unhandled HWIRQ %d\n",
					  cpu_id, hwirq);
				continue;
			}
		}

		break;
	} while (1);
}

static int __init xilinx_intc_of_init(struct device_node *intc,
					     struct device_node *parent)
{
	int ret, irq;
	struct xintc_irq_chip *irqc;
	struct irq_chip *intc_dev;
	u32 cpu_id = 0;

	ret = of_property_read_u32(intc, "cpu-id", &cpu_id);
	if (ret < 0)
		pr_err("%s: %pOF: cpu_id not found\n", __func__, intc);

	/* No parent means it is primary intc */
	if (!parent) {
		irqc = per_cpu_ptr(&primary_intc, cpu_id);
		if (irqc->base) {
			pr_err("%pOF: %s: cpu %d has already irq controller\n",
				intc, __func__, cpu_id);
			return -EINVAL;
		}
	} else {
		irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
		if (!irqc)
			return -ENOMEM;
	}

	irqc->base = of_iomap(intc, 0);
	BUG_ON(!irqc->base);

	ret = of_property_read_u32(intc, "xlnx,num-intr-inputs", &irqc->nr_irq);
	if (ret < 0) {
		pr_err("irq-xilinx: unable to read xlnx,num-intr-inputs\n");
		goto error;
	}

	ret = of_property_read_u32(intc, "xlnx,kind-of-intr", &irqc->intr_mask);
	if (ret < 0) {
		pr_warn("irq-xilinx: unable to read xlnx,kind-of-intr\n");
		irqc->intr_mask = 0;
	}

	if (irqc->intr_mask >> irqc->nr_irq)
		pr_warn("irq-xilinx: mismatch in kind-of-intr param\n");

	/* sw irqs are optinal */
	of_property_read_u32(intc, "xlnx,num-sw-intr", &irqc->sw_irq);

	pr_info("irq-xilinx: %pOF: num_irq=%d, sw_irq=%d, edge=0x%x\n",
		intc, irqc->nr_irq, irqc->sw_irq, irqc->intr_mask);

	/* Right now enable only SW IRQs on that IP and wait */
	if (cpu_id) {
		xil_intc_initial_setup(irqc);
		return 0;
	}

	intc_dev = kzalloc(sizeof(*intc_dev), GFP_KERNEL);
	if (!intc_dev) {
		ret = -ENOMEM;
		goto error;
	}

	intc_dev->name = intc->full_name;
	intc_dev->irq_unmask = intc_enable_or_unmask,
	intc_dev->irq_mask = intc_disable_or_mask,
	intc_dev->irq_ack = intc_ack,
	intc_dev->irq_mask_ack = intc_mask_ack,
	irqc->intc_dev = intc_dev;

	irqc->domain = irq_domain_add_linear(intc, irqc->nr_irq,
						  &xintc_irq_domain_ops, irqc);
	if (!irqc->domain) {
		pr_err("irq-xilinx: Unable to create IRQ domain\n");
		ret = -EINVAL;
		goto err_alloc;
	}

	if (parent) {
		irq = irq_of_parse_and_map(intc, 0);
		if (irq) {
			irq_set_chained_handler_and_data(irq,
							 xil_intc_irq_handler,
							 irqc);
		} else {
			pr_err("irq-xilinx: interrupts property not in DT\n");
			ret = -EINVAL;
			goto err_alloc;
		}
		xil_intc_initial_setup(irqc);
		return 0;
	}

	irq_set_default_host(irqc->domain);
	set_handle_irq(xil_intc_handle_irq);

	ret = cpuhp_setup_state(CPUHP_AP_IRQ_XILINX_STARTING,
				"microblaze/arch_intc:starting",
				xil_intc_start, xil_intc_stop);

	return ret;

err_alloc:
	kfree(intc_dev);
error:
	iounmap(irqc->base);
	if (parent)
		kfree(irqc);
	return ret;

}

IRQCHIP_DECLARE(xilinx_intc_xps, "xlnx,xps-intc-1.00.a", xilinx_intc_of_init);
IRQCHIP_DECLARE(xilinx_intc_opb, "xlnx,opb-intc-1.00.c", xilinx_intc_of_init);
