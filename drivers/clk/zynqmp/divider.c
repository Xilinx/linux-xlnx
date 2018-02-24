// SPDX-License-Identifier: GPL-2.0+
/*
 * Zynq UltraScale+ MPSoC Divider support
 *
 *  Copyright (C) 2016-2018 Xilinx
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

#define to_zynqmp_clk_divider(_hw)		\
	container_of(_hw, struct zynqmp_clk_divider, hw)

/**
 * struct zynqmp_clk_divider - adjustable divider clock
 *
 * @hw:	handle between common and hardware-specific interfaces
 * @flags: Hardware specific flags
 * @clk_id: Id of clock
 * @div_type: divisor type (TYPE_DIV1 or TYPE_DIV2)
 */
struct zynqmp_clk_divider {
	struct clk_hw hw;
	u8 flags;
	u32 clk_id;
	u32 div_type;
};

static int zynqmp_divider_get_val(unsigned long parent_rate, unsigned long rate)
{
	return DIV_ROUND_CLOSEST(parent_rate, rate);
}

static unsigned long zynqmp_clk_divider_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 div, value;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getdivider)
		return -ENXIO;

	ret = eemi_ops->clock_getdivider(clk_id, &div);

	if (ret)
		pr_warn_once("%s() get divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	if (div_type == TYPE_DIV1)
		value = div & 0xFFFF;
	else
		value = (div >> 16) & 0xFFFF;

	if (!value) {
		WARN(!(divider->flags & CLK_DIVIDER_ALLOW_ZERO),
		     "%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
		     clk_name);
		return parent_rate;
	}

	return DIV_ROUND_UP_ULL(parent_rate, value);
}

static long zynqmp_clk_divider_round_rate(struct clk_hw *hw,
					  unsigned long rate,
					  unsigned long *prate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 bestdiv;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getdivider)
		return -ENXIO;

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		ret = eemi_ops->clock_getdivider(clk_id, &bestdiv);

		if (ret)
			pr_warn_once("%s() get divider failed for %s, ret = %d\n",
				     __func__, clk_name, ret);
		if (div_type == TYPE_DIV1)
			bestdiv = bestdiv & 0xFFFF;
		else
			bestdiv  = (bestdiv >> 16) & 0xFFFF;

		return DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	}

	bestdiv = zynqmp_divider_get_val(*prate, rate);

	if ((clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) &&
	    ((clk_hw_get_flags(hw) & CLK_FRAC)))
		bestdiv = rate % *prate ? 1 : bestdiv;
	*prate = rate * bestdiv;

	return rate;
}

/**
 * zynqmp_clk_divider_set_rate - Set rate of divider clock
 * @hw:	handle between common and hardware-specific interfaces
 * @rate: rate of clock to be set
 * @parent_rate: rate of parent clock
 *
 * Return: 0 always
 */
static int zynqmp_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 value, div;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_setdivider)
		return -ENXIO;

	value = zynqmp_divider_get_val(parent_rate, rate);
	if (div_type == TYPE_DIV1) {
		div = value & 0xFFFF;
		div |= ((u16)-1) << 16;
	} else {
		div = ((u16)-1);
		div |= value << 16;
	}

	ret = eemi_ops->clock_setdivider(clk_id, div);

	if (ret)
		pr_warn_once("%s() set divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return 0;
}

static const struct clk_ops zynqmp_clk_divider_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
	.set_rate = zynqmp_clk_divider_set_rate,
};

/**
 * _register_divider - register a divider clock
 * @dev: device registering this clock
 * @name: name of this clock
 * @clk_id: Id of clock
 * @div_type: Type of divisor
 * @parents: name of clock's parents
 * @num_parents: number of parents
 * @flags: framework-specific flags
 * @clk_divider_flags: divider-specific flags for this clock
 *
 * Return: handle to registered clock divider
 */
static struct clk *_register_divider(struct device *dev, const char *name,
				     u32 clk_id, u32 div_type,
				     const char * const *parents,
				     u8 num_parents, unsigned long flags,
				     u8 clk_divider_flags)
{
	struct zynqmp_clk_divider *div;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_divider_ops;
	init.flags = flags;
	init.parent_names = parents;
	init.num_parents = num_parents;

	/* struct clk_divider assignments */
	div->flags = clk_divider_flags;
	div->hw.init = &init;
	div->clk_id = clk_id;
	div->div_type = div_type;

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
 * @clk_id: Id of clock
 * @div_type: Type of divisor
 * @parents: name of clock's parents
 * @num_parents: number of parents
 * @flags: framework-specific flags
 * @clk_divider_flags: divider-specific flags for this clock
 *
 * Return: handle to registered clock divider
 */
struct clk *zynqmp_clk_register_divider(struct device *dev, const char *name,
					u32 clk_id, u32 div_type,
					const char * const *parents,
					u8 num_parents, unsigned long flags,
					u8 clk_divider_flags)
{
	return _register_divider(dev, name, clk_id, div_type, parents,
				 num_parents, flags, clk_divider_flags);
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_divider);
