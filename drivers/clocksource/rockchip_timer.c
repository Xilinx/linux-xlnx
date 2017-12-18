/*
 * Rockchip timer support
 *
 * Copyright (C) Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TIMER_NAME "rk_timer"

#define TIMER_LOAD_COUNT0	0x00
#define TIMER_LOAD_COUNT1	0x04
#define TIMER_CONTROL_REG3288	0x10
#define TIMER_CONTROL_REG3399	0x1c
#define TIMER_INT_STATUS	0x18

#define TIMER_DISABLE		0x0
#define TIMER_ENABLE		0x1
#define TIMER_MODE_FREE_RUNNING			(0 << 1)
#define TIMER_MODE_USER_DEFINED_COUNT		(1 << 1)
#define TIMER_INT_UNMASK			(1 << 2)

struct bc_timer {
	struct clock_event_device ce;
	void __iomem *base;
	void __iomem *ctrl;
	u32 freq;
};

static struct bc_timer bc_timer;

static inline struct bc_timer *rk_timer(struct clock_event_device *ce)
{
	return container_of(ce, struct bc_timer, ce);
}

static inline void __iomem *rk_base(struct clock_event_device *ce)
{
	return rk_timer(ce)->base;
}

static inline void __iomem *rk_ctrl(struct clock_event_device *ce)
{
	return rk_timer(ce)->ctrl;
}

static inline void rk_timer_disable(struct clock_event_device *ce)
{
	writel_relaxed(TIMER_DISABLE, rk_ctrl(ce));
}

static inline void rk_timer_enable(struct clock_event_device *ce, u32 flags)
{
	writel_relaxed(TIMER_ENABLE | TIMER_INT_UNMASK | flags,
		       rk_ctrl(ce));
}

static void rk_timer_update_counter(unsigned long cycles,
				    struct clock_event_device *ce)
{
	writel_relaxed(cycles, rk_base(ce) + TIMER_LOAD_COUNT0);
	writel_relaxed(0, rk_base(ce) + TIMER_LOAD_COUNT1);
}

static void rk_timer_interrupt_clear(struct clock_event_device *ce)
{
	writel_relaxed(1, rk_base(ce) + TIMER_INT_STATUS);
}

static inline int rk_timer_set_next_event(unsigned long cycles,
					  struct clock_event_device *ce)
{
	rk_timer_disable(ce);
	rk_timer_update_counter(cycles, ce);
	rk_timer_enable(ce, TIMER_MODE_USER_DEFINED_COUNT);
	return 0;
}

static int rk_timer_shutdown(struct clock_event_device *ce)
{
	rk_timer_disable(ce);
	return 0;
}

static int rk_timer_set_periodic(struct clock_event_device *ce)
{
	rk_timer_disable(ce);
	rk_timer_update_counter(rk_timer(ce)->freq / HZ - 1, ce);
	rk_timer_enable(ce, TIMER_MODE_FREE_RUNNING);
	return 0;
}

static irqreturn_t rk_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;

	rk_timer_interrupt_clear(ce);

	if (clockevent_state_oneshot(ce))
		rk_timer_disable(ce);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

static int __init rk_timer_init(struct device_node *np, u32 ctrl_reg)
{
	struct clock_event_device *ce = &bc_timer.ce;
	struct clk *timer_clk;
	struct clk *pclk;
	int ret = -EINVAL, irq;

	bc_timer.base = of_iomap(np, 0);
	if (!bc_timer.base) {
		pr_err("Failed to get base address for '%s'\n", TIMER_NAME);
		return -ENXIO;
	}
	bc_timer.ctrl = bc_timer.base + ctrl_reg;

	pclk = of_clk_get_by_name(np, "pclk");
	if (IS_ERR(pclk)) {
		ret = PTR_ERR(pclk);
		pr_err("Failed to get pclk for '%s'\n", TIMER_NAME);
		goto out_unmap;
	}

	ret = clk_prepare_enable(pclk);
	if (ret) {
		pr_err("Failed to enable pclk for '%s'\n", TIMER_NAME);
		goto out_unmap;
	}

	timer_clk = of_clk_get_by_name(np, "timer");
	if (IS_ERR(timer_clk)) {
		ret = PTR_ERR(timer_clk);
		pr_err("Failed to get timer clock for '%s'\n", TIMER_NAME);
		goto out_timer_clk;
	}

	ret = clk_prepare_enable(timer_clk);
	if (ret) {
		pr_err("Failed to enable timer clock\n");
		goto out_timer_clk;
	}

	bc_timer.freq = clk_get_rate(timer_clk);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -EINVAL;
		pr_err("Failed to map interrupts for '%s'\n", TIMER_NAME);
		goto out_irq;
	}

	ce->name = TIMER_NAME;
	ce->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
		       CLOCK_EVT_FEAT_DYNIRQ;
	ce->set_next_event = rk_timer_set_next_event;
	ce->set_state_shutdown = rk_timer_shutdown;
	ce->set_state_periodic = rk_timer_set_periodic;
	ce->irq = irq;
	ce->cpumask = cpu_possible_mask;
	ce->rating = 250;

	rk_timer_interrupt_clear(ce);
	rk_timer_disable(ce);

	ret = request_irq(irq, rk_timer_interrupt, IRQF_TIMER, TIMER_NAME, ce);
	if (ret) {
		pr_err("Failed to initialize '%s': %d\n", TIMER_NAME, ret);
		goto out_irq;
	}

	clockevents_config_and_register(ce, bc_timer.freq, 1, UINT_MAX);

	return 0;

out_irq:
	clk_disable_unprepare(timer_clk);
out_timer_clk:
	clk_disable_unprepare(pclk);
out_unmap:
	iounmap(bc_timer.base);

	return ret;
}

static int __init rk3288_timer_init(struct device_node *np)
{
	return rk_timer_init(np, TIMER_CONTROL_REG3288);
}

static int __init rk3399_timer_init(struct device_node *np)
{
	return rk_timer_init(np, TIMER_CONTROL_REG3399);
}

CLOCKSOURCE_OF_DECLARE(rk3288_timer, "rockchip,rk3288-timer",
		       rk3288_timer_init);
CLOCKSOURCE_OF_DECLARE(rk3399_timer, "rockchip,rk3399-timer",
		       rk3399_timer_init);
