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
#include "xlnx_hdcp1x_tx.h"
#include "xhdcp1x_tx.h"

/**
 * xlnx_hdcp1x_tx_enble: This function enables the cipher block for An,Aksv generation.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true means sucessfull set or othervalue
 */
static bool xlnx_hdcp1x_tx_enble(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	xhdcp1x_tx->is_cipher = xhdcp1x_cipher_enable(xhdcp1x_tx->cipher);
	if (!(xhdcp1x_tx->is_cipher))
		return -EINVAL;

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

/**
 * xlnx_hdcp1x_downstream_capbility: This function queries the downstream device to check
 * if the downstream device is HDCP capable.
 * @xhdcp1x_tx: It points to the HDCP1x config structure
 *
 * @return: true indicates HDCP capable or not (false)
 */
bool xlnx_hdcp1x_downstream_capbility(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u8 rxcaps = 0;

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
				       u8 offset, void *buf, u32 buf_size)
{
	if ((buf_size + offset) > XHDCP1X_BUF_OFFSET_LEN)
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
	u8 value = 0;

	/* Check For Repeater */
	if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
					    XHDCP1X_PORT_OFFSET_BCAPS,
					    &value, XHDCP1X_REMOTE_INFO_BCAPS_VAL))
		return ((value & XHDCP1X_PORT_BIT_BCAPS_REPEATER));
	return 0;
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

	xlnx_hdcp1x_read_bksv_from_remote(xhdcp1x_tx, XHDCP1X_PORT_OFFSET_BKSV,
					  buf, XHDCP1X_REMOTE_BKSV_SIZE);
	remoteksv = xlnx_hdcp1x_buf_to_unit(buf, XHDCP1X_PORT_SIZE_BKSV * XHDCP1X_BYTE_IN_BITS);
	/* Check the is KSV valid & revocation list from application data*/
	if (!(xlnx_hdcp1x_is_ksvvalid(remoteksv))) {
		dev_dbg(xhdcp1x_tx->dev, "Invalid bksv");
		goto invalid_revoked_bksv; /* Invalid bksv */
	} else {
		u64 localksv = XHDCP1X_DEFAULT_INIT, an = XHDCP1X_DEFAULT_INIT;
		u8 buf_ainfo[XHDCP1X_PORT_SIZE_AINFO];

		/* Check for repeater */
		if (xlnx_hdcp1x_tx_test_for_repeater(xhdcp1x_tx)) {
			xhdcp1x_tx->is_repeater = XHDCP1X_ENABLE;
			goto invalid_revoked_bksv;
		} else {
			xhdcp1x_tx->is_repeater = XHDCP1X_DEFAULT_INIT;
		}
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
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref,
						XHDCP1X_PORT_OFFSET_AINFO,
						buf_ainfo, XHDCP1X_PORT_SIZE_AINFO);

		xlnx_hdcp1x_uint_to_buf(buf, an, XHDCP1X_PORT_SIZE_AN * XHDCP1X_BYTE_IN_BITS);
		/* Send an to Remote */
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref, XHDCP1X_PORT_OFFSET_AN,
						buf, XHDCP1X_PORT_SIZE_AN);
		/* Send Aksv to remote */
		xlnx_hdcp1x_uint_to_buf(buf, localksv,
					XHDCP1X_PORT_SIZE_AKSV * XHDCP1X_BYTE_IN_BITS);
		xhdcp1x_tx->handlers.wr_handler(xhdcp1x_tx->interface_ref, XHDCP1X_PORT_OFFSET_AKSV,
						buf, XHDCP1X_PORT_SIZE_AKSV);

		return true;
	}
invalid_revoked_bksv:
	return false;

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

static u16 xlnx_hdcp1x_buf_to_unit16(u8 *buf, u32 size)
{
	int byte;
	u16 remoteksv = XHDCP1X_DEFAULT_INIT;

	if ((size) > 0) {
		for (byte = (((size) - 1) >> 3); byte >= 0; byte--) {
			remoteksv <<= XHDCP1X_BYTE_IN_BITS;
			remoteksv  |= buf[byte];
		}
	}
	return remoteksv;
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
	u32 num_tries = XHDCP1X_MAX_RETRIES;
	/* 100ms delay: The HDCP transmitter must allow the HDCP receiver at least
	 * 100ms to make ro' available from the time Aksv is written. added based on
	 * DP HDCP sepecification: HDCP%20on%20DisplayPort%20Specification%20Rev1_1.pdf
	 */
	msleep(XHDCP1X_RO_AVILABLE_DELAY);
	do {
		if (xhdcp1x_tx->handlers.rd_handler(xhdcp1x_tx->interface_ref,
						    XHDCP1X_PORT_OFFSET_RO,
						    buf, XHDCP1X_REMOTE_RO_SIZE)) {
			u16 remotero = 0, localro = 0;

			/* Determine Remote Ro */
			remotero = xlnx_hdcp1x_buf_to_unit16(buf,
							     (XHDCP1X_REMOTE_RO_SIZE *
							      XHDCP1X_BYTE_IN_BITS));
			/* Determine the Local Ro */
			xhdcp1x_cipher_get_ro(xhdcp1x_tx->cipher, &localro);
			/* Compre the Ro == Ro' */
		if (localro == remotero)
			return true;

		if (num_tries == XHDCP1X_ENABLE)
			xhdcp1x_tx->stats.auth_failed++;
		else
			xhdcp1x_tx->stats.read_failure++;
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
		an = xhdcp1x_cipher_get_mi(xhdcp1x_tx);
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
	if (!xhdcp1x_tx->is_enabled) {
		dev_dbg(xhdcp1x_tx->dev, "Hdcp is not started");
		return -EINVAL;
	}
	xhdcp1x_tx->auth_status = XHDCP1X_TX_UNAUTHENTICATED;

	xhdcp1x_tx->curr_state = A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE;
	xhdcp1x_tx->prev_state = A0_HDCP1X_TX_STATE_DETERMINE_RX_CAPABLE;
	xhdcp1x_tx->state_helper = XHDCP1X_DEFAULT_INIT;
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
