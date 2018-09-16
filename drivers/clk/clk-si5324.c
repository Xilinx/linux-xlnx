// SPDX-License-Identifier: GPL-2.0
/*
 * clk-si5324.c - Si5324 clock driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author:	Venkateshwar Rao G <vgannava.xilinx.com>
 *		Leon Woestenberg <leon@sidebranch.com>
 */

#include <asm/div64.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clk-si5324.h"
#include "si5324.h"
#include "si5324drv.h"

struct si5324_driver_data;

/**
 * struct si5324_parameters - si5324 core parameters
 *
 * @n1_hs_min:		Minimum high-speed n1 output divider
 * @n1_hs_max:		Maximum high-speed n1 output divider
 * @n1_hs:		n1 high-speed output divider
 * @nc1_ls_min:		Minimum low-speed clkout1 output divider
 * @nc1_ls_max:		Maximum low-speed clkout1 output divider
 * @nc1_ls:		Clkout1 low-speed output divider
 * @nc2_ls_min:		Minimum low-speed clkout2 output divider
 * @nc2_ls_max:		Maximum low-speed clkout2 output divider
 * @nc2_ls:		Clkout2 low-speed output divider
 * @n2_hs:		High-speed feedback divider
 * @n2_ls_min:		Minimum low-speed feedback divider
 * @n2_ls_max:		Maximum low-speed feedback divider
 * @n2_ls:		Low-speed feedback divider
 * @n31_min:		Minimum input divider for clk1
 * @n31_max:		Maximum input divider for clk1
 * @n31:		Input divider for clk1
 * @n32_min:		Minimum input divider for clk2
 * @n32_max:		Maximum input divider for clk2
 * @n32:		Input divider for clk2
 * @fin:		Input frequency
 * @fout:		Output frequency
 * @fosc:		Osc frequency
 * @best_delta_fout:	Delta out frequency
 * @best_fout:		Best output frequency
 * @best_n1_hs:		Best high speed output divider
 * @best_nc1_ls:	Best low speed clkout1 divider
 * @best_n2_hs:		Best high speed feedback divider
 * @best_n2_ls:		Best low speed feedback divider
 * @best_n3:		Best input clock divider
 * @valid: Validility
 */
struct si5324_parameters {
	u32 n1_hs_min;
	u32 n1_hs_max;
	u32 n1_hs;
	u32 nc1_ls_min;
	u32 nc1_ls_max;
	u32 nc1_ls;
	u32 nc2_ls_min;
	u32 nc2_ls_max;
	u32 nc2_ls;
	u32 n2_hs;
	u32 n2_ls_min;
	u32 n2_ls_max;
	u32 n2_ls;
	u32 n31_min;
	u32 n31_max;
	u32 n31;
	u32 n32_min;
	u32 n32_max;
	u32 n32;
	u64 fin;
	u64 fout;
	u64 fosc;
	u64 best_delta_fout;
	u64 best_fout;
	u32 best_n1_hs;
	u32 best_nc1_ls;
	u32 best_n2_hs;
	u32 best_n2_ls;
	u32 best_n3;
	int valid;
};

/**
 * struct si5324_hw_data - Clock parameters
 *
 * @hw:		Handle between common and hardware-specific interfaces
 * @drvdata:	Driver private data
 * @num:	Differential pair clock number
 */
struct si5324_hw_data {
	struct clk_hw hw;
	struct si5324_driver_data *drvdata;
	unsigned char num;
};

/**
 * struct si5324_driver_data - Driver parameters
 * @client:		I2C client pointer
 * @regmap:		Device's regmap
 * @onecell:		Clock onecell data
 * @params:		Device parameters
 * @pxtal:		Clock
 * @pxtal_name:		Clock name
 * @xtal:		Reference clock
 * @pclkin1:		Clock in 1
 * @pclkin1_name:	Clock in 1 name
 * @clkin1:		Differential input clock 1
 * @pclkin2:		Clock in 2
 * @pclkin2_name:	Clock in 2 name
 * @clkin2:		Differential input clock 2
 * @pll:		Pll clock
 * @clkout:		Output clock
 * @rate_clkout0:	Clock out 0 rate
 * @rate_clkout1:	Clock out 1 rate
 */
struct si5324_driver_data {
	struct i2c_client		*client;
	struct regmap			*regmap;
	struct clk_onecell_data		onecell;
	struct si5324_parameters	params;
	struct clk			*pxtal;
	const char			*pxtal_name;
	struct clk_hw			xtal;
	struct clk			*pclkin1;
	const char			*pclkin1_name;
	struct clk_hw			clkin1;
	struct clk			*pclkin2;
	const char			*pclkin2_name;
	struct clk_hw			clkin2;
	struct si5324_hw_data		pll;
	struct si5324_hw_data		*clkout;
	unsigned long			rate_clkout0;
	unsigned long			rate_clkout1;
};

static const char * const si5324_input_names[] = {
	"xtal", "clkin1", "clkin2"
};

static const char * const si5324_pll_name = "pll";

static const char * const si5324_clkout_names[] = {
	"clk0", "clk1"
};

enum si53xx_variant {
	si5319,
	si5324,
	si5328
};

static const char * const si53xx_variant_name[] = {
	"si5319", "si5324", "si5328"
};

/**
 * si5324_reg_read - Read a single si5324 register.
 *
 * @drvdata:	Device to read from.
 * @reg:	Register to read.
 *
 * This function reads data from a single register
 *
 * Return:	Data of the register on success, error number on failure
 */
static inline int
si5324_reg_read(struct si5324_driver_data *drvdata, u8 reg)
{
	u32 val;
	int ret;

	ret = regmap_read(drvdata->regmap, reg, &val);
	if (ret < 0) {
		dev_err(&drvdata->client->dev,
			"unable to read from reg%02x\n", reg);
		return ret;
	}

	return (u8)val;
}

/**
 * si5324_bulk_read - Read multiple si5324 registers
 *
 * @drvdata:	Device to read from
 * @reg:	First register to be read from
 * @count:	Number of registers
 * @buf:	Pointer to store read value
 *
 * This function reads from multiple registers which are in
 * sequential order
 *
 * Return:	Number of bytes read
 */
static inline int si5324_bulk_read(struct si5324_driver_data *drvdata,
				   u8 reg, u8 count, u8 *buf)
{
	return regmap_bulk_read(drvdata->regmap, reg, buf, count);
}

/**
 * si5324_reg_write - Write a single si5324 register.
 *
 * @drvdata:	Device to write to.
 * @reg:	Register to write to.
 * @val:	Value to write.
 *
 * This function writes into a single register
 *
 * Return:	Zero on success, a negative error number on failure.
 *
 */
static inline int si5324_reg_write(struct si5324_driver_data *drvdata,
				   u8 reg, u8 val)
{
	int ret = regmap_write(drvdata->regmap, reg, val);

	dev_dbg(&drvdata->client->dev, "%s 0x%02x @%02d\n", __func__,
		(int)val, (int)reg);
	return ret;
}

/**
 * si5324_bulk_write - Write into multiple si5324 registers
 *
 * @drvdata:	Device to write to
 * @reg:	First register
 * @count:	Number of registers
 * @buf:	Block of data to be written
 *
 * This function writes into multiple registers.
 *
 * Return:	Zero on success, a negative error number on failure.
 */
static inline int si5324_bulk_write(struct si5324_driver_data *drvdata,
				    u8 reg, u8 count, const u8 *buf)
{
	return regmap_raw_write(drvdata->regmap, reg, buf, count);
}

/**
 * si5324_set_bits - Set the value of a bitfield in a si5324 register
 *
 * @drvdata:	Device to write to.
 * @reg:	Register to write to.
 * @mask:	Mask of bits to set.
 * @val:	Value to set (unshifted)
 *
 * This function set particular bits in register
 *
 * Return:	Zero on success, a negative error number on failure.
 */
static inline int si5324_set_bits(struct si5324_driver_data *drvdata,
				  u8 reg, u8 mask, u8 val)
{
	return regmap_update_bits(drvdata->regmap, reg, mask, val);
}

/**
 * si5324_bulk_scatter_write - Write into multiple si5324 registers
 *
 * @drvdata:	Device to write to
 * @count:	Number of registers
 * @buf:	Register and data to write
 *
 * This function writes into multiple registers which are need not
 * to be in sequential order.
 *
 * Return:	Number of bytes written
 */
static inline int
si5324_bulk_scatter_write(struct si5324_driver_data *drvdata,
			  u8 count, const u8 *buf)
{
	int i;
	int ret = 0;

	for (i = 0; i < count; i++) {
		ret = si5324_reg_write(drvdata, buf[i * 2], buf[i * 2 + 1]);
		if (ret)
			return ret;
	}
	return ret;
}

/**
 * si5324_initialize - Initializes si5324 device
 *
 * @drvdata:	Device instance
 *
 * This function initializes si5324 with the following settings
 * Keep reset asserted for 20ms
 * 1. freerun mode
 * 2. Disable output clocks during calibration
 * 3. Clock selection mode : default value, manual
 * 4. output signal format : LVDS for clkout1, disable clkout2
 * 5. CS_CA pin in ignored
 * 6. Set lock time to 13.3ms
 * 7. Enables the fastlock.
 *
 * Return:	Zero on success, negative number on failure.
 */
static int si5324_initialize(struct si5324_driver_data *drvdata)
{
	int ret = 0;

	si5324_set_bits(drvdata, SI5324_RESET_CALIB,
			SI5324_RST_ALL, SI5324_RST_ALL);
	msleep(SI5324_RESET_DELAY_MS);
	si5324_set_bits(drvdata, SI5324_RESET_CALIB, SI5324_RST_ALL, 0);
	msleep(SI5324_RESET_DELAY_MS);

	ret = si5324_reg_read(drvdata, SI5324_CONTROL);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_CONTROL,
			 (ret | SI5324_CONTROL_FREE_RUN));

	ret = si5324_reg_read(drvdata, SI5324_CKSEL);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_CKSEL, (ret | SI5324_CKSEL_SQL_ICAL));
	si5324_reg_write(drvdata, SI3324_AUTOSEL, SI5324_AUTOSEL_DEF);
	si5324_reg_write(drvdata, SI5324_OUTPUT_SIGFMT,
			 SI5324_OUTPUT_SF1_DEFAULT);

	ret = si5324_reg_read(drvdata, SI5324_DSBL_CLKOUT);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_DSBL_CLKOUT,
			 (ret | SI5324_DSBL_CLKOUT2));
	ret = si5324_reg_read(drvdata, SI5324_POWERDOWN);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_POWERDOWN, (ret | SI5324_PD_CK2));
	si5324_reg_write(drvdata, SI5324_FOS_LOCKT, SI5324_FOS_DEFAULT);

	ret = si5324_reg_read(drvdata, SI5324_CK_ACTV_SEL);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_CK_ACTV_SEL, SI5324_CK_DEFAULT);
	ret = si5324_reg_read(drvdata, SI5324_FASTLOCK);
	if (ret < 0)
		return ret;

	si5324_reg_write(drvdata, SI5324_FASTLOCK, (ret | SI5324_FASTLOCK_EN));
	return 0;
}

/**
 * si5324_read_parameters - Reads clock divider parameters
 *
 * @drvdata:	Device to read from
 *
 * This function reads the clock divider parameters into driver structure.
 *
 * Following table gives the buffer index, register number and
 * register name with bit fields
 * 0 25 N1_HS[2:0]
 * 6 31 NC1_LS[19:16]
 * 7 32 NC1_LS[15:8]
 * 8 33 NC1_LS[7:0]
 * 9 34 NC2_LS[19:16]
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
	u8 buf[SI5324_PARAM_LEN];

	si5324_bulk_read(drvdata, SI5324_N1_HS, SI5324_N1_PARAM_LEN, &buf[0]);
	si5324_bulk_read(drvdata, SI5324_NC1_LS_H, SI5324_NC_PARAM_LEN,
			 &buf[6]);
	si5324_bulk_read(drvdata, SI5324_N2_HS_LS_H, SI5324_N2_PARAM_LEN,
			 &buf[15]);

	drvdata->params.n1_hs = (buf[0] >> SI5324_N1_HS_VAL_SHIFT);
	drvdata->params.n1_hs += 4;

	drvdata->params.nc1_ls = ((buf[6] & SI5324_DIV_LS_MASK) <<
				  SI5324_HSHIFT) | (buf[7] << SI5324_LSHIFT) |
				  buf[8];
	drvdata->params.nc1_ls += 1;
	drvdata->params.nc2_ls = ((buf[9] & SI5324_DIV_LS_MASK) <<
				  SI5324_HSHIFT) | (buf[10] << SI5324_LSHIFT) |
				  buf[11];
	drvdata->params.nc2_ls += 1;
	drvdata->params.n2_ls = ((buf[15] & SI5324_DIV_LS_MASK) <<
				 SI5324_HSHIFT) | (buf[16] << SI5324_LSHIFT) |
				 buf[17];
	drvdata->params.n2_ls += 1;
	drvdata->params.n2_hs = buf[15] >> SI5324_N2_HS_LS_H_VAL_SHIFT;
	drvdata->params.n2_hs += 4;
	drvdata->params.n31 = ((buf[18] & SI5324_DIV_LS_MASK) <<
				SI5324_HSHIFT) | (buf[19] << SI5324_LSHIFT) |
				buf[20];
	drvdata->params.n31 += 1;
	drvdata->params.n32 = ((buf[21] & SI5324_DIV_LS_MASK) <<
				SI5324_HSHIFT) | (buf[22] << SI5324_LSHIFT) |
				buf[23];
	drvdata->params.n32 += 1;
	drvdata->params.valid = 1;
}

static bool si5324_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return true;
}

/**
 * si5324_regmap_is_readable - Checks the register is readable or not
 *
 * @dev:	Registered device
 * @reg:	Register offset
 *
 * Checks the register is readable or not.
 *
 * Return:	True if the register is reabdle, False if it is not readable.
 */
static bool si5324_regmap_is_readable(struct device *dev, unsigned int reg)
{
	if ((reg > SI5324_POWERDOWN && reg < SI5324_FOS_LOCKT) ||
	    (reg > SI5324_N1_HS && reg < SI5324_NC1_LS_H) ||
	    (reg > SI5324_NC2_LS_L && reg < SI5324_N2_HS_LS_H) ||
	    (reg > SI5324_N32_CLKIN_L && reg < SI5324_FOS_CLKIN_RATE) ||
	    (reg > SI5324_FOS_CLKIN_RATE && reg < SI5324_PLL_ACTV_CLK) ||
	    reg > SI5324_SKEW2)
		return false;

	return true;
}

/**
 * si5324_regmap_is_writable - Checks the register is writable or not
 *
 * @dev:	Registered device
 * @reg:	Register offset
 *
 * Checks the register is writable or not.
 *
 * Return:	True if the register is writeable, False if it's not writeable.
 */
static bool si5324_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	if ((reg > SI5324_POWERDOWN && reg < SI5324_FOS_LOCKT) ||
	    (reg > SI5324_N1_HS && reg < SI5324_NC1_LS_H) ||
	    (reg > SI5324_NC2_LS_L && reg < SI5324_N2_HS_LS_H) ||
	    (reg > SI5324_N32_CLKIN_L && reg < SI5324_FOS_CLKIN_RATE) ||
	    (reg > SI5324_FOS_CLKIN_RATE && reg < SI5324_PLL_ACTV_CLK) ||
	    reg > SI5324_SKEW2 ||
	    (reg >= SI5324_PLL_ACTV_CLK && reg <= SI5324_CLKIN_LOL_STATUS) ||
	    (reg >= SI5324_PARTNO_H && reg <= SI5324_PARTNO_L))
		return false;

	return true;
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

static int si5324_xtal_prepare(struct clk_hw *hw)
{
	return 0;
}

static void si5324_xtal_unprepare(struct clk_hw *hw)
{
}

static const struct clk_ops si5324_xtal_ops = {
	.prepare = si5324_xtal_prepare,
	.unprepare = si5324_xtal_unprepare,
};

/**
 * si5324_clkin_prepare - Prepare the clkin
 *
 * @hw:		Handle between common and hardware-specific interfaces
 *
 * This function enables the particular clk
 *
 * Return:	Zero on success, a negative error number on failure.
 */
static int si5324_clkin_prepare(struct clk_hw *hw)
{
	int ret = 0;
	struct si5324_driver_data *drvdata;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	if (hwdata->num == SI5324_CLKIN1) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin1);
		ret = si5324_set_bits(drvdata, SI5324_CONTROL,
				      SI5324_CONTROL_FREE_RUN, 0);
		ret = si5324_set_bits(drvdata, SI5324_POWERDOWN, SI5324_PD_CK1 |
				      SI5324_PD_CK2, SI5324_PD_CK2);
	} else if (hwdata->num == SI5324_CLKIN2) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin2);
		ret = si5324_set_bits(drvdata, SI5324_CONTROL,
				      SI5324_CONTROL_FREE_RUN, 0);
		ret = si5324_set_bits(drvdata, SI5324_POWERDOWN, SI5324_PD_CK1 |
				      SI5324_PD_CK2, SI5324_PD_CK1);
	}

	return ret;
}

/**
 * si5324_clkin_unprepare - Unprepare the clkin
 *
 * @hw:		Clock hardware
 *
 * This function enables the particular clk.
 */
static void si5324_clkin_unprepare(struct clk_hw *hw)
{
	struct si5324_driver_data *drvdata;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	if (hwdata->num == SI5324_CLKIN1) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin1);
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
	} else if (hwdata->num == SI5324_CLKIN2) {
		drvdata = container_of(hw, struct si5324_driver_data, clkin2);
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
	}
}

static unsigned long si5324_clkin_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return 0;
}

static const struct clk_ops si5324_clkin_ops = {
	.prepare = si5324_clkin_prepare,
	.unprepare = si5324_clkin_unprepare,
	.recalc_rate = si5324_clkin_recalc_rate,
};

static int si5324_pll_reparent(struct si5324_driver_data *drvdata,
			       int num, enum si5324_pll_src parent)
{
	if (parent == SI5324_PLL_SRC_XTAL) {
		si5324_set_bits(drvdata, SI5324_CONTROL,
				SI5324_CONTROL_FREE_RUN,
				SI5324_CONTROL_FREE_RUN);
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
		si5324_set_bits(drvdata, SI5324_CKSEL,
				SI5324_CK_SEL << SI5324_CKSEL_SHIFT,
				1 << SI5324_CKSEL_SHIFT);
	} else if (parent == SI5324_PLL_SRC_CLKIN1) {
		si5324_set_bits(drvdata, SI5324_CONTROL,
				SI5324_CONTROL_FREE_RUN, 0);
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK2);
		si5324_set_bits(drvdata, SI5324_CKSEL,
				SI5324_CK_SEL << SI5324_CKSEL_SHIFT, 0);
	} else if (parent == SI5324_PLL_SRC_CLKIN2) {
		si5324_set_bits(drvdata, SI5324_CONTROL,
				SI5324_CONTROL_FREE_RUN, 0);
		si5324_set_bits(drvdata, SI5324_POWERDOWN,
				SI5324_PD_CK1 | SI5324_PD_CK2, SI5324_PD_CK1);
		si5324_set_bits(drvdata, SI5324_CKSEL,
				SI5324_CK_SEL << SI5324_CKSEL_SHIFT,
				1 << SI5324_CKSEL_SHIFT);
	}

	return 0;
}

static unsigned char si5324_pll_get_parent(struct clk_hw *hw)
{
	return 0;
}

/**
 * si5324_pll_set_parent - Set parent of clock
 *
 * @hw:		Handle between common and hardware-specific interfaces
 * @index:	Parent index
 *
 * This function sets the paraent of clock.
 *
 * Return:	0 on success, negative error number on failure
 */
static int si5324_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);
	enum si5324_pll_src parent;

	if (index == SI5324_SRC_XTAL)
		parent = SI5324_PLL_SRC_XTAL;
	else if (index == SI5324_SRC_CLKIN1)
		parent = SI5324_PLL_SRC_CLKIN1;
	else if (index == SI5324_SRC_CLKIN2)
		parent = SI5324_PLL_SRC_CLKIN2;
	else
		return -EINVAL;

	return si5324_pll_reparent(hwdata->drvdata, hwdata->num, parent);
}

/**
 * si5324_pll_recalc_rate - Recalculate clock frequency
 *
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 *
 * This function recalculate clock frequency.
 *
 * Return:		Current clock frequency
 */
static unsigned long si5324_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	unsigned long rate;
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	if (!hwdata->drvdata->params.valid)
		si5324_read_parameters(hwdata->drvdata);
	WARN_ON(!hwdata->drvdata->params.valid);

	rate = parent_rate * hwdata->drvdata->params.n2_ls *
		hwdata->drvdata->params.n2_hs;

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
	return rate;
}

static int si5324_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	return 0;
}

static const struct clk_ops si5324_pll_ops = {
	.set_parent = si5324_pll_set_parent,
	.get_parent = si5324_pll_get_parent,
	.recalc_rate = si5324_pll_recalc_rate,
	.round_rate = si5324_pll_round_rate,
	.set_rate = si5324_pll_set_rate,
};

static int si5324_clkout_set_drive_strength(
	struct si5324_driver_data *drvdata, int num,
	enum si5324_drive_strength drive)
{
	return 0;
}

static int si5324_clkout_prepare(struct clk_hw *hw)
{
	return 0;
}

static void si5324_clkout_unprepare(struct clk_hw *hw)
{
}

static unsigned long si5324_clkout_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	unsigned long rate;

	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	rate = hwdata->drvdata->rate_clkout0;

	return rate;
}

/**
 * si5324_clkout_round_rate - selects the closest value to requested one.
 *
 * @hw:			Handle between common and hardware-specific interfaces
 * @rate:		Clock rate
 * @parent_rate:	Parent clock rate
 *
 * This function selects the rate closest to the requested one.
 *
 * Return:		Clock rate on success, negative error number on failure
 */
static long si5324_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	u32 ncn_ls, n2_ls, n3n, actual_rate;
	u8 n1_hs, n2_hs, bwsel;
	int ret;

	ret = si5324_calcfreqsettings(SI5324_REF_CLOCK, rate, &actual_rate,
				      &n1_hs, &ncn_ls, &n2_hs, &n2_ls, &n3n,
				      &bwsel);
	if (ret < 0)
		return ret;

	return actual_rate;
}

static int si5324_clkout_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct si5324_hw_data *hwdata =
		container_of(hw, struct si5324_hw_data, hw);

	u32 ncn_ls, n2_ls, n3n, actual_rate;
	u8 n1_hs, n2_hs, bwsel, buf[SI5324_OUT_REGS * 2];
	int i, ret, rc;

	ret = si5324_calcfreqsettings(SI5324_REF_CLOCK, rate, &actual_rate,
				      &n1_hs, &ncn_ls, &n2_hs, &n2_ls, &n3n,
				      &bwsel);
	if (ret < 0)
		return ret;

	hwdata->drvdata->rate_clkout0 = rate;
	i = 0;

	/* Enable Free running mode */
	buf[i] = SI5324_CONTROL;
	buf[i + 1] = SI5324_FREE_RUN_EN;
	i += 2;

	/* Loop bandwidth */
	buf[i] = SI5324_BWSEL;
	buf[i + 1] = (bwsel << SI5324_BWSEL_SHIFT) | SI5324_BWSEL_DEF_VAL;
	i += 2;

	/* Enable reference clock 2 in free running mode */
	buf[i] = SI5324_POWERDOWN;
	/* Enable input clock 2, Disable input clock 1 */
	buf[i + 1] = SI5324_PD_CK1_DIS;
	i += 2;

	/* N1_HS */
	buf[i] = SI5324_N1_HS;
	buf[i + 1] = n1_hs << SI5324_N1_HS_VAL_SHIFT;
	i += 2;

	/* NC1_LS */
	buf[i] = SI5324_NC1_LS_H;
	buf[i + 1] = (u8)((ncn_ls & 0x000F0000) >> 16);
	buf[i + 2] = SI5324_NC1_LS_M;
	buf[i + 3] = (u8)((ncn_ls & 0x0000FF00) >> 8);
	buf[i + 4] = SI5324_NC1_LS_L;
	buf[i + 5] = (u8)(ncn_ls & 0x000000FF);
	i += 6;

	/* N2_HS and N2_LS */
	buf[i] = SI5324_N2_HS_LS_H;
	buf[i + 1] = (n2_hs << SI5324_N2_HS_LS_H_VAL_SHIFT);
	buf[i + 1] |= (u8)((n2_ls & 0x000F0000) >> 16);
	buf[i + 2] = SI5324_N2_LS_H;
	buf[i + 3] = (u8)((n2_ls & 0x0000FF00) >> 8);
	buf[i + 4] = SI5324_N2_LS_L;
	buf[i + 5] = (u8)(n2_ls & 0x000000FF);
	i += 6;

	/* N32 (CLKIN2 or XTAL in FREERUNNING mode) */
	buf[i] = SI5324_N32_CLKIN_H;
	buf[i + 2] = SI5324_N32_CLKIN_M;
	buf[i + 4] = SI5324_N32_CLKIN_L;
	buf[i + 1] = (u8)((n3n & 0x00070000) >> 16);
	buf[i + 3] = (u8)((n3n & 0x0000FF00) >> 8);
	buf[i + 5] = (u8)(n3n & 0x000000FF);
	i += 6;

	/* Start calibration */
	buf[i] = SI5324_RESET_CALIB;
	buf[i + 1] = SI5324_CALIB_EN;
	i += 2;

	hwdata->drvdata->params.valid = 0;
	rc = si5324_bulk_scatter_write(hwdata->drvdata, SI5324_OUT_REGS, buf);

	return rc;
}

static const struct clk_ops si5324_clkout_ops = {
	.prepare = si5324_clkout_prepare,
	.unprepare = si5324_clkout_unprepare,
	.recalc_rate = si5324_clkout_recalc_rate,
	.round_rate = si5324_clkout_round_rate,
	.set_rate = si5324_clkout_set_rate,
};

static const struct of_device_id si5324_dt_ids[] = {
	{ .compatible = "silabs,si5319" },
	{ .compatible = "silabs,si5324" },
	{ .compatible = "silabs,si5328" },
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

	if (!np)
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
			dev_dbg(&client->dev,
				"using clkin1 as parent for pll\n");
			pdata->pll_src = SI5324_PLL_SRC_CLKIN1;
			break;
		case 2:
			dev_dbg(&client->dev,
				"using clkin2 as parent for pll\n");
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

		if (!of_property_read_u32(child, "clock-frequency", &val)) {
			dev_dbg(&client->dev, "clock-frequency = %u\n", val);
			pdata->clkout[num].rate = val;
		} else {
			dev_err(&client->dev,
				"missing clock-frequency property of %s\n",
				child->name);
			goto put_child;
		}
	}
	client->dev.platform_data = pdata;

	return 0;
put_child:
	of_node_put(child);
	return -EINVAL;
}

static u8 instance;

static int si5324_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct si5324_platform_data *pdata;
	struct si5324_driver_data *drvdata;
	struct clk_init_data init;
	struct clk *clk;
	const char *parent_names[3];
	char inst_names[NUM_NAME_IDS][MAX_NAME_LEN];
	u8 num_parents, num_clocks;
	int ret, n;
	enum si53xx_variant variant = id->driver_data;

	if (variant > si5328) {
		dev_err(&client->dev, "si53xx device not present\n");
		return -ENODEV;
	}

	dev_info(&client->dev, "%s probed\n", si53xx_variant_name[variant]);
	ret = si5324_dt_parse(client);
	if (ret)
		return ret;

	pdata = client->dev.platform_data;
	if (!pdata)
		return -EINVAL;

	drvdata = devm_kzalloc(&client->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->client = client;
	drvdata->pxtal = devm_clk_get(&client->dev, "xtal");
	drvdata->pclkin1 = devm_clk_get(&client->dev, "clkin1");
	drvdata->pclkin2 = devm_clk_get(&client->dev, "clkin2");

	if (PTR_ERR(drvdata->pxtal) == -EPROBE_DEFER ||
	    PTR_ERR(drvdata->pclkin1) == -EPROBE_DEFER ||
	    PTR_ERR(drvdata->pclkin2) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	drvdata->regmap = devm_regmap_init_i2c(client, &si5324_regmap_config);
	if (IS_ERR(drvdata->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(drvdata->regmap);
	}

	i2c_set_clientdata(client, drvdata);
	si5324_initialize(drvdata);

	/* setup input clock configuration */
	ret = si5324_pll_reparent(drvdata, 0, pdata->pll_src);
	if (ret) {
		dev_err(&client->dev,
			"failed to reparent pll to %d\n",
			pdata->pll_src);
		return ret;
	}

	for (n = 0; n < SI5324_MAX_CLKOUTS; n++) {
		ret = si5324_clkout_set_drive_strength(drvdata, n,
						       pdata->clkout[n].drive);
		if (ret) {
			dev_err(&client->dev,
				"failed set drive strength of clkout%d to %d\n",
				n, pdata->clkout[n].drive);
			return ret;
		}
	}

	if (!IS_ERR(drvdata->pxtal))
		clk_prepare_enable(drvdata->pxtal);
	if (!IS_ERR(drvdata->pclkin1))
		clk_prepare_enable(drvdata->pclkin1);
	if (!IS_ERR(drvdata->pclkin2))
		clk_prepare_enable(drvdata->pclkin2);

	/* create instance names by appending instance id */
	for (n = 0; n < SI5324_SRC_CLKS; n++) {
		sprintf(inst_names[n], "%s_%d", si5324_input_names[n],
			instance);
	}
	sprintf(inst_names[3], "%s_%d", si5324_pll_name, instance);
	for (n = 0; n < SI5324_MAX_CLKOUTS; n++) {
		sprintf(inst_names[n + 4], "%s_%d", si5324_clkout_names[n],
			instance);
	}

	/* register xtal input clock gate */
	memset(&init, 0, sizeof(init));
	init.name = inst_names[0];
	init.ops = &si5324_xtal_ops;
	init.flags = 0;

	if (!IS_ERR(drvdata->pxtal)) {
		drvdata->pxtal_name = __clk_get_name(drvdata->pxtal);
		init.parent_names = &drvdata->pxtal_name;
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
	init.name = inst_names[1];
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
	init.name = inst_names[2];
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
	num_parents = SI5324_SRC_CLKS;
	parent_names[0] = inst_names[0];
	parent_names[1] = inst_names[1];
	parent_names[2] = inst_names[2];

	/* register PLL */
	drvdata->pll.drvdata = drvdata;
	drvdata->pll.hw.init = &init;
	memset(&init, 0, sizeof(init));
	init.name = inst_names[3];
	init.ops = &si5324_pll_ops;
	init.flags = 0;
	init.flags |= CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk = devm_clk_register(&client->dev, &drvdata->pll.hw);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register %s\n", init.name);
		ret = PTR_ERR(clk);
		goto err_clk;
	}

	/* register clk out divider */
	num_clocks = 2;
	num_parents = 1;
	parent_names[0] = inst_names[3];

	drvdata->clkout = devm_kzalloc(&client->dev, num_clocks *
				       sizeof(*drvdata->clkout), GFP_KERNEL);

	drvdata->onecell.clk_num = num_clocks;
	drvdata->onecell.clks = devm_kzalloc(&client->dev,
					     num_clocks *
					     sizeof(*drvdata->onecell.clks),
					     GFP_KERNEL);

	if (WARN_ON(!drvdata->clkout) || !drvdata->onecell.clks) {
		ret = -ENOMEM;
		goto err_clk;
	}

	for (n = 0; n < num_clocks; n++) {
		drvdata->clkout[n].num = n;
		drvdata->clkout[n].drvdata = drvdata;
		drvdata->clkout[n].hw.init = &init;
		memset(&init, 0, sizeof(init));
		init.name = inst_names[4 + n];
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

	dev_info(&client->dev, "%s probe successful\n",
		 si53xx_variant_name[variant]);
	instance++;
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

static int si5324_i2c_remove(struct i2c_client *client)
{
	of_clk_del_provider(client->dev.of_node);
	return 0;
}

static const struct i2c_device_id si5324_i2c_ids[] = {
	{ "si5319", si5319 },
	{ "si5324", si5324 },
	{ "si5328", si5328 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si5324_i2c_ids);

static struct i2c_driver si5324_driver = {
	.driver = {
		.name = "si5324",
		.of_match_table = of_match_ptr(si5324_dt_ids),
	},
	.probe = si5324_i2c_probe,
	.remove = si5324_i2c_remove,
	.id_table = si5324_i2c_ids,
};
module_i2c_driver(si5324_driver);

MODULE_AUTHOR("Venkateshwar Rao G <vgannava@xilinx.com>");
MODULE_DESCRIPTION("Silicon Labs 5319/5324/5328 clock driver");
MODULE_LICENSE("GPL v2");
