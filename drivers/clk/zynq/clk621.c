/*
 * A try to model the Zynq CPU_1X and CPU_2X clocks. These clocks depend on the
 * setting in the clk621_true register. They have the same clock parent
 * (CPU_MASTER_CLK/CPU_6OR4X) but a common 'divider'.
 *
 *  Copyright (C) 2012 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <mach/clk.h>

/**
 * struct zynq_clk621
 * @hw:		Handle between common and hardware-specific interfaces
 * @clkctrl:	Pointer to clock control register
 * @clk621:	Pointer to CLK_621_TRUE register
 * @basediv:	Base clock divider
 * @divadd:	Clock divider increment for 621 mode
 * @lock:	Register lock
 */
struct zynq_clk621 {
	struct clk_hw	hw;
	void __iomem	*clkctrl;
	void __iomem	*clk621;
	unsigned int	basediv;
	unsigned int	divadd;
	spinlock_t	*lock;
};
#define to_zynq_clk621(_hw) container_of(_hw,\
		struct zynq_clk621, hw);
#define CLK621_MASK 1
#define CLK621_SHIFT 0

/**
 * zynq_clk621_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long zynq_clk621_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	unsigned long rerror1;
	unsigned long rerror2;
	long rate1;
	long rate2;
	struct zynq_clk621 *clk = to_zynq_clk621(hw);

	rate1 = *prate / clk->basediv;
	rate2 = *prate / (clk->basediv + clk->divadd);

	if (rate1 > rate)
		rerror1 = rate1 - rate;
	else
		rerror1 = rate - rate1;
	if (rate2 > rate)
		rerror2 = rate2 - rate;
	else
		rerror2 = rate - rate1;
	if (rerror1 > rerror2)
		return rate2;
	else
		return rate1;
}

/**
 * zynq_clk621_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long zynq_clk621_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	unsigned int div;
	struct zynq_clk621 *clk = to_zynq_clk621(hw);

	div = clk->basediv;

	if ((readl(clk->clk621) & CLK621_MASK) >> CLK621_SHIFT)
		div += clk->divadd;
	return parent_rate / div;
}


/**
 * zynq_clk621_set_rate() - Change clock frequncy
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns 0 on success, negative errno otherwise.
 *
 * I doubt we can safely set a new rate. Changing the rate of one of these
 * clocks will also affect the other. We cannot model this kind of dependency on
 * the same hierarchical level
 */
static int zynq_clk621_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long prate)
{
	return -EINVAL;
}

static const struct clk_ops zynq_clk621_ops = {
	.set_rate = zynq_clk621_set_rate,
	.round_rate = zynq_clk621_round_rate,
	.recalc_rate = zynq_clk621_recalc_rate
};

/**
 * clk_register_zynq_clk621() - Register a clk621 with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @clk621:	Pointer to CLK_621_TRUE register
 * @basediv:	Base clock divider
 * @divadd:	Clock divider increment for 621 mode
 * @pnames:	Array of names of clock parents
 * @num_parents:Number of parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_clk621(const char *name,
		void __iomem *clkctrl, void __iomem *clk621,
		unsigned int basediv,
		unsigned int divadd, const char **pnames, u8 num_parents,
		spinlock_t *lock)
{
	struct clk *ret;
	struct zynq_clk621 *clk;
	struct clk_init_data initd = {
		.name = name,
		.ops = &zynq_clk621_ops,
		.parent_names = pnames,
		.num_parents = num_parents,
		.flags = 0
	};

	clk = kmalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk) {
		pr_err("%s: could not allocate Zynq clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* Populate the struct */
	clk->hw.init = &initd;
	clk->clkctrl = clkctrl;
	clk->clk621 = clk621;
	clk->basediv = basediv;
	clk->divadd = divadd;
	clk->lock = lock;

	ret = clk_register(NULL, &clk->hw);

	if (IS_ERR(ret))
		kfree(clk);

	return ret;
}
