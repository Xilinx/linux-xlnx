/*
 * Zynq UltraScale+ MPSoC PLL driver
 *
 *  Copyright (C) 2016 Xilinx
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/clk.h>
#include <linux/clk/zynqmp.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>

/**
 * struct zynqmp_pll - Structure for PLL clock
 * @hw:		Handle between common and hardware-specific interfaces
 * @pll_ctrl:	PLL control register
 * @pll_status:	PLL status register
 * @lockbit:	Indicates the associated PLL_LOCKED bit in the PLL status
 *		register
 */
struct zynqmp_pll {
	struct clk_hw	hw;
	void __iomem	*pll_ctrl;
	void __iomem	*pll_status;
	u8		lockbit;
};
#define to_zynqmp_pll(_hw)	container_of(_hw, struct zynqmp_pll, hw)

/* Register bitfield defines */
#define PLLCTRL_FBDIV_MASK	0x7f00
#define PLLCTRL_FBDIV_SHIFT	8
#define PLLCTRL_BP_MASK		(1 << 3)
#define PLLCTRL_DIV2_MASK	(1 << 16)
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
	PLL_MODE_FRAC,
	PLL_MODE_INT,
};

#define FRAC_OFFSET 0x8
#define PLLFCFG_FRAC_EN	BIT(31)
#define FRAC_DIV  0x10000  /* 2^16 */

static inline enum pll_mode pll_frac_get_mode(struct clk_hw *hw)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 reg;
	int ret;

	ret = zynqmp_pm_mmio_read((u32)(ulong)(clk->pll_ctrl + FRAC_OFFSET),
					&reg);
	if (ret)
		pr_warn_once("Read fail pll address: %x\n",
				(u32)(ulong)(clk->pll_ctrl + FRAC_OFFSET));

	reg = reg & PLLFCFG_FRAC_EN;
	return reg ? PLL_MODE_FRAC : PLL_MODE_INT;
}

/**
 * pll_frac_set_mode - Set the fractional mode
 * @hw:		Handle between common and hardware-specific interfaces
 * @on:		Flag to determine the mode
 */
static inline void pll_frac_set_mode(struct clk_hw *hw, bool on)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 reg = 0;
	int ret;

	if (on)
		reg = PLLFCFG_FRAC_EN;

	ret = zynqmp_pm_mmio_write((u32)(ulong)(clk->pll_ctrl + FRAC_OFFSET),
					PLLFCFG_FRAC_EN, reg);
	if (ret)
		pr_warn_once("Write fail pll address: %x\n",
				(u32)(ulong)(clk->pll_ctrl + FRAC_OFFSET));
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
	pll_frac_set_mode(hw, !!f);

	if (pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
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
	u32 fbdiv, data;
	unsigned long rate, frac;

	/*
	 * makes probably sense to redundantly save fbdiv in the struct
	 * zynqmp_pll to save the IO access.
	 */
	fbdiv = (zynqmp_pm_mmio_readl(clk->pll_ctrl) & PLLCTRL_FBDIV_MASK) >>
			PLLCTRL_FBDIV_SHIFT;

	rate =  parent_rate * fbdiv;
	if (pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
		data = (zynqmp_pm_mmio_readl(clk->pll_ctrl + FRAC_OFFSET) &
			0xffff);
		frac = (parent_rate * data) / FRAC_DIV;
		rate = rate + frac;
	}

	return rate;
}

static int zynqmp_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	u32 fbdiv, reg;
	u32 data;
	long rate_div, frac, m, f;

	if (pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
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
		reg = zynqmp_pm_mmio_readl(clk->pll_ctrl);
		reg &= ~PLLCTRL_FBDIV_MASK;
		reg |= m << PLLCTRL_FBDIV_SHIFT;
		zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);
		reg = zynqmp_pm_mmio_readl(clk->pll_ctrl + FRAC_OFFSET);
		reg &= ~0xffff;

		data = (FRAC_DIV * f) / FRAC_DIV;
		data = data & 0xffff;
		reg |= data;
		zynqmp_pm_mmio_writel(reg, clk->pll_ctrl + FRAC_OFFSET);
		return (rate + frac);
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, parent_rate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	reg = zynqmp_pm_mmio_readl(clk->pll_ctrl);
	reg &= ~PLLCTRL_FBDIV_MASK;
	reg |= fbdiv << PLLCTRL_FBDIV_SHIFT;
	zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);

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
	u32 reg;
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);
	int ret;

	ret = zynqmp_pm_mmio_read((u32)(ulong)clk->pll_ctrl, &reg);
	if (ret)
		pr_warn_once("Read fail pll address: %x\n",
				(u32)(ulong)clk->pll_ctrl);

	return !(reg & (PLLCTRL_RESET_MASK));
}

/**
 * zynqmp_pll_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * Return:	0 always
 */
static int zynqmp_pll_enable(struct clk_hw *hw)
{
	u32 reg;
	struct zynqmp_pll *clk = to_zynqmp_pll(hw);

	if (zynqmp_pll_is_enabled(hw))
		return 0;

	pr_info("PLL: enable\n");

	reg = zynqmp_pm_mmio_readl(clk->pll_ctrl);
	reg |= PLLCTRL_BP_MASK;
	zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);
	reg |= PLLCTRL_RESET_MASK;
	zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);

	reg &= ~(PLLCTRL_RESET_MASK);
	zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);
	while (!(zynqmp_pm_mmio_readl(clk->pll_status) & (1 << clk->lockbit)))
		cpu_relax();

	reg &= ~PLLCTRL_BP_MASK;
	zynqmp_pm_mmio_writel(reg, clk->pll_ctrl);

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

	if (!zynqmp_pll_is_enabled(hw))
		return;

	pr_info("PLL: shutdown\n");

	/* shut down PLL */
	zynqmp_pm_mmio_write((u32)(ulong)clk->pll_ctrl, PLLCTRL_RESET_MASK,
				PLLCTRL_RESET_VAL);
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
 * @flag:	PLL flags
 * @parent:	Parent clock name
 * @pll_ctrl:	Pointer to PLL control register
 * @pll_status:	Pointer to PLL status register
 * @lock_index:	Bit index to this PLL's lock status bit in @pll_status
 *
 * Return:	Handle to the registered clock
 */
struct clk *clk_register_zynqmp_pll(const char *name, const char *parent,
		unsigned long flag, resource_size_t *pll_ctrl,
		resource_size_t *pll_status, u8 lock_index)
{
	struct zynqmp_pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	int status;

	init.name = name;
	init.ops = &zynqmp_pll_ops;
	init.flags = flag;
	init.parent_names = &parent;
	init.num_parents = 1;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/* Populate the struct */
	pll->hw.init = &init;
	pll->pll_ctrl = pll_ctrl;
	pll->pll_status = pll_status;
	pll->lockbit = lock_index;

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		kfree(pll);

	status = clk_set_rate_range(clk, PS_PLL_VCO_MIN, PS_PLL_VCO_MAX);
	if (status < 0)
		pr_err("%s:ERROR clk_set_rate_range failed %d\n", name, status);

	return clk;
}
