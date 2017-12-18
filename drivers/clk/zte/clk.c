/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/div64.h>

#include "clk.h"

#define to_clk_zx_pll(_hw) container_of(_hw, struct clk_zx_pll, hw)
#define to_clk_zx_audio(_hw) container_of(_hw, struct clk_zx_audio, hw)

#define CFG0_CFG1_OFFSET 4
#define LOCK_FLAG 30
#define POWER_DOWN 31

static int rate_to_idx(struct clk_zx_pll *zx_pll, unsigned long rate)
{
	const struct zx_pll_config *config = zx_pll->lookup_table;
	int i;

	for (i = 0; i < zx_pll->count; i++) {
		if (config[i].rate > rate)
			return i > 0 ? i - 1 : 0;

		if (config[i].rate == rate)
			return i;
	}

	return i - 1;
}

static int hw_to_idx(struct clk_zx_pll *zx_pll)
{
	const struct zx_pll_config *config = zx_pll->lookup_table;
	u32 hw_cfg0, hw_cfg1;
	int i;

	hw_cfg0 = readl_relaxed(zx_pll->reg_base);
	hw_cfg1 = readl_relaxed(zx_pll->reg_base + CFG0_CFG1_OFFSET);

	/* For matching the value in lookup table */
	hw_cfg0 &= ~BIT(zx_pll->lock_bit);
	hw_cfg0 |= BIT(zx_pll->pd_bit);

	for (i = 0; i < zx_pll->count; i++) {
		if (hw_cfg0 == config[i].cfg0 && hw_cfg1 == config[i].cfg1)
			return i;
	}

	return -EINVAL;
}

static unsigned long zx_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	int idx;

	idx = hw_to_idx(zx_pll);
	if (unlikely(idx == -EINVAL))
		return 0;

	return zx_pll->lookup_table[idx].rate;
}

static long zx_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *prate)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	int idx;

	idx = rate_to_idx(zx_pll, rate);

	return zx_pll->lookup_table[idx].rate;
}

static int zx_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	/* Assume current cpu is not running on current PLL */
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	const struct zx_pll_config *config;
	int idx;

	idx = rate_to_idx(zx_pll, rate);
	config = &zx_pll->lookup_table[idx];

	writel_relaxed(config->cfg0, zx_pll->reg_base);
	writel_relaxed(config->cfg1, zx_pll->reg_base + CFG0_CFG1_OFFSET);

	return 0;
}

static int zx_pll_enable(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);
	writel_relaxed(reg & ~BIT(zx_pll->pd_bit), zx_pll->reg_base);

	return readl_relaxed_poll_timeout(zx_pll->reg_base, reg,
					  reg & BIT(zx_pll->lock_bit), 0, 100);
}

static void zx_pll_disable(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);
	writel_relaxed(reg | BIT(zx_pll->pd_bit), zx_pll->reg_base);
}

static int zx_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);

	return !(reg & BIT(zx_pll->pd_bit));
}

const struct clk_ops zx_pll_ops = {
	.recalc_rate = zx_pll_recalc_rate,
	.round_rate = zx_pll_round_rate,
	.set_rate = zx_pll_set_rate,
	.enable = zx_pll_enable,
	.disable = zx_pll_disable,
	.is_enabled = zx_pll_is_enabled,
};
EXPORT_SYMBOL(zx_pll_ops);

struct clk *clk_register_zx_pll(const char *name, const char *parent_name,
				unsigned long flags, void __iomem *reg_base,
				const struct zx_pll_config *lookup_table,
				int count, spinlock_t *lock)
{
	struct clk_zx_pll *zx_pll;
	struct clk *clk;
	struct clk_init_data init;

	zx_pll = kzalloc(sizeof(*zx_pll), GFP_KERNEL);
	if (!zx_pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zx_pll_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	zx_pll->reg_base = reg_base;
	zx_pll->lookup_table = lookup_table;
	zx_pll->count = count;
	zx_pll->lock_bit = LOCK_FLAG;
	zx_pll->pd_bit = POWER_DOWN;
	zx_pll->lock = lock;
	zx_pll->hw.init = &init;

	clk = clk_register(NULL, &zx_pll->hw);
	if (IS_ERR(clk))
		kfree(zx_pll);

	return clk;
}

#define BPAR 1000000
static u32 calc_reg(u32 parent_rate, u32 rate)
{
	u32 sel, integ, fra_div, tmp;
	u64 tmp64 = (u64)parent_rate * BPAR;

	do_div(tmp64, rate);
	integ = (u32)tmp64 / BPAR;
	integ = integ >> 1;

	tmp = (u32)tmp64 % BPAR;
	sel = tmp / BPAR;

	tmp = tmp % BPAR;
	fra_div = tmp * 0xff / BPAR;
	tmp = (sel << 24) | (integ << 16) | (0xff << 8) | fra_div;

	/* Set I2S integer divider as 1. This bit is reserved for SPDIF
	 * and do no harm.
	 */
	tmp |= BIT(28);
	return tmp;
}

static u32 calc_rate(u32 reg, u32 parent_rate)
{
	u32 sel, integ, fra_div, tmp;
	u64 tmp64 = (u64)parent_rate * BPAR;

	tmp = reg;
	sel = (tmp >> 24) & BIT(0);
	integ = (tmp >> 16) & 0xff;
	fra_div = tmp & 0xff;

	tmp = fra_div * BPAR;
	tmp = tmp / 0xff;
	tmp += sel * BPAR;
	tmp += 2 * integ * BPAR;
	do_div(tmp64, tmp);

	return (u32)tmp64;
}

static unsigned long zx_audio_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_zx_audio *zx_audio = to_clk_zx_audio(hw);
	u32 reg;

	reg = readl_relaxed(zx_audio->reg_base);
	return calc_rate(reg, parent_rate);
}

static long zx_audio_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	u32 reg;

	if (rate * 2 > *prate)
		return -EINVAL;

	reg = calc_reg(*prate, rate);
	return calc_rate(reg, *prate);
}

static int zx_audio_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_zx_audio *zx_audio = to_clk_zx_audio(hw);
	u32 reg;

	reg = calc_reg(parent_rate, rate);
	writel_relaxed(reg, zx_audio->reg_base);

	return 0;
}

#define ZX_AUDIO_EN BIT(25)
static int zx_audio_enable(struct clk_hw *hw)
{
	struct clk_zx_audio *zx_audio = to_clk_zx_audio(hw);
	u32 reg;

	reg = readl_relaxed(zx_audio->reg_base);
	writel_relaxed(reg & ~ZX_AUDIO_EN, zx_audio->reg_base);
	return 0;
}

static void zx_audio_disable(struct clk_hw *hw)
{
	struct clk_zx_audio *zx_audio = to_clk_zx_audio(hw);
	u32 reg;

	reg = readl_relaxed(zx_audio->reg_base);
	writel_relaxed(reg | ZX_AUDIO_EN, zx_audio->reg_base);
}

static const struct clk_ops zx_audio_ops = {
	.recalc_rate = zx_audio_recalc_rate,
	.round_rate = zx_audio_round_rate,
	.set_rate = zx_audio_set_rate,
	.enable = zx_audio_enable,
	.disable = zx_audio_disable,
};

struct clk *clk_register_zx_audio(const char *name,
				  const char * const parent_name,
				  unsigned long flags,
				  void __iomem *reg_base)
{
	struct clk_zx_audio *zx_audio;
	struct clk *clk;
	struct clk_init_data init;

	zx_audio = kzalloc(sizeof(*zx_audio), GFP_KERNEL);
	if (!zx_audio)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zx_audio_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	zx_audio->reg_base = reg_base;
	zx_audio->hw.init = &init;

	clk = clk_register(NULL, &zx_audio->hw);
	if (IS_ERR(clk))
		kfree(zx_audio);

	return clk;
}
