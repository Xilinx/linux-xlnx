// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx 'Clocking Wizard' driver
 *
 *  Copyright (C) 2013 - 2014 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 */
#define DEBUG

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>

#define WZRD_MAX_OUTPUTS	8
#define WZRD_ACLK_MAX_FREQ	250000000UL

#define WZRD_CLK_CFG_REG(n)	(0x200 + 4 * (n))

#define WZRD_CLKOUT0_FRAC_EN	BIT(18)
#define WZRD_CLKFBOUT_FRAC_EN	BIT(26)

#define WZRD_CLKFBOUT_MULT_SHIFT	8
#define WZRD_CLKFBOUT_MULT_MASK		0xff
#define WZRD_CLKFBOUT_FRAC_SHIFT	16
#define WZRD_CLKFBOUT_FRAC_MASK		0x3ff
#define WZRD_DIVCLK_DIVIDE_SHIFT	0
#define WZRD_DIVCLK_DIVIDE_MASK		0xff
#define WZRD_CLKOUT_DIVIDE_SHIFT	0
#define WZRD_CLKOUT_DIVIDE_WIDTH	8
#define WZRD_CLKOUT_DIVIDE_MASK		0xff
#define WZRD_CLKOUT_FRAC_SHIFT		8
#define WZRD_CLKOUT_FRAC_MASK		0x3ff

#define WZRD_DR_MAX_INT_DIV_VALUE	255
#define WZRD_DR_NUM_RETRIES		10000
#define WZRD_DR_STATUS_REG_OFFSET	0x04
#define WZRD_DR_LOCK_BIT_MASK		0x00000001
#define WZRD_DR_INIT_REG_OFFSET		0x25C
#define WZRD_DR_DIV_TO_PHASE_OFFSET	4
#define WZRD_DR_BEGIN_DYNA_RECONF	0x03

/* Get the mask from width */
#define div_mask(width)			((1 << (width)) - 1)

/* Extract divider instance from clock hardware instance */
#define to_clk_wzrd_divider(_hw) container_of(_hw, struct clk_wzrd_divider, hw)

/* structure to store the hints as they are calculated and then used */
struct clk_wzrd_vco_hints {
	unsigned int req_rate;		/* requested rate */
	unsigned int best_rate;		/* best rate we can get */
	unsigned int vco_rate;		/* VCO rate used with hints */
	unsigned int divclk_divide;
	unsigned int clkfbout_mult_f;	/* mHz units */
	unsigned int clkout_divide;	/* mHz units */
	bool valid_rate;		/* indicates the hints calc is done */
};

/**
 * struct clk_wzrd - Clock wizard private data structure
 *
 * @clk_data:		Clock data
 * @nb:			Notifier block
 * @base:		Memory base
 * @clk_in1:		Handle to input clock 'clk_in1'
 * @axi_clk:		Handle to input clock 's_axi_aclk'
 * @num_out_clks	Read from devicetree
 * @clkout:		Output clocks
 * @vco_clk_hw		hw Voltage Controlled Oscilator clock
 * @hints		hints used for arbitrary frequency generation
 * @speed_grade:	Speed grade of the device
 * @suspended:		Flag indicating power state of the device
 */
struct clk_wzrd {
	struct clk_onecell_data clk_data;
	struct notifier_block nb;
	void __iomem *base;
	struct clk *clk_in1;
	struct clk *axi_clk;
	struct clk *vco_clk;
	unsigned int num_out_clks;
	struct clk *clkout[WZRD_MAX_OUTPUTS];
	struct clk_hw vco_clk_hw;
	struct clk_wzrd_vco_hints hints;
	unsigned int speed_grade;
	bool suspended;
	spinlock_t *lock;  /* lock */
};

/**
 * struct clk_wzrd_divider - clock divider specific to clk_wzrd
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @base:	base address of register containing the divider
 * @offset:	offset address of register containing the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @flags:	clk_wzrd divider flags
 * @table:	array of value/divider pairs, last entry should have div = 0
 * @lock:	register lock
 */
struct clk_wzrd_divider {
	struct clk_hw hw;
	void __iomem *base;
	u16 offset;
	u8 shift;
	u8 width;
	u8 flags;
	const struct clk_div_table *table;
	spinlock_t *lock;  /* divider lock */
};

#define to_clk_wzrd(_nb) container_of(_nb, struct clk_wzrd, nb)

/* maximum frequencies for input/output clocks per speed grade */
static const unsigned long clk_wzrd_max_freq[] = {
	800000000UL,
	933000000UL,
	1066000000UL
};

/* spin lock variable for clk_wzrd */
static DEFINE_SPINLOCK(clkwzrd_lock);

static unsigned long clk_wzrd_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);
	unsigned int val;

	val = readl(div_addr) >> divider->shift;
	val &= div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
			divider->flags, divider->width);
}

static int clk_wzrd_dynamic_reconfig(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	u32 value;
	unsigned long flags = 0;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	value = DIV_ROUND_CLOSEST(parent_rate, rate);

	/* Cap the value to max */
	if (value > WZRD_DR_MAX_INT_DIV_VALUE)
		value = WZRD_DR_MAX_INT_DIV_VALUE;

	/* Set divisor and clear phase offset */
	writel(value, div_addr);
	writel(0x00, div_addr + WZRD_DR_DIV_TO_PHASE_OFFSET);

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(divider->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (retries == 0) {
		err = -ETIMEDOUT;
		goto err_reconfig;
	}

	/* Initiate reconfiguration */
	writel(WZRD_DR_BEGIN_DYNA_RECONF,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(divider->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (retries == 0)
		err = -ETIMEDOUT;

err_reconfig:
	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return err;
}

static long clk_wzrd_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	u8 div;

	/*
	 * since we donot change parent rate we just round rate to closest
	 * achievable
	 */
	div = DIV_ROUND_CLOSEST(*prate, rate);

	return (*prate / div);
}

static const struct clk_ops clk_wzrd_clk_divider_ops = {
	.round_rate = clk_wzrd_round_rate,
	.set_rate = clk_wzrd_dynamic_reconfig,
	.recalc_rate = clk_wzrd_recalc_rate,
};

static unsigned long clk_wzrd_recalc_ratef(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	unsigned int val;
	u32 div, frac;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);

	val = readl(div_addr);
	div = val & div_mask(divider->width);
	frac = (val >> WZRD_CLKOUT_FRAC_SHIFT) & WZRD_CLKOUT_FRAC_MASK;

	return ((parent_rate * 1000) / ((div * 1000) + frac));
}

#define DIVCLK_DIVIDE_MAX_HW	       106U
#define MIN_IN_FREQ		  10000000U
#define MIN_VCO_FREQ		 800000000ULL /* VCO calculation needs u64 */
#define MAX_VCO_FREQ		1600000000ULL /* VCO calculation needs u64 */

/* Units of mHz because clkbout is in units of mHz */
#define CLKFBOUT_MULT_F_MIN	      2000U
#define CLKFBOUT_MULT_F_MAX	    128000U

#define CLKOUT_DIVIDE_F_MIN	      2000U
#define CLKOUT_DIVIDE_F_MAX	    128000U
static int clk_wzrd_dynamic_reconfig_f(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	u32 value, pre;
	unsigned long flags = 0;
	unsigned long rate_div, f, clockout0_div;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	struct clk_hw *vco_clk = clk_hw_get_parent(hw);
	struct clk_wzrd *clk_wzrd = container_of(vco_clk,
						 struct clk_wzrd,
						 vco_clk_hw);

	void __iomem *div_addr =
		       (void __iomem *)((u64)divider->base + divider->offset);

	/* check to see if we can use hints */
	if (clk_wzrd->hints.valid_rate &&
	    clk_wzrd->hints.best_rate == rate) {
		clockout0_div = clk_wzrd->hints.clkout_divide / 1000;
		f = clk_wzrd->hints.clkout_divide % 1000;
	} else { /* can't use hints, set the rate directly */
		rate_div = ((parent_rate * 1000) / rate);
		clockout0_div = rate_div / 1000;

		pre = DIV_ROUND_CLOSEST((parent_rate * 1000), rate);
		f = (u32)(pre - (clockout0_div * 1000));
		f = f & WZRD_CLKOUT_FRAC_MASK;
	}

	value = (f << WZRD_CLKOUT_DIVIDE_WIDTH) |
		((clockout0_div >> WZRD_CLKOUT_DIVIDE_SHIFT) &
		WZRD_CLKOUT_DIVIDE_MASK);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	/* Set divisor and clear phase offset */
	writel(value, div_addr);
	writel(0x0, div_addr + WZRD_DR_DIV_TO_PHASE_OFFSET);

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(divider->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (!retries) {
		err = -ETIMEDOUT;
		goto err_reconfig;
	}

	/* Initiate reconfiguration */
	writel(WZRD_DR_BEGIN_DYNA_RECONF,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(divider->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (!retries)
		err = -ETIMEDOUT;

err_reconfig:
	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return err;
}

/* This is where all the magic happens. For arbitrary frequency generation
 * applications all of the knobs for adjusting the frequency must be
 * considered together. Nothing is set in this function we just provide
 * hints for use by the actual set_rate functions (one for clkout0 and one
 * for the vco). If things don't match up the hints can always be ignored
 */
static void clk_wzrd_calc_hints(struct clk_wzrd *clk_wzrd, unsigned long rate,
				unsigned long in_rate)
{
	int i, j;

	unsigned int divclk_divide_max, divclk_divide_max_calc;
	unsigned int clkbout_min, clkbout_max;
	unsigned int clkout_divide = 1;
	unsigned int best_divclk = 1;
	unsigned int best_clkfbout_mult_f = 1;
	unsigned int best_clkout_divide = 1;
	unsigned int best_vco_freq = 1, best_out_freq = 1;
	u64 vco_freq;
	u64 out_freq = 1;
	u64 error;
	u64 min_error = 100000000000;

	clk_wzrd->hints.valid_rate = false;

	divclk_divide_max_calc = in_rate / MIN_IN_FREQ;
	divclk_divide_max = min(DIVCLK_DIVIDE_MAX_HW,
				divclk_divide_max_calc);

	/* first go through all possible DIVCLK_DIVIDE values */
	for (i = 1; i <= divclk_divide_max; i++) {
		/* 8*125 takes us to mHz */
		clkbout_min = DIV_ROUND_UP(MIN_VCO_FREQ * i * 8, in_rate) * 125;
		clkbout_max = (MAX_VCO_FREQ * i * 8 / in_rate) * 125;
		clkbout_min = max(CLKFBOUT_MULT_F_MIN, clkbout_min);
		clkbout_max = min(CLKFBOUT_MULT_F_MAX, clkbout_max);
		/* second go through all CLKFBOUT_MULT values */
		for (j = clkbout_min; j <= clkbout_max; j += 125) {
			vco_freq = in_rate * j / i;
			/* finally calculate out the CLKOUT_DIVIDE
			 * there is no need to iterate here
			 * just calculate the closest
			 */
			clkout_divide =
				DIV_ROUND_CLOSEST(vco_freq,
						  (u64)rate * 125) * 125;
			clkout_divide = clamp(clkout_divide,
					      CLKOUT_DIVIDE_F_MIN,
					      CLKOUT_DIVIDE_F_MAX);
			out_freq = in_rate * j / i / clkout_divide;
			error = abs(rate - out_freq);
			if (error < min_error) {
				min_error = error;
				best_vco_freq = vco_freq;
				best_out_freq = out_freq;
				best_divclk = i;
				best_clkfbout_mult_f = j;
				best_clkout_divide = clkout_divide;
			}
		}
	}

	clk_wzrd->hints.req_rate = rate;
	clk_wzrd->hints.vco_rate = best_vco_freq;
	clk_wzrd->hints.best_rate = best_out_freq;
	clk_wzrd->hints.divclk_divide = best_divclk;
	clk_wzrd->hints.clkfbout_mult_f = best_clkfbout_mult_f;
	clk_wzrd->hints.clkout_divide = best_clkout_divide;
	clk_wzrd->hints.valid_rate = true;

#ifdef DEBUG
	pr_info("clk_wzrd: calc results best match\n");
	pr_info("clk_wzrd: divclk: %u, clkfbout_mult: %u clkout_divide: %u\n",
		best_divclk, best_clkfbout_mult_f, best_clkout_divide);
	pr_info("clk_wzrd: out_freq: %u\n", best_out_freq);
#endif
}

/* This round_rate function of the divider is the first
 * function that gets hit when a driver calls clk_set_rate(), so this
 * is where ...calc_hints() is called from
 */
static long clk_wzrd_round_rate_f(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		/* We need a reference to the clk_wzrd
		 * to set the hints and get vco rate
		 */
		struct clk_hw *vco_clk = clk_hw_get_parent(hw);
		struct clk_wzrd *clk_wzrd = container_of(vco_clk,
							 struct clk_wzrd,
							 vco_clk_hw);

		/* during clock setup this could get called multiple times
		 * but we only want to go through the calculation once
		 */
		if (!clk_wzrd->hints.valid_rate ||
		    clk_wzrd->hints.req_rate != rate) {
			unsigned long in_rate;

			/* Actual input to the whole clocking wizard */
			in_rate = clk_hw_get_rate(clk_hw_get_parent(vco_clk));
			clk_wzrd_calc_hints(clk_wzrd, rate, in_rate);
		}
		*prate = clk_wzrd->hints.vco_rate;
		return clk_wzrd->hints.best_rate;
	}

	return rate;
}

static const struct clk_ops clk_wzrd_clk_divider_ops_f = {
	.round_rate = clk_wzrd_round_rate_f,
	.set_rate = clk_wzrd_dynamic_reconfig_f,
	.recalc_rate = clk_wzrd_recalc_ratef,
};

static unsigned long clk_wzrd_vco_recalc_ratef(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	u32 clk_cfg_reg0;
	u32 divclk_divide, clkfbout_mult, clkfbout_frac;
	u64 rate;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_hw);

	clk_cfg_reg0 = ioread32(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	divclk_divide = (clk_cfg_reg0 >> WZRD_DIVCLK_DIVIDE_SHIFT &
			 WZRD_DIVCLK_DIVIDE_MASK);
	clkfbout_mult = (clk_cfg_reg0 >> WZRD_CLKFBOUT_MULT_SHIFT &
			 WZRD_CLKFBOUT_MULT_MASK);
	clkfbout_frac = (clk_cfg_reg0 >> WZRD_CLKFBOUT_FRAC_SHIFT &
			 WZRD_CLKFBOUT_FRAC_MASK);

	rate = parent_rate *
	       (clkfbout_mult * 1000 + clkfbout_frac) / /* multilyer x1000 */
	       divclk_divide / 1000;

	return (unsigned long)rate;
}

static int clk_wzrd_vco_dynamic_reconfig_f(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	unsigned long flags = 0;
	u32 clk_cfg_reg0, axival;
	u32 divclk_divide, clkfbout_mult, clkfbout_frac;
	unsigned int new_mult;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_hw);

	if (clk_wzrd->hints.valid_rate && clk_wzrd->hints.vco_rate == rate) {
		/* divclk_divide gets set instead of being read */
		divclk_divide = clk_wzrd->hints.divclk_divide;
		new_mult = clk_wzrd->hints.clkfbout_mult_f;
	} else {
		clk_cfg_reg0 = ioread32(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
		divclk_divide = (clk_cfg_reg0 >> WZRD_DIVCLK_DIVIDE_SHIFT &
				 WZRD_DIVCLK_DIVIDE_MASK);

		/* The 8*125 give the x1000 that is needed */
		new_mult = (rate * divclk_divide * 8 / parent_rate) * 125;
	}
	new_mult = clamp(new_mult, CLKFBOUT_MULT_F_MIN, CLKFBOUT_MULT_F_MAX);

	clkfbout_mult = new_mult / 1000;
	clkfbout_frac = new_mult % 1000;

	axival = clkfbout_frac << WZRD_CLKFBOUT_FRAC_SHIFT |
		 clkfbout_mult << WZRD_CLKFBOUT_MULT_SHIFT |
		 divclk_divide << WZRD_DIVCLK_DIVIDE_SHIFT;

	spin_lock_irqsave(clk_wzrd->lock, flags);

	/* set new value */
	writel(axival, clk_wzrd->base + WZRD_CLK_CFG_REG(0));

	/* Initiate reconfiguration */
	writel(WZRD_DR_BEGIN_DYNA_RECONF,
	       clk_wzrd->base + WZRD_DR_INIT_REG_OFFSET);

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(clk_wzrd->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	spin_unlock_irqrestore(clk_wzrd->lock, flags);

	if (!retries)
		err = -ETIMEDOUT;

	return err;
}

static long clk_wzrd_vco_round_rate_f(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	return rate;
}

static const struct clk_ops clk_wzrd_vco_mul_ops_f = {
	.round_rate = clk_wzrd_vco_round_rate_f,
	.set_rate = clk_wzrd_vco_dynamic_reconfig_f,
	.recalc_rate = clk_wzrd_vco_recalc_ratef,
};

static struct clk *clk_wzrd_register_divf(struct device *dev,
					  const char *name,
					  const char *parent_name,
					  unsigned long flags,
					  void __iomem *base, u16 offset,
					  u8 shift, u8 width,
					  u8 clk_divider_flags,
					  const struct clk_div_table *table,
					  spinlock_t *lock)
{
	struct clk_wzrd_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

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

	if (clk_divider_flags & CLK_DIVIDER_READ_ONLY)
		init.ops = &clk_divider_ro_ops;
	else
		init.ops = &clk_wzrd_clk_divider_ops_f;

	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	div->base = base;
	div->offset = offset;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;

	/* register the clock */
	hw = &div->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(div);
		return ERR_PTR(ret);
	}

	return hw->clk;
}

static struct clk *clk_wzrd_register_divider(struct device *dev,
					     const char *name,
					     const char *parent_name,
					     unsigned long flags,
					     void __iomem *base, u16 offset,
					     u8 shift, u8 width,
					     u8 clk_divider_flags,
					     const struct clk_div_table *table,
					     spinlock_t *lock)
{
	struct clk_wzrd_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

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
	if (clk_divider_flags & CLK_DIVIDER_READ_ONLY)
		init.ops = &clk_divider_ro_ops;
	else
		init.ops = &clk_wzrd_clk_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	div->base = base;
	div->offset = offset;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;

	/* register the clock */
	hw = &div->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw->clk;
}

static int clk_wzrd_clk_notifier(struct notifier_block *nb, unsigned long event,
				 void *data)
{
	unsigned long max;
	struct clk_notifier_data *ndata = data;
	struct clk_wzrd *clk_wzrd = to_clk_wzrd(nb);

	if (clk_wzrd->suspended)
		return NOTIFY_OK;

	if (ndata->clk == clk_wzrd->clk_in1)
		max = clk_wzrd_max_freq[clk_wzrd->speed_grade - 1];
	else if (ndata->clk == clk_wzrd->axi_clk)
		max = WZRD_ACLK_MAX_FREQ;
	else
		return NOTIFY_DONE;	/* should never happen */

	switch (event) {
	case PRE_RATE_CHANGE:
		if (ndata->new_rate > max)
			return NOTIFY_BAD;
		return NOTIFY_OK;
	case POST_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

static int __maybe_unused clk_wzrd_suspend(struct device *dev)
{
	struct clk_wzrd *clk_wzrd = dev_get_drvdata(dev);

	clk_disable_unprepare(clk_wzrd->axi_clk);
	clk_wzrd->suspended = true;

	return 0;
}

static int __maybe_unused clk_wzrd_resume(struct device *dev)
{
	int ret;
	struct clk_wzrd *clk_wzrd = dev_get_drvdata(dev);

	ret = clk_prepare_enable(clk_wzrd->axi_clk);
	if (ret) {
		dev_err(dev, "unable to enable s_axi_aclk\n");
		return ret;
	}

	clk_wzrd->suspended = false;

	return 0;
}

static SIMPLE_DEV_PM_OPS(clk_wzrd_dev_pm_ops, clk_wzrd_suspend,
			 clk_wzrd_resume);

static int clk_wzrd_probe(struct platform_device *pdev)
{
	int i, ret;
	unsigned long rate;
	struct clk_wzrd *clk_wzrd;
	struct resource *mem;
	struct device_node *np = pdev->dev.of_node;

	const char *clk_in_name = NULL;
	const char *clk_vco_name = NULL;
	struct clk_init_data init;
	unsigned long flags;

	clk_wzrd = devm_kzalloc(&pdev->dev, sizeof(*clk_wzrd), GFP_KERNEL);
	if (!clk_wzrd)
		return -ENOMEM;
	platform_set_drvdata(pdev, clk_wzrd);

	/* hints start off as invalid */
	clk_wzrd->hints.valid_rate = false;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	clk_wzrd->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(clk_wzrd->base))
		return PTR_ERR(clk_wzrd->base);

	clk_wzrd->lock = &clkwzrd_lock;

	ret = of_property_read_u32(np, "speed-grade", &clk_wzrd->speed_grade);
	if (!ret) {
		if (clk_wzrd->speed_grade < 1 || clk_wzrd->speed_grade > 3) {
			dev_warn(&pdev->dev, "invalid speed grade '%d'\n",
				 clk_wzrd->speed_grade);
			clk_wzrd->speed_grade = 0;
		}
	}

	clk_wzrd->clk_in1 = devm_clk_get(&pdev->dev, "clk_in1");
	if (IS_ERR(clk_wzrd->clk_in1)) {
		if (clk_wzrd->clk_in1 != ERR_PTR(-EPROBE_DEFER))
			dev_err(&pdev->dev, "clk_in1 not found\n");
		return PTR_ERR(clk_wzrd->clk_in1);
	}

	clk_wzrd->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clk_wzrd->axi_clk)) {
		if (clk_wzrd->axi_clk != ERR_PTR(-EPROBE_DEFER))
			dev_err(&pdev->dev, "s_axi_aclk not found\n");
		return PTR_ERR(clk_wzrd->axi_clk);
	}
	ret = clk_prepare_enable(clk_wzrd->axi_clk);
	if (ret) {
		dev_err(&pdev->dev, "enabling s_axi_aclk failed\n");
		return ret;
	}
	rate = clk_get_rate(clk_wzrd->axi_clk);
	if (rate > WZRD_ACLK_MAX_FREQ) {
		dev_err(&pdev->dev, "s_axi_aclk frequency (%lu) too high\n",
			rate);
		ret = -EINVAL;
		goto err_disable_clk;
	}

	ret = clk_prepare_enable(clk_wzrd->clk_in1);
	if (ret) {
		dev_err(&pdev->dev, "enabling clk_in1 failed\n");
		return ret;
	}

	/* Init VCO clock */
	clk_vco_name = kasprintf(GFP_KERNEL, "%s_vco", dev_name(&pdev->dev));
	init.name = clk_vco_name;
	if (!clk_vco_name) {
		ret = -ENOMEM;
		goto err_disable_clk;
	}

	init.ops = &clk_wzrd_vco_mul_ops_f;
	init.flags = CLK_IS_BASIC;

	clk_in_name = __clk_get_name(clk_wzrd->clk_in1);
	init.parent_names = &clk_in_name;
	init.num_parents = 1;

	/* register the VCO clock */
	clk_wzrd->vco_clk_hw.init = &init;
	ret = clk_hw_register(&pdev->dev, &clk_wzrd->vco_clk_hw);

	clk_wzrd->num_out_clks =
		of_property_count_strings(np, "clock-output-names");

	/* only allow propagating changes up to the VCO if this is set  */
	if (of_property_read_bool(np, "set-vco-parent"))
		flags = CLK_SET_RATE_PARENT;
	else
		flags = 0;

	/* register div per output */
	for (i = clk_wzrd->num_out_clks - 1; i >= 0 ; i--) {
		const char *clkout_name;

		if (of_property_read_string_index(np, "clock-output-names", i,
						  &clkout_name)) {
			dev_err(&pdev->dev,
				"clock output name not specified\n");
			ret = -EINVAL;
			goto err_rm_int_clks;
		}
		if (!i)
			clk_wzrd->clkout[i] = clk_wzrd_register_divf
				(&pdev->dev, clkout_name,
				clk_vco_name, flags,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		else
			clk_wzrd->clkout[i] = clk_wzrd_register_divider
				(&pdev->dev, clkout_name,
				clk_vco_name, 0,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		if (IS_ERR(clk_wzrd->clkout[i])) {
			int j;

			for (j = i + 1; j < clk_wzrd->num_out_clks; j++)
				clk_unregister(clk_wzrd->clkout[j]);
			dev_err(&pdev->dev,
				"unable to register divider clock\n");
			ret = PTR_ERR(clk_wzrd->clkout[i]);
			goto err_rm_int_clks;
		}
	}

	clk_wzrd->clk_data.clks = clk_wzrd->clkout;
	clk_wzrd->clk_data.clk_num = ARRAY_SIZE(clk_wzrd->clkout);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_wzrd->clk_data);

	if (clk_wzrd->speed_grade) {
		clk_wzrd->nb.notifier_call = clk_wzrd_clk_notifier;

		ret = clk_notifier_register(clk_wzrd->clk_in1,
					    &clk_wzrd->nb);
		if (ret)
			dev_warn(&pdev->dev,
				 "unable to register clock notifier\n");

		ret = clk_notifier_register(clk_wzrd->axi_clk, &clk_wzrd->nb);
		if (ret)
			dev_warn(&pdev->dev,
				 "unable to register clock notifier\n");
	}

	return 0;

err_rm_int_clks:
	clk_unregister(clk_wzrd->vco_clk_hw.clk);
err_disable_clk:
	clk_disable_unprepare(clk_wzrd->axi_clk);

	return ret;
}

static int clk_wzrd_remove(struct platform_device *pdev)
{
	int i;
	struct clk_wzrd *clk_wzrd = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0; i < clk_wzrd->num_out_clks; i++)
		clk_unregister(clk_wzrd->clkout[i]);

	clk_unregister(clk_wzrd->vco_clk_hw.clk);

	if (clk_wzrd->speed_grade) {
		clk_notifier_unregister(clk_wzrd->axi_clk, &clk_wzrd->nb);
		clk_notifier_unregister(clk_wzrd->clk_in1, &clk_wzrd->nb);
	}

	clk_disable_unprepare(clk_wzrd->axi_clk);

	return 0;
}

static const struct of_device_id clk_wzrd_ids[] = {
	{ .compatible = "xlnx,clocking-wizard" },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_wzrd_ids);

static struct platform_driver clk_wzrd_driver = {
	.driver = {
		.name = "clk-wizard",
		.of_match_table = clk_wzrd_ids,
		.pm = &clk_wzrd_dev_pm_ops,
	},
	.probe = clk_wzrd_probe,
	.remove = clk_wzrd_remove,
};
module_platform_driver(clk_wzrd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Brinkmann <soren.brinkmann@xilinx.com");
MODULE_DESCRIPTION("Driver for the Xilinx Clocking Wizard IP core");
