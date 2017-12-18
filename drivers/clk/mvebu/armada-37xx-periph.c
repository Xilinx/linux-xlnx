/*
 * Marvell Armada 37xx SoC Peripheral clocks
 *
 * Copyright (C) 2016 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * Most of the peripheral clocks can be modelled like this:
 *             _____    _______    _______
 * TBG-A-P  --|     |  |       |  |       |   ______
 * TBG-B-P  --| Mux |--| /div1 |--| /div2 |--| Gate |--> perip_clk
 * TBG-A-S  --|     |  |       |  |       |  |______|
 * TBG-B-S  --|_____|  |_______|  |_______|
 *
 * However some clocks may use only one or two block or and use the
 * xtal clock as parent.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define TBG_SEL		0x0
#define DIV_SEL0	0x4
#define DIV_SEL1	0x8
#define DIV_SEL2	0xC
#define CLK_SEL		0x10
#define CLK_DIS		0x14

struct clk_periph_driver_data {
	struct clk_hw_onecell_data *hw_data;
	spinlock_t lock;
};

struct clk_double_div {
	struct clk_hw hw;
	void __iomem *reg1;
	u8 shift1;
	void __iomem *reg2;
	u8 shift2;
};

#define to_clk_double_div(_hw) container_of(_hw, struct clk_double_div, hw)

struct clk_periph_data {
	const char *name;
	const char * const *parent_names;
	int num_parents;
	struct clk_hw *mux_hw;
	struct clk_hw *rate_hw;
	struct clk_hw *gate_hw;
	bool is_double_div;
};

static const struct clk_div_table clk_table6[] = {
	{ .val = 1, .div = 1, },
	{ .val = 2, .div = 2, },
	{ .val = 3, .div = 3, },
	{ .val = 4, .div = 4, },
	{ .val = 5, .div = 5, },
	{ .val = 6, .div = 6, },
	{ .val = 0, .div = 0, }, /* last entry */
};

static const struct clk_div_table clk_table1[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 0, .div = 0, }, /* last entry */
};

static const struct clk_div_table clk_table2[] = {
	{ .val = 0, .div = 2, },
	{ .val = 1, .div = 4, },
	{ .val = 0, .div = 0, }, /* last entry */
};
static const struct clk_ops clk_double_div_ops;

#define PERIPH_GATE(_name, _bit)		\
struct clk_gate gate_##_name = {		\
	.reg = (void *)CLK_DIS,			\
	.bit_idx = _bit,			\
	.hw.init = &(struct clk_init_data){	\
		.ops =  &clk_gate_ops,		\
	}					\
};

#define PERIPH_MUX(_name, _shift)		\
struct clk_mux mux_##_name = {			\
	.reg = (void *)TBG_SEL,			\
	.shift = _shift,			\
	.mask = 3,				\
	.hw.init = &(struct clk_init_data){	\
		.ops =  &clk_mux_ro_ops,	\
	}					\
};

#define PERIPH_DOUBLEDIV(_name, _reg1, _reg2, _shift1, _shift2)	\
struct clk_double_div rate_##_name = {		\
	.reg1 = (void *)_reg1,			\
	.reg2 = (void *)_reg2,			\
	.shift1 = _shift1,			\
	.shift2 = _shift2,			\
	.hw.init = &(struct clk_init_data){	\
		.ops =  &clk_double_div_ops,	\
	}					\
};

#define PERIPH_DIV(_name, _reg, _shift, _table)	\
struct clk_divider rate_##_name = {		\
	.reg = (void *)_reg,			\
	.table = _table,			\
	.shift = _shift,			\
	.hw.init = &(struct clk_init_data){	\
		.ops =  &clk_divider_ro_ops,	\
	}					\
};

#define PERIPH_CLK_FULL_DD(_name, _bit, _shift, _reg1, _reg2, _shift1, _shift2)\
static PERIPH_GATE(_name, _bit);			    \
static PERIPH_MUX(_name, _shift);			    \
static PERIPH_DOUBLEDIV(_name, _reg1, _reg2, _shift1, _shift2);

#define PERIPH_CLK_FULL(_name, _bit, _shift, _reg, _shift1, _table)	\
static PERIPH_GATE(_name, _bit);			    \
static PERIPH_MUX(_name, _shift);			    \
static PERIPH_DIV(_name, _reg, _shift1, _table);

#define PERIPH_CLK_GATE_DIV(_name, _bit,  _reg, _shift, _table)	\
static PERIPH_GATE(_name, _bit);			\
static PERIPH_DIV(_name, _reg, _shift, _table);

#define PERIPH_CLK_MUX_DIV(_name, _shift,  _reg, _shift_div, _table)	\
static PERIPH_MUX(_name, _shift);			    \
static PERIPH_DIV(_name, _reg, _shift_div, _table);

#define PERIPH_CLK_MUX_DD(_name, _shift, _reg1, _reg2, _shift1, _shift2)\
static PERIPH_MUX(_name, _shift);			    \
static PERIPH_DOUBLEDIV(_name, _reg1, _reg2, _shift1, _shift2);

#define REF_CLK_FULL(_name)				\
	{ .name = #_name,				\
	  .parent_names = (const char *[]){ "TBG-A-P",	\
	      "TBG-B-P", "TBG-A-S", "TBG-B-S"},		\
	  .num_parents = 4,				\
	  .mux_hw = &mux_##_name.hw,			\
	  .gate_hw = &gate_##_name.hw,			\
	  .rate_hw = &rate_##_name.hw,			\
	}

#define REF_CLK_FULL_DD(_name)				\
	{ .name = #_name,				\
	  .parent_names = (const char *[]){ "TBG-A-P",	\
	      "TBG-B-P", "TBG-A-S", "TBG-B-S"},		\
	  .num_parents = 4,				\
	  .mux_hw = &mux_##_name.hw,			\
	  .gate_hw = &gate_##_name.hw,			\
	  .rate_hw = &rate_##_name.hw,			\
	  .is_double_div = true,			\
	}

#define REF_CLK_GATE(_name, _parent_name)			\
	{ .name = #_name,					\
	  .parent_names = (const char *[]){ _parent_name},	\
	  .num_parents = 1,					\
	  .gate_hw = &gate_##_name.hw,				\
	}

#define REF_CLK_GATE_DIV(_name, _parent_name)			\
	{ .name = #_name,					\
	  .parent_names = (const char *[]){ _parent_name},	\
	  .num_parents = 1,					\
	  .gate_hw = &gate_##_name.hw,				\
	  .rate_hw = &rate_##_name.hw,				\
	}

#define REF_CLK_MUX_DIV(_name)				\
	{ .name = #_name,				\
	  .parent_names = (const char *[]){ "TBG-A-P",	\
	      "TBG-B-P", "TBG-A-S", "TBG-B-S"},		\
	  .num_parents = 4,				\
	  .mux_hw = &mux_##_name.hw,			\
	  .rate_hw = &rate_##_name.hw,			\
	}

#define REF_CLK_MUX_DD(_name)				\
	{ .name = #_name,				\
	  .parent_names = (const char *[]){ "TBG-A-P",	\
	      "TBG-B-P", "TBG-A-S", "TBG-B-S"},		\
	  .num_parents = 4,				\
	  .mux_hw = &mux_##_name.hw,			\
	  .rate_hw = &rate_##_name.hw,			\
	  .is_double_div = true,			\
	}

/* NB periph clocks */
PERIPH_CLK_FULL_DD(mmc, 2, 0, DIV_SEL2, DIV_SEL2, 16, 13);
PERIPH_CLK_FULL_DD(sata_host, 3, 2, DIV_SEL2, DIV_SEL2, 10, 7);
PERIPH_CLK_FULL_DD(sec_at, 6, 4, DIV_SEL1, DIV_SEL1, 3, 0);
PERIPH_CLK_FULL_DD(sec_dap, 7, 6, DIV_SEL1, DIV_SEL1, 9, 6);
PERIPH_CLK_FULL_DD(tscem, 8, 8, DIV_SEL1, DIV_SEL1, 15, 12);
PERIPH_CLK_FULL(tscem_tmx, 10, 10, DIV_SEL1, 18, clk_table6);
static PERIPH_GATE(avs, 11);
PERIPH_CLK_FULL_DD(pwm, 13, 14, DIV_SEL0, DIV_SEL0, 3, 0);
PERIPH_CLK_FULL_DD(sqf, 12, 12, DIV_SEL1, DIV_SEL1, 27, 24);
static PERIPH_GATE(i2c_2, 16);
static PERIPH_GATE(i2c_1, 17);
PERIPH_CLK_GATE_DIV(ddr_phy, 19, DIV_SEL0, 18, clk_table2);
PERIPH_CLK_FULL_DD(ddr_fclk, 21, 16, DIV_SEL0, DIV_SEL0, 15, 12);
PERIPH_CLK_FULL(trace, 22, 18, DIV_SEL0, 20, clk_table6);
PERIPH_CLK_FULL(counter, 23, 20, DIV_SEL0, 23, clk_table6);
PERIPH_CLK_FULL_DD(eip97, 24, 24, DIV_SEL2, DIV_SEL2, 22, 19);
PERIPH_CLK_MUX_DIV(cpu, 22, DIV_SEL0, 28, clk_table6);

static struct clk_periph_data data_nb[] ={
	REF_CLK_FULL_DD(mmc),
	REF_CLK_FULL_DD(sata_host),
	REF_CLK_FULL_DD(sec_at),
	REF_CLK_FULL_DD(sec_dap),
	REF_CLK_FULL_DD(tscem),
	REF_CLK_FULL(tscem_tmx),
	REF_CLK_GATE(avs, "xtal"),
	REF_CLK_FULL_DD(sqf),
	REF_CLK_FULL_DD(pwm),
	REF_CLK_GATE(i2c_2, "xtal"),
	REF_CLK_GATE(i2c_1, "xtal"),
	REF_CLK_GATE_DIV(ddr_phy, "TBG-A-S"),
	REF_CLK_FULL_DD(ddr_fclk),
	REF_CLK_FULL(trace),
	REF_CLK_FULL(counter),
	REF_CLK_FULL_DD(eip97),
	REF_CLK_MUX_DIV(cpu),
	{ },
};

/* SB periph clocks */
PERIPH_CLK_MUX_DD(gbe_50, 6, DIV_SEL2, DIV_SEL2, 6, 9);
PERIPH_CLK_MUX_DD(gbe_core, 8, DIV_SEL1, DIV_SEL1, 18, 21);
PERIPH_CLK_MUX_DD(gbe_125, 10, DIV_SEL1, DIV_SEL1, 6, 9);
static PERIPH_GATE(gbe1_50, 0);
static PERIPH_GATE(gbe0_50, 1);
static PERIPH_GATE(gbe1_125, 2);
static PERIPH_GATE(gbe0_125, 3);
PERIPH_CLK_GATE_DIV(gbe1_core, 4, DIV_SEL1, 13, clk_table1);
PERIPH_CLK_GATE_DIV(gbe0_core, 5, DIV_SEL1, 14, clk_table1);
PERIPH_CLK_GATE_DIV(gbe_bm, 12, DIV_SEL1, 0, clk_table1);
PERIPH_CLK_FULL_DD(sdio, 11, 14, DIV_SEL0, DIV_SEL0, 3, 6);
PERIPH_CLK_FULL_DD(usb32_usb2_sys, 16, 16, DIV_SEL0, DIV_SEL0, 9, 12);
PERIPH_CLK_FULL_DD(usb32_ss_sys, 17, 18, DIV_SEL0, DIV_SEL0, 15, 18);

static struct clk_periph_data data_sb[] = {
	REF_CLK_MUX_DD(gbe_50),
	REF_CLK_MUX_DD(gbe_core),
	REF_CLK_MUX_DD(gbe_125),
	REF_CLK_GATE(gbe1_50, "gbe_50"),
	REF_CLK_GATE(gbe0_50, "gbe_50"),
	REF_CLK_GATE(gbe1_125, "gbe_125"),
	REF_CLK_GATE(gbe0_125, "gbe_125"),
	REF_CLK_GATE_DIV(gbe1_core, "gbe_core"),
	REF_CLK_GATE_DIV(gbe0_core, "gbe_core"),
	REF_CLK_GATE_DIV(gbe_bm, "gbe_core"),
	REF_CLK_FULL_DD(sdio),
	REF_CLK_FULL_DD(usb32_usb2_sys),
	REF_CLK_FULL_DD(usb32_ss_sys),
	{ },
};

static unsigned int get_div(void __iomem *reg, int shift)
{
	u32 val;

	val = (readl(reg) >> shift) & 0x7;
	if (val > 6)
		return 0;
	return val;
}

static unsigned long clk_double_div_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_double_div *double_div = to_clk_double_div(hw);
	unsigned int div;

	div = get_div(double_div->reg1, double_div->shift1);
	div *= get_div(double_div->reg2, double_div->shift2);

	return DIV_ROUND_UP_ULL((u64)parent_rate, div);
}

static const struct clk_ops clk_double_div_ops = {
	.recalc_rate = clk_double_div_recalc_rate,
};

static const struct of_device_id armada_3700_periph_clock_of_match[] = {
	{ .compatible = "marvell,armada-3700-periph-clock-nb",
	  .data = data_nb, },
	{ .compatible = "marvell,armada-3700-periph-clock-sb",
	.data = data_sb, },
	{ }
};
static int armada_3700_add_composite_clk(const struct clk_periph_data *data,
					 void __iomem *reg, spinlock_t *lock,
					 struct device *dev, struct clk_hw **hw)
{
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL,
		*rate_ops = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *rate_hw = NULL;

	if (data->mux_hw) {
		struct clk_mux *mux;

		mux_hw = data->mux_hw;
		mux = to_clk_mux(mux_hw);
		mux->lock = lock;
		mux_ops = mux_hw->init->ops;
		mux->reg = reg + (u64)mux->reg;
	}

	if (data->gate_hw) {
		struct clk_gate *gate;

		gate_hw = data->gate_hw;
		gate = to_clk_gate(gate_hw);
		gate->lock = lock;
		gate_ops = gate_hw->init->ops;
		gate->reg = reg + (u64)gate->reg;
		gate->flags = CLK_GATE_SET_TO_DISABLE;
	}

	if (data->rate_hw) {
		rate_hw = data->rate_hw;
		rate_ops = rate_hw->init->ops;
		if (data->is_double_div) {
			struct clk_double_div *rate;

			rate =  to_clk_double_div(rate_hw);
			rate->reg1 = reg + (u64)rate->reg1;
			rate->reg2 = reg + (u64)rate->reg2;
		} else {
			struct clk_divider *rate = to_clk_divider(rate_hw);
			const struct clk_div_table *clkt;
			int table_size = 0;

			rate->reg = reg + (u64)rate->reg;
			for (clkt = rate->table; clkt->div; clkt++)
				table_size++;
			rate->width = order_base_2(table_size);
			rate->lock = lock;
		}
	}

	*hw = clk_hw_register_composite(dev, data->name, data->parent_names,
				       data->num_parents, mux_hw,
				       mux_ops, rate_hw, rate_ops,
				       gate_hw, gate_ops, CLK_IGNORE_UNUSED);

	if (IS_ERR(*hw))
		return PTR_ERR(*hw);

	return 0;
}

static int armada_3700_periph_clock_probe(struct platform_device *pdev)
{
	struct clk_periph_driver_data *driver_data;
	struct device_node *np = pdev->dev.of_node;
	const struct clk_periph_data *data;
	struct device *dev = &pdev->dev;
	int num_periph = 0, i, ret;
	struct resource *res;
	void __iomem *reg;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	while (data[num_periph].name)
		num_periph++;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	driver_data = devm_kzalloc(dev, sizeof(*driver_data), GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;

	driver_data->hw_data = devm_kzalloc(dev, sizeof(*driver_data->hw_data) +
			    sizeof(*driver_data->hw_data->hws) * num_periph,
			    GFP_KERNEL);
	if (!driver_data->hw_data)
		return -ENOMEM;
	driver_data->hw_data->num = num_periph;

	spin_lock_init(&driver_data->lock);

	for (i = 0; i < num_periph; i++) {
		struct clk_hw **hw = &driver_data->hw_data->hws[i];

		if (armada_3700_add_composite_clk(&data[i], reg,
						  &driver_data->lock, dev, hw))
			dev_err(dev, "Can't register periph clock %s\n",
			       data[i].name);

	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get,
				  driver_data->hw_data);
	if (ret) {
		for (i = 0; i < num_periph; i++)
			clk_hw_unregister(driver_data->hw_data->hws[i]);
		return ret;
	}

	platform_set_drvdata(pdev, driver_data);
	return 0;
}

static int armada_3700_periph_clock_remove(struct platform_device *pdev)
{
	struct clk_periph_driver_data *data = platform_get_drvdata(pdev);
	struct clk_hw_onecell_data *hw_data = data->hw_data;
	int i;

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0; i < hw_data->num; i++)
		clk_hw_unregister(hw_data->hws[i]);

	return 0;
}

static struct platform_driver armada_3700_periph_clock_driver = {
	.probe = armada_3700_periph_clock_probe,
	.remove = armada_3700_periph_clock_remove,
	.driver		= {
		.name	= "marvell-armada-3700-periph-clock",
		.of_match_table = armada_3700_periph_clock_of_match,
	},
};

builtin_platform_driver(armada_3700_periph_clock_driver);
