/*
 *  linux/arch/arm/mach-lpc22xx/irq.c
 *
 *  Copyright (C) 2004 Philips Semiconductors
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

void __inline__ lpc22xx_mask_irq(unsigned int irq)
{
	VICIntEnClr = 1 << irq;
}

void __inline__ lpc22xx_unmask_irq(unsigned int irq)
{
	VICIntEnable |= 1 << irq;
}

/* Clear pending bit */
void __inline__ lpc22xx_clear_pb(unsigned int irq)
{
	/* write any thing to VICVectAddr */
	VICVectAddr = 0x00;
	/* clear external interrupt */
	EXTINT = 0x0F;
}

void __inline__ lpc22xx_mask_ack_irq(unsigned int irq)
{

	lpc22xx_clear_pb(irq);
	lpc22xx_mask_irq(irq);
}


/* YOU CAN CHANGE THIS ROUTINE FOR SPEED UP */
__inline__ unsigned int fixup_irq (int irq )
{
	lpc22xx_clear_pb(irq);
	return(irq);
}

static struct irqchip lpc22xx_chip = {
	.ack	= lpc22xx_mask_ack_irq,
	.mask	= lpc22xx_mask_irq,
	.unmask = lpc22xx_unmask_irq,
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

void __init lpc22xx_init_irq(void)
{
        int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &lpc22xx_chip);
		set_irq_handler(irq, do_level_IRQ);
		if(irq >= LPC22xx_INTERRUPT_EINT0 && irq <= LPC22xx_INTERRUPT_EINT3)
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE | IRQF_NOAUTOEN);
		else
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* mask and disable all further interrupts */
	VICIntEnClr = 0xFFFFFFFF;

	/* set all to IRQ mode, not FIQ */
	VICIntSelect = 0x00000000;
	
	/* Clear Intrerrupt pending register	*/
	VICVectAddr = 0x00000000;

	/* Set external interrupts*/
	/* These should be different with your board */
	/* ext2: for ethernet controller rtl8019as */
	EXTMODE = 0x04;		// set ext2 edge sensitive
	EXTPOLAR = 0x04;	// set ext2 rising edge effective
	EXTINT = 0x04;		// clear flags
}

