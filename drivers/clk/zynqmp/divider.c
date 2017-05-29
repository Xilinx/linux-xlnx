/*
 * Zynq UltraScale+ MPSoC Divider support
 *
 *  Copyright (C) 2016 Xilinx
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adjustable divider clock implementation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/zynqmp.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/log2.h>

/*
 * DOC: basic adjustable divider clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = ceiling(parent->rate / divisor)
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_divider(_hw) container_of(_hw, struct clk_divider, hw)

#define div_mask(width)	((1 << (width)) - 1)

static unsigned int _get_table_div(const struct clk_div_table *table,
							unsigned int val)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->val == val)
			return clkt->div;
	return 0;
}

static unsigned int _get_div(const struct clk_div_table *table,
			     unsigned int val, unsigned long flags, u8 width)
{
	if (flags & CLK_DIVIDER_ONE_BASED)
		return val;
	if (flags & CLK_DIVIDER_POWER_OF_TWO)
		return 1 << val;
	if (flags & CLK_DIVIDER_MAX_AT_ZERO)
		return val ? val : div_mask(width) + 1;
	if (table)
		return _get_table_div(table, val);
	return val + 1;
}

static unsigned long zynqmp_clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int val;
	int ret;

	ret = zynqmp_pm_mmio_read((u32)(ulong)divider->reg, &val);
	if (ret)
		pr_warn_once("Read fail divider address: %x\n",
				(u32)(ulong)divider->reg);

	val = val >> divider->shift;
	val &= div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags);
}

static long zynqmp_clk_divider_round_rate(struct clk_hw *hw,
				unsigned long rate, unsigned long *prate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int bestdiv;

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		bestdiv = readl(divider->reg) >> divider->shift;
		bestdiv &= div_mask(divider->width);
		bestdiv = _get_div(divider->table, bestdiv, divider->flags,
			divider->width);
		return DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	}

	bestdiv = divider_get_val(rate, *prate, divider->table, divider->width,
			divider->flags);

	if ((clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) &&
	    ((clk_hw_get_flags(hw) & CLK_FRAC)))
		bestdiv = rate % *prate ? 1 : bestdiv;
	*prate = rate * bestdiv;

	return rate;
}

static int zynqmp_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int value;
	u32 val;
	int ret;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);

	if (divider->flags & CLK_DIVIDER_HIWORD_MASK) {
		val = div_mask(divider->width) << (divider->shift + 16);
	} else {
		ret = zynqmp_pm_mmio_read((u32)(ulong)divider->reg, &val);
		if (ret)
			pr_warn_once("Read fail divider address: %x\n",
					(u32)(ulong)divider->reg);
		val &= ~(div_mask(divider->width) << divider->shift);
	}
	val |= value << divider->shift;
	ret = zynqmp_pm_mmio_writel(val, divider->reg);
	if (ret)
		pr_warn_once("Write failed to divider address:%x\n",
				(u32)(ulong)divider->reg);

	return 0;
}

const struct clk_ops zynqmp_clk_divider_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
	.set_rate = zynqmp_clk_divider_set_rate,
};

static struct clk *_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		resource_size_t *reg, u8 shift, u8 width,
		u8 clk_divider_flags, const struct clk_div_table *table)
{
	struct clk_divider *div;
	struct clk *clk;
	struct clk_init_data init;

	if (clk_divider_flags & CLK_DIVIDER_HIWORD_MASK) {
		if (width + shift > 16) {
			pr_warn("divider value exceeds LOWORD field\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->hw.init = &init;
	div->table = table;

	/* register the clock */
	clk = clk_register(dev, &div->hw);

	if (IS_ERR(clk))
		kfree(div);

	return clk;
}

/**
 * zynqmp_clk_register_divider - register a divider clock
 * @dev: device registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @flags: framework-specific flags
 * @reg: register address to adjust divider
 * @shift: number of bits to shift the bitfield
 * @width: width of the bitfield
 * @clk_divider_flags: divider-specific flags for this clock
 *
 * Return: handle to registered clock divider
 */
struct clk *zynqmp_clk_register_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		resource_size_t *reg, u8 shift, u8 width,
		u8 clk_divider_flags)
{
	return _register_divider(dev, name, parent_name, flags, reg, shift,
			width, clk_divider_flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_divider);
