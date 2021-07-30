// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/string.h>
#include "xhdmiphy.h"

struct gthdmi_chars {
	u64 dru_linerate;
	u64 tx_mmcm_fvcomin;
	u64 tx_mmcm_fvcomax;
	u64 rx_mmcm_fvcomin;
	u64 rx_mmcm_fvcomax;
	u32 qpll0_refclk_min;
	u32 qpll1_refclk_min;
	u32 cpll_refclk_min;
	u16 tx_mmcm_scale;
	u16 rx_mmcm_scale;
	u16 pll_scale;
};

static const struct gthdmi_chars gthe4hdmi_chars = {
	.dru_linerate = XHDMIPHY_HDMI_GTHE4_DRU_LRATE,
	.pll_scale = XHDMIPHY_HDMI_GTHE4_PLL_SCALE,
	.qpll0_refclk_min = XHDMIPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN,
	.qpll1_refclk_min = XHDMIPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN,
	.cpll_refclk_min = XHDMIPHY_HDMI_GTHE4_CPLL_REFCLK_MIN,
	.tx_mmcm_scale = XHDMIPHY_HDMI_GTHE4_TX_MMCM_SCALE,
	.tx_mmcm_fvcomin = XHDMIPHY_HDMI_GTHE4_TX_MMCM_FVCO_MIN,
	.tx_mmcm_fvcomax = XHDMIPHY_HDMI_GTHE4_TX_MMCM_FVCO_MAX,
	.rx_mmcm_scale = XHDMIPHY_HDMI_GTHE4_RX_MMCM_SCALE,
	.rx_mmcm_fvcomin = XHDMIPHY_HDMI_GTHE4_RX_MMCM_FVCO_MIN,
	.rx_mmcm_fvcomax = XHDMIPHY_HDMI_GTHE4_RX_MMCM_FVCO_MAX,
};

static const struct gthdmi_chars gtye4hdmi_chars = {
	.dru_linerate = XHDMIPHY_HDMI_GTYE4_DRU_LRATE,
	.pll_scale = XHDMIPHY_HDMI_GTYE4_PLL_SCALE,
	.qpll0_refclk_min = XHDMIPHY_HDMI_GTYE4_QPLL0_REFCLK_MIN,
	.qpll1_refclk_min = XHDMIPHY_HDMI_GTYE4_QPLL1_REFCLK_MIN,
	.cpll_refclk_min = XHDMIPHY_HDMI_GTYE4_CPLL_REFCLK_MIN,
	.tx_mmcm_scale = XHDMIPHY_HDMI_GTYE4_TX_MMCM_SCALE,
	.tx_mmcm_fvcomin = XHDMIPHY_HDMI_GTYE4_TX_MMCM_FVCO_MIN,
	.tx_mmcm_fvcomax = XHDMIPHY_HDMI_GTYE4_TX_MMCM_FVCO_MAX,
	.rx_mmcm_scale = XHDMIPHY_HDMI_GTYE4_RX_MMCM_SCALE,
	.rx_mmcm_fvcomin = XHDMIPHY_HDMI_GTYE4_RX_MMCM_FVCO_MIN,
	.rx_mmcm_fvcomax = XHDMIPHY_HDMI_GTYE4_RX_MMCM_FVCO_MAX,
};

static const struct gthdmi_chars gtye5hdmi_chars = {
	.dru_linerate = XHDMIPHY_HDMI_GTYE5_DRU_LRATE,
	.pll_scale = XHDMIPHY_HDMI_GTYE5_PLL_SCALE,
	.qpll0_refclk_min = XHDMIPHY_HDMI_GTYE5_LCPLL_REFCLK_MIN,
	.qpll1_refclk_min = XHDMIPHY_HDMI_GTYE5_RPLL_REFCLK_MIN,
	.cpll_refclk_min = 0,
	.tx_mmcm_scale = XHDMIPHY_HDMI_GTYE5_TX_MMCM_SCALE,
	.tx_mmcm_fvcomin = XHDMIPHY_HDMI_GTYE5_TX_MMCM_FVCO_MIN,
	.tx_mmcm_fvcomax = XHDMIPHY_HDMI_GTYE5_TX_MMCM_FVCO_MAX,
	.rx_mmcm_scale = XHDMIPHY_HDMI_GTYE5_RX_MMCM_SCALE,
	.rx_mmcm_fvcomin = XHDMIPHY_HDMI_GTYE5_RX_MMCM_FVCO_MIN,
	.rx_mmcm_fvcomax = XHDMIPHY_HDMI_GTYE5_RX_MMCM_FVCO_MAX,
};

/*
 * Following are the MMCM Parameter values for each rate.
 * Based on the MAX rate config in PHY the MMCM
 * should be programmed to generate the vid clk in
 * FRL mode
 */
static struct xhdmiphy_mmcm gthe4_gtye4_mmcm[] = {
	{0, 3, 1, 8, 8, 8}, /* 3x3 G -> 150Mhz */
	{1, 49, 16, 7, 7, 7}, /* 6x3 G -> 175Mhz */
	{2, 54, 16, 6, 6, 6}, /* 6x4 G -> 225Mhz */
	{3, 3, 1, 4, 4, 4}, /* 8 G -> 300Mhz */
	{4, 15, 4, 4, 4, 4}, /* 10 G -> 375Mhz */
	{5, 3, 1, 3, 3, 3} /* 12 G -> (400 * 3/1) / 3 -> 400Mhz */
};

static struct xhdmiphy_mmcm gtye5_mmcm[] = {
	{0, 3, 1, 8, 8, 8}, /* 12 G -> (400 * 3/1) / 3 -> 400Mhz */
	{1, 49, 16, 7, 7, 7}, /* 10 G -> 375Mhz */
	{2, 54, 16, 6, 6, 6}, /* 8 G -> 300Mhz */
	{3, 3, 1, 4, 4, 4}, /* 6x4 G -> 225Mhz */
	{4, 15, 4, 4, 4, 4}, /* 6x3 G -> 175Mhz */
	{5, 3, 1, 3, 3, 3} /* 3x3 G -> 150Mhz */
};

static u16 mmcme4_lockreg1_enc[37] = {0x0, 0x03e8, 0x03e8, 0x03e8, 0x03e8,
				      0x03e8, 0x03e8, 0x03e8, 0x03e8, 0x03e8,
				      0x03e8, 0x0384, 0x0339, 0x02ee, 0x02bc,
				      0x028a, 0x0271, 0x023f, 0x0226, 0x020d,
				      0x01f4, 0x01db, 0x01c2, 0x01a9, 0x0190,
				      0x0190, 0x0177, 0x015e, 0x015e, 0x0145,
				      0x0145, 0x012c, 0x012c, 0x012c, 0x0113,
				      0x0113, 0x0113};

static u16 mmcme4_lockreg2_enc[11] = {0x7c01, 0x1801, 0x1801, 0x2001, 0x2c01,
				      0x3801, 0x4401, 0x4c01, 0x5801, 0x6401,
				      0x7001};

static u16 mmcme4_lockreg3_enc[11] = {0x7fe9, 0x1be9, 0x1be9, 0x23e9, 0x2fe9,
				      0x3be9, 0x47e9, 0x4fe9, 0x5be9, 0x67e9,
				      0x73e9};

static struct xhdmiphy_mmcm *get_mmcm_conf(struct xhdmiphy_dev *inst)
{
	if (inst->conf.gt_type != XHDMIPHY_GTYE5)
		return gthe4_gtye4_mmcm;

	return gtye5_mmcm;
}

static const struct gthdmi_chars *get_gthdmi_ptr(struct xhdmiphy_dev *inst)
{
	if (inst->conf.gt_type == XHDMIPHY_GTHE4)
		return &gthe4hdmi_chars;
	else if (inst->conf.gt_type == XHDMIPHY_GTYE4)
		return &gtye4hdmi_chars;
	else if (inst->conf.gt_type == XHDMIPHY_GTYE5)
		return &gtye5hdmi_chars;

	return NULL;
}

/**
 * xhdmiphy_mmcme5_div_enc - This function returns the DRP encoding of
 * clkfbout_mult optimized for: Phase = 0; Dutycycle = 0.5; No Fractional
 * division. The calculations are based on XAPP888
 *
 * @div_type:	divider type to be encoded
 * @div:	div is the divider to be encoded
 *
 * @return:	- Encoded Value for clk_reg1 [15: 0]
 *		- Encoded Value for clk_reg2 [31:16]
 */
static u32 xhdmiphy_mmcme5_div_enc(enum mmcm_divs div_type, u16 div)
{
	u32 clk_reg1, clk_reg2;
	u16 divde = div;
	u8 hi_time, lo_time;

	if (div_type == XHDMIPHY_MMCM_CLKOUT_DIVIDE) {
		if (div % 2)
			divde = (div / 2);
		else
			divde = (div / 2) + (div % 2);
	}

	hi_time = divde / 2;
	lo_time = hi_time;

	clk_reg2 = lo_time & 0xff;
	clk_reg2 |= (hi_time & 0xff) << 8;

	if (div_type == XHDMIPHY_MMCM_CLKFBOUT_MULT_F) {
		clk_reg1 = (divde % 2) ? 0x00001700 : 0x00001600;
	} else {
		if (div % 2)
			clk_reg1 = (divde % 2) ? 0x0000bb00 : 0x0000ba00;
		else
			clk_reg1 = (divde % 2) ? 0x00001b00 : 0x00001a00;
	}

	return (clk_reg2 << 16) | clk_reg1;
}

/**
 * xhdmiphy_mmcme5_cpres_enc - This function returns the DRP encoding of CP and
 * Res optimized for: Phase = 0; Dutycycle = 0.5; BW = low; No Fractional
 * division
 *
 * @mult:	mult is the divider to be encoded
 *
 * @return:	- [3:0] CP
 *		- [20:17] RES
 *
 * note: For more details about DRP encoding values , refer GT user guide
 */
static u32 xhdmiphy_mmcme5_cpres_enc(u16 mult)
{
	u16 cp, res;

	switch (mult) {
	case 4:
		cp = 5; res = 15;
	break;
	case 5:
		cp = 6; res = 15;
	break;
	case 6:
		cp = 7; res = 15;
	break;
	case 7:
		cp = 13; res = 15;
	break;
	case 8:
		cp = 14; res = 15;
	break;
	case 9:
		cp = 15; res = 15;
	break;
	case 10:
		cp = 14; res = 7;
	break;
	case 11:
		cp = 15; res = 7;
	break;
	case 12 ... 13:
		cp = 15; res = 11;
	break;
	case 14:
		cp = 15; res = 13;
	break;
	case 15:
		cp = 15; res = 3;
	break;
	case 16 ... 17:
		cp = 14; res = 5;
	break;
	case 18 ... 19:
		cp = 15; res = 5;
	break;
	case 20 ... 21:
		cp = 15; res = 9;
		break;
	case 22 ... 23:
		cp = 14; res = 14;
		break;
	case 24 ... 26:
		cp = 15; res = 14;
		break;
	case 27 ... 28:
		cp = 14; res = 1;
		break;
	case 29 ... 33:
		cp = 15; res = 1;
		break;
	case 34 ... 37:
		cp = 14; res = 6;
		break;
	case 38 ... 44:
		cp = 15; res = 6;
		break;
	case 45 ... 57:
		cp = 15; res = 10;
		break;
	case 58 ... 63:
		cp = 13; res = 12;
		break;
	case 64 ... 70:
		cp = 14; res = 12;
		break;
	case 71 ... 86:
		cp = 15; res = 12;
		break;
	case 87 ... 93:
		cp = 14; res = 2;
		break;
	case 94:
		cp = 5; res = 15;
		break;
	case 95:
		cp = 6; res = 15;
		break;
	case 96:
		cp = 7; res = 15;
		break;
	case 97:
		cp = 13; res = 15;
		break;
	case 98:
		cp = 14; res = 15;
		break;
	case 99:
		cp = 15; res = 15;
		break;
	case 100:
		cp = 14; res = 7;
		break;
	case 101:
		cp = 15; res = 7;
		break;
	case 102 ... 103:
		cp = 15; res = 11;
		break;
	case 104:
		cp = 15; res = 13;
		break;
	case 105:
		cp = 15; res = 3;
		break;
	case 106 ... 107:
		cp = 14; res = 5;
		break;
	case 108 ... 109:
		cp = 15; res = 5;
		break;
	case 110 ... 111:
		cp = 15; res = 9;
		break;
	case 112 ... 113:
		cp = 14; res = 14;
		break;
	case 114 ... 116:
		cp = 15; res = 14;
		break;
	case 117 ... 118:
		cp = 14; res = 1;
		break;
	case 119 ... 123:
		cp = 15; res = 1;
		break;
	case 124 ... 127:
		cp = 14; res = 6;
		break;
	case 128 ... 134:
		cp = 15; res = 6;
		break;
	case 135 ... 147:
		cp = 15; res = 10;
		break;
	case 148 ... 153:
		cp = 13; res = 12;
		break;
	case 154 ... 160:
		cp = 14; res = 12;
		break;
	case 161 ... 176:
		cp = 15; res = 12;
		break;
	case 177 ... 183:
		cp = 14; res = 2;
		break;
	case 184 ... 200:
		cp = 14; res = 4;
		break;
	case 201 ... 273:
		cp = 15; res = 4;
		break;
	case 274 ... 300:
		cp = 13; res = 8;
		break;
	case 301 ... 325:
		cp = 14; res = 8;
		break;
	case 326 ... 432:
		cp = 15; res = 8;
		break;
	default:
		cp = 13; res = 8;
		break;
	}

	return ((res & 0xf) << 17) | ((cp & 0xf) | 0x160);
}

/**
 * xhdmiphy_mmcme5_lockreg12_enc - This function returns the DRP encoding of
 * lock Reg1 & Reg2 optimized for: Phase = 0; Dutycycle = 0.5; BW = low;
 * No Fractional division
 *
 * @mult:	mult is the divider to be encoded
 *
 * @return:	- [15:0] lock_1 Reg
 *		- [31:16] lock_2 Reg
 *
 * note: For more details about DRP encoding values , refer GT user guide
 */
static u32 xhdmiphy_mmcme5_lockreg12_enc(u16 mult)
{
	u16 lock_1, lock_2, lock_ref_dly, lock_fb_dly, lock_cnt;
	u16 lock_sat_high = 9;

	switch (mult) {
	case 4:
		lock_ref_dly = 4;
		lock_fb_dly = 4;
		lock_cnt = 1000;
		break;
	case 5:
		lock_ref_dly = 6;
		lock_fb_dly = 6;
		lock_cnt = 1000;
		break;
	case 6 ... 7:
		lock_ref_dly = 7;
		lock_fb_dly = 7;
		lock_cnt = 1000;
		break;
	case 8:
		lock_ref_dly = 9;
		lock_fb_dly = 9;
		lock_cnt = 1000;
		break;
	case 9 ... 10:
		lock_ref_dly = 10;
		lock_fb_dly = 10;
		lock_cnt = 1000;
		break;
	case 11:
		lock_ref_dly = 11;
		lock_fb_dly = 11;
		lock_cnt = 1000;
		break;
	case 12:
		lock_ref_dly = 13;
		lock_fb_dly = 13;
		lock_cnt = 1000;
		break;
	case 13 ... 14:
		lock_ref_dly = 14;
		lock_fb_dly = 14;
		lock_cnt = 1000;
		break;
	case 15:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 900;
		break;
	case 16 ... 17:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 825;
		break;
	case 18:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 750;
		break;
	case 19 ... 20:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 700;
		break;
	case 21:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 650;
		break;
	case 22 ... 23:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 625;
		break;
	case 24:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 575;
		break;
	case 25:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 550;
		break;
	case 26 ... 28:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 525;
		break;
	case 29 ... 30:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 475;
		break;
	case 31:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 450;
		break;
	case 32 ... 33:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 425;
		break;
	case 34 ... 36:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 400;
		break;
	case 37:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 375;
		break;
	case 38 ... 40:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 350;
		break;
	case 41 ... 43:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 325;
		break;
	case 44 ... 47:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 300;
		break;
	case 48 ... 51:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 275;
		break;
	case 52 ... 205:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 950;
		break;
	case 206 ... 432:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 925;
		break;
	default:
		lock_ref_dly = 16;
		lock_fb_dly = 16;
		lock_cnt = 250;
		break;
	}

	lock_1 = ((lock_fb_dly & 0x1f) << 10) | (lock_cnt & 0x3ff);

	lock_2 = ((lock_ref_dly & 0x1f) << 10) | (lock_sat_high & 0x3ff);

	return (lock_2 << 16) | lock_1;
}

/**
 * xhdmiphy_mmcme4_div_enc - This function returns the DRP encoding of
 * clkfbout_mult optimized for: Phase = 0; Dutycycle = 0.5; No Fractional
 * division The calculations are based on XAPP888
 *
 * @div_type:	div is the divider to be encoded
 * @div:	divider value
 *
 * @return:	- Encoded Value for clk_reg1 [31: 0]
 *		- Encoded Value for clk_reg2 [63:32]
 */
static u32 xhdmiphy_mmcme4_div_enc(enum mmcm_divs div_type, u8 div)
{
	u32 clk_reg1, clk_reg2;
	u8 hi_time, lo_time;

	if (div == 1) {
		clk_reg1 = 0x00001041;
		clk_reg2 = 0x00C00000;
	} else {
		hi_time = div / 2;
		lo_time = div - hi_time;
		clk_reg1 = lo_time & 0x3f;
		clk_reg1 |= (hi_time & 0x3f) << 6;
		clk_reg1 |= (div_type == XHDMIPHY_MMCM_DIVCLK_DIVIDE) ?
							0x0000 : 0x1000;
		clk_reg2 = (div % 2) ? 0x00800000 : 0x00000000;
	}

	return clk_reg2 | clk_reg1;
}

/**
 * xhdmiphy_mmcme4_filtreg1_enc - This function returns the drp encoding of
 * filtereg1 optimized for: phase = 0; dutycycle = 0.5; BW = low; no fractional
 * division
 *
 * @mult:	mult is the divider to be encoded
 *
 * @return:	- encoded value
 *
 * note: For more details about DRP encoding values , refer GT user guide
 */
static u16 xhdmiphy_mmcme4_filtreg1_enc(u8 mult)
{
	u16 drp_enc;

	switch (mult) {
	case 1: case 2: case 3:
	case 4: case 5: case 6:
	case 7: case 8: case 9:
	case 10: case 11: case 12:
	case 13: case 14:
	case 16:
		drp_enc = 0x0900;
		break;
	case 15:
		drp_enc = 0x1000;
		break;
		break;
	case 17: case 19: case 20:
	case 29: case 30: case 31:
	case 38: case 39: case 40:
	case 41: case 78: case 79:
	case 80: case 81: case 82:
	case 83: case 84:
	case 85:
		drp_enc = 0x9800;
		break;
	case 26: case 27: case 28:
	case 71: case 72: case 73:
	case 74: case 75: case 76:
	case 77: case 120: case 121:
	case 122: case 123: case 124:
	case 125: case 126: case 127:
	case 128:
		drp_enc = 0x9100;
		break;
	default:
		drp_enc = 0x9900;
		break;
	}

	return drp_enc;
}

/**
 * xhdmiphy_mmcme4_filtreg2_enc - This function returns the drp encoding of
 * filter reg1 optimized for: phase = 0; dutycycle = 0.5; bw = low; no
 * fractional division
 *
 * @mult:	mult is the divider to be encoded
 *
 * @return:	- encoded Value
 *
 * note: For more details about DRP encoding values , refer GT user guide
 */
static u16 xhdmiphy_mmcme4_filtreg2_enc(u8 mult)
{
	u16 drp_enc;

	switch (mult) {
	case 1: case 2:
		drp_enc = 0x9990;
	break;
	case 3:
		drp_enc = 0x9190;
		break;
	case 4:
		drp_enc = 0x1190;
		break;
	case 5:
		drp_enc = 0x8190;
		break;
	case 6: case 7:
		drp_enc = 0x9890;
		break;
	case 8:
		drp_enc = 0x0190;
		break;
	case 9: case 10: case 11:
	case 15: case 17: case 18:
		drp_enc = 0x1890;
		break;
	case 12: case 13: case 14:
	case 19: case 20: case 21:
	case 22: case 23: case 24:
	case 25:
		drp_enc = 0x8890;
		break;
	case 16: case 26: case 27:
	case 28: case 29: case 30:
	case 31: case 32: case 33:
	case 34: case 35: case 36:
	case 37:
		drp_enc = 0x9090;
		break;
	case 38: case 39: case 40:
	case 41: case 42: case 43:
	case 44: case 45: case 46:
	case 47: case 48: case 49:
	case 50: case 51: case 52:
	case 53: case 54: case 55:
	case 56: case 57: case 58:
	case 59: case 60: case 61:
	case 62:
		drp_enc = 0x0890;
		break;
	case 120: case 121: case 122:
	case 123: case 124: case 125:
	case 126: case 127: case 128:
		drp_enc = 0x8090;
		break;
	default:
		drp_enc = 0x1090;
		break;
	}

	return drp_enc;
}

static u16 xhdmiphy_mmcme4_lockreg1_enc(u8 mult)
{
	u16 drp_enc;

	if (mult >= 1 && mult <= 36)
		drp_enc = mmcme4_lockreg1_enc[mult];
	else
		drp_enc = 0x00fa;

	return drp_enc;
}

/**
 * xhdmiphy_mmcme4_lockreg2_enc - This function returns the drp encoding of
 * lockreg2 optimized for: phase = 0; dutycycle = 0.5; no fractional division
 *
 * @mult:	mult is the divider to be encoded
 *
 * @return:	- encoded Value
 */
static u16 xhdmiphy_mmcme4_lockreg2_enc(u8 mult)
{
	if (mult >= 1 && mult <= 10)
		return mmcme4_lockreg2_enc[mult];

	return mmcme4_lockreg2_enc[0];
}

static u16 xhdmiphy_mmcme4_lockreg3_enc(u8 mult)
{
	if (mult >= 1 && mult <= 10)
		return mmcme4_lockreg3_enc[mult];

	return mmcme4_lockreg3_enc[0];
}

/**
 * xhdmiphy_wr_mmcm5_params - This function will write the mixed-mode clock
 * manager (MMCM) values currently stored in the driver's instance structure to
 * hardware
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 *
 * @return:	- 0 if the MMCM write was successful
 *		- 1 otherwise, if the configuration success bit did not go low
 */
static bool xhdmiphy_wr_mmcm5_params(struct xhdmiphy_dev *inst, enum dir dir)
{
	struct xhdmiphy_mmcm *mmcm_params;
	u32 drp_val32;
	u16 drp_rdval;
	u8 chid;

	chid = (dir == XHDMIPHY_DIR_TX) ? XHDMIPHY_CHID_TXMMCM :
		       XHDMIPHY_CHID_RXMMCM;

	mmcm_params = &inst->quad.mmcm[dir];

	/* check parameters if has been initialized */
	if (!mmcm_params->divclk_divide && !mmcm_params->clkfbout_mult &&
	    !mmcm_params->clkout0_div && !mmcm_params->clkout1_div &&
	    !mmcm_params->clkout2_div)
		return 1;

	/* write CLKFBOUT_1 & CLKFBOUT_2 values */
	drp_val32 = xhdmiphy_mmcme5_div_enc(XHDMIPHY_MMCM_CLKFBOUT_MULT_F,
					    mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKFBOUT_1_REG,
		       (u16)(drp_val32 & XHDMIPHY_MMCM5_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKFBOUT_2_REG,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM5_WRITE_VAL));

	/* write DIVCLK_DIVIDE & DESKEW_2 values */
	drp_val32 = xhdmiphy_mmcme5_div_enc(XHDMIPHY_MMCM_DIVCLK_DIVIDE,
					    mmcm_params->divclk_divide);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_DIVCLK_DIVIDE_REG,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM5_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_DESKEW_REG,
		       ((mmcm_params->divclk_divide == 0) ? 0x0000 :
		       ((mmcm_params->divclk_divide % 2) ? 0x0400 : 0x0000)));

	/* write CLKOUT0_1 & CLKOUT0_2 values */
	drp_val32 = xhdmiphy_mmcme5_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout0_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT0_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM5_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT0_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM5_WRITE_VAL));

	/* write CLKOUT1_1 & CLKOUT1_2 values */
	drp_val32 = xhdmiphy_mmcme5_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout1_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT1_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM5_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT1_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM5_WRITE_VAL));

	/* write CLKOUT2_1 & CLKOUT2_2 values */
	drp_val32 = xhdmiphy_mmcme5_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout2_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT2_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM5_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CLKOUT2_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM5_WRITE_VAL));

	/* write CP & RES values */
	drp_val32 = xhdmiphy_mmcme5_cpres_enc(mmcm_params->clkfbout_mult);
	drp_rdval = xhdmiphy_drprd(inst, chid, XHDMIPHY_MMCM5_DRP_CP_REG1,
				   &drp_rdval);
	drp_rdval &= ~(XHDMIPHY_MMCM5_CP_RES_MASK);

	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_CP_REG1,
		       (u16)((drp_val32 & XHDMIPHY_MMCM5_CP_RES_MASK) |
			     drp_rdval));

	drp_rdval = xhdmiphy_drprd(inst, chid, XHDMIPHY_MMCM5_DRP_RES_REG1,
				   &drp_rdval);
	drp_rdval &= ~(XHDMIPHY_MMCM5_RES_MASK);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_RES_REG1,
		       (u16)(((drp_val32 >> 15) & XHDMIPHY_MMCM5_RES_MASK) |
			      drp_rdval));

	/* write lock reg1 & reg2 values */
	drp_val32 = xhdmiphy_mmcme5_lockreg12_enc(mmcm_params->clkfbout_mult);
	/* lock_1 */
	drp_rdval = xhdmiphy_drprd(inst, chid, XHDMIPHY_MMCM5_DRP_LOCK_REG1,
				   &drp_rdval);
	drp_rdval &= ~(XHDMIPHY_MMCM5_LOCK1_MASK1);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_LOCK_REG1,
		       (u16)((drp_val32 & XHDMIPHY_MMCM5_LOCK1_MASK2) |
			     drp_rdval));

	/* lock_2 */
	drp_rdval = xhdmiphy_drprd(inst, chid, XHDMIPHY_MMCM5_DRP_LOCK_REG2,
				   &drp_rdval);
	drp_rdval &= ~(XHDMIPHY_MMCM5_LOCK1_MASK1);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM5_DRP_LOCK_REG2,
		       (u16)(((drp_val32 >> 16) & XHDMIPHY_MMCM5_LOCK1_MASK2) |
			     drp_rdval));

	return 0;
}

/**
 * xhdmiphy_wr_mmcm4_params - This function will write the mixed-mode clock
 * manager (mmcm) values currently stored in the driver's instance structure
 * to hardware .
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for tX or rX
 *
 * @return:	- 0 if the mmcm write was successful
 *		- 1 otherwise, if the configuration success bit did not go low
 */
static bool xhdmiphy_wr_mmcm4_params(struct xhdmiphy_dev *inst, enum dir dir)
{
	struct xhdmiphy_mmcm *mmcm_params;
	u32 drp_val32;
	u16 drp_val;
	u8 chid;

	chid = (dir == XHDMIPHY_DIR_TX) ? XHDMIPHY_CHID_TXMMCM :
					  XHDMIPHY_CHID_RXMMCM;
	mmcm_params = &inst->quad.mmcm[dir];

	if (!mmcm_params->divclk_divide && !mmcm_params->clkfbout_mult &&
	    !mmcm_params->clkout0_div && !mmcm_params->clkout1_div &&
	    !mmcm_params->clkout2_div) {
		return 1;
	}

	/* write power register Value */
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_PWR_REG,
		       XHDMIPHY_MMCM4_WRITE_VAL);

	/* write CLKFBOUT reg1 & reg2 values */
	drp_val32 = xhdmiphy_mmcme4_div_enc(XHDMIPHY_MMCM_CLKFBOUT_MULT_F,
					    mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKFBOUT_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM4_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKFBOUT_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM4_WRITE_VAL));

	/* write gg value */
	drp_val32 = xhdmiphy_mmcme4_div_enc(XHDMIPHY_MMCM_DIVCLK_DIVIDE,
					    mmcm_params->divclk_divide);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DIVCLK_DIV_REG,
		       (u16)(drp_val32 & XHDMIPHY_MMCM4_WRITE_VAL));

	/* write CLKOUT0 reg1 & reg2 values */
	drp_val32 = xhdmiphy_mmcme4_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout0_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT0_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM4_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT0_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM4_WRITE_VAL));

	drp_val32 = xhdmiphy_mmcme4_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout1_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT1_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM4_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT1_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM4_WRITE_VAL));

	drp_val32 = xhdmiphy_mmcme4_div_enc(XHDMIPHY_MMCM_CLKOUT_DIVIDE,
					    mmcm_params->clkout2_div);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT2_REG1,
		       (u16)(drp_val32 & XHDMIPHY_MMCM4_WRITE_VAL));
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_CLKOUT2_REG2,
		       (u16)((drp_val32 >> 16) & XHDMIPHY_MMCM4_WRITE_VAL));

	drp_val = xhdmiphy_mmcme4_lockreg1_enc(mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DRP_LOCK_REG1, drp_val);

	drp_val = xhdmiphy_mmcme4_lockreg2_enc(mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DRP_LOCK_REG2, drp_val);

	drp_val = xhdmiphy_mmcme4_lockreg3_enc(mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DRP_LOCK_REG3, drp_val);

	drp_val = xhdmiphy_mmcme4_filtreg1_enc(mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DRP_FILTER_REG1, drp_val);

	drp_val = xhdmiphy_mmcme4_filtreg2_enc(mmcm_params->clkfbout_mult);
	xhdmiphy_drpwr(inst, chid, XHDMIPHY_MMCM4_DRP_FILTER_REG2, drp_val);

	return 0;
}

void xhdmiphy_mmcm_start(struct xhdmiphy_dev *inst, enum dir dir)
{
	struct xhdmiphy_mmcm *mmcm_ptr;

	if (dir == XHDMIPHY_DIR_RX)
		mmcm_ptr = &inst->quad.rx_mmcm;
	else
		mmcm_ptr = &inst->quad.tx_mmcm;

	/* check values if valid */
	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if (!(mmcm_ptr->clkout0_div > 0 &&
		      mmcm_ptr->clkout0_div <= 128 &&
		      mmcm_ptr->clkout1_div > 0 &&
		      mmcm_ptr->clkout1_div <= 128 &&
		      mmcm_ptr->clkout2_div > 0 &&
		      mmcm_ptr->clkout2_div <= 128))
			return;
	} else {
		if (!(mmcm_ptr->clkout0_div > 0 &&
		      mmcm_ptr->clkout0_div <= 432 &&
		      mmcm_ptr->clkout1_div > 0 &&
		      mmcm_ptr->clkout1_div <= 432 &&
		      mmcm_ptr->clkout2_div > 0 &&
		      mmcm_ptr->clkout2_div <= 432))
			return;
	}

	xhdmiphy_mmcm_reset(inst, dir, true);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5)
		xhdmiphy_wr_mmcm4_params(inst, dir);
	else
		xhdmiphy_wr_mmcm5_params(inst, dir);

	xhdmiphy_mmcm_reset(inst, dir, false);
	xhdmiphy_mmcm_lock_en(inst, dir, false);
}

/**
 * xhdmiphy_mmcm_lock_en - This function will reset the mixed-mode clock
 * manager (MMCM) core.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @enable:	Enable is an indicator whether to "Enable" the locked mask
 *		if set to 1. If set to 0: reset, then disable
 */
void xhdmiphy_mmcm_lock_en(struct xhdmiphy_dev *inst, enum dir dir, u8 enable)
{
	u32 reg_off, reg_val;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_MMCM_TXUSRCLK_CTRL_REG;
	else
		reg_off = XHDMIPHY_MMCM_RXUSRCLK_CTRL_REG;

	/* Assert reset */
	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val |= XHDMIPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK;
	xhdmiphy_write(inst, reg_off, reg_val);

	if (!enable) {
		/* De-assert reset */
		reg_val &= ~XHDMIPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK;
		xhdmiphy_write(inst, reg_off, reg_val);
	}
}

/**
 * xhdmiphy_mmcm_param - This function sets the MMCM programming values based
 * on max rate
 *
 * @inst:	inst is a pointer to the Hdmiphy core instance.
 * @dir:	dir is an indicator for RX or TX.
 *
 * @return:	none
 *
 * note:	based on the MAX Rate set in the GUI, the MMCM values are
 *		selected.
 *		3 -> Max Rate selected in GUI is 3G
 *		6 -> Max Rate selected in GUI is 6G
 *		8 -> Max Rate selected in GUI is 8G
 *		10 -> Max Rate selected in GUI is 10G
 *		12 -> Max Rate selected in GUI in 12G
 */
void xhdmiphy_mmcm_param(struct xhdmiphy_dev *inst, enum dir dir)
{
	struct xhdmiphy_mmcm *mmcm_ptr, *conf;
	u8 maxrate, num;

	if (dir == XHDMIPHY_DIR_RX) {
		mmcm_ptr = &inst->quad.rx_mmcm;
		maxrate = inst->conf.rx_maxrate;
		num = inst->conf.rx_channels;
	} else {
		mmcm_ptr = &inst->quad.tx_mmcm;
		maxrate = inst->conf.tx_maxrate;
		num = inst->conf.tx_channels;
	}

	conf = get_mmcm_conf(inst);

	switch (maxrate) {
	case 3:
		mmcm_ptr->clkfbout_mult = conf[0].clkfbout_mult;
		mmcm_ptr->divclk_divide = conf[0].divclk_divide;
		mmcm_ptr->clkout0_div = conf[0].clkout0_div;
		mmcm_ptr->clkout1_div = conf[0].clkout1_div;
		mmcm_ptr->clkout2_div = conf[0].clkout2_div;
		break;
	case 6:
		if (num == 3) {
			mmcm_ptr->clkfbout_mult = conf[1].clkfbout_mult;
			mmcm_ptr->divclk_divide = conf[1].divclk_divide;
			mmcm_ptr->clkout0_div = conf[1].clkout0_div;
			mmcm_ptr->clkout1_div = conf[1].clkout1_div;
			mmcm_ptr->clkout2_div = conf[1].clkout2_div;
		} else {
			mmcm_ptr->clkfbout_mult = conf[2].clkfbout_mult;
			mmcm_ptr->divclk_divide = conf[2].divclk_divide;
			mmcm_ptr->clkout0_div = conf[2].clkout0_div;
			mmcm_ptr->clkout1_div = conf[2].clkout1_div;
			mmcm_ptr->clkout2_div = conf[2].clkout2_div;
		}
		break;
	case 8:
		mmcm_ptr->clkfbout_mult = conf[3].clkfbout_mult;
		mmcm_ptr->divclk_divide = conf[3].divclk_divide;
		mmcm_ptr->clkout0_div = conf[3].clkout0_div;
		mmcm_ptr->clkout1_div = conf[3].clkout1_div;
		mmcm_ptr->clkout2_div = conf[3].clkout2_div;
		break;
	case 10:
		mmcm_ptr->clkfbout_mult = conf[4].clkfbout_mult;
		mmcm_ptr->divclk_divide = conf[4].divclk_divide;
		mmcm_ptr->clkout0_div = conf[4].clkout0_div;
		mmcm_ptr->clkout1_div = conf[4].clkout1_div;
		mmcm_ptr->clkout2_div = conf[4].clkout2_div;
		break;
	case 12:
		mmcm_ptr->clkfbout_mult = conf[5].clkfbout_mult;
		mmcm_ptr->divclk_divide = conf[5].divclk_divide;
		mmcm_ptr->clkout0_div = conf[5].clkout0_div;
		mmcm_ptr->clkout1_div = conf[5].clkout1_div;
		mmcm_ptr->clkout2_div = conf[5].clkout2_div;
		break;
	default:
		break;
	}
}

static void xhdmiphy_set_clkout2_div(struct xhdmiphy_dev *inst, enum dir dir,
				     u64 linerate, struct xhdmiphy_mmcm *mmcm_ptr)
{
	/* Only do this when the clkout2_div has been set */
	if (mmcm_ptr->clkout2_div) {
		if (dir == XHDMIPHY_DIR_RX) {
			/* Correct divider value if TMDS clock ratio is 1/40 */
			if (inst->rx_tmdsclock_ratio) {
				if ((mmcm_ptr->clkout2_div % 4) == 0) {
					mmcm_ptr->clkout2_div =
					mmcm_ptr->clkout2_div / 4;
				} else {
				/*
				 * Not divisible by 4: repeat
				 * loop with a lower multiply
				 * value
				 */
					if (inst->conf.gt_type != XHDMIPHY_GTYE5)
						mmcm_ptr->clkout2_div = 255;
					else
						mmcm_ptr->clkout2_div = 65535;
				}
			}
		}
		/* TX */
		else if ((((linerate / 1000000) >= XHDMIPHY_LRATE_3400) &&
			  (inst->tx_samplerate == 1)) ||
			  (((linerate / 1000000) / inst->tx_samplerate) >=
			    XHDMIPHY_LRATE_3400)) {
			if ((mmcm_ptr->clkout2_div % 4) == 0) {
				mmcm_ptr->clkout2_div = mmcm_ptr->clkout2_div / 4;
			} else {
			/*
			 * Not divisible by 4: repeat loop with
			 * a lower multiply value
			 */
				if (inst->conf.gt_type != XHDMIPHY_GTYE5)
					mmcm_ptr->clkout2_div = 255;
				else
					mmcm_ptr->clkout2_div = 65535;
			}
		}
	}
}

/**
 * xhdmiphy_cal_mmcm_param - This function calculates the HDMI mmcm parameters.
 *
 * @inst:	inst is a pointer to the Hdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 * @ppc:	ppc specifies the total number of pixels per clock
 *		- 1 = XVIDC_PPC_1
 *		- 2 = XVIDC_PPC_2
 *		- 4 = XVIDC_PPC_4
 * @bpc:	bpc specifies the color depth/bits per color component
 *		- 6 = XVIDC_BPC_6
 *		- 8 = XVIDC_BPC_8
 *		- 10 = XVIDC_BPC_10
 *		- 12 = XVIDC_BPC_12
 *		- 16 = XVIDC_BPC_16
 *
 * @return:	- 0 if calculated PLL parameters updated successfully
 *		- 1 if parameters not updated
 */
u32 xhdmiphy_cal_mmcm_param(struct xhdmiphy_dev *inst, enum chid chid,
			    enum dir dir, enum ppc ppc, enum color_depth bpc)
{
	struct xhdmiphy_mmcm *mmcm_ptr;
	enum pll_type pll_type;
	u64 linerate = 0;
	u32 refclk, div, mult;
	u16 mult_div;
	u8 valid;

	pll_type = xhdmiphy_get_pll_type(inst, dir, XHDMIPHY_CHID_CH1);

	switch (pll_type) {
	case XHDMIPHY_PLL_QPLL:
	case XHDMIPHY_PLL_QPLL0:
	case XHDMIPHY_PLL_LCPLL:
		linerate = inst->quad.cmn0.linerate;
		break;
	case XHDMIPHY_PLL_QPLL1:
	case XHDMIPHY_PLL_RPLL:
		linerate = inst->quad.cmn1.linerate;
		break;
	default:
		linerate = inst->quad.ch1.linerate;
		break;
	}

	if (((linerate / 1000000) > 2970) && ppc == XVIDC_PPC_1) {
		dev_err(inst->dev, "ppc not supported\n");
		return 1;
	}

	div = 1;
	do {
		if (dir == XHDMIPHY_DIR_RX) {
			refclk = inst->rx_refclk_hz;
			mmcm_ptr = &inst->quad.rx_mmcm;
			refclk = refclk / (get_gthdmi_ptr(inst))->rx_mmcm_scale;
			mult = (get_gthdmi_ptr(inst))->rx_mmcm_fvcomax * div / refclk;
		} else {
			refclk = inst->tx_refclk_hz;
			mmcm_ptr = &inst->quad.tx_mmcm;
			refclk = refclk / (get_gthdmi_ptr(inst))->tx_mmcm_scale;
			mult = (get_gthdmi_ptr(inst))->tx_mmcm_fvcomax * div / refclk;
		}

		/* return if refclk is below valid range */
		if (refclk < 20000000)
			return (1);

		/* in case of 4 pixels per clock, the M must be a multiple of four */
		if (ppc == XVIDC_PPC_4) {
			mult = mult / 4;
			mult = mult * 4;
		} else if (ppc == XVIDC_PPC_2) {
		/* else the M must be a multiple of two */
			mult = mult / 2;
			mult = mult * 2;
		}

		valid = (false);
		do {
			mult_div = mult / div;
			mmcm_ptr->clkfbout_mult = mult;
			mmcm_ptr->divclk_divide = div;
			if (inst->conf.transceiver_width == 4) {
				/* link clock: TMDS clock ratio 1/40 */
				if ((linerate / 1000000) >= XHDMIPHY_LRATE_3400) {
					if (dir == XHDMIPHY_DIR_TX &&
					    (((linerate / 1000000) / inst->tx_samplerate) < 3400)) {
						mmcm_ptr->clkout0_div = mult_div * 4;
					} else {
						mmcm_ptr->clkout0_div = mult_div;
					}
				} else {
				/* link clock: TMDS clock ratio 1/10 */
					mmcm_ptr->clkout0_div = mult_div * 4;
				}
			} else {
				/* 2 Byte Mode */
				/* link clock: TMDS clock ratio 1/40 */
				if ((linerate / 1000000) >= XHDMIPHY_LRATE_3400) {
					if (dir == XHDMIPHY_DIR_TX &&
					    (((linerate / 1000000) / inst->tx_samplerate) < 3400)) {
						mmcm_ptr->clkout0_div = mult_div * 2;
					} else {
						mmcm_ptr->clkout0_div = mult_div / 2;
					}
				} else {
				/* link clock: TMDS clock ratio 1/10 */
					mmcm_ptr->clkout0_div = mult_div * 2;
				}
			}
			/* TMDS clock */
			mmcm_ptr->clkout1_div = mult_div *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1);
			/* video clock */
			mmcm_ptr->clkout2_div = 0;
			switch (bpc) {
			case XVIDC_BPC_10:
				/* quad pixel */
				if (ppc == (XVIDC_PPC_4)) {
					mmcm_ptr->clkout2_div = (mult_div * 5 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				/* dual pixel */
				} else if (ppc == (XVIDC_PPC_2)) {
					/*
					 * The clock ratio is 2.5. The PLL only
					 * supports integer value. The mult_div
					 * must be dividable by two
					 * (2 * 2.5 = 5) to get an integer
					 * number
					 */
					if ((mult_div % 2) == 0) {
						mmcm_ptr->clkout2_div =
							(mult_div * 5 / 2 *
							((dir == XHDMIPHY_DIR_TX) ?
							(inst->tx_samplerate) : 1));
					}
				/* single pixel */
				} else {
					/*
					 * The clock ratio is 1.25. The pll only
					 * supports integer values The multDiv
					 * must be dividable by four
					 * (4 * 1.25 = 5) to get an integer
					 * number
					 */
					if ((mult_div % 4) == 0) {
						mmcm_ptr->clkout2_div = (mult_div * 5 / 4 *
							((dir == XHDMIPHY_DIR_TX) ?
							(inst->tx_samplerate) : 1));
					}
				}
				break;
			case XVIDC_BPC_12:
				/* quad pixel */
				if (ppc == (XVIDC_PPC_4)) {
					mmcm_ptr->clkout2_div = (mult_div * 6 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				} else if (ppc == (XVIDC_PPC_2)) {
				/* dual pixel */
					mmcm_ptr->clkout2_div = (mult_div * 3 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				/* single pixel */
				} else {
					/*
					 * The clock ratio is 1.5. The PLL only
					 * supports integer values. The mult_div
					 * must be dividable by two (2 * 1.5 = 3)
					 * to get an integer number
					 */
					if ((mult_div % 2) == 0) {
						mmcm_ptr->clkout2_div = (mult_div * 3 / 2 *
							((dir == XHDMIPHY_DIR_TX) ?
							(inst->tx_samplerate) : 1));
					}
				}
				break;
			case XVIDC_BPC_16:
				/* quad pixel */
				if (ppc == (XVIDC_PPC_4)) {
					mmcm_ptr->clkout2_div = (mult_div * 8 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				} else if (ppc == (XVIDC_PPC_2)) {
				/* dual pixel */
					mmcm_ptr->clkout2_div = (mult_div * 4 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				} else {
				/* single pixel */
					mmcm_ptr->clkout2_div = (mult_div * 2 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				}
				break;
			case XVIDC_BPC_8:
			default:
				/* quad pixel */
				if (ppc == (XVIDC_PPC_4)) {
					mmcm_ptr->clkout2_div = (mult_div * 4 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				} else if (ppc == (XVIDC_PPC_2)) {
				/* dual pixel */
					mmcm_ptr->clkout2_div = (mult_div * 2 *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				} else {
				/* single pixel */
					mmcm_ptr->clkout2_div = (mult_div *
						((dir == XHDMIPHY_DIR_TX) ?
						(inst->tx_samplerate) : 1));
				}
				break;
			}
			xhdmiphy_set_clkout2_div(inst, dir, linerate, mmcm_ptr);

			/* Check values */
			if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
				if (mmcm_ptr->clkout0_div > 0 &&
				    mmcm_ptr->clkout0_div <= 128 &&
				    mmcm_ptr->clkout1_div > 0 &&
				    mmcm_ptr->clkout1_div <= 128 &&
				    mmcm_ptr->clkout2_div > 0 &&
				    mmcm_ptr->clkout2_div <= 128) {
					valid = (true);
				} else {
					/* 4 pixels per clock */
					if (ppc == (XVIDC_PPC_4)) {
						/* Decrease mult value */
						mult -= 4;
					} else if (ppc == (XVIDC_PPC_2)) {
					/* 2 pixels per clock */
						/* Decrease M value */
						mult -= 2;
					} else {
						/* 1 pixel per clock */
						/* Decrease M value */
						mult -= 1;
					}
				}
			} else {
				if (mmcm_ptr->clkout0_div > 0 &&
				    mmcm_ptr->clkout0_div <= 511 &&
				    mmcm_ptr->clkout1_div > 0 &&
				    mmcm_ptr->clkout1_div <= 511 &&
				    mmcm_ptr->clkout2_div > 0 &&
				    mmcm_ptr->clkout2_div <= 511) {
					valid = (true);
				} else { /* 4 pixels per clock */
					if (ppc == (XVIDC_PPC_4)) {
						/* Decrease mult value */
						mult -= 4;
					} else if (ppc == (XVIDC_PPC_2)) {
						/* 2 pixels per clock */
						/* Decrease M value */
						mult -= 2;
					} else {
						/* 1 pixel per clock */
						/* Decrease M value */
						mult -= 1;
					}
				}
			}
		} while (!valid && (mult > 0) && (mult < 129));

		/* Increment divider */
		div++;
	} while (!valid && (div > 0) && (div < 107));

	if (valid)
		return 0;

	dev_err(inst->dev, "failed to caliculate mmmcm params\n");

	return 1;
}

/**
 * xhdmiphy_clk_cal_params - This function will try to find the necessary PLL
 * divisor values to produce the configured line rate given the specified PLL
 * input frequency. This will be done for all channels specified by chid.
 * This function is a wrapper for xhdmiphy_pll_cal.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance.
 * @chid:		chid is the channel ID to calculate the PLL values for.
 * @dir:		dir is an indicator for TX or RX.
 * @pll_in_freq:	pll_clkin_freq is the PLL input frequency on which to
 *			base the calculations on. A value of 0 indicates to use
 *			the currently configured quad PLL reference clock.
 *			A non-zero value indicates to ignore what is currently
 *			configured in SW, and use a custom frequency instead.
 *
 * @return:		- 0 if valid PLL values were found to satisfy the
 *			constraints
 *			- 1 otherwise
 */
static u32 xhdmiphy_clk_cal_params(struct xhdmiphy_dev *inst, enum chid chid,
				   enum dir dir, u32 pll_in_freq)
{
	u32 status = 0;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);

	for (id = id0; id <= id1; id++) {
		status = xhdmiphy_pll_cal(inst, (enum chid)id, dir,
					  pll_in_freq);
		if (status != 0)
			return status;
	}

	return status;
}

/**
 * xhdmiphy_qpll_param - This function calculates the qpll parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:
 *		- 0 if calculated QPLL parameters updated
 *			successfully
 *		- 1 if parameters not updated
 */
u32 xhdmiphy_qpll_param(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir)
{
	enum sysclk_data_sel sysclk_data_sel = 0;
	enum chid act_cmnid = XHDMIPHY_CHID_CMN0;
	enum sysclk_outsel sysclk_out_sel = 0;
	u64 refclk = 0, tx_linerate = 0;
	u32 *refclk_ptr;
	u32 qpll_clkmin = 0;
	u32 status, qpll_refclk;
	u8 sr_arr[] = {1, 3, 5};
	u8 sr_index, sr_val, id, id0, id1;

	/* determine qpll reference clock from the first (master) channel */
	if (dir == XHDMIPHY_DIR_RX) {
		qpll_refclk = inst->rx_refclk_hz;
		refclk_ptr = &inst->rx_refclk_hz;
	} else {
		qpll_refclk = inst->tx_refclk_hz;
		refclk_ptr = &inst->tx_refclk_hz;
	}

	if (inst->conf.gt_type == XHDMIPHY_GTHE4) {
		/* determine which QPLL to use */
		if ((qpll_refclk >= 102343750 && qpll_refclk <= 122500000) ||
		    (qpll_refclk >= 204687500 && qpll_refclk <= 245000000) ||
		    (qpll_refclk >= 409375000 && qpll_refclk <= 490000000)) {
			sysclk_data_sel = XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK;
			sysclk_out_sel = XHDMIPHY_SYSCLKSELOUT_QPLL1_REFCLK;
			act_cmnid = XHDMIPHY_CHID_CMN1;
			qpll_clkmin = (u32)XHDMIPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN;
		} else {
			sysclk_data_sel = XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK;
			sysclk_out_sel = XHDMIPHY_SYSCLKSELOUT_QPLL0_REFCLK;
			act_cmnid = XHDMIPHY_CHID_CMN0;
			qpll_clkmin = (u32)XHDMIPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN;
		}
	} else if (inst->conf.gt_type == XHDMIPHY_GTYE4) {
		if ((qpll_refclk >= 102343750 && qpll_refclk <= 122500000) ||
		    (qpll_refclk >= 204687500 && qpll_refclk <= 245000000) ||
		    (qpll_refclk >= 409375000 && qpll_refclk <= 490000000)) {
			sysclk_data_sel = XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK;
			sysclk_out_sel = XHDMIPHY_SYSCLKSELOUT_QPLL1_REFCLK;
			act_cmnid = XHDMIPHY_CHID_CMN1;
			qpll_clkmin = (u32)XHDMIPHY_HDMI_GTYE4_QPLL1_REFCLK_MIN;
		} else {
			sysclk_data_sel = XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK;
			sysclk_out_sel = XHDMIPHY_SYSCLKSELOUT_QPLL0_REFCLK;
			act_cmnid = XHDMIPHY_CHID_CMN0;
			qpll_clkmin = (u32)XHDMIPHY_HDMI_GTYE4_QPLL0_REFCLK_MIN;
		}
	}

	/* update qpll clock selections */
	xhdmiphy_sysclk_data_sel(inst, dir, sysclk_data_sel);
	xhdmiphy_sysclk_out_sel(inst, dir, sysclk_out_sel);

	/* rx is using qpll */
	if (dir == XHDMIPHY_DIR_RX) {
		/*
		 * check if the reference clock is not below the minimum qpll
		 * input frequency
		 */
		if (qpll_refclk >= qpll_clkmin) {
			refclk = qpll_refclk;
			/* Scaled line rate */
			if (inst->rx_hdmi21_cfg.is_en == 0) {
				if (inst->rx_tmdsclock_ratio)
					xhdmiphy_cfg_linerate(inst,
							      XHDMIPHY_CHID_CMNA,
							      (refclk * 40));
				else
					xhdmiphy_cfg_linerate(inst,
							      XHDMIPHY_CHID_CMNA,
							      (refclk * 10));
			} else {
				xhdmiphy_cfg_linerate(inst,
						      XHDMIPHY_CHID_CMNA,
						      inst->rx_hdmi21_cfg.linerate);
			}
			/* clear DRU is enabled flag */
			inst->rx_dru_enabled = 0;

			/* Set RX data width */
			xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
			for (id = id0; id <= id1; id++) {
				if (inst->conf.transceiver_width == 2) {
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 20;
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 2;
				} else {
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 40;
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 4;
				}
			}
		} else if (inst->conf.dru_present) {
			refclk = xhdmiphy_get_dru_refclk(inst);
			/* check DRU frequency */
			if (refclk == 1) {
				dev_err(inst->dev, "cannot get dru refclk\n");
				return 1;
			}

			/* round input frequency to 10 kHz */
			refclk = (refclk + 5000) / 10000;
			refclk = refclk * 10000;

			/* wet the DRU to operate at a linerate of 2.5 Gbps */
			xhdmiphy_cfg_linerate(inst, XHDMIPHY_CHID_CMNA,
					      (get_gthdmi_ptr(inst))->dru_linerate);

			/* set DRU is enabled flag */
			inst->rx_dru_enabled = 1;

			/* set RX data width to 40 and 4 bytes */
			xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
			for (id = id0; id <= id1; id++) {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 20;
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 2;
			}
		} else {
			dev_err(inst->dev, "dru is not present\n");
			return 1;
		}
	/* TX is using QPLL */
	} else {
		/* Set default TX sample rate */
		inst->tx_samplerate = 1;

		if (inst->tx_hdmi21_cfg.is_en == 0) {
			/* Update TX line rates */
			xhdmiphy_cfg_linerate(inst, XHDMIPHY_CHID_CMNA,
					      ((u64)(*refclk_ptr) * 10));
			tx_linerate = (*refclk_ptr) / 100000;

			/* Check if the linerate is above the 340 Mcsc */
			if ((tx_linerate) >= XHDMIPHY_LRATE_3400)
				(*refclk_ptr) = (*refclk_ptr) / 4;
		} else { /* inst->tx_hdmi21_cfg.is_en == 1 */
			xhdmiphy_cfg_linerate(inst, XHDMIPHY_CHID_CMNA,
					      inst->tx_hdmi21_cfg.linerate);
		}
	}

	/* Calculate QPLL values */
	for (sr_index = 0; sr_index < sizeof(sr_arr); sr_index++) {
		/* Only use oversampling when then TX is using the QPLL */
		if (dir == XHDMIPHY_DIR_TX) {
			sr_val = sr_arr[sr_index];

			if (inst->tx_hdmi21_cfg.is_en == 0) {
				/*
				 * TX reference clock is below the minimum QPLL
				 * clock input frequency
				 */
				if ((*refclk_ptr) < qpll_clkmin) {
					refclk = ((*refclk_ptr) * sr_val);

					/* Calculate scaled line rate */
					if (tx_linerate >= XHDMIPHY_LRATE_3400) {
						xhdmiphy_cfg_linerate(inst,
								      XHDMIPHY_CHID_CMNA,
								      (u64)(refclk * 40));
					} else {
						xhdmiphy_cfg_linerate(inst,
								      XHDMIPHY_CHID_CMNA,
								      (u64)(refclk * 10));
					}
				} else {
					/*
					 * TX reference clock is in QPLL clock input range.
					 * In this case don't increase the reference clock, but
					 * increase the line rate.
					 */
					refclk = (*refclk_ptr);

					/* Calculate scaled line rate */
					if (tx_linerate >= XHDMIPHY_LRATE_3400) {
						xhdmiphy_cfg_linerate(inst,
								      XHDMIPHY_CHID_CMNA,
								      (u64)(refclk * 40 * sr_val));
					} else {
						xhdmiphy_cfg_linerate(inst,
								      XHDMIPHY_CHID_CMNA,
								      (u64)(refclk * 10 * sr_val));
					}
				}
			} else { /* inst->tx_hdmi21_cfg.is_en == 1 */
				refclk = (*refclk_ptr);
			}
		} else {
			/* For all other reference clocks force sample rate to one */
			sr_val = 1;
		}

		status = xhdmiphy_clk_cal_params(inst, act_cmnid, dir, refclk);
		if (status == 0) {
			/* Only execute when the TX is using the QPLL */
			if (dir == XHDMIPHY_DIR_TX) {
				/* Set TX sample rate */
				inst->tx_samplerate = sr_val;

				/*
				 * Update reference clock only when the
				 * reference clock is below the minimum QPLL
				 * input frequency.
				 */
				if ((*refclk_ptr) < qpll_clkmin) {
					(*refclk_ptr) = (*refclk_ptr) * sr_val;
				} else if (sr_val > 1) {
					dev_err(inst->dev,
						"failed to configure qpll params\n");
					return 1;
				}
			}
			/*
			 * Check Userclock Frequency (300 MHz + 0.5%) + 10 KHz
			 * (Clkdet accuracy)
			 */
			if (301500000 < (xhdmiphy_get_linerate(inst, act_cmnid) /
					 (inst->conf.transceiver_width * 10))) {
				dev_err(inst->dev, "user clock error\n");
				return 1;
			}

			return 0;
		}
	}

	dev_err(inst->dev, "failed to configure qpll params\n");
	return 1;
}

/**
 * xhdmiphy_cpll_param - This function calculates the CPLL parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	- 0 if calculated CPLL parameters updated
 *		  successfully
 *		- 1 if parameters not updated
 */
u32 xhdmiphy_cpll_param(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir)
{
	enum chid ch_id = XHDMIPHY_CHID_CHA;
	u64 refclk = 0;
	u32 *refclk_ptr;
	u32 tx_linerate = 0;
	u32 status;
	u8 sr_arr[] = {1, 3, 5};
	u8 sr_index, sr_val, id, id0, id1;

	/* tx is using cpll */
	if (dir == XHDMIPHY_DIR_TX) {
		/* set default tx sample rate */
		inst->tx_samplerate = 1;
		refclk_ptr = &inst->tx_refclk_hz;
		if (inst->tx_hdmi21_cfg.is_en == 0) {
			xhdmiphy_cfg_linerate(inst, ch_id,
					      (u64)((*refclk_ptr) * 10));
			tx_linerate = (*refclk_ptr)  / 100000;
			/* check if the line rate is above the 340 Mcsc */
			if (tx_linerate >= XHDMIPHY_LRATE_3400)
				(*refclk_ptr) = (*refclk_ptr) / 4;
		} else {
			xhdmiphy_cfg_linerate(inst, ch_id,
					      (u64)(inst->tx_hdmi21_cfg.linerate));
			tx_linerate = inst->tx_hdmi21_cfg.linerate / 100000;
		}
	} else {
		/* rx is using cpll */
		refclk_ptr = &inst->rx_refclk_hz;
		/*
		 * check if the reference clock is not below the minimum CPLL
		 * input frequency
		 */
		if ((*refclk_ptr) >= (get_gthdmi_ptr(inst))->cpll_refclk_min) {
			refclk = (*refclk_ptr);
			/* scaled linerate */
			if (inst->rx_hdmi21_cfg.is_en == 0) {
				if (inst->rx_tmdsclock_ratio) {
					xhdmiphy_cfg_linerate(inst, ch_id,
							      (refclk * 40));
				} else {
					xhdmiphy_cfg_linerate(inst, ch_id,
							      (refclk * 10));
				}
			} else { /* inst->rx_hdmi21_cfg.is_en == 1 */
				xhdmiphy_cfg_linerate(inst, ch_id,
						      inst->rx_hdmi21_cfg.linerate);
			}

			inst->rx_dru_enabled = 0;
			xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0,
					&id1);
			for (id = id0; id <= id1; id++) {
				if (inst->conf.transceiver_width == 2) {
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 20;
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 2;
				} else {
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 40;
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 4;
				}
			}
		} else {
			if (inst->conf.dru_present) {
				/* Return config not found error when TMDS ratio is 1/40 */
				if (inst->rx_tmdsclock_ratio) {
					dev_err(inst->dev, "cpll config not found\n");
					return 1;
				}
				refclk = xhdmiphy_get_dru_refclk(inst);
				/* check DRU frequency */
				if (refclk == 1) {
					dev_err(inst->dev,
						"cannot get dru refclk\n");
					return 1;
				}

				/* Round input frequency to 10 kHz */
				refclk = (refclk + 5000) / 10000;
				refclk = refclk * 10000;

				/*
				 * set the dru to operate at a linerate of
				 * 2.5 Gbps
				 */
				xhdmiphy_cfg_linerate(inst, ch_id,
						      (get_gthdmi_ptr(inst))->
						      dru_linerate);
				/* Set dru is enabled flag */
				inst->rx_dru_enabled = 1;

				/* set rx data width */
				xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA,
						&id0, &id1);
				for (id = id0; id <= id1; id++) {
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 20;
					inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 2;
				}

				if (tx_linerate > (((get_gthdmi_ptr(inst))->
						dru_linerate) / 1000000)) {
					dev_err(inst->dev,
						"video format is not supported\n");
					return 1;
				}
			} else {
				/* return config not found error when TMDS ratio is 1/40 */
				if (inst->rx_tmdsclock_ratio) {
					dev_err(inst->dev,
						"cpll config not found\n");
				} else {
					dev_err(inst->dev, "no dru present\n");
				}
				return 1;
			}
		}
	}

	/* try different sample rates */
	for (sr_index = 0; sr_index < sizeof(sr_arr); sr_index++) {
		/* only use oversampling when then tx is using the cpll */
		if (dir == XHDMIPHY_DIR_TX) {
			sr_val = sr_arr[sr_index];
			if (inst->tx_hdmi21_cfg.is_en == 0) {
				/* multiply the reference clock with the sample
				 * rate value
				 */
				refclk = ((*refclk_ptr) * sr_val);
				/* calculate scaled line rate */
				if (tx_linerate >= XHDMIPHY_LRATE_3400) {
					xhdmiphy_cfg_linerate(inst, ch_id,
							      (refclk * 40));
				} else {
					xhdmiphy_cfg_linerate(inst,
							      ch_id,
							      (refclk * 10));
				}
			} else { /* inst->tx_hdmi21_cfg.is_en == 1 */
				refclk = (*refclk_ptr);
			}
		/* for all other reference clocks force sample rate to one */
		} else {
			sr_val = 1;
		}

		status = xhdmiphy_clk_cal_params(inst, ch_id, dir, refclk);
		if (status == (0)) {
			/* only execute when the tx is using the qpll */
			if (dir == XHDMIPHY_DIR_TX) {
				inst->tx_samplerate = sr_val;
				(*refclk_ptr) = (*refclk_ptr) * sr_val;
			}

			/* check userclock frequency */
			/* (300 MHz + 0.5%) + 10 KHz (clkdet accuracy) */
			if (301500000 <
			    (xhdmiphy_get_linerate(inst,
						   XHDMIPHY_CHID_CH1) /
						   (inst->conf.transceiver_width * 10))) {
				dev_err(inst->dev, "user clock error\n");
				return 1;
			}

			return 0;
		}
	}

	dev_err(inst->dev, "cpll config not found\n");

	return 1;
}
