/* SPDX-License-Identifier: GPL-2.0 */
/* clk-idt8t49n24x-core.h - Program 8T49N24x settings via I2C (common code)
 *
 * Copyright (C) 2018, Integrated Device Technology, Inc. <david.cater@idt.com>
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 * This program is distributed "AS IS" and  WITHOUT ANY WARRANTY;
 * including the implied warranties of MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, or NON-INFRINGEMENT.
 */

#ifndef __IDT_CLK_IDT8T49N24X_CORE_H_
#define __IDT_CLK_IDT8T49N24X_CORE_H_

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

/*
 * The configurations in the settings file have 0x317 registers (last offset
 * is 0x316).
 */
#define NUM_CONFIG_REGISTERS	0x317
#define NUM_INPUTS		2
#define NUM_OUTPUTS		4
#define DEBUGFS_BUFFER_LENGTH	200
#define WRITE_BLOCK_SIZE	32

/* Non output-specific registers */
#define IDT24x_REG_DBL_DIS		0x6C
#define IDT24x_REG_DBL_DIS_MASK		0x01
#define IDT24x_REG_DSM_INT_8		0x25
#define IDT24x_REG_DSM_INT_8_MASK	0x01
#define IDT24x_REG_DSM_INT_7_0		0x26
#define IDT24x_REG_DSMFRAC_20_16	0x28
#define IDT24x_REG_DSMFRAC_20_16_MASK	0x1F
#define IDT24x_REG_DSMFRAC_15_8		0x29
#define IDT24x_REG_DSMFRAC_7_0		0x2A
#define IDT24x_REG_OUTEN		0x39
#define IDT24x_REG_OUTMODE0_1		0x3E
#define IDT24x_REG_OUTMODE2_3		0x3D
#define IDT24x_REG_Q_DIS		0x6F

/* Q0 */
#define IDT24x_REG_OUTEN0_MASK		0x01
#define IDT24x_REG_OUTMODE0_MASK	0x0E
#define IDT24x_REG_Q0_DIS_MASK		0x01
#define IDT24x_REG_NS1_Q0		0x3F
#define IDT24x_REG_NS1_Q0_MASK		0x03
#define IDT24x_REG_NS2_Q0_15_8		0x40
#define IDT24x_REG_NS2_Q0_7_0		0x41

/* Q1 */
#define IDT24x_REG_OUTEN1_MASK		0x02
#define IDT24x_REG_OUTMODE1_MASK	0xE0
#define IDT24x_REG_Q1_DIS_MASK		0x02
#define IDT24x_REG_N_Q1_17_16		0x42
#define IDT24x_REG_N_Q1_17_16_MASK	0x03
#define IDT24x_REG_N_Q1_15_8		0x43
#define IDT24x_REG_N_Q1_7_0		0x44
#define IDT24x_REG_NFRAC_Q1_27_24	0x57
#define IDT24x_REG_NFRAC_Q1_27_24_MASK	0x0F
#define IDT24x_REG_NFRAC_Q1_23_16	0x58
#define IDT24x_REG_NFRAC_Q1_15_8	0x59
#define IDT24x_REG_NFRAC_Q1_7_0		0x5A

/* Q2 */
#define IDT24x_REG_OUTEN2_MASK		0x04
#define IDT24x_REG_OUTMODE2_MASK	0x0E
#define IDT24x_REG_Q2_DIS_MASK		0x04
#define IDT24x_REG_N_Q2_17_16		0x45
#define IDT24x_REG_N_Q2_17_16_MASK	0x03
#define IDT24x_REG_N_Q2_15_8		0x46
#define IDT24x_REG_N_Q2_7_0		0x47
#define IDT24x_REG_NFRAC_Q2_27_24	0x5B
#define IDT24x_REG_NFRAC_Q2_27_24_MASK	0x0F
#define IDT24x_REG_NFRAC_Q2_23_16	0x5C
#define IDT24x_REG_NFRAC_Q2_15_8	0x5D
#define IDT24x_REG_NFRAC_Q2_7_0		0x5E

/* Q3 */
#define IDT24x_REG_OUTEN3_MASK		0x08
#define IDT24x_REG_OUTMODE3_MASK	0xE0
#define IDT24x_REG_Q3_DIS_MASK		0x08
#define IDT24x_REG_N_Q3_17_16		0x48
#define IDT24x_REG_N_Q3_17_16_MASK	0x03
#define IDT24x_REG_N_Q3_15_8		0x49
#define IDT24x_REG_N_Q3_7_0		0x4A
#define IDT24x_REG_NFRAC_Q3_27_24	0x5F
#define IDT24x_REG_NFRAC_Q3_27_24_MASK	0x0F
#define IDT24x_REG_NFRAC_Q3_23_16	0x60
#define IDT24x_REG_NFRAC_Q3_15_8	0x61
#define IDT24x_REG_NFRAC_Q3_7_0		0x62

/**
 * struct idt24x_output - device output information
 * @hw:		hw registration info for this specific output clcok. This gets
 *		passed as an argument to CCF api calls (e.g., set_rate).
 *		container_of can then be used to get the reference to this
 *		struct.
 * @chip:	store a reference to the parent device structure. container_of
 *		cannot be used to get to the parent device structure from
 *		idt24x_output, because clk_idt24x_chip contains an array of
 *		output structs (for future enhancements to support devices
 *		with different numbers of output clocks).
 * @index:	identifies output on the chip; used in debug statements
 * @requested:	requested output clock frequency (in Hz)
 * @actual:	actual output clock frequency (in Hz). Will only be set after
 *		successful update of the device.
 * @debug_freq:	stores value for debugfs file. Use this instead of requested
 *		struct var because debugfs expects u64, not u32.
 */
struct idt24x_output {
	struct clk_hw hw;
	struct clk_idt24x_chip *chip;
	u8 index;
	u32 requested;
	u32 actual;
	u64 debug_freq;
};

/**
 * struct idt24x_dividers - output dividers
 * @dsmint:	int component of feedback divider for VCO (2-stage divider)
 * @dsmfrac:	fractional component of feedback divider for VCO
 * @ns1_q0:	ns1 divider component for Q0
 * @ns2_q0:	ns2 divider component for Q0
 * @nint:	int divider component for Q1-3
 * @nfrac:	fractional divider component for Q1-3
 */
struct idt24x_dividers {
	u16 dsmint;
	u32 dsmfrac;

	u8 ns1_q0;
	u16 ns2_q0;

	u32 nint[3];
	u32 nfrac[3];
};

/**
 * struct clk_idt24x_chip - device info for chip
 * @regmap:		register map used to perform i2c writes to the chip
 * @i2c_client:		i2c_client struct passed to probe
 * @min_freq:		min frequency for this chip
 * @max_freq:		max frequency for this chip
 * @settings:		filled in if full register map is specified in the DT
 * @has_settings:	true if settings array is valid
 * @input_clk:		ptr to input clock specified in DT
 * @input_clk_num:	which input clock was specified. 0-based. A value of
 *			NUM_INPUTS indicates that a XTAL is used as the input.
 * @input_clk_nb:	notification support (if input clk changes)
 * @input_clk_freq:	current freq of input_clk
 * @doubler_disabled:	whether input doubler is enabled. This value is read
 *			from the hw on probe (in case it is set in @settings).
 * @clk:		array of outputs. One entry per output supported by the
 *			chip. Frequencies requested via the ccf api will be
 *			recorded in this array.
 * @reg_dsm_int_8:	record current value from hw to avoid modifying
 *			when writing register values
 * @reg_dsm_frac_20_16:	record current value
 * @reg_out_en_x:	record current value
 * @reg_out_mode_0_1:	record current value
 * @reg_out_mode_2_3:	record current value
 * @reg_qx_dis:		record current value
 * @reg_ns1_q0:		record current value
 * @reg_n_qx_17_16:	record current value
 * @reg_nfrac_qx_27_24:	record current value
 * @divs:		output divider values for all outputs
 * @debugfs_dirroot:	debugfs support
 * @debugfs_fileaction:	debugfs support
 * @debugfs_filei2c:	debugfs support
 * @debugfs_map:	debugfs support
 * @dbg_cache:		debugfs support
 * @debugfs_fileqfreq:	debugfs support
 */
struct clk_idt24x_chip {
	struct regmap *regmap;
	struct i2c_client *i2c_client;

	u32 min_freq;
	u32 max_freq;

	u8 settings[NUM_CONFIG_REGISTERS];

	bool has_settings;

	struct clk *input_clk;
	int input_clk_num;
	struct notifier_block input_clk_nb;
	u32 input_clk_freq;

	bool doubler_disabled;

	struct idt24x_output clk[NUM_OUTPUTS];

	unsigned int reg_dsm_int_8;
	unsigned int reg_dsm_frac_20_16;
	unsigned int reg_out_en_x;
	unsigned int reg_out_mode_0_1;
	unsigned int reg_out_mode_2_3;
	unsigned int reg_qx_dis;
	unsigned int reg_ns1_q0;
	unsigned int reg_n_qx_17_16[3];
	unsigned int reg_nfrac_qx_27_24[3];

	struct idt24x_dividers divs;

	struct dentry *debugfs_dirroot, *debugfs_fileaction, *debugfs_filei2c,
		*debugfs_map;
	char dbg_cache[DEBUGFS_BUFFER_LENGTH];
	struct dentry *debugfs_fileqfreq[4];
};

#define to_idt24x_output(_hw) \
	container_of(_hw, struct idt24x_output, hw)
#define to_clk_idt24x_from_client(_client) \
	container_of(_client, struct clk_idt24x_chip, i2c_client)
#define to_clk_idt24x_from_nb(_nb) \
	container_of(_nb, struct clk_idt24x_chip, input_clk_nb)

/**
 * struct clk_register_offsets - register offsets for current context
 * @oe_offset:		offset for current output enable and mode
 * @oe_mask:		mask for current output enable
 * @dis_mask:		mask for current output disable
 * @n_17_16_offset:	offset for current output int divider (bits 17:16)
 * @n_17_16_mask:	mask for current output int divider (bits 17:16)
 * @n_15_8_offset:	offset for current output int divider (bits 15:8)
 * @n_7_0_offset:	offset for current output int divider (bits 7:0)
 * @nfrac_27_24_offset:	offset for current output frac divider (bits 27:24)
 * @nfrac_27_24_mask:	mask for current output frac divider (bits 27:24)
 * @nfrac_23_16_offset:	offset for current output frac divider (bits 23:16)
 * @nfrac_15_8_offset:	offset for current output frac divider (bits 15:8)
 * @nfrac_7_0_offset:	offset for current output frac divider (bits 7:0)
 * @ns1_offset:		offset for stage 1 div for output Q0
 * @ns1_offset_mask:	mask for stage 1 div for output Q0
 * @ns2_15_8_offset:	offset for stage 2 div for output Q0 (bits 15:8)
 * @ns2_7_0_offset:	offset for stage 2 div for output Q0 (bits 7:0)
 */
struct clk_register_offsets {
	u16 oe_offset;
	u8 oe_mask;
	u8 dis_mask;

	u16 n_17_16_offset;
	u8 n_17_16_mask;
	u16 n_15_8_offset;
	u16 n_7_0_offset;
	u16 nfrac_27_24_offset;
	u8 nfrac_27_24_mask;
	u16 nfrac_23_16_offset;
	u16 nfrac_15_8_offset;
	u16 nfrac_7_0_offset;

	u16 ns1_offset;
	u8 ns1_offset_mask;
	u16 ns2_15_8_offset;
	u16 ns2_7_0_offset;
};

int bits_to_shift(unsigned int mask);
int i2cwritebulk(
	struct i2c_client *client, struct regmap *map,
	unsigned int reg, u8 val[], size_t val_count);
int idt24x_get_offsets(
	u8 output_num,
	struct clk_register_offsets *offsets);
int idt24x_set_frequency(struct clk_idt24x_chip *chip);

#endif /* __IDT_CLK_IDT8T49N24X_CORE_H_ */
