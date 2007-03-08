/*
 *  linux/arch/arm/mach-ks8695/time.c
 *
 *  Copyright (C) 2002 Micrel Inc.
 *  Copyright (C) 2006 Greg Ungerer <gerg@snapgear.com>
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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/irq.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/time.h>


/*
 * Cannout read back time on KS8695.
 */
static unsigned long ks8695_gettimeoffset(void)
{
	return 0;
}

static irqreturn_t ks8695_timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);
        __raw_writel(KS8695_INTMASK_TIMERINT1, KS8695_REG(KS8695_INT_STATUS));
	timer_tick();
	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

static struct irqaction ks8695_timer_irq = {
	.name	 = "KS8695 Timer Tick",
	.flags	 = IRQF_DISABLED | IRQF_TIMER,
	.handler = ks8695_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init ks8695_timer_init(void)
{
	unsigned long tmout = CLOCK_TICK_RATE / HZ / 2;

	/* Initialise to a known state (all timers off) */
        __raw_writel(0, KS8695_REG(KS8695_TIMER_CTRL));

	/* enable timer 1 as HZ clock */
        __raw_writel(tmout, KS8695_REG(KS8695_TIMER1));
        __raw_writel(tmout, KS8695_REG(KS8695_TIMER1_PCOUNT));
        __raw_writel(0x02, KS8695_REG(KS8695_TIMER_CTRL));

	setup_irq(KS8695_INT_TIMERINT1, &ks8695_timer_irq);
}

struct sys_timer ks8695_timer = {
	.init	= ks8695_timer_init,
	.offset	= ks8695_gettimeoffset,
};

