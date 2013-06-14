/*
 * Zynq clock initalization code
 * Code is based on clock code from the orion/kirkwood architecture.
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
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk/zynq.h>

#define SLCR_ARM_CLK_CTRL		(slcr_base + 0x120)
#define SLCR_DDR_CLK_CTRL		(slcr_base + 0x124)
#define SLCR_DCI_CLK_CTRL		(slcr_base + 0x128)
#define SLCR_APER_CLK_CTRL		(slcr_base + 0x12c)
#define SLCR_GEM0_CLK_CTRL		(slcr_base + 0x140)
#define SLCR_GEM1_CLK_CTRL		(slcr_base + 0x144)
#define SLCR_SMC_CLK_CTRL		(slcr_base + 0x148)
#define SLCR_LQSPI_CLK_CTRL		(slcr_base + 0x14c)
#define SLCR_SDIO_CLK_CTRL		(slcr_base + 0x150)
#define SLCR_UART_CLK_CTRL		(slcr_base + 0x154)
#define SLCR_SPI_CLK_CTRL		(slcr_base + 0x158)
#define SLCR_CAN_CLK_CTRL		(slcr_base + 0x15c)
#define SLCR_DBG_CLK_CTRL		(slcr_base + 0x164)
#define SLCR_PCAP_CLK_CTRL		(slcr_base + 0x168)
#define SLCR_FPGA0_CLK_CTRL		(slcr_base + 0x170)
#define SLCR_FPGA1_CLK_CTRL		(slcr_base + 0x180)
#define SLCR_FPGA2_CLK_CTRL		(slcr_base + 0x190)
#define SLCR_FPGA3_CLK_CTRL		(slcr_base + 0x1a0)
#define SLCR_621_TRUE			(slcr_base + 0x1c4)

static void __iomem *zynq_slcr_base;


/* clock implementation for Zynq PLLs */

/**
 * struct zynq_pll
 * @hw:		Handle between common and hardware-specific interfaces
 * @pll_ctrl:	PLL control register
 * @pll_cfg:	PLL config register
 * @pll_status:	PLL status register
 * @lock:	Register lock
 * @lockbit:	Indicates the associated PLL_LOCKED bit in the PLL status
 *		register.
 * @bypassed:	Indicates PLL bypass. 1 = bypassed, 0 = PLL output
 */
struct zynq_pll {
	struct clk_hw	hw;
	void __iomem	*pll_ctrl;
	void __iomem	*pll_cfg;
	void __iomem	*pll_status;
	spinlock_t	lock;
	u32		lockbit;
	u8		bypassed;
};
#define to_zynq_pll(_hw)	container_of(_hw, struct zynq_pll, hw)

/* Register bitfield defines */
#define PLLCTRL_FBDIV_MASK	0x7f000
#define PLLCTRL_FBDIV_SHIFT	12
#define PLLCTRL_BYPASS_MASK	0x10
#define PLLCTRL_BYPASS_SHIFT	4
#define PLLCTRL_BPQUAL_MASK	(1 << 3)
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

	spin_lock_irqsave(&clk->lock, flags);

	/* Write new parameters */
	reg = readl(clk->pll_ctrl);
	reg &= ~PLLCTRL_FBDIV_MASK;
	reg |= (fbdiv << PLLCTRL_FBDIV_SHIFT) & PLLCTRL_FBDIV_MASK;
	writel(reg, clk->pll_ctrl);

	reg = (pll_res << PLLCFG_PLLRES_SHIFT) & PLLCFG_PLLRES_MASK;
	reg |= (pll_cp << PLLCFG_PLLCP_SHIFT) & PLLCFG_PLLCP_MASK;
	reg |= (lock_cnt << PLLCFG_LOCKCNT_SHIFT) & PLLCFG_LOCKCNT_MASK;
	writel(reg, clk->pll_cfg);

	/* bypass PLL */
	reg = readl(clk->pll_ctrl);
	reg |= PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pll_ctrl);
	/* reset PLL */
	reg |= PLLCTRL_RESET_MASK;
	writel(reg, clk->pll_ctrl);
	reg &= ~PLLCTRL_RESET_MASK;
	writel(reg, clk->pll_ctrl);
	/* wait for PLL lock */
	while (readl(clk->pll_status) & (1 << clk->lockbit)) ;
	/* remove bypass */
	reg &= ~PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pll_ctrl);

	spin_unlock_irqrestore(&clk->lock, flags);

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

	if (clk->bypassed)
		return parent_rate;

	/* makes probably sense to redundantly save fbdiv in the struct
	 * zynq_pll to save the IO access. */
	fbdiv = (readl(clk->pll_ctrl) & PLLCTRL_FBDIV_MASK) >>
		PLLCTRL_FBDIV_SHIFT;

	return parent_rate * fbdiv;
}

/**
 * zynq_pll_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static int zynq_pll_enable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	if (!clk->bypassed)
		return 0;

	pr_info("PLL: Enable\n");

	/* Power up PLL and wait for lock before removing bypass */
	spin_lock_irqsave(&clk->lock, flags);

	reg = readl(clk->pll_ctrl);
	reg &= ~(PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK);
	writel(reg, clk->pll_ctrl);
	while (!(readl(clk->pll_status) & (1 << clk->lockbit)))
		;

	reg = readl(clk->pll_ctrl);
	reg &= ~PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pll_ctrl);

	spin_unlock_irqrestore(&clk->lock, flags);

	clk->bypassed = 0;

	return 0;
}

/**
 * zynq_pll_disable - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static void zynq_pll_disable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	if (clk->bypassed)
		return;

	pr_info("PLL: Bypass\n");

	/* Set bypass bit and shut down PLL */
	spin_lock_irqsave(&clk->lock, flags);

	reg = readl(clk->pll_ctrl);
	reg |= PLLCTRL_BYPASS_MASK;
	writel(reg, clk->pll_ctrl);
	reg |= PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK;
	writel(reg, clk->pll_ctrl);

	spin_unlock_irqrestore(&clk->lock, flags);

	clk->bypassed = 1;
}

/**
 * zynq_pll_is_enabled - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 1 if the clock is enabled, 0 otherwise.
 *
 * Not sure this is a good idea, but since disabled means bypassed for
 * this clock implementation we say we are always enabled.
 */
static int zynq_pll_is_enabled(struct clk_hw *hw)
{
	return 1;
}

static const struct clk_ops zynq_pll_ops = {
	.enable = zynq_pll_enable,
	.disable = zynq_pll_disable,
	.is_enabled = zynq_pll_is_enabled,
	.set_rate = zynq_pll_set_rate,
	.round_rate = zynq_pll_round_rate,
	.recalc_rate = zynq_pll_recalc_rate
};

/**
 * clk_register_zynq_pll() - Register PLL with the clock framework
 * @np	Pointer to the DT device node
 */
static void clk_register_zynq_pll(struct device_node *np)
{
	struct zynq_pll *pll;
	struct clk *clk;
	int ret;
	u32 reg;
	u32 regs[3];
	const char *parent_name;
	unsigned long flags = 0;
	struct clk_init_data initd = {
		.ops = &zynq_pll_ops,
		.num_parents = 1,
		.flags = 0
	};

	ret = of_property_read_u32_array(np, "reg", regs, ARRAY_SIZE(regs));
	if (WARN_ON(ret))
		return;

	if (of_property_read_string(np, "clock-output-names", &initd.name))
		initd.name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);
	initd.parent_names = &parent_name;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: Could not allocate Zynq PLL clk.\n", __func__);
		return;
	}

	/* Populate the struct */
	pll->hw.init = &initd;
	pll->pll_ctrl = zynq_slcr_base + regs[0];
	pll->pll_cfg = zynq_slcr_base + regs[1];
	pll->pll_status = zynq_slcr_base + regs[2];
	spin_lock_init(&pll->lock);
	ret = of_property_read_u32(np, "lockbit", &pll->lockbit);
	if (WARN_ON(ret))
		goto free_pll;


	if (readl(pll->pll_ctrl) & PLLCTRL_BYPASS_MASK)
		pll->bypassed = 1;
	else
		pll->bypassed = 0;

	spin_lock_irqsave(&pll->lock, flags);

	reg = readl(pll->pll_ctrl);
	reg &= ~PLLCTRL_BPQUAL_MASK;
	writel(reg, pll->pll_ctrl);

	spin_unlock_irqrestore(&pll->lock, flags);

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		goto free_pll;

	WARN_ON(of_clk_add_provider(np, of_clk_src_simple_get, clk));
	/*
	 * at least until all clock lookups and init is converted to DT add a
	 * clkdev to help clk lookups
	 */
	clk_register_clkdev(clk, NULL, initd.name);

	return;

free_pll:
	kfree(pll);
}


static DEFINE_SPINLOCK(armclk_lock);
static DEFINE_SPINLOCK(ddrclk_lock);
static DEFINE_SPINLOCK(dciclk_lock);
static DEFINE_SPINLOCK(pcapclk_lock);
static DEFINE_SPINLOCK(smcclk_lock);
static DEFINE_SPINLOCK(lqspiclk_lock);
static DEFINE_SPINLOCK(gem0clk_lock);
static DEFINE_SPINLOCK(gem1clk_lock);
static DEFINE_SPINLOCK(fpga0clk_lock);
static DEFINE_SPINLOCK(fpga1clk_lock);
static DEFINE_SPINLOCK(fpga2clk_lock);
static DEFINE_SPINLOCK(fpga3clk_lock);
static DEFINE_SPINLOCK(canclk_lock);
static DEFINE_SPINLOCK(sdioclk_lock);
static DEFINE_SPINLOCK(uartclk_lock);
static DEFINE_SPINLOCK(spiclk_lock);
static DEFINE_SPINLOCK(dbgclk_lock);
static DEFINE_SPINLOCK(aperclk_lock);


/* Clock parent arrays */
static const char *cpu_parents[] __initdata = {"armpll", "armpll",
	"ddrpll", "iopll"};
static const char *def_periph_parents[] __initdata = {"iopll", "iopll",
	"armpll", "ddrpll"};
static const char *gem_parents[] __initdata = {"iopll", "iopll", "armpll",
	"ddrpll", "GEM0EMIO", "GEM0EMIO", "GEM0EMIO", "GEM0EMIO"};
static const char *dbg_parents[] __initdata = {"iopll", "iopll", "armpll",
	"ddrpll", "DBGEMIOTRC", "DBGEMIOTRC", "DBGEMIOTRC", "DBGEMIOTRC"};
static const char *dci_parents[] __initdata = {"ddrpll"};
static const char *clk621_parents[] __initdata = {"CPU_MASTER_CLK"};

/**
 * zynq_clkdev_add() - Add a clock device
 * @con_id: Connection identifier
 * @dev_id: Device identifier
 * @clk: Struct clock to associate with given connection and device.
 *
 * Create a clkdev entry for a given device/clk
 */
static void __init zynq_clkdev_add(const char *con_id, const char *dev_id,
		struct clk *clk)
{
	if (clk_register_clkdev(clk, con_id, dev_id))
		pr_warn("Adding clkdev failed.");
}

static const struct of_device_id clk_match[] __initconst = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
	{ .compatible = "xlnx,zynq-pll", .data = clk_register_zynq_pll, },
	{}
};

/**
 * zynq_clock_init() - Clock initalization
 *
 * Register clocks and clock devices with the common clock framework.
 * To avoid enabling unused clocks, only leaf clocks are present for which the
 * drivers supports the common clock framework.
 */
void __init zynq_clock_init(void __iomem *slcr_base)
{
	struct clk *clk;

	pr_info("Zynq clock init\n");

	zynq_slcr_base = slcr_base;
	of_clk_init(clk_match);

	/* CPU clocks */
	clk = clk_register_zynq_d1m("CPU_MASTER_CLK", SLCR_ARM_CLK_CTRL,
			cpu_parents, 4, &armclk_lock);
	clk = clk_register_gate(NULL, "CPU_6OR4X_CLK", "CPU_MASTER_CLK",
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			SLCR_ARM_CLK_CTRL, 24, 0, &armclk_lock);
	zynq_clkdev_add(NULL, "CPU_6OR4X_CLK", clk);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_fixed_factor(NULL, "CPU_3OR2X_DIV_CLK",
			"CPU_MASTER_CLK", 0, 1, 2);
	clk = clk_register_gate(NULL, "CPU_3OR2X_CLK", "CPU_3OR2X_DIV_CLK",
			CLK_IGNORE_UNUSED, SLCR_ARM_CLK_CTRL, 25, 0,
			&armclk_lock);
	zynq_clkdev_add(NULL, "smp_twd", clk);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_zynq_clk621("CPU_1X_DIV_CLK", SLCR_ARM_CLK_CTRL,
		SLCR_621_TRUE, 4, 2, clk621_parents, 1, &armclk_lock);
	clk = clk_register_zynq_clk621("CPU_2X_DIV_CLK", SLCR_ARM_CLK_CTRL,
		SLCR_621_TRUE, 2, 1, clk621_parents, 1, &armclk_lock);
	clk = clk_register_gate(NULL, "CPU_2X_CLK", "CPU_2X_DIV_CLK",
			CLK_IGNORE_UNUSED, SLCR_ARM_CLK_CTRL, 26, 0,
			&armclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_gate(NULL, "CPU_1X_CLK", "CPU_1X_DIV_CLK",
			CLK_IGNORE_UNUSED, SLCR_ARM_CLK_CTRL, 27, 0,
			&armclk_lock);
	zynq_clkdev_add(NULL, "CPU_1X_CLK", clk);
	clk_register_clkdev(clk, "apb_pclk", NULL);
	clk_prepare(clk);
	clk_enable(clk);
	/* DDR clocks */
	clk = clk_register_divider(NULL, "DDR_2X_DIV_CLK", "ddrpll", 0,
			SLCR_DDR_CLK_CTRL, 26, 6, CLK_DIVIDER_ONE_BASED,
			&ddrclk_lock);
	clk = clk_register_gate(NULL, "DDR_2X_CLK", "DDR_2X_DIV_CLK", 0,
			SLCR_DDR_CLK_CTRL, 1, 0, &ddrclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_divider(NULL, "DDR_3X_DIV_CLK", "ddrpll", 0,
			SLCR_DDR_CLK_CTRL, 20, 6, CLK_DIVIDER_ONE_BASED,
			&ddrclk_lock);
	clk = clk_register_gate(NULL, "DDR_3X_CLK", "DDR_3X_DIV_CLK", 0,
			SLCR_DDR_CLK_CTRL, 0, 0, &ddrclk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	clk = clk_register_zynq_gd2m("DCI_CLK", SLCR_DCI_CLK_CTRL, dci_parents,
			1, &dciclk_lock);
	clk_prepare(clk);
	clk_enable(clk);

	/* Peripheral clocks */
	clk = clk_register_zynq_gd1m("LQSPI_CLK", SLCR_LQSPI_CLK_CTRL,
			def_periph_parents, &lqspiclk_lock);
	zynq_clkdev_add(NULL, "LQSPI", clk);

	clk = clk_register_zynq_gd1m("SMC_CLK", SLCR_SMC_CLK_CTRL,
			def_periph_parents, &smcclk_lock);
	zynq_clkdev_add(NULL, "SMC", clk);

	clk = clk_register_zynq_gd1m("PCAP_CLK", SLCR_PCAP_CLK_CTRL,
			def_periph_parents, &pcapclk_lock);
	zynq_clkdev_add(NULL, "PCAP", clk);

	clk = clk_register_zynq_gd2m("GEM0_CLK", SLCR_GEM0_CLK_CTRL,
			gem_parents, 8, &gem0clk_lock);
	zynq_clkdev_add(NULL, "GEM0", clk);
	clk = clk_register_zynq_gd2m("GEM1_CLK", SLCR_GEM1_CLK_CTRL,
			gem_parents, 8, &gem1clk_lock);
	zynq_clkdev_add(NULL, "GEM1", clk);

	clk = clk_register_zynq_d2m("FPGA0_CLK", SLCR_FPGA0_CLK_CTRL,
			def_periph_parents, &fpga0clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA0", clk);
	clk = clk_register_zynq_d2m("FPGA1_CLK", SLCR_FPGA1_CLK_CTRL,
			def_periph_parents, &fpga1clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA1", clk);
	clk = clk_register_zynq_d2m("FPGA2_CLK", SLCR_FPGA2_CLK_CTRL,
			def_periph_parents, &fpga2clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA2", clk);
	clk = clk_register_zynq_d2m("FPGA3_CLK", SLCR_FPGA3_CLK_CTRL,
			def_periph_parents, &fpga3clk_lock);
	clk_prepare(clk);
	clk_enable(clk);
	zynq_clkdev_add(NULL, "FPGA3", clk);
	clk = clk_register_zynq_d2m("CAN_MASTER_CLK", SLCR_CAN_CLK_CTRL,
			def_periph_parents, &canclk_lock);

	clk = clk_register_zynq_d1m("SDIO_MASTER_CLK", SLCR_SDIO_CLK_CTRL,
			def_periph_parents, 4, &sdioclk_lock);
	clk = clk_register_zynq_d1m("UART_MASTER_CLK", SLCR_UART_CLK_CTRL,
			def_periph_parents, 4, &uartclk_lock);
	clk = clk_register_zynq_d1m("SPI_MASTER_CLK", SLCR_SPI_CLK_CTRL,
			def_periph_parents, 4, &spiclk_lock);
	clk = clk_register_zynq_d1m("DBG_MASTER_CLK", SLCR_DBG_CLK_CTRL,
			dbg_parents, 8, &dbgclk_lock);

	/*
	 * clk = clk_register_gate(NULL, "CAN0_CLK", "CAN_MASTER_CLK",
	 *	CLK_SET_RATE_PARENT, SLCR_CAN_CLK_CTRL,
	 *	0, 0, &canclk_lock);
	 * zynq_clkdev_add(NULL, "CAN0", clk);
	 * clk = clk_register_gate(NULL, "CAN1_CLK", "CAN_MASTER_CLK",
	 *	CLK_SET_RATE_PARENT, SLCR_CAN_CLK_CTRL,
	 *	1, 0, &canclk_lock);
	 * zynq_clkdev_add(NULL, "CAN1", clk);
	 */

	clk = clk_register_gate(NULL, "SDIO0_CLK", "SDIO_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_SDIO_CLK_CTRL, 0, 0,
			&sdioclk_lock);
	zynq_clkdev_add(NULL, "SDIO0", clk);
	clk = clk_register_gate(NULL, "SDIO1_CLK", "SDIO_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_SDIO_CLK_CTRL, 1, 0,
			&sdioclk_lock);
	zynq_clkdev_add(NULL, "SDIO1", clk);

	clk = clk_register_gate(NULL, "UART0_CLK", "UART_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_UART_CLK_CTRL, 0, 0,
			&uartclk_lock);
	zynq_clkdev_add(NULL, "UART0", clk);
	clk = clk_register_gate(NULL, "UART1_CLK", "UART_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_UART_CLK_CTRL, 1, 0,
			&uartclk_lock);
	zynq_clkdev_add(NULL, "UART1", clk);

	clk = clk_register_gate(NULL, "SPI0_CLK", "SPI_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_SPI_CLK_CTRL, 0, 0,
			&spiclk_lock);
	zynq_clkdev_add(NULL, "SPI0", clk);
	clk = clk_register_gate(NULL, "SPI1_CLK", "SPI_MASTER_CLK",
			CLK_SET_RATE_PARENT, SLCR_SPI_CLK_CTRL, 1, 0,
			&spiclk_lock);
	zynq_clkdev_add(NULL, "SPI1", clk);
	/*
	 * clk = clk_register_gate(NULL, "DBGTRC_CLK", "DBG_MASTER_CLK",
	 *		CLK_SET_RATE_PARENT, SLCR_DBG_CLK_CTRL,	0, 0,
	 *		&dbgclk_lock);
	 * zynq_clkdev_add(NULL, "DBGTRC", clk);
	 * clk = clk_register_gate(NULL, "DBG1X_CLK", "DBG_MASTER_CLK",
	 *		CLK_SET_RATE_PARENT, SLCR_DBG_CLK_CTRL, 1, 0,
	 *		&dbgclk_lock);
	 * zynq_clkdev_add(NULL, "DBG1X", clk);
	 */

	/* One gated clock for all APER clocks. */
	/*
	 * clk = clk_register_gate(NULL, "DMA_CPU2X", "CPU_2X_CLK", 0,
	 *		SLCR_APER_CLK_CTRL, 0, 0, &aperclk_lock);
	 * zynq_clkdev_add(NULL, "DMA_APER", clk);
	 */
	clk = clk_register_gate(NULL, "USB0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 2, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "USB0_APER", clk);
	clk = clk_register_gate(NULL, "USB1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 3, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "USB1_APER", clk);
	clk = clk_register_gate(NULL, "GEM0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 6, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "GEM0_APER", clk);
	clk = clk_register_gate(NULL, "GEM1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 7, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "GEM1_APER", clk);
	clk = clk_register_gate(NULL, "SDI0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 10, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "SDIO0_APER", clk);
	clk = clk_register_gate(NULL, "SDI1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 11, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "SDIO1_APER", clk);
	clk = clk_register_gate(NULL, "SPI0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 14, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "SPI0_APER", clk);
	clk = clk_register_gate(NULL, "SPI1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 15, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "SPI1_APER", clk);
	/*
	 * clk = clk_register_gate(NULL, "CAN0_CPU1X", "CPU_1X_CLK", 0,
	 *		SLCR_APER_CLK_CTRL, 16, 0, &aperclk_lock);
	 * zynq_clkdev_add(NULL, "CAN0_APER", clk);
	 * clk = clk_register_gate(NULL, "CAN1_CPU1X", "CPU_1X_CLK", 0,
	 *		SLCR_APER_CLK_CTRL, 17, 0, &aperclk_lock);
	 * zynq_clkdev_add(NULL, "CAN1_APER", clk);
	 */
	clk = clk_register_gate(NULL, "I2C0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 18, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "I2C0_APER", clk);
	clk = clk_register_gate(NULL, "I2C1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 19, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "I2C1_APER", clk);
	clk = clk_register_gate(NULL, "UART0_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 20, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "UART0_APER", clk);
	clk = clk_register_gate(NULL, "UART1_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 21, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "UART1_APER", clk);
	clk = clk_register_gate(NULL, "GPIO_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 22, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "GPIO_APER", clk);
	clk = clk_register_gate(NULL, "LQSPI_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 23, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "LQSPI_APER", clk);
	clk = clk_register_gate(NULL, "SMC_CPU1X", "CPU_1X_CLK", 0,
			SLCR_APER_CLK_CTRL, 24, 0, &aperclk_lock);
	zynq_clkdev_add(NULL, "SMC_APER", clk);
}
