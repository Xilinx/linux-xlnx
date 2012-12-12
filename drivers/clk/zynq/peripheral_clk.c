/**
 * Xilinx Zynq Clock Implementations for Peripheral clocks.
 *
 * zynq_periphclk_* where * is one of:
 * d1m: 1 divisor register, muxable
 * d2m: 2 divisor registers, muxable
 * gd1m: 1 divisor register, muxable, gateable
 * gd2m: 2 divisor registers, muxable, gateable
 *
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
 * struct zynq_periph_clk
 * @hw:		handle between common and hardware-specific interfaces
 * @clkctrl:	CLK control register
 * @lock:	register lock
 */
struct zynq_periph_clk {
	struct clk_hw	hw;
	void __iomem	*clkctrl;
	spinlock_t	*lock;
};
#define to_zynq_periph_clk(_hw)\
	container_of(_hw, struct zynq_periph_clk, hw)
#define CLKCTRL_DIV_MASK	0x3f00
#define CLKCTRL_DIV_SHIFT	8
#define CLKCTRL_DIV1_MASK	CLKCTRL_DIV_MASK
#define CLKCTRL_DIV1_SHIFT	CLKCTRL_DIV_SHIFT
#define CLKCTRL_DIV2_MASK	0x3f00000
#define CLKCTRL_DIV2_SHIFT	20
/*
 * This is a hack: We have clocks with 0 - 3 bit muxes. If present they start
 * all in the corresponding clk_ctrl reg. If narrower than 3 bits the bit fiels
 * is write ignore/read zero. Alternatively we could save the mask and shift
 * values in the zynq_periph_clk_* struct, like the clk-mux implementation.
 */
#define CLKCTRL_CLKSRC_MASK	0x70
#define CLKCTRL_CLKSRC_SHIFT	4
#define CLKCTRL_ENABLE_MASK	1
#define CLKCTRL_ENABLE_SHIFT	0

/* Clock gating ops for peripheral clocks featuring 1 gate */
/**
 * zynq_periphclk_gate1_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static int zynq_periphclk_gate1_enable(struct clk_hw *hw)
{
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	spin_lock_irqsave(clk->lock, flags);
	reg = readl(clk->clkctrl);
	reg |= CLKCTRL_ENABLE_MASK;
	writel(reg, clk->clkctrl);
	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_periphclk_gate1_disable - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static void zynq_periphclk_gate1_disable(struct clk_hw *hw)
{
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	spin_lock_irqsave(clk->lock, flags);
	reg = readl(clk->clkctrl);
	reg &= ~CLKCTRL_ENABLE_MASK;
	writel(reg, clk->clkctrl);
	spin_unlock_irqrestore(clk->lock, flags);
}

/**
 * zynq_periphclk_gate1_is_enabled - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 1 if the clock is enabled, 0 otherwise.
 */
static int zynq_periphclk_gate1_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	/* do we need lock for read? */
	spin_lock_irqsave(clk->lock, flags);
	reg = readl(clk->clkctrl);
	spin_unlock_irqrestore(clk->lock, flags);

	return (reg & CLKCTRL_ENABLE_MASK) >> CLKCTRL_ENABLE_SHIFT;
}

/* Rate set/get functions for peripheral clocks with a single divisor */
/**
 * zynq_periphclk_div1_set_rate() - Change clock frequncy
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns 0 on success, negative errno otherwise.
 */
static int zynq_periphclk_div1_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long prate)
{
	u32 div = DIV_ROUND_CLOSEST(prate, rate);
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	if ((div < 1) || (div > 0x3f))
		return -EINVAL;

	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->clkctrl);
	reg &= ~CLKCTRL_DIV_MASK;
	reg |= div << CLKCTRL_DIV_SHIFT;
	writel(reg, clk->clkctrl);

	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_periphclk_div1_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long zynq_periphclk_div1_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *prate)
{
	long div;

	div = DIV_ROUND_CLOSEST(*prate, rate);
	if (div < 1)
		div = 1;
	if (div > 0x3f)
		div = 0x3f;

	return *prate / div;
}

/**
 * zynq_periphclk_div1_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long zynq_periphclk_div1_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	u32 div;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	/*
	 * makes probably sense to redundantly save div in the struct
	 * zynq_periphclk_gd1m to save the IO access. Do we need spinlock for
	 * read access?
	 */
	div = (readl(clk->clkctrl) & CLKCTRL_DIV_MASK) >> CLKCTRL_DIV_SHIFT;
	if (div < 1)
		div = 1;

	return parent_rate / div;
}

/* Rate set/get functions for peripheral clocks with two divisors */
/**
 * zynq_periphclk_get_best_divs2 - Calculate best divisors values
 * @inputrate: Clock input frequency
 * @targetrate: Desired output frequency
 * @div1: Value for divisor 1 (return value)
 * @div2: Value for divisor 2 (return value)
 * Returns the resulting frequency or zero if no valid divisors are found.
 *
 * Calculate the best divisors values to achieve a given target frequency for a
 * given input frequency for clocks with two dividers fields
 */
static unsigned long
zynq_periphclk_get_best_divs2(const unsigned long inputrate,
		const unsigned long targetrate, u32 *div1, u32 *div2)
{
	u32 d1;
	u32 d2;
	unsigned long calcrate;
	unsigned long bestrate = 0;
	unsigned int error;
	unsigned int besterror = ~0;

	/* Probably micro-optimizing, but probably worth thinking about reducing
	 * the iterations and/or getting rid of some divisions */
	for (d1 = 1; d1 <= 0x3f; d1++) {
		d2 = DIV_ROUND_CLOSEST(inputrate / d1, targetrate);
		if ((d2 < 1) || (d2 > 0x3f))
			continue;
		calcrate = (inputrate / d1) / d2;

		if (calcrate > targetrate)
			error = calcrate - targetrate;
		else
			error = targetrate - calcrate;

		if (error < besterror) {
			*div1 = d1;
			*div2 = d2;
			bestrate = calcrate;
			besterror = error;
		}
	}

	return bestrate;
}

/**
 * zynq_periphclk_div2_set_rate() - Change clock frequncy
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns 0 on success, negative errno otherwise.
 */
static int zynq_periphclk_div2_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long prate)
{
	u32 div1;
	u32 div2;
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	if (!zynq_periphclk_get_best_divs2(prate, rate, &div1, &div2))
		return -EINVAL;

	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->clkctrl);
	reg &= ~CLKCTRL_DIV1_MASK;
	reg &= ~CLKCTRL_DIV2_MASK;
	reg |= div1 << CLKCTRL_DIV1_SHIFT;
	reg |= div2 << CLKCTRL_DIV2_SHIFT;
	writel(reg, clk->clkctrl);

	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_periphclk_div2_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long zynq_periphclk_div2_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *prate)
{
	u32 div1;
	u32 div2;
	long ret;

	ret = zynq_periphclk_get_best_divs2(*prate, rate, &div1, &div2);
	if (!ret)
		return -EINVAL;

	return ret;
}

/**
 * zynq_periphclk_div1_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long zynq_periphclk_div2_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	u32 div1;
	u32 div2;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	/*
	 * makes probably sense to redundantly save div in the struct
	 * zynq_periphclk_gd1m to save the IO access. Should we use spinlock for
	 * reading?
	 */
	div1 = (readl(clk->clkctrl) & CLKCTRL_DIV1_MASK) >> CLKCTRL_DIV1_SHIFT;
	div2 = (readl(clk->clkctrl) & CLKCTRL_DIV2_MASK) >> CLKCTRL_DIV2_SHIFT;
	if (div1 < 1)
		div1 = 1;
	if (div2 < 1)
		div2 = 1;

	return (parent_rate / div1) / div2;
}

/* Muxing functions for peripheral clocks */
/**
 * zynq_periphclk_set_parent() - Reparent clock
 * @hw:		Handle between common and hardware-specific interfaces
 * @index:	Index of new parent.
 * Returns 0 on success, negative errno otherwise.
 */
static int zynq_periphclk_set_parent(struct clk_hw *hw, u8 index)
{
	u32 reg;
	unsigned long flags = 0;
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	spin_lock_irqsave(clk->lock, flags);

	reg = readl(clk->clkctrl);
	reg &= ~CLKCTRL_CLKSRC_MASK;
	reg |= index << CLKCTRL_CLKSRC_SHIFT;
	writel(reg, clk->clkctrl);

	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_periphclk_get_parent() - Reparent clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns the index of the current clock parent.
 */
static u8 zynq_periphclk_get_parent(struct clk_hw *hw)
{
	struct zynq_periph_clk *clk =
		to_zynq_periph_clk(hw);

	return (readl(clk->clkctrl) & CLKCTRL_CLKSRC_MASK) >>
		CLKCTRL_CLKSRC_SHIFT;
}

/* Clk register functions */
/**
 * clk_register_zynq_common() - Register a clock with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @ops:	Pointer to the struct clock_ops
 * @pnames:	Array of names of clock parents
 * @num_parents:Number of parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
static struct clk *clk_register_zynq_common(const char *name,
		void __iomem *clkctrl, const struct clk_ops *ops,
		const char **pnames, u8 num_parents, spinlock_t *lock)
{
	struct clk *ret;
	struct zynq_periph_clk *clk;
	struct clk_init_data initd = {
		.name = name,
		.ops = ops,
		.parent_names = pnames,
		.num_parents = num_parents,
		.flags = 0
	};

	clk = kmalloc(sizeof(*clk), GFP_KERNEL);
	clk->hw.init = &initd;

	if (!clk) {
		pr_err("%s: could not allocate Zynq clock\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* Populate the struct */
	clk->clkctrl = clkctrl;
	clk->lock = lock;

	ret = clk_register(NULL, &clk->hw);
	if (IS_ERR(ret))
		kfree(clk);

	return ret;
}

/* Clock ops structs for the different peripheral clock types */
static const struct clk_ops zynq_periphclk_gd1m_ops = {
	.enable = zynq_periphclk_gate1_enable,
	.disable = zynq_periphclk_gate1_disable,
	.is_enabled = zynq_periphclk_gate1_is_enabled,
	.set_parent = zynq_periphclk_set_parent,
	.get_parent = zynq_periphclk_get_parent,
	.set_rate = zynq_periphclk_div1_set_rate,
	.round_rate = zynq_periphclk_div1_round_rate,
	.recalc_rate = zynq_periphclk_div1_recalc_rate
};

static const struct clk_ops zynq_periphclk_gd2m_ops = {
	.enable = zynq_periphclk_gate1_enable,
	.disable = zynq_periphclk_gate1_disable,
	.is_enabled = zynq_periphclk_gate1_is_enabled,
	.set_parent = zynq_periphclk_set_parent,
	.get_parent = zynq_periphclk_get_parent,
	.set_rate = zynq_periphclk_div2_set_rate,
	.round_rate = zynq_periphclk_div2_round_rate,
	.recalc_rate = zynq_periphclk_div2_recalc_rate
};

static const struct clk_ops zynq_periphclk_d2m_ops = {
	.set_parent = zynq_periphclk_set_parent,
	.get_parent = zynq_periphclk_get_parent,
	.set_rate = zynq_periphclk_div2_set_rate,
	.round_rate = zynq_periphclk_div2_round_rate,
	.recalc_rate = zynq_periphclk_div2_recalc_rate
};

static const struct clk_ops zynq_periphclk_d1m_ops = {
	.set_parent = zynq_periphclk_set_parent,
	.get_parent = zynq_periphclk_get_parent,
	.set_rate = zynq_periphclk_div1_set_rate,
	.round_rate = zynq_periphclk_div1_round_rate,
	.recalc_rate = zynq_periphclk_div1_recalc_rate
};

/* Clock register functions for the different peripheral clock types */
/**
 * clk_register_zynq_gd1m() - Register a gd1m clock with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @pnames:	Array of names of clock parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_gd1m(const char *name,
		void __iomem *clkctrl, const char **pnames, spinlock_t *lock)
{
	return clk_register_zynq_common(name, clkctrl, &zynq_periphclk_gd1m_ops,
			pnames, 4, lock);
}

/**
 * clk_register_zynq_gd2m() - Register a gd2m clock with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @pnames:	Array of names of clock parents
 * @num_parents:Number of parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_gd2m(const char *name,
		void __iomem *clkctrl, const char **pnames, u8 num_parents,
		spinlock_t *lock)
{
	return clk_register_zynq_common(name, clkctrl, &zynq_periphclk_gd2m_ops,
			pnames, num_parents, lock);
}

/**
 * clk_register_zynq_d2m() - Register a d2m clock with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @pnames:	Array of names of clock parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_d2m(const char *name,
		void __iomem *clkctrl, const char **pnames, spinlock_t *lock)
{
	return clk_register_zynq_common(name, clkctrl, &zynq_periphclk_d2m_ops,
			pnames, 4, lock);
}

/**
 * clk_register_zynq_d1m() - Register a d1m clock with the clock framework
 * @name:	Clock name
 * @clkctrl:	Pointer to clock control register
 * @pnames:	Array of names of clock parents
 * @num_parents:Number of parents
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_d1m(const char *name,
		void __iomem *clkctrl, const char **pnames, u8 num_parents,
		spinlock_t *lock)
{
	return clk_register_zynq_common(name, clkctrl, &zynq_periphclk_d1m_ops,
			pnames, num_parents, lock);
}
