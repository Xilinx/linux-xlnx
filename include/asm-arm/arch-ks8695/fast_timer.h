#ifndef __ASM_ARM_MACH_FAST_TIMER_H
#define __ASM_ARM_MACH_FAST_TIMER_H

#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

static irqreturn_t fast_timer_interrupt(int irq, void *dev_id)
{
	__raw_writel(KS8695_INTMASK_TIMERINT0, KS8695_REG(KS8695_INT_STATUS));
	do_fast_timer();
	return IRQ_HANDLED;
}

static void fast_timer_set(void)
{
	unsigned long interval, data, pulse, ctrl;

	/* Setup TIMER0 as fast clock */
	interval = (CLOCK_TICK_RATE/1000000) * fast_timer_rate;
	data = interval >> 1;
	pulse = interval - data;
	__raw_writel(data, KS8695_REG(KS8695_TIMER0));
	__raw_writel(pulse, KS8695_REG(KS8695_TIMER0_PCOUNT));
	ctrl = __raw_readl(KS8695_REG(KS8695_TIMER_CTRL)) | 0x01;
	__raw_writel(ctrl, KS8695_REG(KS8695_TIMER_CTRL));
}

static int __init fast_timer_setup(void)
{
	/* Connect the interrupt handler and enable the interrupt */
	if (request_irq(KS8695_INT_TIMERINT0, fast_timer_interrupt,
				SA_INTERRUPT, "fast timer", NULL))
		return -EBUSY;

	fast_timer_rate = 2000;
	fast_timer_set();

	printk("fast timer: %d Hz, IRQ %d\n", fast_timer_rate,
			KS8695_INT_TIMERINT0);
	return 0;
}

static void __exit fast_timer_cleanup(void)
{
	unsigned long ctrl;

	ctrl = __raw_readl(KS8695_REG(KS8695_TIMER_CTRL)) & 0x02;
	__raw_writel(ctrl, KS8695_REG(KS8695_TIMER_CTRL));
	free_irq(KS8695_INT_TIMERINT0, NULL);
}

#endif
