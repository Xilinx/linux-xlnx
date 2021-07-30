// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device.h>
#include "xhdmiphy.h"

static const u8 gthe4_cpll_divs_m[] = {1, 2, 0};
static const u8 gthe4_cpll_divs_n1[] = {4, 5, 0};
static const u8 gthe4_cpll_divs_n2[] = {1, 2, 3, 4, 5, 8, 0};
static const u8 gthe4_cpll_divs_d[] = {1, 2, 4, 8, 0};

static const u8 gthe4_qpll_divs_m[] = {1, 2, 3, 4, 0};
static const u8 gthe4_qpll_divs_n1[] = {16, 20, 25, 30, 32, 40, 60, 64, 66, 75,
					80, 84, 90, 96, 100, 112, 120, 125, 150,
					160, 0};
static const u8 gthe4_qpll_divs_n2[] = {1, 0};
static const u8 gthe4_qpll_divs_d[] = {1, 2, 4, 8, 16, 0};

/**
 * xhdmiphy_gt_mst_rst - This function will set the (TX|RX) MSTRESET port of
 * the GT
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 * @rst:	rst set=true; Clear=false
 */
static void xhdmiphy_gt_mst_rst(struct xhdmiphy_dev *inst, enum chid chid,
				enum dir dir, u8 rst)
{
	u32 reg_off, reg_val, mask_val = 0;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);

	for (id = id0; id <= id1; id++)
		mask_val |=  XHDMIPHY_TXRX_MSTRESET_MASK(id);

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_TX_INIT_REG;
	else
		reg_off = XHDMIPHY_RX_INIT_REG;

	reg_val = xhdmiphy_read(inst, reg_off);

	xhdmiphy_set_clr(inst, reg_off, reg_val, mask_val, rst);
}

 /**
  * This function will translate the configured QPLL's M or CPLL's M or N2
  * values to DRP encoding.
  *
  * @attr_enc:	attribute to encode
  *
  * @return:	encoded value
  *
  * @note: For more details about DRP encoding values , refer GT user guide
  */
static u8 xhdmiphy_drpenc_qpll_mcpll_mn2(u8 attr_enc)
{
	u8 drp_enc;

	switch (attr_enc) {
	case 1:
		drp_enc = 16;
		break;
	case 6:
		drp_enc = 5;
		break;
	case 10:
		drp_enc = 7;
		break;
	case 12:
		drp_enc = 13;
		break;
	case 20:
		drp_enc = 15;
		break;
	case 2: case 3: case 4:
	case 5: case 8: case 16:
		drp_enc = (attr_enc - 2);
		break;
	default:
		drp_enc = 0xf;
	break;
	}

	return drp_enc;
}

/* This function translates the configured CPLL's N1 value to DRP encoding */
static u8 xhdmiphy_drpenc_cpll_n1(u8 attr_enc)
{
	return (attr_enc - 4) & 0x1;
}

/* This function translates the configured QPLL's N value to DRP encoding */
static u16 xhdmiphy_drpenc_qpll_n(u8 attr_enc)
{
	u16 drp_enc;

	if (attr_enc >= 16 && attr_enc <= 160)
		drp_enc = attr_enc - 2;
	else
		drp_enc = 0xff;

	return drp_enc;
}

/* This function translates the configured CPLL's D values to DRP encoding */

static u8 xhdmiphy_drpenc_cpll_txrx_d(u8 attr_enc)
{
	u8 drp_enc;

	switch (attr_enc) {
	case 1:
		drp_enc = 0;
		break;
	case 2:
		drp_enc = 1;
		break;
	case 4:
		drp_enc = 2;
		break;
	case 8:
		drp_enc = 3;
		break;
	case 16:
		drp_enc = 4;
		break;
	default:
		drp_enc = 0x4;
		break;
	}

	return drp_enc;
}

/* This function translates the configured Rx data width to DRP encoding */
static u8 xhdmiphy_drpenc_datawidth(u8 attr_enc)
{
	u8 drp_enc;

	switch (attr_enc) {
	case 16:
		drp_enc = 2;
		break;
	case 20:
		drp_enc = 3;
		break;
	case 32:
		drp_enc = 4;
		break;
	case 40:
		drp_enc = 5;
		break;
	case 64:
		drp_enc = 6;
		break;
	case 80:
		drp_enc = 7;
		break;
	case 128:
		drp_enc = 8;
		break;
	case 160:
		drp_enc = 9;
		break;
	default:
		drp_enc = 0xf;
		break;
	}

	return drp_enc;
}

/* This function translates the configured RXINTDATAWIDTH to DRP encoding */
static u8 xhdmiphy_drpenc_int_datawidth(u8 attr_enc)
{
	u8 drp_enc;

	switch (attr_enc) {
	case 2:
		drp_enc = 0;
		break;
	case 4:
		drp_enc = 1;
		break;
	default:
		drp_enc = 2;
		break;
	}

	return drp_enc;
}

/* This function will translate the configured CLK25 to DRP encoding */
static u16 xhdmiphy_drpenc_clk25(u32 refclk_hz)
{
	u16 drp_enc;
	u32 refclk_mhz = refclk_hz / 1000000;

	drp_enc = ((refclk_mhz / 25) +
		   (((refclk_mhz % 25) > 0) ? 1 : 0)) - 1;

	return (drp_enc & 0x1f);
}

/**
 * xhdmiphy_cpll_cal_counttol - This function configures the cpll Calibration
 * period and the count tolerance registers.
 * cpll_cal_period = ((fPLLClkin * N1 * N2) / (20 * M)) /
 *                       (16000 / (4 * fFreeRunClk))
 * cpll_cal_tol = cpll_cal_period * 0.10
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @dir:		dir is an indicator for TX or RX
 * @freerun_clk:	freerun_clk is the freerunning clock freq in Hz
 *			driving the GT Wiz instance
 *
 * @return:		returns 0 on successful configuration or else returns 1
 */
static bool xhdmiphy_cpll_cal_counttol(struct xhdmiphy_dev *inst, enum chid chid,
				       enum dir dir, u32 freerun_clk)
{
	u64 cpll_cal_period;
	u64 cpll_cal_tol;
	u64 pll_vco_freq;
	u32 reg_val;

	if (!xhdmiphy_is_ch(chid))
		return false;

	pll_vco_freq = xhdmiphy_get_pll_vco_freq(inst, chid, dir);
	cpll_cal_period = pll_vco_freq * 200 / (u64)freerun_clk;
	if (cpll_cal_period % 10)
		cpll_cal_tol = (cpll_cal_period / 10) + 1;
	else
		cpll_cal_tol = cpll_cal_period / 10;

	/* read cpll calibration period value */
	reg_val = xhdmiphy_read(inst, XHDMIPHY_CPLL_CAL_PERIOD_REG) &
				~XHDMIPHY_CPLL_CAL_PERIOD_MASK;
	reg_val |= cpll_cal_period & XHDMIPHY_CPLL_CAL_PERIOD_MASK;

	/* write new cpll calibration period value */
	xhdmiphy_write(inst, XHDMIPHY_CPLL_CAL_PERIOD_REG, reg_val);
	/* read cpll calibration tolerance value */
	reg_val = xhdmiphy_read(inst, XHDMIPHY_CPLL_CAL_TOL_REG) &
				~XHDMIPHY_CPLL_CAL_TOL_MASK;

	reg_val |= cpll_cal_tol & XHDMIPHY_CPLL_CAL_TOL_MASK;
	xhdmiphy_write(inst, XHDMIPHY_CPLL_CAL_TOL_REG, reg_val);

	return true;
}

/**
 * xhdmiphy_m_drpenc - This function will translate the configured M value to
 * DRP encoding.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	The DRP encoding for M
 */
static u8 xhdmiphy_m_drpenc(struct xhdmiphy_dev *inst, enum chid chid)
{
	struct pll_param pll_prm;
	u8 m_refclk_div;

	pll_prm = inst->quad.plls[XHDMIPHY_CH2IDX(chid)].pll_param;

	if (chid >= XHDMIPHY_CHID_CH1 && chid <= XHDMIPHY_CHID_CH4)
		m_refclk_div = pll_prm.m_refclk_div;
	else if (chid == XHDMIPHY_CHID_CMN0 || chid == XHDMIPHY_CHID_CMN1)
		m_refclk_div = pll_prm.m_refclk_div;
	else
		m_refclk_div = 0;

	return xhdmiphy_drpenc_qpll_mcpll_mn2(m_refclk_div);
}

/**
 * xhdmiphy_gthe4_set_cdr - This function will set the clock and data recovery
 * (CDR) values for a given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static bool xhdmiphy_gthe4_set_cdr(struct xhdmiphy_dev *inst, enum chid chid)
{
	struct channel *ch_ptr;
	u8 rx_outdiv;

	if (chid < XHDMIPHY_CHID_CH1 || chid > XHDMIPHY_CHID_CH4)
		return true;

	ch_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
	rx_outdiv = ch_ptr->rx_outdiv;

	ch_ptr->pll_param.cdr[0] = XHDMIPHY_RXCDR_CFG_WORD0;
	ch_ptr->pll_param.cdr[1] = XHDMIPHY_RXCDR_CFG_WORD1;
	ch_ptr->pll_param.cdr[3] = XHDMIPHY_RXCDR_CFG_WORD3;
	ch_ptr->pll_param.cdr[4] = XHDMIPHY_RXCDR_CFG_WORD4;

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX)) {
		/*
		 * rx_outdiv = 1 => cdr[2] = 0x0262
		 * rx_outdiv = 2 => cdr[2] = 0x0252
		 * rx_outdiv = 4 => cdr[2] = 0x0242
		 * rx_outdiv = 8 => cdr[2] = 0x0232
		 * rx_outdiv = 16 => cdr[2] = 0x0222
		 */
		ch_ptr->pll_param.cdr[2] = XHDMIPHY_RXCDR_CFG_WORD2;

		while (rx_outdiv >>= 1)
			ch_ptr->pll_param.cdr[2] -=
						XHDMIPHY_RXCDR_CFG_WORD2_RXDIV;

	} else {
		return true;
	}

	return false;
}

/**
 * xhdmiphy_gthe4_check_pll_oprange - This function will check if a given PLL
 * output frequency is within the operating range of the PLL for the GT type.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @pll_clkout_freq:	pll_clkout_freq is the frequency to check
 *
 * @return:		- 0 if the frequency resides within the PLL's range
 *			- 1 otherwise
 */
static bool xhdmiphy_gthe4_check_pll_oprange(struct xhdmiphy_dev *inst, enum chid chid,
					     u64 pll_clkout_freq)
{
	if ((chid == XHDMIPHY_CHID_CMN0 &&
	     pll_clkout_freq >= XHDMIPHY_QPLL0_MIN &&
	     pll_clkout_freq <= XHDMIPHY_QPLL0_MAX) ||
	    (chid == XHDMIPHY_CHID_CMN1 &&
	     pll_clkout_freq >= XHDMIPHY_QPLL1_MIN &&
	     pll_clkout_freq <= XHDMIPHY_QPLL1_MAX) ||
	    (chid >= XHDMIPHY_CHID_CH1 &&
	     chid <= XHDMIPHY_CHID_CH4 &&
	     pll_clkout_freq >= XHDMIPHY_CPLL_MIN &&
	     pll_clkout_freq <= XHDMIPHY_CPLL_MAX)) {
		return false;
	}

	return true;
}

/**
 * xhdmiphy_d_drpenc - This function will translate the configured D value to
 * DRP encoding.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	The DRP encoding for D
 */
static u8 xhdmiphy_d_drpenc(struct xhdmiphy_dev *inst, enum chid chid,
			    enum dir dir)
{
	u8 out_div;

	out_div = inst->quad.plls[XHDMIPHY_CH2IDX(chid)].outdiv[dir];

	return xhdmiphy_drpenc_cpll_txrx_d(out_div);
}

/**
 * xhdmiphy_gthe4_outdiv_chreconf - This function will set the output divider
 * logic for a given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_outdiv_chreconf(struct xhdmiphy_dev *inst,
					  enum chid chid, enum dir dir)
{
	u32 status = 0;
	u16 drp_val, write_val;

	if (dir == XHDMIPHY_DIR_RX) {
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0063,
					 &drp_val);
		/* Mask out RXOUT_DIV */
		drp_val &= ~XDRP_GTHE4_CHN_REG_0063_RXOUT_DIV_MASK;
		/* Set RXOUT_DIV */
		write_val = (xhdmiphy_d_drpenc(inst, chid, XHDMIPHY_DIR_RX) &
			    XDRP_GTHE4_CHN_REG_0063_FLD_RXOUT_DIV_MASK);
		drp_val |= write_val;
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0063,
					 drp_val);
	} else {
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_007C,
					 &drp_val);
		/* mask out TXOUT_DIV */
		drp_val &= ~XDRP_GTHE4_CHN_REG_007C_TXOUT_DIV_MASK;
		/* Set TXOUT_DIV */
		write_val = (xhdmiphy_d_drpenc(inst, chid, XHDMIPHY_DIR_TX) &
			     XDRP_GTHE4_CHN_REG_007C_FLD_TX_RXDETECT_REF_MASK);
		drp_val |= (write_val << XHDMIPHY_DRP_TXOUT_OFFSET);
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_007C,
					 drp_val);
	}

	return status;
}

/**
 * xhdmiphy_n_drpenc - This function will translate the configured N1/N2 value
 * to DRP encoding.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @nid:	NId specified to operate on N1 (if == 1) or N2 (if == 2)
 *
 * @return:	The DRP encoding for N1/N2
 */
static u16 xhdmiphy_n_drpenc(struct xhdmiphy_dev *inst, enum chid chid, u8 nid)
{
	struct pll_param pll_prm;
	u16 drp_enc;
	u8 nfb_div;

	pll_prm = inst->quad.plls[XHDMIPHY_CH2IDX(chid)].pll_param;

	if (chid == XHDMIPHY_CHID_CMN0 || chid == XHDMIPHY_CHID_CMN1) {
		nfb_div = pll_prm.nfb_div;
		drp_enc = xhdmiphy_drpenc_qpll_n(nfb_div);
	} else if (nid == 1) {
		nfb_div = pll_prm.n1fb_div;
		drp_enc = xhdmiphy_drpenc_cpll_n1(nfb_div);
	} else {
		nfb_div = pll_prm.n2fb_div;
		drp_enc = xhdmiphy_drpenc_qpll_mcpll_mn2(nfb_div);
	}

	return drp_enc;
}

/**
 * xhdmiphy_gthe4_clkch_reconf - This function will configure the channel clock
 * settings.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_clkch_reconf(struct xhdmiphy_dev *inst,
				       enum chid chid)
{
	u32 cpll_vco_mhz, status = 0;
	u16 drp_val, write_val;

	/* Obtain current DRP register value for PLL dividers */
	status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0028, &drp_val);
	/* Mask out clock divider bits */
	drp_val &= ~(XDRP_GTHE4_CHN_REG_0028_CPLL_FBDIV_MASK);
	/* Set CPLL_FBDIV */
	write_val = (xhdmiphy_n_drpenc(inst, chid, 2) &
		     XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_MASK);
	drp_val |= (write_val << XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_SHIFT);
	/* Set CPLL_FBDIV_45 */
	write_val = (xhdmiphy_n_drpenc(inst, chid, 1) &
		     XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_45_MASK);
	drp_val |= (write_val << XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_45_SHIFT);
	/* write new DRP register value for PLL dividers */
	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0028, drp_val);
	/* write CPLL Ref Clk Div */
	status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_002A, &drp_val);
	/* Mask out clock divider bits */
	drp_val &= ~(XDRP_GTHE4_CHN_REG_002A_CPLL_REFCLK_DIV_MASK);
	/* Set CPLL_REFCLKDIV */
	write_val = (xhdmiphy_m_drpenc(inst, chid) &
		     XDRP_GTHE4_CHN_REG_002A_FLD_A_TXDIFFCTRL_MASK);
	drp_val |= (write_val << XDRP_GTHE4_CHN_REG_002A_FLD_A_TXDIFFCTRL_SHIFT);
	/* write new DRP register value for PLL dividers */
	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_002A, drp_val);

	cpll_vco_mhz =
	xhdmiphy_get_pll_vco_freq(inst, chid,
				  xhdmiphy_is_tx_using_cpll(inst, chid) ?
				  XHDMIPHY_DIR_TX : XHDMIPHY_DIR_RX) / 1000000;
	/* CPLL_CFG0 */
	if (cpll_vco_mhz <= XHDMIPHY_DRP_CPLL_VCO_RANGE1)
		drp_val = XHDMIPHY_DRP_CPLL_CFG0_VAL1;
	else if (cpll_vco_mhz <= XHDMIPHY_DRP_CPLL_VCO_RANGE2)
		drp_val = XHDMIPHY_DRP_CPLL_CFG0_VAL2;
	else
		drp_val = XHDMIPHY_DRP_CPLL_CFG0_VAL3;

	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00CB, drp_val);
	/* CPLL_CFG1 */
	if (cpll_vco_mhz <= XHDMIPHY_DRP_CPLL_VCO_RANGE1)
		drp_val = XHDMIPHY_DRP_CPLL_CFG1_VAL1;
	else
		drp_val = XHDMIPHY_DRP_CPLL_CFG1_VAL2;

	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00CC, drp_val);
	/* CPLL_CFG2 */
	if (cpll_vco_mhz <= XHDMIPHY_DRP_CPLL_VCO_RANGE1)
		drp_val = XHDMIPHY_DRP_CPLL_CFG2_VAL1;
	else if (cpll_vco_mhz <= XHDMIPHY_DRP_CPLL_VCO_RANGE2)
		drp_val = XHDMIPHY_DRP_CPLL_CFG2_VAL2;
	else
		drp_val = XHDMIPHY_DRP_CPLL_CFG2_VAL3;

	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00BC, drp_val);
	/* configure CPLL Calibration Registers */
	xhdmiphy_cpll_cal_counttol(inst, chid,
				   (xhdmiphy_is_tx_using_cpll(inst, chid) ?
				   XHDMIPHY_DIR_TX : XHDMIPHY_DIR_RX),
				   inst->conf.drpclk_freq);

	return status;
}

static u8 xhdmiphy_get_refclk_src_cnt(struct xhdmiphy_dev *inst)
{
	enum refclk_sel refclk_sel[XHDMIPHY_REFCLKSEL_MAX];
	enum refclk_sel refclk_sel_tmp[XHDMIPHY_REFCLKSEL_MAX];
	u8 i, j, match, refclk_num = 0;

	refclk_sel[0] = (inst->conf.tx_protocol != XHDMIPHY_PROT_NONE) ?
			 inst->conf.tx_refclk_sel : 99;
	refclk_sel[1] = (inst->conf.rx_protocol != XHDMIPHY_PROT_NONE) ?
			 inst->conf.rx_refclk_sel : 99;
	refclk_sel[2] = (inst->conf.dru_present) ?
			 inst->conf.dru_refclk_sel : 99;
	refclk_sel[3] = (inst->conf.tx_protocol == XHDMIPHY_PROT_HDMI21) ?
			 inst->conf.tx_frl_refclk_sel : 99;
	refclk_sel[4] = (inst->conf.rx_protocol == XHDMIPHY_PROT_HDMI21) ?
			 inst->conf.rx_frl_refclk_sel : 99;

	/* initialize Unique refclock holder */
	for (i = 0; i < XHDMIPHY_REFCLKSEL_MAX; i++)
		refclk_sel_tmp[i] = 99;

	i = 0;
	do {
		if (refclk_sel[i] != 99) {
			match = 0;
			j = 0;
			/* Check if refclk_sel is already in unique holder array */
			do {
				if (refclk_sel_tmp[j] == refclk_sel[i])
					match |= 1;
				j++;
			} while (j < refclk_num);

			/* register in unique holder if new RefClk is detected */
			if (match == 0) {
				refclk_sel_tmp[refclk_num] = refclk_sel[i];
				/* increment refclk counter */
				refclk_num++;
			}
		}
		i++;
	} while (i < XHDMIPHY_REFCLKSEL_MAX);

	return refclk_num;
}

/**
 * xhdmiphy_gthe4_clkcmn_reconf - This function will configure the common
 * channel clock settings.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @cmn_id:	cmn_id is the common channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_clkcmn_reconf(struct xhdmiphy_dev *inst,
					enum chid cmn_id)
{
	u32 qpll_vco_mhz, qpll_clkout_mhz, status = 0;
	u16 drp_val, write_val;
	u8 nfb_div;

	nfb_div = inst->quad.plls[XHDMIPHY_CH2IDX(cmn_id)].pll_param.nfb_div;

	/* Obtain current DRP register value for QPLL_FBDIV */
	status |= xhdmiphy_drprd(inst, XHDMIPHY_CHID_CMN,
		  (cmn_id == XHDMIPHY_CHID_CMN0) ? XDRP_GTHE4_CMN_REG_0014 :
			     XDRP_GTHE4_CMN_REG_0094, &drp_val);
	/* Mask out QPLL_FBDIV */
	drp_val &= ~(XDRP_GTHE4_CMN_REG_0014_FLD_QPLL0_INIT_CFG1_MASK);
	/* Set QPLL_FBDIV */
	write_val = (xhdmiphy_n_drpenc(inst, cmn_id, 0) &
		    XDRP_GTHE4_CMN_REG_0014_FLD_QPLL0_INIT_CFG1_MASK);
	drp_val |= write_val;
	/* write new DRP register value for QPLL_FBDIV */
	status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
				 (cmn_id == XHDMIPHY_CHID_CMN0) ?
				 XDRP_GTHE4_CMN_REG_0014 :
				 XDRP_GTHE4_CMN_REG_0094, drp_val);

	/* Obtain current DRP register value for QPLL_REFCLK_DIV */
	status |= xhdmiphy_drprd(inst, XHDMIPHY_CHID_CMN,
				 (cmn_id == XHDMIPHY_CHID_CMN0) ?
				 XDRP_GTHE4_CMN_REG_0018 :
				 XDRP_GTHE4_CMN_REG_0098, &drp_val);
	/* Mask out QPLL_REFCLK_DIV */
	drp_val &= ~(XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_MASK);
	if (xhdmiphy_get_refclk_src_cnt(inst) > 1)
		drp_val |= (1 << XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_SHIFT);

	write_val = (xhdmiphy_m_drpenc(inst, cmn_id) &
		    XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_MASK1);
	drp_val |= (write_val <<
		    XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_SHIFT1);

	status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
				 (cmn_id == XHDMIPHY_CHID_CMN0) ?
				 XDRP_GTHE4_CMN_REG_0018 :
				 XDRP_GTHE4_CMN_REG_0098, drp_val);
	if ((xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX)) ||
	    (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX))) {
		qpll_vco_mhz =
		xhdmiphy_get_pll_vco_freq(inst, cmn_id,
					  xhdmiphy_is_using_qpll(inst, cmn_id, XHDMIPHY_DIR_TX) ?
					  XHDMIPHY_DIR_TX : XHDMIPHY_DIR_RX) / 1000000;
		qpll_clkout_mhz = qpll_vco_mhz / 2;

		status |= xhdmiphy_drprd(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_000D :
					 XDRP_GTHE4_CMN_REG_008D, &drp_val);

		drp_val &= ~(XDRP_GTHE4_CMN_REG_000D_PPFX_CFG_MASK);
		if (qpll_vco_mhz >= XHDMIPHY_DRP_QPLL_VCO_RANGE1)
			drp_val |= XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL1;
		else if (qpll_vco_mhz >= XHDMIPHY_DRP_QPLL_VCO_RANGE3)
			drp_val |= XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL2;
		else if (qpll_vco_mhz >= XHDMIPHY_DRP_QPLL_VCO_RANGE4)
			drp_val |= XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL3;
		else
			drp_val |= XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL4;

		if (qpll_vco_mhz >= XHDMIPHY_DRP_QPLL_VCO_RANGE2)
			drp_val |= XHDMIPHY_DRP_PPF_MUX_TERM_CTRL0_VAL1;
		else
			drp_val |= XHDMIPHY_DRP_PPF_MUX_TERM_CTRL0_VAL2;

		/* write new DRP register value for PPFx_CFG */
		status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_000D :
					 XDRP_GTHE4_CMN_REG_008D, drp_val);
		/* QPLL_CP */
		if (nfb_div <= XHDMIPHY_DRP_QPLL_NFBDIV)
			drp_val = XHDMIPHY_DRP_QPLL_CP_VAL1;
		else
			drp_val = XHDMIPHY_DRP_QPLL_CP_VAL2;

		/* write new DRP register value for QPLL_CP */
		status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_0016 :
					 XDRP_GTHE4_CMN_REG_0096, drp_val);
		/* QPLL_LPF */
		status |= xhdmiphy_drprd(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_0019 :
					 XDRP_GTHE4_CMN_REG_0099, &drp_val);
		drp_val &= ~(XDRP_GTHE4_CMN_REG_0019_QPLLX_LPF_MASK);
		if (nfb_div <= XHDMIPHY_DRP_QPLL_NFBDIV)
			drp_val |= XHDMIPHY_DRP_QPLL_LPF_VAL1;
		else
			drp_val |= XHDMIPHY_DRP_QPLL_LPF_VAL2;

		status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_0019 :
					 XDRP_GTHE4_CMN_REG_0099, drp_val);
		status |= xhdmiphy_drprd(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_0030 :
					 XDRP_GTHE4_CMN_REG_00B0, &drp_val);
		drp_val &= ~(XDRP_GTHE4_CMN_REG_0030_QPLLX_CFG4_MASK);

		if (qpll_clkout_mhz >= XHDMIPHY_DRP_QPLL_CLKOUT_RANGE1)
			drp_val |= XHDMIPHY_DRP_Q_TERM_CLK_VAL1 <<
				   XHDMIPHY_DRP_Q_DCRNT_CLK_SHIFT;
		else if (qpll_clkout_mhz >= XHDMIPHY_DRP_QPLL_CLKOUT_RANGE2)
			drp_val |= XHDMIPHY_DRP_Q_TERM_CLK_VAL2 <<
				   XHDMIPHY_DRP_Q_DCRNT_CLK_SHIFT;
		else
			drp_val |= XHDMIPHY_DRP_Q_TERM_CLK_VAL3 <<
				   XHDMIPHY_DRP_Q_DCRNT_CLK_SHIFT;

		if (qpll_clkout_mhz >= XHDMIPHY_DRP_QPLL_CLKOUT_RANGE1)
			drp_val |= XHDMIPHY_DRP_Q_DCRNT_CLK_VAL1;
		else if (qpll_clkout_mhz >= XHDMIPHY_DRP_QPLL_CLKOUT_RANGE3)
			drp_val |= XHDMIPHY_DRP_Q_DCRNT_CLK_VAL2;
		else
			drp_val |= XHDMIPHY_DRP_Q_DCRNT_CLK_VAL3;

		/* write new DRP register value for QPLL_CFG4 */
		status |= xhdmiphy_drpwr(inst, XHDMIPHY_CHID_CMN,
					 (cmn_id == XHDMIPHY_CHID_CMN0) ?
					 XDRP_GTHE4_CMN_REG_0030 :
					 XDRP_GTHE4_CMN_REG_00B0, drp_val);
	}

	return status;
}

/**
 * xhdmiphy_gthe4_rxpll_div1_reconf - This function will configure the
 * channel's RX CLKDIV1 settings.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static bool xhdmiphy_gthe4_rxpll_div1_reconf(struct xhdmiphy_dev *inst,
					     enum chid chid)
{
	struct channel *pll_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
	u32 rx_refclk, status = 0;
	u16 drp_val;

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX))
		rx_refclk = inst->rx_refclk_hz;
	else
		rx_refclk = xhdmiphy_get_quad_refclk(inst, pll_ptr->pll_refclk);

	status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_006D, &drp_val);
	drp_val &= ~XDRP_GTHE4_CHN_REG_006D_RXCLK25_MASK;
	drp_val |= xhdmiphy_drpenc_clk25(rx_refclk) << 3;
	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_006D, drp_val);

	return status;
}

/**
 * xhdmiphy_gthe4_rxch_reconf - This function will configure the channel's RX
 * settings.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_rxch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	struct channel *ch_ptr;
	enum chid chid_pll;
	enum pll_type pll_type;
	u64 linkrate;
	u32 xvcorate_mhz, pll_clkout_mhz, pll_clkout_div, status = 0;
	u16 drp_val, write_val;
	u8 i;

	ch_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];

	for (i = 0; i < 5; i++) {
		drp_val = ch_ptr->pll_param.cdr[i];
		if (!drp_val) {
			/* don't modify RX_CDR configuration */
			continue;
		}
		status |= xhdmiphy_drpwr(inst, chid, XHDMIPHY_DRP_RXCDR_CFG(i),
					 drp_val);
		if (i == 2) {
			status |= xhdmiphy_drpwr(inst, chid,
				  XHDMIPHY_DRP_RXCDR_CFG_GEN3(i), drp_val);
		}
	}

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX)) {
		pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX, chid);
		switch (pll_type) {
		case XHDMIPHY_PLL_QPLL:
		case XHDMIPHY_PLL_QPLL0:
			chid_pll = XHDMIPHY_CHID_CMN0;
			pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL1;
			break;
		case XHDMIPHY_PLL_QPLL1:
			chid_pll = XHDMIPHY_CHID_CMN1;
			pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL1;
			break;
		default:
			chid_pll = chid;
			pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL2;
			break;
		}

		linkrate = xhdmiphy_get_linerate(inst, chid_pll) / 1000;
		/* RXCDR_CFG3 & RXCDR_CFG3_GEN3 */
		if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_1)
			drp_val = XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL1;
		else if ((linkrate > XHDMIPHY_DRP_LINERATEKHZ_2) &&
			 (ch_ptr->rx_data_width == XHDMIPHY_DRP_RX_DATAWIDTH_64))
			drp_val = XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL2;
		else if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_3)
			drp_val = XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL1;
		else
			drp_val = XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL3;

		/* write RXCDR_CFG3 Value */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0011,
					 drp_val);
		/* write RXCDR_CFG3_GEN3 Value */
		status |= xhdmiphy_drpwr(inst, chid,
					 XHDMIPHY_DRP_RXCDR_CFG_GEN3(3),
					 drp_val);
		/* RXCDR_CFG2_GEN2 & RXCDR_CFG3_GEN2 */
		/* Get [15:10] from RXCDR_CFG3[5:0] */
		drp_val = (drp_val &
			  XDRP_GTHE4_CHN_REG_0011_RXCDR_CGF3_GEN2_MASK) <<
			  XDRP_GTHE4_CHN_REG_0011_RXCDR_CGF3_GEN2_SHIFT;
		/* Get [9:0] from RXCDR_CFG2[9:0] */
		drp_val &= ~XDRP_GTHE4_CHN_REG_00AF_RXCDR_CGF2_GEN2_MASK;
		drp_val |= ch_ptr->pll_param.cdr[2] &
			   XDRP_GTHE4_CHN_REG_00AF_RXCDR_CGF2_GEN2_MASK;
		/* write RXCDR_CFG2_GEN2 & RXCDR_CFG3_GEN2 value */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00AF,
					 drp_val);

		/* RX_WIDEMODE_CDR Encoding */
		switch (ch_ptr->rx_data_width) {
		case XHDMIPHY_DRP_RX_DATAWIDTH_80:
			if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_4)
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL1 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			else
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL2 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			break;
		case XHDMIPHY_DRP_RX_DATAWIDTH_64:
			if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_5)
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL1 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			else
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL2 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			break;
		case XHDMIPHY_DRP_RX_DATAWIDTH_40:
			if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_3)
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL2 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			else
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL3;
			break;
		case XHDMIPHY_DRP_RX_DATAWIDTH_32:
			if (linkrate > XHDMIPHY_DRP_LINERATEKHZ_6)
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL2 <<
					    XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT;
			else
				write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL3;
			break;
		default:
			write_val = XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL3;
			break;
		}

		/* RX_INT_DATAWIDTH & RX_WIDEMODE_CDR */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0066,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_0066_RX_INT_DATAWIDTH_MASK);
		/* Update RX_WIDEMODE_CDR Value */
		drp_val |= write_val & XDRP_GTHE4_CHN_REG_0066_RX_WIDEMODE_CDR_MASK;
		write_val = (xhdmiphy_drpenc_int_datawidth(ch_ptr->rx_intdata_width) &
			    XDRP_GTHE4_CHN_REG_0066_RX_WIDEMODE_CDR_MASK_VAL);

		drp_val |= write_val;
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0066,
					 drp_val);
		/* RX_DATA_WIDTH */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0003,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_MASK);
		write_val = (xhdmiphy_drpenc_datawidth(ch_ptr->rx_data_width) &
			    XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_ENC_MASK);
		write_val <<= XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_ENC_SHIFT;
		drp_val |= write_val;
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0003,
					 drp_val);
		xvcorate_mhz = xhdmiphy_get_pll_vco_freq(inst, chid_pll,
							 XHDMIPHY_DIR_RX) / 1000000;
		pll_clkout_mhz = xvcorate_mhz / pll_clkout_div;
		/* CH_HSPMUX_RX */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0116,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_0116_CH_RX_HSPMUX_MASK);

		if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE1)
			drp_val |= XHDMIPHY_DRP_PLLX_CLKOUT_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE3)
			drp_val |= XHDMIPHY_DRP_PLLX_CLKOUT_VAL2;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE2)
			drp_val |= XHDMIPHY_DRP_PLLX_CLKOUT_VAL3;
		else
			drp_val |= XHDMIPHY_DRP_PLLX_CLKOUT_VAL4;

		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0116,
					 drp_val);
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_00FB,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_00FB_PREIQ_FREQ_BST_MASK);

		if (pll_clkout_mhz > XHDMIPHY_DRP_PLL_CLKOUT_RANGE4)
			drp_val |= XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL1 <<
				   XHDMIPHY_DRP_PREIQ_FREQ_BST_SHIFT;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE5)
			drp_val |= XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL2 <<
				   XHDMIPHY_DRP_PREIQ_FREQ_BST_SHIFT; /* LPM mode */
		else if (pll_clkout_mhz >= 10000)
			drp_val |= XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL2 <<
				   XHDMIPHY_DRP_PREIQ_FREQ_BST_SHIFT;
		else if (pll_clkout_mhz >= 6000)
			drp_val |= XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL3 <<
				   XHDMIPHY_DRP_PREIQ_FREQ_BST_SHIFT;

		/* write new DRP register value for PREIQ_FREQ_BST */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00FB,
					 drp_val);

		/* RXPI_CFG0 */
		if (pll_clkout_mhz > XHDMIPHY_DRP_PLL_CLKOUT_RANGE8)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE9)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL2;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL3;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE11)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL4;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE12)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL5;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE13)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL6;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE14)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL7;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE15)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL8;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE16)
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL9;
		else
			drp_val = XHDMIPHY_DRP_RXPI_CFG0_VAL10;

		/* write new DRP register value for RXPI_CFG0 */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_009D,
					 drp_val);
		/* RXPI_CFG1 */
		if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val = XHDMIPHY_DRP_RXPI_CFG1_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE12)
			drp_val = XHDMIPHY_DRP_RXPI_CFG1_VAL2;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE14)
			drp_val = XHDMIPHY_DRP_RXPI_CFG1_VAL3;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE18)
			drp_val = XHDMIPHY_DRP_RXPI_CFG1_VAL4;
		else
			drp_val = XHDMIPHY_DRP_RXPI_CFG1_VAL5;

		/* write new DRP register value for RXPI_CFG1 */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0100,
					 drp_val);
	}

	status |= xhdmiphy_gthe4_rxpll_div1_reconf(inst, chid);

	return status;
}

/**
 * xhdmiphy_gthe4_txpll_div1_reconf - This function will configure the
 * channel's TX CLKDIV1 settings.
 *
 * @inst:	inst is a pointer to the XHdmiphy1 core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_txpll_div1_reconf(struct xhdmiphy_dev *inst,
					    enum chid chid)
{
	struct channel *pll_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
	u32 tx_refclkhz;
	u32 status = 0;
	u16 drp_val;

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX))
		tx_refclkhz = inst->tx_refclk_hz;
	else
		tx_refclkhz = xhdmiphy_get_quad_refclk(inst,
						       pll_ptr->pll_refclk);

	status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_007A, &drp_val);
	drp_val &= ~XDRP_GTHE4_CHN_REG_007A_TXCLK25_MASK;
	drp_val |= xhdmiphy_drpenc_clk25(tx_refclkhz) <<
		   XDRP_GTHE4_CHN_REG_007A_TXCLK25_SHIFT;
	status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_007A, drp_val);

	return status;
}

/**
 * xhdmiphy_gthe4_txch_reconf - This function will configure the channel's
 * TX settings.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static u32 xhdmiphy_gthe4_txch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	struct channel *ch_ptr;
	enum chid chid_pll;
	enum pll_type pll_type;
	u32 ret_val, xvcorate_mhz, pll_clkout_mhz, pll_clkout_div;
	u32 status = 0;
	u16 drp_val, write_val;

	ret_val = xhdmiphy_gthe4_txpll_div1_reconf(inst, chid);
	if (!xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX))
		return ret_val;

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX, chid);

	switch (pll_type) {
	case XHDMIPHY_PLL_QPLL:
	case XHDMIPHY_PLL_QPLL0:
		chid_pll = XHDMIPHY_CHID_CMN0;
		pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL1;
		break;
	case XHDMIPHY_PLL_QPLL1:
		chid_pll = XHDMIPHY_CHID_CMN1;
		pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL1;
		break;
	default:
		chid_pll = chid;
		pll_clkout_div = XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL2;
		break;
	}

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX)) {
		ch_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
		/* set TX_PROGDIV_CFG to 20/40 */
		if (pll_type == XHDMIPHY_PLL_QPLL ||
		    pll_type == XHDMIPHY_PLL_QPLL0 ||
		    pll_type == XHDMIPHY_PLL_QPLL1) {
			if (inst->quad.plls[XHDMIPHY_CH2IDX(chid)].tx_outdiv != 16) {
				/* TX_PROGDIV_CFG = 20 */
				xhdmiphy_drpwr(inst, chid,
					       XDRP_GTHE4_CHN_REG_003E,
					       XDRP_GTHE4_CHN_REG_003E_DRP_VAL1);
			} else {
				/* TX_PROGDIV_CFG = 40 */
				xhdmiphy_drpwr(inst, chid,
					       XDRP_GTHE4_CHN_REG_003E,
					       XDRP_GTHE4_CHN_REG_003E_DRP_VAL2);
			}
		}

		/* TX_INT_DATAWIDTH */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0085,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_MASK <<
			    XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_SHIFT);
		write_val = (xhdmiphy_drpenc_int_datawidth(ch_ptr->tx_intdata_width) &
			    XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_MASK);
		write_val = write_val << XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_SHIFT;
		drp_val |= write_val;
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0085,
					 drp_val);
		/* TX_DATA_WIDTH */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_007A,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_007A_TX_DATA_WIDTH_MASK);
		write_val = (xhdmiphy_drpenc_datawidth(ch_ptr->tx_data_width) &
			    XDRP_GTHE4_CHN_REG_007A_TX_DATA_WIDTH_MASK);

		drp_val |= write_val;
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_007A,
					 drp_val);

		/* TXPH_CFG */
		if (ch_ptr->tx_outdiv == XHDMIPHY_DRP_TX_OUTDIV_VAL1) {
			if (ch_ptr->tx_data_width >
			    XHDMIPHY_DRP_TX_DATAWIDTH_VAL1)
				drp_val = XHDMIPHY_DRP_TXPH_CFG_VAL1;
			else
				drp_val = XHDMIPHY_DRP_TXPH_CFG_VAL2;
		} else if (ch_ptr->tx_outdiv ==
			   XHDMIPHY_DRP_TX_OUTDIV_VAL2) {
			if (ch_ptr->tx_data_width >
			    XHDMIPHY_DRP_TX_DATAWIDTH_VAL2)
				drp_val = XHDMIPHY_DRP_TXPH_CFG_VAL1;
			else
				drp_val = XHDMIPHY_DRP_TXPH_CFG_VAL2;
		} else {
			drp_val = XHDMIPHY_DRP_TXPH_CFG_VAL1;
		}

		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0073,
					 drp_val);
		xvcorate_mhz = xhdmiphy_get_pll_vco_freq(inst, chid_pll,
							 XHDMIPHY_DIR_TX) / 1000000;
		pll_clkout_mhz = xvcorate_mhz / pll_clkout_div;

		/* TXPI_CFG */
		if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val = XHDMIPHY_DRP_TXPI_CFG_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE14)
			drp_val = XHDMIPHY_DRP_TXPI_CFG_VAL2;
		else
			drp_val = XHDMIPHY_DRP_TXPI_CFG_VAL3;

		/* write new DRP register value for TXPI_CFG */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00FF,
					 drp_val);

		/* TXPI_CFG3 & TXPI_CFG4 */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_009C,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_009C_TXPI_CFG3_CFG4_MASK);
		if (pll_clkout_mhz > XHDMIPHY_DRP_PLL_CLKOUT_RANGE8)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE9)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL2;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL3;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE11)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL1;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE13)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL2;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE15)
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL3;
		else
			drp_val = XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL4;

		drp_val = (drp_val << XHDMIPHY_DRP_TXPI_CFG3_CFG4_SHIFT) &
			  XDRP_GTHE4_CHN_REG_009C_TXPI_CFG3_CFG4_MASK;
		/* write new DRP register value for TXPI_CFG3 & TXPI_CFG4 */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_009C,
					 drp_val);
		/* TX_PI_BIASSET */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_00FB,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_00FB_TXPI_BIASSET_MASK);
		if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE17)
			drp_val |= XHDMIPHY_DRP_TXPI_BIASSET_VAL1 <<
				   XHDMIPHY_DRP_TXPI_BIASSET_SHIFT;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val |= XHDMIPHY_DRP_TXPI_BIASSET_VAL2 <<
				   XHDMIPHY_DRP_TXPI_BIASSET_SHIFT;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE14)
			drp_val |= XHDMIPHY_DRP_TXPI_BIASSET_VAL3 <<
				   XHDMIPHY_DRP_TXPI_BIASSET_SHIFT;

		/* write new DRP register value for TX_PI_BIASSET */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_00FB,
					 drp_val);
		/* CH_HSPMUX_TX */
		status |= xhdmiphy_drprd(inst, chid, XDRP_GTHE4_CHN_REG_0116,
					 &drp_val);
		drp_val &= ~(XDRP_GTHE4_CHN_REG_0116_CH_TX_HSPMUX_MASK);

		if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE17)
			drp_val |= XHDMIPHY_DRP_CH_HSPMUX_VAL1 <<
				   XHDMIPHY_DRP_CH_HSPMUX_SHIFT;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE10)
			drp_val |= XHDMIPHY_DRP_CH_HSPMUX_VAL2 <<
				   XHDMIPHY_DRP_CH_HSPMUX_SHIFT;
		else if (pll_clkout_mhz >= XHDMIPHY_DRP_PLL_CLKOUT_RANGE14)
			drp_val |= XHDMIPHY_DRP_CH_HSPMUX_VAL3 <<
				   XHDMIPHY_DRP_CH_HSPMUX_SHIFT;
		else
			drp_val |= XHDMIPHY_DRP_CH_HSPMUX_VAL4 <<
				   XHDMIPHY_DRP_CH_HSPMUX_SHIFT;

		/* write new DRP register value for CH_HSPMUX_TX */
		status |= xhdmiphy_drpwr(inst, chid, XDRP_GTHE4_CHN_REG_0116,
					 drp_val);
	}

	return status;
}

/**
 * xhdmiphy_set_gt_linerate_cfg - This function will set the TXRATE or RXRATE
 * port to select the GT Wizard configuration
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 */
static void xhdmiphy_set_gt_linerate_cfg(struct xhdmiphy_dev *inst,
					 enum chid chid, enum dir dir)
{
	enum pll_type pll_type;
	u32 reg_val, mask_val, shift_val, reg_off;
	u16 lr_val;

	pll_type = xhdmiphy_get_pll_type(inst, dir, XHDMIPHY_CHID_CH1);

	if (pll_type == XHDMIPHY_PLL_LCPLL)
		lr_val = inst->quad.lcpll.linerate_cfg;
	else
		lr_val = inst->quad.rpll.linerate_cfg;

	if (dir == XHDMIPHY_DIR_TX) {
		if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
			reg_off = XHDMIPHY_TX_RATE_CH12_REG;
		else
			reg_off = XHDMIPHY_TX_RATE_CH34_REG;

		mask_val = XHDMIPHY_TX_RATE_MASK(chid);
		shift_val = XHDMIPHY_TX_RATE_SHIFT(chid);
	} else {
		if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
			reg_off = XHDMIPHY_RX_RATE_CH12_REG;
		else
			reg_off = XHDMIPHY_RX_RATE_CH34_REG;

		mask_val = XHDMIPHY_RX_RATE_MASK(chid);
		shift_val = XHDMIPHY_RX_RATE_SHIFT(chid);
	}

	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val &= ~mask_val;
	reg_val |= (lr_val << shift_val);
	xhdmiphy_write(inst, reg_off, reg_val);
}

static u32 xhdmiphy_gtye5_rxch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	u8 val_cmp;

	val_cmp = xhdmiphy_check_linerate_cfg(inst, chid, XHDMIPHY_DIR_RX);

	if (!val_cmp) {
		xhdmiphy_set_gt_linerate_cfg(inst, chid, XHDMIPHY_DIR_RX);
	} else {
		xhdmiphy_gt_mst_rst(inst, chid, XHDMIPHY_DIR_RX, true);
		xhdmiphy_gt_mst_rst(inst, chid, XHDMIPHY_DIR_RX, false);
	}

	return 0;
}

static u32 xhdmiphy_gtye5_txch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	u8 val_cmp;

	/* Compare the current and next CFG values */
	val_cmp = xhdmiphy_check_linerate_cfg(inst, chid, XHDMIPHY_DIR_TX);
	if (!val_cmp) {
		xhdmiphy_set_gt_linerate_cfg(inst, chid, XHDMIPHY_DIR_TX);
	} else {
		/* Toggle RX Master Reset */
		xhdmiphy_gt_mst_rst(inst, chid, XHDMIPHY_DIR_TX, true);
		xhdmiphy_gt_mst_rst(inst, chid, XHDMIPHY_DIR_TX, false);
	}

	return 0;
}

/**
 * xhdmiphy_get_gt_linerate - This function will get the TXRATE or RXRATE port
 * to select the GT Wizard configuration
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 *
 * return:	returns linerate
 */
static u16 xhdmiphy_get_gt_linerate(struct xhdmiphy_dev *inst, enum chid chid,
				    enum dir dir)
{
	u32 reg_val, mask_val, shift_val, reg_off;
	u16 lr_val;

	if (dir == XHDMIPHY_DIR_TX) {
		if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
			reg_off = XHDMIPHY_TX_RATE_CH12_REG;
		else
			reg_off = XHDMIPHY_TX_RATE_CH34_REG;

		mask_val = XHDMIPHY_TX_RATE_MASK(chid);
		shift_val = XHDMIPHY_TX_RATE_SHIFT(chid);
	} else {
		if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
			reg_off = XHDMIPHY_RX_RATE_CH12_REG;
		else
			reg_off = XHDMIPHY_RX_RATE_CH34_REG;

		mask_val = XHDMIPHY_RX_RATE_MASK(chid);
		shift_val = XHDMIPHY_RX_RATE_SHIFT(chid);
	}

	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val &= mask_val;
	lr_val = (u16)(reg_val >> shift_val);

	return lr_val;
}

/**
 * xhdmiphy_check_linerate_cfg - This function will check the current CFG
 * setting and compare it with the next CFG value
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 *
 * @return:	true if Current and Next CFG are the same
 *		false if Current and Next CFG are different
 */
bool xhdmiphy_check_linerate_cfg(struct xhdmiphy_dev *inst, enum chid chid,
				 enum dir dir)
{
	enum pll_type pll_type;
	u16 cur_val, lr_val;

	pll_type = xhdmiphy_get_pll_type(inst, dir, XHDMIPHY_CHID_CH1);

	if (pll_type == XHDMIPHY_PLL_LCPLL)
		lr_val = inst->quad.lcpll.linerate_cfg;
	else
		lr_val = inst->quad.rpll.linerate_cfg;

	cur_val = xhdmiphy_get_gt_linerate(inst, chid, dir);
	if (cur_val != lr_val)
		return false;

	return true;
}

const struct gt_conf gthe4_conf = {
	.cfg_set_cdr = xhdmiphy_gthe4_set_cdr,
	.check_pll_oprange = xhdmiphy_gthe4_check_pll_oprange,
	.outdiv_ch_reconf = xhdmiphy_gthe4_outdiv_chreconf,
	.clk_ch_reconf = xhdmiphy_gthe4_clkch_reconf,
	.clk_cmn_reconf = xhdmiphy_gthe4_clkcmn_reconf,
	.rxch_reconf = xhdmiphy_gthe4_rxch_reconf,
	.txch_reconf = xhdmiphy_gthe4_txch_reconf,

	 .cpll_divs = {
		.m = gthe4_cpll_divs_m,
		.n1 = gthe4_cpll_divs_n1,
		.n2 = gthe4_cpll_divs_n2,
		.d = gthe4_cpll_divs_d,
	},
	.qpll_divs = {
		.m = gthe4_qpll_divs_m,
		.n1 = gthe4_qpll_divs_n1,
		.n2 = gthe4_qpll_divs_n2,
		.d = gthe4_qpll_divs_d,
	},
};

const struct gt_conf gtye5_conf = {
	.rxch_reconf = xhdmiphy_gtye5_rxch_reconf,
	.txch_reconf = xhdmiphy_gtye5_txch_reconf,
	.cpll_divs = {
		.m = NULL,
		.n1 = NULL,
		.n2 = NULL,
		.d = NULL,
	},
	.qpll_divs = {
		.m = NULL,
		.n1 = NULL,
		.n2 = NULL,
		.d = NULL,
	},
};
