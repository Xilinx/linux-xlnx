// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Protocol Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver provides standard HDCP2X protocol specific functionalites.
 * It consists of:
 * - A state machine which handles the states as specified in the HDCP
 *	specification.
 * This driver still have Xilinx specific functionalities as it is not upstreamed now,
 * it will be updated as more generic and standardized driver in the next upstream version.
 *
 * Reference :
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 */

#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xhdcp2x_tx.h"
#include "xlnx_hdcp2x_tx.h"

static int hdcp2x_tx_receive_message(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 msg_id)
{
	return xlnx_hdcp2x_tx_read_msg(xhdcp2x_tx, msg_id);
}

static enum hdcp2x_tx_state hdcp2x_tx_verify_mprime(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	int result;

	/*
	 * Wait for the receiver to respond within 100 msecs.
	 * If the receiver has timed out we go to state A9 for a retry.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.
	 */
	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  HDCP2X_TX_REPEATERAUTH_STREAM_READY_SIZE,
						  0);
	if (result < 0)
		return A9_HDCP2X_TX_STREAM_MANAGE;

	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A9_HDCP2X_TX_VERIFY_MPRIME;

	if (xhdcp2x_tx->xhdcp2x_info.content_strm_mng_chk_cntr)
		dev_dbg(xhdcp2x_tx->dev, "content stream manage message");

	result = hdcp2x_tx_receive_message(xhdcp2x_tx,
					   HDCP_2_2_REP_STREAM_READY);
	if (result < 0)
		return A9_HDCP2X_TX_STREAM_MANAGE;

	if (memcmp(tx_msg->msg_type.rpt_auth_stream_rdy.m_prime,
		   xhdcp2x_tx->xhdcp2x_info.M,
		   HDCP_2_2_MPRIME_LEN))
		return A9_HDCP2X_TX_STREAM_MANAGE;

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP2X_TX_WAIT_FOR_ENCRYPTION_TIMEOUT,
				   XHDCP2X_TX_TS_WAIT_FOR_CIPHER);

	xhdcp2x_tx->xhdcp2x_info.is_content_stream_type_set = 1;

	return A5_HDCP2X_TX_AUTHENTICATED;
}

static enum  hdcp2x_tx_state hdcp2x_tx_process_stream_manage(struct xlnx_hdcp2x_config
							     *xhdcp2x_tx)
{
	struct xhdcp2x_tx_internal_info *hdcp2x_tx_internal_info =
				&xhdcp2x_tx->xhdcp2x_info;
	int result;

	if (!xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired)
		return A9_HDCP2X_TX_STREAM_MANAGE;

	xlnx_hdcp2x_tx_read_rxstatus(xhdcp2x_tx);
	if ((hdcp2x_tx_internal_info->dp_rx_status &
			XHDCP2X_TX_RXSTATUS_REAUTH_REQ_MASK) ==
		XHDCP2X_TX_RXSTATUS_REAUTH_REQ_MASK) {
		xlnx_hdcp2x_handle_reauth_request(xhdcp2x_tx);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	if (!hdcp2x_tx_internal_info->is_content_stream_type_set) {
		xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP2X_TX_WAIT_FOR_STREAM_TYPE_TIMEOUT,
					   XHDCP2X_TX_TS_WAIT_FOR_STREAM_TYPE);
		return A9_HDCP2X_TX_STREAM_MANAGE;
	}

	if (!hdcp2x_tx_internal_info->content_strm_mng_chk_cntr)
		dev_dbg(xhdcp2x_tx->dev, "verify receiver-id");

	if (hdcp2x_tx_internal_info->content_strm_mng_chk_cntr >=
			XHDCP2X_TX_MAX_ALLOWED_STREAM_MANAGE_CHECKS) {
		dev_err(xhdcp2x_tx->dev,
			"content stream manage check counter fail");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	if (hdcp2x_tx_internal_info->seq_num_m <
			hdcp2x_tx_internal_info->prev_seq_num_m) {
		return A0_HDCP2X_TX_AKE_INIT;
	}

	hdcp2x_tx_internal_info->prev_seq_num_m = hdcp2x_tx_internal_info->seq_num_m;

	result = xlnx_hdcp2x_tx_rptr_auth_stream_mng(xhdcp2x_tx);
	if (result < 0) {
		dev_dbg(xhdcp2x_tx->dev, "write message fail: stream manage");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_STREAM_READY_TIMEOUT_MS,
				   HDCP_2_2_REP_STREAM_READY);

	hdcp2x_tx_internal_info->content_strm_mng_chk_cntr++;

	return A9_HDCP2X_TX_VERIFY_MPRIME;
}

static enum hdcp2x_tx_state hdcp2x_tx_process_rcvid(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	dev_dbg(xhdcp2x_tx->dev, "receiver id sent ack");

	return A0_HDCP2X_TX_AKE_INIT;
}

static enum hdcp2x_tx_state hdcp2x_tx_wait_for_rcvid(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info =
		(struct hdcp2x_tx_pairing_info *)xhdcp2x_tx->xhdcp2x_info.state_context;
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	u8 V[HDCP2X_TX_V_SIZE];
	u32 seq_num_v;
	int i, result;
	u8 device_count;

	/*
	 * Wait for the receiver to respond within 3 secs.
	 * If the receiver has timed out we go to state A0.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.
	 */
	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx, 0, 1);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A6_HDCP2X_TX_WAIT_FOR_RCVID;

	dev_dbg(xhdcp2x_tx->dev, "wait for receiver id");

	result = hdcp2x_tx_receive_message(xhdcp2x_tx,
					   HDCP_2_2_REP_SEND_RECVID_LIST);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	device_count = (HDCP_2_2_DEV_COUNT_HI(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[0]) << 4 |
			HDCP_2_2_DEV_COUNT_LO(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[1]));

	xhdcp2x_tx->xhdcp2x_topology.devicecount = device_count + 1;
	xhdcp2x_tx->xhdcp2x_topology.depth =
		HDCP_2_2_DEPTH(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[0]);
	xhdcp2x_tx->xhdcp2x_topology.max_dev_exceeded =
		HDCP_2_2_MAX_DEVS_EXCEEDED(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[1]);
	xhdcp2x_tx->xhdcp2x_topology.max_cascaded_exceeded =
		HDCP_2_2_MAX_CASCADE_EXCEEDED(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[1]);
	xhdcp2x_tx->xhdcp2x_topology.hdcp2x_legacy_ds =
	HDCP2X_TX_LEGACY2X_DEVICE_DOWNSTREAM(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[1]);
	xhdcp2x_tx->xhdcp2x_topology.hdcp1x_legacy_ds =
	HDCP2X_TX_LEGACY1X_DEVICE_DOWNSTREAM(tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo[1]);

	if (xhdcp2x_tx->xhdcp2x_topology.max_dev_exceeded ||
	    xhdcp2x_tx->xhdcp2x_topology.max_cascaded_exceeded) {
		dev_err(xhdcp2x_tx->dev, "Failed with topology errors");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	dev_dbg(xhdcp2x_tx->dev, "start compute V-hash");

	xlnx_hdcp2x_tx_compute_v(xhdcp2x_tx->xhdcp2x_info.rn,
				 xhdcp2x_tx->xhdcp2x_info.r_rx,
				 tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo,
				 xhdcp2x_tx->xhdcp2x_info.r_tx,
				 (u8 *)tx_msg->msg_type.rpt_auth_send_rcvid.rcvids,
				 device_count,
				 tx_msg->msg_type.rpt_auth_send_rcvid.seq_num_v,
				 hdcp2x_tx_pairing_info->km, V);

	dev_dbg(xhdcp2x_tx->dev, "start compute V-hash done");

	if (memcmp(tx_msg->msg_type.rpt_auth_send_rcvid.vprime, V,
		   HDCP_2_2_V_PRIME_HALF_LEN)) {
		dev_err(xhdcp2x_tx->dev, "v-prime compare fail");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	for (i = 0; i < device_count; i++) {
		memcpy(&xhdcp2x_tx->xhdcp2x_topology.rcvid[i + 1],
		       &tx_msg->msg_type.rpt_auth_send_rcvid.rcvids[i],
			   HDCP_2_2_RECEIVER_ID_LEN);
		if (xhdcp2x_tx->xhdcp2x_hw.tx_mode == XHDCP2X_TX_TRANSMITTER) {
			u8 *rcv_id = tx_msg->msg_type.rpt_auth_send_rcvid.rcvids[i];

			if (xlnx_hdcp2x_tx_is_device_revoked(xhdcp2x_tx, rcv_id)) {
				xhdcp2x_tx->xhdcp2x_info.is_device_revoked = 1;
				xhdcp2x_tx->xhdcp2x_info.auth_status =
						XHDCP2X_TX_DEVICE_IS_REVOKED;
				return A0_HDCP2X_TX_AKE_INIT;
			}
		}
	}

	seq_num_v = drm_hdcp_be24_to_cpu(tx_msg->msg_type.rpt_auth_send_rcvid.seq_num_v);
	if (seq_num_v < xhdcp2x_tx->xhdcp2x_info.seq_num_v)
		return A0_HDCP2X_TX_AKE_INIT;

	xhdcp2x_tx->xhdcp2x_info.seq_num_v = seq_num_v;

	result = xlnx_hdcp2x_tx_write_rptr_auth_send_ack(xhdcp2x_tx,
							 &V[HDCP_2_2_V_PRIME_HALF_LEN]);
	if (result < 0) {
		dev_err(xhdcp2x_tx->dev, "write message fail - V prime");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	return A9_HDCP2X_TX_STREAM_MANAGE;
}

static enum hdcp2x_tx_state hdcp2x_tx_authenticated(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (!xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired)
		return A5_HDCP2X_TX_AUTHENTICATED;

	if (xhdcp2x_tx->xhdcp2x_info.auth_status
				!= XHDCP2X_TX_AUTHENTICATED)
		dev_dbg(xhdcp2x_tx->dev, "HDCP 2X Authenticated");

	if (xhdcp2x_tx->xhdcp2x_internal_timer.reason_id ==
			XHDCP2X_TX_TS_WAIT_FOR_CIPHER) {
		u8 handle_reauth_request = 0;

		if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_DP) {
			if ((xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
				(XHDCP2X_RX_STATUS_REAUTH_REQ |
				 XHDCP2X_RX_STATUS_LINK_INTEGRITY_FAIL))) {
				xhdcp2x_tx->xhdcp2x_info.dp_rx_status &=
					~(XHDCP2X_RX_STATUS_REAUTH_REQ |
					XHDCP2X_RX_STATUS_LINK_INTEGRITY_FAIL);
				handle_reauth_request = 1;
			}
		} else {
			if (xhdcp2x_tx->xhdcp2x_info.rx_status &
					XHDCP2X_TX_RXSTATUS_REAUTH_REQ_MASK)
				handle_reauth_request = 1;
		}

		if (handle_reauth_request) {
			xlnx_hdcp2x_handle_reauth_request(xhdcp2x_tx);
			return A0_HDCP2X_TX_AKE_INIT;
		}

		xhdcp2x_tx->xhdcp2x_info.auth_status =
				XHDCP2X_TX_AUTHENTICATED;

		xlnx_hdcp2x_tx_enable_encryption(xhdcp2x_tx);
		xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP2X_TX_WAIT_REAUTH_CHECK_TIMEOUT,
					   XHDCP2X_TX_TS_RX_REAUTH_CHECK);

		dev_info(xhdcp2x_tx->dev, "HDCP 2X Authenticated");

		return A5_HDCP2X_TX_AUTHENTICATED;
	}

	if (xhdcp2x_tx->xhdcp2x_internal_timer.reason_id ==
			XHDCP2X_TX_TS_RX_REAUTH_CHECK) {
		u8 handle_reauth_request = 0;
		u8 handle_repeater_rdy = 0;

		dev_dbg(xhdcp2x_tx->dev, "check for re-authentication");

		if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_DP) {
			if ((xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
				(XHDCP2X_RX_STATUS_REAUTH_REQ |
				 XHDCP2X_RX_STATUS_LINK_INTEGRITY_FAIL))) {
				xhdcp2x_tx->xhdcp2x_info.dp_rx_status &=
					~(XHDCP2X_RX_STATUS_REAUTH_REQ |
					XHDCP2X_RX_STATUS_LINK_INTEGRITY_FAIL);
				handle_reauth_request = 1;
			}

			if ((xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
					XHDCP2X_RX_STATUS_RPTR_RDY) ==
							XHDCP2X_RX_STATUS_RPTR_RDY)
				handle_repeater_rdy = 1;
		} else {
			if (xhdcp2x_tx->xhdcp2x_info.rx_status &
					XHDCP2X_TX_RXSTATUS_REAUTH_REQ_MASK)
				handle_reauth_request = 1;

			if (xhdcp2x_tx->xhdcp2x_info.rx_status &
					XHDCP2X_TX_RXSTATUS_READY_MASK)
				handle_repeater_rdy = 1;
		}

		if (handle_reauth_request) {
			xlnx_hdcp2x_handle_reauth_request(xhdcp2x_tx);
			return A0_HDCP2X_TX_AKE_INIT;
		}
		if (handle_repeater_rdy) {
			/* The downstream topology has changed */
			return A6_HDCP2X_TX_WAIT_FOR_RCVID;
		}

		xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP2X_TX_WAIT_REAUTH_CHECK_TIMEOUT,
					   XHDCP2X_TX_TS_RX_REAUTH_CHECK);
	}

	return A5_HDCP2X_TX_AUTHENTICATED;
}

static enum hdcp2x_tx_state hdcp2x_tx_rptr_check(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (xhdcp2x_tx->xhdcp2x_info.is_rcvr_repeater) {
		xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_RECVID_LIST_TIMEOUT_MS,
					   HDCP_2_2_REP_SEND_RECVID_LIST);
		return A6_HDCP2X_TX_WAIT_FOR_RCVID;
	}

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP2X_TX_WAIT_FOR_ENCRYPTION_TIMEOUT,
				   XHDCP2X_TX_TS_WAIT_FOR_CIPHER);

	return A5_HDCP2X_TX_AUTHENTICATED;
}

static enum hdcp2x_tx_state hdcp2x_tx_exchange_ks(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info =	(struct hdcp2x_tx_pairing_info *)
					xhdcp2x_tx->xhdcp2x_info.state_context;
	u8 riv[HDCP_2_2_RIV_LEN];
	u8 ks[HDCP2X_TX_KS_SIZE];
	u8 edkeys_ks[HDCP_2_2_E_DKEY_KS_LEN];
	int result;

	dev_dbg(xhdcp2x_tx->dev, "tx exchange ks");

	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng,
					  riv, HDCP_2_2_RIV_LEN, HDCP_2_2_RIV_LEN);
	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher,
				    riv, XHDCP2X_CIPHER_REG_RIV_1_OFFSET, HDCP_2_2_RIV_LEN);
	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng, ks,
					  HDCP2X_TX_KS_SIZE,
					  HDCP2X_TX_KS_SIZE);
	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher,
				    ks, XHDCP2X_CIPHER_REG_KS_1_OFFSET, HDCP2X_TX_KS_SIZE);
	xlnx_hdcp2x_tx_compute_edkey_ks(xhdcp2x_tx->xhdcp2x_info.rn,
					hdcp2x_tx_pairing_info->km, ks,
					xhdcp2x_tx->xhdcp2x_info.r_rx,
					xhdcp2x_tx->xhdcp2x_info.r_tx,
					edkeys_ks);

	result = xlnx_hdcp2x_tx_write_ske_send_eks(xhdcp2x_tx, edkeys_ks, riv);
	if (result < 0) {
		dev_err(xhdcp2x_tx->dev, "ske send eks write fail");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	result = xlnx_hdcp2x_tx_write_type_value(xhdcp2x_tx);
	if (result < 0) {
		dev_err(xhdcp2x_tx->dev, "SKE send ES write fail");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	return A4_HDCP2X_TX_REPEATER_CHECK;
}

static enum hdcp2x_tx_state hdcp2x_tx_verify_lprime_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info =
		(struct hdcp2x_tx_pairing_info *)xhdcp2x_tx->xhdcp2x_info.state_context;
	u8 lprime[HDCP_2_2_L_PRIME_LEN];
	int result = 0;

	/*
	 * Wait for the receiver to respond within 20 msecs.
	 * If the receiver has timed out we go to state A2 for a retry.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.
	 */
	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  sizeof(struct hdcp2x_tx_lc_send_lc_prime),
						  0);
	if (result < 0)
		return A2_HDCP2X_TX_LC_CHECK;

	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A2_HDCP2X_TX_VERIFY_LPRIME;

	result = hdcp2x_tx_receive_message(xhdcp2x_tx, HDCP_2_2_LC_SEND_LPRIME);
	if (result < 0)
		return A2_HDCP2X_TX_LC_CHECK;

	xlnx_hdcp2x_tx_compute_lprime(xhdcp2x_tx->xhdcp2x_info.rn,
				      hdcp2x_tx_pairing_info->km,
				      xhdcp2x_tx->xhdcp2x_info.r_rx,
				      xhdcp2x_tx->xhdcp2x_info.r_tx,
				      lprime);

	if (memcmp(tx_msg->msg_type.lcsend_lcprime.lprime,
		   lprime, sizeof(lprime))) {
		dev_err(xhdcp2x_tx->dev, "compare L fail");
		return A2_HDCP2X_TX_LC_CHECK;
	}

	dev_dbg(xhdcp2x_tx->dev, "locality check counter=%d\n",
		(u16)xhdcp2x_tx->xhdcp2x_info.lc_counter);

	return A3_HDCP2X_TX_EXCHANGE_KS;
}

static enum hdcp2x_tx_state hdcp2x_tx_lc_check(struct xlnx_hdcp2x_config
		*xhdcp2x_tx)
{
	int result;

	xhdcp2x_tx->xhdcp2x_info.lc_counter++;

	if (xhdcp2x_tx->xhdcp2x_info.lc_counter >
			HDCP2X_TX_MAX_ALLOWED_LOCALITY_CHECKS) {
		dev_dbg(xhdcp2x_tx->dev, "lc_counter = %d/n",
			(u16)xhdcp2x_tx->xhdcp2x_info.lc_counter - 1);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng,
					  xhdcp2x_tx->xhdcp2x_info.rn,
					  HDCP_2_2_RN_LEN,
					  HDCP_2_2_RN_LEN);
	result = xlnx_hdcp2x_tx_write_lcinit(xhdcp2x_tx,
					     xhdcp2x_tx->xhdcp2x_info.rn);
	if (result < 0) {
		dev_err(xhdcp2x_tx->dev, "write lc-init message fail");
		return A0_HDCP2X_TX_AKE_INIT;
	}

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_DP_LPRIME_TIMEOUT_MS,
				   HDCP_2_2_LC_SEND_LPRIME);

	return A2_HDCP2X_TX_VERIFY_LPRIME;
}

static enum hdcp2x_tx_state hdcp2x_tx_compute_hprime(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info =
	(struct hdcp2x_tx_pairing_info *)xhdcp2x_tx->xhdcp2x_info.state_context;
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	u8 h_prime[HDCP_2_2_H_PRIME_LEN];
	int result = 0;

	/*
	 * Wait for the receiver to respond within 1 second.
	 * If the receiver has timed out we go to state A0.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.
	 */
	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  sizeof(struct hdcp2x_tx_ake_sendprime),
						  0);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;
	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A1_HDCP2X_TX_VERIFY_HPRIME;

	result = hdcp2x_tx_receive_message(xhdcp2x_tx, HDCP_2_2_AKE_SEND_HPRIME);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	xlnx_hdcp2x_tx_compute_hprime(xhdcp2x_tx->xhdcp2x_info.r_rx,
				      hdcp2x_tx_pairing_info->rxcaps,
				      xhdcp2x_tx->xhdcp2x_info.r_tx,
				      xhdcp2x_tx->xhdcp2x_info.txcaps,
				      hdcp2x_tx_pairing_info->km, h_prime);

	if (memcmp(tx_msg->msg_type.ake_send_prime.h_prime, h_prime,
		   sizeof(h_prime))) {
		dev_err(xhdcp2x_tx->dev, "compare H' fail");
		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx,
						      hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	return A2_HDCP2X_TX_LC_CHECK;
}

static enum hdcp2x_tx_state hdcp2x_tx_wait_for_pairing_info(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info = &xhdcp2x_tx->xhdcp2x_pairing_info;
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	int result = 0;

	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  sizeof(struct hdcp2x_tx_ake_send_pairing_info),
						  0);
	if (result < 0) {
		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx, hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}
	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A1_HDCP2X_TX_WAIT_FOR_PAIRING;

	dev_dbg(xhdcp2x_tx->dev, "wait for pairing to be done");
	result = hdcp2x_tx_receive_message(xhdcp2x_tx,
					   HDCP_2_2_AKE_SEND_PAIRING_INFO);
	if (result < 0) {
		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx, hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	memcpy(hdcp2x_tx_pairing_info->ekh_km,
	       tx_msg->msg_type.ake_send_pairing_info.ekh_km,
	       sizeof(hdcp2x_tx_pairing_info->ekh_km));

	hdcp2x_tx_pairing_info = xlnx_hdcp2x_tx_update_pairinginfo(xhdcp2x_tx,
								   hdcp2x_tx_pairing_info, 1);

	return A2_HDCP2X_TX_LC_CHECK;
}

static enum hdcp2x_tx_state hdcp2x_tx_wait_for_hprime_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info =
				(struct hdcp2x_tx_pairing_info *)
				xhdcp2x_tx->xhdcp2x_info.state_context;
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	u8 h_prime[HDCP_2_2_H_PRIME_LEN];
	int result = 0;

	/*
	 * Wait for the receiver to respond within 1 second.
	 * If the receiver has timed out we go to state A0.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.
	 */
	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  sizeof(struct hdcp2x_tx_ake_sendprime), 0);
	if (result < 0) {
		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx,
						      hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}
	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A1_HDCP2X_TX_WAIT_FOR_HPRIME;

	dev_dbg(xhdcp2x_tx->dev, "wait for H-Prime");

	result = hdcp2x_tx_receive_message(xhdcp2x_tx,
					   HDCP_2_2_AKE_SEND_HPRIME);
	if (result < 0) {
		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx,
						      hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	xlnx_hdcp2x_tx_compute_hprime(hdcp2x_tx_pairing_info->rrx,
				      hdcp2x_tx_pairing_info->rxcaps,
				      hdcp2x_tx_pairing_info->rtx,
				      xhdcp2x_tx->xhdcp2x_info.txcaps,
				      hdcp2x_tx_pairing_info->km, h_prime);
	dev_dbg(xhdcp2x_tx->dev, "Compute H' done");

	if (memcmp(tx_msg->msg_type.ake_send_prime.h_prime, h_prime, sizeof(h_prime))) {
		dev_dbg(xhdcp2x_tx->dev, "compare H' fail");

		xlnx_hdcp2x_tx_invalidate_paring_info(xhdcp2x_tx,
						      hdcp2x_tx_pairing_info->rcvid);
		return A0_HDCP2X_TX_AKE_INIT;
	}

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_PAIRING_TIMEOUT_MS,
				   HDCP_2_2_AKE_SEND_PAIRING_INFO);

	return A1_HDCP2X_TX_WAIT_FOR_PAIRING;
}

static enum hdcp2x_tx_state hdcp2x_tx_wait_for_ack(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg *xhdcp2x_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	struct hdcp2x_tx_pairing_info  *xhdcp2x_pairing_info = NULL;
	struct hdcp2x_tx_pairing_info xhdcp2x_new_pairing_info;
	const u8 *kpubdpcptr = NULL;
	int result;

	result = xlnx_hdcp2x_tx_wait_for_receiver(xhdcp2x_tx,
						  sizeof(struct hdcp2x_tx_ake_sendcert), 0);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	if (!xhdcp2x_tx->xhdcp2x_info.msg_available)
		return A1_HDCP2X_TX_WAIT_FOR_ACK;

	result = hdcp2x_tx_receive_message(xhdcp2x_tx, HDCP_2_2_AKE_SEND_CERT);

	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	kpubdpcptr = xlnx_hdcp2x_tx_get_publickey(xhdcp2x_tx);

	if (!kpubdpcptr)
		return A0_HDCP2X_TX_AKE_INIT;

	result = xlnx_hdcp2x_tx_verify_certificate(&xhdcp2x_msg->msg_type.ake_send_cert.cert_rx,
						   kpubdpcptr, HDCP2X_TX_KPUB_DCP_LLC_N_SIZE,
						   &kpubdpcptr[HDCP2X_TX_KPUB_DCP_LLC_N_SIZE],
						   HDCP2X_TX_KPUB_DCP_LLC_E_SIZE);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	if (xhdcp2x_tx->xhdcp2x_hw.tx_mode == XHDCP2X_TX_TRANSMITTER) {
		u8 *rcv_id = xhdcp2x_msg->msg_type.ake_send_cert.cert_rx.rcvid;

		if (xlnx_hdcp2x_tx_is_device_revoked(xhdcp2x_tx, rcv_id)) {
			xhdcp2x_tx->xhdcp2x_info.is_device_revoked = 1;
			xhdcp2x_tx->xhdcp2x_info.auth_status =
					XHDCP2X_TX_DEVICE_IS_REVOKED;
			return A0_HDCP2X_TX_AKE_INIT;
		}
		xhdcp2x_tx->xhdcp2x_info.is_device_revoked  = 0;
	}
	memcpy(&xhdcp2x_tx->xhdcp2x_topology.rcvid[0],
	       xhdcp2x_msg->msg_type.ake_send_cert.cert_rx.rcvid,
		   HDCP_2_2_RECEIVER_ID_LEN);
	xhdcp2x_tx->xhdcp2x_topology.devicecount = 1;

	if (xhdcp2x_msg->msg_type.ake_send_cert.rxcaps[2] & 0x1)
		xhdcp2x_tx->xhdcp2x_info.is_rcvr_repeater = 1;
	else
		xhdcp2x_tx->xhdcp2x_info.is_rcvr_repeater = 0;

	memcpy(xhdcp2x_tx->xhdcp2x_info.r_rx,
	       xhdcp2x_msg->msg_type.ake_send_cert.r_rx,
	       sizeof(xhdcp2x_tx->xhdcp2x_info.r_rx));

	xhdcp2x_pairing_info =
		xlnx_hdcp2x_tx_get_pairing_info(xhdcp2x_tx,
						xhdcp2x_msg->msg_type.ake_send_cert.cert_rx.rcvid);
	if (xhdcp2x_pairing_info) {
		if (xhdcp2x_pairing_info->ready) {
			memcpy(xhdcp2x_pairing_info->rxcaps,
			       xhdcp2x_msg->msg_type.ake_send_cert.rxcaps,
			       sizeof(xhdcp2x_pairing_info->rxcaps));

			result = xlnx_hdcp2x_tx_write_ake_storedkm(xhdcp2x_tx,
								   xhdcp2x_pairing_info);
			if (result < 0)
				return A0_HDCP2X_TX_AKE_INIT;

			xhdcp2x_tx->xhdcp2x_info.state_context =
					xhdcp2x_pairing_info;

			xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_PAIRING_TIMEOUT_MS,
						   HDCP_2_2_AKE_SEND_HPRIME);
			return A1_HDCP2X_TX_VERIFY_HPRIME;
		}
	}
	memcpy(xhdcp2x_new_pairing_info.rrx, xhdcp2x_tx->xhdcp2x_info.r_rx,
	       sizeof(xhdcp2x_new_pairing_info.rrx));
	memcpy(xhdcp2x_new_pairing_info.rtx, xhdcp2x_tx->xhdcp2x_info.r_tx,
	       sizeof(xhdcp2x_new_pairing_info.rtx));
	memcpy(xhdcp2x_new_pairing_info.rxcaps, xhdcp2x_msg->msg_type.ake_send_cert.rxcaps,
	       sizeof(xhdcp2x_new_pairing_info.rxcaps));
	memcpy(xhdcp2x_new_pairing_info.rcvid, xhdcp2x_msg->msg_type.ake_send_cert.cert_rx.rcvid,
	       sizeof(xhdcp2x_new_pairing_info.rcvid));

	xlnx_hdcp2x_tx_generatekm(xhdcp2x_tx, xhdcp2x_new_pairing_info.km);

	xhdcp2x_pairing_info =
			xlnx_hdcp2x_tx_update_pairinginfo(xhdcp2x_tx,
							  &xhdcp2x_new_pairing_info,
							  0);
	if (!xhdcp2x_pairing_info)
		return A0_HDCP2X_TX_AKE_INIT;

	xhdcp2x_tx->xhdcp2x_info.state_context =  (void *)xhdcp2x_pairing_info;

	result = xlnx_hdcp2x_tx_write_akenostored_km(xhdcp2x_tx,
						     &xhdcp2x_new_pairing_info,
						     &xhdcp2x_msg->msg_type.ake_send_cert.cert_rx);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS,
				   HDCP_2_2_AKE_SEND_HPRIME);

	return A1_HDCP2X_TX_WAIT_FOR_HPRIME;
}

static enum hdcp2x_tx_state hdcp2x_tx_write_ake_init(struct xlnx_hdcp2x_config
						     *xhdcp2x_tx)
{
	if (!xhdcp2x_tx->xhdcp2x_info.is_enabled)
		return H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE;

	if (!xhdcp2x_tx->xhdcp2x_info.is_rcvr_hdcp2x_capable) {
		xhdcp2x_tx->xhdcp2x_info.auth_status =
		XHDCP2X_TX_INCOMPATIBLE_RX;
		return H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE;
	}

	xhdcp2x_tx->xhdcp2x_info.auth_status =
			XHDCP2X_TX_AUTHENTICATION_BUSY;

	xlnx_hdcp2x_tx_disable_encryption(xhdcp2x_tx);
	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_CERT_TIMEOUT_MS, A0_HDCP2X_TX_AKE_INIT);

	return A1_HDCP2X_TX_EXCHANGE_KM;
}

static enum hdcp2x_tx_state hdcp2x_tx_exchange_km_process(struct xlnx_hdcp2x_config
							  *xhdcp2x_tx)
{
	int result;

	if (!xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired)
		return A1_HDCP2X_TX_EXCHANGE_KM;

	result = xlnx_hdcp2x_tx_write_ake_init(xhdcp2x_tx);
	if (result < 0)
		return A0_HDCP2X_TX_AKE_INIT;

	xlnx_hdcp2x_tx_start_timer(xhdcp2x_tx, HDCP_2_2_CERT_TIMEOUT_MS, HDCP_2_2_AKE_SEND_CERT);

	memset(&xhdcp2x_tx->xhdcp2x_topology, 0, sizeof(xhdcp2x_tx->xhdcp2x_topology));

	xhdcp2x_tx->xhdcp2x_info.seq_num_v  = 0;
	xhdcp2x_tx->xhdcp2x_info.seq_num_m = 0;
	xhdcp2x_tx->xhdcp2x_info.content_strm_mng_chk_cntr  = 0;
	xhdcp2x_tx->xhdcp2x_info.lc_counter = 0;
	xhdcp2x_tx->xhdcp2x_info.prev_seq_num_m = 0;

	return A1_HDCP2X_TX_WAIT_FOR_ACK;
}

static enum hdcp2x_tx_state hdcp2x_tx_wait_for_tx_state(struct xlnx_hdcp2x_config
		*xhdcp2x_tx)
{
	if (xhdcp2x_tx->xhdcp2x_info.auth_status !=
			XHDCP2X_TX_AUTHENTICATION_BUSY)
		return H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE;

	xhdcp2x_tx->xhdcp2x_info.is_rcvr_hdcp2x_capable =
			xlnx_hdcp2x_downstream_capbility(xhdcp2x_tx);

	if (xhdcp2x_tx->xhdcp2x_info.is_rcvr_hdcp2x_capable)
		return A0_HDCP2X_TX_AKE_INIT;

	xhdcp2x_tx->xhdcp2x_info.auth_status =
			XHDCP2X_TX_INCOMPATIBLE_RX;

	return H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE;
}

static enum hdcp2x_tx_state hdcp2x_tx_idle_state(struct xlnx_hdcp2x_config
		*xhdcp2x_tx)
{
	return H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE;
}

/*
 * HDCP Transmitter State Diagram available in
 * HDCP2.3 specification. Section 2.8
 * https://www.digital-cp.com/sites/default/files/
 * HDCP%20Interface%20Independent%20Adaptation%20Specification%20Rev2_3.pdf
 */
int hdcp2x_tx_protocol_authenticate_sm(struct xlnx_hdcp2x_config *hdcp2x_tx)
{
	int status = H0_HDCP2X_TX_NO_RX_ATTACHED;
	enum hdcp2x_tx_state hdcp_state = hdcp2x_tx->xhdcp2x_info.curr_state;

	switch (hdcp_state) {
	case H0_HDCP2X_TX_NO_RX_ATTACHED:
		status = hdcp2x_tx_idle_state(hdcp2x_tx);
		break;
	case H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE:
		status = hdcp2x_tx_wait_for_tx_state(hdcp2x_tx);
		break;
	case A0_HDCP2X_TX_AKE_INIT:
		status = hdcp2x_tx_write_ake_init(hdcp2x_tx);
		break;
	case A1_HDCP2X_TX_EXCHANGE_KM:
		status = hdcp2x_tx_exchange_km_process(hdcp2x_tx);
		break;
	case A1_HDCP2X_TX_WAIT_FOR_ACK:
		status = hdcp2x_tx_wait_for_ack(hdcp2x_tx);
		break;
	case A1_HDCP2X_TX_WAIT_FOR_HPRIME:
		status = hdcp2x_tx_wait_for_hprime_msg(hdcp2x_tx);
		break;
	case A1_HDCP2X_TX_WAIT_FOR_PAIRING:
		status = hdcp2x_tx_wait_for_pairing_info(hdcp2x_tx);
		break;
	case A1_HDCP2X_TX_VERIFY_HPRIME:
		status = hdcp2x_tx_compute_hprime(hdcp2x_tx);
		break;
	case A2_HDCP2X_TX_LC_CHECK:
		status = hdcp2x_tx_lc_check(hdcp2x_tx);
		break;
	case A2_HDCP2X_TX_VERIFY_LPRIME:
		status = hdcp2x_tx_verify_lprime_msg(hdcp2x_tx);
		break;
	case A3_HDCP2X_TX_EXCHANGE_KS:
		status = hdcp2x_tx_exchange_ks(hdcp2x_tx);
		break;
	case A4_HDCP2X_TX_REPEATER_CHECK:
		status = hdcp2x_tx_rptr_check(hdcp2x_tx);
		break;
	case A5_HDCP2X_TX_AUTHENTICATED:
		status = hdcp2x_tx_authenticated(hdcp2x_tx);
		break;
	case A6_HDCP2X_TX_WAIT_FOR_RCVID:
		status = hdcp2x_tx_wait_for_rcvid(hdcp2x_tx);
		break;
	case A7_HDCP2X_TX_VERIFY_RCVID:
		status = hdcp2x_tx_process_rcvid(hdcp2x_tx);
		break;
	case A9_HDCP2X_TX_STREAM_MANAGE:
		status = hdcp2x_tx_process_stream_manage(hdcp2x_tx);
		break;
	case A9_HDCP2X_TX_VERIFY_MPRIME:
		status = hdcp2x_tx_verify_mprime(hdcp2x_tx);
		break;
	default:
		status = hdcp_state;
		dev_dbg(hdcp2x_tx->dev, "Invalid HDCP State");
		break;
	}

	return status;
}
