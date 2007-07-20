/*
 * arch/microblaze/kernel/opb_timer.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/profile.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/xparameters.h>

#define BASE_ADDR CONFIG_XILINX_TIMER_0_BASEADDR

#define TCSR0 (0x00)
#define TLR0  (0x04)
#define TCR0  (0x08)
#define TCSR1 (0x10)
#define TLR1  (0x14)
#define TCR1  (0x18)

#define TCSR_MDT   (1<<0)
#define TCSR_UDT   (1<<1)
#define TCSR_GENT  (1<<2)
#define TCSR_CAPT  (1<<3)
#define TCSR_ARHT  (1<<4)
#define TCSR_LOAD  (1<<5)
#define TCSR_ENIT  (1<<6)
#define TCSR_ENT   (1<<7)
#define TCSR_TINT  (1<<8)
#define TCSR_PWMA  (1<<9)
#define TCSR_ENALL (1<<10)

extern void heartbeat(void);

static void timer_ack(void)
{
	iowrite32(ioread32(BASE_ADDR + TCSR0), BASE_ADDR + TCSR0);
}

irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	heartbeat();

	timer_ack();

	write_seqlock(&xtime_lock);

	do_timer(1);
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

struct irqaction timer_irqaction = {
	.handler = timer_interrupt,
	.flags   = SA_INTERRUPT,
	.name    = "timer",
};

void system_timer_init(void)
{
	/* set the initial value to the load register */
	iowrite32(CONFIG_XILINX_CPU_CLOCK_FREQ/HZ, BASE_ADDR + TLR0);

	/* load the initial value */
	iowrite32(TCSR_LOAD, BASE_ADDR + TCSR0);

	/* see opb timer data sheet for detail
	 * !ENALL - don't enable 'em all
	 * !PWMA  - disable pwm
	 * TINT   - clear interrupt status
	 * ENT    - enable timer itself
	 * EINT   - enable interrupt
	 * !LOAD  - clear the bit to let go
	 * ARHT   - auto reload
	 * !CAPT  - no external trigger
	 * !GENT  - no external signal
	 * UDT    - set the timer as down counter
	 * !MDT0  - generate mode
	 *
	 */
	iowrite32(TCSR_TINT|TCSR_ENT|TCSR_ENIT|TCSR_ARHT|TCSR_UDT, BASE_ADDR + TCSR0);

	setup_irq(CONFIG_XILINX_TIMER_0_IRQ, &timer_irqaction);
}
