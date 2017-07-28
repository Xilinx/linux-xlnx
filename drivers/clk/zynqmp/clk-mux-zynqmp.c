// SPDX-License-Identifier: GPL-2.0+
/*
 * Zynq UltraScale+ MPSoC mux
 *
 *  Copyright (C) 2016-2018 Xilinx
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

/**
 * struct zynqmp_clk_mux - multiplexer clock
 *
 * @hw: handle between common and hardware-specific interfaces
 * @flags: hardware-specific flags
 * @clk_id: Id of clock
 */
struct zynqmp_clk_mux {
	struct clk_hw hw;
	u8 flags;
	u32 clk_id;
};

#define to_zynqmp_clk_mux(_hw) container_of(_hw, struct zynqmp_clk_mux, hw)

/**
 * zynqmp_clk_mux_get_parent - Get parent of clock
 * @hw: handle between common and hardware-specific interfaces
 *
 * Return: Parent index
 */
static u8 zynqmp_clk_mux_get_parent(struct clk_hw *hw)
{
	struct zynqmp_clk_mux *mux = to_zynqmp_clk_mux(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = mux->clk_id;
	u32 val;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getparent)
		return -ENXIO;

	ret = eemi_ops->clock_getparent(clk_id, &val);

	if (ret)
		pr_warn_once("%s() getparent failed for clock: %s, ret = %d\n",
			     __func__, clk_name, ret);

	if (val && (mux->flags & CLK_MUX_INDEX_BIT))
		val = ffs(val) - 1;

	if (val && (mux->flags & CLK_MUX_INDEX_ONE))
		val--;

	return val;
}

/**
 * zynqmp_clk_mux_set_parent - Set parent of clock
 * @hw: handle between common and hardware-specific interfaces
 * @index: Parent index
 *
 * Return: 0 always
 */
static int zynqmp_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct zynqmp_clk_mux *mux = to_zynqmp_clk_mux(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = mux->clk_id;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_setparent)
		return -ENXIO;

	if (mux->flags & CLK_MUX_INDEX_BIT)
		index = 1 << index;

	if (mux->flags & CLK_MUX_INDEX_ONE)
		index++;

	ret = eemi_ops->clock_setparent(clk_id, index);

	if (ret)
		pr_warn_once("%s() set parent failed for clock: %s, ret = %d\n",
			     __func__, clk_name, ret);

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

/**
 * zynqmp_clk_register_mux_table - register a mux table with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @clk_id: Id of this clock
 * @parent_names: name of this clock's parents
 * @num_parents: number of parents
 * @flags: framework-specific flags for this clock
 * @clk_mux_flags: mux-specific flags for this clock
 *
 * Return: clock handle of the registered clock mux
 */
struct clk *zynqmp_clk_register_mux_table(struct device *dev, const char *name,
					  u32 clk_id,
					  const char * const *parent_names,
					  u8 num_parents,
					  unsigned long flags,
					  u8 clk_mux_flags)
{
	struct zynqmp_clk_mux *mux;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the mux */
	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	if (clk_mux_flags & CLK_MUX_READ_ONLY)
		init.ops = &zynqmp_clk_mux_ro_ops;
	else
		init.ops = &zynqmp_clk_mux_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	/* struct clk_mux assignments */
	mux->flags = clk_mux_flags;
	mux->hw.init = &init;
	mux->clk_id = clk_id;

	clk = clk_register(dev, &mux->hw);

	if (IS_ERR(clk))
		kfree(mux);

	return clk;
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_mux_table);

/**
 * zynqmp_clk_register_mux - register a mux clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @clk_id: Id of this clock
 * @parent_names: name of this clock's parents
 * @num_parents: number of parents
 * @flags: framework-specific flags for this clock
 * @clk_mux_flags: mux-specific flags for this clock
 *
 * Return: clock handle of the registered clock mux
 */
struct clk *zynqmp_clk_register_mux(struct device *dev, const char *name,
				    u32 clk_id, const char **parent_names,
				    u8 num_parents, unsigned long flags,
				    u8 clk_mux_flags)
{
	return zynqmp_clk_register_mux_table(dev, name, clk_id, parent_names,
					     num_parents, flags, clk_mux_flags);
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_mux);
