/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "hw.h"
#include "ar9003_mac.h"
#include "ar9003_2p2_initvals.h"
#include "ar9485_initvals.h"
#include "ar9340_initvals.h"
#include "ar9330_1p1_initvals.h"
#include "ar9330_1p2_initvals.h"
#include "ar955x_1p0_initvals.h"
#include "ar9580_1p0_initvals.h"
#include "ar9462_2p0_initvals.h"
#include "ar9462_2p1_initvals.h"
#include "ar9565_1p0_initvals.h"

/* General hardware code for the AR9003 hadware family */

/*
 * The AR9003 family uses a new INI format (pre, core, post
 * arrays per subsystem). This provides support for the
 * AR9003 2.2 chipsets.
 */
static void ar9003_hw_init_mode_regs(struct ath_hw *ah)
{
	if (AR_SREV_9330_11(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9331_1p1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9331_1p1_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9331_1p1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9331_1p1_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9331_1p1_radio_core);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9331_1p1_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9331_1p1_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p1);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9331_modes_lowest_ob_db_tx_gain_1p1);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9331_1p1_baseband_core_txfir_coeff_japan_2484);

		/* additional clock settings */
		if (ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p1_xtal_25M);
		else
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p1_xtal_40M);
	} else if (AR_SREV_9330_12(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9331_1p2_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9331_1p2_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9331_1p2_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9331_1p2_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9331_1p2_radio_core);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9331_1p2_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9331_1p2_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p2);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9331_modes_lowest_ob_db_tx_gain_1p2);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9331_1p2_baseband_core_txfir_coeff_japan_2484);

		/* additional clock settings */
		if (ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p2_xtal_25M);
		else
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p2_xtal_40M);
	} else if (AR_SREV_9340(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9340_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9340_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9340_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9340_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9340_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9340_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9340_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9340_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9340Common_wo_xlna_rx_gain_table_1p0);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9340Modes_high_ob_db_tx_gain_table_1p0);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9340Modes_fast_clock_1p0);

		if (!ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
				       ar9340_1p0_radio_core_40M);
	} else if (AR_SREV_9485_11_OR_LATER(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9485_1_1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9485_1_1_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_PRE], ar9485_1_1);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9485_1_1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9485_1_1_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9485_1_1_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9485_1_1_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9485_1_1_soc_preamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9485Common_wo_xlna_rx_gain_1_1);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9485_modes_lowest_ob_db_tx_gain_1_1);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9485_1_1_baseband_core_txfir_coeff_japan_2484);

		if (ah->config.no_pll_pwrsave) {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
				       ar9485_1_1_pcie_phy_clkreq_disable_L1);
			INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				       ar9485_1_1_pcie_phy_clkreq_disable_L1);
		} else {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
				       ar9485_1_1_pll_on_cdr_on_clkreq_disable_L1);
			INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				       ar9485_1_1_pll_on_cdr_on_clkreq_disable_L1);
		}
	} else if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       ar9462_2p1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       ar9462_2p1_mac_postamble);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       ar9462_2p1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       ar9462_2p1_baseband_postamble);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       ar9462_2p1_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       ar9462_2p1_radio_postamble);
		INIT_INI_ARRAY(&ah->ini_radio_post_sys2ant,
			       ar9462_2p1_radio_postamble_sys2ant);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       ar9462_2p1_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       ar9462_2p1_soc_postamble);
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_rx_gain);
		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       ar9462_2p1_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9462_2p1_baseband_core_txfir_coeff_japan_2484);
	} else if (AR_SREV_9462_20(ah)) {

		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE], ar9462_2p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9462_2p0_mac_postamble);

		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9462_2p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9462_2p0_baseband_postamble);

		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9462_2p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9462_2p0_radio_postamble);
		INIT_INI_ARRAY(&ah->ini_radio_post_sys2ant,
				ar9462_2p0_radio_postamble_sys2ant);

		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9462_2p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9462_2p0_soc_postamble);

		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_common_rx_gain_table_2p0);

		/* Awake -> Sleep Setting */
		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9462_pciephy_clkreq_disable_L1_2p0);
		/* Sleep -> Awake Setting */
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9462_pciephy_clkreq_disable_L1_2p0);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9462_modes_fast_clock_2p0);

		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9462_2p0_baseband_core_txfir_coeff_japan_2484);
	} else if (AR_SREV_9550(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar955x_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar955x_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar955x_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar955x_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar955x_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar955x_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar955x_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar955x_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar955x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			ar955x_1p0_common_wo_xlna_rx_gain_bounds);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar955x_1p0_modes_xpa_tx_gain_table);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar955x_1p0_modes_fast_clock);
	} else if (AR_SREV_9580(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9580_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9580_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9580_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9580_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9580_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9580_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9580_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9580_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9580_1p0_rx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9580_1p0_low_ob_db_tx_gain_table);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9580_1p0_modes_fast_clock);
	} else if (AR_SREV_9565(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       ar9565_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       ar9565_1p0_mac_postamble);

		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       ar9565_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       ar9565_1p0_baseband_postamble);

		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       ar9565_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       ar9565_1p0_radio_postamble);

		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       ar9565_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       ar9565_1p0_soc_postamble);

		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_Common_rx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_Modes_lowest_ob_db_tx_gain_table);

		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9565_1p0_pciephy_clkreq_disable_L1);
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9565_1p0_pciephy_clkreq_disable_L1);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9565_1p0_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9565_1p0_baseband_core_txfir_coeff_japan_2484);
	} else {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9300_2p2_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9300_2p2_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9300_2p2_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9300_2p2_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9300_2p2_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9300_2p2_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9300_2p2_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9300_2p2_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9300Common_rx_gain_table_2p2);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9300Modes_lowest_ob_db_tx_gain_table_2p2);

		/* Load PCIE SERDES settings from INI */

		/* Awake Setting */

		INIT_INI_ARRAY(&ah->iniPcieSerdes,
				ar9300PciePhy_pll_on_clkreq_disable_L1_2p2);

		/* Sleep Setting */

		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				ar9300PciePhy_pll_on_clkreq_disable_L1_2p2);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9300Modes_fast_clock_2p2);
	}
}

static void ar9003_tx_gain_table_mode0(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_lowest_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_lowest_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_lowest_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485_modes_lowest_ob_db_tx_gain_1_1);
	else if (AR_SREV_9550(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar955x_1p0_modes_xpa_tx_gain_table);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_lowest_ob_db_tx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p1_modes_low_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_modes_low_ob_db_tx_gain_table_2p0);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_low_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_lowest_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode1(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_high_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_high_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_high_ob_db_tx_gain_table);
	else if (AR_SREV_9550(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar955x_1p0_modes_no_xpa_tx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p1_modes_high_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_modes_high_ob_db_tx_gain_table_2p0);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_high_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_high_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode2(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_low_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_low_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_low_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_low_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_low_ob_db_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_low_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_low_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode3(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_power_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_power_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_high_power_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_high_power_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_high_power_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_high_power_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_high_power_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode4(struct ath_hw *ah)
{
	if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_mixed_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_mixed_ob_db_tx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		       ar9462_2p1_modes_mix_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		       ar9462_modes_mix_ob_db_tx_gain_table_2p0);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_mixed_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode5(struct ath_hw *ah)
{
	if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_green_ob_db_tx_gain_1_1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_ub124_tx_gain_table_1p0);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_type5_tx_gain_table);
	else if (AR_SREV_9300_22(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_type5_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode6(struct ath_hw *ah)
{
	if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_low_ob_db_and_spur_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_green_spur_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_type6_tx_gain_table);
}

typedef void (*ath_txgain_tab)(struct ath_hw *ah);

static void ar9003_tx_gain_table_apply(struct ath_hw *ah)
{
	static const ath_txgain_tab modes[] = {
		ar9003_tx_gain_table_mode0,
		ar9003_tx_gain_table_mode1,
		ar9003_tx_gain_table_mode2,
		ar9003_tx_gain_table_mode3,
		ar9003_tx_gain_table_mode4,
		ar9003_tx_gain_table_mode5,
		ar9003_tx_gain_table_mode6,
	};
	int idx = ar9003_hw_get_tx_gain_idx(ah);

	if (idx >= ARRAY_SIZE(modes))
		idx = 0;

	modes[idx](ah);
}

static void ar9003_rx_gain_table_mode0(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9340Common_rx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9485_common_rx_gain_1_1);
	else if (AR_SREV_9550(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar955x_1p0_common_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
				ar955x_1p0_common_rx_gain_bounds);
	} else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9580_1p0_rx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_2p1_common_rx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_common_rx_gain_table_2p0);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_Common_rx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9300Common_rx_gain_table_2p2);
}

static void ar9003_rx_gain_table_mode1(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9331_common_wo_xlna_rx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9331_common_wo_xlna_rx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9340Common_wo_xlna_rx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9485Common_wo_xlna_rx_gain_1_1);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9462_2p1_common_wo_xlna_rx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9462_common_wo_xlna_rx_gain_table_2p0);
	else if (AR_SREV_9550(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar955x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			ar955x_1p0_common_wo_xlna_rx_gain_bounds);
	} else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9580_1p0_wo_xlna_rx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_common_wo_xlna_rx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9300Common_wo_xlna_rx_gain_table_2p2);
}

static void ar9003_rx_gain_table_mode2(struct ath_hw *ah)
{
	if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_mixed_rx_gain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_core,
			       ar9462_2p1_baseband_core_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
			       ar9462_2p1_baseband_postamble_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p1_baseband_postamble_5g_xlna);
	} else if (AR_SREV_9462_20(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_common_mixed_rx_gain_table_2p0);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_core,
			       ar9462_2p0_baseband_core_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
			       ar9462_2p0_baseband_postamble_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p0_baseband_postamble_5g_xlna);
	}
}

static void ar9003_rx_gain_table_mode3(struct ath_hw *ah)
{
	if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_5g_xlna_only_rx_gain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p1_baseband_postamble_5g_xlna);
	} else if (AR_SREV_9462_20(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p0_5g_xlna_only_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p0_baseband_postamble_5g_xlna);
	}
}

static void ar9003_rx_gain_table_apply(struct ath_hw *ah)
{
	switch (ar9003_hw_get_rx_gain_idx(ah)) {
	case 0:
	default:
		ar9003_rx_gain_table_mode0(ah);
		break;
	case 1:
		ar9003_rx_gain_table_mode1(ah);
		break;
	case 2:
		ar9003_rx_gain_table_mode2(ah);
		break;
	case 3:
		ar9003_rx_gain_table_mode3(ah);
		break;
	}
}

/* set gain table pointers according to values read from the eeprom */
static void ar9003_hw_init_mode_gain_regs(struct ath_hw *ah)
{
	ar9003_tx_gain_table_apply(ah);
	ar9003_rx_gain_table_apply(ah);
}

/*
 * Helper for ASPM support.
 *
 * Disable PLL when in L0s as well as receiver clock when in L1.
 * This power saving option must be enabled through the SerDes.
 *
 * Programming the SerDes must go through the same 288 bit serial shift
 * register as the other analog registers.  Hence the 9 writes.
 */
static void ar9003_hw_configpcipowersave(struct ath_hw *ah,
					 bool power_off)
{
	/*
	 * Increase L1 Entry Latency. Some WB222 boards don't have
	 * this change in eeprom/OTP.
	 *
	 */
	if (AR_SREV_9462(ah)) {
		u32 val = ah->config.aspm_l1_fix;
		if ((val & 0xff000000) == 0x17000000) {
			val &= 0x00ffffff;
			val |= 0x27000000;
			REG_WRITE(ah, 0x570c, val);
		}
	}

	/* Nothing to do on restore for 11N */
	if (!power_off /* !restore */) {
		/* set bit 19 to allow forcing of pcie core into L1 state */
		REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
		REG_WRITE(ah, AR_WA, ah->WARegVal);
	}

	/*
	 * Configire PCIE after Ini init. SERDES values now come from ini file
	 * This enables PCIe low power mode.
	 */
	if (ah->config.pcieSerDesWrite) {
		unsigned int i;
		struct ar5416IniArray *array;

		array = power_off ? &ah->iniPcieSerdes :
				    &ah->iniPcieSerdesLowPower;

		for (i = 0; i < array->ia_rows; i++) {
			REG_WRITE(ah,
				  INI_RA(array, i, 0),
				  INI_RA(array, i, 1));
		}
	}
}

/* Sets up the AR9003 hardware familiy callbacks */
void ar9003_hw_attach_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	ar9003_hw_init_mode_regs(ah);
	priv_ops->init_mode_gain_regs = ar9003_hw_init_mode_gain_regs;

	ops->config_pci_powersave = ar9003_hw_configpcipowersave;

	ar9003_hw_attach_phy_ops(ah);
	ar9003_hw_attach_calib_ops(ah);
	ar9003_hw_attach_mac_ops(ah);
}
