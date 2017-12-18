/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <linux/of_platform.h>

#include "clock.h"

static struct ti_dt_clk dm814_clks[] = {
	DT_CLK(NULL, "devosc_ck", "devosc_ck"),
	DT_CLK(NULL, "mpu_ck", "mpu_ck"),
	DT_CLK(NULL, "sysclk4_ck", "sysclk4_ck"),
	DT_CLK(NULL, "sysclk5_ck", "sysclk5_ck"),
	DT_CLK(NULL, "sysclk6_ck", "sysclk6_ck"),
	DT_CLK(NULL, "sysclk8_ck", "sysclk8_ck"),
	DT_CLK(NULL, "sysclk10_ck", "sysclk10_ck"),
	DT_CLK(NULL, "sysclk18_ck", "sysclk18_ck"),
	DT_CLK(NULL, "timer_sys_ck", "devosc_ck"),
	DT_CLK(NULL, "timer1_fck", "timer1_fck"),
	DT_CLK(NULL, "timer2_fck", "timer2_fck"),
	DT_CLK(NULL, "cpsw_125mhz_gclk", "cpsw_125mhz_gclk"),
	DT_CLK(NULL, "cpsw_cpts_rft_clk", "cpsw_cpts_rft_clk"),
	{ .node_name = NULL },
};

static bool timer_clocks_initialized;

static int __init dm814x_adpll_early_init(void)
{
	struct device_node *np;

	if (!timer_clocks_initialized)
		return -ENODEV;

	np = of_find_node_by_name(NULL, "pllss");
	if (!np) {
		pr_err("Could not find node for plls\n");
		return -ENODEV;
	}

	of_platform_populate(np, NULL, NULL, NULL);

	return 0;
}
core_initcall(dm814x_adpll_early_init);

static const char * const init_clocks[] = {
	"pll040clkout",		/* MPU 481c5040.adpll.clkout */
	"pll290clkout",		/* DDR 481c5290.adpll.clkout */
};

static int __init dm814x_adpll_enable_init_clocks(void)
{
	int i, err;

	if (!timer_clocks_initialized)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(init_clocks); i++) {
		struct clk *clock;

		clock = clk_get(NULL, init_clocks[i]);
		if (WARN(IS_ERR(clock), "could not find init clock %s\n",
			 init_clocks[i]))
			continue;
		err = clk_prepare_enable(clock);
		if (WARN(err, "could not enable init clock %s\n",
			 init_clocks[i]))
			continue;
	}

	return 0;
}
postcore_initcall(dm814x_adpll_enable_init_clocks);

int __init dm814x_dt_clk_init(void)
{
	ti_dt_clocks_register(dm814_clks);
	omap2_clk_disable_autoidle_all();
	omap2_clk_enable_init_clocks(NULL, 0);
	timer_clocks_initialized = true;

	return 0;
}
