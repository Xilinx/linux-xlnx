// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx 'Clocking Wizard' driver
 *
 *  Copyright (C) 2013 - 2014 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>

#define WZRD_NUM_OUTPUTS	7
#define WZRD_ACLK_MAX_FREQ	250000000UL

#define WZRD_CLK_CFG_REG(n)	(0x200 + 4 * (n))

#define WZRD_CLKOUT0_FRAC_EN	BIT(18)
#define WZRD_CLKFBOUT_FRAC_EN	BIT(26)

#define WZRD_CLKFBOUT_MULT_SHIFT	8
#define WZRD_CLKFBOUT_MULT_MASK		(0xff << WZRD_CLKFBOUT_MULT_SHIFT)
#define WZRD_CLKFBOUT_FRAC_SHIFT	16
#define WZRD_CLKFBOUT_FRAC_MASK		(0x3ff << WZRD_CLKFBOUT_FRAC_SHIFT)
#define WZRD_DIVCLK_DIVIDE_SHIFT	0
#define WZRD_DIVCLK_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_DIVIDE_SHIFT	0
#define WZRD_CLKOUT_DIVIDE_WIDTH	8
#define WZRD_CLKOUT_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_FRAC_SHIFT		8
#define WZRD_CLKOUT_FRAC_MASK		0x3ff

#define WZRD_DR_MAX_INT_DIV_VALUE	255
#define WZRD_DR_NUM_RETRIES		10000
#define WZRD_DR_STATUS_REG_OFFSET	0x04
#define WZRD_DR_LOCK_BIT_MASK		0x00000001
#define WZRD_DR_INIT_REG_OFFSET		0x25C
#define WZRD_DR_DIV_TO_PHASE_OFFSET	4
#define WZRD_DR_BEGIN_DYNA_RECONF	0x03

/* Multiplier limits, from UG572 Table 3-4 for Ultrascale+ */
#define CLKFBOUT_MULT_F_MIN		2000U
#define CLKFBOUT_MULT_F_MAX		128000U

/* Divider limits, from UG572 Table 3-4 for Ultrascale+ */
#define DIVCLK_DIVIDE_MIN		1U
#define DIVCLK_DIVIDE_MAX		106U

/* Get the mask from width */
#define div_mask(width)			((1 << (width)) - 1)

/* Extract divider instance from clock hardware instance */
#define to_clk_wzrd_divider(_hw) container_of(_hw, struct clk_wzrd_divider, hw)

/*
 *         MMCM Block Diagram
 *
 *         +----------------+  +-----------------+
 * input ->| vco_clk_div_hw |->| vco_clk_mul_hw  |--+
 * rate    | (int divide)   |  | (frac multiply) |  |
 *         +----------------+  +-----------------+  |
 *                                                  |
 *      +--------------------------------VCO-rate---+
 *      |
 *      |  +----------------+
 *      +->| clkout[0]      |-> output0 rate
 *      |  | (frac divide)  |
 *      |  +----------------+
 *      |
 *      |  +----------------+
 *      +->| clkout[1]      |-> output1 rate
 *      |  | (int divide)   |
 *      |  +----------------+
 *      |
 *     ...
 *      |
 *      |  +----------------+
 *      +->| clkout[1]      |-> output6 rate
 *         | (int divide)   |
 *         +----------------+
 *
 * struct clk_wzrd - Clock wizard private data structure
 *
 * @clk_data:		Clock data
 * @nb:			Notifier block
 * @base:		Memory base
 * @clk_in1:		Handle to input clock 'clk_in1'
 * @axi_clk:		Handle to input clock 's_axi_aclk'
 * @clkout:		Output clocks
 * @speed_grade:	Speed grade of the device
 * @suspended:		Flag indicating power state of the device
 * @lock		lock pointer
 * @vco_clk:		hw Voltage Controlled Oscilator clock
 */
struct clk_wzrd {
	struct clk_onecell_data clk_data;
	struct notifier_block nb;
	void __iomem *base;
	struct clk *clk_in1;
	struct clk *axi_clk;
	struct clk *clkout[WZRD_NUM_OUTPUTS];
	unsigned int speed_grade;
	bool suspended;
	spinlock_t *lock;
	struct clk_hw vco_clk_div_hw;
	struct clk_hw vco_clk_mul_hw;
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
			(void __iomem *)(divider->base + divider->offset);
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
			(void __iomem *)(divider->base + divider->offset);

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

static int clk_wzrd_dynamic_reconfig_f(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	u32 value, pre;
	unsigned long flags = 0;
	unsigned long rate_div, f, clockout0_div;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	rate_div = ((parent_rate * 1000) / rate);
	clockout0_div = rate_div / 1000;

	pre = DIV_ROUND_CLOSEST((parent_rate * 1000), rate);
	f = (u32)(pre - (clockout0_div * 1000));
	f = f & WZRD_CLKOUT_FRAC_MASK;

	value = ((f << WZRD_CLKOUT_DIVIDE_WIDTH) | (clockout0_div &
			WZRD_CLKOUT_DIVIDE_MASK));

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

static long clk_wzrd_round_rate_f(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	return rate;
}

static const struct clk_ops clk_wzrd_clk_divider_ops_f = {
	.round_rate = clk_wzrd_round_rate_f,
	.set_rate = clk_wzrd_dynamic_reconfig_f,
	.recalc_rate = clk_wzrd_recalc_ratef,
};

static unsigned long clk_wzrd_vco_mul_recalc_rate_f(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	u32 clk_cfg_reg0;
	u32 clkfbout_mult, clkfbout_frac;
	u64 rate;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_mul_hw);

	clk_cfg_reg0 = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	clkfbout_mult = (clk_cfg_reg0 & WZRD_CLKFBOUT_MULT_MASK) >>
			 WZRD_CLKFBOUT_MULT_SHIFT;
	clkfbout_frac = (clk_cfg_reg0 & WZRD_CLKFBOUT_FRAC_MASK) >>
			 WZRD_CLKFBOUT_FRAC_SHIFT;

	rate = parent_rate *
	       (clkfbout_mult * 1000 + clkfbout_frac) / /* multiplier x1000 */
	       1000;

	return (unsigned long)rate;
}

static int clk_wzrd_vco_mul_dynamic_reconfig_f(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	unsigned long flags = 0;
	u32 clk_cfg_reg0, value;
	u32 divclk_divide, clkfbout_mult, clkfbout_frac;
	unsigned int new_mult;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_mul_hw);

	/* The 8*125 give the x1000 that is needed */
	new_mult = (rate * 8 / parent_rate) * 125;
	new_mult = clamp(new_mult, CLKFBOUT_MULT_F_MIN, CLKFBOUT_MULT_F_MAX);

	clkfbout_mult = new_mult / 1000;
	clkfbout_frac = new_mult % 1000;

	/* Read divclk_divide so it can be left unchanged */
	clk_cfg_reg0 = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	divclk_divide = (clk_cfg_reg0 & WZRD_DIVCLK_DIVIDE_MASK) >>
			 WZRD_DIVCLK_DIVIDE_SHIFT;

	value = clkfbout_frac << WZRD_CLKFBOUT_FRAC_SHIFT |
		 clkfbout_mult << WZRD_CLKFBOUT_MULT_SHIFT |
		 divclk_divide << WZRD_DIVCLK_DIVIDE_SHIFT;

	if (clk_wzrd->lock)
		spin_lock_irqsave(clk_wzrd->lock, flags);
	else
		__acquire(clk_wzrd->lock);

	/* Write the configuration out */
	writel(value, clk_wzrd->base + WZRD_CLK_CFG_REG(0));

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(clk_wzrd->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (!retries) {
		err = -ETIMEDOUT;
		goto err_reconfig;
	}

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

	if (!retries)
		err = -ETIMEDOUT;

err_reconfig:
	if (clk_wzrd->lock)
		spin_unlock_irqrestore(clk_wzrd->lock, flags);
	else
		__release(clk_wzrd->lock);

	return err;
}

static long clk_wzrd_vco_mul_round_rate_f(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	return rate;
}

static const struct clk_ops clk_wzrd_vco_mul_ops_f = {
	.round_rate = clk_wzrd_vco_mul_round_rate_f,
	.set_rate = clk_wzrd_vco_mul_dynamic_reconfig_f,
	.recalc_rate = clk_wzrd_vco_mul_recalc_rate_f,
};

static unsigned long clk_wzrd_vco_div_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	u32 clk_cfg_reg0;
	u32 divclk_divide;
	u64 rate;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_div_hw);

	clk_cfg_reg0 = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	divclk_divide = (clk_cfg_reg0 & WZRD_DIVCLK_DIVIDE_MASK) >>
			 WZRD_DIVCLK_DIVIDE_SHIFT;

	rate = parent_rate / divclk_divide;

	return (unsigned long)rate;
}

static int clk_wzrd_vco_div_dynamic_reconfig(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	unsigned long flags = 0;
	u32 clk_cfg_reg0, value;
	u32 divclk_divide, clkfbout_mult, clkfbout_frac;

	struct clk_wzrd *clk_wzrd = container_of(hw,
						 struct clk_wzrd,
						 vco_clk_div_hw);

	divclk_divide = DIV_ROUND_CLOSEST(parent_rate, rate);

	divclk_divide = clamp(divclk_divide,
			      DIVCLK_DIVIDE_MIN,
			      DIVCLK_DIVIDE_MAX);

	/*
	 * Read clkfbout_mult and clkfbout_frac
	 * so they can be left unchanged
	 */
	clk_cfg_reg0 = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	clkfbout_mult = (clk_cfg_reg0 & WZRD_CLKFBOUT_MULT_MASK) >>
			 WZRD_CLKFBOUT_MULT_SHIFT;
	clkfbout_frac = (clk_cfg_reg0 & WZRD_CLKFBOUT_FRAC_MASK) >>
			 WZRD_CLKFBOUT_FRAC_SHIFT;

	value = clkfbout_frac << WZRD_CLKFBOUT_FRAC_SHIFT |
		 clkfbout_mult << WZRD_CLKFBOUT_MULT_SHIFT |
		 divclk_divide << WZRD_DIVCLK_DIVIDE_SHIFT;

	if (clk_wzrd->lock)
		spin_lock_irqsave(clk_wzrd->lock, flags);
	else
		__acquire(clk_wzrd->lock);

	/* Write the configuration out */
	writel(value, clk_wzrd->base + WZRD_CLK_CFG_REG(0));

	/* Check status register */
	retries = WZRD_DR_NUM_RETRIES;
	while (retries--) {
		if (readl(clk_wzrd->base + WZRD_DR_STATUS_REG_OFFSET) &
							WZRD_DR_LOCK_BIT_MASK)
			break;
	}

	if (!retries) {
		err = -ETIMEDOUT;
		goto err_reconfig;
	}

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

	if (!retries)
		err = -ETIMEDOUT;

err_reconfig:
	if (clk_wzrd->lock)
		spin_unlock_irqrestore(clk_wzrd->lock, flags);
	else
		__release(clk_wzrd->lock);

	return err;
}

static long clk_wzrd_vco_div_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	return rate;
}

static const struct clk_ops clk_wzrd_vco_div_ops = {
	.round_rate = clk_wzrd_vco_div_round_rate,
	.set_rate = clk_wzrd_vco_div_dynamic_reconfig,
	.recalc_rate = clk_wzrd_vco_div_recalc_rate,
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

	init.flags = flags;
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
	init.flags = flags;
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
	int outputs;
	unsigned long flags = 0;
	struct device_node *np = pdev->dev.of_node;

	const char *clk_in_name = NULL;
	const char *clk_vco_div_name = NULL, *clk_vco_mul_name = NULL;
	struct clk_init_data init;

	clk_wzrd = devm_kzalloc(&pdev->dev, sizeof(*clk_wzrd), GFP_KERNEL);
	if (!clk_wzrd)
		return -ENOMEM;
	platform_set_drvdata(pdev, clk_wzrd);

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

	/* Only allow the parent rate to be set if there is a single output */
	outputs = of_property_count_strings(np, "clock-output-names");
	if (outputs == 1)
		flags = CLK_SET_RATE_PARENT;

	/* Write the divider clock name */
	clk_vco_div_name = kasprintf(GFP_KERNEL, "%s_div",
				     dev_name(&pdev->dev));

	if (!clk_vco_div_name) {
		ret = -ENOMEM;
		goto err_disable_clk;
	}

	/* Write the multiplier clock name */
	clk_vco_mul_name = kasprintf(GFP_KERNEL, "%s_mul",
				     dev_name(&pdev->dev));
	if (!clk_vco_mul_name) {
		ret = -ENOMEM;
		kfree(clk_vco_div_name);
		goto err_disable_clk;
	}

	/* setup and register the VCO divider clock */
	init.name = clk_vco_div_name;
	init.ops = &clk_wzrd_vco_div_ops;
	clk_in_name = __clk_get_name(clk_wzrd->clk_in1);
	dev_info(&pdev->dev, "clk_in_name: %s\n", clk_in_name);
	init.parent_names = &clk_in_name;
	init.num_parents = 1;
	init.flags = 0;

	clk_wzrd->vco_clk_div_hw.init = &init;
	ret = clk_hw_register(&pdev->dev, &clk_wzrd->vco_clk_div_hw);
	if (ret)
		goto err_disable_clk_free_names;

	/* setup and register the VCO multiplier clock */
	init.name = clk_vco_mul_name;
	init.ops = &clk_wzrd_vco_mul_ops_f;
	init.parent_names = &clk_vco_div_name;
	init.num_parents = 1;
	/* needs CLK_SET_RATE_PARENT if set above */
	init.flags = flags;

	clk_wzrd->vco_clk_mul_hw.init = &init;
	ret = clk_hw_register(&pdev->dev, &clk_wzrd->vco_clk_mul_hw);
	if (ret)
		goto err_rm_hw_div_clk;

	/* register div per output */
	for (i = outputs - 1; i >= 0 ; i--) {
		const char *clkout_name;

		if (of_property_read_string_index(np, "clock-output-names", i,
						  &clkout_name)) {
			dev_err(&pdev->dev,
				"clock output name not specified\n");
			ret = -EINVAL;
			goto err_rm_hw_clks;
		}
		if (!i)
			clk_wzrd->clkout[i] = clk_wzrd_register_divf
				(&pdev->dev, clkout_name,
				clk_vco_mul_name, flags,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		else
			clk_wzrd->clkout[i] = clk_wzrd_register_divider
				(&pdev->dev, clkout_name,
				clk_vco_mul_name, 0,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		if (IS_ERR(clk_wzrd->clkout[i])) {
			int j;

			for (j = i + 1; j < outputs; j++)
				clk_unregister(clk_wzrd->clkout[j]);
			dev_err(&pdev->dev,
				"unable to register divider clock\n");
			ret = PTR_ERR(clk_wzrd->clkout[i]);
			goto err_rm_hw_clks;
		}
	}

	kfree(clk_vco_div_name);
	kfree(clk_vco_mul_name);

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

err_rm_hw_clks:
	clk_unregister(clk_wzrd->vco_clk_mul_hw.clk);
err_rm_hw_div_clk:
	clk_unregister(clk_wzrd->vco_clk_div_hw.clk);
err_disable_clk_free_names:
	kfree(clk_vco_div_name);
	kfree(clk_vco_mul_name);
err_disable_clk:
	clk_disable_unprepare(clk_wzrd->axi_clk);

	return ret;
}

static int clk_wzrd_remove(struct platform_device *pdev)
{
	int i;
	struct clk_wzrd *clk_wzrd = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0; i < WZRD_NUM_OUTPUTS; i++)
		clk_unregister(clk_wzrd->clkout[i]);

	clk_unregister(clk_wzrd->vco_clk_div_hw.clk);
	clk_unregister(clk_wzrd->vco_clk_mul_hw.clk);

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
