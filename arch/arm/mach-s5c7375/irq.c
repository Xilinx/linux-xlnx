/*
 *  linux/arch/armnommu/mach-s5c7375/irq.c
 *
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

void __inline__ s5c7375_mask_irq(unsigned int irq)
{
        rINTMSK |= ((unsigned long) 1 << irq);
}

void __inline__ s5c7375_unmask_irq(unsigned int irq)
{
        rINTMSK &= ~((unsigned long)1 << irq);
}

void __inline__ s5c7375_mask_ack_irq(unsigned int irq)
{
	s5c7375_mask_irq(irq);
}

void __inline__ s5c7375_clear_pb(unsigned int irq)
{
     rIRQISPC = (0x00000001<<irq);	/* Clear pending bit */
     rIRQISPC;  /* WriteBack */
}


/* YOU CAN CHANGE THIS ROUTINE FOR SPEED UP */
__inline__ unsigned int fixup_irq (int irq )
{
	s5c7375_clear_pb(irq);
	return(irq);
}

static struct irqchip s5c7375_chip = {
	.ack	= s5c7375_clear_pb,
	.mask	= s5c7375_mask_irq,
	.unmask = s5c7375_unmask_irq,
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

void __init s5c7375_init_irq(void)
{
        int irq;

        /* Disable all IRQs */

        rINTMSK = 0xffffffff; /* all masked */
        /****************
         * All IRQs are IRQ, not FIQ 
         * Write only one register,
         * 0 : IRQ mode
         * 1 : FIQ mode
         *******************************************/
        rINTMOD = 0x00000000; 

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &s5c7375_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	rINTCON = 0x0; // all interrupt is disabled
//	rINTCON = 0xD; // vectored mode
	DisableFIQ();	// fiq is disabled
#ifndef CONFIG_S5C7375VM
	EnableIRQ();		// irq is enabled
#else
	DisableIRQ();
#endif
	DisableGMask();	// global mask is disabled
}

