/*
 *  linux/arch/armnommu/mach-s3c3410/time.c
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
#include <asm/hardware.h>
#include <asm/mach/time.h>
#include <asm/arch/time.h>


extern void s3c3410_unmask_irq(unsigned int irq);
                                                                                                                                           
unsigned long s3c3410_gettimeoffset (void)
{
        return (inw(S3C3410X_TCNT0) / CLOCKS_PER_USEC);
}
                                                                                                                                           
static irqreturn_t
s3c3410_timer_interrupt(int irq, void *dev_id)
{
    timer_tick();
                                                                                                                                           
    return IRQ_HANDLED;
}

static struct irqaction s3c3410_timer_irq = {
        .name           = "S3C3410 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
        .handler        = s3c3410_timer_interrupt
};
                                                                                                                                           
/*
 * Set up timer interrupt, and return the current time in seconds.
 */
                                                                                                                                           
void __init  s3c3410_time_init (void)
{
        u_int8_t tmod;
        u_int16_t period;
                                                                                                                                           
        /*
         * disable and clear timer 0, set to
         * internal clock and interval mode
         */
        tmod = S3C3410X_T16_OMS_INTRV | S3C3410X_T16_CL;
        outb(tmod, S3C3410X_TCON0);
                                                                                                                                           
        /* initialize the timer period and prescaler */
        period = (CONFIG_ARM_CLK/S3C3410X_TIMER0_PRESCALER)/HZ;
        outw(period, S3C3410X_TDAT0);
        outb(S3C3410X_TIMER0_PRESCALER-1, S3C3410X_TPRE0);
                                                                                                                                           
        /*
         * @todo do those really need to be function pointers ?
         */
        gettimeoffset     = s3c3410_gettimeoffset;
        s3c3410_timer_irq.handler = s3c3410_timer_interrupt;
                                                                                                                                           
        /* set up the interrupt vevtor for timer 0 match */
        setup_irq(S3C3410X_INTERRUPT_TMC0, &s3c3410_timer_irq);
                                                                                                                                           
        /* enable the timer IRQ */
        s3c3410_unmask_irq(S3C3410X_INTERRUPT_TMC0);
                                                                                                                                           
        /* let timer 0 run... */
        tmod |= S3C3410X_T16_TEN;
        tmod &= ~S3C3410X_T16_CL;
        outb(tmod, S3C3410X_TCON0);
}
                                                                                                                                           
