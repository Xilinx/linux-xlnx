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

#include <linux/xlnx/xilinx-hdcp1x-cipher.h>
#include <linux/xlnx/xlnx_timer.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include "xlnx_hdcp_tx.h"
#include "xlnx_hdcp_sha1.h"
#include "xlnx_hdcp1x_tx.h"
#include "xhdcp1x_tx.h"

#define XHDCP1X_WRITE_CHUNK_SZ	8
#define XHDCP1X_WRITE_ADDRESS_OFFSET 0x100

/**
 * xlnx_hdcp1x_tx_enble: This function enables the cipher block for An,Aksv generation.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true means sucessfull set or othervalue
 */
static bool xlnx_hdcp1x_tx_enble(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	xhdcp1x_tx->is_enabled = XHDCP1X_ENABLE;
	xhdcp1x_tx->is_cipher = xhdcp1x_cipher_enable(xhdcp1x_tx->cipher);

	if (xhdcp1x_tx->is_cipher)
		return -EINVAL;

	xlnx_hdcp_tmrcntr_stop(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr,
			       XTC_TIMER_0);

	return true;
}

/**
 * xlnx_hdcp1x_tx_start_authenticate: This function sets the initial states for HDCP state machine
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true means sucessfull set or othervalue
 */
static bool xlnx_hdcp1x_tx_start_authenticate(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	if (!(xhdcp1x_tx->is_enabled))
		return -EINVAL;

	xhdcp1x_tx->auth_status = XHDCP1X_TX_AUTHENTICATION_BUSY;
	xhdcp1x_tx->curr_state = H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
	xhdcp1x_tx->prev_state = H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;
	return true;
}

/**
 * xlnx_hdcp1x_tx_process_ri_event: This function provides an indication
 * whether RI updation is done in hardware or not
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: none
 */
void xlnx_hdcp1x_tx_process_ri_event(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	xhdcp1x_tx->is_riupdate = true;
}

/**
 * xlnx_start_hdcp1x_engine: This function calls necessary functions for HDCP state machine.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: nothing
 */
void xlnx_start_hdcp1x_engine(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	xlnx_hdcp1x_tx_enble(xhdcp1x_tx);
	xlnx_hdcp1x_tx_start_authenticate(xhdcp1x_tx);
}

/**
 * xlnx_hdcp1x_tx_init: This function initilizes cipher and set defaults.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 * @is_repeater: the downstream is repeater or monitor.
 *
 * @return: true means init sucessfull or flase/other value
 */
bool xlnx_hdcp1x_tx_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, bool is_repeater)
{
	/* Default Configuration */
	xhdcp1x_tx->pending_events = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->curr_state = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->prev_state = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->is_encryption_en = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->encryption_map = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->is_enabled = XHDCP1X_ENABLE;
	if (is_repeater) {
		dev_info(xhdcp1x_tx->dev, "Hdcp1x Repeater Functionality is not supported\n");
		return false;
	}
	/* initialize the Cipher core */
	xhdcp1x_tx->cipher = xhdcp1x_cipher_init(xhdcp1x_tx->dev,
						 xhdcp1x_tx->interface_base);
	if (IS_ERR(xhdcp1x_tx->cipher))
		return -EINVAL;

	return true;
}

/**
 * xlnx_hdcp1x_task_monitor: This function monitors the HDCP states.
 * @xhdcp1x_tx: reference to the HDCP config structure
 *
 * @return: it return the HDCP state
 */
int xlnx_hdcp1x_task_monitor(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	enum hdcp1x_tx_state new_state;

	new_state = hdcp1x_tx_protocol_authenticate_sm(xhdcp1x_tx);
	xhdcp1x_tx->prev_state = xhdcp1x_tx->curr_state;
	xhdcp1x_tx->curr_state = new_state;

	return xhdcp1x_tx->stats.auth_status;
}

static int xlnx_hdcp1x_tx_writedata(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				    u8 offset,
				    const void *buf, u32 buf_size)
{
	u8 slave = DRM_HDCP_DDC_ADDR;
	u8 tx_buf[XHDCP1X_WRITE_CHUNK_SZ + 1];
	int num_written = 0;
	u32 this_time = 0;
	const u8 *write_buf = buf;

	if ((buf_size + offset) > XHDCP1X_WRITE_ADDRESS_OFFSET)
		buf_size = (XHDCP1X_WRITE_ADDRESS_OFFSET - offset);
	do {
		this_time = XHDCP1X_WRITE_CHUNK_SZ;
		if (this_time > buf_size)
			this_time = buf_size;
		tx_buf[0] = offset;
		memcpy(&tx_buf[1], write_buf, this_time);
		if (xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
						    slave,
						    tx_buf,
						    (this_time + 1)) < 0) {
			num_written = -1;
			break;
		}
		num_written += this_time;
		write_buf += this_time;
		buf_size -= this_time;
	} while ((buf_size != 0) && (num_written > 0));

	return num_written;
}

/**
 * xlnx_hdcp1x_downstream_capbility: This function queries the downstream device to check
 * if the downstream device is HDCP capable.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates HDCP capable or not (false)
 */
bool xlnx_hdcp1x_downstream_capbility(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 value[XHDMI_HDCP1X_PORT_SIZE_BSTATUS] = {0};
	u8 rxcaps = 0;

	if (xhdcp1x_tx->protocol == XHDCP1X_TX_HDMI) {
		xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						XHDMI_HDCP1X_PORT_OFFSET_BCAPS,
						value, 2);
		if (value[0] & 0x80) {
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_BSTATUS,
							value, XHDMI_HDCP1X_PORT_SIZE_BSTATUS);
			return 1;
		}

		return 0;
	}
	xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
					XHDCP1X_PORT_OFFSET_BCAPS,
					(void *)&rxcaps,
					XHDCP1X_REMOTE_INFO_BCAPS_VAL);

	return (rxcaps & XHDCP1X_PORT_BIT_BCAPS_HDCP_CAPABLE);
}

/**
 * xlnx_hdcp1x_tx_check_rxcapable: This function ensure that remote end is HDCP capable.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates HDCP capable remote end or not (false)
 */
bool xlnx_hdcp1x_tx_check_rxcapable(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 value = 0;

	xlnx_hdcp1x_tx_disable_encryption(xhdcp1x_tx, XHDCP1X_STREAM_MAP);
	xhdcp1x_tx->is_encryption_en = XHDCP1X_DEFAULT_INIT;

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		if ((xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						     XHDMI_HDCP1X_PORT_OFFSET_BCAPS,
						     &value,
						     XHDMI_HDCP1X_PORT_SIZE_BCAPS)) > 0) {
			if ((value & 0x80)) {
				xlnx_hdcp_tmrcntr_stop(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr,
						       XTC_TIMER_0);
				return true;
			}
		}
	}
	/* Check the Rx HDCP Capable or Not */
	if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
					    XHDCP1X_PORT_OFFSET_BCAPS,
					    &value, XHDCP1X_REMOTE_INFO_BCAPS_VAL)) {
		if (value & XHDCP1X_PORT_BIT_BCAPS_HDCP_CAPABLE)
			return true;
	}
	dev_dbg(xhdcp1x_tx->dev, "HDCP1x RX Not Capable");

	return false;
}

/**
 * xlnx_hdcp1x_read_bksv_from_remote: This function reads the bksv from the remote.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 * @offset: the remote HDCP bksv offset
 * @buf: the remote bksv value stored in buffer
 * @buf_size: the size of the bskv
 *
 * @return: true indicates read sucessfull or not (false)
 */
bool xlnx_hdcp1x_read_bksv_from_remote(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				       u32 offset, void *buf, u32 buf_size)
{
	if ((buf_size + (offset - XDPTX_HDCP1X_DPCD_OFFSET)) > XHDCP1X_BUF_OFFSET_LEN)
		buf_size = (XHDCP1X_BUF_OFFSET_LEN - offset);

	xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
					offset,
					buf, buf_size);

	return true;
}

static void xlnx_hdcp1x_uint_to_buf(u8 *buf, u64 resval, u32 size)
{
	int byte;

	if ((size) > 0) {
		for (byte = 0; byte <= (int)(((size) - 1) >> 3); byte++) {
			buf[byte] = (uint8_t)(resval & 0xFFu);
			resval >>= XHDCP1X_BYTE_IN_BITS;
		}
	}
}

static u64 xlnx_hdcp1x_buf_to_unit(u8 *buf, u32 size)
{
	u64 remoteksv = 0;
	int byte;

	if ((size) > 0) {
		for (byte = (((size) - 1) >> 3); byte >= 0; byte--) {
			remoteksv <<= XHDCP1X_BYTE_IN_BITS;
			remoteksv  |= buf[byte];
		}
	}
	return remoteksv;
}

/**
 * xlnx_hdcp1x_tx_test_for_repeater: This function checks the remote end to see if its a repeater.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates sucessfull or not (false)
 */
int xlnx_hdcp1x_tx_test_for_repeater(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 value = 0, ret = 0;

	/* Check For Repeater */
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		ret = xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						      XHDMI_HDCP1X_PORT_OFFSET_BCAPS,
						      &value,
						      XHDMI_HDCP1X_PORT_SIZE_BCAPS);
		if (ret != XHDMI_HDCP1X_PORT_SIZE_BCAPS)
			return false;
		if (value & XHDMI_HDCP1X_PORT_BIT_BCAPS_REPEATER) {
			xhdcp1x_tx->is_repeater = 1;
			return true;
		}

	} else {
		ret = xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						      XHDCP1X_PORT_OFFSET_BCAPS,
						      &value,
						      XHDMI_HDCP1X_PORT_SIZE_BCAPS);
		if (ret != XHDCP1X_PORT_SIZE_BCAPS)
			return false;
		if (value & XHDCP1X_PORT_BIT_BCAPS_REPEATER) {
			xhdcp1x_tx->is_repeater = 1;
			return true;
		}
	}

	return false;
}

/**
 * xlnx_hdcp1x_set_keys: This function loads the key to key management block
 * @xhdcp1x_tx: It points to the HDCP1x config structre
 * @data: Key information received from sysfs
 *
 * @return: return 1 indicates success or 0 for failure
 */

int xlnx_hdcp1x_set_keys(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data)
{
	int ret = 0;

	ret = xlnx_hdcp1x_keymngt_init(xhdcp1x_tx, data);
	return ret;
}

/**
 * xlnx_hdcp1x_exchangeksvs: This function exchanges the ksvs between the two ends of the link.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates keys exchange sucessfull or not (false)
 */
bool xlnx_hdcp1x_exchangeksvs(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	/* Reading the Downstream Capabilities */
	u8 buf[XHDCP1X_BYTE_IN_BITS] = {XHDCP1X_DEFAULT_INIT};
	u64 remoteksv = XHDCP1X_DEFAULT_INIT;
	u64 localksv = XHDCP1X_DEFAULT_INIT, an = XHDCP1X_DEFAULT_INIT;
	u8 buf_ainfo[XHDCP1X_PORT_SIZE_AINFO];

	xlnx_hdcp1x_read_bksv_from_remote(xhdcp1x_tx, XHDCP1X_PORT_OFFSET_BKSV,
					  buf, XHDCP1X_REMOTE_BKSV_SIZE);
	remoteksv = xlnx_hdcp1x_buf_to_unit(buf,
					    XHDCP1X_PORT_SIZE_BKSV * XHDCP1X_BYTE_IN_BITS);

	/* Check the is KSV valid & revocation list from application data*/
	if (!(xlnx_hdcp1x_is_ksvvalid(remoteksv))) {
		dev_dbg(xhdcp1x_tx->dev, "Invalid bksv");
		return false;
	}
	xhdcp1x_tx->tmr_cnt = 0;
	/* Check for repeater */
	if (xlnx_hdcp1x_tx_test_for_repeater(xhdcp1x_tx))
		xhdcp1x_tx->is_repeater = 1;
	else
		xhdcp1x_tx->is_repeater = 0;

	/* Generate the an */
	an = xlnx_hdcp1x_tx_generate_an(xhdcp1x_tx);

	/* Save the an in statehelp for later use */
	xhdcp1x_tx->state_helper = an;
	/* Determine the Local KSV */
	localksv = xhdcp1x_cipher_get_localksv(xhdcp1x_tx->cipher);
	/* Load the cipher with the remote ksv */
	xhdcp1x_cipher_set_remoteksv(xhdcp1x_tx->cipher,
				     remoteksv);
	/* Clear AINFO */
	memset(buf_ainfo, XHDCP1X_DEFAULT_INIT, XHDCP1X_PORT_SIZE_AINFO);
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
		xlnx_hdcp1x_tx_writedata(xhdcp1x_tx,
					 XHDMI_HDCP1X_PORT_OFFSET_AINFO,
					 buf_ainfo, XHDCP1X_PORT_SIZE_AINFO);
	else
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
						XHDCP1X_PORT_OFFSET_AINFO,
						buf_ainfo, XHDCP1X_PORT_SIZE_AINFO);

	xlnx_hdcp1x_uint_to_buf(buf, an, XHDCP1X_PORT_SIZE_AN * XHDCP1X_BYTE_IN_BITS);
	/* Send an to Remote */
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
		xlnx_hdcp1x_tx_writedata(xhdcp1x_tx,
					 XHDMI_HDCP1X_PORT_OFFSET_AN,
					 buf, XHDCP1X_PORT_SIZE_AN);
	else
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
						XHDCP1X_PORT_OFFSET_AN,
						buf, XHDCP1X_PORT_SIZE_AN);
	/* Send Aksv to remote */
	xlnx_hdcp1x_uint_to_buf(buf, localksv,
				XHDCP1X_PORT_SIZE_AKSV * XHDCP1X_BYTE_IN_BITS);
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
		xlnx_hdcp1x_tx_writedata(xhdcp1x_tx,
					 XHDMI_HDCP1X_PORT_OFFSET_AKSV,
					 buf, XHDCP1X_PORT_SIZE_AKSV);
	else
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
						XHDCP1X_PORT_OFFSET_AKSV,
						buf, XHDCP1X_PORT_SIZE_AKSV);
	return true;
}

/**
 * xlnx_hdcp1x_computationsstate: This function initiates the computations for a state machine
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates comutations done (true) or not (false)
 */
bool xlnx_hdcp1x_computationsstate(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 X = 0, Y = 0, Z = 0, cipher_req = XHDCP1X_DEFAULT_INIT;
	u64 value = XHDCP1X_DEFAULT_INIT;

	/* Update value with an */
	value = xhdcp1x_tx->state_helper;
	/* Load the cipher B registers with an */
	X = (u32)(value & 0x0FFFFFFFul);
	value >>= 28;
	Y = (u32)(value & 0x0FFFFFFFul);
	value >>= 28;
	Z = (u32)(value & 0x000000FFul);
	if (xhdcp1x_tx->is_repeater)
		Z |= (1ul << XHDCP1X_BYTE_IN_BITS);

	xhdcp1x_cipher_setb(xhdcp1x_tx->cipher, X, Y, Z);
	/* Initiate the block cipher */
	xhdcp1x_cipher_do_request(xhdcp1x_tx->cipher,
				  XHDCP1X_CIPHER_REQUEST_BLOCK);
	cipher_req = xhdcp1x_cipher_is_request_complete(xhdcp1x_tx->cipher);

	if (cipher_req != 1) {
		dev_dbg(xhdcp1x_tx->dev, "CipherDoRequest Computations not done");
		return false;
	}

	return true;
}

static u16 xlnx_hdcp1x_buf_to_uint16(u8 *buf, u32 size)
{
	int byte = 0;
	u16 buff_to_uint = XHDCP1X_DEFAULT_INIT;

	if ((size) > 0) {
		for (byte = (((size) - 1) >> 3); byte >= 0; byte--) {
			buff_to_uint <<= XHDCP1X_BYTE_IN_BITS;
			buff_to_uint  |= buf[byte];
		}
	}
	return buff_to_uint;
}

/**
 * xlnx_hdcp1x_tx_validaterxstate: This function validates the attached receiver
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates key matched (true) or not (false)
 */
bool xlnx_hdcp1x_tx_validaterxstate(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 buf[XHDCP1X_REMOTE_RO_SIZE];
	int ret = 0;
	u32 num_tries = XHDCP1X_MAX_RETRIES;
	/* 100ms delay: The HDCP transmitter must allow the HDCP receiver at least
	 * 100ms to make ro' available from the time Aksv is written. added based on
	 * DP HDCP sepecification: HDCP%20on%20DisplayPort%20Specification%20Rev1_1.pdf
	 */
	msleep(XHDCP1X_RO_AVILABLE_DELAY);
	do {
		if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
			ret = xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							      XHDMI_HDCP1X_PORT_OFFSET_RO,
							      buf,
							      XHDMI_HDCP1X_PORT_SIZE_RO);
		} else {
			ret = xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDCP1X_PORT_OFFSET_RO,
						    buf, 2);
		}
		if (ret > 0) {
			u16 remotero = 0;
			u16 localro = 0;

			/* Determine Remote Ro */
			remotero = xlnx_hdcp1x_buf_to_uint16(buf,
							     (XHDCP1X_REMOTE_RO_SIZE *
							     XHDCP1X_BYTE_IN_BITS));
			/* Determine the Local Ro */
			xhdcp1x_cipher_get_ro(xhdcp1x_tx->cipher, &localro);
			/* Compare the Ro == Ro' */
			if (localro == remotero)
				return true;
		}
		num_tries--;
	} while (num_tries > 0);

	return false;
}

/**
 * xlnx_hdcp1x_is_ksvvalid: This function validates a KSV value as
 * having 20 1's and 20 0's
 * @ksv: ksv is the value to validate
 *
 * @return: true value indicates valid (true) or not (false)
 */
int xlnx_hdcp1x_is_ksvvalid(u64 ksv)
{
	u32 is_valid = false, num_ones = 0;

	/* Determine num_ones */
	while (ksv) {
		if ((ksv & 1) != 0)
			num_ones++;

		ksv >>= 1;
	}

	/* Check for 20 1's */
	if (num_ones == XHDCP1X_KSV_NUM_OF_1S)
		is_valid = true;

	return is_valid;
}

/**
 * xlnx_hdcp1x_tx_generate_an: This function generates the an from a random number generator
 * @xhdcp1x_tx:	Instanceptr is the HDCP1x config structure
 *
 * @return: A 64-bit pseudo random number (an).
 */
u64 xlnx_hdcp1x_tx_generate_an(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u64 an = 0;
	/* Attempt to generate an */

	if (xhdcp1x_cipher_do_request(xhdcp1x_tx->cipher, 2) == 0) {
		/* Wait until done */
		while (!xhdcp1x_cipher_is_request_complete(xhdcp1x_tx->cipher)) {
			/* Waiting for cipher request completion
			 * before generating the An,
			 */
		}
		an = xhdcp1x_cipher_get_mi(xhdcp1x_tx->cipher);
	}

	/* Check if zero */
	if (!an)
		an = 0x351F7175406A74Dull;

	return an;
}

/**
 * xhdcp1x_tx_set_keyselect: Select the aksv key
 * @xhdcp1x_tx: reference to HDCP1x config structure
 * @keyselect: ket selection from a group of input keys
 *
 * @return: 0 on success, error otherwise
 */
int xhdcp1x_tx_set_keyselect(struct xlnx_hdcp1x_config *xhdcp1x_tx,
			     u8 keyselect)
{
	if (!xhdcp1x_tx)
		return -EINVAL;

	return xhdcp1x_cipher_set_keyselect(xhdcp1x_tx->cipher, keyselect);
}

/**
 * xhdcp1x_tx_load_aksv: loads the local ksv to hdcp port
 * @xhdcp1x_tx: reference to HDCP1X instance
 *
 * @return: 0 on success, error otherwise
 */
int xhdcp1x_tx_load_aksv(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 buf[XHDCP1X_PORT_SIZE_AKSV] = {0};

	if (!xhdcp1x_tx)
		return -EINVAL;

	if (xhdcp1x_cipher_load_aksv(xhdcp1x_tx->cipher, buf))
		return -EAGAIN;
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
		xlnx_hdcp1x_tx_writedata(xhdcp1x_tx,
					 XHDMI_HDCP1X_PORT_OFFSET_AKSV,
					 buf, XHDCP1X_PORT_SIZE_AKSV);
	else
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
					XHDCP1X_PORT_OFFSET_AKSV,
					buf, XHDCP1X_PORT_SIZE_AKSV);
	return 0;
}

/**
 * xlnx_hdcp1x_tx_disable: This function disables the HDCP functionality
 * @xhdcp1x_tx:	Instanceptr is the HDCP config structure
 *
 * @return:	no return vale
 */
void xlnx_hdcp1x_tx_disable(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	xhdcp1x_tx->is_enabled = XHDCP1X_DISABLE;
	xhdcp1x_cipher_disable(xhdcp1x_tx->cipher);
}

/**
 * xlnx_hdcp1x_tx_reset: This function resets the HDCP functionality
 * @xhdcp1x_tx:	Instanceptr is the HDCP config structure
 *
 * @return:	flase/other values
 */
int xlnx_hdcp1x_tx_reset(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	if (!(xhdcp1x_tx->is_enabled)) {
		dev_dbg(xhdcp1x_tx->dev, "Hdcp is not started");
		return -EINVAL;
	}

	xhdcp1x_tx->auth_status = XHDCP1X_TX_UNAUTHENTICATED;

	xhdcp1x_tx->curr_state = A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE;
	xhdcp1x_tx->prev_state = A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE;
	xhdcp1x_tx->state_helper = XHDCP1X_DEFAULT_INIT;
	xhdcp1x_tx->tmr_cnt = 0;
	xhdcp1x_tx->is_riupdate = 0;
	xhdcp1x_tx->is_encryption_en = XHDCP1X_DEFAULT_INIT;

	xlnx_hdcp1x_tx_disable_encryption(xhdcp1x_tx, xhdcp1x_tx->encryption_map);
	xhdcp1x_tx->encryption_map = XHDCP1X_DEFAULT_INIT;
	xlnx_hdcp1x_tx_disable(xhdcp1x_tx);

	return 0;
}

/**
 * xlnx_hdcp1x_tx_enable_encryption: This function enables the encryption
 * for a HDCP state machine.
 * @xhdcp1x_tx: points to the HDCP config structure
 *
 * @return: no return value
 */
void xlnx_hdcp1x_tx_enable_encryption(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u64 stream_map = XHDCP1X_STREAM_MAP;

	if (!(xhdcp1x_tx->is_encryption_en)) {
		xhdcp1x_tx->encryption_map |= stream_map;
		/* Check for encryption enabled */
		if (xhdcp1x_tx->encryption_map) {
			stream_map = XHDCP1X_DEFAULT_INIT;
			/* Determine stream_map */
			stream_map = xhdcp1x_cipher_getencryption(xhdcp1x_tx->cipher);

			/* Check if there is something to do */
			if (stream_map != xhdcp1x_tx->encryption_map) {
				/* Enable it */
				xhdcp1x_cipher_enable_encryption(xhdcp1x_tx->cipher,
								 xhdcp1x_tx->encryption_map);
			}
		}
		xhdcp1x_tx->is_encryption_en = XHDCP1X_ENCRYPTION_EN;
	}
}

/**
 * xlnx_hdcp1x_tx_disable_encryption: This function resets the HDCP functionality
 * @xhdcp1x_tx:	Instanceptr is the HDCP config structure
 * @stream_map: Bit map of the streams to disable encryption.
 *
 * @return: flase/other values
 */
void xlnx_hdcp1x_tx_disable_encryption(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				       u64 stream_map)
{
	u32 status = 0;

	status = xhdcp1x_cipher_disableencryption(xhdcp1x_tx->cipher, stream_map);
	if (!status)
		xhdcp1x_tx->encryption_map &= ~stream_map;
}

/**
 * xlnx_hdcp1x_check_link_integrity: This function checks the link
 * integrity of HDCP link.
 * @xhdcp1x_tx:	Instanceptr is the HDCP config structure
 *
 * @return: 1 if link integrity is passed, error otherwise
 */

int xlnx_hdcp1x_check_link_integrity(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 buf[2];
	int num_tries = XHDCP1X_MAX_RETRIES, ri_check_status = 0;

	xhdcp1x_tx->is_riupdate = 0;
	do {
		if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDMI_HDCP1X_PORT_OFFSET_RO,
						    buf,
						    XHDMI_HDCP1X_PORT_SIZE_RO)) {
			u16 remote_ri = 0, local_ri = 0;

			remote_ri =
				xlnx_hdcp1x_buf_to_uint16(buf,
							  XHDMI_HDCP1X_PORT_SIZE_RO
							  * XHDCP1X_BYTE_IN_BITS);

			xhdcp1x_cipher_get_ri(xhdcp1x_tx->cipher, &local_ri);
			if (local_ri != remote_ri) {
				dev_dbg(xhdcp1x_tx->dev, "Ri checking failed\n");
				ri_check_status = 0;
			} else {
				ri_check_status = 1;
				dev_dbg(xhdcp1x_tx->dev, "Ri checking passed\n");
			}
		} else {
			dev_err(xhdcp1x_tx->dev, "Ri reading failed\n");
		}
		num_tries--;
	} while ((ri_check_status == 0) && (num_tries > 0));
	return ri_check_status;
}

/**
 * xhdcp1x_tx_set_check_linkstate: This function enables/disables the link
 * integrity, RI checking of HDCP link.
 * @xhdcp1x_tx: Instanceptr is the HDCP config structure
 * @is_enabled: Flag to enable/disable RI calculation
 */

void xhdcp1x_tx_set_check_linkstate(struct xlnx_hdcp1x_config *xhdcp1x_tx, int is_enabled)
{
	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		if (is_enabled)
			xhdcp1x_cipher_set_ri_update(xhdcp1x_tx->cipher, true);
		else
			xhdcp1x_cipher_set_ri_update(xhdcp1x_tx->cipher, false);
	}
}

int xlnx_hdcp1x_get_repeater_info(struct xlnx_hdcp1x_config *xhdcp1x_tx, u16 *info)
{
	u8 value = 0;

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDMI_HDCP1X_PORT_OFFSET_BCAPS,
						    (void *)&value,
						    XHDMI_HDCP1X_PORT_SIZE_BCAPS) > 0) {
			u8 ready_mask = 0;

			ready_mask  = XHDMI_HDCP1X_PORT_BIT_BCAPS_REPEATER;
			ready_mask |= XHDMI_HDCP1X_PORT_BIT_BCAPS_READY;
			if ((value & ready_mask) == ready_mask) {
				u8 buf[XHDMI_HDCP1X_PORT_SIZE_BSTATUS];
				u64 converted_value;

				xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
								XHDMI_HDCP1X_PORT_OFFSET_BSTATUS,
								buf,
								XHDMI_HDCP1X_PORT_SIZE_BSTATUS);
				converted_value =
					xlnx_hdcp1x_buf_to_unit(buf,
								BITS_PER_BYTE * sizeof(u16));
				*info = (converted_value & XHDMI_HDCP1X_PORT_BINFO_VALUE);
				return 1;
			}
		}
	} else {
		if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDCP1X_PORT_OFFSET_BCAPS,
						    (void *)&value, 1) > 0) {
			if ((value & XHDCP1X_PORT_BIT_BCAPS_REPEATER) != 0) {
				xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
								XHDCP1X_PORT_OFFSET_BSTATUS,
								&value, 1);
				if ((value & XHDCP1X_PORT_BIT_BSTATUS_READY) != 0) {
					u8 buf[XHDMI_HDCP1X_PORT_SIZE_BSTATUS];
					u16 binfo = 0;

					xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
									XHDCP1X_PORT_OFFSET_BINFO,
									buf,
									XHDCP1X_PORT_SIZE_BINFO);
					binfo = xlnx_hdcp1x_buf_to_unit(buf,
									BITS_PER_BYTE *
									sizeof(u16));
					*info = (binfo & XHDCP1X_PORT_BINFO_VALUE);

					return 1;
				}
			}
		}
	}

	return 0;
}

int xlnx_hdcp1x_gettopology_maxcascadeexceeded(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 value = 0;

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						XHDMI_HDCP1X_PORT_OFFSET_BSTATUS,
						(void *)&value,
						XHDMI_HDCP1X_PORT_SIZE_BSTATUS);
		return ((value & XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEPTH_ERR) ? 1 : 0);
	}

	return ((value & XHDCP1X_PORT_BINFO_BIT_DEPTH_ERR) ? 1 : 0);
}

int xlnx_hdcp1x_gettopology_maxdevsexceeded(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 value = 0;

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						XHDMI_HDCP1X_PORT_OFFSET_BSTATUS,
						(void *)&value,
						XHDMI_HDCP1X_PORT_SIZE_BSTATUS);
		return ((value & XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEV_CNT_ERR) ? 1 : 0);
	}

	return ((value & XHDCP1X_PORT_BINFO_BIT_DEV_CNT_ERR) ? 1 : 0);
}

enum hdcp1x_tx_state xlnx_hdcp1x_tx_wait_for_ready(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u16 repeater_info = 0;

	if (!xhdcp1x_tx->xhdcp1x_internal_timer.timer_expired)
		return A6_HDCP1X_TX_STATE_WAIT_FOR_READY;

	xhdcp1x_tx->tmr_cnt++;
	if (xhdcp1x_tx->tmr_cnt > XHDMI_HDCP1X_READY_TIMEOUT)
		return  H0_HDCP1X_TX_STATE_DISABLED_NO_RX_ATTACHED;

	if (!xlnx_hdcp1x_get_repeater_info(xhdcp1x_tx, &repeater_info)) {
		xlnx_hdcp1x_tx_start_timer(xhdcp1x_tx, XHDMI_HDCP1X_WAIT_FOR_READY_TIMEOUT, 0);

		return A6_HDCP1X_TX_STATE_WAIT_FOR_READY;
	}
	xhdcp1x_tx->state_helper = repeater_info;
	xhdcp1x_tx->tmr_cnt = 0;

	return A7_HDCP1X_TX_STATE_READ_KSV_LIST;
}

static u64 xlnx_hdcp1x_buf_to_u64(u8 *buf, u64 size)
{
	u64 buff_to_int = 0;

	if ((size) > 0) {
		int byte;

		for (byte = (((size) - 1) >> 3); byte >= 0; byte--) {
			buff_to_int <<= XHDCP1X_BYTE_IN_BITS;
			buff_to_int  |= buf[byte];
		}
	}

	return buff_to_int;
}

int xlnx_hdcp1x_tx_validate_ksv_list(struct xlnx_hdcp1x_config *xhdcp1x_tx, u16 repeater_info)
{
	struct xlnx_sha1_context sha1_context;
	u8 buf[DRM_HDCP_KSV_LEN * XHDCP1X_PORT_SIZE_BKSV];
	u8 ksv_list_holder[XHDMI_HDCP1X_PORT_MAX_DEV_CNT * XHDCP1X_PORT_SIZE_BKSV];
	int num_to_read = 0;
	int ksv_count = 0, byte_count = 0;
	int ret = 0;
	unsigned int ksv_list_size = 0;
	u64 value = 0, buf_read_ksv_count = 0, mo = 0, remote_ksv;
	u8 sha_result[SHA1_HASH_SIZE];
	u8 bksv[XHDCP1X_PORT_SIZE_BKSV];

	memset(ksv_list_holder, 0, (XHDMI_HDCP1X_PORT_MAX_DEV_CNT * XHDCP1X_PORT_SIZE_BKSV));

	memset(buf, 0, DRM_HDCP_KSV_LEN * XHDCP1X_PORT_SIZE_BKSV);

	xlnx_sha1_reset(&sha1_context);

	repeater_info = xhdcp1x_tx->state_helper;
	num_to_read = ((repeater_info & XHDMI_HDCP1X_PORT_MAX_DEV_CNT) * DRM_HDCP_KSV_LEN);

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
		if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDMI_HDCP1X_OFFSET_KSVFIFO,
						    ksv_list_holder, num_to_read)) {
			xlnx_sha1_input(&sha1_context, ksv_list_holder, num_to_read);

			while (byte_count < num_to_read) {
				if ((byte_count + 1) % DRM_HDCP_KSV_LEN == 0) {
					value = xlnx_hdcp1x_buf_to_unit((ksv_list_holder
									+ ((((byte_count + 1)
									/ XHDCP1X_PORT_SIZE_BKSV)
									- 1)
									* XHDCP1X_PORT_SIZE_BKSV)),
									XHDCP1X_PORT_SIZE_BKSV *
									BITS_PER_BYTE);

					if (!xlnx_hdcp1x_is_ksvvalid(value))
						return false;

					xhdcp1x_tx->repeatervalues.ksvlist[ksv_count++] = value;
					value = 0;
				}
				byte_count++;
			}
		}
	} else {
		unsigned int ksv_list_byte_count = 0;

		byte_count = (num_to_read / DRM_HDCP_KSV_LEN);
		ksv_list_size = byte_count;
		do {
			int total_bytes = XHDCP1X_PORT_SIZE_KSVFIFO;
			int min_bytes_to_read = XHDCP1X_PORT_MIN_BYTES;

			if (total_bytes > num_to_read)
				total_bytes = num_to_read;

			if (min_bytes_to_read > byte_count)
				min_bytes_to_read = byte_count;

			ret = xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							      XHDCP1X_PORT_OFFSET_KSVFIFO,
							      buf, total_bytes);
			if (!ret)
				return false;

			xlnx_sha1_input(&sha1_context, buf, total_bytes);
			while (buf_read_ksv_count < total_bytes) {
				ksv_list_holder[ksv_list_byte_count++] =
				buf[buf_read_ksv_count];
				buf_read_ksv_count++;
			}
			num_to_read -= total_bytes;
			byte_count -= min_bytes_to_read;

		} while (num_to_read > 0);
	}

	/* Insert repeater_info into the SHA-1 transform */
	buf[0] = (u8)repeater_info;

	if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
		buf[1] = (u8)(repeater_info
		>> XHDMI_HDCP1X_PORT_BSTATUS_DEPTH_SHIFT);
	else
		buf[1] = (u8)(repeater_info >> XHDCP1X_PORT_BINFO_DEPTH_SHIFT);

	xlnx_sha1_input(&sha1_context, buf, 2);

	/* Insert the mo into the SHA-1 transform */
	mo = xhdcp1x_cipher_get_mo(xhdcp1x_tx->cipher);
	xlnx_hdcp1x_uint_to_buf(buf, mo, XHDCP1X_BYTE_IN_BITS * BITS_PER_BYTE);

	xlnx_sha1_input(&sha1_context, buf, BITS_PER_BYTE);

	/* Finalize the SHA-1 result and confirm success */
	if (xlnx_sha1_result(&sha1_context, sha_result) == XLNX_SHA_SUCCESS) {
		u64 offset = 0;
		const u8 *sha1_buf = sha_result;
		int num_iterations = (SHA1_HASH_SIZE >> 2);

		if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
			offset = XHDMI_HDCP1X_PORT_OFFSET_VH0;
		else
			offset = XHDCP1X_PORT_OFFSET_VH0;

		do {
			u32 calc_calue = 0;
			u32 read_value = 0;

			/* Determine calc_calue */
			calc_calue = *sha1_buf++;
			calc_calue <<= BITS_PER_BYTE;
			calc_calue |= *sha1_buf++;
			calc_calue <<= BITS_PER_BYTE;
			calc_calue |= *sha1_buf++;
			calc_calue <<= BITS_PER_BYTE;
			calc_calue |= *sha1_buf++;

			if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
				offset = XHDMI_HDCP1X_PORT_OFFSET_VH0;

			else
				offset = XHDCP1X_PORT_OFFSET_VH0;

			ret =
				xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
								offset, buf,
								XHDCP1X_PORT_SIZE_VH0);

			if (!ret) {
				dev_err(xhdcp1x_tx->dev, "Unable to read V");
				return false;
			}
			memcpy(&xhdcp1x_tx->repeatervalues.v[offset - XHDMI_HDCP1X_PORT_OFFSET_VH0],
			       buf, XHDCP1X_PORT_SIZE_VH0);
			read_value = xlnx_hdcp1x_buf_to_unit(buf,
							     BITS_PER_BYTE * XHDCP1X_PORT_SIZE_VH0);
			if (calc_calue != read_value) {
				dev_err(xhdcp1x_tx->dev, "V` Ksv's miss match");
				return false;
			}
			offset += XHDCP1X_PORT_SIZE_VH0;
			num_iterations--;
		} while (num_iterations > 0);
	} else {
		dev_err(xhdcp1x_tx->dev, "SHA mismatch Occurred");
		return false;
	}
	if (xhdcp1x_tx->is_repeater) {
		if (xhdcp1x_tx->protocol == XHDCP1X_TX_DP) {
			u64 val = 0;
			u32 this_ksv = 0;

			while (this_ksv < ksv_list_size) {
				val = xlnx_hdcp1x_buf_to_u64((ksv_list_holder +
							       (this_ksv *
							       XHDCP1X_PORT_SIZE_BKSV)),
							       XHDCP1X_PORT_SIZE_BKSV *
							       BITS_PER_BYTE);
				if (!(val)) {
					this_ksv++;
					continue;
				}
				xhdcp1x_tx->repeatervalues.ksvlist[ksv_count++] = val;

				val = 0;
				this_ksv++;
			}
		}

		memset(bksv, 0, XHDCP1X_PORT_SIZE_BKSV);
		if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_BKSV,
							bksv,
							XHDMI_HDCP1X_PORT_SIZE_BKSV);
		else
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDCP1X_PORT_OFFSET_BKSV,
							bksv, XHDCP1X_PORT_SIZE_BKSV);

		/* Determine theremote_ksv */
		remote_ksv = xlnx_hdcp1x_buf_to_unit(bksv,
						     XHDCP1X_PORT_SIZE_BKSV * BITS_PER_BYTE);
		/* Check for invalid */
		if (!xlnx_hdcp1x_is_ksvvalid(remote_ksv)) {
			dev_dbg(xhdcp1x_tx->dev, "Invalid Bksv reads");
			return 0;
		}
		xhdcp1x_tx->repeatervalues.ksvlist[ksv_count] = remote_ksv;
	}
	return true;
}

int xlnx_hdcp1x_setrepeaterinfo(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 bksv[BITS_PER_BYTE];
	u32 ksv_count = 0, buf;

	if (xhdcp1x_tx->is_repeater) {
		if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP) {
			/* Set the SHA1 Hash value */
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_VH0,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_VH0);

			/* V'H0 */
			xhdcp1x_tx->repeatervalues.v[0] = (u16)buf;

			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_VH1,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_VH1);

			/* V'H1 */
			xhdcp1x_tx->repeatervalues.v[1] = (u16)buf;

			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_VH2,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_VH2);

			/* V'H2 */
			xhdcp1x_tx->repeatervalues.v[2] = (u16)buf;

			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_VH3,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_VH3);

			/* V'H3 */
			xhdcp1x_tx->repeatervalues.v[3] = (u16)buf;

			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_VH4,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_VH4);

			/* V'H4 */
			xhdcp1x_tx->repeatervalues.v[4] = (u16)buf;
			/* Copy the Depth read from the downstream HDCP device */
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_BSTATUS,
							(void *)&buf,
							XHDMI_HDCP1X_PORT_SIZE_BSTATUS);

			xhdcp1x_tx->repeatervalues.depth = ((buf &
							     XHDCP1X_PORT_BINFO_DEPTH_MASK) >>
							     BITS_PER_BYTE);
			xhdcp1x_tx->repeatervalues.device_count = (buf &
								  XHDCP1X_PORT_BINFO_DEV_CNT_MASK);
			xhdcp1x_tx->repeatervalues.device_count++;
		} else {
			u16 repeater_info;

			repeater_info = (u16)xhdcp1x_tx->state_helper;

			xhdcp1x_tx->repeatervalues.depth = ((repeater_info &
							    XHDCP1X_PORT_BINFO_DEPTH_MASK) >>
							    BITS_PER_BYTE);
			xhdcp1x_tx->repeatervalues.device_count = repeater_info &
								  XHDCP1X_PORT_BINFO_DEV_CNT_MASK;
			xhdcp1x_tx->repeatervalues.device_count++;
		}
	} else {
		u64 remote_ksv = 0;

		xhdcp1x_tx->repeatervalues.depth = 0;

		xhdcp1x_tx->repeatervalues.device_count = 1;

		if (xhdcp1x_tx->protocol != XHDCP1X_TX_DP)
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDMI_HDCP1X_PORT_OFFSET_BKSV,
							bksv,
							XHDMI_HDCP1X_PORT_SIZE_BKSV);
		else
			xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
							XHDCP1X_PORT_OFFSET_BKSV,
							bksv, XHDCP1X_PORT_SIZE_BKSV);

		remote_ksv = xlnx_hdcp1x_buf_to_uint16(bksv,
						       XHDCP1X_PORT_SIZE_BKSV * BITS_PER_BYTE);

		if (!xlnx_hdcp1x_is_ksvvalid(remote_ksv)) {
			dev_dbg(xhdcp1x_tx->dev, "bksv invalid");
			return 0;
		}
		xhdcp1x_tx->repeatervalues.ksvlist[ksv_count++] = remote_ksv;
	}

	return 1;
}

int xlnx_hdcp1x_tx_read_ksv_list(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 num_attempts = 3;
	u32 ksv_list_valid = 0;
	u16 repeater_info;
	u8 bksv[BITS_PER_BYTE];

	repeater_info = (xhdcp1x_tx->state_helper & XHDMI_HDCP1X_PORT_BINFO_VALUE);

	if ((!xlnx_hdcp1x_gettopology_maxcascadeexceeded(xhdcp1x_tx)) &&
	    (!xlnx_hdcp1x_gettopology_maxdevsexceeded(xhdcp1x_tx))) {
		dev_dbg(xhdcp1x_tx->dev, "Received Correct topology from Downstream Devices");
	} else {
		u64 remote_ksv = 0;

		dev_dbg(xhdcp1x_tx->dev, "Received Incorrect topology from Downstream Devices");
		xlnx_hdcp1x_tx_disable_encryption(xhdcp1x_tx, xhdcp1x_tx->encryption_map);
		xhdcp1x_tx->repeatervalues.depth =
				((repeater_info & XHDCP1X_PORT_BINFO_DEPTH_MASK) >>
				 XHDMI_HDCP1X_PORT_BSTATUS_DEPTH_SHIFT);
		xhdcp1x_tx->repeatervalues.device_count =
				(repeater_info &
				 XHDMI_HDCP1X_PORT_BSTATUS_DEV_CNT_MASK);

		xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						XHDMI_HDCP1X_PORT_OFFSET_BKSV,
						bksv, XHDCP1X_PORT_SIZE_BKSV);
		remote_ksv = xlnx_hdcp1x_buf_to_uint16(bksv,
						       XHDCP1X_PORT_SIZE_BKSV * BITS_PER_BYTE);

		if (!xlnx_hdcp1x_is_ksvvalid(remote_ksv))
			xhdcp1x_tx->repeatervalues.ksvlist[0] =	remote_ksv;
		else
			xhdcp1x_tx->repeatervalues.ksvlist[0] = 0x0;

		memset(xhdcp1x_tx->repeatervalues.v, 0x0, sizeof(u32) * XHDCP1X_PORT_SIZE_BKSV);

		xhdcp1x_tx->repeatervalues.hdcp14_propagatetopo_errupstream = true;

		if ((repeater_info & 0x800) != 0)
			dev_dbg(xhdcp1x_tx->dev, "Max Cascade Exceeded");
		else
			dev_dbg(xhdcp1x_tx->dev, "Max Devicec Exceeded");

		return 0;
	}

	do {
		ksv_list_valid = xlnx_hdcp1x_tx_validate_ksv_list(xhdcp1x_tx, repeater_info);
		num_attempts--;
	} while ((num_attempts > 0) && (!ksv_list_valid));

	if (ksv_list_valid) {
		if (xhdcp1x_tx->is_repeater)
			xlnx_hdcp1x_setrepeaterinfo(xhdcp1x_tx);
		xhdcp1x_tx->downstreamready = 1;
		return 1;
	}

	return 0;
}

void xlnx_hdcp1x_tx_timer_init(struct xlnx_hdcp1x_config *xhdcp1x_tx,
			       struct xlnx_hdcp_timer_config *tmr_cntrl)
{
	xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr = *tmr_cntrl;

	xlnx_hdcp_tmrcntr_set_options(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr,
				      0,
				      XTC_INT_MODE_OPTION | XTC_DOWN_COUNT_OPTION);
}

void xlnx_hdcp1x_tx_start_timer(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				u32 timeout, u8 reason_id)
{
	u32 ticks = (u32)(xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr.hw_config.sys_clock_freq
					/ XHDCP1X_TX_CLKDIV_MHZ) * timeout * XHDCP1X_TX_CLKDIV_HZ;

	xlnx_hdcp_tmrcntr_stop(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr, 0);

	xhdcp1x_tx->xhdcp1x_internal_timer.timer_expired = (0);
	xhdcp1x_tx->xhdcp1x_internal_timer.reason_id = reason_id;
	xhdcp1x_tx->xhdcp1x_internal_timer.initial_ticks = ticks;

	xlnx_hdcp_tmrcntr_set_reset_value(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr,
					  0, ticks);
	xlnx_hdcp_tmrcntr_start(&xhdcp1x_tx->xhdcp1x_internal_timer.tmr_ctr, 0);
}

void xlnx_hdcp1x_tx_timer_handler(void *callbackref, u8 tmr_cnt_number)
{
	struct xlnx_hdcp1x_config *xhdcp1x_tx =	(struct xlnx_hdcp1x_config *)callbackref;

	if (tmr_cnt_number == XTC_TIMER_1)
		return;

	xhdcp1x_tx->xhdcp1x_internal_timer.timer_expired = 1;
}
