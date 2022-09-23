// SPDX-License-Identifier: GPL-2.0
/*
 * Common clock framework driver for the ProXO family of quartz-based oscillators.
 *
 * Copyright (c) 2022 Renesas Electronics Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/swab.h>

/* Most ProXO products have a 50MHz xtal, can be overridden in device tree */
#define PROXO_DEFAULT_XTAL	50000000

/* VCO range is 6.86 GHz to 8.65 GHz */
#define PROXO_FVCO_MIN		6860000000ULL
#define PROXO_FVCO_MAX		8650000000ULL

/* Output range is 15MHz to 2.1GHz */
#define PROXO_FOUT_MIN		15000000UL
#define PROXO_FOUT_MAX		2100000000UL

#define PROXO_FRAC_BITS		24
#define PROXO_FRAC_DIVISOR	BIT(PROXO_FRAC_BITS)

/* Disable the doubler if the crystal is > 80MHz */
#define PROXO_FDBL_MAX		80000000U

#define PROXO_OUTDIV_MIN	4
#define PROXO_OUTDIV_MAX	511
#define PROXO_FB_MIN		41

#define PROXO_REG_FREQ0		0x10
#define PROXO_REG_XO		0x51
#define PROXO_REG_TRIG		0x62

#define OUTDIV_8_MASK		0x80
#define FBDIV_INT_8_7_MASK	0x30
#define FBDIV_INT_6_0_MASK	0x7f
#define DOUBLE_DIS_MASK		0x80
#define CP_MASK			0x0e
#define PLL_MODE_MASK		0x01

enum proxo_model {
	PROXO_XP,
};

enum proxo_pll_mode {
	PLL_MODE_FRAC,
	PLL_MODE_INT,
};

struct clk_proxo {
	struct clk_hw hw;
	struct regmap *regmap;
	struct i2c_client *i2c_client;
	enum proxo_model model;
	u32 fxtal;
	u64 fvco;
	u32 fout;
	u8 double_dis;
	u16 fb_int;
	u32 fb_frac;
	u16 out_div;
};

#define to_clk_proxo(_hw)	container_of(_hw, struct clk_proxo, hw)

static u8 proxo_get_cp_value(u64 fvco)
{
	if (fvco < 7000000000ULL)
		return 5;
	else if (fvco >= 7000000000ULL && fvco < 7400000000ULL)
		return 4;
	else if (fvco >= 7400000000ULL && fvco < 7800000000ULL)
		return 3;
	else
		return 2;
}

static u64 proxo_calc_fvco(u32 fxtal, u8 double_dis, u16 fb_int, u32 fb_frac)
{
	u64 fref, fvco;
	u8 doubler;

	doubler = double_dis ? 1 : 2;
	fref = (u64)fxtal * doubler;
	fvco = (fref * fb_int) + div_u64(fref * fb_frac, PROXO_FRAC_DIVISOR);

	return fvco;
}

static int proxo_get_divs(struct clk_proxo *proxo, u16 *out_div, u16 *fb_int, u32 *fb_frac,
			  u8 *double_dis)
{
	int ret;
	u8 reg[6];
	unsigned int xo;

	ret = regmap_bulk_read(proxo->regmap, PROXO_REG_FREQ0, reg, ARRAY_SIZE(reg));
	if (ret)
		return ret;

	ret = regmap_read(proxo->regmap, PROXO_REG_XO, &xo);
	if (ret)
		return ret;

	*out_div = (u16_get_bits(reg[1], OUTDIV_8_MASK) << 8) + reg[0];
	*fb_int = (u16_get_bits(reg[2], FBDIV_INT_8_7_MASK) << 7) + (reg[1] & FBDIV_INT_6_0_MASK);
	*fb_frac = ((u32)reg[5] << 16) + ((u32)reg[4] << 8) + reg[3];
	*double_dis = !!(xo & DOUBLE_DIS_MASK);

	if (*fb_frac > (PROXO_FRAC_DIVISOR >> 1))
		(*fb_int)--;

	pr_debug("%s - out_div: %u, fb_int: %u, fb_frac: %u, doubler_dis: %u\n",
		 __func__, *out_div, *fb_int, *fb_frac, *double_dis);

	return ret;
}

static int proxo_get_defaults(struct clk_proxo *proxo)
{
	int ret;

	ret = proxo_get_divs(proxo, &proxo->out_div, &proxo->fb_int, &proxo->fb_frac,
			     &proxo->double_dis);
	if (ret)
		return ret;

	proxo->fvco = proxo_calc_fvco(proxo->fxtal, proxo->double_dis, proxo->fb_int,
				      proxo->fb_frac);
	proxo->fout = div_u64(proxo->fvco, proxo->out_div);

	pr_debug("%s - out_div: %u, fb_int: %u, fb_frac: %u, doubler_dis: %u, fvco: %llu, fout: %u\n",
		 __func__, proxo->out_div, proxo->fb_int, proxo->fb_frac, proxo->double_dis,
		 proxo->fvco, proxo->fout);

	return ret;
}

static int proxo_calc_divs(unsigned long frequency, struct clk_proxo *proxo, u32 *fout,
			   u16 *out_div, u16 *fb_int, u32 *fb_frac, u8 *double_dis)
{
	int i;
	u8 doubler;
	u16 out_div_start;
	u32 fref;
	u64 fvco;
	bool found = false, allow_frac = false;

	out_div_start = 1 + div64_u64(PROXO_FVCO_MIN, frequency);
	doubler = proxo->fxtal <= PROXO_FDBL_MAX ? 2 : 1;
	fref = proxo->fxtal * doubler;
	*fout = (u32)max(PROXO_FOUT_MIN, min(PROXO_FOUT_MAX, (unsigned long)*fout));
	*out_div = PROXO_OUTDIV_MIN;
	*fb_int = PROXO_FB_MIN;
	*fb_frac = 0;
	*double_dis = doubler == 1 ? 1 : 0;

retry:
	for (i = out_div_start; i <= PROXO_OUTDIV_MAX; ++i) {
		*out_div = i;
		fvco = frequency * *out_div;
		if (fvco > PROXO_FVCO_MAX) {
			allow_frac = true;
			goto retry;
		}
		*fb_int = div_u64_rem(fvco, fref, fb_frac);
		if (*fb_frac == 0) {
			found = true;
			break;
		}
		if (allow_frac) {
			*fb_frac = 1 + (u32)div_u64((u64)*fb_frac << PROXO_FRAC_BITS, fref);
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	if (fvco < PROXO_FVCO_MIN || fvco > PROXO_FVCO_MAX)
		return -EINVAL;

	fvco = ((u64)fref * *fb_int) + div_u64((u64)fref * *fb_frac, PROXO_FRAC_DIVISOR);
	*fout = div_u64(fvco, *out_div);

	return 0;
}

static int proxo_update_frequency(struct clk_proxo *proxo)
{
	enum proxo_pll_mode pll_mode;
	u8 cp_value;
	u16 fb_int;
	u8 reg[6];

	cp_value = proxo_get_cp_value(proxo->fvco);
	pll_mode = proxo->fb_frac == 0 ? PLL_MODE_INT : PLL_MODE_FRAC;
	fb_int = proxo->fb_frac > (PROXO_FRAC_DIVISOR >> 1) ? proxo->fb_int + 1 : proxo->fb_int;

	reg[0] = proxo->out_div & 0xff;
	reg[1] = ((proxo->out_div >> 1) & OUTDIV_8_MASK) + (fb_int & FBDIV_INT_6_0_MASK);
	reg[2] = (fb_int >> 3) & FBDIV_INT_8_7_MASK;
	reg[2] = u8_replace_bits(reg[2], cp_value, CP_MASK);
	reg[2] = u8_replace_bits(reg[2], pll_mode, PLL_MODE_MASK);
	reg[3] = proxo->fb_frac & 0xff;
	reg[4] = (proxo->fb_frac >> 8) & 0xff;
	reg[5] = (proxo->fb_frac >> 16) & 0xff;

	return regmap_bulk_write(proxo->regmap, PROXO_REG_FREQ0, reg, ARRAY_SIZE(reg));
}

static int proxo_set_frequency(struct clk_proxo *proxo, unsigned long frequency)
{
	int ret;

	ret = proxo_calc_divs(frequency, proxo, &proxo->fout, &proxo->out_div, &proxo->fb_int,
			      &proxo->fb_frac, &proxo->double_dis);
	if (ret)
		return ret;

	proxo->fvco = proxo_calc_fvco(proxo->fxtal, proxo->double_dis, proxo->fb_int,
				      proxo->fb_frac);
	proxo->fout = div_u64(proxo->fvco, proxo->out_div);

	pr_debug("%s - out_div: %u, fb_int: %u, fb_frac: %u, doubler_dis: %u, fvco: %llu, fout: %u\n",
		 __func__, proxo->out_div, proxo->fb_int, proxo->fb_frac,
	proxo->double_dis, proxo->fvco, proxo->fout);

	proxo_update_frequency(proxo);

	/* trigger frequency change */
	regmap_write(proxo->regmap, PROXO_REG_TRIG, 0x00);
	regmap_write(proxo->regmap, PROXO_REG_TRIG, 0x01);
	regmap_write(proxo->regmap, PROXO_REG_TRIG, 0x00);

	return ret;
}

static unsigned long proxo_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_proxo *proxo = to_clk_proxo(hw);
	int ret;
	u8 double_dis;
	u16 out_div, fb_int;
	u32 fout, fb_frac;
	u64 fvco;

	ret = proxo_get_divs(proxo, &out_div, &fb_int, &fb_frac, &double_dis);
	if (ret) {
		dev_err(&proxo->i2c_client->dev, "unable to recalc rate\n");
		return 0;
	}

	fvco = proxo_calc_fvco(proxo->fxtal, double_dis, fb_int, fb_frac);
	fout = div_u64(fvco, out_div);

	return fout;
}

static long proxo_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *parent_rate)
{
	struct clk_proxo *proxo = to_clk_proxo(hw);
	int ret;
	u8 double_dis;
	u16 out_div, fb_int;
	u32 fout, fb_frac;

	if (!rate)
		return 0;

	ret = proxo_calc_divs(rate, proxo, &fout, &out_div, &fb_int, &fb_frac, &double_dis);
	if (ret) {
		dev_err(&proxo->i2c_client->dev, "unable to round rate\n");
		return 0;
	}

	return fout;
}

static int proxo_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct clk_proxo *proxo = to_clk_proxo(hw);

	if (rate < PROXO_FOUT_MIN || rate > PROXO_FOUT_MAX) {
		dev_err(&proxo->i2c_client->dev, "requested frequency %lu Hz is out of range\n",
			rate);
		return -EINVAL;
	}

	return proxo_set_frequency(proxo, rate);
}

static const struct clk_ops proxo_clk_ops = {
	.recalc_rate = proxo_recalc_rate,
	.round_rate = proxo_round_rate,
	.set_rate = proxo_set_rate,
};

static const struct i2c_device_id proxo_i2c_id[] = {
	{ "proxo-xp", PROXO_XP },
	{},
};
MODULE_DEVICE_TABLE(i2c, proxo_i2c_id);

static const struct regmap_config proxo_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x63,
	.cache_type = REGCACHE_RBTREE,
	.use_single_write = true,
	.use_single_read = true,
};

static int proxo_probe(struct i2c_client *client)
{
	struct clk_proxo *proxo;
	struct clk_init_data init;
	const struct i2c_device_id *id = i2c_match_id(proxo_i2c_id, client);
	int ret;

	proxo = devm_kzalloc(&client->dev, sizeof(*proxo), GFP_KERNEL);
	if (!proxo)
		return -ENOMEM;

	init.ops = &proxo_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	proxo->hw.init = &init;
	proxo->i2c_client = client;
	proxo->model = id->driver_data;

	if (of_property_read_string(client->dev.of_node, "clock-output-names", &init.name))
		init.name = client->dev.of_node->name;

	if (of_property_read_u32(client->dev.of_node, "renesas,crystal-frequency", &proxo->fxtal))
		proxo->fxtal = PROXO_DEFAULT_XTAL;

	proxo->regmap = devm_regmap_init_i2c(client, &proxo_regmap_config);
	if (IS_ERR(proxo->regmap))
		return PTR_ERR(proxo->regmap);

	i2c_set_clientdata(client, proxo);

	ret = proxo_get_defaults(proxo);
	if (ret) {
		dev_err(&client->dev, "getting defaults failed\n");
		return ret;
	}

	ret = devm_clk_hw_register(&client->dev, &proxo->hw);
	if (ret) {
		dev_err(&client->dev, "clock registration failed\n");
		return ret;
	}

	ret = of_clk_add_hw_provider(client->dev.of_node, of_clk_hw_simple_get, &proxo->hw);
	if (ret) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return ret;
	}

	ret = clk_set_rate_range(proxo->hw.clk, PROXO_FOUT_MIN, PROXO_FOUT_MAX);
	if (ret) {
		dev_err(&client->dev, "clk_set_rate_range failed\n");
		return ret;
	}

	dev_info(&client->dev, "registered, current frequency %u Hz\n", proxo->fout);

	return ret;
}

static int proxo_remove(struct i2c_client *client)
{
	of_clk_del_provider(client->dev.of_node);
	return 0;
}

static const struct of_device_id proxo_of_match[] = {
	{ .compatible = "renesas,proxo-xp" },
	{},
};
MODULE_DEVICE_TABLE(of, proxo_of_match);

static struct i2c_driver proxo_i2c_driver = {
	.driver = {
		.name = "proxo",
		.of_match_table = proxo_of_match,
	},
	.probe_new = proxo_probe,
	.remove = proxo_remove,
	.id_table = proxo_i2c_id,
};
module_i2c_driver(proxo_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Helms <alexander.helms.jy@renesas.com");
MODULE_DESCRIPTION("Renesas ProXO common clock framework driver");
