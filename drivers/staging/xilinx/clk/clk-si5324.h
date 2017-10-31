/*
 * clk-si5324.h: Silicon Laboratories Si5324A/B/C I2C Clock Generator
 *
 * Leon Woestenberg <leon@sidebranch.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _CLK_SI5324_H_
#define _CLK_SI5324_H_

#define SI5324_BUS_BASE_ADDR			0x68

#define SI5324_REG0			0
#define SI5324_REG0_FREE_RUN			(1<<6)

#define SI5324_CKSEL 3

#define SI5324_DSBL_CLKOUT 10

#define SI5324_POWERDOWN		11
#define SI5324_PD_CK1 (1<<0)
#define SI5324_PD_CK2 (1<<1)

/* output clock dividers */
#define SI5324_N1_HS_OUTPUT_DIVIDER 25
#define SI5324_NC1_LS_H 31
#define SI5324_NC1_LS_M 32
#define SI5324_NC1_LS_L 33

#define SI5324_NC2_LS_H 34
#define SI5324_NC2_LS_M 35
#define SI5324_NC2_LS_L 36

#define SI5324_RESET 136
#define SI5324_RST_REG (1<<7)

/* selects 2kHz to 710 MHz */
#define SI5324_CLKIN_MIN_FREQ			2000
#define SI5324_CLKIN_MAX_FREQ			(710 * 1000 * 1000)


/* generates 2kHz to 945 MHz */
#define SI5324_CLKOUT_MIN_FREQ			2000
#define SI5324_CLKOUT_MAX_FREQ			(945 * 1000 * 1000)

/**
 * The following constants define the limits of the divider settings.
 */
#define SI5324_N1_HS_MIN  6        /**< Minimum N1_HS setting (4 and 5 are for higher output frequencies than we support */
#define SI5324_N1_HS_MAX 11        /**< Maximum N1_HS setting */
#define SI5324_NC_LS_MIN  1        /**< Minimum NCn_LS setting (1 and even values) */
#define SI5324_NC_LS_MAX 0x100000  /**< Maximum NCn_LS setting (1 and even values) */
#define SI5324_N2_HS_MIN  4        /**< Minimum NC2_HS setting */
#define SI5324_N2_HS_MAX 11        /**< Maximum NC2_HS setting */
#define SI5324_N2_LS_MIN  2        /**< Minimum NC2_LS setting (even values only) */
#define SI5324_N2_LS_MAX 0x100000  /**< Maximum NC2_LS setting (even values only) */
#define SI5324_N3_MIN     1        /**< Minimum N3n setting */
#define SI5324_N3_MAX    0x080000  /**< Maximum N3n setting */

#endif
