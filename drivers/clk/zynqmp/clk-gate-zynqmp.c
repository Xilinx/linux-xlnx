/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 * DOC: basic gatable clock which can gate and ungate it's output
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

/*
 * It works on following logic:
 *
 * For enabling clock, enable = 1
 *	set2dis = 1	-> clear bit	-> set = 0
 *	set2dis = 0	-> set bit	-> set = 1
 *
 * For disabling clock, enable = 0
 *	set2dis = 1	-> set bit	-> set = 1
 *	set2dis = 0	-> clear bit	-> set = 0
 *
 * So, result is always: enable xor set2dis.
 */
static void clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct clk_gate *gate = to_clk_gate(hw);
	int set = gate->flags & CLK_GATE_SET_TO_DISABLE ? 1 : 0;
	u32 reg;
	int ret;

	set ^= enable;

	if (gate->flags & CLK_GATE_HIWORD_MASK) {
		reg = BIT(gate->bit_idx + 16);
	} else {
		ret = zynqmp_pm_mmio_read((u32)(ulong)gate->reg, &reg);
		if (ret)
			pr_warn_once("Read fail gate address: %x\n",
					(u32)(ulong)gate->reg);

		if (!set)
			reg &= ~BIT(gate->bit_idx);
	}

	if (set)
		reg |= BIT(gate->bit_idx);
	ret = zynqmp_pm_mmio_writel(reg, gate->reg);
	if (ret)
		pr_warn_once("Write failed gate address:%x\n", (u32)(ulong)reg);
}

static int zynqmp_clk_gate_enable(struct clk_hw *hw)
{
	clk_gate_endisable(hw, 1);

	return 0;
}

static void zynqmp_clk_gate_disable(struct clk_hw *hw)
{
	clk_gate_endisable(hw, 0);
}

static int zynqmp_clk_gate_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	int ret;
	struct clk_gate *gate = to_clk_gate(hw);

	ret = zynqmp_pm_mmio_read((u32)(ulong)gate->reg, &reg);
	if (ret)
		pr_warn_once("Read failed gate address: %x\n",
				(u32)(ulong)gate->reg);

	/* if a set bit disables this clk, flip it before masking */
	if (gate->flags & CLK_GATE_SET_TO_DISABLE)
		reg ^= BIT(gate->bit_idx);

	reg &= BIT(gate->bit_idx);

	return reg ? 1 : 0;
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
 * @parent_name: name of this clock's parent
 * @flags: framework-specific flags for this clock
 * @reg: register address to control gating of this clock
 * @bit_idx: which bit in the register controls gating of this clock
 * @clk_gate_flags: gate-specific flags for this clock
 *
 * Return: clock handle of the registered clock gate
 */
struct clk *zynqmp_clk_register_gate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		resource_size_t *reg, u8 bit_idx,
		u8 clk_gate_flags)
{
	struct clk_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	if ((clk_gate_flags & CLK_GATE_HIWORD_MASK) && (bit_idx > 15)) {
		pr_err("gate bit exceeds LOWORD field\n");
		return ERR_PTR(-EINVAL);
	}

	/* allocate the gate */
	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_gate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = parent_name ? 1 : 0;

	/* struct clk_gate assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = clk_gate_flags;
	gate->hw.init = &init;

	clk = clk_register(dev, &gate->hw);

	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_gate);
