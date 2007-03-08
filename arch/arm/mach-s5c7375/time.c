/*
 *  linux/arch/armnommu/mach-s5c7375/time.c
 *
 *  Copyright (C) SAMSUNG ELECTRONICS 
 *                      Hyok S. Choi <hyok.choi@samsung.com>
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
#include <asm/arch/s5c7375.h>
#include <asm/mach/time.h>
#include <asm/arch/time.h>

unsigned long s5c7375_gettimeoffset (void)
{
	return (((RESCHED_PERIOD  * CLOCKS_PER_USEC) /1000) - rT3LDR)  / CLOCKS_PER_USEC;
}

static irqreturn_t
s5c7375_timer_interrupt(int irq, void *dev_id)
{
    /* clear interrupt pending bit */
    rT3ISR = 0;
    timer_tick();

    return IRQ_HANDLED;
}

static struct irqaction s5c7375_timer_irq = {
        .name           = "S5C7375 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
        .handler        = s5c7375_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */

void __init  s5c7375_time_init (void)
{
	//- APB bus speed setting
	/*
	 * Number of AHB clock cycles allocated in the ENABLE or
	 * SETUP state of the 2-nd APB peripheral minus one.
	 */
	rAPBCON2=(unsigned long)0x00010000; 

	s5c7375_timer_irq.handler = s5c7375_timer_interrupt;

	/*
	 * Timer 3 is used for OS_timer by external clock.
	 */
	rT3CTR = TMR_TE_DISABLE | TMR_IE_PULSE | TMR_OE_ENABLE | TMR_UD_DOWN \
			| TMR_UDS_TxCTR | TMR_OM_PULSE | TMR_ES_POS | TMR_M_PERIODIC_TIMER;

	/*
	 * prescaler to 0x6B 'cause : 
	 * 	27M / (0x6B +1) = 4usec
	 */
	rT3PSR = SYS_TIMER03_PRESCALER; // 0x6B
	/* rT3LDR  =  X second * (frequency/second ) */
	rT3LDR = RESCHED_PERIOD  * CLOCKS_PER_USEC /1000;
			/* is equal to 
			 *	RESCHED_PERIOD * 1000    // for msec to usec
			 * 	   * (ECLK/ (SYS_TIMER03_PRESCALER +1)) /1000000;
			 *	= 2500
			 */
   	/* clear interrupt pending bit */
	rT3ISR = 0;

	setup_irq(INT_N_TIMER3, &s5c7375_timer_irq);

	/* timer 3 enable it! */
	rT3CTR |= TMR_TE_ENABLE;

}


struct sys_timer s5c7375_timer = {
	.init		= s5c7375_time_init,
	.offset		= s5c7375_gettimeoffset,
};
