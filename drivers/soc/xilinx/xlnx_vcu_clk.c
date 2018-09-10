// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx VCU clock driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Rajan Vaja <rajan.vaja@xilinx.com>
 * Tejas Patel <tejas.patel@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/xilinx/xlnx_vcu.h>

/* vcu slcr registers, bitmask and shift */
#define VCU_PLL_CTRL			0x24
#define VCU_PLL_CTRL_RESET_MASK		BIT(0)
#define VCU_PLL_CTRL_RESET_SHIFT	0
#define VCU_PLL_CTRL_BYPASS_MASK	BIT(3)
#define VCU_PLL_CTRL_BYPASS_SHIFT	3
#define VCU_PLL_CTRL_FBDIV_MASK		0x7f
#define VCU_PLL_CTRL_FBDIV_SHIFT	8
#define VCU_PLL_CTRL_POR_IN_MASK	BIT(1)
#define VCU_PLL_CTRL_POR_IN_SHIFT	1
#define VCU_PLL_CTRL_PWR_POR_MASK	BIT(2)
#define VCU_PLL_CTRL_PWR_POR_SHIFT	2
#define VCU_PLL_CTRL_CLKOUTDIV_MASK	0x03
#define VCU_PLL_CTRL_CLKOUTDIV_SHIFT	16
#define VCU_PLL_CTRL_DEFAULT		0

#define VCU_PLL_CFG			0x28
#define VCU_PLL_CFG_RES_MASK		0x0f
#define VCU_PLL_CFG_RES_SHIFT		0
#define VCU_PLL_CFG_CP_MASK		0x0f
#define VCU_PLL_CFG_CP_SHIFT		5
#define VCU_PLL_CFG_LFHF_MASK		0x03
#define VCU_PLL_CFG_LFHF_SHIFT		10
#define VCU_PLL_CFG_LOCK_CNT_MASK	0x03ff
#define VCU_PLL_CFG_LOCK_CNT_SHIFT	13
#define VCU_PLL_CFG_LOCK_DLY_MASK	0x7f
#define VCU_PLL_CFG_LOCK_DLY_SHIFT	25
#define VCU_ENC_CORE_CTRL		0x30
#define VCU_ENC_MCU_CTRL		0x34
#define VCU_ENC_MCU_CTRL_GATE_BIT	BIT(12)
#define VCU_DEC_CORE_CTRL		0x38
#define VCU_DEC_MCU_CTRL		0x3c
#define VCU_PLL_DIVISOR_MASK		0x3f
#define VCU_PLL_DIVISOR_SHIFT		4
#define VCU_SRCSEL_MASK			0x01
#define VCU_SRCSEL_SHIFT		0
#define VCU_SRCSEL_PLL			1

#define VCU_PLL_STATUS			0x60
#define VCU_PLL_STATUS_LOCK_STATUS_MASK	0x01
#define VCU_PLL_LOCK_TIMEOUT		2000000

#define PLL_FBDIV_MIN			25
#define PLL_FBDIV_MAX			125

#define MHZ				1000000
#define FVCO_MIN			(1500U * MHZ)
#define FVCO_MAX			(3000U * MHZ)
#define DIVISOR_MIN			0
#define DIVISOR_MAX			63
#define FRAC				100
#define LIMIT				(10 * MHZ)

#define FRAC_OFFSET			0x8
#define PLLFCFG_FRAC_EN			BIT(31)
#define FRAC_DIV			0x10000 /* 2^16 */

#define to_vcu_pll(_hw)	container_of(_hw, struct vcu_pll, hw)
#define div_mask(width)	((1 << (width)) - 1)

enum pll_mode {
	PLL_MODE_INT,
	PLL_MODE_FRAC,
};

enum vcu_clks {
	vcu_pll_half, vcu_core_enc, vcu_core_dec,
	mcu_core_enc, mcu_core_dec, clk_max
};

/**
 * struct xvcu_pll_cfg - Helper data
 * @fbdiv: The integer portion of the feedback divider to the PLL
 * @cp:	PLL charge pump control
 * @res: PLL loop filter resistor control
 * @lfhf: PLL loop filter high frequency capacitor control
 * @lock_dly: Lock circuit configuration settings for lock windowsize
 * @lock_cnt: Lock circuit counter setting
 */
struct xvcu_pll_cfg {
	u32 fbdiv;
	u32 cp;
	u32 res;
	u32 lfhf;
	u32 lock_dly;
	u32 lock_cnt;
};

/**
 * struct vcu_pll - VCU PLL control/status data
 * @hw:	Clock hardware
 * @pll_ctrl: PLL control register address
 * @pll_status: PLL status register address
 * @pll_cfg: PLL config register address
 * @lockbit: PLL lock status bit
 */
struct vcu_pll {
	struct clk_hw	hw;
	void __iomem	*pll_ctrl;
	void __iomem	*pll_status;
	void __iomem	*pll_cfg;
	u8		lockbit;
};

static struct clk_hw_onecell_data *vcu_clk_data;
static const char * const vcu_mux_parents[] = {
	"dummy_name",
	"vcu_pll_half"
};

static DEFINE_SPINLOCK(mcu_enc_lock);
static DEFINE_SPINLOCK(mcu_dec_lock);
static DEFINE_SPINLOCK(core_enc_lock);
static DEFINE_SPINLOCK(core_dec_lock);

static const struct xvcu_pll_cfg xvcu_pll_cfg[] = {
	{ 25, 3, 10, 3, 63, 1000 },
	{ 26, 3, 10, 3, 63, 1000 },
	{ 27, 4, 6, 3, 63, 1000 },
	{ 28, 4, 6, 3, 63, 1000 },
	{ 29, 4, 6, 3, 63, 1000 },
	{ 30, 4, 6, 3, 63, 1000 },
	{ 31, 6, 1, 3, 63, 1000 },
	{ 32, 6, 1, 3, 63, 1000 },
	{ 33, 4, 10, 3, 63, 1000 },
	{ 34, 5, 6, 3, 63, 1000 },
	{ 35, 5, 6, 3, 63, 1000 },
	{ 36, 5, 6, 3, 63, 1000 },
	{ 37, 5, 6, 3, 63, 1000 },
	{ 38, 5, 6, 3, 63, 975 },
	{ 39, 3, 12, 3, 63, 950 },
	{ 40, 3, 12, 3, 63, 925 },
	{ 41, 3, 12, 3, 63, 900 },
	{ 42, 3, 12, 3, 63, 875 },
	{ 43, 3, 12, 3, 63, 850 },
	{ 44, 3, 12, 3, 63, 850 },
	{ 45, 3, 12, 3, 63, 825 },
	{ 46, 3, 12, 3, 63, 800 },
	{ 47, 3, 12, 3, 63, 775 },
	{ 48, 3, 12, 3, 63, 775 },
	{ 49, 3, 12, 3, 63, 750 },
	{ 50, 3, 12, 3, 63, 750 },
	{ 51, 3, 2, 3, 63, 725 },
	{ 52, 3, 2, 3, 63, 700 },
	{ 53, 3, 2, 3, 63, 700 },
	{ 54, 3, 2, 3, 63, 675 },
	{ 55, 3, 2, 3, 63, 675 },
	{ 56, 3, 2, 3, 63, 650 },
	{ 57, 3, 2, 3, 63, 650 },
	{ 58, 3, 2, 3, 63, 625 },
	{ 59, 3, 2, 3, 63, 625 },
	{ 60, 3, 2, 3, 63, 625 },
	{ 61, 3, 2, 3, 63, 600 },
	{ 62, 3, 2, 3, 63, 600 },
	{ 63, 3, 2, 3, 63, 600 },
	{ 64, 3, 2, 3, 63, 600 },
	{ 65, 3, 2, 3, 63, 600 },
	{ 66, 3, 2, 3, 63, 600 },
	{ 67, 3, 2, 3, 63, 600 },
	{ 68, 3, 2, 3, 63, 600 },
	{ 69, 3, 2, 3, 63, 600 },
	{ 70, 3, 2, 3, 63, 600 },
	{ 71, 3, 2, 3, 63, 600 },
	{ 72, 3, 2, 3, 63, 600 },
	{ 73, 3, 2, 3, 63, 600 },
	{ 74, 3, 2, 3, 63, 600 },
	{ 75, 3, 2, 3, 63, 600 },
	{ 76, 3, 2, 3, 63, 600 },
	{ 77, 3, 2, 3, 63, 600 },
	{ 78, 3, 2, 3, 63, 600 },
	{ 79, 3, 2, 3, 63, 600 },
	{ 80, 3, 2, 3, 63, 600 },
	{ 81, 3, 2, 3, 63, 600 },
	{ 82, 3, 2, 3, 63, 600 },
	{ 83, 4, 2, 3, 63, 600 },
	{ 84, 4, 2, 3, 63, 600 },
	{ 85, 4, 2, 3, 63, 600 },
	{ 86, 4, 2, 3, 63, 600 },
	{ 87, 4, 2, 3, 63, 600 },
	{ 88, 4, 2, 3, 63, 600 },
	{ 89, 4, 2, 3, 63, 600 },
	{ 90, 4, 2, 3, 63, 600 },
	{ 91, 4, 2, 3, 63, 600 },
	{ 92, 4, 2, 3, 63, 600 },
	{ 93, 4, 2, 3, 63, 600 },
	{ 94, 4, 2, 3, 63, 600 },
	{ 95, 4, 2, 3, 63, 600 },
	{ 96, 4, 2, 3, 63, 600 },
	{ 97, 4, 2, 3, 63, 600 },
	{ 98, 4, 2, 3, 63, 600 },
	{ 99, 4, 2, 3, 63, 600 },
	{ 100, 4, 2, 3, 63, 600 },
	{ 101, 4, 2, 3, 63, 600 },
	{ 102, 4, 2, 3, 63, 600 },
	{ 103, 5, 2, 3, 63, 600 },
	{ 104, 5, 2, 3, 63, 600 },
	{ 105, 5, 2, 3, 63, 600 },
	{ 106, 5, 2, 3, 63, 600 },
	{ 107, 3, 4, 3, 63, 600 },
	{ 108, 3, 4, 3, 63, 600 },
	{ 109, 3, 4, 3, 63, 600 },
	{ 110, 3, 4, 3, 63, 600 },
	{ 111, 3, 4, 3, 63, 600 },
	{ 112, 3, 4, 3, 63, 600 },
	{ 113, 3, 4, 3, 63, 600 },
	{ 114, 3, 4, 3, 63, 600 },
	{ 115, 3, 4, 3, 63, 600 },
	{ 116, 3, 4, 3, 63, 600 },
	{ 117, 3, 4, 3, 63, 600 },
	{ 118, 3, 4, 3, 63, 600 },
	{ 119, 3, 4, 3, 63, 600 },
	{ 120, 3, 4, 3, 63, 600 },
	{ 121, 3, 4, 3, 63, 600 },
	{ 122, 3, 4, 3, 63, 600 },
	{ 123, 3, 4, 3, 63, 600 },
	{ 124, 3, 4, 3, 63, 600 },
	{ 125, 3, 4, 3, 63, 600 },
};

static int xvcu_divider_get_val(unsigned long rate, unsigned long parent_rate,
				const struct clk_div_table *table, u8 width,
				unsigned long flags)
{
	unsigned int div;

	if (flags & CLK_DIVIDER_ROUND_CLOSEST)
		div = DIV_ROUND_CLOSEST_ULL((u64)parent_rate, rate);
	else
		div = DIV_ROUND_UP_ULL((u64)parent_rate, rate);

	return min_t(unsigned int, div, div_mask(width));
}

static unsigned long xvcu_divider_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned int val;

	val = clk_readl(divider->reg) >> divider->shift;
	val &= div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static long xvcu_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int bestdiv;

	bestdiv = xvcu_divider_get_val(rate, *prate, divider->table,
				       divider->width, divider->flags);

	*prate = rate * bestdiv;

	return rate;
}

static int xvcu_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int value;
	u32 val;

	value = xvcu_divider_get_val(rate, parent_rate, divider->table,
				     divider->width, divider->flags);
	if (value < 0)
		return value;

	val = clk_readl(divider->reg);
	val &= ~(div_mask(divider->width) << divider->shift);
	val |= (u32)value << divider->shift;
	clk_writel(val, divider->reg);

	return 0;
}

static const struct clk_ops xvcu_divider_ops = {
	.recalc_rate = xvcu_divider_recalc_rate,
	.round_rate = xvcu_divider_round_rate,
	.set_rate = xvcu_divider_set_rate,
};

/**
 * xvcu_register_divider - Register custom divider hardware
 * @dev:		VCU clock device
 * @name:		Divider name
 * @parent_name:	Divider parent name
 * @flags:		Clock flags
 * @reg:		Divider register base address
 * @shift:		Divider bits shift
 * @width:		Divider bits width
 * @clk_divider_flags:	Divider specific flags
 * @lock:		Shared register lock
 *
 * Register custom divider hardware to CCF.
 *
 * Return: Clock hardware for generated clock
 */
static struct clk_hw *xvcu_register_divider(struct device *dev,
					    const char *name,
					    const char *parent_name,
					    unsigned long flags,
					    void __iomem *reg, u8 shift,
					    u8 width, u8 clk_divider_flags,
					    spinlock_t *lock)
{
	struct clk_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &xvcu_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;

	/* register the clock */
	hw = &div->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw;
}

/**
 * xvcu_pll_bypass_ctrl - Enable/Disable PLL bypass mode
 * @pll:	PLL data
 * @enable:	Enable/Disable flag
 *
 * Enable/Disable PLL bypass mode:
 *	0 - Disable
 *	1 - Enable
 */
static void xvcu_pll_bypass_ctrl(struct vcu_pll *pll, bool enable)
{
	u32 reg;

	reg = clk_readl(pll->pll_ctrl);
	if (enable)
		reg |= VCU_PLL_CTRL_BYPASS_MASK;
	else
		reg &= ~VCU_PLL_CTRL_BYPASS_MASK;
	clk_writel(reg, pll->pll_ctrl);
}

/**
 * xvcu_pll_config - Configure PLL based on FBDIV value
 * @pll:	PLL data
 *
 * PLL needs to be configured before taking out of reset. Configuration
 * data depends on the value of FBDIV for proper PLL locking.
 */
static void xvcu_pll_config(struct vcu_pll *pll)
{
	unsigned int fbdiv, reg;
	int i;

	reg = clk_readl(pll->pll_ctrl);
	fbdiv = (reg >> VCU_PLL_CTRL_FBDIV_SHIFT) & VCU_PLL_CTRL_FBDIV_MASK;

	for (i = ARRAY_SIZE(xvcu_pll_cfg) - 1; i >= 0; i--) {
		if (fbdiv != xvcu_pll_cfg[i].fbdiv)
			continue;

		/* Set RES, CP, LFHF, LOCK_CNT and LOCK_DLY cfg values */
		reg = (xvcu_pll_cfg[i].res << VCU_PLL_CFG_RES_SHIFT) |
		      (xvcu_pll_cfg[i].cp << VCU_PLL_CFG_CP_SHIFT) |
		      (xvcu_pll_cfg[i].lfhf << VCU_PLL_CFG_LFHF_SHIFT) |
		      (xvcu_pll_cfg[i].lock_cnt << VCU_PLL_CFG_LOCK_CNT_SHIFT) |
		      (xvcu_pll_cfg[i].lock_dly << VCU_PLL_CFG_LOCK_DLY_SHIFT);
		clk_writel(reg, pll->pll_cfg);
	}
}

/**
 * xvcu_pll_enable_disable - Enable/Disable PLL
 * @pll:	PLL data
 * @enable:	Enable/Disable flag
 *
 * Enable/Disable PLL based on request:
 *	0 - Disable
 *	1 - Enable
 */
static void xvcu_pll_enable_disable(struct vcu_pll *pll, bool enable)
{
	u32 reg;

	reg = clk_readl(pll->pll_ctrl);
	if (enable)
		reg &= ~(VCU_PLL_CTRL_RESET_MASK | VCU_PLL_CTRL_POR_IN_MASK |
				VCU_PLL_CTRL_PWR_POR_MASK);
	else
		reg |= (VCU_PLL_CTRL_RESET_MASK | VCU_PLL_CTRL_POR_IN_MASK |
				VCU_PLL_CTRL_PWR_POR_MASK);
	clk_writel(reg, pll->pll_ctrl);
}

/**
 * xvcu_pll_is_enabled - Check if PLL is enabled or not
 * @hw:		Clock hardware
 *
 * Check if PLL is enabled or not. PLL enabled means PLL is not in
 * reset state.
 *
 * Return: PLL status (0 - Disabled, 1 - Enabled)
 */
static int xvcu_pll_is_enabled(struct clk_hw *hw)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	u32 reg;

	reg = clk_readl(pll->pll_ctrl);

	return !(reg & (VCU_PLL_CTRL_RESET_MASK | VCU_PLL_CTRL_POR_IN_MASK |
		 VCU_PLL_CTRL_PWR_POR_MASK));
}

/**
 * xvcu_pll_enable - Enable PLL
 * @hw:		Clock hardware
 *
 * Enable PLL if it is not enabled. Configure PLL, enable and wait for
 * the PLL lock. Put PLL into bypass state during PLL configuration.
 *
 * Return: 0 on success else error code
 */
static int xvcu_pll_enable(struct clk_hw *hw)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	u32 reg;
	int ret;

	if (xvcu_pll_is_enabled(hw))
		return 0;

	pr_info("VCU PLL: enable\n");

	xvcu_pll_bypass_ctrl(pll, 1);

	xvcu_pll_config(pll);

	xvcu_pll_enable_disable(pll, 1);

	ret = readl_poll_timeout_atomic(pll->pll_status, reg,
					reg & VCU_PLL_STATUS_LOCK_STATUS_MASK,
					1, VCU_PLL_LOCK_TIMEOUT);
	if (ret) {
		pr_err("VCU PLL is not locked\n");
		return ret;
	}

	xvcu_pll_bypass_ctrl(pll, 0);

	return ret;
}

/**
 * xvcu_pll_disable - Disable PLL
 * @hw:		Clock hardware
 *
 * Disable PLL if it is enabled.
 *
 * Return: 0 on success else error code
 */
static void xvcu_pll_disable(struct clk_hw *hw)
{
	struct vcu_pll *pll = to_vcu_pll(hw);

	if (!xvcu_pll_is_enabled(hw))
		return;

	pr_info("PLL: shutdown\n");
	xvcu_pll_enable_disable(pll, 0);
}

/**
 * xvcu_pll_frac_get_mode - Get PLL fraction mode
 * @hw:		Clock hardware
 *
 * Check if PLL is configured for integer mode or fraction mode.
 *
 * Return: PLL mode:
 *	PLL_MODE_FRAC - Fraction mode
 *	PLL_MODE_INT - Integer mode
 */
static inline enum pll_mode xvcu_pll_frac_get_mode(struct clk_hw *hw)
{
	struct vcu_pll *clk = to_vcu_pll(hw);
	u32 reg;

	reg = clk_readl(clk->pll_ctrl + FRAC_OFFSET);

	reg = reg & PLLFCFG_FRAC_EN;
	return reg ? PLL_MODE_FRAC : PLL_MODE_INT;
}

/**
 * xvcu_pll_frac_set_mode - Set PLL fraction mode
 * @hw:		Clock hardware
 * @on:		Enable/Disable flag
 *
 * Configure PLL for integer mode or fraction mode.
 *	1 - Fraction mode
 *	0 - Integer mode
 */
static inline void xvcu_pll_frac_set_mode(struct clk_hw *hw, bool on)
{
	struct vcu_pll *clk = to_vcu_pll(hw);
	u32 reg = 0;

	if (on)
		reg = PLLFCFG_FRAC_EN;

	reg = clk_readl(clk->pll_ctrl + FRAC_OFFSET);
	reg |= PLLFCFG_FRAC_EN;
	clk_writel(reg, (clk->pll_ctrl + FRAC_OFFSET));
}

static long vcu_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	u32 fbdiv;
	long rate_div, f;

	/* Enable the fractional mode if needed */
	rate_div = (rate * FRAC_DIV) / *prate;
	f = rate_div % FRAC_DIV;
	xvcu_pll_frac_set_mode(hw, !!f);

	if (xvcu_pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
		if (rate > FVCO_MAX) {
			fbdiv = rate / FVCO_MAX;
			rate = rate / (fbdiv + 1);
		}
		if (rate < FVCO_MIN) {
			fbdiv = DIV_ROUND_UP(FVCO_MIN, rate);
			rate = rate * fbdiv;
		}
		return rate;
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, *prate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	return *prate * fbdiv;
}

static unsigned long vcu_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	u32 fbdiv, data, reg;
	unsigned long rate, frac;

	reg = clk_readl(pll->pll_ctrl);
	fbdiv = (reg >> VCU_PLL_CTRL_FBDIV_SHIFT) & VCU_PLL_CTRL_FBDIV_MASK;

	rate = parent_rate * fbdiv;
	if (xvcu_pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
		data = (clk_readl(pll->pll_ctrl + FRAC_OFFSET) & 0xFFFF);
		frac = (parent_rate * data) / FRAC_DIV;
		rate = rate + frac;
	}

	return rate;
}

static int vcu_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vcu_pll *pll = to_vcu_pll(hw);
	u32 fbdiv, reg;
	long rate_div, frac, m, f;

	if (xvcu_pll_frac_get_mode(hw) == PLL_MODE_FRAC) {
		rate_div = ((rate * FRAC_DIV) / parent_rate);
		m = rate_div / FRAC_DIV;
		f = rate_div % FRAC_DIV;
		m = clamp_t(u32, m, (PLL_FBDIV_MIN), (PLL_FBDIV_MAX));
		rate = parent_rate * m;
		frac = (parent_rate * f) / FRAC_DIV;
		reg = clk_readl(pll->pll_ctrl);
		reg &= ~(VCU_PLL_CTRL_FBDIV_MASK << VCU_PLL_CTRL_FBDIV_SHIFT);
		reg |= m << VCU_PLL_CTRL_FBDIV_SHIFT;
		clk_writel(reg, pll->pll_ctrl);

		reg = clk_readl(pll->pll_ctrl + FRAC_OFFSET);
		reg &= ~0xFFFF;
		reg |= (f & 0xFFFF);
		clk_writel(reg, pll->pll_ctrl + FRAC_OFFSET);

		return (rate + frac);
	}

	fbdiv = DIV_ROUND_CLOSEST(rate, parent_rate);
	fbdiv = clamp_t(u32, fbdiv, PLL_FBDIV_MIN, PLL_FBDIV_MAX);
	reg = clk_readl(pll->pll_ctrl);
	reg &= ~(VCU_PLL_CTRL_FBDIV_MASK << VCU_PLL_CTRL_FBDIV_SHIFT);
	reg |= fbdiv << VCU_PLL_CTRL_FBDIV_SHIFT;
	clk_writel(reg, pll->pll_ctrl);

	return parent_rate * fbdiv;
}

static const struct clk_ops vcu_pll_ops = {
	.enable = xvcu_pll_enable,
	.disable = xvcu_pll_disable,
	.is_enabled = xvcu_pll_is_enabled,
	.round_rate = vcu_pll_round_rate,
	.recalc_rate = vcu_pll_recalc_rate,
	.set_rate = vcu_pll_set_rate,
};

/**
 * xvcu_register_pll - Register VCU PLL
 * @dev:	VCU clock device
 * @name:	PLL name
 * @parent:	PLL parent
 * @reg_base:	PLL register base address
 * @flags:	Hardware specific flags
 *
 * Register PLL to CCF.
 *
 * Return: Clock hardware for generated clock
 */
static struct clk_hw *xvcu_register_pll(struct device *dev, const char *name,
					const char *parent,
					void __iomem *reg_base,
					unsigned long flags)
{
	struct vcu_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	init.name = name;
	init.parent_names = &parent;
	init.ops = &vcu_pll_ops;
	init.num_parents = 1;
	init.flags = flags;

	pll = devm_kmalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->hw.init = &init;
	pll->pll_ctrl = reg_base + VCU_PLL_CTRL;
	pll->pll_status = reg_base + VCU_PLL_STATUS;
	pll->pll_cfg = reg_base + VCU_PLL_CFG;
	pll->lockbit = VCU_PLL_STATUS_LOCK_STATUS_MASK;

	hw = &pll->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	clk_hw_set_rate_range(hw, FVCO_MIN, FVCO_MAX);
	if (ret < 0)
		pr_err("%s:ERROR clk_set_rate_range failed %d\n", name, ret);

	return hw;
}

/**
 * register_vcu_leaf_clocks - Register VCU leaf clocks
 * @dev:		VCU clock device
 * @name:		Clock name
 * @parents:		Clock parents
 * @nparents:		Clock parent count
 * @default_parent:	Default parent to set
 * @reg:		Clock control register address
 * @lock:		Clock register access lock
 *
 * Register VCU leaf clocks. These clocks are MCU/core
 * encoder and decoder clocks. Topology for these clocks
 * are Mux, Divisor and Gate.
 *
 * Return: Clock hardware for the generated gate clock
 */
static struct clk_hw *register_vcu_leaf_clocks(struct device *dev,
					       const char *name,
					       const char * const *parents,
					       u8 nparents,
					       struct clk *default_parent,
					       void __iomem *reg,
					       spinlock_t *lock)
{
	char *clk_mux, *clk_div;
	struct clk_hw *hw;

	clk_mux = devm_kasprintf(dev, GFP_KERNEL, "%s%s", name, "_mux");
	hw = clk_hw_register_mux(dev, clk_mux, parents, nparents,
				 CLK_SET_RATE_PARENT | CLK_IS_BASIC |
				 CLK_SET_RATE_NO_REPARENT,
				 reg, VCU_SRCSEL_SHIFT, 1, 0, lock);

	if (default_parent)
		clk_set_parent(hw->clk, default_parent);

	clk_div = devm_kasprintf(dev, GFP_KERNEL, "%s%s", name, "_div");
	xvcu_register_divider(dev, clk_div, clk_mux,
			      CLK_IS_BASIC | CLK_SET_RATE_PARENT |
			      CLK_SET_RATE_NO_REPARENT,
			      reg, VCU_PLL_DIVISOR_SHIFT, 6,
			      CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
			      CLK_DIVIDER_ROUND_CLOSEST,
			      lock);

	return clk_hw_register_gate(dev, name, clk_div,
				    CLK_SET_RATE_PARENT | CLK_IS_BASIC,
				    reg, 12, 0, lock);
}

/**
 * unregister_vcu_leaf_clocks - Unegister VCU leaf clocks
 * @hw:		VCU leaf clock hardware
 *
 * Unregister VCU leaf clocks. These clocks are MCU/core
 * encoder and decoder clocks. Unregister clocks in order
 * from gate, div and mux maintaining their parent dependency.
 *
 */
static void unregister_vcu_leaf_clocks(struct clk_hw *hw)
{
	struct clk_hw *parent;

	parent = clk_hw_get_parent(hw);
	clk_hw_unregister_gate(hw);
	hw = parent;

	parent = clk_hw_get_parent(hw);
	clk_hw_unregister_divider(hw);
	hw = parent;

	clk_hw_unregister_mux(hw);
}

/**
 * xvcu_clock_init - Initialize VCU clocks
 * @dev:	VCU clock device
 * @reg_base:	Clock register base address
 *
 * Register VCU PLL and clocks and add VCU to clock provider list.
 *
 * Return: 0 on success else error code.
 */
static int xvcu_clock_init(struct device *dev, void __iomem *reg_base)
{
	struct clk_hw *hw;
	struct clk *ref_clk;
	const char *parent;
	u32 vcu_pll_ctrl, clkoutdiv;
	int i;

	ref_clk = devm_clk_get(dev, "pll_ref");
	if (IS_ERR(ref_clk)) {
		dev_err(dev, "failed to get pll_ref clock\n");
		return PTR_ERR(ref_clk);
	}

	vcu_clk_data = devm_kzalloc(dev, sizeof(*vcu_clk_data) +
				    sizeof(*vcu_clk_data->hws) * clk_max,
				    GFP_KERNEL);
	if (!vcu_clk_data)
		return -ENOMEM;

	parent = __clk_get_name(ref_clk);
	hw = xvcu_register_pll(dev, "vcu_pll", parent, reg_base,
			       CLK_SET_RATE_NO_REPARENT);
	if (IS_ERR(hw)) {
		dev_err(dev, "VCU PLL registration failed\n");
		return PTR_ERR(hw);
	}

	/*
	 * The divide-by-2 should be always enabled (== 1) to meet the timing
	 * in the design. Otherwise, it's an error
	 */
	vcu_pll_ctrl = clk_readl(reg_base + VCU_PLL_CTRL);
	clkoutdiv = vcu_pll_ctrl >> VCU_PLL_CTRL_CLKOUTDIV_SHIFT;
	clkoutdiv = clkoutdiv & VCU_PLL_CTRL_CLKOUTDIV_MASK;
	if (clkoutdiv != 1) {
		dev_err(dev, "clkoutdiv is invalid\n");
		return -EINVAL;
	}

	vcu_clk_data->hws[vcu_pll_half] =
		clk_hw_register_fixed_factor(dev, "vcu_pll_half", "vcu_pll",
					     CLK_SET_RATE_NO_REPARENT |
					     CLK_SET_RATE_PARENT,
					     1, 2);

	vcu_clk_data->hws[vcu_core_enc] =
		register_vcu_leaf_clocks(dev, "vcu_core_enc_clk",
					 vcu_mux_parents, 2,
					 vcu_clk_data->hws[vcu_pll_half]->clk,
					 reg_base + VCU_ENC_CORE_CTRL,
					 &core_enc_lock);
	vcu_clk_data->hws[vcu_core_dec] =
		register_vcu_leaf_clocks(dev, "vcu_core_dec_clk",
					 vcu_mux_parents, 2,
					 vcu_clk_data->hws[vcu_pll_half]->clk,
					 reg_base + VCU_DEC_CORE_CTRL,
					 &core_dec_lock);
	vcu_clk_data->hws[mcu_core_enc] =
		register_vcu_leaf_clocks(dev, "mcu_core_enc_clk",
					 vcu_mux_parents, 2,
					 vcu_clk_data->hws[vcu_pll_half]->clk,
					 reg_base + VCU_ENC_MCU_CTRL,
					 &mcu_enc_lock);
	vcu_clk_data->hws[mcu_core_dec] =
		register_vcu_leaf_clocks(dev, "mcu_core_dec_clk",
					 vcu_mux_parents, 2,
					 vcu_clk_data->hws[vcu_pll_half]->clk,
					 reg_base + VCU_DEC_MCU_CTRL,
					 &mcu_dec_lock);

	for (i = 0; i < clk_max; i++) {
		if (IS_ERR(vcu_clk_data->hws[i])) {
			dev_err(dev, "clk %d: register failed with %ld\n",
				i, PTR_ERR(vcu_clk_data->hws[i]));
		}
	}

	vcu_clk_data->num = clk_max;
	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
				      vcu_clk_data);
}

static int xvcu_clk_probe(struct platform_device *pdev)
{
	struct xvcu_device *xvcu = dev_get_drvdata(pdev->dev.parent);
	int ret;

	ret = xvcu_clock_init(pdev->dev.parent, xvcu->vcu_slcr_ba);
	if (ret)
		dev_err(&pdev->dev, "clock init fail with error %d\n", ret);
	else
		dev_dbg(&pdev->dev, "clock init successful\n");

	return ret;
}

static int xvcu_clk_remove(struct platform_device *pdev)
{
	unregister_vcu_leaf_clocks(vcu_clk_data->hws[vcu_core_enc]);
	unregister_vcu_leaf_clocks(vcu_clk_data->hws[vcu_core_dec]);
	unregister_vcu_leaf_clocks(vcu_clk_data->hws[mcu_core_enc]);
	unregister_vcu_leaf_clocks(vcu_clk_data->hws[mcu_core_dec]);
	clk_hw_unregister(vcu_clk_data->hws[vcu_pll_half]);
	of_clk_del_provider(pdev->dev.parent->of_node);

	devm_kfree(pdev->dev.parent, vcu_clk_data);

	return 0;
}

static struct platform_driver xvcu_clk_driver = {
	.driver = {
		.name = "xilinx-vcu-clk",
	},
	.probe = xvcu_clk_probe,
	.remove = xvcu_clk_remove,
};

module_platform_driver(xvcu_clk_driver);

MODULE_AUTHOR("Rajan Vaja <rajan.vaja@xilinx.com>");
MODULE_DESCRIPTION("Xilinx VCU clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:xilinx-vcu-clk");
