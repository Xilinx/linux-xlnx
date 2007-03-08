/*
 *  linux/arch/arm/mach-s3c44b0x/irq.c
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

void __inline__ s3c44b0x_mask_irq(unsigned int irq)
{
	SYSREG_OR_SET(S3C44B0X_INTMSK, 1<<irq);
}

void __inline__ s3c44b0x_unmask_irq(unsigned int irq)
{
	SYSREG_CLR(S3C44B0X_INTMSK, 1<<irq);
}

void __inline__ s3c44b0x_mask_ack_irq(unsigned int irq)
{
	s3c44b0x_mask_irq(irq);
}

/* Clear pending bit */
void __inline__ s3c44b0x_clear_pb(unsigned int irq)
{
	SYSREG_OR_SET(S3C44B0X_I_ISPC, 1<<irq);
}


/* YOU CAN CHANGE THIS ROUTINE FOR SPEED UP */
__inline__ unsigned int fixup_irq (int irq )
{
	s3c44b0x_clear_pb(irq);
	return(irq);
}

static struct irqchip s3c44b0x_chip = {
	.ack	= s3c44b0x_clear_pb,
	.mask	= s3c44b0x_mask_irq,
	.unmask = s3c44b0x_unmask_irq,
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


void __init s3c44b0x_init_irq(void)
{
        int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &s3c44b0x_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* Note: in Samsung s3c44b0x um
	
	"1. INTMSK register can be masked only when it is sure that the corresponding interrupt does not be
	requested. If your application should mask any interrupt mask bit(INTMSK) just when the
	corresponding interrupt is issued, please contact our FAE (field application engineer).
	
	2. If you need that all interrupt is masked, we recommend that I/F bits in CPSR are set using MRS, MSR
	instructions. The I, F bit in CPSR can be masked even when any interrupt is issued."
	
	*/
	/* at this moment, the I/F bits should has been set, so it's safe to use rINTMAK */
	
	/* mask and disable all further interrupts */
	SYSREG_SET(S3C44B0X_INTMSK, 0x07ffffff);

	/* set all to IRQ mode, not FIQ */

	SYSREG_SET(S3C44B0X_INTCON, 0x5);	 // Vectored & IRQ & !FIQ
	SYSREG_SET(S3C44B0X_INTMOD, 0x00000000); // All IRQ mode

	/* Clear Intrerrupt pending register */

	SYSREG_OR_SET(S3C44B0X_I_ISPC, 0x7fffffff);

	/*
	 * enable the gloabal interrupt flag, this should be
	 * safe now, all sources are masked out and acknowledged
	 */
	SYSREG_CLR(S3C44B0X_INTMSK, 1<<26);
}

