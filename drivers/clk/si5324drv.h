/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Si5324 clock driver
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 */

#ifndef SI5324DRV_H_
#define SI5324DRV_H_

#include <linux/types.h>

/******************************************************************************
 * User settable defines that depend on the specific board design.
 * The defaults are for the Xilinx KC705 board.
 *****************************************************************************/

#define SI5324_XTAL_FREQ 114285000UL

/******************************************************************************
 * Defines independent on the specific board design. Should not be changed.
 *****************************************************************************/

#define SI5324_SUCCESS		0 /*< Operation was successful */
#define SI5324_ERR_IIC		-1 /*< IIC error occurred */
#define SI5324_ERR_FREQ		-2 /*< Could not calculate frequency setting */
#define SI5324_ERR_PARM		-3 /*< Invalid parameter */

#define SI5324_CLKSRC_CLK1	1 /*< Use clock input 1 */
#define SI5324_CLKSRC_CLK2	2 /*< Use clock input 2 */
#define SI5324_CLKSRC_XTAL	3 /*< Use crystal (free running mode) */

#define SI5324_FOSC_MIN		4850000000UL /*< Min oscillator frequency */
#define SI5324_FOSC_MAX		5670000000UL /*< Max oscillator frequency */
#define SI5324_F3_MIN		10000 /*< Min phase detector frequency */
#define SI5324_F3_MAX		2000000 /*< Max phase detector frequency */
#define SI5324_FIN_MIN		2000 /*< Min input frequency */
#define SI5324_FIN_MAX		710000000UL /*< Max input frequency */
#define SI5324_FOUT_MIN		2000 /*< Min output frequency */
#define SI5324_FOUT_MAX		945000000UL /*< Max output frequency */

#define SI5324_N1_HS_MIN	6
#define SI5324_N1_HS_MAX	11
#define SI5324_NC_LS_MIN	1
#define SI5324_NC_LS_MAX	0x100000
#define SI5324_N2_HS_MIN	4
#define SI5324_N2_HS_MAX	11
#define SI5324_N2_LS_MIN	2 /* even values only */
#define SI5324_N2_LS_MAX	0x100000
#define SI5324_N3_MIN		1
#define SI5324_N3_MAX		0x080000
#define SI5324_FIN_FOUT_SHIFT	28

struct si5324_settingst {
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
};

int si5324_calcfreqsettings(u32 clkinfreq, u32 clkoutfreq, u32 *clkactual,
			    u8 *n1_hs, u32 *ncn_ls, u8 *n2_hs,
			    u32 *n2_ls, u32 *n3n, u8 *bwsel);
void si5324_rate_approx(u64 f, u64 md, u32 *num, u32 *denom);
int si5324_calc_ncls_limits(struct si5324_settingst *settings);

#endif /* SI5324DRV_H_ */
