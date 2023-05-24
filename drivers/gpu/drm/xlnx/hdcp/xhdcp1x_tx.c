// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP1X Protocol Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 *
 * This driver provides standard HDCP1X protocol specific functionalites.
 * It consists of:
 * - A state machine which handles the states as specified in the HDCP
 *	specification.
 * This driver still have Xilinx specific functionalities as it is not upstreamed now,
 * it will be updated as more generic and standardized driver in the next upstream version.
 *
 * Reference :
 * https://www.digital-cp.com/sites/default/files/specifications/HDCP%20on%20DisplayPort%20Specification%20Rev1_1.pdf
 *
 */
#include <linux/dev_printk.h>
#include "xhdcp1x_tx.h"
#include "xlnx_hdcp1x_tx.h"

static enum hdcp1x_tx_state hdcp1x_run_unautheticated_state(struct xlnx_hdcp1x_config *hdcp1x)
{
	hdcp1x->state_helper = 0;
	return H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
}

static enum hdcp1x_tx_state
	    hdcp1x_tx_run_testfor_repeater_state_A5(struct xlnx_hdcp1x_config *hdcp1x)
{
	if (xlnx_hdcp1x_tx_test_for_repeater(hdcp1x))
		return H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
	else
		return A4_HDCP1X_TX_STATE_AUTHENTICATED;
}

static enum hdcp1x_tx_state hdcp1x_tx_run_authenticated_state_A4(struct xlnx_hdcp1x_config *hdcp1x)
{
	hdcp1x->state_helper = 0;
	xlnx_hdcp1x_tx_enable_encryption(hdcp1x);

	return A4_HDCP1X_TX_STATE_AUTHENTICATED;
}

static enum hdcp1x_tx_state hdcp1x_tx_run_validaterx_state_A3(struct xlnx_hdcp1x_config *hdcp1x)
{
	if (xlnx_hdcp1x_tx_validaterxstate(hdcp1x))
		return A5_HDCP1X_TX_STATE_TESTFORREPEATER;
	else
		return  H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
}

static enum hdcp1x_tx_state hdcp1x_tx_run_computations_state_A2(struct xlnx_hdcp1x_config *hdcp1x)
{
	if (xlnx_hdcp1x_computationsstate(hdcp1x))
		return A3_HDCP1X_TX_STATE_VALIDATE_RX;
	else
		return A2_HDCP1X_TX_STATE_COMPUTATIONS;
}

static enum hdcp1x_tx_state hdcp1x_tx_run_exchange_ksv_state_A1(struct xlnx_hdcp1x_config *hdcp1x)
{
	if (xlnx_hdcp1x_exchangeksvs(hdcp1x))
		return A2_HDCP1X_TX_STATE_COMPUTATIONS;
	else
		return H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
}

static enum hdcp1x_tx_state
	    hdcp1x_tx_run_determine_rx_capable_state_A0(struct xlnx_hdcp1x_config *hdcp1x)
{
	if (xlnx_hdcp1x_tx_check_rxcapable(hdcp1x))
		return A1_HDCP1X_TX_STATE_EXCHANGE_KSVS;
	else
		return H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
}

static enum hdcp1x_tx_state hdcp1x_tx_run_disablestate(struct xlnx_hdcp1x_config *hdcp1x)
{
	hdcp1x->pending_events = 0;
	hdcp1x->is_enabled = 0;
	return A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE;
}

/*
 * HDCP Transmitter State Diagram available in
 * HDCP1.1 specification. Section 2.3
 * https://www.digital-cp.com/sites/default/files/
 * specifications/HDCP%20on%20DisplayPort%20Specification%20Rev1_1.pdf
 */
int hdcp1x_tx_protocol_authenticate_sm(struct xlnx_hdcp1x_config *hdcp1x)
{
	int status = H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
	enum hdcp1x_tx_state hdcp1x_state = hdcp1x->curr_state;

	switch (hdcp1x_state) {
	case H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED:
		status = hdcp1x_tx_run_disablestate(hdcp1x);
		break;
	case A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE:
		status = hdcp1x_tx_run_determine_rx_capable_state_A0(hdcp1x);
		break;
	case A1_HDCP1X_TX_STATE_EXCHANGE_KSVS:
		status = hdcp1x_tx_run_exchange_ksv_state_A1(hdcp1x);
		break;
	case A2_HDCP1X_TX_STATE_COMPUTATIONS:
		status = hdcp1x_tx_run_computations_state_A2(hdcp1x);
		break;
	case A3_HDCP1X_TX_STATE_VALIDATE_RX:
		status = hdcp1x_tx_run_validaterx_state_A3(hdcp1x);
		break;
	case A4_HDCP1X_TX_STATE_AUTHENTICATED:
		status = hdcp1x_tx_run_authenticated_state_A4(hdcp1x);
		break;
	case A5_HDCP1X_TX_STATE_TESTFORREPEATER:
		status = hdcp1x_tx_run_testfor_repeater_state_A5(hdcp1x);
		break;
	case REPTR_HDCP1X_TX_STATE_UNAUTHENTICATED:
		status = hdcp1x_run_unautheticated_state(hdcp1x);
		break;
	default:
		status = hdcp1x_state;
		dev_dbg(hdcp1x->dev, "Invalid HDCP1x State");
		break;
	}
	return status;
}
