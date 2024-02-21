// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Protocol Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Kunal Vasant Rane <kunal.rane@amd.com>
 *
 * This driver provides standard HDCP2X protocol specific functionalities.
 * It consists of:
 * - HDCP, Random number Generator, MMULT, Cipher and Timer core initialization
 *   functions. Along with that it offers enable, disable and callback functionalities
 *   HDCP2X RX driver.
 *
 * This driver still have Xilinx specific functionalities as it is not upstreamed now,
 * it will be updated as more generic and standardized driver in the next upstream version.
 *
 * Reference :
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 */

#include <linux/xlnx/xlnx_hdcp_common.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xilinx-hdcp2x-rx.h"

#define XHDCP2X_CIPHER_OFFSET		0x0000
#define XHDCP2X_RNG_OFFSET		0x1000
#define XHDCP2X_MMULT_OFFSET		0x2000
#define XHDCP2X_TIMER_CLOCK_FREQ_HZ	99990001
#define XHDCP2X_CLK_DIV			1000000
#define XHDCP2X_CLK_MUL			1000
#define XHDCP2X_PRIVATE_KEY_OFFSET	562
#define XHDCP2X_PUBLIC_KEY_OFFSET	40
#define XHDCP2X_RX_LC128_SIZE		16

static enum xhdcp2x_rx_state xhdcp2x_state_B0(void *);
static enum xhdcp2x_rx_state xhdcp2x_state_B1(void *);
static enum xhdcp2x_rx_state xhdcp2x_state_B2(void *);
static enum xhdcp2x_rx_state xhdcp2x_state_B3(void *);
static enum xhdcp2x_rx_state xhdcp2x_state_B4(void *);
static void xhdcp2x_rx_run_statemachine(struct xlnx_hdcp2x_config *hdcp2x);

/*
 * HDCP Receiver State Diagram available in
 * HDCP2.9 specification. Section 2.9
 * https://www.digital-cp.com/sites/default/files/
 * HDCP%20Interface%20Independent%20Adaptation%20Specification%20Rev2_3.pdf
 */
static enum xhdcp2x_rx_state (*xhdcp2x_rx_state_table[])(void *) = {
	xhdcp2x_state_B0,
	xhdcp2x_state_B1,
	xhdcp2x_state_B2,
	xhdcp2x_state_B3,
	xhdcp2x_state_B4
};

static int xhdcp2x_rx_set_reauth_req(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int status = 0;

	if (!xhdcp2x_rx)
		return -EINVAL;

	xhdcp2x_rx->info.reauth_request_cnt++;

	xhdcp2x_rx->info.reauth_req = 1;

	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		u8 rxstatus = RX_STATUS_REAUTH_REQ;

		status = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
							 RX_STATUS_OFFSET, &rxstatus,
							 1);
		if (status < 0)
			return -EINVAL;

		xhdcp2x_rx->handlers.cp_irq_handler(xhdcp2x_rx->interface_ref);
	}

	return 0;
}

static void xhdcp2x_rx_run_statemachine(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	enum xhdcp2x_rx_state new_state;

	do {
		new_state = xhdcp2x_rx_state_table[xhdcp2x_rx->curr_state](xhdcp2x_rx);
		xhdcp2x_rx->prev_state = xhdcp2x_rx->curr_state;
		xhdcp2x_rx->curr_state = new_state;
	} while (xhdcp2x_rx->prev_state != xhdcp2x_rx->curr_state);
}

static void xhdcp2x_sm_work_func(struct work_struct *work)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx;

	xhdcp2x_rx = container_of(work, struct xlnx_hdcp2x_config, sm_work.work);
	mutex_lock(&xhdcp2x_rx->hdcprx_mutex);

	if (xhdcp2x_rx->info.msg_event)
		xhdcp2x_rx_run_statemachine(xhdcp2x_rx);

	mutex_unlock(&xhdcp2x_rx->hdcprx_mutex);
}

int *xhdcp2x_rx_init(struct device *dev, void *protocol_ref, void __iomem *xhdcp_base_address,
		     enum xhdcp2x_rx_protocol protocol_rx, bool is_repeater, u8 lane_count)
{
	int status;
	struct xlnx_hdcp2x_config *xhdcp2x_rx;

	if (!dev || !protocol_ref || !xhdcp_base_address)
		return ERR_PTR(-EINVAL);

	if (is_repeater) {
		dev_info(dev, "Hdcp2x repeater functionality not supported\n");
		return ERR_PTR(-EINVAL);
	}

	xhdcp2x_rx = kzalloc(sizeof(*xhdcp2x_rx), GFP_KERNEL);
	if (!xhdcp2x_rx)
		return ERR_PTR(-ENOMEM);

	xhdcp2x_rx->xhdcp2x_hw.hdcp2xcore_address = (void __iomem *)xhdcp_base_address;
	xhdcp2x_rx->dev = dev;
	xhdcp2x_rx->interface_ref = protocol_ref;
	xhdcp2x_rx->interface_base = xhdcp_base_address;
	xhdcp2x_rx->is_repeater = is_repeater ? 1 : 0;
	xhdcp2x_rx->lane_count = lane_count;
	xhdcp2x_rx->protocol = protocol_rx;
	xhdcp2x_rx->rx_caps[0] = HDCP_2_2_RX_CAPS_VERSION_VAL;
	xhdcp2x_rx->rx_caps[1] = 0x00;
	xhdcp2x_rx->rx_caps[2] = (xhdcp2x_rx->mode == xhdcp2x_rx_receiver) ?
				RXCAPS_HDCP_ENABLE :  RXCAPS_REPEATER;
	xhdcp2x_rx->keys_loaded = 0;

	xhdcp2x_rx->xhdcp2x_hw.rng_inst.rng_coreaddress =
		xhdcp2x_rx->xhdcp2x_hw.hdcp2xcore_address + XHDCP2X_RNG_OFFSET;
	xhdcp2x_rx->xhdcp2x_hw.mmult_inst.mmult_coreaddress =
		xhdcp2x_rx->xhdcp2x_hw.hdcp2xcore_address + XHDCP2X_MMULT_OFFSET;
	xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress =
		xhdcp2x_rx->xhdcp2x_hw.hdcp2xcore_address + XHDCP2X_CIPHER_OFFSET;

	status = xlnx_hdcp2x_rng_cfg_init(&xhdcp2x_rx->xhdcp2x_hw.rng_inst);
	if (status < 0)
		return ERR_PTR(-EINVAL);

	status = xlnx_hdcp2x_mmult_cfginit(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst);
	if (status < 0)
		return ERR_PTR(-EINVAL);

	status = xlnx_hdcp2x_cipher_cfg_init(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst);
	if (status < 0)
		return ERR_PTR(-EINVAL);

	xlnx_hdcp2x_rx_cipher_init(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst);

	mutex_init(&xhdcp2x_rx->hdcprx_mutex);

	INIT_DELAYED_WORK(&xhdcp2x_rx->sm_work, xhdcp2x_sm_work_func);

	return (void *)xhdcp2x_rx;
}

void *xhdcp2x_timer_init(struct device *dev, void __iomem *timer_base_address)
{
	struct xlnx_hdcp_timer_config *tmr_config;
	int ret;

	tmr_config = devm_kzalloc(dev, sizeof(*tmr_config), GFP_KERNEL);
	if (!tmr_config)
		return ERR_PTR(-ENOMEM);

	tmr_config->hw_config.coreaddress = (void __iomem *)
		timer_base_address;

	tmr_config->hw_config.sys_clock_freq = XHDCP2X_TIMER_CLOCK_FREQ_HZ;

	ret = xlnx_hdcp_tmrcntr_init(tmr_config);
	if (ret < 0)
		return ERR_PTR(-EINVAL);

	return tmr_config;
}

void xhdcp2x_timer_attach(struct xlnx_hdcp2x_config *xhdcp2x_rx,
			  struct xlnx_hdcp_timer_config *tmrcntr)
{
	xhdcp2x_rx->tmr_config = *tmrcntr;

	xlnx_hdcp_tmrcntr_set_options(&xhdcp2x_rx->tmr_config,
				      XHDCP2X_RX_TMR_CNTR_0, XTC_AUTO_RELOAD_OPTION);
	xlnx_hdcp_tmrcntr_set_options(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1,
				      XTC_INT_MODE_OPTION | XTC_DOWN_COUNT_OPTION);
}

int xhdcp2x_rx_disable(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int status = 0;

	if (!xhdcp2x_rx)
		return -EINVAL;

	status = xhdcp2x_rx_reset(xhdcp2x_rx);
	if (!status)
		return -EINVAL;

	xhdcp2x_rx->curr_state = XHDCP2X_STATE_B0;
	xhdcp2x_rx->prev_state = XHDCP2X_STATE_B0;
	xhdcp2x_rx->info.msg_event = 0;

	if (xhdcp2x_rx->info.authentication_status == XHDCP2X_RX_AUTHENTICATED) {
		status = xhdcp2x_rx_set_reauth_req(xhdcp2x_rx);
		if (!status)
			return -EINVAL;
	}

	xlnx_hdcp2x_rng_disable(&xhdcp2x_rx->xhdcp2x_hw.rng_inst);
	xlnx_hdcp2x_cipher_disable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);
	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1);

	xhdcp2x_rx->info.is_enabled = 0;

	return 0;
}

int xhdcp2x_rx_reset(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	memset(&xhdcp2x_rx->msg_buffer, 0, sizeof(union xhdcp2x_rx_message));
	xhdcp2x_rx->msg_size = 0;

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_UNAUTHENTICATED;
	xhdcp2x_rx->info.is_no_storedkm = 0;
	xhdcp2x_rx->info.reauth_req = 0;
	xhdcp2x_rx->info.is_encrypted = 0;
	xhdcp2x_rx->info.lc_init_attempts = 0;
	xhdcp2x_rx->info.auth_request_cnt = 0;
	xhdcp2x_rx->info.reauth_request_cnt = 0;
	xhdcp2x_rx->info.link_error_cnt = 0;
	xhdcp2x_rx->info.error_flag = XHDCP2X_RX_ERROR_FLAG_NONE;
	xhdcp2x_rx->info.error_flag_sticky = XHDCP2X_RX_ERROR_FLAG_NONE;
	xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B0_WAIT_AKEINIT;

	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1);
	xlnx_hdcp2x_cipher_disable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);

	if (xhdcp2x_rx->handlers.notify_handler)
		xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
						    XHDCP2X_RX_NOTIFY_UN_AUTHENTICATED);

	return 0;
}

static int xhdcp2x_rx_set_rx_caps(struct xlnx_hdcp2x_config *xhdcp2x_rx, u8 enable)
{
	u8 rx_caps[RX_CAPS_SIZE] = {0};
	u8 numwritten = 0;

	if (enable)
		memcpy(rx_caps, xhdcp2x_rx->rx_caps, RX_CAPS_SIZE);

	numwritten = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
						     RX_CAPS_OFFSET, rx_caps, RX_CAPS_SIZE);

	if (numwritten != RX_CAPS_SIZE)
		return -EINVAL;

	return 0;
}

int xhdcp2x_rx_enable(struct xlnx_hdcp2x_config *xhdcp2x_rx, u8 lane_count)
{
	int ret = 0;

	xlnx_hdcp2x_rng_enable(&xhdcp2x_rx->xhdcp2x_hw.rng_inst);
	xlnx_hdcp2x_mmult_enable(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst);
	xlnx_hdcp2x_cipher_enable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);
	xlnx_hdcp2x_cipher_set_lanecount(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst,
					 lane_count);
	xhdcp2x_rx->info.is_enabled = 1;

	ret = xhdcp2x_rx_set_rx_caps(xhdcp2x_rx, 1);
	if (ret < 0)
		return -EINVAL;

	return ret;
}

int xhdcp2x_rx_set_callback(void *ref, u32 handler_type, void *callbackfunc)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)ref;

	if (!xhdcp2x_rx || !callbackfunc)
		return -EINVAL;

	switch (handler_type) {
	case (XHDCP2X_RX_HANDLER_DP_AUX_READ):
		xhdcp2x_rx->handlers.rd_handler = callbackfunc;
		break;

	case (XHDCP2X_RX_HANDLER_DP_AUX_WRITE):
		xhdcp2x_rx->handlers.wr_handler = callbackfunc;
		break;

	case (XHDCP2X_RX_HANDLER_DP_CP_IRQ_SET):
		xhdcp2x_rx->handlers.cp_irq_handler = callbackfunc;
		break;

	case (XHDCP2X_RX_NOTIFICATION_HANDLER):
		xhdcp2x_rx->handlers.notify_handler = callbackfunc;
		break;
	default:
		dev_info(xhdcp2x_rx->dev, "wrong handler type\n");
		return -EINVAL;
	}

	return 0;
}

static int xhdcp2x_rx_calc_nprime(struct xlnx_hdcp2x_config *xhdcp2x_rx, const u8 *private_key_ptr)
{
	int status = 0;
	struct xhdcp2x_rx_kpriv_rx *privatekey = (struct xhdcp2x_rx_kpriv_rx *)private_key_ptr;

	status = xhdcp2x_rx_calc_mont_nprime(xhdcp2x_rx->mmult, xhdcp2x_rx->nprimep,
					     (u8 *)privatekey->p,
					     XHDCP2X_RX_P_SIZE / XHDCP2X_KEY_SIZE);
	if (status) {
		dev_info(xhdcp2x_rx->dev, "Error: HDCP2X RX MMULT NPrimerP generation failed");
		return status;
	}

	status = xhdcp2x_rx_calc_mont_nprime(xhdcp2x_rx->mmult, xhdcp2x_rx->nprimeq,
					     (u8 *)privatekey->q,
					     XHDCP2X_RX_P_SIZE / XHDCP2X_KEY_SIZE);
	if (status) {
		dev_info(xhdcp2x_rx->dev, "Error: HDCP2X RX MMULT NPrimeQ generation failed");
		return status;
	}

	return status;
}

int xhdcp2x_rx_set_key(void *ref, void *xhdcp2x_lc128_key, void *xhdcp2x_private_key)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)ref;

	int status = 0;

	xhdcp2x_rx->lc128key = (u8 *)xhdcp2x_lc128_key;
	xhdcp2x_rx->privatekeyptr = (u8 *)xhdcp2x_private_key;

	xhdcp2x_rx->publiccertptr = xhdcp2x_private_key + XHDCP2X_PUBLIC_KEY_OFFSET;
	xhdcp2x_rx->privatekeyptr = xhdcp2x_private_key + XHDCP2X_PRIVATE_KEY_OFFSET;

	status = xhdcp2x_rx_calc_nprime(xhdcp2x_rx, xhdcp2x_private_key +
					XHDCP2X_PRIVATE_KEY_OFFSET);

	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst,
				    xhdcp2x_rx->lc128key,
				    XHDCP2X_CIPHER_REG_LC128_1_OFFSET, XHDCP2X_RX_LC128_SIZE);

	xhdcp2x_rx->keys_loaded = 1;

	return status;
}

void xhdcp2x_rx_timer_handler(void *callbackref, u8 tmr_cnt_number)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)callbackref;

	if (tmr_cnt_number == XHDCP2X_RX_TMR_CNTR_0)
		return;

	xhdcp2x_rx->info.timer_expired = 1;
	xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_TIMER_EVENT;
}

void xhdcp2x_rx_set_stream_type(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	u8 buf[R_IV_SIZE] = {0};

	xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
					RX_STREAM_TYPE_OFFSET,
					xhdcp2x_rx->param.streamidtype + 1,
					RX_STREAM_TYPE_SIZE);

	if (xhdcp2x_rx->param.streamidtype[1]) {
		memcpy(buf, xhdcp2x_rx->param.riv, R_IV_SIZE);
		buf[R_IV_SIZE - 1] ^= 0x01;
		xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst,
					    buf, XHDCP2X_CIPHER_REG_RIV_1_OFFSET, R_IV_SIZE);
	}
}

/**
 * xhdcp2x_rx_push_events - Pushes events from interface driver to HDCP driver
 * @ref: reference to HDCP2X instance
 * @events: events that are pushed from interface driver
 *
 * Return: 0 on success, error otherwise
 */
int xhdcp2x_rx_push_events(void *ref, u32 events)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)ref;

	if (!xhdcp2x_rx)
		return -EINVAL;

	if (events) {
		if (events == XHDCP2X_RX_DPCD_AKE_INIT_RCVD)
			xhdcp2x_rx->info.msg_event = events;
		else
			xhdcp2x_rx->info.msg_event |= events;
		schedule_delayed_work(&xhdcp2x_rx->sm_work, 0);
	}

	return 0;
}

static void xhdcp2x_rx_reset_params(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	memset(xhdcp2x_rx->param.km,		0, sizeof(xhdcp2x_rx->param.km));
	memset(xhdcp2x_rx->param.ks,		0, sizeof(xhdcp2x_rx->param.ks));
	memset(xhdcp2x_rx->param.rn,		0, sizeof(xhdcp2x_rx->param.rn));
	memset(xhdcp2x_rx->param.ekh,		0, sizeof(xhdcp2x_rx->param.ekh));
	memset(xhdcp2x_rx->param.riv,		0, sizeof(xhdcp2x_rx->param.riv));
	memset(xhdcp2x_rx->param.rrx,		0, sizeof(xhdcp2x_rx->param.rrx));
	memset(xhdcp2x_rx->param.rtx,		0, sizeof(xhdcp2x_rx->param.rtx));
	memset(xhdcp2x_rx->param.rxcaps,	0, sizeof(xhdcp2x_rx->param.rxcaps));
	memset(xhdcp2x_rx->param.txcaps,	0, sizeof(xhdcp2x_rx->param.txcaps));
	memset(xhdcp2x_rx->param.hprime,	0, sizeof(xhdcp2x_rx->param.hprime));
	memset(xhdcp2x_rx->param.lprime,	0, sizeof(xhdcp2x_rx->param.lprime));
	memset(xhdcp2x_rx->param.vprime,	0, sizeof(xhdcp2x_rx->param.vprime));
	memset(xhdcp2x_rx->param.seqnumm,	0, sizeof(xhdcp2x_rx->param.seqnumm));
	memset(xhdcp2x_rx->param.streamidtype,	0, sizeof(xhdcp2x_rx->param.streamidtype));
	memset(xhdcp2x_rx->param.mprime,	0, sizeof(xhdcp2x_rx->param.mprime));
}

static void xhdcp2x_rx_reset_after_error(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int authentication_status = xhdcp2x_rx->info.authentication_status;

	xlnx_hdcp2x_cipher_disable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);
	xlnx_hdcp2x_cipher_enable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);

	memset(&xhdcp2x_rx->msg_buffer, 0, sizeof(union xhdcp2x_rx_message));

	xhdcp2x_rx->msg_size = 0;
	xhdcp2x_rx->curr_state = XHDCP2X_STATE_B0;
	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_UNAUTHENTICATED;
	xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B0_WAIT_AKEINIT;
	xhdcp2x_rx->info.is_no_storedkm = 0;
	xhdcp2x_rx->info.is_encrypted = 0;
	xhdcp2x_rx->info.lc_init_attempts = 0;

	xhdcp2x_rx->info.timer_expired = 0;
	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1);

	xhdcp2x_rx_reset_params(xhdcp2x_rx);

	if (authentication_status == XHDCP2X_RX_AUTHENTICATED) {
		if (xhdcp2x_rx->handlers.notify_handler)
			xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
							    XHDCP2X_RX_NOTIFY_UN_AUTHENTICATED);
	}
}

static int xhdcp2x_rx_read_dpcd_msg(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int size = 0;
	u8 buf[XHDCP2X_RX_MAX_MESSAGE_SIZE] = {0};
	u32 msg_event = xhdcp2x_rx->info.msg_event;

	switch (msg_event) {
	case XHDCP2X_RX_DPCD_AKE_INIT_RCVD:
		size = xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
		R_TX_OFFSET, buf, R_TX_SIZE + TX_CAPS_SIZE);

		xhdcp2x_rx->msg_buffer[0] = XHDCP2X_RX_MSG_ID_AKEINIT;
		memcpy(&xhdcp2x_rx->msg_buffer[1], buf,	R_TX_SIZE + TX_CAPS_SIZE);
		size += 1;
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_DPCD_AKE_INIT_RCVD;
		break;
	case XHDCP2X_RX_DPCD_AKE_NO_STORED_KM_RCVD:
		size = xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
		E_KPUB_KM_OFFSET, buf, E_KPUB_KM_SIZE);

		xhdcp2x_rx->msg_buffer[0] = XHDCP2X_RX_MSG_ID_AKENOSTOREDKM;
		memcpy(&xhdcp2x_rx->msg_buffer[1], buf,	E_KPUB_KM_SIZE);
		size += 1;
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_DPCD_AKE_NO_STORED_KM_RCVD;
		break;
	case XHDCP2X_RX_DPCD_AKE_STORED_KM_RCVD:
		size = xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
		E_KH_KM_OFFSET, buf, E_KH_KM_SIZE + M_SIZE);

		xhdcp2x_rx->msg_buffer[0] = XHDCP2X_RX_MSG_ID_AKESTOREDKM;
		memcpy(&xhdcp2x_rx->msg_buffer[1], buf,	E_KH_KM_SIZE + M_SIZE);
		size += 1;
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_DPCD_AKE_STORED_KM_RCVD;
		break;
	case XHDCP2X_RX_DPCD_LC_INIT_RCVD:
		size = xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
		R_N_OFFSET, buf, R_N_SIZE);

		xhdcp2x_rx->msg_buffer[0] = XHDCP2X_RX_MSG_ID_LCINIT;
		memcpy(&xhdcp2x_rx->msg_buffer[1], buf,	R_N_SIZE);
		size += 1;
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_DPCD_LC_INIT_RCVD;
		break;
	case XHDCP2X_RX_DPCD_SKE_SEND_EKS_RCVD:
		size = xhdcp2x_rx->handlers.rd_handler(xhdcp2x_rx->interface_ref,
		E_DKEY_KS_OFFSET, buf, E_DKEY_KS_SIZE + R_IV_SIZE);

		xhdcp2x_rx->msg_buffer[0] = XHDCP2X_RX_MSG_ID_SKESENDEKS;
		memcpy(&xhdcp2x_rx->msg_buffer[1], buf,	E_DKEY_KS_SIZE + R_IV_SIZE);
		size += 1;
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_DPCD_SKE_SEND_EKS_RCVD;
		break;
	case XHDCP2X_RX_TIMER_EVENT:
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_TIMER_EVENT;
		break;
	default:
		break;
	}

	return size;
}

static int xhdcp2x_rx_write_dpcd_msg(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	union xhdcp2x_rx_message buffer;
	int bytes_written = 0, status = 0;

	memcpy(&buffer, xhdcp2x_rx->msg_buffer, sizeof(union xhdcp2x_rx_message));

	switch (buffer.msgid) {
	case XHDCP2X_RX_MSG_ID_AKESENDCERT:
		bytes_written = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
								CERT_RX_OFFSET,
								buffer.ake_send_cert.certrx,
								CERT_RX_SIZE + R_RX_SIZE +
								RX_CAPS_SIZE);
		if (bytes_written != CERT_RX_SIZE + R_RX_SIZE + RX_CAPS_SIZE)
			status = -EINVAL;
		break;
	case XHDCP2X_RX_MSG_ID_AKESENDHPRIME:
		bytes_written = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
								H_PRIME_OFFSET,
								buffer.ake_send_hprime.hprime,
								H_PRIME_SIZE);
		if (bytes_written != H_PRIME_SIZE)
			status = -EINVAL;
		break;
	case XHDCP2X_RX_MSG_ID_AKESENDPAIRINGINFO:
		bytes_written = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
								E_KH_KM_PAIRING_OFFSET,
								buffer.ake_send_hprime.hprime,
								E_KH_KM_PAIRING_SIZE);
		if (bytes_written != E_KH_KM_PAIRING_SIZE)
			status = -EINVAL;
		break;
	case XHDCP2X_RX_MSG_ID_LCSENDLPRIME:
		bytes_written = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
								L_PRIME_OFFSET,
								buffer.lc_send_lprime.lprime,
								L_PRIME_SIZE);
		if (bytes_written != L_PRIME_SIZE)
			status = -EINVAL;
		break;
	default:
		status = -EINVAL;
		break;
	}

	return status;
}

/*
 * this function will become common function for both DP and HDMI interface
 * for polling DPCD and DDC registers
 */
static int xhdcp2x_rx_poll_message(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	u32 size = 0;

	if (xhdcp2x_rx->info.msg_event != XHDCP2X_RX_DPCD_FLAG_NONE)
		size = xhdcp2x_rx_read_dpcd_msg(xhdcp2x_rx);

	return size;
}

static int xhdcp2x_rx_process_message_ake_init(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx->curr_state = XHDCP2X_STATE_B0;
	xhdcp2x_rx->prev_state = XHDCP2X_STATE_B0;
	xhdcp2x_rx->info.msg_event = 0;

	xhdcp2x_rx->info.auth_request_cnt++;

	xlnx_hdcp2x_cipher_disable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);
	xlnx_hdcp2x_cipher_enable(xhdcp2x_rx->xhdcp2x_hw.cipher_inst.cipher_coreaddress);

	xlnx_hdcp_tmrcntr_reset(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_0);
	xhdcp2x_rx->info.timer_expired = 0;
	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1);

	xhdcp2x_rx_reset_params(xhdcp2x_rx);

	memcpy(xhdcp2x_rx->param.rtx, msgptr->ake_init.rtx, XHDCP2X_RX_RTX_SIZE);
	memcpy(xhdcp2x_rx->param.txcaps, msgptr->ake_init.txcaps, XHDCP2X_RX_TXCAPS_SIZE);

	if (!xhdcp2x_rx->info.authentication_status) {
		if (xhdcp2x_rx->handlers.notify_handler)
			xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
							    XHDCP2X_RX_NOTIFY_UN_AUTHENTICATED);
	}

	if (xhdcp2x_rx->info.authentication_status == XHDCP2X_RX_REAUTH_REQUESTED) {
		if (xhdcp2x_rx->handlers.notify_handler)
			xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
							    XHDCP2X_RX_NOTIFY_RE_AUTHENTICATE);
	}

	return 0;
}

static int xhdcp2x_rx_process_message_ake_nostoredkm(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	u32 size;
	int status = 0;

	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	status = xhdcp2x_rx_rsaes_oaep_decrypt(xhdcp2x_rx,
					       (struct xhdcp2x_rx_kpriv_rx *)
					       xhdcp2x_rx->privatekeyptr,
					       msgptr->ake_no_storedkm.ekpubkm,
					       xhdcp2x_rx->param.km,
					       &size);

	return (!status && size == XHDCP2X_RX_KM_SIZE) ? 0 : 1;
}

static int xhdcp2x_rx_process_message_ake_storedkm(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int status = 0;

	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx_compute_ekh(xhdcp2x_rx->privatekeyptr,
			       msgptr->ake_storedkm.ekhkm, msgptr->ake_storedkm.m,
			       xhdcp2x_rx->param.km);

	return status;
}

static int xhdcp2x_rx_process_message_lcinit(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx->info.lc_init_attempts++;

	memcpy(xhdcp2x_rx->param.rn, msgptr->lc_init.rn, XHDCP2X_RX_RN_SIZE);

	return 0;
}

static int xhdcp2x_rx_process_message_ske_send_eks(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx_compute_ks(xhdcp2x_rx->param.rrx,
			      xhdcp2x_rx->param.rtx, xhdcp2x_rx->param.km,
			      xhdcp2x_rx->param.rn, msgptr->ske_sendeks.edkeyks,
			      xhdcp2x_rx->param.ks);

	memcpy(xhdcp2x_rx->param.riv, msgptr->ske_sendeks.riv, XHDCP2X_RX_RIV_SIZE);

	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst,
				    xhdcp2x_rx->param.ks, XHDCP2X_CIPHER_REG_KS_1_OFFSET,
				    XHDCP2X_RX_KS_SIZE);
	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_rx->xhdcp2x_hw.cipher_inst,
				    xhdcp2x_rx->param.riv, XHDCP2X_CIPHER_REG_RIV_1_OFFSET,
				    XHDCP2X_RX_RIV_SIZE);

	if (xhdcp2x_rx->handlers.notify_handler)
		xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
						    XHDCP2X_RX_NOTIFY_SKE_SEND_EKS);

	return 0;
}

static u8 xhdcp2x_rx_is_read_message_complete(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		if (xhdcp2x_rx->info.sub_state ==
			XHDCP2X_RX_STATE_B1_SEND_AKESENDPAIRINGINFO ||
			xhdcp2x_rx->info.sub_state ==
				XHDCP2X_RX_STATE_B1_WAIT_LCINIT) {
			if (xhdcp2x_rx->info.msg_event &
				XHDCP2X_RX_DPCD_HPRIME_READ_DONE_RCVD) {
				xhdcp2x_rx->info.msg_event &=
				~XHDCP2X_RX_DPCD_HPRIME_READ_DONE_RCVD;
				return 1;
			}
			return 0;
		}
		return 1;
	}

	return 0;
}

static int xhdcp2x_rx_send_message_ake_send_cert(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	int status = 0;
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	msgptr->ake_send_cert.msgid = XHDCP2X_RX_MSG_ID_AKESENDCERT;
	memcpy(msgptr->ake_send_cert.rxcaps, xhdcp2x_rx->rx_caps, XHDCP2X_RX_RXCAPS_SIZE);

	xhdcp2x_rx_generate_random(xhdcp2x_rx, XHDCP2X_RX_RRX_SIZE, msgptr->ake_send_cert.rrx);
	memcpy(msgptr->ake_send_cert.certrx, xhdcp2x_rx->publiccertptr, XHDCP2X_RX_CERT_SIZE);

	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		status = xhdcp2x_rx_write_dpcd_msg(xhdcp2x_rx);
		if (status < 0)
			return -EINVAL;
	}

	memcpy(xhdcp2x_rx->param.rrx, msgptr->ake_send_cert.rrx, XHDCP2X_RX_RRX_SIZE);
	memcpy(xhdcp2x_rx->param.rxcaps, msgptr->ake_send_cert.rxcaps, XHDCP2X_RX_RXCAPS_SIZE);

	return status;
}

static int xhdcp2x_rx_send_message_ake_send_pairing_info(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	u8 m[XHDCP2X_RX_RTX_SIZE + XHDCP2X_RX_RRX_SIZE];
	u8 ekhkm[XHDCP2X_RX_EKH_SIZE];
	u8 rxstatus = RX_STATUS_PAIRING_AVAILABLE;
	int status = 0;

	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	memcpy(m, xhdcp2x_rx->param.rtx, XHDCP2X_RX_RTX_SIZE);
	memcpy(m + XHDCP2X_RX_RTX_SIZE, xhdcp2x_rx->param.rrx, XHDCP2X_RX_RRX_SIZE);

	xhdcp2x_rx_compute_ekh(xhdcp2x_rx->privatekeyptr, xhdcp2x_rx->param.km, m, ekhkm);

	msgptr->ake_send_pairinginfo.msgid = XHDCP2X_RX_MSG_ID_AKESENDPAIRINGINFO;
	memcpy(msgptr->ake_send_pairinginfo.ekhkm, ekhkm, XHDCP2X_RX_EKH_SIZE);

	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		status = xhdcp2x_rx_write_dpcd_msg(xhdcp2x_rx);
		if (status < 0)
			return -EINVAL;

		status = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
							 RX_STATUS_OFFSET, &rxstatus, 1);
		if (status < 0)
			return -EINVAL;

		xhdcp2x_rx->handlers.cp_irq_handler(xhdcp2x_rx->interface_ref);
	}

	memcpy(xhdcp2x_rx->param.ekh, ekhkm, XHDCP2X_RX_EKH_SIZE);

	return status;
}

static int xhdcp2x_rx_send_message_ake_send_hprime(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	u8 rxstatus = RX_STATUS_H_PRIME_AVAILABLE;
	int status = 0;

	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx_compute_hprime(xhdcp2x_rx->param.rrx, xhdcp2x_rx->param.rxcaps,
				  xhdcp2x_rx->param.rtx, xhdcp2x_rx->param.txcaps,
				  xhdcp2x_rx->param.km,
				  msgptr->ake_send_hprime.hprime);

	msgptr->ake_send_hprime.msgid = XHDCP2X_RX_MSG_ID_AKESENDHPRIME;

	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		status = xhdcp2x_rx_write_dpcd_msg(xhdcp2x_rx);
		if (status < 0)
			return -EINVAL;

		status = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
							 RX_STATUS_OFFSET, &rxstatus, 1);
		if (status < 0)
			return -EINVAL;

		xhdcp2x_rx->handlers.cp_irq_handler(xhdcp2x_rx->interface_ref);
	}

	memcpy(xhdcp2x_rx->param.hprime, msgptr->ake_send_hprime.hprime, XHDCP2X_RX_HPRIME_SIZE);

	return status;
}

static void xhdcp2x_rx_start_timer(struct xlnx_hdcp2x_config *xhdcp2x_rx, u32 timeout_msec
		, u8 reason_id)
{
	u32 ticks = (u32)(xhdcp2x_rx->tmr_config.hw_config.sys_clock_freq /
			XHDCP2X_CLK_DIV) * timeout_msec * XHDCP2X_CLK_MUL;

	xhdcp2x_rx->info.timer_expired = 0;
	xhdcp2x_rx->info.timer_reason_id = reason_id;
	xhdcp2x_rx->info.timer_initial_ticks = ticks;

	xlnx_hdcp_tmrcntr_set_reset_value(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1, ticks);
	xlnx_hdcp_tmrcntr_start(&xhdcp2x_rx->tmr_config, XHDCP2X_RX_TMR_CNTR_1);
}

static int xhdcp2x_rx_send_message_lc_send_lprime(struct xlnx_hdcp2x_config *xhdcp2x_rx)
{
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;
	int status = 0;

	xhdcp2x_rx_compute_lprime(xhdcp2x_rx->param.rn,
				  xhdcp2x_rx->param.km, xhdcp2x_rx->param.rrx,
				  xhdcp2x_rx->param.rtx, msgptr->lc_send_lprime.lprime);

	msgptr->lc_send_lprime.msgid = XHDCP2X_RX_MSG_ID_LCSENDLPRIME;

	if (xhdcp2x_rx->protocol == XHDCP2X_RX_DP) {
		status = xhdcp2x_rx_write_dpcd_msg(xhdcp2x_rx);
		if (status < 0)
			return -EINVAL;
	}

	memcpy(xhdcp2x_rx->param.lprime, msgptr->lc_send_lprime.lprime, XHDCP2X_RX_LPRIME_SIZE);

	return status;
}

static enum xhdcp2x_rx_state xhdcp2x_state_B0(void *instance)
{
	int status = 0;

	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)instance;
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_UNAUTHENTICATED;

	if (xhdcp2x_rx->info.error_flag) {
		xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
		return XHDCP2X_STATE_B0;
	}

	xhdcp2x_rx->msg_size = xhdcp2x_rx_poll_message(xhdcp2x_rx);
	if (xhdcp2x_rx->msg_size > 0) {
		switch (msgptr->msgid) {
		case XHDCP2X_RX_MSG_ID_AKEINIT:
			if (xhdcp2x_rx->keys_loaded) {
				status = xhdcp2x_rx_process_message_ake_init(xhdcp2x_rx);
				if (!status)
					return XHDCP2X_STATE_B1;
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		default:
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		}
	}

	return XHDCP2X_STATE_B0;
}

static enum xhdcp2x_rx_state xhdcp2x_state_B1(void *instance)
{
	int status = 0;
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)instance;
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_AUTHENTICATION_BUSY;

	if (xhdcp2x_rx->info.error_flag) {
		xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
		return XHDCP2X_STATE_B0;
	}
	xhdcp2x_rx->msg_size = xhdcp2x_rx_poll_message(xhdcp2x_rx);
	if (xhdcp2x_rx->msg_size > 0) {
		switch (msgptr->msgid) {
		case XHDCP2X_RX_MSG_ID_AKEINIT:
			if (xhdcp2x_rx->keys_loaded) {
				status = xhdcp2x_rx_process_message_ake_init(xhdcp2x_rx);
				if (!status) {
					xhdcp2x_rx->info.sub_state =
					XHDCP2X_RX_STATE_B1_SEND_AKESENDCERT;
					break;
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		case XHDCP2X_RX_MSG_ID_AKENOSTOREDKM:
			if (xhdcp2x_rx->info.sub_state == XHDCP2X_RX_STATE_B1_WAIT_AKEKM) {
				status = xhdcp2x_rx_process_message_ake_nostoredkm(xhdcp2x_rx);
				if (!status) {
					xhdcp2x_rx->info.is_no_storedkm = 1;
					xhdcp2x_rx->info.sub_state =
					XHDCP2X_RX_STATE_B1_SEND_AKESENDHPRIME;
					break;
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		case XHDCP2X_RX_MSG_ID_AKESTOREDKM:
			if (xhdcp2x_rx->info.sub_state == XHDCP2X_RX_STATE_B1_WAIT_AKEKM) {
				status = xhdcp2x_rx_process_message_ake_storedkm(xhdcp2x_rx);
				if (!status) {
					xhdcp2x_rx->info.is_no_storedkm = 0;
					xhdcp2x_rx->info.sub_state =
					XHDCP2X_RX_STATE_B1_SEND_AKESENDHPRIME;
					break;
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		default:
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		}
	}
	switch (xhdcp2x_rx->info.sub_state) {
	case XHDCP2X_RX_STATE_B1_SEND_AKESENDCERT:
		if (xhdcp2x_rx_is_read_message_complete(xhdcp2x_rx)) {
			if (xhdcp2x_rx->keys_loaded) {
				status = xhdcp2x_rx_send_message_ake_send_cert(xhdcp2x_rx);
				if (status < 0)
					return -EINVAL;
				xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B1_WAIT_AKEKM;
			}
		}
		break;
	case XHDCP2X_RX_STATE_B1_SEND_AKESENDHPRIME:
		if (xhdcp2x_rx_is_read_message_complete(xhdcp2x_rx)) {
			status = xhdcp2x_rx_send_message_ake_send_hprime(xhdcp2x_rx);
			if (status < 0)
				return -EINVAL;
			if (xhdcp2x_rx->info.is_no_storedkm) {
				xhdcp2x_rx->info.sub_state =
				XHDCP2X_RX_STATE_B1_SEND_AKESENDPAIRINGINFO;
			} else {
				xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B1_WAIT_LCINIT;
				return XHDCP2X_STATE_B2;
			}
		}
		break;
	case XHDCP2X_RX_STATE_B1_SEND_AKESENDPAIRINGINFO:
		if (xhdcp2x_rx_is_read_message_complete(xhdcp2x_rx)) {
			status = xhdcp2x_rx_send_message_ake_send_pairing_info(xhdcp2x_rx);
			if (status < 0)
				return -EINVAL;
			xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B1_WAIT_LCINIT;
			return XHDCP2X_STATE_B2;
		}
		break;
	default:
		break;
	}

	return XHDCP2X_STATE_B1;
}

static enum xhdcp2x_rx_state xhdcp2x_state_B2(void *instance)
{
	int status = 0;

	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)instance;
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_AUTHENTICATION_BUSY;

	if (xhdcp2x_rx->info.error_flag) {
		xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
		return XHDCP2X_STATE_B0;
	}

	xhdcp2x_rx_is_read_message_complete(xhdcp2x_rx);

	xhdcp2x_rx->msg_size = xhdcp2x_rx_poll_message(xhdcp2x_rx);

	if (xhdcp2x_rx->msg_size > 0) {
		switch (msgptr->msgid) {
		case XHDCP2X_RX_MSG_ID_AKEINIT:
			if (xhdcp2x_rx->keys_loaded) {
				status = xhdcp2x_rx_process_message_ake_init(xhdcp2x_rx);
				if (!status) {
					xhdcp2x_rx->info.sub_state =
						XHDCP2X_RX_STATE_B1_SEND_AKESENDCERT;
				return XHDCP2X_STATE_B1;
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		case XHDCP2X_RX_MSG_ID_LCINIT:
			if (xhdcp2x_rx->info.sub_state ==
					XHDCP2X_RX_STATE_B1_WAIT_LCINIT ||
				xhdcp2x_rx->info.sub_state ==
					XHDCP2X_RX_STATE_B2_WAIT_SKESENDEKS) {
				if (xhdcp2x_rx->info.lc_init_attempts <= XHDCP2X_RX_MAX_LCINIT) {
					status = xhdcp2x_rx_process_message_lcinit(xhdcp2x_rx);
					if (!status) {
						xhdcp2x_rx->info.sub_state =
						XHDCP2X_RX_STATE_B2_SEND_LCSENDLPRIME;
						break;
					}
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		case XHDCP2X_RX_MSG_ID_SKESENDEKS:
			if (xhdcp2x_rx->info.sub_state == XHDCP2X_RX_STATE_B2_WAIT_SKESENDEKS) {
				xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B3_COMPUTE_KS;
				return XHDCP2X_STATE_B3;
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		default:
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		}
	}

	switch (xhdcp2x_rx->info.sub_state) {
	case XHDCP2X_RX_STATE_B2_SEND_LCSENDLPRIME:
		if (xhdcp2x_rx_is_read_message_complete(xhdcp2x_rx)) {
			status = xhdcp2x_rx_send_message_lc_send_lprime(xhdcp2x_rx);
			if (status < 0)
				return -EINVAL;

		xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B2_WAIT_SKESENDEKS;
		}
		break;
	default:
		break;
	}

	return XHDCP2X_STATE_B2;
}

static enum xhdcp2x_rx_state xhdcp2x_state_B3(void *instance)
{
	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)instance;

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_AUTHENTICATION_BUSY;

	if (xhdcp2x_rx->info.error_flag) {
		xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
		return XHDCP2X_STATE_B0;
	}
	xhdcp2x_rx_process_message_ske_send_eks(xhdcp2x_rx);

	if (xhdcp2x_rx->mode == xhdcp2x_rx_receiver) {
		xhdcp2x_rx->info.sub_state = XHDCP2X_RX_STATE_B4_AUTHENTICATED;
		return XHDCP2X_STATE_B4;
	}

	return XHDCP2X_STATE_B3;
}

static enum xhdcp2x_rx_state xhdcp2x_state_B4(void *instance)
{
	int status = 0;
	u8 rxstatus = 0;

	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)instance;
	union xhdcp2x_rx_message *msgptr = (union xhdcp2x_rx_message *)xhdcp2x_rx->msg_buffer;

	status = xhdcp2x_rx->handlers.wr_handler(xhdcp2x_rx->interface_ref,
						 RX_STATUS_OFFSET, &rxstatus, 1);
	if (status < 0)
		return XHDCP2X_STATE_B0;

	if (xhdcp2x_rx->curr_state != xhdcp2x_rx->prev_state) {
		if (xhdcp2x_rx->handlers.notify_handler)
			xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
							    XHDCP2X_RX_NOTIFY_AUTHENTICATED);

		xhdcp2x_rx_start_timer(xhdcp2x_rx,
				       XHDCP2X_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	if (xhdcp2x_rx->info.timer_expired) {
		xhdcp2x_rx->info.msg_event &= ~XHDCP2X_RX_TIMER_EVENT;
		status =
		xlnx_hdcp2x_cipher_is_encrypted(xhdcp2x_rx
						->xhdcp2x_hw.cipher_inst.cipher_coreaddress);
		if (xhdcp2x_rx->info.is_encrypted != status) {
			if (xhdcp2x_rx->handlers.notify_handler)
				xhdcp2x_rx->handlers.notify_handler(xhdcp2x_rx->interface_ref,
						       XHDCP2X_RX_NOTIFY_ENCRYPTION_DONE);
		}
		xhdcp2x_rx->info.is_encrypted = status;
		xhdcp2x_rx_start_timer(xhdcp2x_rx, XHDCP2X_RX_ENCRYPTION_STATUS_INTERVAL, 0);
	}

	xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_AUTHENTICATED;

	if (xhdcp2x_rx->info.error_flag) {
		xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
		return XHDCP2X_STATE_B0;
	} else if (xhdcp2x_rx->info.error_flag & XHDCP2X_RX_ERROR_FLAG_LINK_INTEGRITY) {
		status = xhdcp2x_rx_set_reauth_req(xhdcp2x_rx);
		if (status < 0)
			return XHDCP2X_STATE_B0;

		xhdcp2x_rx->info.authentication_status = XHDCP2X_RX_REAUTH_REQUESTED;
	}
	xhdcp2x_rx->msg_size = xhdcp2x_rx_poll_message(xhdcp2x_rx);

	if (xhdcp2x_rx->msg_size > 0) {
		switch (msgptr->msgid) {
		case XHDCP2X_RX_MSG_ID_AKEINIT:
			if (xhdcp2x_rx->keys_loaded) {
				status = xhdcp2x_rx_process_message_ake_init(xhdcp2x_rx);
				if (!status) {
					xhdcp2x_rx->info.sub_state =
						XHDCP2X_RX_STATE_B1_SEND_AKESENDCERT;
					return XHDCP2X_STATE_B1;
				}
			}
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		default:
			xhdcp2x_rx_reset_after_error(xhdcp2x_rx);
			return XHDCP2X_STATE_B0;
		}
	}

	return XHDCP2X_STATE_B4;
}
