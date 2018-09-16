/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Si5324 clock generator platform data
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 */

#ifndef __LINUX_PLATFORM_DATA_SI5324_H__
#define __LINUX_PLATFORM_DATA_SI5324_H__

/**
 * enum si5324_pll_src - Si5324 pll clock source
 *
 * @SI5324_PLL_SRC_DEFAULT:	Default, do not change eeprom config
 * @SI5324_PLL_SRC_XTAL:	Pll source clock is XTAL input
 * @SI5324_PLL_SRC_CLKIN1:	Pll source clock is CLKIN1 input
 * @SI5324_PLL_SRC_CLKIN2:	Pll source clock is CLKIN2 input
 *
 * Defines enums for clock sources.
 */
enum si5324_pll_src {
	SI5324_PLL_SRC_XTAL = 0,
	SI5324_PLL_SRC_CLKIN1 = 1,
	SI5324_PLL_SRC_CLKIN2 = 2,
};

/**
 * enum si5324_drive_strength - Si5324 clock output drive strength
 *
 * @SI5324_DRIVE_DEFAULT:	Default, do not change eeprom config
 * @SI5324_DRIVE_2MA:		2mA clock output drive strength
 * @SI5324_DRIVE_4MA:		4mA clock output drive strength
 * @SI5324_DRIVE_6MA:		6mA clock output drive strength
 * @SI5324_DRIVE_8MA:		8mA clock output drive strength
 *
 * Defines enums for drive strength
 */
enum si5324_drive_strength {
	SI5324_DRIVE_DEFAULT = 0,
	SI5324_DRIVE_2MA = 2,
	SI5324_DRIVE_4MA = 4,
	SI5324_DRIVE_6MA = 6,
	SI5324_DRIVE_8MA = 8,
};

/**
 * struct si5324_clkout_config - Si5324 clock output configuration
 *
 * @drive:	output drive strength
 * @rate:	clkout rate
 */
struct si5324_clkout_config {
	enum si5324_drive_strength drive;
	unsigned long rate;
};

/**
 * struct si5324_platform_data - Platform data for the Si5324 clock driver
 *
 * @pll_src:	Pll source clock setting
 * @clkout:	Array of clkout configuration
 */
struct si5324_platform_data {
	enum si5324_pll_src pll_src;
	struct si5324_clkout_config clkout[2];
};

#endif
