// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/string.h>
#include "xhdmiphy.h"

u32 xhdmiphy_read(struct xhdmiphy_dev *inst, u32 addr)
{
	return ioread32(inst->phy_base + addr);
}

void xhdmiphy_write(struct xhdmiphy_dev *inst, u32 addr, u32 value)
{
	iowrite32(value, inst->phy_base + addr);
}

void xhdmiphy_set_clr(struct xhdmiphy_dev *inst, u32 addr, u32 reg_val,
		      u32 mask_val, u8 set_clr)
{
	if (set_clr)
		reg_val |= mask_val;
	else
		reg_val &= ~mask_val;

	xhdmiphy_write(inst, addr, reg_val);
}

/**
 * xhdmiphy_cfg_set_cdr - This function is a transceiver adaptor to set the
 * clock and data recovery (CDR) values for a given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel id to operate on
 *
 * @return:	- 0 if the configuration was successful
 *		- 1 otherwise
 */
static bool xhdmiphy_cfg_set_cdr(struct xhdmiphy_dev *inst, enum chid chid)
{
	return inst->gt_adp->cfg_set_cdr(inst, chid);
}

/**
 * xhdmiphy_check_pll_oprange - This function is a transceiver adaptor to check
 * if a given PLL output frequency is within the operating range of the PLL for
 * the GT type.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @pllclk_out_freq:	pllclk_out_freq is the frequency to check
 *
 * @return:	- 0 if the frequency resides within the PLL's range
 *		- 1 otherwise
 */
static bool xhdmiphy_check_pll_oprange(struct xhdmiphy_dev *inst, enum chid chid,
				       u64 pllclk_out_freq)
{
	return inst->gt_adp->check_pll_oprange(inst, chid, pllclk_out_freq);
}

u32 xhdmiphy_outdiv_ch_reconf(struct xhdmiphy_dev *inst, enum chid chid,
			      enum dir dir)
{
	return inst->gt_adp->outdiv_ch_reconf(inst, chid, dir);
}

u32 xhdmiphy_clk_ch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	return inst->gt_adp->clk_ch_reconf(inst, chid);
}

u32 xhdmiphy_clk_cmn_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	return inst->gt_adp->clk_cmn_reconf(inst, chid);
}

u32 xhdmiphy_rxch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	return inst->gt_adp->rxch_reconf(inst, chid);
}

u32 xhdmiphy_txch_reconf(struct xhdmiphy_dev *inst, enum chid chid)
{
	return inst->gt_adp->txch_reconf(inst, chid);
}

/**
 * xhdmiphy_is_hdmi - This function checks if Instance is HDMI 2.0 or HDMI 2.1
 *
 * @inst:	inst is a pointer to the HDMIPHY instance
 * @dir:	dir is an indicator for RX or TX
 *
 * @return:	true if HDMI 2.0 or 2.1 else false
 */
bool xhdmiphy_is_hdmi(struct xhdmiphy_dev *inst, enum dir dir)
{
	if (dir == XHDMIPHY_DIR_TX) {
		if (inst->conf.tx_protocol == XHDMIPHY_PROT_HDMI ||
		    inst->conf.tx_protocol == XHDMIPHY_PROT_HDMI21)
			return true;
	} else {
		if (inst->conf.rx_protocol == XHDMIPHY_PROT_HDMI ||
		    inst->conf.rx_protocol == XHDMIPHY_PROT_HDMI21)
			return true;
	}

	return false;
}

bool xhdmiphy_is_using_qpll(struct xhdmiphy_dev *inst, enum chid chid,
			    enum dir dir)
{
	enum pll_type pll_type;

	pll_type = xhdmiphy_get_pll_type(inst, dir, chid);

	if (pll_type == XHDMIPHY_PLL_QPLL || pll_type == XHDMIPHY_PLL_QPLL0 ||
	    pll_type == XHDMIPHY_PLL_QPLL1)
		return 1;

	return 0;
}

bool xhdmiphy_is_cmn(enum chid chid)
{
	return (chid == XHDMIPHY_CHID_CMNA ||
		(chid >= XHDMIPHY_CHID_CMN0 &&
		chid <= XHDMIPHY_CHID_CMN1));
}

bool xhdmiphy_is_ch(enum chid chid)
{
	return (chid == XHDMIPHY_CHID_CHA ||
		(chid >= XHDMIPHY_CHID_CH1 &&
		chid <= XHDMIPHY_CHID_CH4));
}

/**
 * xhdmiphy_ch2ids - This function will set the channel id's to correspond with
 * the supplied channel ID based on the protocol. HDMI uses 3 channels; This ID
 * translation is done to allow other functions to operate iteratively over
 * multiple channels.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID used to determine the indices
 * @id0:	Id0 is a pointer to the start channel ID to set
 * @id1:	Id1 is a pointer to the end channel ID to set
 */
void xhdmiphy_ch2ids(struct xhdmiphy_dev *inst, enum chid chid, u8 *id0,
		     u8 *id1)
{
	u8 channels;

	if (chid == XHDMIPHY_CHID_CHA) {
		*id0 = XHDMIPHY_CHID_CH1;
		if ((xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX)) ||
		    (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX))) {
			if (inst->conf.tx_protocol == XHDMIPHY_PROT_HDMI21 ||
			    inst->conf.rx_protocol == XHDMIPHY_PROT_HDMI21)
				*id1 = XHDMIPHY_CHID_CH4;
			else if ((inst->conf.tx_protocol == XHDMIPHY_PROT_HDMI) &&
				 inst->conf.gt_as_tx_tmdsclk)
				*id1 = XHDMIPHY_CHID_CH4;
			else
				*id1 = XHDMIPHY_CHID_CH3;

		} else {
			channels = ((inst->conf.tx_channels >=
				inst->conf.rx_channels) ?
				inst->conf.tx_channels :
				inst->conf.rx_channels);

			if (channels == 1)
				*id1 = XHDMIPHY_CHID_CH1;
			else if (channels == 2)
				*id1 = XHDMIPHY_CHID_CH2;
			else if (channels == 3)
				*id1 = XHDMIPHY_CHID_CH3;
			else
				*id1 = XHDMIPHY_CHID_CH4;
		}
	} else if (chid == XHDMIPHY_CHID_CMNA) {
		*id0 = XHDMIPHY_CHID_CMN0;
		if (inst->conf.gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
		    inst->conf.gt_type == XHDMIPHY_GTTYPE_GTYE4)
			*id1 = XHDMIPHY_CHID_CMN1;
		else
			*id1 = XHDMIPHY_CHID_CMN0;
	} else {
		*id0 = *id1 = chid;
	}
}

/**
 * xhdmiphy_pll_refclk_sel - configure the PLL reference clock selection for the
 * specified channel(s). This is applied to both direction to the software
 * configuration only.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @refclk_sel:	refclk_sel is the reference clock selection to configure
 */
void xhdmiphy_pll_refclk_sel(struct xhdmiphy_dev *inst, enum chid chid,
			     enum refclk_sel refclk_sel)
{
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].pll_refclk = refclk_sel;
}

/**
 * xhdmiphy_sysclk_data_sel - configure the SYSCLKDATA reference clock
 * selection for the direction. Same configuration applies to all channels in
 * the quad. This is applied to the software configuration only.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @sys_clk_data_sel: sys_clk_data_sel is reference clock selection to configure
 */
void xhdmiphy_sysclk_data_sel(struct xhdmiphy_dev *inst, enum dir dir,
			      enum sysclk_data_sel sys_clk_data_sel)
{
	struct channel *ch_ptr;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

	/* select in software - same for all channels */
	for (id = id0; id <= id1; id++) {
		ch_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(id)];
		ch_ptr->data_refclk[dir] = sys_clk_data_sel;
	}
}

/**
 * xhdmiphy_sysclk_out_sel - configure the SYSCLKOUT reference clock selection
 * for the direction. Same configuration applies to all channels in the quad.
 * This is applied to the software configuration only.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @sys_clkout_sel:	sys_clkout_sel is reference clock selection to configure
 */
void xhdmiphy_sysclk_out_sel(struct xhdmiphy_dev *inst, enum dir dir,
			     enum sysclk_outsel sys_clkout_sel)
{
	struct channel *ch_ptr;
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

	/* select in software - same for all channels */
	for (id = id0; id <= id1; id++) {
		ch_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(id)];
		ch_ptr->out_refclk[dir] = sys_clkout_sel;
	}
}

void xhdmiphy_cfg_init(struct xhdmiphy_dev *inst)
{
	u8 sel;

	if (inst->conf.gt_type == XHDMIPHY_GTHE4)
		inst->gt_adp = &gthe4_conf;
	else if (inst->conf.gt_type == XHDMIPHY_GTYE5)
		inst->gt_adp = &gtye5_conf;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		const enum sysclk_data_sel sysclk[7][2] = {
			{0, XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK},
			{1, XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK},
			{2, XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK},
			{3, XHDMIPHY_SYSCLKSELDATA_QPLL_OUTCLK},
			{4, XHDMIPHY_SYSCLKSELDATA_PLL0_OUTCLK},
			{5, XHDMIPHY_SYSCLKSELDATA_PLL1_OUTCLK},
			{6, XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK},
		};

		for (sel = 0; sel < 7; sel++) {
			if (inst->conf.tx_pllclk_sel == sysclk[sel][0])
				inst->conf.tx_pllclk_sel = sysclk[sel][1];
			if (inst->conf.rx_pllclk_sel == sysclk[sel][0])
				inst->conf.rx_pllclk_sel = sysclk[sel][1];
		}

		inst->conf.tx_refclk_sel = inst->conf.tx_refclk_sel +
					   XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;
		inst->conf.rx_refclk_sel = inst->conf.rx_refclk_sel +
					   XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;
		inst->conf.tx_frl_refclk_sel = inst->conf.tx_frl_refclk_sel +
					       XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;
		inst->conf.rx_frl_refclk_sel = inst->conf.rx_frl_refclk_sel +
					       XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;
		inst->conf.dru_refclk_sel = inst->conf.dru_refclk_sel +
					    XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;
	}
}

static unsigned int xhdmiphy_pll2sysclk_data(enum pll_type pll_sel)
{
	if (pll_sel == XHDMIPHY_PLL_CPLL)
		return XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK;
	else if (pll_sel == XHDMIPHY_PLL_QPLL)
		return XHDMIPHY_SYSCLKSELDATA_QPLL_OUTCLK;
	else if (pll_sel == XHDMIPHY_PLL_QPLL0)
		return XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK;

	return XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK;
}

static unsigned short xhdmiphy_pll2sysclk_out(enum pll_type pll_sel)
{
	if (pll_sel == XHDMIPHY_PLL_CPLL)
		return XHDMIPHY_SYSCLKSELOUT_CPLL_REFCLK;
	else if (pll_sel == XHDMIPHY_PLL_QPLL)
		return XHDMIPHY_SYSCLKSELOUT_QPLL_REFCLK;
	else if (pll_sel == XHDMIPHY_PLL_QPLL0)
		return XHDMIPHY_SYSCLKSELOUT_QPLL0_REFCLK;

	return XHDMIPHY_SYSCLKSELOUT_QPLL1_REFCLK;
}

void xhdmiphy_write_refclksel(struct xhdmiphy_dev *inst)
{
	struct channel *ch_ptr;
	enum gt_type gt_type = inst->conf.gt_type;
	u32 reg_val = 0;

	/* point to the first channel since settings apply to all channels */
	ch_ptr = &inst->quad.ch1;

	/* pll_refclk */
	reg_val &= ~XHDMIPHY_REFCLKSEL_QPLL0_MASK;
	reg_val = inst->quad.cmn0.pll_refclk;

	reg_val &= ~XHDMIPHY_REFCLKSEL_CPLL_MASK;
	reg_val |= (ch_ptr->cpll_refclk << XHDMIPHY_REFCLKSEL_CPLL_SHIFT);
	if (gt_type == XHDMIPHY_GTTYPE_GTHE4 ||
	    gt_type == XHDMIPHY_GTTYPE_GTYE4) {
		reg_val &= ~XHDMIPHY_REFCLKSEL_QPLL1_MASK;
		reg_val |= (inst->quad.cmn1.pll_refclk <<
				XHDMIPHY_REFCLKSEL_QPLL1_SHIFT);
	}

	/* sys_clk_data_sel. PLLCLKSEL */
	reg_val &= ~XHDMIPHY_REFCLKSEL_SYSCLKSEL_MASK;
	/* TXSYSCLKSEL[0].TXPLLCLKSEL */
	reg_val |= (ch_ptr->tx_data_refclk <<
		    XHDMIPHY_TXSYSCLKSEL_DATA_SHIFT(gt_type)) &
		   XHDMIPHY_TXSYSCLKSEL_DATA_MASK(gt_type);

	/* RXSYSCLKSEL[0].RXPLLCLKSEL */
	reg_val |= (ch_ptr->rx_data_refclk <<
		    XHDMIPHY_RXSYSCLKSEL_DATA_SHIFT(gt_type)) &
		   XHDMIPHY_RXSYSCLKSEL_DATA_MASK(gt_type);

	/* sys_clkout_sel */
	reg_val |= (ch_ptr->tx_outrefclk <<
		    XHDMIPHY_TXSYSCLKSEL_OUT_SHIFT(gt_type)) &
		   XHDMIPHY_TXSYSCLKSEL_OUT_MASK(gt_type);

	reg_val |= (ch_ptr->rx_outrefclk <<
		    XHDMIPHY_RXSYSCLKSEL_OUT_SHIFT(gt_type)) &
		   XHDMIPHY_RXSYSCLKSEL_OUT_MASK(gt_type);

	xhdmiphy_write(inst, XHDMIPHY_REFCLKSEL_REG, reg_val);
}

/**
 * xhdmiphy_pll_init - This function will initialize the PLL selection for a
 * given channel.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @qpll_refclk_sel:	qpll_refclk_sel is the QPLL reference clock selection
 *			for the quad
 * @cpll_refclk_sel:	cpll_refclk_sel is the CPLL reference clock selection
 *			for the quad
 * @txpll_sel:		txpll_sel is the reference clock selection for the
 *			quad's TX PLL dividers
 * @rxpll_sel:		rxpll_sel is the reference clock selection for the
 *			quad's RX PLL dividers
 */
void xhdmiphy_pll_init(struct xhdmiphy_dev *inst, enum chid chid,
		       enum refclk_sel qpll_refclk_sel,
		       enum refclk_sel cpll_refclk_sel,
		       enum pll_type txpll_sel, enum pll_type rxpll_sel)
{
	xhdmiphy_pll_refclk_sel(inst, XHDMIPHY_CHID_CMNA, qpll_refclk_sel);
	xhdmiphy_pll_refclk_sel(inst, XHDMIPHY_CHID_CHA, cpll_refclk_sel);
	xhdmiphy_sysclk_data_sel(inst, XHDMIPHY_DIR_TX,
				 xhdmiphy_pll2sysclk_data(txpll_sel));
	xhdmiphy_sysclk_data_sel(inst, XHDMIPHY_DIR_RX,
				 xhdmiphy_pll2sysclk_data(rxpll_sel));
	xhdmiphy_sysclk_out_sel(inst, XHDMIPHY_DIR_TX,
				xhdmiphy_pll2sysclk_out(txpll_sel));
	xhdmiphy_sysclk_out_sel(inst, XHDMIPHY_DIR_RX,
				xhdmiphy_pll2sysclk_out(rxpll_sel));

	xhdmiphy_write_refclksel(inst);
}

/**
 * xhdmiphy_cfg_linerate - confure the channel's line rate. This is a software
 * only configuration and this value is used in the PLL calculator.
 *
 * @inst:		inst is a pointer to the xhdmiphy core instance
 * @chid:		chid is the channel ID to operate on
 * @linkrate_freq:	LineRate is the line rate to configure software
 */
void xhdmiphy_cfg_linerate(struct xhdmiphy_dev *inst, enum chid chid,
			   u64 linkrate_freq)
{
	u8 id, id0, id1;

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++)
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].linerate = linkrate_freq;
}

/**
 * xhdmiphy_get_sysclk_datasel - obtain the current [RT]XSYSCLKSEL[0]
 * configuration.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance.
 * @dir:	dir is an indicator for TX or RX
 * @chid:	chId is the channel ID which to operate on
 *
 * @return:	The current [RT]XSYSCLKSEL[0] selection
 */
static unsigned int xhdmiphy_get_sysclk_datasel(struct xhdmiphy_dev *inst,
						enum dir dir, enum chid chid)
{
	u32 sel, reg_val, gt_type;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_REFCLKSEL_REG);
	gt_type = inst->conf.gt_type;

	/* synchronize software configuration to hardware */
	if (dir == XHDMIPHY_DIR_TX) {
		sel = reg_val & XHDMIPHY_TXSYSCLKSEL_DATA_MASK(gt_type);
		sel >>= XHDMIPHY_TXSYSCLKSEL_DATA_SHIFT(gt_type);
	} else {
		sel = reg_val & XHDMIPHY_RXSYSCLKSEL_DATA_MASK(gt_type);
		sel >>= XHDMIPHY_RXSYSCLKSEL_DATA_SHIFT(gt_type);
	}

	return sel;
}

/**
 * xhdmiphy_get_sysclk_outsel - obtain the current [RT]XSYSCLKSEL[1]
 * configuration.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @chid:	chid is the channel ID which to operate on
 *
 * @return:	The current [RT]XSYSCLKSEL[1] selection
 */
static u32 xhdmiphy_get_sysclk_outsel(struct xhdmiphy_dev *inst,
				      enum dir dir, enum chid chid)
{
	u32 sel, reg_val, gt_type;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_REFCLKSEL_REG);
	gt_type = inst->conf.gt_type;

	/* synchronize software configuration to hardware */
	if (dir == XHDMIPHY_DIR_TX) {
		sel = reg_val & XHDMIPHY_TXSYSCLKSEL_OUT_MASK(gt_type);
		sel >>= XHDMIPHY_TXSYSCLKSEL_OUT_SHIFT(gt_type);
	} else {
		sel = reg_val & XHDMIPHY_RXSYSCLKSEL_OUT_MASK(gt_type);
		sel >>= XHDMIPHY_RXSYSCLKSEL_OUT_SHIFT(gt_type);
	}

	return sel;
}

/**
 * xhdmiphy_get_pll_type - obtain the channel's PLL reference clock selection.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @chid:	chid is the channel ID which to operate on
 *
 * @return:	The PLL type being used by the channel
 */
u32 xhdmiphy_get_pll_type(struct xhdmiphy_dev *inst, enum dir dir,
			  enum chid chid)
{
	enum pll_type pll_type;
	enum sysclk_data_sel sysclk_data_sel;
	enum sysclk_outsel sysclk_out_sel;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		sysclk_data_sel = xhdmiphy_get_sysclk_datasel(inst, dir, chid);
		sysclk_out_sel = xhdmiphy_get_sysclk_outsel(inst, dir, chid);

		if (sysclk_data_sel == XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK &&
		    sysclk_out_sel == XHDMIPHY_SYSCLKSELOUT_CPLL_REFCLK)
			pll_type = XHDMIPHY_PLL_CPLL;
		else if ((sysclk_data_sel == XHDMIPHY_SYSCLKSELDATA_QPLL_OUTCLK) &&
			 (sysclk_out_sel == XHDMIPHY_SYSCLKSELOUT_QPLL_REFCLK))
			pll_type = XHDMIPHY_PLL_QPLL;
		else if ((sysclk_data_sel == XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK) &&
			 (sysclk_out_sel == XHDMIPHY_SYSCLKSELOUT_QPLL0_REFCLK))
			pll_type = XHDMIPHY_PLL_QPLL0;
		else if ((sysclk_data_sel == XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK) &&
			 (sysclk_out_sel == XHDMIPHY_SYSCLKSELOUT_QPLL1_REFCLK))
			pll_type = XHDMIPHY_PLL_QPLL1;
		else
			pll_type = XHDMIPHY_PLL_UNKNOWN;

	} else {
		if (dir == XHDMIPHY_DIR_TX)
			pll_type = inst->conf.tx_pllclk_sel - 2;
		else if (dir == XHDMIPHY_DIR_RX)
			pll_type = inst->conf.rx_pllclk_sel - 2;
		else
			pll_type = XHDMIPHY_PLL_UNKNOWN;
	}

	return pll_type;
}

u64 xhdmiphy_get_linerate(struct xhdmiphy_dev *inst, enum chid chid)
{
	enum chid ch_id = chid;

	if (chid == XHDMIPHY_CHID_CHA)
		ch_id = XHDMIPHY_CHID_CH1;
	else if (chid == XHDMIPHY_CHID_CMNA)
		ch_id = XHDMIPHY_CHID_CMN0;

	return inst->quad.plls[ch_id - XHDMIPHY_CHID_CH1].linerate;
}

/**
 * xhdmiphy_set_tx_vs - This function will set the TX voltage swing value for
 * a given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @vs:		vs is the voltage swing value to write
 */
void xhdmiphy_set_tx_vs(struct xhdmiphy_dev *inst, enum chid chid, u8 vs)
{
	u32 reg_val, mask_val, reg_off;

	if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
		reg_off = XHDMIPHY_TX_DRIVER_CH12_REG;
	else
		reg_off = XHDMIPHY_TX_DRIVER_CH34_REG;

	reg_val = xhdmiphy_read(inst, reg_off);
	mask_val = XHDMIPHY_TX_DRIVER_TXDIFFCTRL_MASK(chid);

	reg_val &= ~mask_val;
	reg_val |= ((vs & XHDMIPHY_TX_TXDIFFCTRL_MASK) <<
		    XHDMIPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(chid));
	xhdmiphy_write(inst, reg_off, reg_val);

	reg_val = xhdmiphy_read(inst, XHDMIPHY_TX_DRIVER_EXT_REG);
	mask_val = XHDMIPHY_TX_DRIVER_EXT_TXDIFFCTRL_MASK(chid);

	reg_val &= ~mask_val;
	reg_val |= ((vs & XHDMIPHY_TX_EXT_TXDIFFCTRL_MASK) <<
		    XHDMIPHY_TX_DRIVER_EXT_TXDIFFCTRL_SHIFT(chid));

	xhdmiphy_write(inst, XHDMIPHY_TX_DRIVER_EXT_REG, reg_val);
}

/**
 * xhdmiphy_set_tx_pe - This function will set the TX pre-emphasis value for
 * a given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @pe:		pe is the pre-emphasis value to write
 */
void xhdmiphy_set_tx_pe(struct xhdmiphy_dev *inst, enum chid chid, u8 pe)
{
	u32 reg_val, mask_val, reg_off;

	if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
		reg_off = XHDMIPHY_TX_DRIVER_CH12_REG;
	else
		reg_off = XHDMIPHY_TX_DRIVER_CH34_REG;

	reg_val = xhdmiphy_read(inst, reg_off);

	mask_val = XHDMIPHY_TX_DRIVER_TXPRECURSOR_MASK(chid);
	reg_val &= ~mask_val;
	reg_val |= (pe << XHDMIPHY_TX_DRIVER_TXPRECURSOR_SHIFT(chid));
	xhdmiphy_write(inst, reg_off, reg_val);
}

/**
 * xhdmiphy_set_tx_pc - This function will set the TX post-curosr value for a
 * given channel.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @pc:		pc is the post-curosr value to write
 */
void xhdmiphy_set_tx_pc(struct xhdmiphy_dev *inst, enum chid chid, u8 pc)
{
	u32 reg_val, mask_val, reg_off;

	if (chid == XHDMIPHY_CHID_CH1 || chid == XHDMIPHY_CHID_CH2)
		reg_off = XHDMIPHY_TX_DRIVER_CH12_REG;
	else
		reg_off = XHDMIPHY_TX_DRIVER_CH34_REG;

	reg_val = xhdmiphy_read(inst, reg_off);

	mask_val = XHDMIPHY_TX_DRIVER_TXPOSTCURSOR_MASK(chid);
	reg_val &= ~mask_val;
	reg_val |= (pc << XHDMIPHY_TX_DRIVER_TXPOSTCURSOR_SHIFT(chid));
	xhdmiphy_write(inst, reg_off, reg_val);
}

void xhdmiphy_set_rxlpm(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir,
			u8 enable)
{
	u32 reg_val, mask_val;

	reg_val = xhdmiphy_read(inst, XHDMIPHY_RX_EQ_CDR_REG);

	if (chid == XHDMIPHY_CHID_CHA)
		mask_val = XHDMIPHY_RX_CONTROL_RXLPMEN_ALL_MASK;
	else
		mask_val = XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(chid);

	xhdmiphy_set_clr(inst, XHDMIPHY_RX_EQ_CDR_REG, reg_val, mask_val,
			 enable);
}

static unsigned int xhdmiphy_drp_access(struct xhdmiphy_dev *inst,
					enum chid chid, enum dir dir, u16 addr,
					u16 *val)
{
	u32 reg_off_ctrl, reg_off_sts, reg_val, err;

	/* determine which DRP registers to use based on channel */
	if (xhdmiphy_is_cmn(chid)) {
		reg_off_ctrl = XHDMIPHY_DRP_CONTROL_COMMON_REG;
		reg_off_sts = XHDMIPHY_DRP_STATUS_COMMON_REG;
	} else if (XHDMIPHY_ISTXMMCM(chid)) {
		reg_off_ctrl = XHDMIPHY_DRP_CONTROL_TXMMCM_REG;
		reg_off_sts = XHDMIPHY_DRP_STATUS_TXMMCM_REG;
	} else if (XHDMIPHY_ISRXMMCM(chid)) {
		reg_off_ctrl = XHDMIPHY_DRP_CONTROL_RXMMCM_REG;
		reg_off_sts = XHDMIPHY_DRP_STATUS_RXMMCM_REG;
	} else {
		reg_off_ctrl = XHDMIPHY_DRP_CONTROL_CH1_REG +
			(4 * XHDMIPHY_CH2IDX(chid));
		reg_off_sts = XHDMIPHY_DRP_STATUS_CH1_REG +
			(4 * (XHDMIPHY_CH2IDX(chid)));
	}

	err = readl_poll_timeout(inst->phy_base + reg_off_sts, reg_val,
				 !(reg_val & XHDMIPHY_DRP_STATUS_DRPBUSY_MASK),
				 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(inst->dev, "drp busy timeout\n");
		return err;
	}

	/* write the command to the channel's DRP */
	reg_val = (addr & XHDMIPHY_DRP_CONTROL_DRPADDR_MASK);
	reg_val |= XHDMIPHY_DRP_CONTROL_DRPEN_MASK;
	if (dir == XHDMIPHY_DIR_TX) {
		reg_val |= XHDMIPHY_DRP_CONTROL_DRPWE_MASK;
		reg_val |= ((*val << XHDMIPHY_DRP_CONTROL_DRPDI_SHIFT) &
			    XHDMIPHY_DRP_CONTROL_DRPDI_MASK);
	}
	xhdmiphy_write(inst, reg_off_ctrl, reg_val);

	err = readl_poll_timeout(inst->phy_base + reg_off_sts, reg_val,
				 (reg_val & XHDMIPHY_DRP_STATUS_DRPRDY_MASK),
				 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(inst->dev, "drp ready timeout\n");
		return err;
	}

	if (dir == XHDMIPHY_DIR_RX) {
		reg_val &= XHDMIPHY_DRP_STATUS_DRPO_MASK;
		*val = reg_val;
	}

	return 0;
}

/**
 * xhdmiphy_drpwr - This function will initiate a write drp transaction. It is
 * a wrapper around xhdmiphy_drp_access.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel id on which to direct the DRP access
 * @addr:	Addr is the DRP address to issue the DRP access to
 * @val:	val is the value to write to the DRP address
 *
 * @return:	- 0 if the DRP access was successful
 *		- -ETIMEDOUT otherwise, if the busy bit did not go low, or if
 *			the ready bit did not go high
 */
u32 xhdmiphy_drpwr(struct xhdmiphy_dev *inst, enum chid chid, u16 addr, u16 val)
{
	return xhdmiphy_drp_access(inst, chid, XHDMIPHY_DIR_TX, addr, &val);
}

/**
 * xhdmiphy_drprd - This function will initiate a read DRP transaction. It is
 * a wrapper around xhdmiphy_drp_access.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID on which to direct the DRP access
 * @addr:	addr is the DRP address to issue the DRP access to
 * @ret_val:	ret_val is the DRP read_value returned implicitly
 *
 * @return:	- 0 if the DRP access was successful
 *		- -ETIMEDOUT otherwise, if the busy bit did not go low, or if
 *		the ready bit did not go high
 */
u32 xhdmiphy_drprd(struct xhdmiphy_dev *inst, enum chid chid, u16 addr,
		   u16 *ret_val)
{
	return xhdmiphy_drp_access(inst, chid, XHDMIPHY_DIR_RX, addr, ret_val);
}

void xhdmiphy_ibufds_en(struct xhdmiphy_dev *inst, enum dir dir, u8 enable)
{
	enum refclk_sel *type_ptr, *dru_typ_ptr, dru_type_dummy;
	u32 reg_val, reg_addr, mask_val = 0;

	dru_type_dummy = XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK;
	dru_typ_ptr = &dru_type_dummy;

	if (inst->conf.gt_type != XHDMIPHY_GTYE5) {
		if (dir == XHDMIPHY_DIR_TX) {
			type_ptr = &inst->conf.tx_refclk_sel;
		} else {
			type_ptr = &inst->conf.rx_refclk_sel;
			if (inst->conf.dru_present)
				dru_typ_ptr = &inst->conf.dru_refclk_sel;
		}

		if ((*type_ptr == XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0) ||
		    (inst->conf.dru_present &&
		     (*dru_typ_ptr == XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0))) {
			mask_val = XHDMIPHY_IBUFDS_GTXX_CTRL_GTREFCLK0_CEB_MASK;
		} else if ((*type_ptr == XHDMIPHY_PLL_REFCLKSEL_GTREFCLK1) ||
			   (inst->conf.dru_present &&
			    (*dru_typ_ptr == XHDMIPHY_PLL_REFCLKSEL_GTREFCLK1))) {
			mask_val = XHDMIPHY_IBUFDS_GTXX_CTRL_GTREFCLK1_CEB_MASK;
		}
	}

	if (dir == XHDMIPHY_DIR_TX)
		reg_addr = XHDMIPHY_MISC_TXUSRCLK_REG;
	else
		reg_addr = XHDMIPHY_MISC_RXUSRCLK_REG;

	mask_val = XHDMIPHY_MISC_XXUSRCLK_REFCLK_CEB_MASK;
	reg_val = xhdmiphy_read(inst, reg_addr);

	if (inst->conf.gt_type != XHDMIPHY_GTYE5)
		xhdmiphy_set_clr(inst, reg_addr, reg_val, mask_val, !enable);
	else
		xhdmiphy_set_clr(inst, reg_addr, reg_val, mask_val, enable);
}

/**
 * xhdmiphy_clkout1_obuftds_en - This function enables the tx or rx clkout1
 * obuftds peripheral.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @enable:	enable specifies true/false value to either enable or disable
 *		the obuftds, respectively
 */
void xhdmiphy_clkout1_obuftds_en(struct xhdmiphy_dev *inst, enum dir dir,
				 u8 enable)
{
	u32 reg_val, reg_off, mask;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_MISC_TXUSRCLK_REG;
	else
		reg_off = XHDMIPHY_MISC_RXUSRCLK_REG;

	/* read XXUSRCLK MISC register */
	reg_val = xhdmiphy_read(inst, reg_off);
	mask = XHDMIPHY_MISC_XXUSRCLK_CKOUT1_OEN_MASK;
	/* write new value to XXUSRCLK MISC register */
	xhdmiphy_set_clr(inst, reg_off, reg_val, mask, enable);
}

/**
 * xhdmiphy_get_quad_refclk - obtain the current reference clock frequency for
 * the quad based on the reference clock type.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @refclk_type:	refclk_type is the type to obtain the clock selection for
 *
 * @return:	The current reference clock frequency for the quad for the
 *		specified type selection
 */
u32 xhdmiphy_get_quad_refclk(struct xhdmiphy_dev *inst,
			     enum refclk_sel refclk_type)
{
	u32 freq;
	u8 index = refclk_type - XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0;

	freq = (refclk_type > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK) ? 0 :
		inst->quad.refclk[index];

	return freq;
}

/**
 * xhdmiphy_set_gpi - This function will set the GPI ports to the GT Wizard
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 * @set:	set Set=true; Clear=false
 */
void xhdmiphy_set_gpi(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir,
		      u8 set)
{
	u32 reg_val, mask_val = 0;
	u8 id, id0, id1;

	/* read GPI register */
	reg_val = xhdmiphy_read(inst, XHDMIPHY_GT_DBG_GPI_REG);

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		if (dir == XHDMIPHY_DIR_TX)
			mask_val |= XHDMIPHY_TX_GPI_MASK(id);
		else
			mask_val |= XHDMIPHY_RX_GPI_MASK(id);
	}

	xhdmiphy_set_clr(inst, XHDMIPHY_GT_DBG_GPI_REG, reg_val, mask_val, set);
}

/**
 * xhdmiphy_get_gpo - This function will get the GPO ports value from the GT
 * Wizard
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to operate on
 * @dir:	dir is an indicator for TX or RX
 *
 * @return:	- This will return the GPO port value.
 */
u8 xhdmiphy_get_gpo(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir)
{
	u32 reg_val;

	/* Read GPI register */
	reg_val = xhdmiphy_read(inst, XHDMIPHY_GT_DBG_GPO_REG);

	if (dir == XHDMIPHY_DIR_TX) {
		return ((reg_val &
			XHDMIPHY_TX_GPO_MASK_ALL(inst->conf.tx_channels)) >>
			XHDMIPHY_TX_GPO_SHIFT);
	}

	return ((reg_val &
		XHDMIPHY_RX_GPO_MASK_ALL(inst->conf.rx_channels)) >>
		XHDMIPHY_RX_GPO_SHIFT);
}

/**
 * xhdmiphy_set_bufgtdiv - This function obtains the divider value of the
 * BUFG_GT peripheral.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @div:	div 3-bit divider value
 */
void xhdmiphy_set_bufgtdiv(struct xhdmiphy_dev *inst, enum dir dir, u8 div)
{
	u32 reg_val, reg_off;

	if (div == 0)
		div = 1;
	else
		div = div - 1;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_BUFGGT_TXUSRCLK_REG;
	else
		reg_off = XHDMIPHY_BUFGGT_RXUSRCLK_REG;

	/* read BUFG_GT register */
	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val &= ~XHDMIPHY_BUFGGT_XXUSRCLK_DIV_MASK;

	/* shift divider value to correct position */
	div <<= XHDMIPHY_BUFGGT_XXUSRCLK_DIV_SHIFT;
	div &= XHDMIPHY_BUFGGT_XXUSRCLK_DIV_MASK;
	reg_val |= div;

	/* write new value to BUFG_GT ctrl register */
	xhdmiphy_write(inst, reg_off, reg_val);
}

/**
 * xhdmiphy_powerdown_gtpll - This function will power down the specified GT PLL.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to power down the PLL for
 * @hold:	hold is an indicator whether to "hold" the power down if set
 *		to 1. If set to 0: power down, then power back up
 */
void xhdmiphy_powerdown_gtpll(struct xhdmiphy_dev *inst, enum chid chid, u8 hold)
{
	u32 mask_val = 0, reg_val;
	u8 id, id0, id1;

	if (xhdmiphy_is_ch(chid))
		xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	else
		xhdmiphy_ch2ids(inst, XHDMIPHY_CHID_CHA, &id0, &id1);

	for (id = id0; id <= id1; id++) {
		if (chid == XHDMIPHY_CHID_CMN0) {
			mask_val |= XHDMIPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(id);
		} else if (chid == XHDMIPHY_CHID_CMN1) {
			mask_val |= XHDMIPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(id);
		} else if (chid == XHDMIPHY_CHID_CMNA) {
			mask_val |= XHDMIPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(id) |
				   XHDMIPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(id);
		} else {
			mask_val |= XHDMIPHY_POWERDOWN_CONTROL_CPLLPD_MASK(id);
		}
	}

	reg_val = xhdmiphy_read(inst, XHDMIPHY_POWERDOWN_CONTROL_REG);
	reg_val |= mask_val;
	xhdmiphy_write(inst, XHDMIPHY_POWERDOWN_CONTROL_REG, reg_val);

	if (!hold) {
		reg_val &= ~mask_val;
		xhdmiphy_write(inst, XHDMIPHY_POWERDOWN_CONTROL_REG, reg_val);
	}
}

/**
 * xhdmiphy_mmcm_reset - This function will reset the mixed-mode clock manager
 * (MMCM) core.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @dir:	dir is an indicator for TX or RX
 * @hold:	Hold is an indicator whether to "hold" the reset if set to 1
 *		If set to 0: reset, then enable
 */
void xhdmiphy_mmcm_reset(struct xhdmiphy_dev *inst, enum dir dir, u8 hold)
{
	u32 reg_off, reg_val;

	if (dir == XHDMIPHY_DIR_TX)
		reg_off = XHDMIPHY_MMCM_TXUSRCLK_CTRL_REG;
	else
		reg_off = XHDMIPHY_MMCM_RXUSRCLK_CTRL_REG;

	/* assert reset */
	reg_val = xhdmiphy_read(inst, reg_off);
	reg_val |= XHDMIPHY_MMCM_USRCLK_CTRL_RST_MASK;
	xhdmiphy_write(inst, reg_off, reg_val);

	if (!hold) {
		/* de-assert reset */
		reg_val &= ~XHDMIPHY_MMCM_USRCLK_CTRL_RST_MASK;
		xhdmiphy_write(inst, reg_off, reg_val);
	}
}

u64 xhdmiphy_get_pll_vco_freq(struct xhdmiphy_dev *inst, enum chid chid,
			      enum dir dir)
{
	struct channel *pll_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
	struct pll_param pll_prm;
	u64 pll_vco_rate, pll_refclk;
	u32 qrefclk;

	pll_prm = inst->quad.plls[XHDMIPHY_CH2IDX(chid)].pll_param;
	qrefclk = xhdmiphy_get_quad_refclk(inst, pll_ptr->pll_refclk);

	if (dir == XHDMIPHY_DIR_TX) {
		if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_TX))
			pll_refclk = inst->tx_refclk_hz;
		else
			pll_refclk = qrefclk;
	} else {
		if (xhdmiphy_is_hdmi(inst, XHDMIPHY_DIR_RX)) {
			if (inst->rx_dru_enabled)
				pll_refclk = xhdmiphy_get_dru_refclk(inst);
			else
				pll_refclk = inst->rx_refclk_hz;
		} else {
			pll_refclk = qrefclk;
		}
	}

	pll_vco_rate = (u64)(pll_refclk * pll_prm.n1fb_div * pll_prm.n2fb_div) /
			     pll_prm.m_refclk_div;

	return pll_vco_rate;
}

/**
 * xhdmiphy_pll_cal - This function will try to find the necessary PLL divisor
 * values to produce the configured line rate given the specified PLL input
 * frequency.
 *
 * @inst:	inst is a pointer to the xhdmiphy core instance
 * @chid:	chid is the channel ID to calculate the PLL values for
 * @dir:	dir is an indicator for TX or RX
 * @pll_clkin_freq:
 *		pll_clkin_freq is the PLL input frequency on which to base the
 *		calculations on. A value of 0 indicates to use the currently
 *		configured quad PLL reference clock. A non-zero value indicates
 *		to ignore what is currently configured in SW, and use a custom
 *		frequency instead.
 *
 * @return:	- 0 if valid PLL values were found to satisfy the constraints
 *		- 1 otherwise
 */
u32 xhdmiphy_pll_cal(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir,
		     u32 pll_clkin_freq)
{
	const struct gtpll_divs *gtpll_divs;
	struct channel *pll_ptr = &inst->quad.plls[XHDMIPHY_CH2IDX(chid)];
	u64 pllclk_out_freq, linerate_freq;
	u64 pll_clkin_freqin = pll_clkin_freq;
	u32 status;
	const u8 *m, *n1, *n2, *d;
	u8 id, id0, id1;

	if (!pll_clkin_freqin)
		pll_clkin_freqin =
			xhdmiphy_get_quad_refclk(inst, pll_ptr->pll_refclk);

	/* select PLL value table offsets */
	if (xhdmiphy_is_ch(chid))
		gtpll_divs = &inst->gt_adp->cpll_divs;
	else
		gtpll_divs = &inst->gt_adp->qpll_divs;

	for (n2 = gtpll_divs->n2; *n2 != 0; n2++) {
		for (n1 = gtpll_divs->n1; *n1 != 0; n1++) {
			for (m = gtpll_divs->m; *m != 0; m++) {
				pllclk_out_freq = (pll_clkin_freqin * *n1 * *n2) / *m;
				/* Test if the calculated PLL clock is in the VCO range */
				status = xhdmiphy_check_pll_oprange(inst, chid,
								    pllclk_out_freq);
				if (status != 0)
					continue;

				if ((xhdmiphy_is_ch(chid)))
					pllclk_out_freq *= 2;

				/* Apply TX/RX divisor */
				for (d = gtpll_divs->d; *d != 0; d++) {
					linerate_freq = pllclk_out_freq / *d;
					if (linerate_freq == pll_ptr->linerate)
						goto calc_done;
				}
			}
		}
	}
	/* Calculation failed, don't change divisor settings */
	return 1;

calc_done:
	/* Found the multiplier and divisor values for requested line rate */
	pll_ptr->pll_param.m_refclk_div = *m;
	pll_ptr->pll_param.nfb_div = *n1;
	pll_ptr->pll_param.n2fb_div = *n2;
	pll_ptr->pll_param.is_lowerband = 1;

	if (xhdmiphy_is_cmn(chid)) {
		/* Same divisor value for all channels if using a QPLL */
		chid = XHDMIPHY_CHID_CHA;
	}

	xhdmiphy_ch2ids(inst, chid, &id0, &id1);
	for (id = id0; id <= id1; id++) {
		inst->quad.plls[XHDMIPHY_CH2IDX(id)].outdiv[dir] = *d;
		if (dir == XHDMIPHY_DIR_RX)
			xhdmiphy_cfg_set_cdr(inst, (enum chid)id);
	}

	return 0;
}
