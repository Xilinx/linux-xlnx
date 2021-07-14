/*
 * Copyright (C) 2007-2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012-2013 Xilinx, Inc.
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpuhotplug.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/timecounter.h>
#include <asm/cpuinfo.h>

static void __iomem *clocksource_baseaddr;

struct xilinx_timer {
	void __iomem *timer_baseaddr;
	u32 irq;
	unsigned int freq_div_hz;
	unsigned int timer_clock_freq;
};

static DEFINE_PER_CPU(struct xilinx_timer, timer_priv);

#define TCSR0	(0x00)
#define TLR0	(0x04)
#define TCR0	(0x08)
#define TCSR1	(0x10)
#define TLR1	(0x14)
#define TCR1	(0x18)

#define TCSR_MDT	(1<<0)
#define TCSR_UDT	(1<<1)
#define TCSR_GENT	(1<<2)
#define TCSR_CAPT	(1<<3)
#define TCSR_ARHT	(1<<4)
#define TCSR_LOAD	(1<<5)
#define TCSR_ENIT	(1<<6)
#define TCSR_ENT	(1<<7)
#define TCSR_TINT	(1<<8)
#define TCSR_PWMA	(1<<9)
#define TCSR_ENALL	(1<<10)

static unsigned int (*read_fn)(void __iomem *);
static void (*write_fn)(u32, void __iomem *);

static void timer_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int timer_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void timer_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int timer_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

static inline void xilinx_timer0_stop(void)
{
	int cpu = smp_processor_id();
	struct xilinx_timer *timer = per_cpu_ptr(&timer_priv, cpu);
	void __iomem *timer_baseaddr = timer->timer_baseaddr;

	write_fn(read_fn(timer_baseaddr + TCSR0) & ~TCSR_ENT,
		 timer_baseaddr + TCSR0);
}

static inline void xilinx_timer0_start_periodic(void)
{
	int cpu = smp_processor_id();
	struct xilinx_timer *timer = per_cpu_ptr(&timer_priv, cpu);
	void __iomem *timer_baseaddr = timer->timer_baseaddr;
	unsigned long load_val = timer->freq_div_hz;

	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	write_fn(load_val, timer_baseaddr + TLR0);

	/* load the initial value */
	write_fn(TCSR_LOAD, timer_baseaddr + TCSR0);

	/* see timer data sheet for detail
	 * !ENALL - don't enable 'em all
	 * !PWMA - disable pwm
	 * TINT - clear interrupt status
	 * ENT- enable timer itself
	 * ENIT - enable interrupt
	 * !LOAD - clear the bit to let go
	 * ARHT - auto reload
	 * !CAPT - no external trigger
	 * !GENT - no external signal
	 * UDT - set the timer as down counter
	 * !MDT0 - generate mode
	 */
	write_fn(TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT,
		 timer_baseaddr + TCSR0);
}

static inline void xilinx_timer0_start_oneshot(unsigned long load_val)
{
	int cpu = smp_processor_id();
	struct xilinx_timer *timer = per_cpu_ptr(&timer_priv, cpu);
	void __iomem *timer_baseaddr = timer->timer_baseaddr;

	if (!load_val)
		load_val = 1;
	/* loading value to timer reg */
	write_fn(load_val, timer_baseaddr + TLR0);

	/* load the initial value */
	write_fn(TCSR_LOAD, timer_baseaddr + TCSR0);

	write_fn(TCSR_TINT|TCSR_ENIT|TCSR_ENT|TCSR_ARHT|TCSR_UDT,
		 timer_baseaddr + TCSR0);
}

static int xilinx_timer_set_next_event(unsigned long delta,
					struct clock_event_device *dev)
{
	pr_debug("%s: next event, delta %x\n", __func__, (u32)delta);
	xilinx_timer0_start_oneshot(delta);
	return 0;
}

static int xilinx_timer_shutdown(struct clock_event_device *evt)
{
	pr_info("%s\n", __func__);
	xilinx_timer0_stop();
	return 0;
}

static int xilinx_timer_set_periodic(struct clock_event_device *evt)
{
	pr_info("%s\n", __func__);
	xilinx_timer0_start_periodic();
	return 0;
}

static DEFINE_PER_CPU(struct clock_event_device, clockevent_xilinx_timer) = {
	.name			= "xilinx_clockevent",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.shift			= 8,
	.rating			= 300,
	.set_next_event		= xilinx_timer_set_next_event,
	.set_state_shutdown	= xilinx_timer_shutdown,
	.set_state_periodic	= xilinx_timer_set_periodic,
};

static inline void timer_ack(void)
{
	int cpu = smp_processor_id();
	struct xilinx_timer *timer = per_cpu_ptr(&timer_priv, cpu);
	void __iomem *timer_baseaddr = timer->timer_baseaddr;

	write_fn(read_fn(timer_baseaddr + TCSR0), timer_baseaddr + TCSR0);
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	timer_ack();
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static __init int xilinx_clockevent_init(int cpu, struct xilinx_timer *timer)
{
	struct clock_event_device *ce = per_cpu_ptr(&clockevent_xilinx_timer,
						    cpu);

	ce->mult = div_sc(timer->timer_clock_freq, NSEC_PER_SEC, ce->shift);
	ce->max_delta_ns = clockevent_delta2ns((u32)~0, ce);
	ce->max_delta_ticks = (u32)~0;
	ce->min_delta_ns = clockevent_delta2ns(1, ce);
	ce->min_delta_ticks = 1;
	ce->cpumask = cpumask_of(cpu);
	clockevents_register_device(ce);

	return 0;
}

static int microblaze_timer_starting(unsigned int cpu)
{
	int ret;
	struct xilinx_timer *timer = per_cpu_ptr(&timer_priv, cpu);
	struct clock_event_device *ce = per_cpu_ptr(&clockevent_xilinx_timer,
						    cpu);

	pr_debug("%s: cpu %d\n", __func__, cpu);

	if (!timer->timer_baseaddr) {
		/* It should never fail */
		pr_err("%s: clockevent timer for cpu %d failed\n",
		       __func__, cpu);
		return -EINVAL;
	}

	ret = request_irq(timer->irq, timer_interrupt, IRQF_TIMER |
			  IRQF_PERCPU | IRQF_NOBALANCING,
			  "timer", ce);
	if (ret) {
		pr_err("%s: request_irq failed\n", __func__);
		return ret;
	}

	return xilinx_clockevent_init(cpu, timer);
}

static int microblaze_timer_dying(unsigned int cpu)
{
	pr_debug("%s: cpu %d\n", __func__, cpu);

	return 0;
}

static u64 xilinx_clock_read(void)
{
	return read_fn(clocksource_baseaddr + TCR0);
}

static u64 xilinx_read(struct clocksource *cs)
{
	/* reading actual value of timer 1 */
	return (u64)xilinx_clock_read();
}

static struct timecounter xilinx_tc = {
	.cc = NULL,
};

static u64 xilinx_cc_read(const struct cyclecounter *cc)
{
	return xilinx_read(NULL);
}

static struct cyclecounter xilinx_cc = {
	.read = xilinx_cc_read,
	.mask = CLOCKSOURCE_MASK(32),
	.shift = 8,
};

static struct clocksource clocksource_microblaze = {
	.name		= "xilinx_clocksource",
	.rating		= 300,
	.read		= xilinx_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init xilinx_clocksource_init(unsigned int timer_clock_freq)
{
	int ret;

	ret = clocksource_register_hz(&clocksource_microblaze,
				      timer_clock_freq);
	if (ret) {
		pr_err("failed to register clocksource");
		return ret;
	}

	/* stop timer1 */
	write_fn(read_fn(clocksource_baseaddr + TCSR0) & ~TCSR_ENT,
		 clocksource_baseaddr + TCSR0);
	/* start timer1 - up counting without interrupt */
	write_fn(TCSR_TINT|TCSR_ENT|TCSR_ARHT, clocksource_baseaddr + TCSR0);

	/* register timecounter - for ftrace support */
	xilinx_cc.mult = div_sc(timer_clock_freq, NSEC_PER_SEC,
				xilinx_cc.shift);

	timecounter_init(&xilinx_tc, &xilinx_cc, sched_clock());

	sched_clock_register(xilinx_clock_read, 32, timer_clock_freq);

	return 0;
}

static int __init xilinx_timer_init(struct device_node *timer)
{
	struct clk *clk;
	static int initialized;
	u32 timer_num = 1;
	int ret = 0, cpu_id = 0;
	void __iomem *timer_baseaddr;
	unsigned int timer_clock_freq;
	bool clocksource = false;
	bool clockevent = false;

	ret = of_property_read_u32(timer, "cpu-id", (u32 *)&cpu_id);
	if (!ret && NR_CPUS > 1) {
		/* cpu_id will say if this is clocksource or clockevent */
		if (cpu_id >= NR_CPUS)
			clocksource = true;
		else
			clockevent = true;
	} else {
		/* No cpu_id property continue to work in old style */
		clocksource = true;
		clockevent = true;
	}

	if (clocksource) {
		/* TODO Add support for clocksource from one timer only */
		ret = of_property_read_u32(timer, "xlnx,one-timer-only", 
					   &timer_num);
		if (ret) {
			pr_err("%pOF: missing %s property\n",
				timer, "xlnx,one-timer-only");
			return -EINVAL;			
		}

		if (timer_num) {
			pr_err("%pOF: Please enable two timers in HW\n", timer);
			return -EINVAL;
		}
	}

	timer_baseaddr = of_iomap(timer, 0);
	if (!timer_baseaddr) {
		pr_err("ERROR: invalid timer base address\n");
		return -ENXIO;
	}

	write_fn = timer_write32;
	read_fn = timer_read32;

	write_fn(TCSR_MDT, timer_baseaddr + TCSR0);
	if (!(read_fn(timer_baseaddr + TCSR0) & TCSR_MDT)) {
		write_fn = timer_write32_be;
		read_fn = timer_read32_be;
	}

	clk = of_clk_get(timer, 0);
	if (IS_ERR(clk)) {
		pr_err("ERROR: timer CCF input clock not found\n");
		/* If there is clock-frequency property than use it */
		of_property_read_u32(timer, "clock-frequency",
				    &timer_clock_freq);
	} else {
		timer_clock_freq = clk_get_rate(clk);
	}

	if (!timer_clock_freq) {
		pr_err("ERROR: Using CPU clock frequency\n");
		return -EINVAL;
	}

	if (clocksource) {
		if (clocksource_baseaddr) {
			pr_err("%s: cpu %d has already clocksource timer\n",
			       __func__, cpu_id);
			return -EINVAL;
		}

		/* At this point we know that clocksource timer is second one */
		clocksource_baseaddr = timer_baseaddr + TCSR1;
		pr_info("%s: Timer base: 0x%x, Clocksource base: 0x%x\n",
			__func__, (u32)timer_baseaddr,
			(u32)clocksource_baseaddr);

		ret = xilinx_clocksource_init(timer_clock_freq);
		if (ret)
			return ret;
	}

	if (clockevent) {
		struct xilinx_timer *timer_st;

		/* Record what we know already */
		timer_st = per_cpu_ptr(&timer_priv, cpu_id);
		if (timer_st->timer_baseaddr) {
			pr_err("%s: cpu %d has already clockevent timer\n",
			       __func__, cpu_id);
			return -EINVAL;
		}

		timer_st->timer_baseaddr = timer_baseaddr;

		timer_st->irq = irq_of_parse_and_map(timer, 0);
		if (timer_st->irq <= 0) {
			pr_err("Failed to parse and map irq");
			return -EINVAL;
		}

		pr_info("%pOF: irq=%d, cpu_id %d\n",
			timer, timer_st->irq, cpu_id);

		timer_st->timer_clock_freq = timer_clock_freq;

		timer_st->freq_div_hz = timer_clock_freq / HZ;

		/* Can't call it several times */
		if (!initialized && !cpu_id) {
			ret = cpuhp_setup_state(CPUHP_AP_MICROBLAZE_TIMER_STARTING,
					"clockevents/microblaze/arch_timer:starting",
					microblaze_timer_starting,
					microblaze_timer_dying);
			if (!ret)
				initialized++;
		}
	}

	return ret;
}

TIMER_OF_DECLARE(xilinx_timer, "xlnx,xps-timer-1.00.a",
		       xilinx_timer_init);
