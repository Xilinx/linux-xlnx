/*
 * arch/microblaze/kernel/opb_intc.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/xparameters.h>

/* No one else should require these constants, so define them locally here. */
#define ISR 0x00			/* Interrupt Status Register */
#define IPR 0x04			/* Interrupt Pending Register */
#define IER 0x08			/* Interrupt Enable Register */
#define IAR 0x0c			/* Interrupt Acknowledge Register */
#define SIE 0x10			/* Set Interrupt Enable bits */
#define CIE 0x14			/* Clear Interrupt Enable bits */
#define IVR 0x18			/* Interrupt Vector Register */
#define MER 0x1c			/* Master Enable Register */

#define MER_ME  (1<<0)
#define MER_HIE (1<<1)

#define BASE_ADDR XPAR_INTC_0_BASEADDR

static void opb_intc_enable(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("enable: %d\n", irq);
	iowrite32(mask, BASE_ADDR + SIE);
}

static void opb_intc_disable(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("disable: %d\n", irq);
	iowrite32(mask, BASE_ADDR + CIE);
}

static void opb_intc_disable_and_ack(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));
	pr_debug("disable_and_ack: %d\n", irq);
	iowrite32(mask, BASE_ADDR + CIE);
	if (!(irq_desc[irq].status & IRQ_LEVEL))
		iowrite32(mask, BASE_ADDR + IAR);	/* ack edge triggered intr */
}

static void opb_intc_end(unsigned int irq)
{
	unsigned long mask = (0x00000001 << (irq & 31));

	pr_debug("end: %d\n", irq);
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		iowrite32(mask, BASE_ADDR + SIE);
		/* ack level sensitive intr */
		if (irq_desc[irq].status & IRQ_LEVEL)
			iowrite32(mask, BASE_ADDR + IAR);
	}
}

static struct irq_chip obp_intc = {
	.name     = "OPB Interrupt Controller",
	.enable	  = opb_intc_enable,
	.disable  = opb_intc_disable,
	.ack	  = opb_intc_disable_and_ack,
	.end	  = opb_intc_end,
};

unsigned int get_irq(struct pt_regs *regs)
{
	int irq;

	/*
	 * NOTE: This function is the one that needs to be improved in
	 * order to handle multiple interrupt controllers.  It currently
	 * is hardcoded to check for interrupts only on the first INTC.
	 */

	irq = ioread32(BASE_ADDR + IVR);

	/* If no interrupt is pending then all bits of the IVR are set to 1. As
	 * the IVR is as many bits wide as numbers of inputs are available.
	 * Therefore, if all bits of the IVR are set to one, its content will
	 * be bigger than XPAR_INTC_MAX_NUM_INTR_INPUTS.
	 */
	if (irq >= XPAR_INTC_MAX_NUM_INTR_INPUTS)
		irq = -1;	/* report no pending interrupt. */

	pr_debug("get_irq: %d\n", irq);

	return irq;
}

void __init init_IRQ(void)
{
	int i;

	printk(KERN_INFO "OPB INTC #0 at 0x%08lX\n",
	       (unsigned long) BASE_ADDR);

	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	iowrite32(0, BASE_ADDR + IER);

	/* Acknowledge any pending interrupts just in case. */
	iowrite32(0xffffffff, BASE_ADDR + IAR);

	/* Turn on the Master Enable. */
	iowrite32(MER_HIE|MER_ME, BASE_ADDR + MER);

	for (i = 0; i < NR_IRQS; ++i) {
		irq_desc[i].chip = &obp_intc;

		if (XPAR_INTC_0_KIND_OF_INTR & (0x00000001 << i))
			irq_desc[i].status &= ~IRQ_LEVEL;
		else
			irq_desc[i].status |= IRQ_LEVEL;
	}
}

void irq_early_init(void)
{
	iowrite32(0, BASE_ADDR + IER);
}

