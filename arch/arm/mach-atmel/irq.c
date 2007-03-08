/*
 *  linux/arch/armnommu/mach-atmel/irq.c
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *	      Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

 /* Internal Sources */
#define LevelSensitive	(0<<5)
#define EdgeTriggered	(1<<5)

 /* External Sources */
#define LowLevel	(0<<5)
#define NegativeEdge	(1<<5)
#define HighLevel	(2<<5)
#define PositiveEdge	(3<<5)

static unsigned char eb01_irq_prtable[32] = {
	7, /* FIQ */
	0, /* SWIRQ */
	0, /* US0IRQ */
	0, /* US1IRQ */
	2, /* TC0IRQ */
	2, /* TC1IRQ */
	2, /* TC2IRQ */
	0, /* WDIRQ */
	0, /* PIOAIRQ */
	0, /* reserved */
	0, /* reserved */
	0, /* reserved */
	0, /* reserved */
	0, /* reserved */
	0, /* reserved */
	0, /* reserved */
	1, /* IRQ0 */
	0, /* IRQ1 */
	0, /* IRQ2 */
};

static unsigned char eb01_irq_type[32] = {
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,

	EdgeTriggered,	/* IRQ0 = neg. edge */
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
	EdgeTriggered,
};

void __inline__ at91_mask_irq(unsigned int irq)
{
	unsigned long mask = 1 << (irq);
	__raw_writel(mask, AIC_IDCR);
}	

void __inline__ at91_unmask_irq(unsigned int irq)
{
	unsigned long mask = 1 << (irq);
	__raw_writel(mask, AIC_IECR);
}

void __inline__ at91_mask_ack_irq(unsigned int irq)
{
	at91_mask_irq(irq);
}

void __inline__ at91_end_of_isr(void)
{
	/* Indicates end of ISR to AIC */
	__raw_writel(0x1L, AIC_EOICR); /* AIC don't care the value */
}

void __inline__ at91_unmask_and_eoi(unsigned int irq)
{
	at91_unmask_irq(irq);
	at91_end_of_isr();
}

static struct irqchip at91_chip = {
	.ack	= at91_mask_ack_irq,
	.mask	= at91_mask_irq,
	.unmask = at91_unmask_and_eoi,
};

#ifdef CONFIG_PM
static unsigned long ic_irq_enable;

static int irq_suspend(struct sys_device *dev, u32 state)
{
	return 0;
}

static int irq_resume(struct sys_device *dev)
{
	/* disable all irq sources */
	return 0;
}
#else
#define irq_suspend NULL
#define irq_resume NULL
#endif

static struct sysdev_class irq_class = {
	set_kset_name("irq"),
	.suspend	= irq_suspend,
	.resume		= irq_resume,
};

static struct sys_device irq_device = {
	.id	= 0,
	.cls	= &irq_class,
};

static int __init irq_init_sysfs(void)
{
	int ret = sysdev_class_register(&irq_class);
	if (ret == 0)
		ret = sysdev_register(&irq_device);
	return ret;
}

device_initcall(irq_init_sysfs);

void __init atmel_init_irq(void)
{
	int irq;

	/* Disable all interrupts */
	__raw_writel(0xFFFFFFFF, AIC_IDCR);

	/* Clear all interrupts	*/
	__raw_writel(0xFFFFFFFF, AIC_ICCR);

	for ( irq = 0 ; irq < 32 ; irq++ ) {
		__raw_writel(irq, AIC_EOICR);
	}

	for ( irq = 0 ; irq < 32 ; irq++ ) {
		__raw_writel(eb01_irq_prtable[irq] | eb01_irq_type[irq],
			AIC_SMR(irq));
	}

	for (irq = 0; irq < NR_IRQS; irq++) {
		if (!VALID_IRQ(irq)) continue;
		set_irq_chip(irq, &at91_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
}
