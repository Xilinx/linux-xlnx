// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device.h>
#include "xhdmiphy.h"

void xhdmiphy_intr_en(struct xhdmiphy_dev *inst, u32 intr)
{
	u32 reg_val;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_INTR_EN_REG);
	reg_val |= intr;
	xhdmiphy_write(inst, XHDMIPHY_INTR_EN_REG, reg_val);
}

void xhdmiphy_intr_dis(struct xhdmiphy_dev *inst, u32 intr)
{
	u32 reg_val;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_INTR_DIS_REG);
	reg_val |= intr;
	xhdmiphy_write(inst, XHDMIPHY_INTR_DIS_REG, reg_val);
}

static void xhdmiphy_set(struct xhdmiphy_dev *inst, u32 addr, u32 set)
{
	xhdmiphy_write(inst, addr, xhdmiphy_read(inst, addr) | set);
}

inline void xhdmiphy_clr(struct xhdmiphy_dev *inst, u32 addr, u32 clr)
{
	xhdmiphy_write(inst, addr, xhdmiphy_read(inst, addr) & ~clr);
}

/**
 * xhdmiphy_outdiv_reconf - This function will set the current output divider
 * configuration over DRP.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID for which to write the settings for
 * @dir:	dir is an indicator for RX or TX
 */
static void xhdmiphy_outdiv_reconf(struct xhdmiphy_dev *inst, enum chid chid,
				   enum dir dir)
{
	u32 ret = 0;
	u8 id, id0, id1;

	if (!xhdmiphy_is_ch(chid))
		chid = XHDMIPHY_CHID_CHA;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);

	for (id = id0; id <= id1; id++) {
		ret = xhdmiphy_outdiv_ch_reconf(inst, (enum chid)id, dir);
		if (ret != 0)
			break;
	}
}

/**
 * xhdmiphy_clkdet_freq_threshold - This function sets the clock detector
 * frequency lock counter threshold value.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @thres_val:	thresholdVal is the threshold value to be set
 */
static void xhdmiphy_clkdet_freq_threshold(struct xhdmiphy_dev *inst,
					   u16 thres_val)
{
	u32 reg_val;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_CLKDET_CTRL_REG);

	reg_val &= ~XHDMIPHY_CLKDET_CTRL_RX_FREQ_RST_MASK;
	reg_val |= (thres_val << XHDMIPHY_CLKDET_CTRL_FREQ_LOCK_THRESH_SHIFT);
	xhdmiphy_write(inst, XHDMIPHY_CLKDET_CTRL_REG, reg_val);
}

/**
 * xhdmiphy_patgen_set_ratio - This function sets the Pattern Generator for the
 * GT channel 4 when it is used to generate the TX TMDS Clock.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @tx_linerate:	tx_linerate in Mbps
 */
static void xhdmiphy_patgen_set_ratio(struct xhdmiphy_dev *inst,
				      u64 tx_linerate)
{
	u32 reg_val;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_PATGEN_CTRL_REG)
				 & ~XHDMIPHY_PATGEN_CTRL_RATIO_MASK;

	if (tx_linerate >= XHDMIPHY_LRATE_3400 && inst->tx_samplerate == 1)
		reg_val |= XHDMIPHY_patgen_ratio_40 &
			   XHDMIPHY_PATGEN_CTRL_RATIO_MASK;
	else
		reg_val |= inst->tx_samplerate &
			   XHDMIPHY_PATGEN_CTRL_RATIO_MASK;

	xhdmiphy_write(inst, XHDMIPHY_PATGEN_CTRL_REG, reg_val);
}

static void xhdmiphy_dru_reset(struct xhdmiphy_dev *inst, enum chid chid,
			       u8 rst)
{
	u32 reg_val, mask_val = 0;
	u8 id, id0, id1;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_DRU_CTRL_REG);

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);

	for (id = id0; id <= id1; id++)
		mask_val |= XHDMIPHY_DRU_CTRL_RST_MASK(id);

	xhdmiphy_set_clr(inst, XHDMIPHY_DRU_CTRL_REG, reg_val, mask_val, rst);
}

static void xhdmiphy_dru_en(struct xhdmiphy_dev *inst, enum chid chid, u8 en)
{
	u32 reg_val, mask_val = 0;
	u8 id, id0, id1;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_DRU_CTRL_REG);

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);

	for (id = id0; id <= id1; id++)
		mask_val |= XHDMIPHY_DRU_CTRL_EN_MASK(id);

	xhdmiphy_set_clr(inst, XHDMIPHY_DRU_CTRL_REG, reg_val, mask_val, en);
}

static void xhdmiphy_dru_mode_en(struct xhdmiphy_dev *inst, u8 en)
{
	u32 reg_val, reg_mask = 0;
	u8 id, id0, id1;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_RX_EQ_CDR_REG);

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		reg_mask |= XHDMIPHY_RX_STATUS_RXCDRHOLD_MASK(id) |
			    XHDMIPHY_RX_STATUS_RXOSOVRDEN_MASK(id) |
			    XHDMIPHY_RX_STATUS_RXLPMLFKLOVRDEN_MASK(id) |
			    XHDMIPHY_RX_STATUS_RXLPMHFOVRDEN_MASK(id);
	}

	xhdmiphy_set_clr(inst, XHDMIPHY_RX_EQ_CDR_REG, reg_val, reg_mask, en);
}

static void xhdmiphy_set_dru_centerfreq(struct xhdmiphy_dev *inst,
					enum chid chid, u64 center_freq)
{
	u32 center_freq_l, center_freq_h, reg_off;
	u8 id, id0, id1;

	/* Split the 64-bit input into 2 32-bit values */
	center_freq_l = lower_32_bits(center_freq);
	center_freq_h = upper_32_bits(center_freq);

	center_freq_h &= XHDMIPHY_DRU_CFREQ_H_MASK;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		reg_off = XHDMIPHY_DRU_CFREQ_L_REG(id);
		xhdmiphy_write(inst, reg_off, center_freq_l);

		reg_off = XHDMIPHY_DRU_CFREQ_H_REG(id);
		xhdmiphy_write(inst, reg_off, center_freq_h);
	}
}

/**
 * xhdmiphy_get_dru_refclk - This function returns the frequency of the DRU
 * reference clock as measured by the clock detector peripheral.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 *
 * @return:	The measured frequency of the DRU reference clock
 */
u32 xhdmiphy_get_dru_refclk(struct xhdmiphy_dev *inst)
{
	u32 dru_freq = xhdmiphy_read(inst, XHDMIPHY_CLKDET_FREQ_DRU_REG);

	if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4) {
		if (dru_freq > XHDMIPHY_HDMI_GTHE4_DRU_REFCLK_MIN &&
		    dru_freq < XHDMIPHY_HDMI_GTHE4_DRU_REFCLK_MAX) {
			return XHDMIPHY_HDMI_GTHE4_DRU_REFCLK;
		} else if (dru_freq > XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2_MIN &&
			   dru_freq < XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2_MAX) {
			return XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2;
		}
	} else if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4) {
		if (dru_freq > XHDMIPHY_HDMI_GTYE4_DRU_REFCLK_MIN &&
		    dru_freq < XHDMIPHY_HDMI_GTYE4_DRU_REFCLK_MAX) {
			return XHDMIPHY_HDMI_GTYE4_DRU_REFCLK;
		} else if (dru_freq > XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2_MIN &&
			   dru_freq < XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2_MAX) {
			return XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2;
		}
	} else {
		if (dru_freq > XHDMIPHY_HDMI_GTYE5_DRU_REFCLK_MIN &&
		    dru_freq < XHDMIPHY_HDMI_GTYE5_DRU_REFCLK_MAX) {
			return XHDMIPHY_HDMI_GTYE5_DRU_REFCLK;
		}
		if (dru_freq > XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1_MIN &&
		    dru_freq < XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1_MAX) {
			return XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1;
		}
		if (dru_freq > XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2_MIN &&
		    dru_freq < XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2_MAX) {
			return XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2;
		}
	}

	return 1;
}

/**
 * xhdmiphy_dru_cal_centerfreq - This function calculates the center frequency
 * value for the DRU.
 *
 * @inst:	inst is a pointer to the xhdmiphy GT core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	The calculated DRU Center frequency value.
 *		According to XAPP1240 Center_f = fDIN * (2^32)/fdruclk
 *		The DRU clock is derived from the measured reference clock and
 *		the current QPLL settings.
 */
static u64 xhdmiphy_dru_cal_centerfreq(struct xhdmiphy_dev *inst,
				       enum chid chid)
{
	struct channel *ch_ptr;
	struct pll_param *pll_prm;
	u64 dru_refclk, clkdet_refclk, data_rate, f_din, fdru_clk;

	clkdet_refclk = xhdmiphy_read(inst, XHDMIPHY_CLKDET_FREQ_RX_REG);
	pll_prm = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)].pll_param;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		dru_refclk = xhdmiphy_get_dru_refclk(inst);
		/* take the master channel (channel 1) */
		ch_ptr = &inst->quad.ch1;
		if (chid == XHDMIPHY_CHID_CMN0 || chid == XHDMIPHY_CHID_CMN1) {
			fdru_clk = (dru_refclk * pll_prm->nfb_div) /
				    (pll_prm->m_refclk_div *
				     (ch_ptr->rx_outdiv * 20));
		} else {
			fdru_clk = (dru_refclk * ch_ptr->pll_param.n1fb_div *
				    ch_ptr->pll_param.n2fb_div * 2) /
				    (ch_ptr->pll_param.m_refclk_div *
				    ch_ptr->rx_outdiv * 20);
		}
	} else {
		fdru_clk = (u64)(XHDMIPHY_HDMI_GTYE5_DRU_LRATE / 20);
	}

	data_rate = 10 * clkdet_refclk;
	f_din = data_rate * ((u64)1 << 32);

	if (f_din && fdru_clk)
		return (f_din / fdru_clk);

	return 0;
}

/**
 * xhdmiphy_dir_reconf - This function will set the current RX/TX configuration
 * over DRP.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID for which to write the settings for
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static bool xhdmiphy_dir_reconf(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir)
{
	u32 status = 0;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		if (dir == XHDMIPHY_DIR_TX)
			status |= xhdmiphy_txch_reconf(inst, id);
		else
			status |= xhdmiphy_rxch_reconf(inst, id);

		if (status != 0)
			return true;
	}

	return status;
}

/**
 * xhdmiphy_clk_reconf - This function will set the current clocking settings
 * for each channel to hardware based on the configuration stored in the
 * driver's instance. hardware based on the configuration stored in the driver's
 * instance.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID for which to write the settings for
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static bool xhdmiphy_clk_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	u32 status = 0;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		if (xhdmiphy_is_ch(id)) {
			status |= xhdmiphy_clk_ch_reconf(inst, (enum chid)id);
		} else if (xhdmiphy_is_cmn(chid)) {
			if (((xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX)) ||
			     (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX))) &&
			     inst->qpll_present == 0) {
				dev_err(inst->dev,
					"return failure: qpll is not present\n");
				return false;
			}
			status |= xhdmiphy_clk_cmn_reconf(inst, (enum chid)id);
		}
		if (status)
			return false;
	}

	return status;
}

/**
 * xhdmiphy_get_rcfg_chid - obtain the reconfiguration channel ID for given
 * PLL type
 *
 * @pll_type:	pll_type is the PLL type being used by the channel
 *
 * @return:	The Channel ID to be used for reconfiguration
 */
static u8 xhdmiphy_get_rcfg_chid(enum pll_type pll_type)
{
	enum chid chid;

	switch (pll_type) {
	case XHDMIPHY_PLL_QPLL:
	case XHDMIPHY_PLL_QPLL0:
	case XHDMIPHY_PLL_LCPLL:
		chid = XHDMIPHY_CHID_CMN0;
		break;
	case XHDMIPHY_PLL_QPLL1:
	case XHDMIPHY_PLL_RPLL:
		chid = XHDMIPHY_CHID_CMN1;
		break;
	default:
		chid = XHDMIPHY_CHID_CHA;
		break;
	}

	return chid;
}

/*
 * This function sets the system clock selection
 *
 * @inst:	inst is a pointer to the xhmdiphy core instance
 *
 * @return:	none
 */
static void xhdmiphy_set_sys_clksel(struct xhdmiphy_dev *inst)
{
	if (inst->conf.tx_pllclk_sel == inst->conf.rx_pllclk_sel) {
		if (inst->conf.rx_pllclk_sel ==
		    XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK)
			xhdmiphy_pll_init(inst, XHDMIPHY_CHID_CHA,
					  inst->conf.rx_refclk_sel,
					  inst->conf.rx_refclk_sel,
					  XHDMIPHY_PLL_CPLL,
					  XHDMIPHY_PLL_CPLL);
		else
			xhdmiphy_pll_init(inst, XHDMIPHY_CHID_CMN0,
					  inst->conf.rx_refclk_sel,
					  inst->conf.rx_refclk_sel,
					  XHDMIPHY_PLL_QPLL0,
					  XHDMIPHY_PLL_QPLL0);
	} else if (inst->conf.tx_pllclk_sel ==
					XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK) {
		xhdmiphy_pll_init(inst, XHDMIPHY_CHID_CHA,
				  inst->conf.rx_refclk_sel,
				  inst->conf.tx_refclk_sel,
				  XHDMIPHY_PLL_CPLL,
				  XHDMIPHY_PLL_QPLL0);
	} else {
		xhdmiphy_pll_init(inst, XHDMIPHY_CHID_CMN0,
				  inst->conf.tx_refclk_sel,
				  inst->conf.rx_refclk_sel,
				  XHDMIPHY_PLL_QPLL0,
				  XHDMIPHY_PLL_CPLL);
	}
}

/**
 * xhdmiphy_mmcm_clkin_sel - This function will set the CLKINSEL port
 * of theMMCM
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @sel:	sel CLKINSEL value
 *		0 - CLKIN1
 *		1 - CLKIN2
 */
static void xhdmiphy_mmcm_clkin_sel(struct xhdmiphy_dev *inst, enum dir dir,
				    enum mmcmclk_insel sel)
{
	u32 reg_off, reg_val;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_MMCM_TXUSRCLK_CTRL_REG;
	else
		reg_off = XHDMIPHY_MMCM_RXUSRCLK_CTRL_REG;

	reg_val = xhdmiphy_read(inst, reg_off);

	if (sel == XHDMIPHY_MMCM_CLKINSEL_CLKIN2)
		reg_val &= ~XHDMIPHY_MMCM_USRCLK_CTRL_CLKINSEL_MASK;
	else
		reg_val |= XHDMIPHY_MMCM_USRCLK_CTRL_CLKINSEL_MASK;

	xhdmiphy_write(inst, reg_off, reg_val);
}

/**
 * xhdmiphy_reset_gtpll - This function will reset the GT's PLL logic.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID which to operate on
 * @dir:	dir is an indicator for TX or RX
 * @hold:	hold is an indicator whether to "hold" the reset if set to 1.
 *		If set to 0: reset, then enable.
 */
static void xhdmiphy_reset_gtpll(struct xhdmiphy_dev *inst, enum chid chid,
				 enum dir dir, u8 hold)
{
	u32 reg_val, mask_val, reg_off;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_TX_INIT_REG;
	else
		reg_off = XHDMIPHY_RX_INIT_REG;

	if (chid == XHDMIPHY_CHID_CHA)
		mask_val = XHDMIPHY_TXRX_INIT_PLLGTRESET_ALL_MASK;
	else
		mask_val = XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(chid);

	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val |= mask_val;
	xhdmiphy_write(inst, reg_off, reg_val);

	if (!hold) {
		reg_val &= ~mask_val;
		xhdmiphy_write(inst, reg_off, reg_val);
	}
}

u32 xhdmiphy_init_phy(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

	xhdmiphy_cfg_init(inst);

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
			XHDMIPHY_GT_STATE_IDLE;
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
			XHDMIPHY_GT_STATE_IDLE;
		if (inst->conf.transceiver_width == 2) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_data_width = 20;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_intdata_width = 2;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 20;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 2;
		} else {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_data_width = 40;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_intdata_width = 4;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_data_width = 40;
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_intdata_width = 4;
		}
	}
	xhdmiphy_clr(inst, XHDMIPHY_CLKDET_CTRL_REG,
		     XHDMIPHY_CLKDET_CTRL_RUN_MASK);

	/* sets clock detector frequency lock counter threshold value */
	xhdmiphy_write(inst, XHDMIPHY_CLKDET_FREQ_TMR_TO_REG,
		       inst->conf.axilite_freq);
	xhdmiphy_clkdet_freq_threshold(inst, 40);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_set_sys_clksel(inst);

		/* indicate of QPLL is present in design */
		if ((xhdmiphy_is_using_qpll(inst, XHDMIPHY_CHID_CH1,
					    XHDMIPHY_DIR_TX) &&
		     (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX))) ||
		    (xhdmiphy_is_using_qpll(inst, XHDMIPHY_CHID_CH1,
					    XHDMIPHY_DIR_RX) &&
		     (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX)))) {
			inst->qpll_present = true;
		} else {
			inst->qpll_present = false;
		}

		if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
		    inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4) {
			xhdmiphy_set_bufgtdiv(inst, XHDMIPHY_DIR_TX, 1);
			xhdmiphy_set_bufgtdiv(inst, XHDMIPHY_DIR_RX, 1);
		}
		xhdmiphy_powerdown_gtpll(inst, XHDMIPHY_CHID_CMNA, true);
		xhdmiphy_powerdown_gtpll(inst, XHDMIPHY_CHID_CHA, true);
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_RX, true);
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_TX, true);
	}

	xhdmiphy_mmcm_reset(inst, XHDMIPHY_DIR_TX, true);
	xhdmiphy_mmcm_reset(inst, XHDMIPHY_DIR_RX, true);

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX))
		xhdmiphy_ibufds_en(inst, XHDMIPHY_DIR_TX, (false));

	if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX))
		xhdmiphy_ibufds_en(inst, XHDMIPHY_DIR_RX, (false));

	/* dru Settings */
	if (inst->conf.dru_present) {
		xhdmiphy_ibufds_en(inst, XHDMIPHY_DIR_RX, true);
		xhdmiphy_dru_reset(inst, XHDMIPHY_CHID_CHA, true);
		xhdmiphy_dru_en(inst, XHDMIPHY_CHID_CHA, false);
	}

	if (inst->conf.gt_type != XHDMIPHY_GTYE5)
		xhdmiphy_set_rxlpm(inst, XHDMIPHY_CHID_CHA,
				   XHDMIPHY_DIR_RX, 0);

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		if (inst->conf.gt_type != XHDMIPHY_GTYE5)
			xhdmiphy_set_tx_vs(inst, (enum chid)id,
					   XHDMIPHY_HDMI_GTHE4_DEFAULT_VS_VAL);
		else
			xhdmiphy_set_tx_vs(inst, (enum chid)id,
					   XHDMIPHY_HDMI_DEFAULT_VS_VAL);

		xhdmiphy_set_tx_pe(inst, (enum chid)id,
				   XHDMIPHY_HDMI_DEFAULT_PC_PE_VAL);
		xhdmiphy_set_tx_pc(inst, (enum chid)id,
				   XHDMIPHY_HDMI_DEFAULT_PC_PE_VAL);
	}

	/* clear interrupt register */
	xhdmiphy_write(inst, XHDMIPHY_INTR_STS_REG, XHDMIPHY_INTR_STS_ALL_MASK);

	/* interrupt enable */
	xhdmiphy_intr_en(inst, XHDMIPHY_INTR_TXRESETDONE_MASK |
			 XHDMIPHY_INTR_RXRESETDONE_MASK);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_intr_en(inst, XHDMIPHY_INTR_CPLL_LOCK_MASK |
				 XHDMIPHY_INTR_QPLL_LOCK_MASK |
				 XHDMIPHY_INTR_TXALIGNDONE_MASK |
				 XHDMIPHY_INTR_QPLL1_LOCK_MASK);
	} else {
		xhdmiphy_intr_en(inst, XHDMIPHY_INTR_LCPLL_LOCK_MASK |
				 XHDMIPHY_INTR_RPLL_LOCK_MASK |
				 XHDMIPHY_INTR_TXGPO_RE_MASK |
				 XHDMIPHY_INTR_RXGPO_RE_MASK);
	}

	xhdmiphy_intr_en(inst, XHDMIPHY_INTR_TXFREQCHANGE_MASK |
			 XHDMIPHY_INTR_RXFREQCHANGE_MASK |
			 XHDMIPHY_INTR_TXMMCMUSRCLK_LOCK_MASK |
			 XHDMIPHY_INTR_TXTMRTIMEOUT_MASK |
			 XHDMIPHY_INTR_RXTMRTIMEOUT_MASK |
			 XHDMIPHY_INTR_RXMMCMUSRCLK_LOCK_MASK);

	xhdmiphy_set(inst, XHDMIPHY_CLKDET_CTRL_REG,
		     XHDMIPHY_CLKDET_CTRL_RUN_MASK);

	return 0;
}

/**
 * xhdmiphy_tx_align_rst - This function resets the GT TX alignment module.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @rst:	rst specifies true/false value to either assert or deassert
 *		reset on the TX alignment module, respectively
 */
static void xhdmiphy_tx_align_rst(struct xhdmiphy_dev *inst, enum chid chid,
				  u8 rst)
{
	u32 reg_val, mask_val = 0;
	u8 id, id0, id1;

	/* read Tx align register */
	reg_val = xhdmiphy_read(inst, XHDMIPHY_TX_BUFFER_BYPASS_REG);

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++)
		mask_val |= XHDMIPHY_TX_BUFFER_BYPASS_TXPHDLYRESET_MASK(id);

	xhdmiphy_set_clr(inst, XHDMIPHY_TX_BUFFER_BYPASS_REG, reg_val, mask_val,
			 rst);
}

/**
 * xhdmiphy_tx_align_start - This function resets the GT TX alignment module.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @start:	Start specifies true/false value to either start or ttop the TX
 *		alignment module, respectively
 */
static void xhdmiphy_tx_align_start(struct xhdmiphy_dev *inst, enum chid chid,
				    u8 start)
{
	u32 reg_val,  mask_val = 0;
	u8 id, id0, id1;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_TX_BUFFER_BYPASS_REG);

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++)
		mask_val |= XHDMIPHY_TX_BUFFER_BYPASS_TXPHALIGN_MASK(id);

	xhdmiphy_set_clr(inst, XHDMIPHY_TX_BUFFER_BYPASS_REG, reg_val, mask_val,
			 start);
}

static void xhdmiphy_txgpo_risingedge_handler(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

	xhdmiphy_check_linerate_cfg(inst, XHDMIPHY_CHID_CH1, XHDMIPHY_DIR_TX);
	xhdmiphy_set_gpi(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_TX, false);

	/* wait for GPO TX = 0 */
	while (xhdmiphy_get_gpo(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_TX))
		;

	xhdmiphy_mmcm_start(inst, XHDMIPHY_DIR_TX);

	xhdmiphy_dir_reconf(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_TX);

	/* deassert reset on GT Reset IP TX */
	xhdmiphy_write(inst, XHDMIPHY_COMMON_INIT_REG,
		       (xhdmiphy_read(inst,
		       XHDMIPHY_COMMON_INIT_REG) & ~0x1));
	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
						XHDMIPHY_GT_STATE_LOCK;
	}
}

/**
 * xhdmiphy_lcpll_param - This function calculates the lcpll parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	- 0 if calculated LCPLL parameters updated successfully
 *		- 1 if parameters not updated
 */
static u32 xhdmiphy_lcpll_param(struct xhdmiphy_dev *inst, enum chid chid,
				enum dir dir)
{
	u64 linerate = 0;
	u32 status = 0;
	u32 *refclk_ptr;
	u8 tmdsclk_ratio = 0, is_hdmi21 = 0;

	/* pre-calculation */
	if (dir == XHDMIPHY_DIR_RX) {
		refclk_ptr = &inst->rx_refclk_hz;
		is_hdmi21 = inst->rx_hdmi21_cfg.is_en;
		tmdsclk_ratio = inst->rx_tmdsclock_ratio;

		/* calculate line rate */
		if (is_hdmi21)
			linerate = inst->rx_hdmi21_cfg.linerate;
		else
			linerate = (u64)(*refclk_ptr) *
				   ((tmdsclk_ratio ? 40 : 10));

		inst->rx_dru_enabled = 0;

		/* enable DRU based on incoming REFCLK */
		if (!is_hdmi21 && !tmdsclk_ratio &&
		    inst->rx_refclk_hz < XHDMIPHY_LCPLL_MIN_REFCLK) {
			if (inst->conf.dru_present) {
				/* check DRU frequency */
				if (xhdmiphy_get_dru_refclk(inst) == 1) {
					dev_err(inst->dev,
						"cannot get dru refclk\n");
					return 1;
				}

				inst->rx_dru_enabled = 1;
				linerate = XHDMIPHY_HDMI_GTYE5_DRU_LRATE;
			} else {
				dev_err(inst->dev, "dru is not present\n");
				return 1;
			}
		}
	} else {
		refclk_ptr = &inst->tx_refclk_hz;
		is_hdmi21 = inst->tx_hdmi21_cfg.is_en;
		inst->tx_samplerate = 1;

		if (!is_hdmi21) {
			/* Determine if HDMI 2.0 mode */
			if (*refclk_ptr >= XHDMIPHY_HDMI20_REFCLK_RANGE7) {
				tmdsclk_ratio = 1;
				(*refclk_ptr) = (*refclk_ptr) / 4;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE5) {
			/* check for x1 Over sampling mode */
				inst->tx_samplerate = 1;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE3) {
			/* check for x2 Over sampling mode */
				inst->tx_samplerate = 2;
				(*refclk_ptr) = (*refclk_ptr) * 2;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE1) {
			/* check for x3 Over sampling mode */
				inst->tx_samplerate = 3;
				(*refclk_ptr) = (*refclk_ptr) * 3;
			} else if (*refclk_ptr <
				   XHDMIPHY_HDMI20_REFCLK_RANGE1) {
			/* check for x5 Over sampling mode */
				inst->tx_samplerate = 5;
				(*refclk_ptr) = (*refclk_ptr) * 5;
			}
		}

		if (is_hdmi21)
			linerate = inst->tx_hdmi21_cfg.linerate;
		else
			linerate = (u64)(*refclk_ptr) *
						((tmdsclk_ratio ? 40 : 10));
	}

	/* check for DRU mode */
	if (dir == XHDMIPHY_DIR_RX &&
	    inst->rx_dru_enabled) {
		inst->quad.lcpll.linerate_cfg = 0;
	} else if (!is_hdmi21) {
		/* check for HDMI 1.4/2.0 GT linerate config */
		/* HDMI 1.4 */
		if (!tmdsclk_ratio) {
			if ((XHDMIPHY_HDMI14_REFCLK_RANGE1 <= (*refclk_ptr)) &&
			    ((*refclk_ptr) <= XHDMIPHY_HDMI14_REFCLK_RANGE2)) {
				inst->quad.lcpll.linerate_cfg = 1;
			} else if ((XHDMIPHY_HDMI14_REFCLK_RANGE3 <= (*refclk_ptr)) &&
						/* 297 MHz + 0.5% + 10 KHz error */
						((*refclk_ptr) <=
						 XHDMIPHY_HDMI14_REFCLK_RANGE3)) {
				inst->quad.lcpll.linerate_cfg = 2;
			} else {
				status = 1;
			}
		} else {
			/* HDMI 2.0 */
			if ((XHDMIPHY_HDMI20_REFCLK_RANGE2 <= (*refclk_ptr)) &&
			    ((*refclk_ptr) <= XHDMIPHY_HDMI20_REFCLK_RANGE4)) {
				inst->quad.lcpll.linerate_cfg = 3;
			} else if ((XHDMIPHY_HDMI20_REFCLK_RANGE4 <= (*refclk_ptr)) &&
				   ((*refclk_ptr) <=
				    XHDMIPHY_HDMI20_REFCLK_RANGE6)) {
				inst->quad.lcpll.linerate_cfg = 4;
			} else {
				status = 1;
			}
		}
	} else if (is_hdmi21) {
		/* check for HDMI 2.1 GT linerate config */
		if (linerate == XHDMIPHY_LRATE_3G)
			inst->quad.lcpll.linerate_cfg = 5;
		else if (linerate == XHDMIPHY_LRATE_6G)
			inst->quad.lcpll.linerate_cfg = 6;
		else if (linerate == XHDMIPHY_LRATE_8G)
			inst->quad.lcpll.linerate_cfg = 7;
		else if (linerate == XHDMIPHY_LRATE_10G)
			inst->quad.lcpll.linerate_cfg = 8;
		else if (linerate == XHDMIPHY_LRATE_12G)
			inst->quad.lcpll.linerate_cfg = 9;
		else
			status = 1;
	}

	xhdmiphy_cfg_linerate(inst, XHDMIPHY_CHID_CMN0, linerate);

	if (status == 1)
		dev_err(inst->dev, "failed to configure lcpll params\n");

	return status;
}

/**
 * xhdmiphy_rpll_param - This function calculates the rpll parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel Id to operate on
 * @dir:	dir is an indicator for Rx or Tx
 *
 * @return:	- 0 if calculated RPLL parameters updated successfully
 *		- 1 if parameters not updated
 */
static u32 xhdmiphy_rpll_param(struct xhdmiphy_dev *inst, enum chid chid,
			       enum dir dir)
{
	u64 linerate = 0;
	u32 status = 0;
	u32 *refclk_ptr;
	u8 tmdsclk_ratio = 0, is_hdmi21 = 0;

	/* Pre-calculation */
	if (dir == XHDMIPHY_DIR_RX) {
		refclk_ptr = &inst->rx_refclk_hz;
		is_hdmi21 = inst->tx_hdmi21_cfg.is_en;
		tmdsclk_ratio = inst->rx_tmdsclock_ratio;

		/* calculate line rate */
		if (is_hdmi21) {
			linerate = inst->rx_hdmi21_cfg.linerate;
		} else {
			linerate = (u64)(*refclk_ptr) *
						((tmdsclk_ratio ? 40 : 10));
		}

		inst->rx_dru_enabled = 0;

		/* enable DRU based on incoming REFCLK */
		if (!is_hdmi21 && !tmdsclk_ratio &&
		    inst->rx_refclk_hz < XHDMIPHY_RPLL_MIN_REFCLK) {
			if (inst->conf.dru_present) {
				/* check DRU frequency */
				if (xhdmiphy_get_dru_refclk(inst) == 1) {
					dev_err(inst->dev,
						"cannot get dru refclk\n");
					return 1;
				}

				inst->rx_dru_enabled = 1;
				linerate = XHDMIPHY_HDMI_GTYE5_DRU_LRATE;
			} else {
				dev_err(inst->dev, "dru is not present\n");
				return 1;
			}
		}
	} else {
		refclk_ptr = &inst->tx_refclk_hz;
		is_hdmi21 = inst->tx_hdmi21_cfg.is_en;
		inst->tx_samplerate = 1;

		if (!is_hdmi21) {
			/* determine if HDMI 2.0 mode */
			if (*refclk_ptr >= XHDMIPHY_HDMI20_REFCLK_RANGE7) {
				tmdsclk_ratio = 1;
				(*refclk_ptr) = (*refclk_ptr) / 4;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE5) {
			/* check for x1 over sampling mode */
				inst->tx_samplerate = 1;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE3) {
			/* check for x2 Over sampling mode */
				inst->tx_samplerate = 2;
				(*refclk_ptr) = (*refclk_ptr) * 2;
			} else if (*refclk_ptr >=
				   XHDMIPHY_HDMI20_REFCLK_RANGE1) {
			/* check for x3 Over sampling mode */
				inst->tx_samplerate = 3;
				(*refclk_ptr) = (*refclk_ptr) * 3;
			/* check for x5 Over sampling mode */
			} else if (*refclk_ptr <
				   XHDMIPHY_HDMI20_REFCLK_RANGE1) {
				inst->tx_samplerate = 5;
				(*refclk_ptr) = (*refclk_ptr) * 5;
			}
		}

		/* calculate line rate */
		if (is_hdmi21)
			linerate = inst->tx_hdmi21_cfg.linerate;
		else
			linerate = (u64)(*refclk_ptr) * ((tmdsclk_ratio ? 40 : 10));
	}

	/* check for DRU mode */
	if (dir == XHDMIPHY_DIR_RX && inst->rx_dru_enabled) {
		inst->quad.rpll.linerate_cfg = 0;
	/* check for HDMI 1.4/2.0 GT linerate config */
	} else if (!is_hdmi21) {
		/* HDMI 1.4 */
		if (!tmdsclk_ratio) {
			if ((XHDMIPHY_HDMI14_REFCLK_RANGE1 <= (*refclk_ptr)) &&
			    ((*refclk_ptr) <= 200000000)) {
				inst->quad.rpll.linerate_cfg = 1;
			} else if ((200000000 <= (*refclk_ptr)) &&
				   ((*refclk_ptr) <=
				    XHDMIPHY_HDMI14_REFCLK_RANGE3)) {
				inst->quad.rpll.linerate_cfg = 2;
			} else {
				status = 1;
			}
		/* HDMI 2.0 */
		} else {
			if ((XHDMIPHY_HDMI20_REFCLK_RANGE2 <= (*refclk_ptr)) &&
			    ((*refclk_ptr) <= 100000000)) {
				inst->quad.rpll.linerate_cfg = 3;
			} else if ((100000000 <= (*refclk_ptr)) &&
				   ((*refclk_ptr) <=
				    XHDMIPHY_HDMI20_REFCLK_RANGE6)) {
				inst->quad.rpll.linerate_cfg = 4;
			} else {
				status = 1;
			}
		}
	/* check for HDMI 2.1 gt linerate config */
	} else if (is_hdmi21) {
		if (linerate == XHDMIPHY_LRATE_3G)
			inst->quad.rpll.linerate_cfg = 5;
		else if (linerate == XHDMIPHY_LRATE_6G)
			inst->quad.rpll.linerate_cfg = 6;
		else if (linerate == XHDMIPHY_LRATE_8G)
			inst->quad.rpll.linerate_cfg = 7;
		else if (linerate == XHDMIPHY_LRATE_10G)
			inst->quad.rpll.linerate_cfg = 8;
		else if (linerate == XHDMIPHY_LRATE_12G)
			inst->quad.rpll.linerate_cfg = 9;
		else
			status = 1;
	}

	/* update line rate value */
	xhdmiphy_cfg_linerate(inst, XHDMIPHY_CHID_CMN1, linerate);
	if (status == 1)
		dev_err(inst->dev, "failed to configure rpll params\n");

	return status;
}

/**
 * xhdmiphy_txpll_param - This function calculates the Tx pll parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel ID to operate on
 *
 * @return:	- 0 if calculated qpll parameters updated successfully
 *		- 1 if parameters not updated
 */
static u32 xhdmiphy_txpll_param(struct xhdmiphy_dev *inst, enum chid chid)
{
	enum pll_type pll_type;

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					 XHDMIPHY_CHID_CH1);

	if (pll_type == XHDMIPHY_PLL_LCPLL)
		return xhdmiphy_lcpll_param(inst, chid, XHDMIPHY_DIR_TX);

	return xhdmiphy_rpll_param(inst, chid, XHDMIPHY_DIR_TX);
}

/**
 * xhdmiphy_rxpll_param - This function calculates the Rx pll parameters.
 *
 * @inst:	inst is a pointer to the HDMI GT core instance
 * @chid:	chid is the channel id to operate on
 *
 * @return:	- 0 if calculated qpll parameters updated
 *		successfully
 *		- 1 if parameters not updated
 */
static bool xhdmiphy_rxpll_param(struct xhdmiphy_dev *inst, enum chid chid)
{
	enum pll_type pll_type;

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					 XHDMIPHY_CHID_CH1);

	if (pll_type == XHDMIPHY_PLL_LCPLL)
		return xhdmiphy_lcpll_param(inst, chid, XHDMIPHY_DIR_RX);

	return xhdmiphy_rpll_param(inst, chid, XHDMIPHY_DIR_RX);
}

/**
 * xhdmiphy_set_tx_param - This function update/set the HDMI TX parameter.
 *
 * @inst:		is a pointer to the Hdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @ppc:		ppc is the pixels per clock to set
 * @bpc:		bpc is the bits per color to set
 * @fmt:		fmt is the color format to set
 *
 * @return:
 *		- 0 if TX parameters set/updated
 *		- gc if low resolution video not supported
 */
u32 xhdmiphy_set_tx_param(struct xhdmiphy_dev *inst, enum chid chid,
			  enum ppc ppc, enum color_depth bpc,
			  enum color_fmt fmt)
{
	u32 status;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		/*
		 * only calculate the QPLL/CPLL parameters when the GT TX and
		 * RX are not coupled
		 */
		if (xhdmiphy_is_tx_using_cpll(inst, chid)) {
			status = xhdmiphy_cpll_param(inst, chid, XHDMIPHY_DIR_TX);
		} else {
			status = xhdmiphy_qpll_param(inst, chid, XHDMIPHY_DIR_TX);
			/* update sysClk and PLL clk registers immediately */
			xhdmiphy_write_refclksel(inst);
		}
	} else {
		status = xhdmiphy_txpll_param(inst, chid);
	}

	if (status == 1)
		return status;

	/* Is HDMITXSS PPC match with HDMIPHY PPC? */
	if (ppc == inst->conf.ppc) {
		status = 0;
	} else {
		dev_err(inst->dev,
			"HDMITXSS ppc does't match with hdmiphy ppc\n");
		status = 1;
	}
	if (status == 0) {
		/* HDMI 2.1 */
		if (inst->tx_hdmi21_cfg.is_en) {
			xhdmiphy_mmcm_param(inst, XHDMIPHY_DIR_TX);

			return status;
		}

		/*
		 * calculate TXPLL parameters.
		 * In HDMI the colordepth in YUV422 is always 12 bits,
		 * although on the link itself it is being transmitted as
		 * 8-bits. Therefore if the colorspace is YUV422, then force the
		 * colordepth to 8 bits.
		 */
		if (fmt == XVIDC_CSF_YCRCB_422) {
			status = xhdmiphy_cal_mmcm_param(inst, chid,
							 XHDMIPHY_DIR_TX, ppc,
							 XVIDC_BPC_8);
		} else {
			status = xhdmiphy_cal_mmcm_param(inst, chid,
							 XHDMIPHY_DIR_TX, ppc,
							 bpc);
		}
	} else {
		status = 1;
	}

	return status;
}

static u32 xhdmiphy_set_rx_param(struct xhdmiphy_dev *inst, enum chid chid)
{
	u64 dru_center_freq;
	u32 status;
	enum chid ch_id = chid;
	enum pll_type pll_type;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if (xhdmiphy_is_rx_using_cpll(inst, chid)) {
			status = xhdmiphy_cpll_param(inst, chid,
						     XHDMIPHY_DIR_RX);
		} else {
			status = xhdmiphy_qpll_param(inst, chid,
						     XHDMIPHY_DIR_RX);
			/* update SysClk and PLL clk registers immediately */
			xhdmiphy_write_refclksel(inst);
		}
	} else {
		status = xhdmiphy_rxpll_param(inst, chid);
	}

	if (inst->rx_dru_enabled) {
		pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
						 XHDMIPHY_CHID_CH1);
		/* update the chid */
		ch_id = xhdmiphy_get_rcfg_chid(pll_type);
		dru_center_freq = xhdmiphy_dru_cal_centerfreq(inst, ch_id);
		xhdmiphy_set_dru_centerfreq(inst, XHDMIPHY_CHID_CHA,
					    dru_center_freq);
	}

	return status;
}

static void xhdmiphy_tx_timertimeout_handler(struct xhdmiphy_dev *inst)
{
	enum chid chid;
	enum pll_type pll_type;
	u8 val_cmp, id, id0, id1;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
						 XHDMIPHY_CHID_CH1);
		/* determine which channel(s) to operate on */
		chid = xhdmiphy_get_rcfg_chid(pll_type);
		xhdmiphy_mmcm_start(inst, XHDMIPHY_DIR_TX);
		xhdmiphy_powerdown_gtpll(inst, (pll_type == XHDMIPHY_PLL_CPLL) ?
					       XHDMIPHY_CHID_CHA :
					       XHDMIPHY_CHID_CMNA,
					       false);

		if (pll_type != XHDMIPHY_PLL_CPLL)
			xhdmiphy_write_refclksel(inst);

		xhdmiphy_clk_reconf(inst, chid);
		xhdmiphy_outdiv_reconf(inst, XHDMIPHY_CHID_CHA,
				       XHDMIPHY_DIR_TX);
		if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
		    inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4) {
			xhdmiphy_set_bufgtdiv(inst, XHDMIPHY_DIR_TX,
					      (pll_type == XHDMIPHY_PLL_CPLL) ?
					      inst->quad.plls->tx_outdiv :
					      (inst->quad.plls->tx_outdiv != 16) ?
					      inst->quad.plls->tx_outdiv :
					      inst->quad.plls->tx_outdiv / 2);
		}

		xhdmiphy_dir_reconf(inst, XHDMIPHY_CHID_CHA,
				    XHDMIPHY_DIR_TX);
		/* assert PLL reset */
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_TX, true);
		/* de-assert PLL reset */
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_TX, false);

		if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
		    inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4) {
			xhdmiphy_tx_align_start(inst, chid, false);
		}

		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_LOCK;
		}
	} else {
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
						XHDMIPHY_GT_STATE_GPO_RE;
		}
		/* compare the current and next CFG values */
		val_cmp = xhdmiphy_check_linerate_cfg(inst,
						      XHDMIPHY_CHID_CH1,
						      XHDMIPHY_DIR_TX);
		if (!val_cmp) {
			xhdmiphy_set_gpi(inst, XHDMIPHY_CHID_CHA,
					 XHDMIPHY_DIR_TX, true);
		} else {
			xhdmiphy_txgpo_risingedge_handler(inst);
		}
	}
}

static void xhdmiphy_rxgpo_risingedge_handler(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

	xhdmiphy_check_linerate_cfg(inst, XHDMIPHY_CHID_CH1, XHDMIPHY_DIR_RX);
	/* De-assert GPI port. */
	xhdmiphy_set_gpi(inst, XHDMIPHY_CHID_CHA,
			 XHDMIPHY_DIR_RX, false);

	/* Wait for GPO RX = 0 */
	while (xhdmiphy_get_gpo(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_RX))
		;

	/* Configure RXRATE Port */
	xhdmiphy_dir_reconf(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_RX);
	/* Deassert reset on GT Reset IP RX */
	xhdmiphy_write(inst, XHDMIPHY_COMMON_INIT_REG,
		       (xhdmiphy_read(inst,
		       XHDMIPHY_COMMON_INIT_REG) & ~0x2));
	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
						XHDMIPHY_GT_STATE_LOCK;
	}
}

static void xhdmiphy_rx_timertimeout_handler(struct xhdmiphy_dev *inst)
{
	enum chid chid;
	enum pll_type pll_type;
	u32 status;
	u8 id, id0, id1, val_cmp;

	if (!inst->rx_hdmi21_cfg.is_en) {
		dev_info(inst->dev, "xhdmi 2.0 protocl is enabled\n");
	} else {
		if (inst->conf.rx_refclk_sel == inst->conf.rx_frl_refclk_sel) {
			xhdmiphy_mmcm_clkin_sel(inst, XHDMIPHY_DIR_RX,
						XHDMIPHY_MMCM_CLKINSEL_CLKIN1);
			xhdmiphy_mmcm_start(inst, XHDMIPHY_DIR_RX);
		}
	}

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					 XHDMIPHY_CHID_CH1);
	/* determine which channel(s) to operate on */
	chid = xhdmiphy_get_rcfg_chid(pll_type);
	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

	status = xhdmiphy_set_rx_param(inst, chid);
	if (status != 0) {
		for (id = id0; id <= id1; id++) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_IDLE;
		}

		return;
	}

	/* enable DRU to set the clock muxes */
	xhdmiphy_dru_en(inst, XHDMIPHY_CHID_CHA, inst->rx_dru_enabled);

	xhdmiphy_dru_mode_en(inst, inst->rx_dru_enabled);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		/* enable PLL */
		xhdmiphy_powerdown_gtpll(inst, (pll_type == XHDMIPHY_PLL_CPLL) ?
					 XHDMIPHY_CHID_CHA :
					 XHDMIPHY_CHID_CMNA, false);

		/* update reference clock election */
		if (!inst->rx_hdmi21_cfg.is_en) {
			xhdmiphy_pll_refclk_sel(inst,
						((pll_type == XHDMIPHY_PLL_CPLL) ?
						XHDMIPHY_CHID_CHA :
						XHDMIPHY_CHID_CMNA),
						((inst->rx_dru_enabled) ?
						inst->conf.dru_refclk_sel :
						inst->conf.rx_refclk_sel));
		}
		xhdmiphy_write_refclksel(inst);

		pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
						 XHDMIPHY_CHID_CH1);
		/* determine which channel(s) to operate on */
		chid = xhdmiphy_get_rcfg_chid(pll_type);
		xhdmiphy_clk_reconf(inst, chid);
		xhdmiphy_outdiv_reconf(inst, XHDMIPHY_CHID_CHA,
				       XHDMIPHY_DIR_RX);
		xhdmiphy_dir_reconf(inst, XHDMIPHY_CHID_CHA,
				    XHDMIPHY_DIR_RX);
		/* assert RX PLL reset */
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_RX, true);
		/* de-assert RX PLL reset */
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_RX, false);
		for (id = id0; id <= id1; id++) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_LOCK;
		}
	} else {
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++) {
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
						XHDMIPHY_GT_STATE_GPO_RE;
		}

		/* compare the current and next CFG values */
		val_cmp = xhdmiphy_check_linerate_cfg(inst, XHDMIPHY_CHID_CH1,
						      XHDMIPHY_DIR_RX);
		if (!val_cmp) {
			xhdmiphy_set_gpi(inst, XHDMIPHY_CHID_CHA,
					 XHDMIPHY_DIR_RX, true);
		} else {
			xhdmiphy_rxgpo_risingedge_handler(inst);
		}
	}
}

void xhdmiphy_hdmi20_conf(struct xhdmiphy_dev *inst, enum dir dir)
{
	enum pll_type pll_type;

	pll_type = xhdmiphy_get_pll_type(inst, dir, XHDMIPHY_CHID_CH1);

	if (dir == XHDMIPHY_DIR_TX) {
		inst->tx_hdmi21_cfg.linerate = 0;
		inst->tx_hdmi21_cfg.nchannels = 0;
		inst->tx_hdmi21_cfg.is_en = false;
	} else {
		inst->rx_hdmi21_cfg.linerate = 0;
		inst->rx_hdmi21_cfg.nchannels = 0;
		inst->rx_hdmi21_cfg.is_en = false;
	}

	xhdmiphy_mmcm_clkin_sel(inst, dir, XHDMIPHY_MMCM_CLKINSEL_CLKIN1);

	if (dir == XHDMIPHY_DIR_TX)
		xhdmiphy_intr_en(inst, XHDMIPHY_INTR_TXFREQCHANGE_MASK);
	else
		xhdmiphy_intr_en(inst, XHDMIPHY_INTR_RXFREQCHANGE_MASK);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_pll_refclk_sel(inst, ((pll_type == XHDMIPHY_PLL_CPLL) ?
					XHDMIPHY_CHID_CHA : XHDMIPHY_CHID_CMNA),
					((dir == XHDMIPHY_DIR_TX) ?
					inst->conf.tx_refclk_sel :
					inst->conf.rx_refclk_sel));
		xhdmiphy_write_refclksel(inst);
	}
}

/**
 * xhdmiphy_hdmi21_conf - This function will configure the GT for HDMI 2.1
 * operation
 *
 * @inst:	inst is a pointer to the Hdmiphy core instance
 * @dir:	chid is the channel ID to operate on
 * @linerate:	dir is an indicator for RX or TX
 * @nchannels:	no of channels
 *
 * @return:	- 0 if Tx parameters set/updated
 *		- 1 if low resolution video not supported
 */
u32 xhdmiphy_hdmi21_conf(struct xhdmiphy_dev *inst, enum dir dir, u64 linerate,
			 u8 nchannels)
{
	enum pll_type pll_type;
	u32 status = 0;

	if (inst->conf.gt_type == XHDMIPHY_GTHE4 ||
	    inst->conf.gt_type == XHDMIPHY_GTYE4) {
		char speedgrade[5] = "-2";
		char comp_val[5] = "-1";
		char *speed_grade_ptr = &speedgrade[0];
		char *compval_ptr = &comp_val[0];

		if (strncmp(speed_grade_ptr, compval_ptr, 2) == 0) {
			if (linerate > XHDMIPHY_LRATE_8G) {
				dev_err(inst->dev, "linkrate is not supported\n");
				return 1;
			}
		}
	}

	pll_type = xhdmiphy_get_pll_type(inst, dir, XHDMIPHY_CHID_CH1);
	if (dir == XHDMIPHY_DIR_TX) {
		if (inst->conf.tx_refclk_sel != inst->conf.tx_frl_refclk_sel)
			xhdmiphy_intr_dis(inst,
					  XHDMIPHY_INTR_TXFREQCHANGE_MASK);

		/* enable 4th channel output */
		xhdmiphy_clkout1_obuftds_en(inst, XHDMIPHY_DIR_TX, (true));
	} else {
		if (inst->conf.rx_refclk_sel != inst->conf.rx_frl_refclk_sel)
			xhdmiphy_intr_dis(inst, XHDMIPHY_INTR_RXFREQCHANGE_MASK);
	}

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_pll_refclk_sel(inst, ((pll_type == XHDMIPHY_PLL_CPLL) ?
					XHDMIPHY_CHID_CHA : XHDMIPHY_CHID_CMNA),
					((dir == XHDMIPHY_DIR_TX) ?
					inst->conf.tx_frl_refclk_sel :
					inst->conf.rx_frl_refclk_sel));

		xhdmiphy_write_refclksel(inst);
	}

	/* update HDMI confurations */
	if (dir == XHDMIPHY_DIR_TX) {
		inst->tx_refclk_hz = XHDMIPHY_HDMI21_FRL_REFCLK;
		inst->tx_hdmi21_cfg.linerate = linerate;
		inst->tx_hdmi21_cfg.nchannels = nchannels;
		inst->tx_hdmi21_cfg.is_en = true;

		status = xhdmiphy_set_tx_param(inst,
					       ((pll_type == XHDMIPHY_PLL_CPLL) ?
					       XHDMIPHY_CHID_CHA :
					       XHDMIPHY_CHID_CMNA),
					       inst->conf.ppc, XVIDC_BPC_8,
					       XVIDC_CSF_RGB);

		xhdmiphy_mmcm_lock_en(inst, dir, true);

		if (inst->conf.tx_refclk_sel == inst->conf.tx_frl_refclk_sel)
			xhdmiphy_mmcm_clkin_sel(inst, dir,
						XHDMIPHY_MMCM_CLKINSEL_CLKIN1);
		else
			xhdmiphy_mmcm_clkin_sel(inst, dir,
						XHDMIPHY_MMCM_CLKINSEL_CLKIN2);

		if (inst->conf.tx_refclk_sel != inst->conf.tx_frl_refclk_sel)
			xhdmiphy_tx_timertimeout_handler(inst);
	} else {
		inst->rx_refclk_hz = XHDMIPHY_HDMI21_FRL_REFCLK;
		inst->rx_hdmi21_cfg.linerate = linerate;
		inst->rx_hdmi21_cfg.nchannels = nchannels;
		inst->rx_hdmi21_cfg.is_en = true;

		/* set mmcm dividers for frl mode */
		xhdmiphy_mmcm_param(inst, XHDMIPHY_DIR_RX);

		/* mask the mmcm lock */
		xhdmiphy_mmcm_lock_en(inst, dir, true);

		if (inst->conf.rx_refclk_sel != inst->conf.rx_frl_refclk_sel) {
			/* set mmcm clkinsel to clk2 */
			xhdmiphy_mmcm_clkin_sel(inst, dir,
						XHDMIPHY_MMCM_CLKINSEL_CLKIN2);

			xhdmiphy_mmcm_start(inst, XHDMIPHY_DIR_RX);

			xhdmiphy_rx_timertimeout_handler(inst);
		}
	}

	return status;
}

/**
 * xhdmiphy_is_pll_locked - This function will check the status of a PLL lock
 * on the specified channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID which to operate on
 *
 * @return:	- true if the specified PLL is locked
 *		- false otherwise
 */
static bool xhdmiphy_is_pll_locked(struct xhdmiphy_dev *inst, enum chid chid)
{
	u32 reg_val, mask_val;
	enum pll_type tx_pll, rx_pll;

	if (chid == XHDMIPHY_CHID_CMN0) {
		if (inst->conf.gt_type != XHDMIPHY_GTYE5)
			mask_val = XHDMIPHY_PLL_LOCK_STATUS_QPLL0_MASK;
		else
			mask_val = XHDMIPHY_PLL_LOCK_STATUS_LCPLL_MASK;
	} else if ((chid == XHDMIPHY_CHID_CMN1) &&
		   (inst->conf.gt_type != XHDMIPHY_GTYE5)) {
		mask_val = XHDMIPHY_PLL_LOCK_STATUS_QPLL1_MASK;
	} else if (chid == XHDMIPHY_CHID_CMN1) {
		mask_val = XHDMIPHY_PLL_LOCK_STATUS_RPLL_MASK;
	} else {
		/* This will result to 1 */
		mask_val = XHDMIPHY_PLL_LOCK_STATUS_CPLL_ALL_MASK;
	}

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if (chid == XHDMIPHY_CHID_CMNA) {
			mask_val = XHDMIPHY_PLL_LOCK_STATUS_QPLL0_MASK |
				   XHDMIPHY_PLL_LOCK_STATUS_QPLL1_MASK;
		} else if (chid == XHDMIPHY_CHID_CHA) {
			tx_pll = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
						       XHDMIPHY_CHID_CH1);
			rx_pll = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
						       XHDMIPHY_CHID_CH1);
			if (rx_pll == XHDMIPHY_PLL_CPLL &&
			    xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX)) {
				mask_val = XHDMIPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK;
			} else if (tx_pll == XHDMIPHY_PLL_CPLL &&
				   xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX)) {
				mask_val = XHDMIPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK;
			} else {
				mask_val = XHDMIPHY_PLL_LOCK_STATUS_CPLL_ALL_MASK;
			}
		} else {
			mask_val = XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(chid);
		}
	}

	reg_val = xhdmiphy_read(inst, XHDMIPHY_PLL_LOCK_STATUS_REG);

	if ((reg_val & mask_val) == mask_val)
		return true;

	return false;
}

static void xhdmiphy_lcpll_lock_handler(struct xhdmiphy_dev *inst)
{
	enum pll_type tx_pll_type;
	enum chid chid;
	u8 id, id0, id1;

	tx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					    XHDMIPHY_CHID_CH1);

	/* Determine which channel(s) to operate on */
	chid = xhdmiphy_get_rcfg_chid(XHDMIPHY_PLL_LCPLL);
	if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
		dev_info(inst->dev, "lcpll is locked\n");
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++) {
			if (tx_pll_type == XHDMIPHY_PLL_LCPLL)
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_RESET;
			else
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_RESET;
		}
	} else {
		dev_info(inst->dev, "lcpll lock lost !\n");
	}
}

static void xhdmiphy_rpll_lock_handler(struct xhdmiphy_dev *inst)
{
	enum chid chid;
	enum pll_type tx_pll_type;
	u8 id, id0, id1;

	tx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					    XHDMIPHY_CHID_CH1);
	/* determine which channel(s) to operate on */
	chid = xhdmiphy_get_rcfg_chid(XHDMIPHY_PLL_RPLL);
	if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
		dev_info(inst->dev, "rpll is locked\n");
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++) {
			if (tx_pll_type == XHDMIPHY_PLL_RPLL) {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_RESET;
			} else {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_RESET;
			}
		}
	} else {
		dev_info(inst->dev, "rpll lock lost !\n");
	}
}

/**
 * xhdmiphy_rst_gt_txrx - This function will reset the GT's TX/RX logic.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID which to operate on
 * @dir:	dir is an indicator for TX or RX
 * @hold:	hold is an indicator whether to "hold" the reset if set to 1
 *		If set to 0: reset, then enable
 */
static void xhdmiphy_rst_gt_txrx(struct xhdmiphy_dev *inst, enum chid chid,
				 enum dir dir, u8 hold)
{
	u32 reg_val, mask_val, reg_off;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_TX_INIT_REG;
	else
		reg_off = XHDMIPHY_RX_INIT_REG;

	if (chid == XHDMIPHY_CHID_CHA)
		mask_val = XHDMIPHY_TXRX_INIT_GTRESET_ALL_MASK;
	else
		mask_val = XHDMIPHY_TXRX_INIT_GTRESET_MASK(chid);

	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val |= mask_val;
	xhdmiphy_write(inst, reg_off, reg_val);

	if (!hold) {
		reg_val &= ~mask_val;
		xhdmiphy_write(inst, reg_off, reg_val);
	}
}

static void xhdmiphy_qpll_lock_handler(struct xhdmiphy_dev *inst)
{
	enum chid chid;
	enum pll_type tx_pll_type, rx_pll_type;
	u8 id, id0, id1;

	tx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					    XHDMIPHY_CHID_CH1);
	rx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					    XHDMIPHY_CHID_CH1);

	/* rX is using QPLL */
	if (rx_pll_type == XHDMIPHY_PLL_QPLL ||
	    rx_pll_type == XHDMIPHY_PLL_QPLL0 ||
	    rx_pll_type == XHDMIPHY_PLL_QPLL1) {
		chid = xhdmiphy_get_rcfg_chid(rx_pll_type);

		if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
			dev_info(inst->dev, "qpll is locked\n");
			xhdmiphy_rst_gt_txrx(inst, XHDMIPHY_CHID_CHA,
					     XHDMIPHY_DIR_RX, false);
			xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
			for (id = id0; id <= id1; id++)
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_RESET;

		} else {
			dev_info(inst->dev, "qpll lock lost!\n");
		}
	} else {
		/* tX is using QPLL */
		chid = xhdmiphy_get_rcfg_chid(tx_pll_type);
		if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
			dev_info(inst->dev, "qpll locked\n");
			xhdmiphy_rst_gt_txrx(inst, XHDMIPHY_CHID_CHA,
					     XHDMIPHY_DIR_TX, false);
			xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0,
					&id1);
			for (id = id0; id <= id1; id++) {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_RESET;
			}
		} else {
			dev_info(inst->dev, "qpll lock lost !\n");
		}
	}
}

static void xhdmiphy_cpll_lock_handler(struct xhdmiphy_dev *inst)
{
	enum pll_type tx_pll_type;
	enum pll_type rx_pll_type;
	enum chid chid;
	u8 id, id0, id1;

	tx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					    XHDMIPHY_CHID_CH1);
	rx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					    XHDMIPHY_CHID_CH1);
	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

	if (rx_pll_type == XHDMIPHY_PLL_CPLL) {
		chid = xhdmiphy_get_rcfg_chid(rx_pll_type);
		if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
			dev_info(inst->dev, "cpll locked\n");
			xhdmiphy_rst_gt_txrx(inst, XHDMIPHY_CHID_CHA,
					     XHDMIPHY_DIR_RX, false);
			for (id = id0; id <= id1; id++) {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_RESET;
			}
		} else {
			dev_info(inst->dev, "cpll lock lost\n");
		}
	} else {
		/*
		 * TX is using CPLL. Determine which channel(s)
		 * to operate on
		 */
		chid = xhdmiphy_get_rcfg_chid(tx_pll_type);
		if (xhdmiphy_is_pll_locked(inst, chid) == 0) {
			dev_info(inst->dev, "cpll locked\n");
			xhdmiphy_rst_gt_txrx(inst, XHDMIPHY_CHID_CHA,
					     XHDMIPHY_DIR_TX, false);
			for (id = id0; id <= id1; id++) {
				inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_RESET;
			}
		} else {
			dev_info(inst->dev, "cpll lock lost\n");
		}
	}
}

static void xhdmiphy_txgt_aligndone_handler(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
						XHDMIPHY_GT_STATE_READY;
}

static void xhdmiphy_txgt_rstdone_handler(struct xhdmiphy_dev *inst)
{
	enum pll_type pll_type;
	enum chid chid;
	u8 id, id0, id1;

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					 XHDMIPHY_CHID_CH1);
	chid = xhdmiphy_get_rcfg_chid(pll_type);

	/* Set TX TMDS Clock Pattern Generator */
	if (inst->conf.gt_as_tx_tmdsclk &&
	    (inst->tx_hdmi21_cfg.is_en == 0 ||
	    (inst->tx_hdmi21_cfg.is_en == 1 &&
	    inst->tx_hdmi21_cfg.nchannels == 3))) {
		xhdmiphy_patgen_set_ratio(inst,
					  (u64)((xhdmiphy_get_linerate(inst,
					  chid)) / 1000000));
		xhdmiphy_set(inst, XHDMIPHY_PATGEN_CTRL_REG,
			     XHDMIPHY_PATGEN_CTRL_ENABLE_MASK);
	} else {
		xhdmiphy_clr(inst, XHDMIPHY_PATGEN_CTRL_REG,
			     XHDMIPHY_PATGEN_CTRL_ENABLE_MASK);
	}

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
		    inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4) {
			xhdmiphy_tx_align_rst(inst, XHDMIPHY_CHID_CHA, true);
			xhdmiphy_tx_align_rst(inst, XHDMIPHY_CHID_CHA, false);
		}
		/* GT alignment */
		xhdmiphy_tx_align_start(inst, XHDMIPHY_CHID_CHA, true);
		xhdmiphy_tx_align_start(inst, XHDMIPHY_CHID_CHA, false);
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
		for (id = id0; id <= id1; id++)
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
						XHDMIPHY_GT_STATE_ALIGN;
	} else {
		/* Deassert TX LNKRDY MASK */
		xhdmiphy_write(inst, XHDMIPHY_TX_INIT_REG,
			       (xhdmiphy_read(inst, XHDMIPHY_TX_INIT_REG) &
			       ~XHDMIPHY_TXPCS_RESET_MASK));

		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

		for (id = id0; id <= id1; id++)
			inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_READY;
	}
}

static void xhdmiphy_rxgt_rstdone_handler(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
						XHDMIPHY_GT_STATE_READY;

	xhdmiphy_write(inst, XHDMIPHY_RX_INIT_REG,
		       (xhdmiphy_read(inst, XHDMIPHY_RX_INIT_REG) &
		       ~XHDMIPHY_RXPCS_RESET_MASK));
	if (inst->rx_dru_enabled)
		xhdmiphy_dru_reset(inst, XHDMIPHY_CHID_CHA, false);

	if (inst->phycb[RX_READY_CB].cb)
		inst->phycb[RX_READY_CB].cb(inst->phycb[RX_READY_CB].data);
}

static void xhdmiphy_tx_freqchnage_handler(struct xhdmiphy_dev *inst)
{
	enum pll_type pll_type;
	u8 id, id0, id1;

	if (inst->tx_hdmi21_cfg.is_en) {
		if (inst->conf.tx_refclk_sel != inst->conf.tx_frl_refclk_sel)
			return;
	}

	/* set tX TMDS clock pattern generator */
	if (inst->conf.gt_as_tx_tmdsclk &&
	    (inst->tx_hdmi21_cfg.is_en == 0 ||
	    (inst->tx_hdmi21_cfg.is_en == 1 &&
	    inst->tx_hdmi21_cfg.nchannels == 3)))
		xhdmiphy_clr(inst, XHDMIPHY_PATGEN_CTRL_REG,
			     XHDMIPHY_PATGEN_CTRL_ENABLE_MASK);

	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					 XHDMIPHY_CHID_CH1);

	/* If the TX frequency has changed, the PLL is always disabled */
	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_powerdown_gtpll(inst, (pll_type == XHDMIPHY_PLL_CPLL) ?
					 XHDMIPHY_CHID_CHA :
					 XHDMIPHY_CHID_CMNA, true);
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_TX, true);
	} else {
		/* Mask RESET DONE */
		/* Deassert TX LNKRDY MASK */
		xhdmiphy_write(inst, XHDMIPHY_TX_INIT_REG,
			       (xhdmiphy_read(inst,
			       XHDMIPHY_TX_INIT_REG) |
			       XHDMIPHY_TXPCS_RESET_MASK));
	}

	xhdmiphy_mmcm_lock_en(inst, XHDMIPHY_DIR_TX, true);
	xhdmiphy_set(inst, XHDMIPHY_CLKDET_CTRL_REG,
		     XHDMIPHY_CLKDET_CTRL_TX_TMR_CLR_MASK);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5)
		xhdmiphy_tx_align_start(inst, XHDMIPHY_CHID_CHA, false);

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].tx_state =
							XHDMIPHY_GT_STATE_IDLE;

	/* If there is no reference clock, load TX timer in usec */
	if (xhdmiphy_read(inst, XHDMIPHY_CLKDET_FREQ_TX_REG))
		xhdmiphy_write(inst, XHDMIPHY_CLKDET_TMR_TX_REG,
			       inst->conf.axilite_freq / 1000);
}

static void xhdmiphy_rx_freqchange_handler(struct xhdmiphy_dev *inst)
{
	enum pll_type pll_type;
	u32 rx_refclk;
	u8 id, id0, id1;

	if (inst->rx_hdmi21_cfg.is_en)
		if (inst->conf.rx_refclk_sel != inst->conf.rx_frl_refclk_sel)
			return;

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].rx_state =
							XHDMIPHY_GT_STATE_IDLE;
	if (!inst->rx_hdmi21_cfg.is_en)
		/* mask the MMCM Lock */
		xhdmiphy_mmcm_lock_en(inst, XHDMIPHY_DIR_RX, true);

	/* determine PLL type and RX reference clock selection */
	pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					 XHDMIPHY_CHID_CH1);

	/* fetch New RX Reference Clock Frequency */
	rx_refclk = xhdmiphy_read(inst, XHDMIPHY_CLKDET_FREQ_RX_REG);

	/* round input frequency to 10 kHz */
	rx_refclk = (rx_refclk + 5000) / 10000;
	rx_refclk = rx_refclk * 10000;

	/* store RX reference clock */
	if (inst->rx_hdmi21_cfg.is_en)
		inst->rx_refclk_hz = XHDMIPHY_HDMI21_FRL_REFCLK;
	else
		inst->rx_refclk_hz = rx_refclk;

	/* If the RX frequency has changed, the PLL is always disabled */
	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		xhdmiphy_powerdown_gtpll(inst, (pll_type == XHDMIPHY_PLL_CPLL) ?
					 XHDMIPHY_CHID_CHA :
					 XHDMIPHY_CHID_CMNA, true);
		xhdmiphy_reset_gtpll(inst, XHDMIPHY_CHID_CHA,
				     XHDMIPHY_DIR_RX, true);
	} else {
		xhdmiphy_write(inst, XHDMIPHY_RX_INIT_REG,
			       (xhdmiphy_read(inst, XHDMIPHY_RX_INIT_REG) |
					      XHDMIPHY_RXPCS_RESET_MASK));
	}

	/* if DRU is present, disable it and assert reset */
	if (inst->conf.dru_present) {
		xhdmiphy_dru_reset(inst, XHDMIPHY_CHID_CHA, true);
		xhdmiphy_dru_en(inst, XHDMIPHY_CHID_CHA, false);
	}
	/* clear RX timer */
	xhdmiphy_set(inst, XHDMIPHY_CLKDET_CTRL_REG,
		     XHDMIPHY_CLKDET_CTRL_RX_TMR_CLR_MASK);
	/*
	 * If there is reference clock, load RX timer in usec.
	 * The reference clock should be larger than 25Mhz. We are using a
	 * 20Mhz instead to keep some margin for errors.
	 */

	if (rx_refclk > 20000000) {
		xhdmiphy_write(inst, XHDMIPHY_CLKDET_TMR_RX_REG,
			       inst->conf.axilite_freq / 1000);
		if (inst->phycb[RX_INIT_CB].cb)
			inst->phycb[RX_INIT_CB].cb(inst->phycb[RX_INIT_CB].data);
	}
}

void xhdmiphy_gt_handler(struct xhdmiphy_dev *inst, u32 event_ack, u32 event)
{
	enum gt_state *tx_state;
	enum gt_state *rx_state;
	enum pll_type tx_pll_type;
	enum pll_type rx_pll_type;

	/* read states for Ch1 */
	tx_state = &inst->quad.ch1.tx_state;
	rx_state = &inst->quad.ch1.rx_state;

	/* determine PLL type */
	tx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX,
					    XHDMIPHY_CHID_CH1);
	rx_pll_type = xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX,
					    XHDMIPHY_CHID_CH1);
	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if ((event & XHDMIPHY_INTR_QPLL0_LOCK_MASK) ||
		    (event & XHDMIPHY_INTR_QPLL1_LOCK_MASK))
			xhdmiphy_qpll_lock_handler(inst);

		if (event & XHDMIPHY_INTR_CPLL_LOCK_MASK)
			xhdmiphy_cpll_lock_handler(inst);

		if ((event & XHDMIPHY_INTR_TXRESETDONE_MASK) &&
		    (*tx_state == XHDMIPHY_GT_STATE_RESET))
			xhdmiphy_txgt_rstdone_handler(inst);

		if ((event & XHDMIPHY_INTR_TXALIGNDONE_MASK) &&
		    (*tx_state == XHDMIPHY_GT_STATE_ALIGN))
			xhdmiphy_txgt_aligndone_handler(inst);

		if ((event & XHDMIPHY_INTR_RXRESETDONE_MASK) &&
		    (*rx_state == XHDMIPHY_GT_STATE_RESET))
			xhdmiphy_rxgt_rstdone_handler(inst);
	} else {
		if (event & XHDMIPHY_INTR_TXGPO_RE_MASK)
			xhdmiphy_txgpo_risingedge_handler(inst);

		if (event & XHDMIPHY_INTR_RXGPO_RE_MASK)
			xhdmiphy_rxgpo_risingedge_handler(inst);

		if ((event & XHDMIPHY_INTR_LCPLL_LOCK_MASK) &&
		    (((*tx_state != XHDMIPHY_GT_STATE_IDLE) &&
		    tx_pll_type == XHDMIPHY_PLL_LCPLL) ||
		    ((*rx_state != XHDMIPHY_GT_STATE_IDLE) &&
		    rx_pll_type == XHDMIPHY_PLL_LCPLL))) {
			xhdmiphy_lcpll_lock_handler(inst);
		}

		if ((event & XHDMIPHY_INTR_RPLL_LOCK_MASK) &&
		    (((*tx_state != XHDMIPHY_GT_STATE_IDLE) &&
		    tx_pll_type == XHDMIPHY_PLL_RPLL) ||
		    ((*rx_state != XHDMIPHY_GT_STATE_IDLE) &&
		    rx_pll_type == XHDMIPHY_PLL_RPLL)))
			xhdmiphy_rpll_lock_handler(inst);

		if ((event & XHDMIPHY_INTR_TXRESETDONE_MASK) &&
		    (*tx_state == XHDMIPHY_GT_STATE_RESET))
			xhdmiphy_txgt_rstdone_handler(inst);

		if ((event & XHDMIPHY_INTR_RXRESETDONE_MASK) &&
		    (*rx_state == XHDMIPHY_GT_STATE_RESET))
			xhdmiphy_rxgt_rstdone_handler(inst);
	}

	xhdmiphy_write(inst, XHDMIPHY_INTR_STS_REG, event_ack);
}

void xhdmiphy_clkdet_handler(struct xhdmiphy_dev *inst, u32 event_ack,
			     u32 event)
{
	if (event & XHDMIPHY_INTR_TXFREQCHANGE_MASK)
		xhdmiphy_tx_freqchnage_handler(inst);

	if (event & XHDMIPHY_INTR_RXFREQCHANGE_MASK)
		xhdmiphy_rx_freqchange_handler(inst);

	if (event & XHDMIPHY_INTR_TXTMRTIMEOUT_MASK)
		xhdmiphy_tx_timertimeout_handler(inst);

	if (event & XHDMIPHY_INTR_RXTMRTIMEOUT_MASK)
		xhdmiphy_rx_timertimeout_handler(inst);

	xhdmiphy_write(inst, XHDMIPHY_INTR_STS_REG, event_ack);
}
