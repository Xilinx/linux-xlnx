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
