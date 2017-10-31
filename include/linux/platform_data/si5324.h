/*
 * Si5324A/B/C programmable clock generator platform_data.
 */

#ifndef __LINUX_PLATFORM_DATA_SI5324_H__
#define __LINUX_PLATFORM_DATA_SI5324_H__

/**
 * enum si5324_pll_src - Si5324 pll clock source
 * @SI5324_PLL_SRC_DEFAULT: default, do not change eeprom config
 * @SI5324_PLL_SRC_XTAL: pll source clock is XTAL input
 * @SI5324_PLL_SRC_CLKIN1: pll source clock is CLKIN1 input
 * @SI5324_PLL_SRC_CLKIN2: pll source clock is CLKIN2 input
 */
enum si5324_pll_src {
	SI5324_PLL_SRC_XTAL = 0,
	SI5324_PLL_SRC_CLKIN1 = 1,
	SI5324_PLL_SRC_CLKIN2 = 2,
};

/**
 * enum si5324_drive_strength - Si5324 clock output drive strength
 * @SI5324_DRIVE_DEFAULT: default, do not change eeprom config
 * @SI5324_DRIVE_2MA: 2mA clock output drive strength
 * @SI5324_DRIVE_4MA: 4mA clock output drive strength
 * @SI5324_DRIVE_6MA: 6mA clock output drive strength
 * @SI5324_DRIVE_8MA: 8mA clock output drive strength
 */
enum si5324_drive_strength {
	SI5324_DRIVE_DEFAULT = 0,
	SI5324_DRIVE_2MA = 2,
	SI5324_DRIVE_4MA = 4,
	SI5324_DRIVE_6MA = 6,
	SI5324_DRIVE_8MA = 8,
};

/**
 * enum si5324_disable_state - Si5324 clock output disable state
 * @SI5324_DISABLE_DEFAULT: default, do not change eeprom config
 * @SI5324_DISABLE_LOW: CLKx is set to a LOW state when disabled
 * @SI5324_DISABLE_HIGH: CLKx is set to a HIGH state when disabled
 * @SI5324_DISABLE_FLOATING: CLKx is set to a FLOATING state when
 *				disabled
 * @SI5324_DISABLE_NEVER: CLKx is NEVER disabled
 */
enum si5324_disable_state {
	SI5324_DISABLE_DEFAULT = 0,
	SI5324_DISABLE_LOW,
	SI5324_DISABLE_HIGH,
	SI5324_DISABLE_FLOATING,
	SI5324_DISABLE_NEVER,
};

/**
 * struct si5324_clkout_config - Si5324 clock output configuration
 * @clkout: clkout number
 * @multisynth_src: multisynth source clock
 * @clkout_src: clkout source clock
 * @pll_master: if true, clkout can also change pll rate
 * @drive: output drive strength
 * @rate: initial clkout rate, or default if 0
 */
struct si5324_clkout_config {
	enum si5324_drive_strength drive;
	enum si5324_disable_state disable_state;
	bool pll_master;
	unsigned long rate;
};

/**
 * struct si5324_platform_data - Platform data for the Si5324 clock driver
 * @clk_xtal: xtal input clock
 * @clk_clkin: clkin input clock
 * @pll_src: pll source clock setting
 * @clkout: array of clkout configuration
 */
struct si5324_platform_data {
	enum si5324_pll_src pll_src;
	struct si5324_clkout_config clkout[2];
};

#endif
