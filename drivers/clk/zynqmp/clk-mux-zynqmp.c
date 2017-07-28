/*
 * Zynq UltraScale+ MPSoC mux
 *
 *  Copyright (C) 2016 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clk/zynqmp.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>

/*
 * DOC: basic adjustable multiplexer clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is only affected by parent switching.  No clk_set_rate support
 * parent - parent is adjustable through clk_set_parent
 */

#define to_clk_mux(_hw) container_of(_hw, struct clk_mux, hw)

static u8 zynqmp_clk_mux_get_parent(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 val;
	int ret;

	/*
	 * FIXME need a mux-specific flag to determine if val is bitwise or
	 * numeric e.g. sys_clkin_ck's clksel field is 3 bits wide, but ranges
	 * from 0x1 to 0x7 (index starts at one)
	 * OTOH, pmd_trace_clk_mux_ck uses a separate bit for each clock, so
	 * val = 0x4 really means "bit 2, index starts at bit 0"
	 */
	ret = zynqmp_pm_mmio_read((u32)(ulong)mux->reg, &val);
	if (ret)
		pr_warn_once("Read fail mux address: %x\n",
				(u32)(ulong)mux->reg);
	val = val >> mux->shift;
	val &= mux->mask;

	if (mux->table) {
		int i;

		for (i = 0; i < num_parents; i++)
			if (mux->table[i] == val)
				return i;
		return 0;
	}

	if (val && (mux->flags & CLK_MUX_INDEX_BIT))
		val = ffs(val) - 1;

	if (val && (mux->flags & CLK_MUX_INDEX_ONE))
		val--;

	return val;
}

static int zynqmp_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val;
	int ret;

	if (mux->table) {
		index = mux->table[index];
	} else {
		if (mux->flags & CLK_MUX_INDEX_BIT)
			index = 1 << index;

		if (mux->flags & CLK_MUX_INDEX_ONE)
			index++;
	}

	if (mux->flags & CLK_MUX_HIWORD_MASK) {
		val = mux->mask << (mux->shift + 16);
	} else {
		ret = zynqmp_pm_mmio_read((u32)(ulong)mux->reg, &val);
		if (ret)
			pr_warn_once("Read fail mux address: %x\n",
					(u32)(ulong)mux->reg);
		val &= ~(mux->mask << mux->shift);
	}
	val |= index << mux->shift;
	ret = zynqmp_pm_mmio_writel(val, mux->reg);
	if (ret)
		pr_warn_once("Write failed to mux address:%x\n",
				(u32)(ulong)mux->reg);

	return 0;
}

const struct clk_ops zynqmp_clk_mux_ops = {
	.get_parent = zynqmp_clk_mux_get_parent,
	.set_parent = zynqmp_clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(zynqmp_clk_mux_ops);

const struct clk_ops zynqmp_clk_mux_ro_ops = {
	.get_parent = zynqmp_clk_mux_get_parent,
};
EXPORT_SYMBOL_GPL(zynqmp_clk_mux_ro_ops);

struct clk *zynqmp_clk_register_mux_table(struct device *dev, const char *name,
		const char * const *parent_names, u8 num_parents,
		unsigned long flags,
		resource_size_t *reg, u8 shift, u32 mask,
		u8 clk_mux_flags, u32 *table)
{
	struct clk_mux *mux;
	struct clk *clk;
	struct clk_init_data init;
	u8 width = 0;

	if (clk_mux_flags & CLK_MUX_HIWORD_MASK) {
		width = fls(mask) - ffs(mask) + 1;
		if (width + shift > 16) {
			pr_err("mux value exceeds LOWORD field\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* allocate the mux */
	mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	if (clk_mux_flags & CLK_MUX_READ_ONLY)
		init.ops = &zynqmp_clk_mux_ro_ops;
	else
		init.ops = &zynqmp_clk_mux_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	/* struct clk_mux assignments */
	mux->reg = reg;
	mux->shift = shift;
	mux->mask = mask;
	mux->flags = clk_mux_flags;
	mux->table = table;
	mux->hw.init = &init;

	clk = clk_register(dev, &mux->hw);

	if (IS_ERR(clk))
		kfree(mux);

	return clk;
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_mux_table);

struct clk *zynqmp_clk_register_mux(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents,
		unsigned long flags,
		resource_size_t *reg, u8 shift, u8 width,
		u8 clk_mux_flags)
{
	u32 mask = BIT(width) - 1;

	return zynqmp_clk_register_mux_table(dev, name, parent_names,
					num_parents, flags, reg, shift, mask,
					clk_mux_flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_mux);
