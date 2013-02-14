/*
 * This file contains driver for the Xilinx PS Timer Counter IP.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * based on arch/mips/kernel/time.c timer driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <asm/smp_twd.h>
#include "common.h"

/*
 * This driver configures the 2 16-bit count-up timers as follows:
 *
 * T1: Timer 1, clocksource for generic timekeeping
 * T2: Timer 2, clockevent source for hrtimers
 * T3: Timer 3, <unused>
 *
 * The input frequency to the timer module for emulation is 2.5MHz which is
 * common to all the timer channels (T1, T2, and T3). With a pre-scaler of 32,
 * the timers are clocked at 78.125KHz (12.8 us resolution).

 * The input frequency to the timer module in silicon is configurable and
 * obtained from device tree. The pre-scaler of 32 is used.
 */
#define XTTCPS_CLOCKSOURCE	0	/* Timer 1 as a generic timekeeping */
#define XTTCPS_CLOCKEVENT	1	/* Timer 2 as a clock event */

/*
 * Timer Register Offset Definitions of Timer 1, Increment base address by 4
 * and use same offsets for Timer 2
 */
#define XTTCPS_CLK_CNTRL_OFFSET	0x00 /* Clock Control Reg, RW */
#define XTTCPS_CNT_CNTRL_OFFSET	0x0C /* Counter Control Reg, RW */
#define XTTCPS_COUNT_VAL_OFFSET	0x18 /* Counter Value Reg, RO */
#define XTTCPS_INTR_VAL_OFFSET		0x24 /* Interval Count Reg, RW */
#define XTTCPS_ISR_OFFSET		0x54 /* Interrupt Status Reg, RO */
#define XTTCPS_IER_OFFSET		0x60 /* Interrupt Enable Reg, RW */

#define XTTCPS_CNT_CNTRL_DISABLE_MASK	0x1

/*
 * Setup the timers to use pre-scaling, using a fixed value for now that will
 * work across most input frequency, but it may need to be more dynamic
 */
#define PRESCALE_EXPONENT	11	/* 2 ^ PRESCALE_EXPONENT = PRESCALE */
#define PRESCALE		2048	/* The exponent must match this */
#define CLK_CNTRL_PRESCALE (((PRESCALE_EXPONENT - 1) << 1) | 0x1)
#define CNT_CNTRL_RESET		(1 << 4)

/**
 * struct xttcps_timer - This definition defines local timer structure
 *
 * @base_addr:	Base address of timer
 */
struct xttcps_timer {
	void __iomem *base_addr;
	int frequency;
	struct clk *clk;
	struct notifier_block clk_rate_change_nb;
};

static struct xttcps_timer timers[2];
static struct clock_event_device xttcps_clockevent;

/**
 * xttcps_set_interval - Set the timer interval value
 *
 * @timer:	Pointer to the timer instance
 * @cycles:	Timer interval ticks
 **/
static void xttcps_set_interval(struct xttcps_timer *timer,
					unsigned long cycles)
{
	u32 ctrl_reg;

	/* Disable the counter, set the counter value  and re-enable counter */
	ctrl_reg = __raw_readl(timer->base_addr + XTTCPS_CNT_CNTRL_OFFSET);
	ctrl_reg |= XTTCPS_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + XTTCPS_CNT_CNTRL_OFFSET);

	__raw_writel(cycles, timer->base_addr + XTTCPS_INTR_VAL_OFFSET);

	/*
	 * Reset the counter (0x10) so that it starts from 0, one-shot
	 * mode makes this needed for timing to be right.
	 */
	ctrl_reg |= CNT_CNTRL_RESET;
	ctrl_reg &= ~XTTCPS_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + XTTCPS_CNT_CNTRL_OFFSET);
}

/**
 * xttcps_clock_event_interrupt - Clock event timer interrupt handler
 *
 * @irq:	IRQ number of the Timer
 * @dev_id:	void pointer to the xttcps_timer instance
 *
 * returns: Always IRQ_HANDLED - success
 **/
static irqreturn_t xttcps_clock_event_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &xttcps_clockevent;
	struct xttcps_timer *timer = dev_id;

	/* Acknowledge the interrupt and call event handler */
	__raw_readl(timer->base_addr + XTTCPS_ISR_OFFSET);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction event_timer_irq = {
	.name	= "xttcps clockevent",
	.flags	= IRQF_DISABLED | IRQF_TIMER,
	.handler = xttcps_clock_event_interrupt,
};

/**
 * xttcps_timer_hardware_init - Initialize the timer hardware
 *
 * Initialize the hardware to start the clock source, get the clock
 * event timer ready to use, and hook up the interrupt.
 */
static void __init xttcps_timer_hardware_init(void)
{
	/*
	 * Setup the clock source counter to be an incrementing counter
	 * with no interrupt and it rolls over at 0xFFFF. Pre-scale
	 * it by 32 also. Let it start running now.
	 */
	__raw_writel(0x0, timers[XTTCPS_CLOCKSOURCE].base_addr +
				XTTCPS_IER_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE,
			timers[XTTCPS_CLOCKSOURCE].base_addr +
			XTTCPS_CLK_CNTRL_OFFSET);
	__raw_writel(0x10, timers[XTTCPS_CLOCKSOURCE].base_addr +
				XTTCPS_CNT_CNTRL_OFFSET);

	/*
	 * Setup the clock event timer to be an interval timer which
	 * is prescaled by 32 using the interval interrupt. Leave it
	 * disabled for now.
	 */
	__raw_writel(0x23, timers[XTTCPS_CLOCKEVENT].base_addr +
			XTTCPS_CNT_CNTRL_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE,
			timers[XTTCPS_CLOCKEVENT].base_addr +
			XTTCPS_CLK_CNTRL_OFFSET);
	__raw_writel(0x1, timers[XTTCPS_CLOCKEVENT].base_addr +
			XTTCPS_IER_OFFSET);
}

/**
 * __raw_readl_cycles - Reads the timer counter register
 *
 * returns: Current timer counter register value
 **/
static cycle_t __raw_readl_cycles(struct clocksource *cs)
{
	struct xttcps_timer *timer = &timers[XTTCPS_CLOCKSOURCE];

	return (cycle_t)__raw_readl(timer->base_addr +
				XTTCPS_COUNT_VAL_OFFSET);
}

/*
 * Instantiate and initialize the clock source structure
 */
static struct clocksource clocksource_xttcps = {
	.name		= "xttcps_timer1",
	.rating		= 200,			/* Reasonable clock source */
	.read		= __raw_readl_cycles,
	.mask		= CLOCKSOURCE_MASK(16),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/**
 * xttcps_set_next_event - Sets the time interval for next event
 *
 * @cycles:	Timer interval ticks
 * @evt:	Address of clock event instance
 *
 * returns: Always 0 - success
 **/
static int xttcps_set_next_event(unsigned long cycles,
					struct clock_event_device *evt)
{
	struct xttcps_timer *timer = &timers[XTTCPS_CLOCKEVENT];

	xttcps_set_interval(timer, cycles);
	return 0;
}

/**
 * xttcps_set_mode - Sets the mode of timer
 *
 * @mode:	Mode to be set
 * @evt:	Address of clock event instance
 **/
static void xttcps_set_mode(enum clock_event_mode mode,
					struct clock_event_device *evt)
{
	struct xttcps_timer *timer = &timers[XTTCPS_CLOCKEVENT];
	u32 ctrl_reg;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		xttcps_set_interval(timer, timer->frequency / HZ);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl_reg = __raw_readl(timer->base_addr +
					XTTCPS_CNT_CNTRL_OFFSET);
		ctrl_reg |= XTTCPS_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + XTTCPS_CNT_CNTRL_OFFSET);
		break;
	case CLOCK_EVT_MODE_RESUME:
		ctrl_reg = __raw_readl(timer->base_addr +
					XTTCPS_CNT_CNTRL_OFFSET);
		ctrl_reg &= ~XTTCPS_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + XTTCPS_CNT_CNTRL_OFFSET);
		break;
	}
}

/*
 * Instantiate and initialize the clock event structure
 */
static struct clock_event_device xttcps_clockevent = {
	.name		= "xttcps_timer2",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event	= xttcps_set_next_event,
	.set_mode	= xttcps_set_mode,
	.rating		= 200,
};

static int xttcps_timer_rate_change_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;

	switch (event) {
	case POST_RATE_CHANGE:
	{
		unsigned long flags;

		timers[XTTCPS_CLOCKSOURCE].frequency =
			ndata->new_rate / PRESCALE;
		timers[XTTCPS_CLOCKEVENT].frequency =
			ndata->new_rate / PRESCALE;

		/*
		 * Do whatever is necessare to maintain a proper time base
		 *
		 * I cannot find a way to adjust the currently used clocksource
		 * to the new frequency. __clocksource_updatefreq_hz() sounds
		 * good, but does not work. Not sure what's that missing.
		 *
		 * This approach works, but triggers two clocksource switches.
		 * The first after unregister to clocksource jiffies. And
		 * another one after the register to the newly registered timer.
		 *
		 * Alternatively we could 'waste' another HW timer to ping pong
		 * between clock sources. That would also use one register and
		 * one unregister call, but only trigger one clocksource switch
		 * for the cost of another HW timer used by the OS.
		 */
		clocksource_unregister(&clocksource_xttcps);
		clocksource_register_hz(&clocksource_xttcps,
				ndata->new_rate / PRESCALE);

		/*
		 * clockevents_update_freq should be called with IRQ disabled on
		 * the CPU the timer provides events for. The timer we use is
		 * common to both CPUs, not sure if we need to run on both
		 * cores.
		 */
		local_irq_save(flags);
		clockevents_update_freq(&xttcps_clockevent,
				timers[XTTCPS_CLOCKEVENT].frequency);
		local_irq_restore(flags);

		/* fall through */
	}
	case PRE_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

/**
 * xttcps_timer_init - Initialize the timer
 *
 * Initializes the timer hardware and register the clock source and clock event
 * timers with Linux kernal timer framework
 */
void __init xttcps_timer_init(void)
{
	unsigned int irq;
	struct device_node *timer = NULL;
	void __iomem *timer_baseaddr;
	const char * const timer_list[] = {
		"xlnx,ps7-ttc-1.00.a",
		NULL
	};
	struct clk *clk;

	/*
	 * Get the 1st Triple Timer Counter (TTC) block from the device tree
	 * and use it. Note that the event timer uses the interrupt and it's the
	 * 2nd TTC hence the irq_of_parse_and_map(,1)
	 */
	timer = of_find_compatible_node(NULL, NULL, timer_list[0]);
	if (!timer) {
		pr_err("ERROR: no compatible timer found\n");
		BUG();
	}

	timer_baseaddr = of_iomap(timer, 0);
	if (!timer_baseaddr) {
		pr_err("ERROR: invalid timer base address\n");
		BUG();
	}

	irq = irq_of_parse_and_map(timer, 1);
	if (!irq || irq == NO_IRQ) {
		pr_err("ERROR: invalid interrupt number\n");
		BUG();
	}

	timers[XTTCPS_CLOCKSOURCE].base_addr = timer_baseaddr;
	timers[XTTCPS_CLOCKEVENT].base_addr = timer_baseaddr + 4;

	event_timer_irq.dev_id = &timers[XTTCPS_CLOCKEVENT];
	setup_irq(irq, &event_timer_irq);

	pr_info("%s #0 at %p, irq=%d\n", timer_list[0], timer_baseaddr, irq);

	clk = clk_get_sys("CPU_1X_CLK", NULL);
	if (IS_ERR(clk)) {
		pr_err("ERROR: timer input clock not found\n");
		BUG();
	}

	clk_prepare_enable(clk);
	timers[XTTCPS_CLOCKSOURCE].clk = clk;
	timers[XTTCPS_CLOCKEVENT].clk = clk;
	timers[XTTCPS_CLOCKSOURCE].clk_rate_change_nb.notifier_call =
		xttcps_timer_rate_change_cb;
	timers[XTTCPS_CLOCKEVENT].clk_rate_change_nb.notifier_call =
		xttcps_timer_rate_change_cb;
	timers[XTTCPS_CLOCKSOURCE].clk_rate_change_nb.next = NULL;
	timers[XTTCPS_CLOCKEVENT].clk_rate_change_nb.next = NULL;
	timers[XTTCPS_CLOCKSOURCE].frequency =
		clk_get_rate(clk) / PRESCALE;
	timers[XTTCPS_CLOCKEVENT].frequency =
		clk_get_rate(clk) / PRESCALE;
	if (clk_notifier_register(clk,
		&timers[XTTCPS_CLOCKSOURCE].clk_rate_change_nb))
		pr_warn("Unable to register clock notifier.\n");

	xttcps_timer_hardware_init();
	clocksource_register_hz(&clocksource_xttcps,
				timers[XTTCPS_CLOCKSOURCE].frequency);

	/* Indicate that clock event is on 1st CPU as SMP boot needs it */
	xttcps_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&xttcps_clockevent,
			timers[XTTCPS_CLOCKEVENT].frequency, 1, 0xfffe);
#ifdef CONFIG_HAVE_ARM_TWD
	twd_local_timer_of_register();
#endif
}
