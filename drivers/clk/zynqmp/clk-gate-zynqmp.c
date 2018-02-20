// SPDX-License-Identifier: GPL-2.0+
/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016-2018 Xilinx
 *
 * Gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/clk/zynqmp.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>

/**
 * struct clk_gate - gating clock
 *
 * @hw:	handle between common and hardware-specific interfaces
 * @flags:	hardware-specific flags
 * @clk_id:	Id of clock
 */
struct zynqmp_clk_gate {
	struct clk_hw hw;
	u8 flags;
	u32 clk_id;
};

#define to_zynqmp_clk_gate(_hw) container_of(_hw, struct zynqmp_clk_gate, hw)

/**
 * zynqmp_clk_gate_enable - Enable clock
 * @hw: handle between common and hardware-specific interfaces
 *
 * Return: 0 always
 */
static int zynqmp_clk_gate_enable(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int ret = 0;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_enable)
		return -ENXIO;

	ret = eemi_ops->clock_enable(clk_id);

	if (ret)
		pr_warn_once("%s() clock enabled failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return 0;
}

/*
 * zynqmp_clk_gate_disable - Disable clock
 * @hw: handle between common and hardware-specific interfaces
 */
static void zynqmp_clk_gate_disable(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int ret = 0;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_disable)
		return;

	ret = eemi_ops->clock_disable(clk_id);

	if (ret)
		pr_warn_once("%s() clock disable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
}

/**
 * zynqmp_clk_gate_is_enable - Check clock state
 * @hw: handle between common and hardware-specific interfaces
 *
 * Return: 1 if enabled, 0 if disabled
 */
static int zynqmp_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int state, ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getstate)
		return 0;

	ret = eemi_ops->clock_getstate(clk_id, &state);
	if (ret)
		pr_warn_once("%s() clock get state failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return state ? 1 : 0;
}

const struct clk_ops zynqmp_clk_gate_ops = {
	.enable = zynqmp_clk_gate_enable,
	.disable = zynqmp_clk_gate_disable,
	.is_enabled = zynqmp_clk_gate_is_enabled,
};
EXPORT_SYMBOL_GPL(zynqmp_clk_gate_ops);

/**
 * zynqmp_clk_register_gate - register a gate clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @clk_id: Id of this clock
 * @parents: name of this clock's parents
 * @num_parents: number of parents
 * @flags: framework-specific flags for this clock
 * @clk_gate_flags: gate-specific flags for this clock
 *
 * Return: clock handle of the registered clock gate
 */
struct clk *zynqmp_clk_register_gate(struct device *dev, const char *name,
				     u32 clk_id, const char * const *parents,
				     u8 num_parents, unsigned long flags,
				     u8 clk_gate_flags)
{
	struct zynqmp_clk_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the gate */
	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_gate_ops;
	init.flags = flags;
	init.parent_names = parents;
	init.num_parents = num_parents;

	/* struct clk_gate assignments */
	gate->flags = clk_gate_flags;
	gate->hw.init = &init;
	gate->clk_id = clk_id;

	clk = clk_register(dev, &gate->hw);

	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
