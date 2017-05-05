/*
 * clk-si5324.c: Silicon Laboratories Si5324 Clock Multiplier / Jitter Attenuator
 *
 * Leon Woestenberg <leon@sidebranch.com>
 *
 * References:
 * [1] "Si5324 Data Sheet"
 *     https://www.silabs.com/Support%20Documents/TechnicalDocs/Si5324.pdf
 * [2] http://www.silabs.com/Support%20Documents/TechnicalDocs/Si53xxReferenceManual.pdf
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


/* if both both DEBUG and DEBUG_TRACE are defined, trace_printk() is used */
//#define DEBUG
//#define DEBUG_TRACE

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/rational.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/platform_data/si5324.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/div64.h>

#include "clk-si5324.h"
#include "si5324drv.h"

/* select either trace or printk logging */
#ifdef DEBUG_TRACE
#define do_si5324_dbg(format, ...) do { \
  trace_printk("si5324: " format, ##__VA_ARGS__); \
} while(0)
#else
#define do_si5324_dbg(format, ...) do { \
  printk(KERN_DEBUG "si5324: " format, ##__VA_ARGS__); \
} while(0)
#endif

/* either enable or disable debugging */
#ifdef DEBUG
#  define si5324_dbg(x...) do_si5324_dbg(x)
#else
#  define si5324_dbg(x...)
#endif

/* bypass is a hardware debug function, but can be useful in applications,
 * @TODO consider support through device-tree option later */
#define FORCE_BYPASS 0

struct si5324_driver_data;

struct si5324_parameters {
	// Current Si5342 parameters

	/* high-speed output divider */
	u32 n1_hs_min;
	u32 n1_hs_max;
	u32 n1_hs;

	/* low-speed output divider for clkout1 */
	u32 nc1_ls_min;
	u32 nc1_ls_max;
	u32 nc1_ls;

	/* low-speed output divider for clkout2 */
	u32 nc2_ls_min;
	u32 nc2_ls_max;
	u32 nc2_ls;

	/* high-speed feedback divider (PLL multiplier) */
	u32 n2_hs;
	/* low-speed feedback divider (PLL multiplier) */
	u32 n2_ls_min;
	u32 n2_ls_max;
	u32 n2_ls;

	/* input divider for clk1 */
	u32 n31_min;
	u32 n31_max;
	u32 n31;

	/* input divider for clk1 */
	u32 n32_min;
	u32 n32_max;
	u32 n32;

	// Current frequencies (fixed point 36.28 notation)
	u64 fin;
	u64 fout;
	u64 fosc;
	// Best settings found
	u64 best_delta_fout;
	u64 best_fout;
	u32 best_n1_hs;
	u32 best_nc1_ls;
	u32 best_n2_hs;
	u32 best_n2_ls;
	u32 best_n3;
	int valid;
};

struct si5324_hw_data {
	struct clk_hw			hw;
	struct si5324_driver_data	*drvdata;
	unsigned char			num;
};

struct si5324_driver_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	struct clk_onecell_data onecell;

	struct si5324_parameters	params;

	struct clk		*pxtal;
	const char		*pxtal_name;
	struct clk_hw		xtal;

	struct clk		*pclkin1;
	const char		*pclkin1_name;
	struct clk_hw		clkin1;

	struct clk		*pclkin2;
	const char		*pclkin2_name;
	struct clk_hw		clkin2;

	struct si5324_hw_data	pll;
	struct si5324_hw_data	*clkout;

	/* temporary solution to provide actual rates */
	unsigned long rate_clkout0;
	unsigned long rate_clkout1;
};

static const char * const si5324_input_names[] = {
	"xtal", "clkin1", "clkin2"
};

static const char * const si5324_pll_name = "pll";

static const char * const si5324_clkout_names[] = {
	"clk0", "clk1"
};

/*
 * Si5324 i2c regmap
 */
static inline u8 si5324_reg_read(struct si5324_driver_data *drvdata, u8 reg)
{
	u32 val;
	int ret;

	ret = regmap_read(drvdata->regmap, reg, &val);
	if (ret) {
		dev_err(&drvdata->client->dev,
			"unable to read from reg%02x\n", reg);
		return 0;
	} else {
		dev_dbg(&drvdata->client->dev, "Read value 0x%02x @%02d\n",
			(int)val, (int)reg);
	}

	return (u8)val;
}

static inline int si5324_bulk_read(struct si5324_driver_data *drvdata,
				   u8 reg, u8 count, u8 *buf)
{
	return regmap_bulk_read(drvdata->regmap, reg, buf, count);
}

static inline int si5324_reg_write(struct si5324_driver_data *drvdata,
				   u8 reg, u8 val)
{
	u8 readback_val;
	int ret = regmap_write(drvdata->regmap, reg, val);
	dev_dbg(&drvdata->client->dev, "si5324_reg_write() 0x%02x @%02d\n", (int)val, (int)reg);
#if 0
	readback_val = si5324_reg_read(drvdata, reg);
	if (readback_val != val) {
		dev_err(&drvdata->client->dev, "readback 0x%02x @%02d, expected 0x%02x\n", (int)readback_val, (int)reg, (int)val);
	}
#endif
	return ret;
}

static inline int si5324_bulk_write(struct si5324_driver_data *drvdata,
				    u8 reg, u8 count, const u8 *buf)
{
	return regmap_raw_write(drvdata->regmap, reg, buf, count);
}

static inline int si5324_set_bits(struct si5324_driver_data *drvdata,
				  u8 reg, u8 mask, u8 val)
{
	return regmap_update_bits(drvdata->regmap, reg, mask, val);
}

/* similar to Si5324_DoSettings() */
static inline int si5324_bulk_scatter_write(struct si5324_driver_data *drvdata,
					u8 count/*number of reg/val pairs*/, const u8 *buf)
{
	int i;
	int result = 0;
	for (i = 0; i < count; i++) {
		result = si5324_reg_write(drvdata, buf[i * 2]/*reg*/, buf[i * 2 + 1]/*val*/);
		if (result) return result;
	}
	return result;
}

/* bare-metal: SI5324_DEFAULTS[] */
static void si5324_initialize(struct si5324_driver_data *drvdata)
{
	/* keep RST_REG asserted for 10 ms */
	si5324_set_bits(drvdata, SI5324_RESET,
		SI5324_RST_REG, SI5324_RST_REG);
	msleep(10);
	si5324_set_bits(drvdata, SI5324_RESET,
		SI5324_RST_REG, 0);
	/* wait 10 ms after de-assert */
	msleep(10);

#if (defined(FORCE_BYPASS) && FORCE_BYPASS)
#error FORCE_BYPASS not used/tested/supported.
	dev_dbg(&drvdata->client->dev, "Configuring for bypass mode of CLKIN1 to CLKOUT1\n");
	si5324_reg_write(drvdata,  0, 0x16 /* bypass */);
#  if 0 /*@TODO pin-select by default, but considering support clock input selection */
	si5324_reg_write(drvdata,  3, 0x15 /* sq_ical */);
	si5324_reg_write(drvdata,  4, 0x12 /* manual selection mode */);
	si5324_reg_write(drvdata, 11, 0x40 /* enable both */);
	si5324_reg_write(drvdata, 21, 0xfc /* cksel_pin off */);
#  endif

#else /* normal, non-bypass mode */
	// Disable output clocks during calibration (bit 4 SQ_ICAL=1),
	// other bits are default
	si5324_reg_write(drvdata, 3, 0x15);

#if 0 /* bare-metal setting */
	// Auto select clock (automatic revertive) (bit 7:6 AUTOSEL_REG=10)
	// History delay default
	si5324_reg_write(drvdata, 4, 0x92);
#else
	/* manual */
	si5324_reg_write(drvdata, 4, 0x12);
#endif
	// Disable CKOUT2 (SFOUT2_REG=001)
	// set CKOUT1 to LVDS (SFOUT1_REG=111)
	// (default is LVPECL for both)
	si5324_reg_write(drvdata, 6, 0x0F);
	// enable CKOUT1 output (bit 2 DSBL1_REG=0)
	// disable CKOUT2 output (bit 3 DSBL2_REG=1)
	si5324_reg_write(drvdata, 10, 0x08);
	// Disable CKIN2 input buffer (bit 1 PD_CK2=1)
	// enable CKIN1 buffer (bit 0 PD_CK1=0)
	// (bit 6 is reserved, write default value)
	si5324_reg_write(drvdata, 11, 0x42);
#if 1 // XPAR_VID_PHY_CONTROLLER_HDMI_FAST_SWITCH
    // Set lock time to 13.3ms (bits 2:0 LOCKT=011)
    // other bits are default
	si5324_reg_write(drvdata, 19, 0x23);  // 0x29
#else
	// Set lock time to 53ms as recommended (bits 2:0 LOCKT=001)
	// other bits are default
	si5324_reg_write(drvdata, 19, 0x2f);  // 0x29
#endif
#if 1
	/* bare-metal does not set this */
	/* ignore pin control  CS_CA pin is ignored, CS_CA output pin tristated */
	si5324_reg_write(drvdata, 21, 0xfc);
#endif
	// Enable fast locking (bit 0 FASTLOCK=1)
	si5324_reg_write(drvdata, 137, 0x01);   // FASTLOCK=1 (enable fast locking)
#endif
}

#define SI5324_PARAMETERS_REG		25
#define SI5324_PARAMETERS_LENGTH		24

/*
 *  0 25 N1_HS[2:0]
 *  6 31 NC1_LS[19:16]
 *  7 32 NC1_LS[15:8]
 *  8 33 NC1_LS[7:0]
 *  9 34 NC2_LS[19:16]
 * 10 35 NC2_LS[15:8]
 * 11 36 NC2_LS[7:0]
 * 15 40 N2_HS[2:0] N2_LS[19:16]
 * 16 41 N2_LS[15:8]
 * 17 42 N2_LS[7:0]
 * 18 43 N31[18:16]
 * 19 44 N31[15:8]
 * 20 45 N31[7:0]
 * 21 46 N32[18:16]
 * 22 47 N32[15:8]
 * 23 48 N32[7:0]
 */

static void si5324_read_parameters(struct si5324_driver_data *drvdata)
{
	u8 buf[SI5324_PARAMETERS_LENGTH];

	si5324_bulk_read(drvdata, 25, 1, &buf[0]);
	si5324_bulk_read(drvdata, 31, 6, &buf[6]);
	si5324_bulk_read(drvdata, 40, 9, &buf[15]);

	/* high-speed output divider */
	drvdata->params.n1_hs = (buf[0] >> 5);
	drvdata->params.n1_hs += 4;
	si5324_dbg("N1_HS = %u\n", drvdata->params.n1_hs);
	/* low-speed output divider for clkout1 */
	drvdata->params.nc1_ls = ((buf[6] & 0x0f) << 16) | (buf[ 7] << 8) | buf[ 8];
	drvdata->params.nc1_ls += 1;
	si5324_dbg("NC1_LS = %u\n", drvdata->params.nc1_ls);
	/* low-speed output divider for clkout2 */
	drvdata->params.nc2_ls = ((buf[9] & 0x0f) << 16) | (buf[10] << 8) | buf[11];
	drvdata->params.nc2_ls += 1;
	si5324_dbg("NC2_LS = %u\n", drvdata->params.nc2_ls);
	/* low-speed feedback divider (PLL multiplier) */
	drvdata->params.n2_ls = ((buf[15] & 0x0f) << 16) | (buf[16] << 8) | buf[17];
	drvdata->params.n2_ls += 1;
	si5324_dbg("N2_LS = %u\n", drvdata->params.n2_ls);
	/* high-speed feedback divider (PLL multiplier) */
	drvdata->params.n2_hs = buf[15] >> 5;
	drvdata->params.n2_hs += 4;
	si5324_dbg("N2_HS = %u\n", drvdata->params.n2_hs);
	/* input divider for clk1 */
	drvdata->params.n31 = ((buf[18] & 0x0f) << 16) | (buf[19] << 8) | buf[20];
	drvdata->params.n31 += 1;
	si5324_dbg("N31 = %u\n", drvdata->params.n31);
	/* input divider for clk2 */
	drvdata->params.n32 = ((buf[21] & 0x0f) << 16) | (buf[22] << 8) | buf[23];
	drvdata->params.n32 += 1;
	si5324_dbg("N32 = %u\n", drvdata->params.n32);
	drvdata->params.valid = 1;
}

static void si5324_write_parameters(struct si5324_driver_data *drvdata)
{
	u8 buf[SI5324_PARAMETERS_LENGTH];
	u32 reg_val;
	/* high-speed output divider */
	reg_val = drvdata->params.n1_hs - 4;
	buf[0] = reg_val << 5;
	/* low-speed output divider for clkout1 */
	reg_val = drvdata->params.nc1_ls - 1;
	buf[6] = (reg_val >> 16) & 0x0f;
	buf[7] = (reg_val >> 8) & 0xff;
	buf[8] = reg_val & 0xff;
	/* low-speed output divider for clkout2 */
	reg_val = drvdata->params.nc2_ls;
	buf[ 9] = (reg_val >> 16) & 0x0f;
	buf[10] = (reg_val >> 8) & 0xff;
	buf[11] = reg_val & 0xff;
	/* low-speed feedback divider (PLL multiplier) */
	reg_val = drvdata->params.n2_ls + 1;
	buf[15] = (reg_val >> 16) & 0x0f;
	buf[16] = (reg_val >> 8) & 0xff;
	buf[17] = reg_val & 0xff;
	/* high-speed feedback divider (PLL multiplier) */
	reg_val = drvdata->params.n2_hs - 4;
	buf[15] |= reg_val << 5;
	/* input divider for clk1 */
	reg_val = drvdata->params.n31;
	buf[18] = (reg_val >> 16) & 0x0f;
	buf[19] = (reg_val >> 8) & 0xff;
	buf[20] = reg_val & 0xff;
	/* input divider for clk2 */
	reg_val = drvdata->params.n31;
	buf[21] = (reg_val >> 16) & 0x0f;
	buf[22] = (reg_val >> 8) & 0xff;
	buf[23] = reg_val & 0xff;
	si5324_bulk_write(drvdata, 25, 1, &buf[0]);
	si5324_bulk_write(drvdata, 31, 6, &buf[6]);
	si5324_bulk_write(drvdata, 40, 9, &buf[15]);

}

static bool si5324_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return true;
#if 0/*@TODO */
	return false;
#endif
}

static bool si5324_regmap_is_readable(struct device *dev, unsigned int reg)
{
	bool result = true;
	/* reserved registers */
	if (reg >= 12 && reg <= 18)
		result =  false;
	else if (reg >= 26 && reg <= 30)
		result =  false;
	else if (reg >= 37 && reg <= 39)
		result =  false;
	else if (reg >= 49 && reg <= 54)
		result =  false;
	else if (reg >= 56 && reg <= 127)
		result =  false;
	else if (reg >= 144)
		result =  false;
#if 0
	si5324_dbg("si5324_regmap_is_readable(reg0x%02x) = %u\n", reg, result);
#endif
	return result;
}

static bool si5324_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	bool result = true;
	/* reserved registers */
	if (reg >= 12 && reg <= 18)
		result =  false;
	else if (reg >= 26 && reg <= 30)
		result =  false;
	else if (reg >= 37 && reg <= 39)
		result =  false;
	else if (reg >= 49 && reg <= 54)
		result =  false;
	else if (reg >= 56 && reg <= 127)
		result =  false;
	else if (reg >= 144)
		result =  false;
	/* read-only */
	else if (reg >= 128 && reg <= 130)
		result =  false;
	else if (reg >= 134 && reg <= 135)
		result =  false;
#if 0
	si5324_dbg("si5324_regmap_is_writeable(reg0x%02x) = %u\n", reg, result);
#endif
	return result;
}

static const struct regmap_config si5324_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 144,
	.writeable_reg = si5324_regmap_is_writeable,
	.readable_reg = si5324_regmap_is_readable,
	.volatile_reg = si5324_regmap_is_volatile,
};

/*
 * Si5324 xtal clock input
 */
static int si5324_xtal_prepare(struct clk_hw *hw)
{
	struct si5324_driver_data *drvdata =
		container_of(hw, struct si5324_driver_data, xtal);
#if (!defined(FORCE_BYPASS) || !FORCE_BYPASS)
	si5324_dbg("si5324_xtal_prepare; enable free-running mode from crystal.\n");
	/* enable free-run */
	si5324_set_bits(drvdata, 0, 0x40, 0x40);
	/* select CKIN_2 [7:6]=01 */
	si5324_set_bits(drvdata, 3, 0xc0, 0x40);
	/* clkin2 powered, clkin1 powered-down, xtal connects to clkin2 */
	si5324_set_bits(drvdata, SI5324_POWERDOWN,
			SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
#endif
	return 0;
}

static void si5324_xtal_unprepare(struct clk_hw *hw)
{
	struct si5324_driver_data *drvdata =
		container_of(hw, struct si5324_driver_data, xtal);
	si5324_dbg("si5324_xtal_unprepare\n");
}

static const struct clk_ops si5324_xtal_ops = {
	.prepare = si5324_xtal_prepare,
	.unprepare = si5324_xtal_unprepare,
};

/*
 * Si5324 clkin1/clkin2 clock input
 */
static int si5324_clkin_prepare(struct clk_hw *hw)
{
	struct si5324_driver_data *drvdata;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	si5324_dbg("si5324_clkin_prepare() for hwdata->num = %d\n", hwdata->num);

	/* clkin1? */
	if (hwdata->num == 0/*@TODO: verify if this should be 1*/) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin1);
		/* disable free-run */
		si5324_set_bits(drvdata, SI5324_REG0, SI5324_REG0_FREE_RUN, 0);
		/* clkin1 powered, clkin2 powered-down*/
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
			SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK2);
	} else if (hwdata->num == 1/*@TODO: verify if this should be 2*/) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin2);
		/* disable free-run */
		si5324_set_bits(drvdata, SI5324_REG0, SI5324_REG0_FREE_RUN, 0);
		/* clkin2 powered, clkin1 powered-down*/
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
			SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
	} else {
	}
	return 0;
}

static void si5324_clkin_unprepare(struct clk_hw *hw)
{
	struct si5324_driver_data *drvdata;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	si5324_dbg("si5324_clkin_unprepare\n");
	if (hwdata->num == 0/*@TODO:1?*/) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin1);
	} else if (hwdata->num == 1/*@TODO:2?*/) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin2);
	} else {
	}
}

/*
 * @recalc_rate	Recalculate the rate of this clock, by querying hardware. The
 *		parent rate is an input parameter.  It is up to the caller to
 *		ensure that the prepare_mutex is held across this call.
 *		Returns the calculated rate.
 */
static unsigned long si5324_clkin_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	return 0;
#if 0
	struct si5324_driver_data *drvdata = NULL;
	unsigned long rate;
	unsigned char idiv;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	idiv = 1;

	si5324_dbg("si5324_clkin_recalc_rate() hwdata->num = %d\n", hwdata->num);
	if (hwdata->num == 0) {
		drvdata = container_of(hw, struct si5324_driver_data, xtal);
		si5324_dbg("si5324_clkin_recalc_rate(parent_rate=%lu for xtal)\n", parent_rate);
	} else if (hwdata->num == 1) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin1);
		si5324_dbg("si5324_clkin_recalc_rate(parent_rate=%lu for clkin1)\n", parent_rate);
	} else if (hwdata->num == 2) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin2);
		si5324_dbg("si5324_clkin_recalc_rate(parent_rate=%lu for clkin2)\n", parent_rate);
	} else {
		si5324_dbg("si5324_clkin_recalc_rate() hwdata->num = %d\n", hwdata->num);
		return 0;
	}

	rate = parent_rate;

	/* f3 (behind /N3x) is out of range, i,e, >2MHz? */
	if (rate > 2000000) {
		/* set input divider */
		idiv = (rate + 2000000 - 1) / 2000000;
		rate = parent_rate / idiv;
	}
/*
	si5324_set_bits(drvdata, SI5324_PLL_INPUT_SOURCE,
			SI5324_CLKIN_DIV_MASK, idiv);
*/
	if (drvdata)
	dev_dbg(&drvdata->client->dev, "%s - clkin div = %d, rate = %lu\n",
		__func__, (1 << (idiv >> 6)), rate);
	return rate;
#endif
}

static const struct clk_ops si5324_clkin_ops = {
	.prepare = si5324_clkin_prepare,
	.unprepare = si5324_clkin_unprepare,
	.recalc_rate = si5324_clkin_recalc_rate,
};

/* Select other clock input to the PLL
 */
static int _si5324_pll_reparent(struct si5324_driver_data *drvdata,
				int num, enum si5324_pll_src parent)
{
	si5324_dbg("_si5324_pll_reparent() for parent = %d\n", (int)parent);

	if (parent == SI5324_PLL_SRC_XTAL) {
		/* enable free-run */
		si5324_set_bits(drvdata, SI5324_REG0,
				SI5324_REG0_FREE_RUN, SI5324_REG0_FREE_RUN);
		/* clkin2 powered, clkin1 powered-down, xtal connects to clkin2 */
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
		/* select clkin2 */
		si5324_set_bits(drvdata, SI5324_CKSEL,
				3 << 6, 1 << 6);
	} else if (parent == SI5324_PLL_SRC_CLKIN1) {
		/* disable free-run */
		si5324_set_bits(drvdata, SI5324_REG0,
				SI5324_REG0_FREE_RUN, 0);
		/* clkin1 powered, clkin2 powered-down */
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK2);
		/* select clkin1 */
		si5324_set_bits(drvdata, SI5324_CKSEL,
				3 << 6, 0);
	} else if (parent == SI5324_PLL_SRC_CLKIN2) {
		/* disable free-run */
		si5324_set_bits(drvdata, SI5324_REG0,
				SI5324_REG0_FREE_RUN, 0);
		/* clkin2 powered, clkin1 powered-down */
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
		/* select clkin2 */
		si5324_set_bits(drvdata, SI5324_CKSEL,
				3 << 6, 1 << 6);
	}
	dev_dbg(&drvdata->client->dev, "_si5324_pll_reparent()\n");
	si5324_reg_read(drvdata, 0);
	si5324_reg_read(drvdata, 4);
	si5324_reg_read(drvdata, 3);
	return 0;
}

static unsigned char si5324_pll_get_parent(struct clk_hw *hw)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	return 0;
}

static int si5324_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	enum si5324_pll_src parent;
	si5324_dbg("si5324_pll_set_parent(index=%d)\n", index);

	if (index == 0)
		parent = SI5324_PLL_SRC_XTAL;
	else if (index == 1)
		parent = SI5324_PLL_SRC_CLKIN1;
	else if (index == 2)
		parent = SI5324_PLL_SRC_CLKIN2;
	else
		return -EINVAL;

	return _si5324_pll_reparent(hwdata->drvdata, hwdata->num, parent);
}

static unsigned long si5324_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	unsigned long rate;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	si5324_dbg("si5324_pll_recalc_rate(parent_rate=%lu)\n",
		parent_rate);

	if (!hwdata->drvdata->params.valid)
		si5324_read_parameters(hwdata->drvdata);
	WARN_ON(!hwdata->drvdata->params.valid);

	rate = parent_rate * hwdata->drvdata->params.n2_ls * hwdata->drvdata->params.n2_hs;

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: n2_ls = %u, n2_hs = %u, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->drvdata->params.n2_ls, hwdata->drvdata->params.n2_hs,
		parent_rate, (unsigned long)rate);
	return rate;
}

static long si5324_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	si5324_dbg("si5324_pll_round_rate(rate=%lu, parent_rate=%lu)\n",
		rate, *parent_rate);

#if 0
	unsigned long rfrac, denom, a, b, c;
	unsigned long long lltmp;

	if (rate < SI5324_PLL_VCO_MIN)
		rate = SI5324_PLL_VCO_MIN;
	if (rate > SI5324_PLL_VCO_MAX)
		rate = SI5324_PLL_VCO_MAX;

	/* determine integer part of feedback equation */
	a = rate / *parent_rate;

	if (a < SI5324_PLL_A_MIN)
		rate = *parent_rate * SI5324_PLL_A_MIN;
	if (a > SI5324_PLL_A_MAX)
		rate = *parent_rate * SI5324_PLL_A_MAX;

	/* find best approximation for b/c = fVCO mod fIN */
	denom = 1000 * 1000;
	lltmp = rate % (*parent_rate);
	lltmp *= denom;
	do_div(lltmp, *parent_rate);
	rfrac = (unsigned long)lltmp;

	b = 0;
	c = 1;
	if (rfrac)
		rational_best_approximation(rfrac, denom,
				    SI5324_PLL_B_MAX, SI5324_PLL_C_MAX, &b, &c);

	/* calculate parameters */
	hwdata->drvdata->params.p3  = c;
	hwdata->drvdata->params.p2  = (128 * b) % c;
	hwdata->drvdata->params.p1  = 128 * a;
	hwdata->drvdata->params.p1 += (128 * b / c);
	hwdata->drvdata->params.p1 -= 512;

	/* recalculate rate by fIN * (a + b/c) */
	lltmp  = *parent_rate;
	lltmp *= b;
	do_div(lltmp, c);

	rate  = (unsigned long)lltmp;
	rate += *parent_rate * a;

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: a = %lu, b = %lu, c = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), a, b, c,
		*parent_rate, rate);
#endif
	return rate;
}

static int si5324_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	si5324_dbg("si5324_pll_set_rate(rate=%lu, parent_rate=%lu)\n",
		rate, parent_rate);
#if 0
	u8 reg = (hwdata->num == 0) ? SI5324_PLLA_PARAMETERS :
		SI5324_PLLB_PARAMETERS;

	/* write multisynth parameters */
	si5324_write_parameters(hwdata->drvdata, reg, &hwdata->drvdata->params);

	/* plla/pllb ctrl is in clk6/clk7 ctrl registers */
	si5324_set_bits(hwdata->drvdata, SI5324_CLK6_CTRL + hwdata->num,
		SI5324_CLK_INTEGER_MODE,
		(hwdata->drvdata->params.p2 == 0) ? SI5324_CLK_INTEGER_MODE : 0);

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: p1 = %lu, p2 = %lu, p3 = %lu, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw),
		hwdata->drvdata->params.p1, hwdata->drvdata->params.p2, hwdata->drvdata->params.p3,
		parent_rate, rate);
#endif
	return 0;
}

static const struct clk_ops si5324_pll_ops = {
	.set_parent = si5324_pll_set_parent,
	.get_parent = si5324_pll_get_parent,
	.recalc_rate = si5324_pll_recalc_rate,
	.round_rate = si5324_pll_round_rate,
	.set_rate = si5324_pll_set_rate,
};

static int _si5324_clkout_set_drive_strength(
	struct si5324_driver_data *drvdata, int num,
	enum si5324_drive_strength drive)
{
#if 0
	u8 mask;

	if (num > 8)
		return -EINVAL;

	switch (drive) {
	case SI5324_DRIVE_2MA:
		mask = SI5324_CLK_DRIVE_STRENGTH_2MA;
		break;
	case SI5324_DRIVE_4MA:
		mask = SI5324_CLK_DRIVE_STRENGTH_4MA;
		break;
	case SI5324_DRIVE_6MA:
		mask = SI5324_CLK_DRIVE_STRENGTH_6MA;
		break;
	case SI5324_DRIVE_8MA:
		mask = SI5324_CLK_DRIVE_STRENGTH_8MA;
		break;
	default:
		return 0;
	}

	si5324_set_bits(drvdata, SI5324_CLK0_CTRL + num,
			SI5324_CLK_DRIVE_STRENGTH_MASK, mask);
#endif
	return 0;
}

static int _si5324_clkout_set_disable_state(
	struct si5324_driver_data *drvdata, int num,
	enum si5324_disable_state state)
{
#if 0
	u8 reg = (num < 4) ? SI5324_CLK3_0_DISABLE_STATE :
		SI5324_CLK7_4_DISABLE_STATE;
	u8 shift = (num < 4) ? (2 * num) : (2 * (num-4));
	u8 mask = SI5324_CLK_DISABLE_STATE_MASK << shift;
	u8 val;

	if (num > 8)
		return -EINVAL;

	switch (state) {
	case SI5324_DISABLE_LOW:
		val = SI5324_CLK_DISABLE_STATE_LOW;
		break;
	case SI5324_DISABLE_HIGH:
		val = SI5324_CLK_DISABLE_STATE_HIGH;
		break;
	case SI5324_DISABLE_FLOATING:
		val = SI5324_CLK_DISABLE_STATE_FLOAT;
		break;
	case SI5324_DISABLE_NEVER:
		val = SI5324_CLK_DISABLE_STATE_NEVER;
		break;
	default:
		return 0;
	}

	si5324_set_bits(drvdata, reg, mask, val << shift);
#endif
	return 0;
}

static int si5324_clkout_prepare(struct clk_hw *hw)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	/* clear power-down bit for output clock num */
	si5324_set_bits(hwdata->drvdata, SI5324_DSBL_CLKOUT,
			1 << (hwdata->num + 2), 0);
	return 0;
}

static void si5324_clkout_unprepare(struct clk_hw *hw)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	/* set power-down bit for output clock num */
	si5324_set_bits(hwdata->drvdata, SI5324_DSBL_CLKOUT,
			1 << (hwdata->num + 2), 1 << (hwdata->num + 2));
}

/*
 * Si5324 clkout divider
 */
static unsigned long si5324_clkout_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	unsigned long rate = 0;

	si5324_dbg("si5324_clkout_recalc_rate(parent_rate=%lu)\n", parent_rate);
#if 0
	si5324_dbg("si5324_clkout_recalc_rate(parent_rate=%lu) clkout%d\n",
		parent_rate, hwdata->num);

	//if (!hwdata->drvdata->params.valid)
		si5324_read_parameters(hwdata->drvdata, 24);
	WARN_ON(!hwdata->drvdata->params.valid);

	/* clkout1? */
	if (hwdata->num == 0)
		rate = (parent_rate / hwdata->drvdata->params.n1_hs) / hwdata->drvdata->params.nc1_ls;
	/* clkout2? */
	else if (hwdata->num == 1)
		rate = (parent_rate / hwdata->drvdata->params.n1_hs) / hwdata->drvdata->params.nc2_ls;

	si5324_dbg("si5324_clkout_recalc_rate(parent_rate=%lu) => clkout%d=%lu (invalid)\n",
		parent_rate, hwdata->num, rate);
#endif

	rate = hwdata->drvdata->rate_clkout0;

	si5324_dbg("si5324_clkout_recalc_rate() = %lu\n", rate);
	return rate;
}

/* round_rate selects the rate closest to the requested one.
determine_rate does the same but even better by changing the clock’s
parent. The actual setting is done by set_rate. recalc_rate is called
when a parent changes rate.  */
static long si5324_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	unsigned char rdiv;
	u32 NCn_ls, N2_ls, N3n;
	u8  N1_hs, N2_hs, BwSel;
	u32 actual_rate;
	int result;
	si5324_dbg("si5324_clkout_round_rate(rate=%lu, parent_rate=%lu)\n",
		rate, *parent_rate);

	si5324_dbg("%s - %s: parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), *parent_rate, rate);

	// Calculate the frequency settings for the Si5324
	result = Si5324_CalcFreqSettings(114285000, rate, &actual_rate,
		&N1_hs, &NCn_ls, &N2_hs, &N2_ls, &N3n, &BwSel);

#if 0
	if (rate > SI5324_CLKOUT_MAX_FREQ)
		rate = SI5324_CLKOUT_MAX_FREQ;
	if (rate < SI5324_CLKOUT_MIN_FREQ)
		rate = SI5324_CLKOUT_MIN_FREQ;

	/* request frequency if multisync master */
	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		/* use r divider for frequencies below 1MHz */
		rdiv = SI5324_OUTPUT_CLK_DIV_1;
		while (rate < SI5324_MULTISYNTH_MIN_FREQ &&
		       rdiv < SI5324_OUTPUT_CLK_DIV_128) {
			rdiv += 1;
			rate *= 2;
		}
		*parent_rate = rate;
	} else {
		unsigned long new_rate, new_err, err;

		/* round to closed rdiv */
		rdiv = SI5324_OUTPUT_CLK_DIV_1;
		new_rate = *parent_rate;
		err = abs(new_rate - rate);
		do {
			new_rate >>= 1;
			new_err = abs(new_rate - rate);
			if (new_err > err || rdiv == SI5324_OUTPUT_CLK_DIV_128)
				break;
			rdiv++;
			err = new_err;
		} while (1);
	}
	rate = *parent_rate >> rdiv;

	dev_dbg(&hwdata->drvdata->client->dev,
		"%s - %s: rdiv = %u, parent_rate = %lu, rate = %lu\n",
		__func__, clk_hw_get_name(hw), (1 << rdiv),
		*parent_rate, rate);
#endif

	si5324_dbg("si5324_clkout_round_rate() = %lu\n", actual_rate);
	return actual_rate;
}

static int si5324_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	u32 NCn_ls, N2_ls, N3n;
	u8  N1_hs, N2_hs, BwSel;
	u32 actual_rate;
	int result;
	u8  buf[14*2]; // Need to set 14 registers
	int i;
	int rc;

	si5324_dbg("si5324_clkout_set_rate(rate = %lu)\n", rate);

	// Calculate the frequency settings for the Si5324
	result = Si5324_CalcFreqSettings(114285000, rate, &actual_rate,
	                                 &N1_hs, &NCn_ls, &N2_hs, &N2_ls, &N3n,
	                                 &BwSel);
	si5324_dbg("N1_HS = %u\n", (unsigned int)N1_hs + 4);
	si5324_dbg("NC1_LS = %u\n", (unsigned int)NCn_ls + 1);
	si5324_dbg("N2_HS = %u\n", (unsigned int)N2_hs + 4);
	si5324_dbg("N2_LS = %u\n", (unsigned int)N2_ls + 1);
	si5324_dbg("N3 = %u\n", (unsigned int)N3n + 1);
	si5324_dbg("actual rate = %u\n", actual_rate);

	/* remember actual clkout0 output rate */
	hwdata->drvdata->rate_clkout0 = rate;

	i = 0;

	// Free running mode or use a reference clock
	buf[i] = 0;
	// Enable free running mode
	buf[i+1] = 0x54;
	i += 2;

	// Loop bandwidth
	buf[i]   = 2;
	buf[i+1] = (BwSel << 4) | 0x02;
	i += 2;

	// Enable reference clock 2 in free running mode
	buf[i] = 11;
	//Enable input clock 2
	buf[i+1] = 0x40;
	i += 2;

	// N1_HS
	buf[i]   = 25;
	buf[i+1] = N1_hs << 5;
	i += 2;

	// NC1_LS
	buf[i]   = 31;
	buf[i+1] = (u8)((NCn_ls & 0x000F0000) >> 16);
	buf[i+2] = 32;
	buf[i+3] = (u8)((NCn_ls & 0x0000FF00) >>  8);
	buf[i+4] = 33;
	buf[i+5] = (u8)( NCn_ls & 0x000000FF       );
	i += 6;

	// N2_HS and N2_LS
	buf[i]    = 40;
	buf[i+1]  = (N2_hs << 5);
	// N2_LS upper bits (same register as N2_HS)
	buf[i+1] |= (u8)((N2_ls & 0x000F0000) >> 16);
	buf[i+2]  = 41;
	buf[i+3]  = (u8)((N2_ls & 0x0000FF00) >>  8);
	buf[i+4]  = 42;
	buf[i+5]  = (u8)( N2_ls & 0x000000FF       );
	i += 6;

	// N32 (CLKIN2 or XTAL in FREERUNNING mode)
	buf[i]   = 46;
	buf[i+2] = 47;
	buf[i+4] = 48;
	buf[i+1] = (u8)((N3n & 0x00070000) >> 16);
	buf[i+3] = (u8)((N3n & 0x0000FF00) >>  8);
	buf[i+5] = (u8)( N3n & 0x000000FF       );
	i += 6;

	// Start calibration
	buf[i]   = 136;
	buf[i+1] = 0x40;
	i += 2;

	hwdata->drvdata->params.valid = 0;
	// disable CKOUT1 output (bit 2 DSBL1_REG=1)
	// disable CKOUT2 output (bit 3 DSBL2_REG=1)
	//si5324_reg_write(hwdata->drvdata, 10, 0x0c);
	si5324_reg_read(hwdata->drvdata, 0);
	si5324_reg_read(hwdata->drvdata, 3);
	si5324_reg_read(hwdata->drvdata, 4);
	si5324_reg_read(hwdata->drvdata, 11);
	si5324_reg_read(hwdata->drvdata, 21);
	rc = si5324_bulk_scatter_write(hwdata->drvdata, 14, buf);
	//  enable CKOUT1 output (bit 2 DSBL1_REG=1)
	// disable CKOUT2 output (bit 3 DSBL2_REG=1)
	//si5324_reg_write(hwdata->drvdata, 10, 0x08);

#if 0
	/* dump all registers */
	for (i = 0; i < 145; i++)
		si5324_reg_read(hwdata->drvdata, i);
#endif
	return rc;
}

static const struct clk_ops si5324_clkout_ops = {
	.prepare = si5324_clkout_prepare,
	.unprepare = si5324_clkout_unprepare,
	.recalc_rate = si5324_clkout_recalc_rate,
	.round_rate = si5324_clkout_round_rate,
	.set_rate = si5324_clkout_set_rate,
};

/*
 * Si5324 i2c probe and DT
 */
#ifdef CONFIG_OF
static const struct of_device_id si5324_dt_ids[] = {
	{ .compatible = "silabs,si5324" },
	{ .compatible = "silabs,si5319" },
	{ }
};
MODULE_DEVICE_TABLE(of, si5324_dt_ids);

static int si5324_dt_parse(struct i2c_client *client)
{
	struct device_node *child, *np = client->dev.of_node;
	struct si5324_platform_data *pdata;
	struct property *prop;
	const __be32 *p;
	int num = 0;
	u32 val;

	if (np == NULL)
		return 0;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/*
	 * property silabs,pll-source : <num src>, [<..>]
	 * allow to selectively set pll source
	 */
	of_property_for_each_u32(np, "silabs,pll-source", prop, p, num) {
		if (num >= 1) {
			dev_err(&client->dev,
				"invalid pll %d on pll-source prop\n", num);
			return -EINVAL;
		}

		p = of_prop_next_u32(prop, p, &val);
		if (!p) {
			dev_err(&client->dev,
				"missing pll-source for pll %d\n", num);
			return -EINVAL;
		}

		switch (val) {
		case 0:
			dev_dbg(&client->dev, "using xtal as parent for pll\n");
			pdata->pll_src = SI5324_PLL_SRC_XTAL;
			break;
		case 1:
			dev_dbg(&client->dev, "using clkin1 as parent for pll\n");
			pdata->pll_src = SI5324_PLL_SRC_CLKIN1;
			break;
		case 2:
			dev_dbg(&client->dev, "using clkin2 as parent for pll\n");
			pdata->pll_src = SI5324_PLL_SRC_CLKIN2;
			break;
		default:
			dev_err(&client->dev,
				 "invalid parent %d for pll %d\n", val, num);
			return -EINVAL;
		}
	}

	/* per clkout properties */
	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, "reg", &num)) {
			dev_err(&client->dev, "missing reg property of %s\n",
				child->name);
			goto put_child;
		}

		if (num >= 2) {
			dev_err(&client->dev, "invalid clkout %d\n", num);
			goto put_child;
		}

		if (!of_property_read_u32(child, "silabs,drive-strength",
					  &val)) {
			switch (val) {
			case SI5324_DRIVE_2MA:
			case SI5324_DRIVE_4MA:
			case SI5324_DRIVE_6MA:
			case SI5324_DRIVE_8MA:
				pdata->clkout[num].drive = val;
				break;
			default:
				dev_err(&client->dev,
					"invalid drive strength %d for clkout %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "silabs,disable-state",
					  &val)) {
			switch (val) {
			case 0:
				pdata->clkout[num].disable_state =
					SI5324_DISABLE_LOW;
				break;
			case 1:
				pdata->clkout[num].disable_state =
					SI5324_DISABLE_HIGH;
				break;
			case 2:
				pdata->clkout[num].disable_state =
					SI5324_DISABLE_FLOATING;
				break;
			case 3:
				pdata->clkout[num].disable_state =
					SI5324_DISABLE_NEVER;
				break;
			default:
				dev_err(&client->dev,
					"invalid disable state %d for clkout %d\n",
					val, num);
				goto put_child;
			}
		}

		if (!of_property_read_u32(child, "clock-frequency", &val)) {
			dev_dbg(&client->dev, "clock-frequency = %u\n", val);
			pdata->clkout[num].rate = val;
		}

		pdata->clkout[num].pll_master =
			of_property_read_bool(child, "silabs,pll-master");
	}
	client->dev.platform_data = pdata;

	return 0;
put_child:
	of_node_put(child);
	return -EINVAL;
}
#else
static int si5324_dt_parse(struct i2c_client *client)
{
	return 0;
}
#endif /* CONFIG_OF */

static int si5324_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct si5324_platform_data *pdata;
	struct si5324_driver_data *drvdata;
	struct clk_init_data init;
	struct clk *clk;
	const char *parent_names[3];
	u8 val;
	u8 num_parents, num_clocks;
	int ret, n;

	ret = si5324_dt_parse(client);
	if (ret)
		return ret;

	pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	drvdata = devm_kzalloc(&client->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&client->dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, drvdata);
	drvdata->client = client;
	drvdata->pxtal = devm_clk_get(&client->dev, "xtal");
	drvdata->pclkin1 = devm_clk_get(&client->dev, "clkin1");
	drvdata->pclkin2 = devm_clk_get(&client->dev, "clkin2");

	if (PTR_ERR(drvdata->pxtal) == -EPROBE_DEFER ||
		PTR_ERR(drvdata->pclkin1) == -EPROBE_DEFER ||
		PTR_ERR(drvdata->pclkin2) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
#if 0
	dev_err(&client->dev, "drvdata->pxtal =%p\n", drvdata->pxtal);
#endif
	drvdata->regmap = devm_regmap_init_i2c(client, &si5324_regmap_config);
	if (IS_ERR(drvdata->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(drvdata->regmap);
	}

	if ((si5324_reg_read(drvdata, 134) == 0x01) && \
                (si5324_reg_read(drvdata, 135) == 0x82)) {
	        si5324_dbg("DevID : 0x01 0x82 : Si5324 found");
	} else if ((si5324_reg_read(drvdata, 134) == 0x01) && \
                        (si5324_reg_read(drvdata, 135) == 0x32)) {
		si5324_dbg("DevID : 0x01 0x32 : Si5319 found");
	} else {
		dev_err(&client->dev, "Identification registers do not indicate \
                                        presence of Si5324 or Si5319.\n");
		return -ENODEV;
	}
	si5324_initialize(drvdata);

#if (!defined(FORCE_BYPASS) || !FORCE_BYPASS)
	/* setup input clock configuration */
	ret = _si5324_pll_reparent(drvdata, 0, pdata->pll_src);
	if (ret) {
		dev_err(&client->dev,
			"failed to reparent pll to %d\n",
			pdata->pll_src);
		return ret;
	}
#endif

	for (n = 0; n < 2; n++) {

		ret = _si5324_clkout_set_drive_strength(drvdata, n,
							pdata->clkout[n].drive);
		if (ret) {
			dev_err(&client->dev,
				"failed set drive strength of clkout%d to %d\n",
				n, pdata->clkout[n].drive);
			return ret;
		}

		ret = _si5324_clkout_set_disable_state(drvdata, n,
						pdata->clkout[n].disable_state);
		if (ret) {
			dev_err(&client->dev,
				"failed set disable state of clkout%d to %d\n",
				n, pdata->clkout[n].disable_state);
			return ret;
		}
	}

	if (!IS_ERR(drvdata->pxtal)) {
		si5324_dbg("Enabling xtal clock\n");
		clk_prepare_enable(drvdata->pxtal);
	}
	if (!IS_ERR(drvdata->pclkin1))
		clk_prepare_enable(drvdata->pclkin1);
	if (!IS_ERR(drvdata->pclkin2))
		clk_prepare_enable(drvdata->pclkin2);

	/* register xtal input clock gate */
	memset(&init, 0, sizeof(init));
	init.name = si5324_input_names[0];
	init.ops = &si5324_xtal_ops;
	init.flags = 0;
	if (!IS_ERR(drvdata->pxtal)) {
		drvdata->pxtal_name = __clk_get_name(drvdata->pxtal);
		init.parent_names = &drvdata->pxtal_name;
		si5324_dbg("xtal parent name: %s\n", init.parent_names[0]);
		init.num_parents = 1;
	}
	drvdata->xtal.init = &init;
	clk = devm_clk_register(&client->dev, &drvdata->xtal);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	/* register clkin1 input clock gate */
	memset(&init, 0, sizeof(init));
	init.name = si5324_input_names[1];
	init.ops = &si5324_clkin_ops;
	if (!IS_ERR(drvdata->pclkin1)) {
		drvdata->pclkin1_name = __clk_get_name(drvdata->pclkin1);
		init.parent_names = &drvdata->pclkin1_name;
		init.num_parents = 1;
	}
	drvdata->clkin1.init = &init;
	clk = devm_clk_register(&client->dev, &drvdata->clkin1);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register %s\n",
			init.name);
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	/* register clkin2 input clock gate */
	memset(&init, 0, sizeof(init));
	init.name = si5324_input_names[2];
	init.ops = &si5324_clkin_ops;
	if (!IS_ERR(drvdata->pclkin2)) {
		drvdata->pclkin2_name = __clk_get_name(drvdata->pclkin2);
		init.parent_names = &drvdata->pclkin2_name;
		init.num_parents = 1;
	}
	drvdata->clkin2.init = &init;
	clk = devm_clk_register(&client->dev, &drvdata->clkin2);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register %s\n",
			init.name);
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	/* Si5324 allows to mux xtal or clkin1 or clkin2 to PLL input */
	num_parents = 3;
	parent_names[0] = si5324_input_names[0];
	parent_names[1] = si5324_input_names[1];
	parent_names[2] = si5324_input_names[2];

	/* register PLL */
	drvdata->pll.num = 0;
	drvdata->pll.drvdata = drvdata;
	drvdata->pll.hw.init = &init;
	memset(&init, 0, sizeof(init));
	init.name = si5324_pll_name;
	init.ops = &si5324_pll_ops;
	init.flags = 0;
	init.flags |= CLK_SET_RATE_PARENT;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	clk = devm_clk_register(&client->dev, &drvdata->pll.hw);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	/* register clk multisync and clk out divider */
	num_clocks = 2;
	num_parents = 1;
	parent_names[0] = si5324_pll_name;

	drvdata->clkout = devm_kzalloc(&client->dev, num_clocks *
				       sizeof(*drvdata->clkout), GFP_KERNEL);

	drvdata->onecell.clk_num = num_clocks;
	drvdata->onecell.clks = devm_kzalloc(&client->dev,
		num_clocks * sizeof(*drvdata->onecell.clks), GFP_KERNEL);

	if (WARN_ON(!drvdata->clkout) || (!drvdata->onecell.clks)) {
		ret = -ENOMEM;
		goto err_clk;
	}

	for (n = 0; n < num_clocks; n++) {
		drvdata->clkout[n].num = n;
		drvdata->clkout[n].drvdata = drvdata;
		drvdata->clkout[n].hw.init = &init;
		memset(&init, 0, sizeof(init));
		init.name = si5324_clkout_names[n];
		init.ops = &si5324_clkout_ops;
		init.flags = 0;
		init.flags |= CLK_SET_RATE_PARENT;
		init.parent_names = parent_names;
		init.num_parents = num_parents;
		clk = devm_clk_register(&client->dev, &drvdata->clkout[n].hw);
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "unable to register %s\n",
				init.name);
			ret = PTR_ERR(clk);
			goto err_clk;
		}
		/* refer to output clock in onecell */
		drvdata->onecell.clks[n] = clk;

		/* set initial clkout rate */
		if (pdata->clkout[n].rate != 0) {
			int ret;
			si5324_dbg("Initializing clkout%d for DT specified frequency %d Hz.\n", n, pdata->clkout[n].rate);
			ret = clk_set_rate(clk, pdata->clkout[n].rate);
			if (ret != 0) {
				dev_err(&client->dev, "Cannot set rate : %d\n",
					ret);
			}
		}
	}

	ret = of_clk_add_provider(client->dev.of_node, of_clk_src_onecell_get,
				  &drvdata->onecell);
	if (ret) {
		dev_err(&client->dev, "unable to add clk provider\n");
		goto err_clk;
	}
	si5324_dbg("Initialized Si5324.\n");

	return 0;

err_clk:
	if (!IS_ERR(drvdata->pxtal))
		clk_disable_unprepare(drvdata->pxtal);
	if (!IS_ERR(drvdata->pclkin1))
		clk_disable_unprepare(drvdata->pclkin1);
	if (!IS_ERR(drvdata->pclkin2))
		clk_disable_unprepare(drvdata->pclkin2);
	return ret;
}

static const struct i2c_device_id si5324_i2c_ids[] = {
	{ "si5324", 0 },
	{ "si5319", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si5324_i2c_ids);

static struct i2c_driver si5324_driver = {
	.driver = {
		.name = "si5324",
		.of_match_table = of_match_ptr(si5324_dt_ids),
	},
	.probe = si5324_i2c_probe,
	.id_table = si5324_i2c_ids,
};
module_i2c_driver(si5324_driver);

MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Silicon Labs Si5324 jitter attenuating clock multiplier driver");
MODULE_LICENSE("GPL");
