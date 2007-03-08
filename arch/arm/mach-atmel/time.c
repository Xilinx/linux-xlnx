/*
 *  linux/arch/armnommu/mach-atmel/time.c
 *
 *  Copyright (C) SAMSUNG ELECTRONICS 
 *		      Hyok S. Choi <hyok.choi@samsung.com>
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
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/time.h>
#include <asm/arch/time.h>

unsigned long atmel_gettimeoffset (void)
{
	volatile struct at91_timers* tt = (struct at91_timers*) (AT91_TC_BASE);
	volatile struct at91_timer_channel* tc = &tt->chans[KERNEL_TIMER].ch;
	return tc->cv * (1000*1000)/(ARM_CLK/128);
}

static irqreturn_t
atmel_timer_interrupt(int irq, void *dev_id)
{
	/* Clear the timer status interrupt. */
	volatile struct at91_timers* tt = (struct at91_timers*) (AT91_TC_BASE);
	volatile struct at91_timer_channel* tc = &tt->chans[KERNEL_TIMER].ch;
	(void)tc->sr;

	timer_tick();
	return IRQ_HANDLED;
}

static struct irqaction atmel_timer_irq = {
	.name		= "ATMEL Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= atmel_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */

void __init  atmel_time_init (void)
{
	register volatile struct at91_timers* tt = (struct at91_timers*) (AT91_TC_BASE);
	register volatile struct at91_timer_channel* tc = &tt->chans[KERNEL_TIMER].ch;
	unsigned long v;

	/* enable Kernel timer */
	HW_AT91_TIMER_INIT(KERNEL_TIMER)

	/* No SYNC */
	tt->bcr = 0;
	/* program NO signal on XC1 */
	v = tt->bmr;
	v &= ~TCNXCNS(KERNEL_TIMER,3);
	v |= TCNXCNS(KERNEL_TIMER,1);
	tt->bmr = v;

	tc->ccr = 2;  /* disable the channel */

	/* select ACLK/128 as inupt frequency for TC1 and enable CPCTRG */
	tc->cmr = 3 | (1 << 14);

	tc->idr = ~0ul;  /* disable all interrupt */
	tc->rc = ((ARM_CLK/128)/HZ - 1);   /* load the count limit into the CR register */
	tc->ier = TC_CPCS;  /* enable CPCS interrupt */

	/*
	 * @todo do those really need to be function pointers ?
	 */
	atmel_timer_irq.handler = atmel_timer_interrupt;

	/* set up the interrupt */
	setup_irq(KERNEL_TIMER_IRQ_NUM, &atmel_timer_irq);

	/* enable the channel */
	tc->ccr = TC_SWTRG|TC_CLKEN;
}

struct sys_timer atmel_timer = {
	.init		= atmel_time_init,
	.offset		= atmel_gettimeoffset,
};
