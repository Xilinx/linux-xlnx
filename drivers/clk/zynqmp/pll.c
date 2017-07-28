// SPDX-License-Identifier: GPL-2.0+
/*
 * Zynq UltraScale+ MPSoC PLL driver
 *
 *  Copyright (C) 2016-2018 Xilinx
 */

#include <linux/clk.h>
#include <linux/clk/zynqmp.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>

/**
 * struct zynqmp_pll - Structure for PLL clock
 * @hw:		Handle between common and hardware-specific interfaces
 * @clk_id:	PLL clock ID
 */
struct zynqmp_pll {
	struct clk_hw hw;
	u32 clk_id;
};

#define to_zynqmp_pll(_hw)	container_of(_hw, struct zynqmp_pll, hw)

/* Register bitfield defines */
#define PLLCTRL_FBDIV_MASK	0x7f00
#define PLLCTRL_FBDIV_SHIFT	8
#define PLLCTRL_BP_MASK		BIT(3)
#define PLLCTRL_DIV2_MASK	BIT(16)
#define PLLCTRL_RESET_MASK	1
#define PLLCTRL_RESET_VAL	1
#define PLL_STATUS_LOCKED	1
#define PLLCTRL_RESET_SHIFT	0
#define PLLCTRL_DIV2_SHIFT	16

#define PLL_FBDIV_MIN	25
#define PLL_FBDIV_MAX	125

#define PS_PLL_VCO_MIN 1500000000
#define PS_PLL_VCO_MAX 3000000000UL

enum pll_mode {
	PLL_MODE_INT,
	PLL_MODE_FRAC,
};

#define FRAC_OFFSET 0x8
#define PLLFCFG_FRAC_EN	BIT(31)
#define FRAC_DIV  0x10000  /* 2^16 */

/**
 * pll_get_mode - Get mode of PLL
 * @hw: Handle between common and hardware-specific interfaces
 *
 * Return: Mode of PLL
 */
static inline enum pll_mode pll_get_mode(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl)
		return -ENXIO;

	ret = eemi_ops->ioctl(0, IOCTL_GET_PLL_FRAC_MODE, clk_id, 0,
			      ret_payload);
	if (ret)
		pr_warn_once("%s() PLL get frac mode failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return ret_payload[1];
}

/**
 * pll_set_mode - Set the PLL mode
 * @hw:		Handle between common and hardware-specific interfaces
 * @on:		Flag to determine the mode
 */
static inline void pll_set_mode(struct clk_hw *hw, bool on)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	int ret;
	u32 mode;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->ioctl) {
		pr_warn_once("eemi_ops not found\n");
		return;
	}

	if (on)
		mode = PLL_MODE_FRAC;
	else
		mode = PLL_MODE_INT;

	ret = eemi_ops->ioctl(0, IOCTL_SET_PLL_FRAC_MODE, clk_id, mode, NULL);
	if (ret)
		pr_warn_once("%s() PLL set frac mode failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
}

/**
 * zynqmp_pll_round_rate - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 *
 * Return:	Frequency closest to @rate the hardware can generate
 */
static long zynqmp_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	u32 fbdiv;
	long rate_div, f;

	/* Enable the fractional mode if needed */
	rate_div = ((rate * FRAC_DIV) / *prate);
	f = rate_div % FRAC_DIV;
	pll_set_mode(hw, !!f);

	if (pll_get_mode(hw) == PLL_MODE_FRAC) {
		if (rate > PS_PLL_VCO_MAX) {
			fbdiv = rate / PS_PLL_VCO_MAX;
			rate = rate / (fbdiv + 1);
		}
		if (rate < PS_PLL_VCO_MIN) {
			fbdiv = DIV_ROUND_UP(PS_PLL_VCO_MIN, rate);
			rate = rate * fbdiv;
		}
		return rate;
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, *prate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	return *prate * fbdiv;
}

/**
 * zynqmp_pll_recalc_rate - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Return:		Current clock frequency
 */
static unsigned long zynqmp_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 fbdiv, data;
	unsigned long rate, frac;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getdivider)
		return 0;

	/*
	 * makes probably sense to redundantly save fbdiv in the struct
	 * zynqmp_pll to save the IO access.
	 */
	ret = eemi_ops->clock_getdivider(clk_id, &fbdiv);
	if (ret)
		pr_warn_once("%s() get divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	rate =  parent_rate * fbdiv;
	if (pll_get_mode(hw) == PLL_MODE_FRAC) {
		eemi_ops->ioctl(0, IOCTL_GET_PLL_FRAC_DATA, clk_id, 0,
				ret_payload);
		data = ret_payload[1];
		frac = (parent_rate * data) / FRAC_DIV;
		rate = rate + frac;
	}

	return rate;
}

/**
 * zynqmp_pll_set_rate - Set rate of PLL
 * @hw:			Handle between common and hardware-specific interfaces
 * @rate:		Frequency of clock to be set
 * @parent_rate:	Clock frequency of parent clock
 */
static int zynqmp_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 clk_id = clk->clk_id;
	const char *clk_name = clk_hw_get_name(hw);
	u32 fbdiv, data;
	long rate_div, frac, m, f;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_setdivider)
		return -ENXIO;

	if (pll_get_mode(hw) == PLL_MODE_FRAC) {
		unsigned int children;

		/*
		 * We're running on a ZynqMP compatible machine, make sure the
		 * VPLL only has one child.
		 */
		children = clk_get_children("vpll");

		/* Account for vpll_to_lpd and dp_video_ref */
		if (children > 2)
			WARN(1, "Two devices are using vpll which is forbidden\n");

		rate_div = ((rate * FRAC_DIV) / parent_rate);
		m = rate_div / FRAC_DIV;
		f = rate_div % FRAC_DIV;
		m = clamp_t(u32, m, (PLL_FBDIV_MIN), (PLL_FBDIV_MAX));
		rate = parent_rate * m;
		frac = (parent_rate * f) / FRAC_DIV;

		ret = eemi_ops->clock_setdivider(clk_id, m);
		if (ret)
			pr_warn_once("%s() set divider failed for %s, ret = %d\n",
				     __func__, clk_name, ret);

		data = (FRAC_DIV * f) / FRAC_DIV;
		eemi_ops->ioctl(0, IOCTL_SET_PLL_FRAC_DATA, clk_id, data, NULL);

		return (rate + frac);
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, parent_rate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	ret = eemi_ops->clock_setdivider(clk_id, fbdiv);
	if (ret)
		pr_warn_once("%s() set divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return parent_rate * fbdiv;
}

/**
 * zynqmp_pll_is_enabled - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return:	1 if the clock is enabled, 0 otherwise
 */
static int zynqmp_pll_is_enabled(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	unsigned int state;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_getstate)
		return 0;

	ret = eemi_ops->clock_getstate(clk_id, &state);
	if (ret)
		pr_warn_once("%s() clock get state failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return state ? 1 : 0;
}

/**
 * zynqmp_pll_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return:	0 always
 */
static int zynqmp_pll_enable(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_enable)
		return 0;

	if (zynqmp_pll_is_enabled(hw))
		return 0;

	pr_info("PLL: enable\n");

	ret = eemi_ops->clock_enable(clk_id);
	if (ret)
		pr_warn_once("%s() clock enable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return 0;
}

/**
 * zynqmp_pll_disable - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 *
 */
static void zynqmp_pll_disable(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = clk->clk_id;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->clock_disable)
		return;

	if (!zynqmp_pll_is_enabled(hw))
		return;

	pr_info("PLL: shutdown\n");

	ret = eemi_ops->clock_disable(clk_id);
	if (ret)
		pr_warn_once("%s() clock disable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
}

static const struct clk_ops zynqmp_pll_ops = {
	.enable = zynqmp_pll_enable,
	.disable = zynqmp_pll_disable,
	.is_enabled = zynqmp_pll_is_enabled,
	.round_rate = zynqmp_pll_round_rate,
	.recalc_rate = zynqmp_pll_recalc_rate,
	.set_rate = zynqmp_pll_set_rate,
};

/**
 * clk_register_zynqmp_pll - Register PLL with the clock framework
 * @name:	PLL name
 * @clk_id:	Clock ID
 * @parents:	Parent clock names
 * @num_parents:Number of parents
 * @flag:	PLL flgas
 *
 * Return:	Handle to the registered clock
 */
struct clk *clk_register_zynqmp_pll(const char *name, u32 clk_id,
				    const char * const *parents,
				    u8 num_parents, unsigned long flag)
{
	struct zynqmp_pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	int status;

	init.name = name;
	init.ops = &zynqmp_pll_ops;
	init.flags = flag;
	init.parent_names = parents;
	init.num_parents = num_parents;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/* Populate the struct */
	pll->hw.init = &init;
	pll->clk_id = clk_id;

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		kfree(pll);

	status = clk_set_rate_range(clk, PS_PLL_VCO_MIN, PS_PLL_VCO_MAX);
	if (status < 0)
		pr_err("%s:ERROR clk_set_rate_range failed %d\n", name, status);

	return clk;
}
