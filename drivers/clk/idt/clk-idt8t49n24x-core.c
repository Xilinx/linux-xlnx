// SPDX-License-Identifier: GPL-2.0
/* clk-idt8t49n24x-core.c - Program 8T49N24x settings via I2C (common code)
 *
 * Copyright (C) 2018, Integrated Device Technology, Inc. <david.cater@idt.com>
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 * This program is distributed "AS IS" and  WITHOUT ANY WARRANTY;
 * including the implied warranties of MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, or NON-INFRINGEMENT.
 */

#include <linux/i2c.h>
#include <linux/regmap.h>

#include "clk-idt8t49n24x-core.h"

/*
 * In Timing Commander, Q0 is changed from 25MHz to Q0 75MHz, the following
 * changes occur:
 *
 * 2 bytes change in EEPROM data string.
 *
 * DSM_INT R0025[0],R0026[7:0] : 35 => 30
 * NS2_Q0 R0040[7:0],R0041[7:0] : 14 => 4
 *
 * In EEPROM
 * 1. R0026
 * 2. R0041
 *
 * Note that VCO_Frequency (metadata) also changed (3500 =>3000).
 * This reflects a change to DSM_INT.
 *
 * Note that the Timing Commander code has workarounds in the workflow scripts
 * to handle dividers for the 8T49N241 (because the development of that GUI
 * predates chip override functionality). That affects NS1_Qx (x in 1-3)
 * and NS2_Qx. NS1_Qx contains the upper bits of NS_Qx, and NS2_Qx contains
 * the lower bits. That is NOT the case for Q0, though. In that case NS1_Q0
 * is the 1st stage output divider (/5, /6, /4) and NS2_Q0 is the 16-bit
 * second stage (with actual divide being twice the value stored in the
 * register).
 *
 * NS1_Q0 R003F[1:0]
 */

#define IDT24x_VCO_MIN			2999997000u
#define IDT24x_VCO_MAX			4000004000u
#define IDT24x_VCO_OPT			3500000000u
#define IDT24x_MIN_INT_DIVIDER		6
#define IDT24x_MIN_NS1			4
#define IDT24x_MAX_NS1			6

static u8 q0_ns1_options[3] = { 5, 6, 4 };

/**
 * bits_to_shift - num bits to shift given specified mask
 * @mask:	32-bit word input to count zero bits on right
 *
 * Given a bit mask indicating where a value will be stored in
 * a register, return the number of bits you need to shift the value
 * before ORing it into the register value.
 *
 * Return: number of bits to shift
 */
int bits_to_shift(unsigned int mask)
{
	/* the number of zero bits on the right */
	unsigned int c = 32;

	mask &= ~mask + 1;
	if (mask)
		c--;
	if (mask & 0x0000FFFF)
		c -= 16;
	if (mask & 0x00FF00FF)
		c -= 8;
	if (mask & 0x0F0F0F0F)
		c -= 4;
	if (mask & 0x33333333)
		c -= 2;
	if (mask & 0x55555555)
		c -= 1;
	return c;
}

/*
 * TODO: Consider replacing this with regmap_multi_reg_write, which
 * supports introducing a delay after each write. Experiment to see if
 * the writes succeed consistently when using that API.
 */
static int regmap_bulk_write_with_retry(
	struct regmap *map, unsigned int offset, u8 val[],
	int val_count, int max_attempts)
{
	int err = 0;
	int count = 1;

	do {
		err = regmap_bulk_write(map, offset, val, val_count);
		if (err == 0)
			return 0;

		usleep_range(100, 200);
	} while (count++ <= max_attempts);
	return err;
}

static int regmap_write_with_retry(
	struct regmap *map, unsigned int offset, unsigned int val,
	int max_attempts)
{
	int err = 0;
	int count = 1;

	do {
		err = regmap_write(map, offset, val);
		if (err == 0)
			return 0;
		usleep_range(100, 200);
	} while (count++ <= max_attempts);
	return err;
}

/*
 * TODO: Consider using regmap_multi_reg_write instead. Explore
 * use of regmap to configure WRITE_BLOCK_SIZE, and using the delay
 * mechanism in regmap_multi_reg_write instead of retrying multiple
 * times (regmap_bulk_write_with_retry).
 */
int i2cwritebulk(
	struct i2c_client *client, struct regmap *map,
	unsigned int reg, u8 val[], size_t val_count)
{
	char dbg[128];
	u8 block[WRITE_BLOCK_SIZE];
	unsigned int block_offset = reg;
	int x;
	int err = 0;
	int currentOffset = 0;

	dev_dbg(&client->dev, "I2C->0x%04x : [hex] . First byte: %02x, Second byte: %02x",
		reg, reg >> 8, reg & 0xFF);
	dbg[0] = 0;

	for (x = 0; x < val_count; x++) {
		char data[4];

		block[currentOffset++] = val[x];
		sprintf(data, "%02x ", val[x]);
		strcat(dbg, data);
		if (x > 0 && (x + 1) % WRITE_BLOCK_SIZE == 0) {
			dev_dbg(&client->dev, "%s", dbg);
			dbg[0] = '\0';
			sprintf(dbg,
				"(loop) calling regmap_bulk_write @ 0x%04x [%d bytes]",
				block_offset, WRITE_BLOCK_SIZE);
			dev_dbg(&client->dev, "%s", dbg);
			dbg[0] = '\0';
			err = regmap_bulk_write_with_retry(
				map, block_offset, block, WRITE_BLOCK_SIZE, 5);
			if (err != 0)
				break;
			block_offset += WRITE_BLOCK_SIZE;
			currentOffset = 0;
		}
	}
	if (err == 0 && currentOffset > 0) {
		dev_dbg(&client->dev, "%s", dbg);
		dev_dbg(&client->dev, "(final) calling regmap_bulk_write @ 0x%04x [%d bytes]",
			block_offset, currentOffset);
		err = regmap_bulk_write_with_retry(
			map, block_offset, block, currentOffset, 5);
	}

	return err;
}

static int i2cwrite(
	struct i2c_client *client, struct regmap *map,
	unsigned int reg, unsigned int val)
{
	int err;

	dev_dbg(&client->dev, "I2C->0x%x : [hex] %x", reg, val);
	err = regmap_write_with_retry(map, reg, val, 5);
	usleep_range(100, 200);
	return err;
}

static int i2cwritewithmask(
	struct i2c_client *client, struct regmap *map, unsigned int reg,
	u8 val, u8 original, u8 mask)
{
	return i2cwrite(client, map, reg,
		((val << bits_to_shift(mask)) & mask) | (original & ~mask));
}

int idt24x_get_offsets(
	u8 output_num,
	struct clk_register_offsets *offsets)
{
	switch (output_num) {
	case 0:
		offsets->oe_offset = IDT24x_REG_OUTEN;
		offsets->oe_mask = IDT24x_REG_OUTEN0_MASK;
		offsets->dis_mask = IDT24x_REG_Q0_DIS_MASK;
		offsets->ns1_offset = IDT24x_REG_NS1_Q0;
		offsets->ns1_offset_mask = IDT24x_REG_NS1_Q0_MASK;
		offsets->ns2_15_8_offset = IDT24x_REG_NS2_Q0_15_8;
		offsets->ns2_7_0_offset = IDT24x_REG_NS2_Q0_7_0;
		break;
	case 1:
		offsets->oe_offset = IDT24x_REG_OUTEN;
		offsets->oe_mask = IDT24x_REG_OUTEN1_MASK;
		offsets->dis_mask = IDT24x_REG_Q1_DIS_MASK;
		offsets->n_17_16_offset = IDT24x_REG_N_Q1_17_16;
		offsets->n_17_16_mask = IDT24x_REG_N_Q1_17_16_MASK;
		offsets->n_15_8_offset = IDT24x_REG_N_Q1_15_8;
		offsets->n_7_0_offset = IDT24x_REG_N_Q1_7_0;
		offsets->nfrac_27_24_offset = IDT24x_REG_NFRAC_Q1_27_24;
		offsets->nfrac_27_24_mask =
			IDT24x_REG_NFRAC_Q1_27_24_MASK;
		offsets->nfrac_23_16_offset = IDT24x_REG_NFRAC_Q1_23_16;
		offsets->nfrac_15_8_offset = IDT24x_REG_NFRAC_Q1_15_8;
		offsets->nfrac_7_0_offset = IDT24x_REG_NFRAC_Q1_7_0;
		break;
	case 2:
		offsets->oe_offset = IDT24x_REG_OUTEN;
		offsets->oe_mask = IDT24x_REG_OUTEN2_MASK;
		offsets->dis_mask = IDT24x_REG_Q2_DIS_MASK;
		offsets->n_17_16_offset = IDT24x_REG_N_Q2_17_16;
		offsets->n_17_16_mask = IDT24x_REG_N_Q2_17_16_MASK;
		offsets->n_15_8_offset = IDT24x_REG_N_Q2_15_8;
		offsets->n_7_0_offset = IDT24x_REG_N_Q2_7_0;
		offsets->nfrac_27_24_offset = IDT24x_REG_NFRAC_Q2_27_24;
		offsets->nfrac_27_24_mask =
			IDT24x_REG_NFRAC_Q2_27_24_MASK;
		offsets->nfrac_23_16_offset = IDT24x_REG_NFRAC_Q2_23_16;
		offsets->nfrac_15_8_offset = IDT24x_REG_NFRAC_Q2_15_8;
		offsets->nfrac_7_0_offset = IDT24x_REG_NFRAC_Q2_7_0;
		break;
	case 3:
		offsets->oe_offset = IDT24x_REG_OUTEN;
		offsets->oe_mask = IDT24x_REG_OUTEN3_MASK;
		offsets->dis_mask = IDT24x_REG_Q3_DIS_MASK;
		offsets->n_17_16_offset = IDT24x_REG_N_Q3_17_16;
		offsets->n_17_16_mask = IDT24x_REG_N_Q3_17_16_MASK;
		offsets->n_15_8_offset = IDT24x_REG_N_Q3_15_8;
		offsets->n_7_0_offset = IDT24x_REG_N_Q3_7_0;
		offsets->nfrac_27_24_offset = IDT24x_REG_NFRAC_Q3_27_24;
		offsets->nfrac_27_24_mask =
			IDT24x_REG_NFRAC_Q3_27_24_MASK;
		offsets->nfrac_23_16_offset = IDT24x_REG_NFRAC_Q3_23_16;
		offsets->nfrac_15_8_offset = IDT24x_REG_NFRAC_Q3_15_8;
		offsets->nfrac_7_0_offset = IDT24x_REG_NFRAC_Q3_7_0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * idt24x_calc_div_q0 - Calculate dividers and VCO freq to generate
 *		the specified Q0 frequency.
 * @chip:	Device data structure. contains all requested frequencies
 *		for all outputs.
 *
 * The actual output divider is ns1 * ns2 * 2. fOutput = fVCO / (ns1 * ns2 * 2)
 *
 * The options for ns1 (when the source is the VCO) are 4,5,6. ns2 is a
 * 16-bit value.
 *
 * chip->divs: structure for specifying ns1/ns2 values. If 0 after this
 * function, Q0 is not requested
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int idt24x_calc_div_q0(struct clk_idt24x_chip *chip)
{
	u8 x;
	u32 min_div, max_div, best_vco = 0;
	u16 min_ns2, max_ns2;
	bool is_lower_vco = false;

	chip->divs.ns1_q0 = 0;
	chip->divs.ns2_q0 = 0;

	if (chip->clk[0].requested == 0)
		return 0;

	min_div = div64_u64(
		(u64)IDT24x_VCO_MIN, chip->clk[0].requested * 2) * 2;
	max_div = div64_u64(
		(u64)IDT24x_VCO_MAX, chip->clk[0].requested * 2) * 2;

	dev_dbg(&chip->i2c_client->dev,
		"%s. requested: %u, min_div: %u, max_div: %u",
		__func__, chip->clk[0].requested, min_div, max_div);

	min_ns2 = div64_u64((u64)min_div, IDT24x_MAX_NS1 * 2);
	max_ns2 = div64_u64((u64)max_div, IDT24x_MIN_NS1 * 2);

	dev_dbg(&chip->i2c_client->dev,
		"%s. min_ns2: %u, max_ns2: %u", __func__, min_ns2, max_ns2);

	for (x = 0; x < ARRAY_SIZE(q0_ns1_options); x++) {
		u16 y = min_ns2;

		while (y <= max_ns2) {
			u32 actual_div = q0_ns1_options[x] * y * 2;
			u32 current_vco = actual_div *
				chip->clk[0].requested;

			if (current_vco < IDT24x_VCO_MIN)
				dev_dbg(&chip->i2c_client->dev,
					"%s. ignore div: (ns1=%u * ns2=%u * 2 * %u) == %u < %u",
					__func__, q0_ns1_options[x], y,
					chip->clk[0].requested,
					current_vco, IDT24x_VCO_MIN);
			else if (current_vco > IDT24x_VCO_MAX) {
				dev_dbg(&chip->i2c_client->dev,
					"%s. ignore div: (ns1=%u * ns2=%u * 2 * %u) == %u > %u. EXIT LOOP.",
					__func__, q0_ns1_options[x], y,
					chip->clk[0].requested,
					current_vco, IDT24x_VCO_MAX);
				y = max_ns2;
			} else {
				bool use = false;

				dev_dbg(&chip->i2c_client->dev,
					"%s. contender: (ns1=%u * ns2=%u * 2 * %u) == %u [in range]",
					__func__, q0_ns1_options[x], y,
					chip->clk[0].requested,
					current_vco);
				if (current_vco <= IDT24x_VCO_OPT) {
					if (current_vco > best_vco ||
					    !is_lower_vco) {
						is_lower_vco = true;
						use = true;
					}
				} else if (!is_lower_vco &&
					   current_vco > best_vco)
					use = true;
				if (use) {
					chip->divs.ns1_q0 = x;
					chip->divs.ns2_q0 = y;
					best_vco = current_vco;
				}
			}
			y++;
		}
	}

	dev_dbg(&chip->i2c_client->dev,
		"%s. best: (ns1=%u [/%u] * ns2=%u * 2 * %u) == %u",
		__func__, chip->divs.ns1_q0, q0_ns1_options[chip->divs.ns1_q0],
		chip->divs.ns2_q0, chip->clk[0].requested, best_vco);
	return 0;
}

/**
 * idt24x_calc_divs - Calculate dividers to generate the specified frequency.
 * @chip:	Device data structure. contains all requested frequencies
 *		for all outputs.
 *
 * Calculate the clock dividers (dsmint, dsmfrac for vco; ns1/ns2 for q0,
 * n/nfrac for q1-3) for a given target frequency.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int idt24x_calc_divs(struct clk_idt24x_chip *chip)
{
	u32 vco = 0;
	int result;

	result = idt24x_calc_div_q0(chip);
	if (result < 0)
		return result;

	dev_dbg(&chip->i2c_client->dev,
		"%s: after idt24x_calc_div_q0. ns1: %u [/%u], ns2: %u",
		__func__, chip->divs.ns1_q0, q0_ns1_options[chip->divs.ns1_q0],
		chip->divs.ns2_q0);

	chip->divs.dsmint = 0;
	chip->divs.dsmfrac = 0;

	if (chip->clk[0].requested > 0) {
		/* Q0 is in use and is governing the actual VCO freq */
		vco = q0_ns1_options[chip->divs.ns1_q0] * chip->divs.ns2_q0 *
			2 * chip->clk[0].requested;
	} else {
		u32 freq = 0;
		u32 walk;
		u32 min_div, max_div;
		bool is_lower_vco = false;

		/*
		 * Q0 is not in use. Use the first requested (fractional)
		 * output frequency as the one controlling the VCO.
		 */
		for (walk = 1; walk < NUM_OUTPUTS; walk++) {
			if (chip->clk[walk].requested != 0) {
				freq = chip->clk[walk].requested;
				break;
			}
		}

		if (freq == 0) {
			dev_err(&chip->i2c_client->dev,
				"%s: NO FREQUENCIES SPECIFIED", __func__);
			return -EINVAL;
		}

		/*
		 * First, determine the min/max div for the output frequency.
		 */
		min_div = IDT24x_MIN_INT_DIVIDER;
		max_div = div64_u64((u64)IDT24x_VCO_MAX, freq * 2) * 2;

		dev_dbg(&chip->i2c_client->dev,
			"%s: calc_divs for fractional output. freq: %u, min_div: %u, max_div: %u",
			__func__, freq, min_div, max_div);

		walk = min_div;

		while (walk <= max_div) {
			u32 current_vco = freq * walk;

			dev_dbg(&chip->i2c_client->dev,
				"%s: calc_divs for fractional output. walk: %u, freq: %u, vco: %u",
				__func__, walk, freq, vco);
			if (current_vco >= IDT24x_VCO_MIN &&
			    vco <= IDT24x_VCO_MAX) {
				if (current_vco <= IDT24x_VCO_OPT) {
					if (current_vco > vco ||
					    !is_lower_vco) {
						is_lower_vco = true;
						vco = current_vco;
					}
				} else if (!is_lower_vco && current_vco > vco) {
					vco = current_vco;
				}
			}
			/* Divider must be even. */
			walk += 2;
		}
	}

	if (vco != 0) {
		u32 pfd;
		u64 rem;
		int x;

		/* Setup dividers for outputs with fractional dividers. */
		for (x = 1; x < NUM_OUTPUTS; x++) {
			if (chip->clk[x].requested != 0) {
				/*
				 * The value written to the chip is half
				 * the calculated divider.
				 */
				chip->divs.nint[x - 1] = div64_u64_rem(
					(u64)vco,
					chip->clk[x].requested * 2,
					&rem);
				chip->divs.nfrac[x - 1] = div64_u64(
					rem * 1 << 28,
					chip->clk[x].requested * 2);
				dev_dbg(&chip->i2c_client->dev,
					"%s: div to get Q%i freq %u from vco %u: int part: %u, rem: %llu, frac part: %u",
					__func__, x,
					chip->clk[x].requested,
					vco, chip->divs.nint[x - 1], rem,
					chip->divs.nfrac[x - 1]);
			}
		}

		/* Calculate freq for pfd */
		pfd = chip->input_clk_freq * (chip->doubler_disabled ? 1 : 2);

		/*
		 * Calculate dsmint & dsmfrac:
		 * -----------------------------
		 * dsm = float(vco)/float(pfd)
		 * dsmfrac = dsm-floor(dsm) * 2^21
		 * rem = vco % pfd
		 * therefore:
		 * dsmfrac = (rem * 2^21)/pfd
		 */
		chip->divs.dsmint = div64_u64_rem(vco, pfd, &rem);
		chip->divs.dsmfrac = div64_u64(rem * 1 << 21, pfd);

		dev_dbg(&chip->i2c_client->dev,
			"%s: vco: %u, pfd: %u, dsmint: %u, dsmfrac: %u, rem: %llu",
			__func__, vco, pfd, chip->divs.dsmint,
			chip->divs.dsmfrac, rem);
	} else {
		dev_err(&chip->i2c_client->dev,
			"%s: no integer divider in range found. NOT SUPPORTED.",
			__func__);
		return -EINVAL;
	}
	return 0;
}

/**
 * idt24x_enable_output - Enable/disable a particular output
 * @chip:	Device data structure
 * @output:	Output to enable/disable
 * @enable:	Enable (true/false)
 *
 * Return: passes on regmap_write return value.
 */
static int idt24x_enable_output(
	struct clk_idt24x_chip *chip, u8 output, bool enable)
{
	struct clk_register_offsets offsets;
	int err;
	struct i2c_client *client = chip->i2c_client;

	/*
	 * When an output is enabled, enable it in the original
	 * data read from the chip and cached. Otherwise it may be
	 * accidentally	turned off when another output is enabled.
	 *
	 * E.g., the driver starts with all outputs off in reg_out_en_x.
	 * Q1 is enabled with the appropriate mask. Q2 is then enabled,
	 * which results in Q1 being turned back off (because Q1 was off
	 * in reg_out_en_x).
	 */

	err = idt24x_get_offsets(output, &offsets);
	if (err) {
		dev_err(&client->dev,
			"%s: error calling idt24x_get_offsets for %d: %i",
			__func__, output, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: q%u enable? %d. reg_out_en_x before: 0x%x, reg_out_mode_0_1 before: 0x%x, reg_out_mode_2_3 before: 0x%x, reg_qx_dis before: 0x%x",
		__func__, output, enable, chip->reg_out_en_x,
		chip->reg_out_mode_0_1, chip->reg_out_mode_2_3,
		chip->reg_qx_dis);

	chip->reg_out_en_x = chip->reg_out_en_x & ~offsets.oe_mask;
	if (enable)
		chip->reg_out_en_x |= (1 << bits_to_shift(offsets.oe_mask));

	chip->reg_qx_dis = chip->reg_qx_dis & ~offsets.dis_mask;
	dev_dbg(&client->dev,
		"%s: q%u enable? %d. reg_qx_dis mask: 0x%x, before checking enable: 0x%x",
		__func__, output, enable, offsets.dis_mask,
		chip->reg_qx_dis);
	if (!enable)
		chip->reg_qx_dis |= (1 << bits_to_shift(offsets.dis_mask));

	dev_dbg(&client->dev,
		"%s: q%u enable? %d. reg_out_en_x after: 0x%x, reg_qx_dis after: 0x%x",
		__func__, output, enable, chip->reg_out_en_x,
		chip->reg_qx_dis);

	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_OUTEN, chip->reg_out_en_x);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_OUTEN: %i",
			__func__, err);
		return err;
	}

	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_OUTMODE0_1,
		chip->reg_out_mode_0_1);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_OUTMODE0_1: %i",
			__func__, err);
		return err;
	}

	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_OUTMODE2_3,
		chip->reg_out_mode_2_3);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_OUTMODE2_3: %i",
			__func__, err);
		return err;
	}

	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_Q_DIS, chip->reg_qx_dis);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_Q_DIS: %i",
			__func__, err);
		return err;
	}

	return 0;
}

/**
 * idt24x_update_device - write registers to the chip
 * @chip:	Device data structure
 *
 * Write all values to hardware that we	have calculated.
 *
 * Return: passes on regmap_bulk_write return value.
 */
static int idt24x_update_device(struct clk_idt24x_chip *chip)
{
	int err;
	struct i2c_client *client = chip->i2c_client;
	int x = -1;

	dev_dbg(&client->dev,
		"%s: setting DSM_INT_8 (val %u @ %u)",
		__func__, chip->divs.dsmint >> 8,
		IDT24x_REG_DSM_INT_8);
	err = i2cwritewithmask(
		client, chip->regmap, IDT24x_REG_DSM_INT_8,
		(chip->divs.dsmint >> 8) & IDT24x_REG_DSM_INT_8_MASK,
		chip->reg_dsm_int_8, IDT24x_REG_DSM_INT_8_MASK);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_DSM_INT_8: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting DSM_INT_7_0 (val %u @ 0x%x)",
		__func__, chip->divs.dsmint & 0xFF,
		IDT24x_REG_DSM_INT_7_0);
	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_DSM_INT_7_0,
		chip->divs.dsmint & 0xFF);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_DSM_INT_7_0: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_DSMFRAC_20_16 (val %u @ 0x%x)",
		__func__, chip->divs.dsmfrac >> 16,
		IDT24x_REG_DSMFRAC_20_16);
	err = i2cwritewithmask(
		client, chip->regmap, IDT24x_REG_DSMFRAC_20_16,
		(chip->divs.dsmfrac >> 16) & IDT24x_REG_DSMFRAC_20_16_MASK,
		chip->reg_dsm_int_8, IDT24x_REG_DSMFRAC_20_16_MASK);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_DSMFRAC_20_16: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_DSMFRAC_15_8 (val %u @ 0x%x)",
		__func__, (chip->divs.dsmfrac >> 8) & 0xFF,
		IDT24x_REG_DSMFRAC_15_8);
	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_DSMFRAC_15_8,
		(chip->divs.dsmfrac >> 8) & 0xFF);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_DSMFRAC_15_8: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_DSMFRAC_7_0 (val %u @ 0x%x)",
		__func__, chip->divs.dsmfrac & 0xFF,
		IDT24x_REG_DSMFRAC_7_0);
	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_DSMFRAC_7_0,
		chip->divs.dsmfrac & 0xFF);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_DSMFRAC_7_0: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_NS1_Q0 (val %u @ 0x%x)",
		__func__, chip->divs.ns1_q0, IDT24x_REG_NS1_Q0);
	err = i2cwritewithmask(
		client, chip->regmap, IDT24x_REG_NS1_Q0,
		chip->divs.ns1_q0 & IDT24x_REG_NS1_Q0_MASK,
		chip->reg_ns1_q0, IDT24x_REG_NS1_Q0_MASK);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_NS1_Q0: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_NS2_Q0_15_8 (val %u @ 0x%x)",
		__func__, (chip->divs.ns2_q0 >> 8) & 0xFF,
		IDT24x_REG_NS2_Q0_15_8);
	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_NS2_Q0_15_8,
		(chip->divs.ns2_q0 >> 8) & 0xFF);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_NS2_Q0_15_8: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: setting IDT24x_REG_NS2_Q0_7_0 (val %u @ 0x%x)",
		__func__, chip->divs.ns2_q0 & 0xFF,
		IDT24x_REG_NS2_Q0_7_0);
	err = i2cwrite(
		client, chip->regmap, IDT24x_REG_NS2_Q0_7_0,
		chip->divs.ns2_q0 & 0xFF);
	if (err) {
		dev_err(&client->dev,
			"%s: error setting IDT24x_REG_NS2_Q0_7_0: %i",
			__func__, err);
		return err;
	}

	dev_dbg(&client->dev,
		"%s: calling idt24x_enable_output for Q0. requestedFreq: %u",
		__func__, chip->clk[0].requested);
	idt24x_enable_output(chip, 0, chip->clk[0].requested != 0);

	dev_dbg(&client->dev,
		"%s: writing values for q1-q3", __func__);
	for (x = 1; x < NUM_OUTPUTS; x++) {
		struct clk_register_offsets offsets;

		if (chip->clk[x].requested != 0) {
			dev_dbg(&client->dev,
				"%s: calling idt24x_get_offsets for %u",
				__func__, x);
			err = idt24x_get_offsets(x, &offsets);
			if (err) {
				dev_err(&client->dev,
					"%s: error calling idt24x_get_offsets: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: (q%u, nint: %u, nfrac: %u)",
				__func__, x, chip->divs.nint[x - 1],
				chip->divs.nfrac[x - 1]);

			dev_dbg(&client->dev,
				"%s: setting n_17_16_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				chip->divs.nint[x - 1] >> 16,
				offsets.n_17_16_offset);
			err = i2cwritewithmask(
				client, chip->regmap, offsets.n_17_16_offset,
				(chip->divs.nint[x - 1] >> 16) &
					offsets.n_17_16_mask,
				chip->reg_n_qx_17_16[x - 1],
				offsets.n_17_16_mask);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting n_17_16_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting n_15_8_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				(chip->divs.nint[x - 1] >> 8) & 0xFF,
				offsets.n_15_8_offset);
			err = i2cwrite(
				client, chip->regmap, offsets.n_15_8_offset,
				(chip->divs.nint[x - 1] >> 8) & 0xFF);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting n_15_8_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting n_7_0_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				chip->divs.nint[x - 1] & 0xFF,
				offsets.n_7_0_offset);
			err = i2cwrite(
				client, chip->regmap, offsets.n_7_0_offset,
				chip->divs.nint[x - 1] & 0xFF);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting n_7_0_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting nfrac_27_24_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				(chip->divs.nfrac[x - 1] >> 24),
				offsets.nfrac_27_24_offset);
			err = i2cwritewithmask(
				client, chip->regmap,
				offsets.nfrac_27_24_offset,
				(chip->divs.nfrac[x - 1] >> 24) &
					offsets.nfrac_27_24_mask,
				chip->reg_nfrac_qx_27_24[x - 1],
				offsets.nfrac_27_24_mask);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting nfrac_27_24_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting nfrac_23_16_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				(chip->divs.nfrac[x - 1] >> 16) & 0xFF,
				offsets.nfrac_23_16_offset);
			err = i2cwrite(
				client, chip->regmap,
				offsets.nfrac_23_16_offset,
				(chip->divs.nfrac[x - 1] >> 16) & 0xFF);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting nfrac_23_16_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting nfrac_15_8_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				(chip->divs.nfrac[x - 1] >> 8) & 0xFF,
				offsets.nfrac_15_8_offset);
			err = i2cwrite(
				client, chip->regmap,
				offsets.nfrac_15_8_offset,
				(chip->divs.nfrac[x - 1] >> 8) & 0xFF);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting nfrac_15_8_offset: %i",
					__func__, err);
				return err;
			}

			dev_dbg(&client->dev,
				"%s: setting nfrac_7_0_offset (q%u, val %u @ 0x%x)",
				__func__, x,
				chip->divs.nfrac[x - 1] & 0xFF,
				offsets.nfrac_7_0_offset);
			err = i2cwrite(
				client, chip->regmap, offsets.nfrac_7_0_offset,
				chip->divs.nfrac[x - 1] & 0xFF);
			if (err) {
				dev_err(&client->dev,
					"%s: error setting nfrac_7_0_offset: %i",
					__func__, err);
				return err;
			}
		}
		idt24x_enable_output(chip, x,
				     chip->clk[x].requested != 0);
		chip->clk[x].actual = chip->clk[x].requested;
	}
	return 0;
}

/**
 * idt24x_set_frequency - Adjust output frequency on the attached chip.
 * @chip:	Device data structure, including all requested frequencies.
 *
 * Return: 0 on success.
 */
int idt24x_set_frequency(struct clk_idt24x_chip *chip)
{
	int err;
	struct i2c_client *client = chip->i2c_client;
	int x;
	bool all_disabled = true;

	for (x = 0; x < NUM_OUTPUTS; x++) {
		if (chip->clk[x].requested == 0) {
			idt24x_enable_output(chip, x, false);
			chip->clk[x].actual = 0;
		} else {
			all_disabled = false;
		}
	}

	if (all_disabled)
		/*
		 * no requested frequencies, so nothing else to calculate
		 * or write to the chip. If the consumer wants to disable
		 * all outputs, they can request 0 for all frequencies.
		 */
		return 0;

	if (chip->input_clk_freq == 0) {
		dev_err(&client->dev,
			"%s: no input frequency; can't continue.", __func__);
		return -EINVAL;
	}

	err = idt24x_calc_divs(chip);
	if (err) {
		dev_err(&client->dev,
			"%s: error calling idt24x_calc_divs: %i",
			__func__, err);
		return err;
	}

	err = idt24x_update_device(chip);
	if (err) {
		dev_err(&client->dev,
			"%s: error updating the device: %i",
			__func__, err);
		return err;
	}

	return 0;
}
