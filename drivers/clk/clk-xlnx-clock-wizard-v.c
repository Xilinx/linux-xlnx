// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx 'Clocking Wizard' driver
 *
 *  Copyright (C) 2020 Xilinx
 *
 *  Shubhrajyoti Datta <shubhrajyoti.datta@xilinx.com>
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

#define WZRD_CLK_CFG_REG(n)	(0x330 + 4 * (n))

#define WZRD_CLKFBOUT_FRAC_EN	BIT(1)

#define WZRD_CLKFBOUT_L_SHIFT	0
#define WZRD_CLKFBOUT_H_SHIFT	8
#define WZRD_CLKFBOUT_L_MASK	(0xff << WZRD_CLKFBOUT_L_SHIFT)
#define WZRD_CLKFBOUT_H_MASK	(0xff << WZRD_CLKFBOUT_H_SHIFT)
#define WZRD_CLKFBOUT_FRAC_SHIFT	16
#define WZRD_CLKFBOUT_FRAC_MASK		(0x3f)
#define WZRD_DIVCLK_DIVIDE_SHIFT	0
#define WZRD_DIVCLK_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_DIVIDE_SHIFT	0
#define WZRD_CLKOUT_DIVIDE_WIDTH	8
#define WZRD_CLKOUT_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_FRAC_SHIFT		8
#define WZRD_CLKOUT_FRAC_MASK		0x3ff

#define WZRD_DR_MAX_INT_DIV_VALUE	32767
#define WZRD_DR_NUM_RETRIES		10000
#define WZRD_DR_STATUS_REG_OFFSET	0x04
#define WZRD_DR_LOCK_BIT_MASK		0x00000001
#define WZRD_DR_INIT_REG_OFFSET		0x14
#define WZRD_DR_DIV_TO_PHASE_OFFSET	4
#define WZRD_DR_BEGIN_DYNA_RECONF	0x03

/* Get the mask from width */
#define div_mask(width)			((1 << (width)) - 1)

/* Extract divider instance from clock hardware instance */
#define to_clk_wzrd_divider(_hw) container_of(_hw, struct clk_wzrd_divider, hw)

enum clk_wzrd_int_clks {
	wzrd_clk_mul,
	wzrd_clk_mul_div,
	wzrd_clk_mul_frac,
	wzrd_clk_int_max
};

/**
 * struct clk_wzrd - Clock wizard private data structure
 *
 * @clk_data:		Clock data
 * @nb:			Notifier block
 * @base:		Memory base
 * @clk_in1:		Handle to input clock 'clk_in1'
 * @axi_clk:		Handle to input clock 's_axi_aclk'
 * @clks_internal:	Internal clocks
 * @clkout:		Output clocks
 * @suspended:		Flag indicating power state of the device
 * @is_versal:		Flag indicating if it versal device
 */
struct clk_wzrd {
	struct clk_onecell_data clk_data;
	struct notifier_block nb;
	void __iomem *base;
	struct clk *clk_in1;
	struct clk *axi_clk;
	struct clk *clks_internal[wzrd_clk_int_max];
	struct clk *clkout[WZRD_NUM_OUTPUTS];
	bool suspended;
	bool is_versal;
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

/* spin lock variable for clk_wzrd */
static DEFINE_SPINLOCK(clkwzrd_lock);

static unsigned long clk_wzrd_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);
	unsigned int vall, valh;
	u32 div;
	u32 p5en, edge, prediv2;
	u32 all;

	edge = !!(readl(div_addr) & BIT(8));
	p5en = !!(readl(div_addr) & BIT(13));
	prediv2 = !!(readl(div_addr) & BIT(11));
	vall = readl(div_addr + 4) & 0xff;
	valh = readl(div_addr + 4) >> 8;
	all = valh + vall + edge;
	if (!all)
		all = 1;
	div = (prediv2 + 1) * (all + (prediv2 * p5en) / 2);
	return  DIV_ROUND_UP_ULL((u64)parent_rate, div);
}

static int clk_wzrd_dynamic_reconfig(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	int err = 0;
	u16 retries;
	u32 value;
	unsigned long flags = 0;
	u32 regh, edged;
	u32 p5en, p5fedge;
	u32 regval, regval1;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr =
			(void __iomem *)((u64)divider->base + divider->offset);
	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	value = DIV_ROUND_CLOSEST(parent_rate, rate);
	regh = (value / 4);
	regh = regh * 2;
	regval = regh | (regh << 8);
	regval1 = readl(div_addr);
	regval1 = regval1 &  ~(BIT(8) | BIT(13) | BIT(15));
	if (value % 4 > 1) {
		edged = 1;
		regval1 |= (edged << 8);
	}
	p5fedge = value % 2;
	p5en = value % 2;
	regval1 = regval1 | p5en << 13 | p5fedge << 15;
	writel(regval1, div_addr);
	regval = regh | regh << 8;
	writel(regval, div_addr + 4);
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

	if (retries == 0) {
		pr_err("NOT LOCKED\n");
		err = -ETIMEDOUT;
	}

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
	u32 regl, regh, edge, mult;
	u32 regld, reghd, edged, div;
	unsigned long rate;
	const char *clk_name;
	struct clk_wzrd *clk_wzrd;
	struct resource *mem;
	int outputs;
	struct device_node *np = pdev->dev.of_node;

	clk_wzrd = devm_kzalloc(&pdev->dev, sizeof(*clk_wzrd), GFP_KERNEL);
	if (!clk_wzrd)
		return -ENOMEM;
	platform_set_drvdata(pdev, clk_wzrd);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	clk_wzrd->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(clk_wzrd->base))
		return PTR_ERR(clk_wzrd->base);

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

	/* register multiplier */
	edge = !!(readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0)) & BIT(8));
	regl = (readl(clk_wzrd->base + WZRD_CLK_CFG_REG(1)) &
		     WZRD_CLKFBOUT_L_MASK) >> WZRD_CLKFBOUT_L_SHIFT;
	regh = (readl(clk_wzrd->base + WZRD_CLK_CFG_REG(1)) &
		     WZRD_CLKFBOUT_H_MASK) >> WZRD_CLKFBOUT_H_SHIFT;
	mult = (regl  + regh + edge);
	if (!mult)
		mult = 1;
	mult = mult * 64;

	regl = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(51)) &
		     WZRD_CLKFBOUT_FRAC_EN;
	if (!regl) {
		regl = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(48)) &
			WZRD_CLKFBOUT_FRAC_MASK;
		mult = mult + regl;
	}

	clk_name = kasprintf(GFP_KERNEL, "%s_mul", dev_name(&pdev->dev));
	if (!clk_name) {
		ret = -ENOMEM;
		goto err_disable_clk;
	}
	clk_wzrd->clks_internal[wzrd_clk_mul] = clk_register_fixed_factor
			(&pdev->dev, clk_name,
			 __clk_get_name(clk_wzrd->clk_in1),
			0, mult, 64);
	kfree(clk_name);
	if (IS_ERR(clk_wzrd->clks_internal[wzrd_clk_mul])) {
		dev_err(&pdev->dev, "unable to register fixed-factor clock\n");
		ret = PTR_ERR(clk_wzrd->clks_internal[wzrd_clk_mul]);
		goto err_disable_clk;
	}

	outputs = of_property_count_strings(np, "clock-output-names");
	clk_name = kasprintf(GFP_KERNEL, "%s_mul_div", dev_name(&pdev->dev));
	if (!clk_name) {
		ret = -ENOMEM;
		goto err_rm_int_clk;
	}

	/* register div */
	edged = !!(readl(clk_wzrd->base + WZRD_CLK_CFG_REG(20)) &
		     BIT(10));
	regld = (readl(clk_wzrd->base + WZRD_CLK_CFG_REG(21)) &
		     WZRD_CLKFBOUT_L_MASK) >> WZRD_CLKFBOUT_L_SHIFT;
	reghd = (readl(clk_wzrd->base + WZRD_CLK_CFG_REG(21)) &
		     WZRD_CLKFBOUT_H_MASK) >> WZRD_CLKFBOUT_H_SHIFT;
	div = (regld  + reghd + edged);
	if (!div)
		div = 1;
	clk_wzrd->clks_internal[wzrd_clk_mul_div] = clk_register_fixed_factor
			(&pdev->dev, clk_name,
			 __clk_get_name(clk_wzrd->clks_internal[wzrd_clk_mul]),
			0, 1, div);
	if (IS_ERR(clk_wzrd->clks_internal[wzrd_clk_mul_div])) {
		dev_err(&pdev->dev, "unable to register divider clock\n");
		ret = PTR_ERR(clk_wzrd->clks_internal[wzrd_clk_mul_div]);
		goto err_rm_int_clk;
	}

	/* register div per output */
	for (i = outputs - 1; i >= 0 ; i--) {
		const char *clkout_name;

		if (of_property_read_string_index(np, "clock-output-names", i,
						  &clkout_name)) {
			dev_err(&pdev->dev,
				"clock output name not specified\n");
			ret = -EINVAL;
			goto err_rm_int_clks;
		}

		clk_wzrd->clkout[i] = clk_wzrd_register_divider
				(&pdev->dev, clkout_name,
				clk_name, 0,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(3) + i * 8),
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
			goto err_rm_int_clks;
		}
	}

	kfree(clk_name);

	clk_wzrd->clk_data.clks = clk_wzrd->clkout;
	clk_wzrd->clk_data.clk_num = ARRAY_SIZE(clk_wzrd->clkout);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_wzrd->clk_data);

	return 0;

err_rm_int_clks:
	clk_unregister(clk_wzrd->clks_internal[1]);
err_rm_int_clk:
	kfree(clk_name);
	clk_unregister(clk_wzrd->clks_internal[0]);
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
	for (i = 0; i < wzrd_clk_int_max; i++)
		clk_unregister(clk_wzrd->clks_internal[i]);

	clk_disable_unprepare(clk_wzrd->axi_clk);

	return 0;
}

static const struct of_device_id clk_wzrd_ids[] = {
	{ .compatible = "xlnx,clk-wizard-1.0" },
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
MODULE_AUTHOR("Shubhrajyoti Datta <shubhrajyoti.datta@xilinx.com>");
MODULE_DESCRIPTION("Driver for the Versal Clocking Wizard IP core");
