/*
 *  linux/arch/arm/mach-p2001/irq.c
 *
 *  Copyright (C) 2004-2005 Tobias Lorenz
 *
 *  IRQ handling code
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


/**************************************************************************
 * IRQ Chip
 **************************************************************************/

void __inline__ p2001_irqchip_mask(unsigned int irq)
{
	unsigned long mask = 1 << (irq);
	P2001_INT_CTRL->Main_NIRQ_Int_Ctrl &= ~mask;
}	

void __inline__ p2001_irqchip_unmask(unsigned int irq)
{
	unsigned long mask = 1 << (irq);
	P2001_INT_CTRL->Main_NIRQ_Int_Ctrl |= mask;
}

void __inline__ p2001_irqchip_ack(unsigned int irq)
{
	p2001_irqchip_mask(irq);
}

#if 0
int __inline__ p2001_irqchip_retrigger(unsigned int irq)
{
	/* not implemented */
	return 0;
}
#endif

#if 0
int __inline__ p2001_irqchip_type(unsigned int irq, unsigned int flags)
{
	/* not implemented */
	return 0;
}
#endif

#if 0
int __inline__ p2001_irqchip_wake(unsigned int irq, unsigned int on)
{
	/* not implemented */
	return 0;
}
#endif

static struct irqchip p2001_irqchip = {
	.ack		= p2001_irqchip_ack,		/* Acknowledge the IRQ. */
	.mask		= p2001_irqchip_mask,		/* Mask the IRQ in hardware. */
	.unmask		= p2001_irqchip_unmask,		/* Unmask the IRQ in hardware. */
//	.retrigger	= p2001_irqchip_retrigger,	/* Ask the hardware to re-trigger the IRQ. */
//	.type		= p2001_irqchip_type,		/* Set the type of the IRQ. */
//	.wake		= p2001_irqchip_wake,		/* Set wakeup-enable on the selected IRQ */
};



/**************************************************************************
 * System IRQ Device/Class
 **************************************************************************/

#if 0
static int irq_shutdown(struct sys_device *dev)
{
	return 0;
}
#endif

#ifdef CONFIG_PM
static int irq_suspend(struct sys_device *dev, u32 state)
{
	return 0;
}

static int irq_resume(struct sys_device *dev)
{
	/* disable all irq sources */
	return 0;
}
#endif

static struct sysdev_class irq_class = {
//	.shutdown	= irq_shutdown,
#ifdef CONFIG_PM
	.suspend	= irq_suspend,
	.resume		= irq_resume,
#endif
	set_kset_name("irq"),
};

static struct sys_device irq_device = {
	.id	= 0,
	.cls	= &irq_class,
};



/**************************************************************************
 * Module functions
 **************************************************************************/

static int __init irq_init_sysfs(void)
{
	int ret = sysdev_class_register(&irq_class);
	if (ret == 0)
		ret = sysdev_register(&irq_device);
	return ret;
}

device_initcall(irq_init_sysfs);

void __init p2001_init_irq(void)
{
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &p2001_irqchip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
}
