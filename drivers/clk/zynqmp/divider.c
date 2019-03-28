// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC Divider support
 *
 *  Copyright (C) 2016-2018 Xilinx
 *
 * Adjustable divider clock implementation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include "clk-zynqmp.h"

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

#define CLK_FRAC	BIT(8) /* has a fractional parent */

/**
 * struct zynqmp_clk_divider - adjustable divider clock
 * @hw:		handle between common and hardware-specific interfaces
 * @flags:	Hardware specific flags
 * @clk_id:	Id of clock
 * @div_type:	divisor type (TYPE_DIV1 or TYPE_DIV2)
 * @max_div:	Maximum supported divisor
 */
struct zynqmp_clk_divider {
	struct clk_hw hw;
	u16 flags;
	u32 clk_id;
	u32 div_type;
	u32 max_div;
};

static inline int zynqmp_divider_get_val(unsigned long parent_rate,
					 unsigned long rate)
{
	return DIV_ROUND_CLOSEST(parent_rate, rate);
}

/**
 * zynqmp_clk_divider_recalc_rate() - Recalc rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @parent_rate:	rate of parent clock
 *
 * Return: 0 on success else error+reason
 */
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

	ret = eemi_ops->clock_getdivider(clk_id, &div);

	if (ret)
		pr_warn_once("%s() get divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	if (div_type == TYPE_DIV1)
		value = div & 0xFFFF;
	else
		value = div >> 16;

	if (!value) {
		WARN(!(divider->flags & CLK_DIVIDER_ALLOW_ZERO),
		     "%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
		     clk_name);
		return parent_rate;
	}

	return DIV_ROUND_UP_ULL(parent_rate, value);
}

static void zynqmp_compute_divider(struct clk_hw *hw,
				   unsigned long rate,
				   unsigned long parent_rate,
				   u32 max_div,
				   int *bestdiv)
{
	int div1;
	int div2;
	long error = LONG_MAX;
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);
	struct zynqmp_clk_divider *pdivider = to_zynqmp_clk_divider(parent_hw);

	if (!pdivider)
		return;

	*bestdiv = 1;
	for (div1 = 1; div1 <= pdivider->max_div; div1++) {
		for (div2 = 1; div2 <= max_div; div2++) {
			long new_error = ((parent_rate / div1) / div2) - rate;

			if (abs(new_error) < abs(error)) {
				*bestdiv = div2;
				error = new_error;
			}
		}
	}
}

/**
 * zynqmp_clk_divider_round_rate() - Round rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @rate:		rate of clock to be set
 * @prate:		rate of parent clock
 *
 * Return: 0 on success else error+reason
 */
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

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		ret = eemi_ops->clock_getdivider(clk_id, &bestdiv);

		if (ret)
			pr_warn_once("%s() get divider failed for %s, ret = %d\n",
				     __func__, clk_name, ret);
		if (div_type == TYPE_DIV1)
			bestdiv = bestdiv & 0xFFFF;
		else
			bestdiv  = bestdiv >> 16;

		return DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	}

	bestdiv = zynqmp_divider_get_val(*prate, rate);

	/*
	 * In case of two divisors, compute best divider values and return
	 * divider2 value based on compute value. div1 will  be automatically
	 * set to optimum based on required total divider value.
	 */
	if (bestdiv > divider->max_div && div_type == TYPE_DIV2 &&
	    (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		zynqmp_compute_divider(hw, rate, *prate,
				       divider->max_div, &bestdiv);
	}

	if ((clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) &&
	    (divider->flags & CLK_FRAC))
		bestdiv = rate % *prate ? 1 : bestdiv;
	*prate = rate * bestdiv;

	return rate;
}

/**
 * zynqmp_clk_divider_set_rate() - Set rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @rate:		rate of clock to be set
 * @parent_rate:	rate of parent clock
 *
 * Return: 0 on success else error+reason
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

	value = zynqmp_divider_get_val(parent_rate, rate);
	if (div_type == TYPE_DIV1) {
		div = value & 0xFFFF;
		div |= 0xffff << 16;
	} else {
		div = 0xffff;
		div |= value << 16;
	}

	ret = eemi_ops->clock_setdivider(clk_id, div);

	if (ret)
		pr_warn_once("%s() set divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return ret;
}

static const struct clk_ops zynqmp_clk_divider_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
	.set_rate = zynqmp_clk_divider_set_rate,
};

/**
 * zynqmp_clk_register_divider() - Register a divider clock
 * @name:		Name of this clock
 * @clk_id:		Id of clock
 * @parents:		Name of this clock's parents
 * @num_parents:	Number of parents
 * @nodes:		Clock topology node
 *
 * Return: clock hardware to registered clock divider
 */
struct clk_hw *zynqmp_clk_register_divider(const char *name,
					   u32 clk_id,
					   const char * const *parents,
					   u8 num_parents,
					   const struct clock_topology *nodes)
{
	struct zynqmp_clk_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_divider_ops;
	init.flags = nodes->flag;
	init.parent_names = parents;
	init.num_parents = 1;

	/* struct clk_divider assignments */
	div->flags = nodes->type_flag;
	div->hw.init = &init;
	div->clk_id = clk_id;
	div->div_type = nodes->type;

	/*
	 * To achieve best possible rate, maximum limit of divider is required
	 * while computation. Get maximum supported divisor from firmware. To
	 * maintain backward compatibility assign maximum possible value(0xFFFF)
	 * if query for max divisor is not successful.
	 */
	qdata.qid = PM_QID_CLOCK_GET_MAX_DIVISOR;
	qdata.arg1 = clk_id;
	qdata.arg2 = nodes->type;
	ret = eemi_ops->query_data(qdata, ret_payload);
	if (ret)
		div->max_div = 0XFFFF;
	else
		div->max_div = ret_payload[1];

	hw = &div->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_divider);
