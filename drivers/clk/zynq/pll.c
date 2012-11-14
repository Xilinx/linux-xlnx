/**
 * Clock implementation modeling the PLLs used in Xilinx Zynq.
 * All PLLs are sourced by the fixed rate PS_CLK.
 * Rate is adjustable by reprogramming the feedback divider.
 * PLLs can be bypassed. When the bypass bit is set the PLL_OUT = PS_CLK
 *
 * The bypass functionality is modelled as mux. The parent clock is the same in
 * both cases, but only in one case the input clock is multiplied by fbdiv.
 * Bypassing the PLL also shuts it down.
 *
 * Functions to set a new rate are provided, though they are only compile
 * tested!!! There is no code calling those, yet.
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
 * struct zynq_pll
 * @hw:		Handle between common and hardware-specific interfaces
 * @pllctrl:	PLL control register
 * @pllcfg:	PLL config register
 * @pllstatus:	PLL status register
 * @lock:	Register lock
 * @lockbit:	Indicates the associated PLL_LOCKED bit in the PLL status
 *		register.
 * @bypassed:	Indicates PLL bypass. 1 = bypassed, 0 = PLL output
 */
struct zynq_pll {
	struct clk_hw	hw;
	void __iomem	*pllctrl;
	void __iomem	*pllcfg;
	void __iomem	*pllstatus;
	spinlock_t	*lock;
	u8		lockbit;
	u8		bypassed;
};
#define to_zynq_pll(_hw)	container_of(_hw, struct zynq_pll, hw)

/* Register bitfield defines */
#define PLLCTRL_FBDIV_MASK	0x7f000
#define PLLCTRL_FBDIV_SHIFT	12
#define PLLCTRL_BYPASS_MASK	0x10
#define PLLCTRL_BYPASS_SHIFT	4
#define PLLCTRL_PWRDWN_MASK	2
#define PLLCTRL_PWRDWN_SHIFT	1
#define PLLCTRL_RESET_MASK	1
#define PLLCTRL_RESET_SHIFT	0
#define PLLCFG_PLLRES_MASK	0xf0
#define PLLCFG_PLLRES_SHIFT	4
#define PLLCFG_PLLCP_MASK	0xf00
#define PLLCFG_PLLCP_SHIFT	8
#define PLLCFG_LOCKCNT_MASK	0x3ff000
#define PLLCFG_LOCKCNT_SHIFT	12

/**
 * zynq_pll_get_params() - Get PLL parameters for given feedback divider
 * @fbdiv: Desired feedback divider
 * @rpll_cp: PLL_CP value (return value)
 * @rpll_res: PLL_RES value (return value)
 * @rlock_cnt: LOCK_CNT value (return value)
 * Returns 0 on success.
 */
static int zynq_pll_get_pll_params(unsigned int fbdiv, u32 *rpll_cp,
		u32 *rpll_res, u32 *rlock_cnt)
{
	unsigned int pll_cp;
	unsigned int pll_res;
	unsigned int lock_cnt;

	/* Check that fbdiv is in a valid range */
	if ((fbdiv < 13) || (fbdiv > 66))
		return -EINVAL;

	/* Set other PLL parameters according to target fbdiv */
	if ((fbdiv >= 41) && (fbdiv <= 47))
		pll_cp = 3;
	else
		pll_cp = 2;

	if (fbdiv <= 15)
		pll_res = 6;
	else if ((fbdiv >= 16) && (fbdiv <= 19))
		pll_res = 10;
	else if ((fbdiv >= 31) && (fbdiv <= 40))
		pll_res = 2;
	else if (fbdiv >= 48)
		pll_res = 4;
	else
		pll_res = 12;

	switch (fbdiv) {
	case 13:
		lock_cnt = 750;
		break;
	case 14:
		lock_cnt = 700;
		break;
	case 15:
		lock_cnt = 650;
		break;
	case 16:
		lock_cnt = 625;
		break;
	case 17:
		lock_cnt = 575;
		break;
	case 18:
		lock_cnt = 550;
		break;
	case 19:
		lock_cnt = 525;
		break;
	case 20:
		lock_cnt = 500;
		break;
	case 21:
		lock_cnt = 475;
		break;
	case 22:
		lock_cnt = 450;
		break;
	case 23:
		lock_cnt = 425;
		break;
	case 24 ... 25:
		lock_cnt = 400;
		break;
	case 26:
		lock_cnt = 375;
		break;
	case 27 ... 28:
		lock_cnt = 350;
		break;
	case 29 ... 30:
		lock_cnt = 325;
		break;
	case 31 ... 33:
		lock_cnt = 300;
		break;
	case 34 ... 36:
		lock_cnt = 275;
		break;
	default:
		lock_cnt = 250;
		break;
	}

	*rpll_cp = pll_cp;
	*rpll_res = pll_res;
	*rlock_cnt = lock_cnt;
	return 0;
}

/**
 * zynq_pll_set_rate() - Change frequency of a PLL
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns 0 on success, negative errno otherwise.
 */
static int zynq_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long prate)
{
	struct zynq_pll *clk = to_zynq_pll(hw);
	u32 reg, fbdiv, pll_res, pll_cp, lock_cnt;
	unsigned long flags;

	/*
	 * Set a new rate to the PLL includes bypassing and resetting the PLL,
	 * hence the connected subsystem will see old_f->bypass_f->new_f. Every
	 * driver must register clock notifiers for its clock to make sure it is
	 * asked for rate changes. This way it can make sure it can work with
	 * new_f and do whatever is necessary to continue working after such a
	 * change.
	 */
	/* Rate change is only possible if not bypassed */
	if (clk->bypassed)
		return -EINVAL;

	fbdiv = DIV_ROUND_CLOSEST(rate, prate);
	if (zynq_pll_get_pll_params(fbdiv, &pll_cp, &pll_res, &lock_cnt))
		return -EINVAL;

	spin_lock_irqsave(clk->lock, flags);

	/* Write new parameters */
	reg = readl(clk->pllctrl);
	reg &= ~PLLCTRL_FBDIV_MASK;
	reg |= (fbdiv << PLLCTRL_FBDIV_SHIFT) & PLLCTRL_FBDIV_MASK;
	writel(reg, clk->pllctrl);

	reg = (pll_res << PLLCFG_PLLRES_SHIFT) & PLLCFG_PLLRES_MASK;
	reg |= (pll_cp << PLLCFG_PLLCP_SHIFT) & PLLCFG_PLLCP_MASK;
	reg |= (lock_cnt << PLLCFG_LOCKCNT_SHIFT) & PLLCFG_LOCKCNT_MASK;
	writel(reg, clk->pllcfg);

	/* bypass PLL */
	reg = readl(clk->pllctrl);
	reg |= PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pllctrl);
	/* reset PLL */
	reg |= PLLCTRL_RESET_MASK;
	writel(reg, clk->pllctrl);
	reg &= ~PLLCTRL_RESET_MASK;
	writel(reg, clk->pllctrl);
	/* wait for PLL lock */
	while (readl(clk->pllstatus) & (1 << clk->lockbit)) ;
	/* remove bypass */
	reg &= ~PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pllctrl);

	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_pll_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long zynq_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct zynq_pll *clk = to_zynq_pll(hw);
	u32 fbdiv;

	if (clk->bypassed)
		return *prate;

	fbdiv = DIV_ROUND_CLOSEST(rate, *prate);
	if (fbdiv < 13)
		fbdiv = 13;
	else if (fbdiv > 66)
		fbdiv = 66;

	return *prate * fbdiv;
}

/**
 * zynq_pll_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long zynq_pll_recalc_rate(struct clk_hw *hw, unsigned long
		parent_rate)
{
	struct zynq_pll *clk = to_zynq_pll(hw);
	u32 fbdiv;

	/* makes probably sense to redundantly save fbdiv in the struct
	 * zynq_pll to save the IO access. */
	fbdiv = (readl(clk->pllctrl) & PLLCTRL_FBDIV_MASK) >>
		PLLCTRL_FBDIV_SHIFT;

	return parent_rate * fbdiv;
}

/**
 * zynq_pll_set_parent() - Reparent clock
 * @hw:		Handle between common and hardware-specific interfaces
 * @index:	Index of new parent.
 * Returns 0 on success, negative errno otherwise.
 */
static int zynq_pll_set_parent(struct clk_hw *hw, u8 index)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	/*
	 * We assume bypassing is a preparation for sleep mode, thus not only
	 * set the bypass bit, but also power down the whole PLL.
	 * For this reason, removing the bypass must do the power up sequence
	 */
	switch (index) {
	case 0:
		/* Power up PLL and wait for lock before removing bypass */
		spin_lock_irqsave(clk->lock, flags);

		reg = readl(clk->pllctrl);
		reg &= ~(PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK);
		writel(reg, clk->pllctrl);
		while (readl(clk->pllstatus) & (1 << clk->lockbit)) ;

		reg = readl(clk->pllctrl);
		reg &= ~PLLCTRL_BYPASS_MASK;
		writel(reg, clk->pllctrl);

		spin_unlock_irqrestore(clk->lock, flags);

		clk->bypassed = 0;
		break;
	case 1:
		/* Set bypass bit and shut down PLL */
		spin_lock_irqsave(clk->lock, flags);

		reg = readl(clk->pllctrl);
		reg |= PLLCTRL_BYPASS_MASK;
		writel(reg, clk->pllctrl);
		reg |= PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK;
		writel(reg, clk->pllctrl);

		spin_unlock_irqrestore(clk->lock, flags);

		clk->bypassed = 1;
		break;
	default:
		/* Is this correct error code? */
		return -EINVAL;
	}

	return 0;
}

/**
 * zynq_pll_get_parent() - Reparent clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns the index of the current clock parent.
 */
static u8 zynq_pll_get_parent(struct clk_hw *hw)
{
	struct zynq_pll *clk = to_zynq_pll(hw);

	return clk->bypassed;
}

static const struct clk_ops zynq_pll_ops = {
	.set_parent = zynq_pll_set_parent,
	.get_parent = zynq_pll_get_parent,
	.set_rate = zynq_pll_set_rate,
	.round_rate = zynq_pll_round_rate,
	.recalc_rate = zynq_pll_recalc_rate
};

/**
 * clk_register_zynq_pll() - Register PLL with the clock framework
 * @name:	Clock name
 * @pllctrl:	Pointer to PLL control register
 * @pllcfg:	Pointer to PLL configuration register
 * @pllstatus:	Pointer to PLL status register
 * @lockbit:	Indicates the associated PLL_LOCKED bit in the PLL status
 *		register.
 * @lock:	Register lock
 * Returns clk_register() return value or errpointer.
 */
struct clk *clk_register_zynq_pll(const char *name, void __iomem *pllctrl,
		void __iomem *pllcfg, void __iomem *pllstatus, u8 lockbit,
		spinlock_t *lock)
{
	struct zynq_pll *clk;
	const char *pnames[] = {"PS_CLK", "PS_CLK"};
	struct clk_init_data initd = {
		.name = name,
		.ops = &zynq_pll_ops,
		.parent_names = pnames,
		.num_parents = 2,
		.flags = 0
	};

	clk = kmalloc(sizeof(*clk), GFP_KERNEL);
	clk->hw.init = &initd;

	if (!clk) {
		pr_err("%s: could not allocate Zynq PLL clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* Populate the struct */
	clk->pllctrl = pllctrl;
	clk->pllcfg = pllcfg;
	clk->pllstatus = pllstatus;
	clk->lockbit = lockbit;
	clk->lock = lock;

	if (readl(clk->pllctrl) & PLLCTRL_BYPASS_MASK)
		clk->bypassed = 1;
	else
		clk->bypassed = 0;

	return clk_register(NULL, &clk->hw);
}
