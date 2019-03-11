/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Si5324 clock driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Authors:	Leon Woestenberg <leon@sidebranch.com>
 *		Venkateshwar Rao <vgannava@xilinx.com>
 */

#ifndef _CLK_SI5324_H_
#define _CLK_SI5324_H_

#define SI5324_BUS_BASE_ADDR		0x68

#define SI5324_CONTROL			0
#define SI5324_CONTROL_FREE_RUN		BIT(6)
#define SI5324_FREE_RUN_EN		0x54

#define SI5324_INCK_PRIOR		1
#define SI5324_INCK_PRIOR_1_MASK	0xC
#define SI5324_INCK_PRIOI_2_MASK	0x3

#define SI5324_BWSEL			2
#define SI5324_BWSEL_MASK		0xF0
#define SI5324_BWSEL_SHIFT		4
#define SI5324_BWSEL_DEF_VAL		2

#define SI5324_CKSEL			3
#define SI5324_CKSEL_SQL_ICAL		BIT(4)
#define SI5324_CKSEL_SHIFT		6
#define SI5324_CK_SEL			3

#define SI3324_AUTOSEL			4
#define SI5324_AUTOSEL_DEF		0x12

#define SI5324_ICMOS			5
#define SI5324_OUTPUT_SIGFMT		6
#define SI5324_OUTPUT_SF1_DEFAULT	0xF
#define SI5324_REFFRE_FOS		7
#define SI5324_HLOG			8
#define SI5324_AVG_HIST			9
#define SI5324_DSBL_CLKOUT		10
#define SI5324_DSBL_CLKOUT2		BIT(3)
#define SI5324_POWERDOWN		11
#define SI5324_PD_CK1			BIT(0)
#define SI5324_PD_CK2			BIT(1)
#define SI5324_PD_CK1_DIS		0x41
#define SI5324_PD_CK2_DIS		0x42
#define SI5324_FOS_LOCKT		19
#define SI5324_FOS_DEFAULT		0x23
#define SI5324_CK_ACTV_SEL		21
#define SI5324_CK_DEFAULT		0xFC
#define SI5324_CK_ACTV			BIT(1)
#define SI5324_CK_SELPIN		BIT(1)
#define SI5324_LOS_MSK			23
#define SI5324_FOS_L0L_MASK		24

/* output clock dividers */
#define SI5324_N1_HS			25
#define SI5324_N1_HS_VAL_SHIFT		5
#define SI5324_HSHIFT			16
#define SI5324_LSHIFT			8
#define SI5324_NC1_LS_H			31
#define SI5324_NC1_LS_M			32
#define SI5324_NC1_LS_L			33
#define SI5324_DIV_LS_MASK		0x0F
#define SI5324_DIV_HS_MASK		0xF0
#define SI5324_NC2_LS_H			34
#define SI5324_NC2_LS_M			35
#define SI5324_NC2_LS_L			36

#define SI5324_N2_HS_LS_H		40
#define SI5324_N2_HS_LS_H_VAL_SHIFT	5
#define SI5324_N2_LS_H			41
#define SI5324_N2_LS_L			42
#define SI5324_N31_CLKIN_H		43
#define SI5324_N31_CLKIN_M		44
#define SI5324_N31_CLKIN_L		45
#define SI5324_N32_CLKIN_H		46
#define SI5324_N32_CLKIN_M		47
#define SI5324_N32_CLKIN_L		48
#define SI5324_FOS_CLKIN_RATE		55
#define SI5324_PLL_ACTV_CLK		128
#define SI5324_LOS_STATUS		129
#define SI5324_CLKIN_LOL_STATUS		130
#define SI5324_LOS_FLG			131
#define SI5324_FOS_FLG			132
#define SI5324_PARTNO_H			134
#define SI5324_PARTNO_L			135

#define SI5324_RESET_CALIB		136
#define SI5324_RST_ALL			BIT(7)
#define SI5324_CALIB_EN			BIT(6)

#define SI5324_FASTLOCK			137
#define SI5324_FASTLOCK_EN		BIT(0)
#define SI5324_LOS1_LOS2_EN		138
#define SI5324_SKEW1			142
#define SI5324_SKEW2			143

/* selects 2kHz to 710 MHz */
#define SI5324_CLKIN_MIN_FREQ		2000
#define SI5324_CLKIN_MAX_FREQ		(710 * 1000 * 1000)

/* generates 2kHz to 945 MHz */
#define SI5324_CLKOUT_MIN_FREQ		2000
#define SI5324_CLKOUT_MAX_FREQ		(945 * 1000 * 1000)

/* The following constants define the limits of the divider settings. */
#define SI5324_N1_HS_MIN		6
#define SI5324_N1_HS_MAX		11
#define SI5324_NC_LS_MIN		1
#define SI5324_NC_LS_MAX		0x100000
#define SI5324_N2_HS_MIN		4
#define SI5324_N2_HS_MAX		11
#define SI5324_N2_LS_MIN		2
#define SI5324_N2_LS_MAX		0x100000
#define SI5324_N3_MIN			1
#define SI5324_N3_MAX			0x080000

#define SI5324_SRC_XTAL			0
#define SI5324_SRC_CLKIN1		1
#define SI5324_SRC_CLKIN2		2
#define SI5324_SRC_CLKS			3

#define SI5324_CLKIN1			0
#define SI5324_CLKIN2			1
#define SI5324_MAX_CLKOUTS		2
#define NUM_NAME_IDS			6 /* 3 clkin, 1 pll, 2 clkout */
#define MAX_NAME_LEN			11
#define SI5324_PARAM_LEN		24
#define SI5324_NC_PARAM_LEN		6
#define SI5324_OUT_REGS			14
#define SI5324_N1_PARAM_LEN		1
#define SI5324_N2_PARAM_LEN		9
#define SI5324_REF_CLOCK		114285000UL
#define SI5324_RESET_DELAY_MS		20

#endif
