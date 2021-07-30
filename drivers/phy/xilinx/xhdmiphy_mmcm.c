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

	xhdmiphy_wr_mmcm4_params(inst, dir);

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
