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
		if (status != 0)
			return true;
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

	xhdmiphy_set_gpi(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_TX, false);

	/* wait for GPO TX = 0 */
	while (xhdmiphy_get_gpo(inst, XHDMIPHY_CHID_CHA, XHDMIPHY_DIR_TX))
		;

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

static void xhdmiphy_rxgpo_risingedge_handler(struct xhdmiphy_dev *inst)
{
	u8 id, id0, id1;

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
