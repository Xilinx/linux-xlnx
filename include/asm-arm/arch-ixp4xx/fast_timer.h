#ifndef __ASM_ARM_MACH_FAST_TIMER_H
#define __ASM_ARM_MACH_FAST_TIMER_H

#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/irq.h>

static irqreturn_t
fast_timer_interrupt(int irq, void *dev_id)
{
	/* Clear Pending Interrupt by writing '1' to it */
	*IXP4XX_OSST = IXP4XX_OSST_TIMER_2_PEND;

	do_fast_timer();
	return IRQ_HANDLED;
}

static void fast_timer_set(void)
{
	unsigned long interval;

	/* Setup the Timer counter value */
	interval = (CLOCK_TICK_RATE + fast_timer_rate/2) / fast_timer_rate;
	*IXP4XX_OSRT2 = (interval & ~IXP4XX_OST_RELOAD_MASK) | IXP4XX_OST_ENABLE;
}

static int __init fast_timer_setup(void)
{
	/* Clear Pending Interrupt by writing '1' to it */
	*IXP4XX_OSST = IXP4XX_OSST_TIMER_2_PEND;

	/* Connect the interrupt handler and enable the interrupt */
	if (request_irq(IRQ_IXP4XX_TIMER2, fast_timer_interrupt, SA_INTERRUPT,
			"fast timer", NULL))
		return -EBUSY;

	fast_timer_rate = 5000;
	fast_timer_set();

	printk("fast timer: %d Hz, IRQ %d\n", fast_timer_rate,
			IRQ_IXP4XX_TIMER2);
	return 0;
}

static void __exit fast_timer_cleanup(void)
{
	*IXP4XX_OSRT2 = IXP4XX_OST_DISABLED;
	free_irq(IRQ_IXP4XX_TIMER2, NULL);
}

#endif
