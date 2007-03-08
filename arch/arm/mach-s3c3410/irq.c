/*
 *  linux/arch/arm/mach-s5c7375/irq.c
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *	      Hyok S. Choi (hyok.choi@samsung.com)
 * based the codes by
 *     2003 Thomas Eschenbacher <thomas.eschenbacher@gmx.de>
 *
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

void __inline__ s3c3410_mask_irq(unsigned int irq)
{
	outl( inl(S3C3410X_INTMSK) & ~( 1 << irq ), S3C3410X_INTMSK);
}

void __inline__ s3c3410_unmask_irq(unsigned int irq)
{
	outl( inl(S3C3410X_INTMSK) | ( 1 << irq ), S3C3410X_INTMSK);
}

void __inline__ s3c3410_mask_ack_irq(unsigned int irq)
{
	s3c3410_mask_irq(irq);
}

/* Clear pending bit */
void __inline__ s3c3410_clear_pb(unsigned int irq)
{
     outl( ~(1 << irq), S3C3410X_INTPND);}


/* YOU CAN CHANGE THIS ROUTINE FOR SPEED UP */
__inline__ unsigned int fixup_irq (int irq )
{
	s3c3410_clear_pb(irq);
	return(irq);
}

static struct irqchip s3c3410_chip = {
	.ack	= s3c3410_clear_pb,
	.mask	= s3c3410_mask_irq,
	.unmask = s3c3410_unmask_irq,
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

void __init s3c3410_init_irq(void)
{
        int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &s3c3410_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* mask and disable all further interrupts */
	outl(0x00000000, S3C3410X_INTMSK);

	/* set all to IRQ mode, not FIQ */
	outl(0x00000000, S3C3410X_INTMOD);

	/* Clear Intrerrupt pending register	*/
	outl(0x00000000, S3C3410X_INTPND);

	/*
	 * enable the gloabal interrupt flag, this should be
	 * safe now, all sources are masked out and acknowledged
	 */
	outb(inb(S3C3410X_SYSCON) | S3C3410X_SYSCON_GIE, S3C3410X_SYSCON);
}

