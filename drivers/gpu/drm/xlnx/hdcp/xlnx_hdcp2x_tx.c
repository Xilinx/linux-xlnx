// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Specific HDCP2X driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver configures Xilinx HDCP IP and its internal modules like Cipher and
 * Random Number Generator.
 * It consists of:
 * - Functionality for checking if the HDCP2X Receiver sink does respond within
 *   specified times.
 * - Message handling from/to the HDCP2X receiver sink.
 *
 * Reference:
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 */

#include <linux/io.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xhdcp2x_tx.h"
#include "xlnx_hdcp2x_tx.h"

#define XHDCP2X_CIPHER_OFFSET	0U
#define XHDCP2X_RNG_OFFSET	0x1000U

#define XHDCP2X_SRM_MESSAGE_HEADER_LENGTH	0x05

/* Public transmitter DCP LLC key - n=384 bytes, e=1 byte */

/*
 * Reference:
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20HDMI%20Specification%20Rev2_3.pdf
 * Table B.1
 */
static const u8 hdcp2x_tx_kpubdpc[HDCP2X_TX_KPUB_DCP_LLC_N_SIZE
				 + XHDCP2X_TX_KPUB_DCP_LLC_E_SIZE] = {
	0xB0, 0xE9, 0xAA, 0x45, 0xF1, 0x29, 0xBA, 0x0A,
	0x1C, 0xBE, 0x17, 0x57, 0x28, 0xEB, 0x2B, 0x4E,
	0x8F, 0xD0, 0xC0, 0x6A, 0xAD, 0x79, 0x98, 0x0F,
	0x8D, 0x43, 0x8D, 0x47, 0x04, 0xB8, 0x2B, 0xF4,
	0x15, 0x21, 0x56, 0x19, 0x01, 0x40, 0x01, 0x3B,
	0xD0, 0x91, 0x90, 0x62, 0x9E, 0x89, 0xC2, 0x27,
	0x8E, 0xCF, 0xB6, 0xDB, 0xCE, 0x3F, 0x72, 0x10,
	0x50, 0x93, 0x8C, 0x23, 0x29, 0x83, 0x7B, 0x80,
	0x64, 0xA7, 0x59, 0xE8, 0x61, 0x67, 0x4C, 0xBC,
	0xD8, 0x58, 0xB8, 0xF1, 0xD4, 0xF8, 0x2C, 0x37,
	0x98, 0x16, 0x26, 0x0E, 0x4E, 0xF9, 0x4E, 0xEE,
	0x24, 0xDE, 0xCC, 0xD1, 0x4B, 0x4B, 0xC5, 0x06,
	0x7A, 0xFB, 0x49, 0x65, 0xE6, 0xC0, 0x00, 0x83,
	0x48, 0x1E, 0x8E, 0x42, 0x2A, 0x53, 0xA0, 0xF5,
	0x37, 0x29, 0x2B, 0x5A, 0xF9, 0x73, 0xC5, 0x9A,
	0xA1, 0xB5, 0xB5, 0x74, 0x7C, 0x06, 0xDC, 0x7B,
	0x7C, 0xDC, 0x6C, 0x6E, 0x82, 0x6B, 0x49, 0x88,
	0xD4, 0x1B, 0x25, 0xE0, 0xEE, 0xD1, 0x79, 0xBD,
	0x39, 0x85, 0xFA, 0x4F, 0x25, 0xEC, 0x70, 0x19,
	0x23, 0xC1, 0xB9, 0xA6, 0xD9, 0x7E, 0x3E, 0xDA,
	0x48, 0xA9, 0x58, 0xE3, 0x18, 0x14, 0x1E, 0x9F,
	0x30, 0x7F, 0x4C, 0xA8, 0xAE, 0x53, 0x22, 0x66,
	0x2B, 0xBE, 0x24, 0xCB, 0x47, 0x66, 0xFC, 0x83,
	0xCF, 0x5C, 0x2D, 0x1E, 0x3A, 0xAB, 0xAB, 0x06,
	0xBE, 0x05, 0xAA, 0x1A, 0x9B, 0x2D, 0xB7, 0xA6,
	0x54, 0xF3, 0x63, 0x2B, 0x97, 0xBF, 0x93, 0xBE,
	0xC1, 0xAF, 0x21, 0x39, 0x49, 0x0C, 0xE9, 0x31,
	0x90, 0xCC, 0xC2, 0xBB, 0x3C, 0x02, 0xC4, 0xE2,
	0xBD, 0xBD, 0x2F, 0x84, 0x63, 0x9B, 0xD2, 0xDD,
	0x78, 0x3E, 0x90, 0xC6, 0xC5, 0xAC, 0x16, 0x77,
	0x2E, 0x69, 0x6C, 0x77, 0xFD, 0xED, 0x8A, 0x4D,
	0x6A, 0x8C, 0xA3, 0xA9, 0x25, 0x6C, 0x21, 0xFD,
	0xB2, 0x94, 0x0C, 0x84, 0xAA, 0x07, 0x29, 0x26,
	0x46, 0xF7, 0x9B, 0x3A, 0x19, 0x87, 0xE0, 0x9F,
	0xEB, 0x30, 0xA8, 0xF5, 0x64, 0xEB, 0x07, 0xF1,
	0xE9, 0xDB, 0xF9, 0xAF, 0x2C, 0x8B, 0x69, 0x7E,
	0x2E, 0x67, 0x39, 0x3F, 0xF3, 0xA6, 0xE5, 0xCD,
	0xDA, 0x24, 0x9B, 0xA2, 0x78, 0x72, 0xF0, 0xA2,
	0x27, 0xC3, 0xE0, 0x25, 0xB4, 0xA1, 0x04, 0x6A,
	0x59, 0x80, 0x27, 0xB5, 0xDA, 0xB4, 0xB4, 0x53,
	0x97, 0x3B, 0x28, 0x99, 0xAC, 0xF4, 0x96, 0x27,
	0x0F, 0x7F, 0x30, 0x0C, 0x4A, 0xAF, 0xCB, 0x9E,
	0xD8, 0x71, 0x28, 0x24, 0x3E, 0xBC, 0x35, 0x15,
	0xBE, 0x13, 0xEB, 0xAF, 0x43, 0x01, 0xBD, 0x61,
	0x24, 0x54, 0x34, 0x9F, 0x73, 0x3E, 0xB5, 0x10,
	0x9F, 0xC9, 0xFC, 0x80, 0xE8, 0x4D, 0xE3, 0x32,
	0x96, 0x8F, 0x88, 0x10, 0x23, 0x25, 0xF3, 0xD3,
	0x3E, 0x6E, 0x6D, 0xBB, 0xDC, 0x29, 0x66, 0xEB,
	0x03
};

/*
 * This function is used to load the system renewability messages (SRMs)
 * which carries the Receiver ID revocation list.
 * Reference:
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 * Section 5.1.
 */
static int xlnx_hdcp2x_loadsrm_revocation_table(struct xlnx_hdcp2x_config *xhdcp2x_tx,
						const u8 *srm_input)
{
	struct hdcp2x_tx_revoclist *revocation_list = &xhdcp2x_tx->xhdcp2x_revoc_list;
	const u8 *kpubdpc = NULL;
	const u8 *srmblock = NULL;
	const u8 *rcv_id;
	u32 block_size, length_field;
	u16 num_of_devices;
	int ret, i, j;
	u8 srm_generator, srm_id;

	srmblock = srm_input;

	/* Byte 1 contains the SRM ID and HDCP2 Indicator field */
	srm_id = srmblock[0];

	if (srm_id != HDCP2X_TX_SRM_ID)
		return -EINVAL;

	/* Byte 5 contains the SRM Generation Number */
	srm_generator = srmblock[4];

	/* Get the length of the first-generation SRM in bytes.*/
	length_field  = drm_hdcp_be24_to_cpu(srmblock + XHDCP2X_SRM_MESSAGE_HEADER_LENGTH);

	/* The size of the first-generation SRM block */
	block_size = length_field + XHDCP2X_SRM_MESSAGE_HEADER_LENGTH;

	kpubdpc = hdcp2x_tx_kpubdpc;

	ret = xlnx_hdcp2x_verify_srm(srmblock, block_size, kpubdpc, HDCP2X_TX_KPUB_DCP_LLC_N_SIZE,
				     &kpubdpc[HDCP2X_TX_KPUB_DCP_LLC_N_SIZE],
				     HDCP2X_TX_KPUB_DCP_LLC_E_SIZE);
	if (ret)
		return -EINVAL;

	srmblock += block_size;

	for (i = 1; i < srm_generator; i++) {
		/*
		 * Byte 1-2 contain the length of the next-generation SRM in bytes.
		 * Value is in big endian format, Microblaze is little endian.
		 */
		length_field  = srmblock[0] << BITS_PER_BYTE;
		length_field |= srmblock[1];

		block_size = length_field;

		ret = xlnx_hdcp2x_verify_srm(srmblock, block_size,
					     kpubdpc, HDCP2X_TX_KPUB_DCP_LLC_N_SIZE,
					     &kpubdpc[HDCP2X_TX_KPUB_DCP_LLC_N_SIZE],
					     HDCP2X_TX_KPUB_DCP_LLC_E_SIZE);
		if (ret)
			return -EINVAL;

		srmblock += block_size;
	}

	srmblock = srm_input;

	/* Get the length of the first-generation SRM in bytes.*/
	length_field  = drm_hdcp_be24_to_cpu(srmblock + XHDCP2X_SRM_MESSAGE_HEADER_LENGTH);

	block_size = length_field + XHDCP2X_SRM_MESSAGE_HEADER_LENGTH;

	/*
	 * Byte 9,10 contain the number of devices of the firs-generation SRM block
	 * Value is in big endian format, Microblaze is little endia.
	 */
	num_of_devices = ((srmblock[8] << 2) | DRM_HDCP_2_KSV_COUNT_2_LSBITS(srmblock[9]));
	revocation_list->num_of_devices = 0;

	/* Byte 12 will contain the first byte of the first receiver ID */
	rcv_id = &srmblock[12];

	for (i = 0; i < num_of_devices; i++) {
		if (revocation_list->num_of_devices ==
				HDCP2X_TX_REVOCATION_LIST_MAX_DEVICES)
			return -EINVAL;
		memcpy(revocation_list->rcvid[revocation_list->num_of_devices],
		       rcv_id, XHDCP2X_TX_SRM_RCVID_SIZE);
		revocation_list->num_of_devices++;
		rcv_id += XHDCP2X_TX_SRM_RCVID_SIZE;
	}
	srmblock += block_size;

	for (j = 1; j < srm_generator; j++) {
		/*
		 * Byte 1-2 contain the length of the next-generation SRM in bytes.
		 * Value is in big endian format, Microblaze is little endian.
		 */
		length_field  = srmblock[0] << BITS_PER_BYTE;
		length_field |= srmblock[1];

		block_size = length_field;

		/*
		 * Byte 3,4 contain the number of devices of the next-generation SRM block
		 * Value is in big endian format, Microblaze is little endian.
		 */
		num_of_devices  = (srmblock[2] & DRM_HDCP_2_VRL_LENGTH_SIZE) << BITS_PER_BYTE;
		num_of_devices |=  srmblock[3];

		/* Byte 5 will contain the first byte of the first receiver ID */
		rcv_id = &srmblock[4];

		for (i = 0; i < num_of_devices; i++) {
			if (revocation_list->num_of_devices ==
					HDCP2X_TX_REVOCATION_LIST_MAX_DEVICES)
				return -EINVAL;
			memcpy(revocation_list->rcvid[revocation_list->num_of_devices],
			       rcv_id, XHDCP2X_TX_SRM_RCVID_SIZE);
			revocation_list->num_of_devices++;
			rcv_id += XHDCP2X_TX_SRM_RCVID_SIZE;
		}
		srmblock += block_size;
	}

	xhdcp2x_tx->xhdcp2x_info.is_revoc_list_valid = 1;

	return 0;
}

static bool xlnx_hdcp2x_tx_ds_authenticated(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	return((xhdcp2x_tx->xhdcp2x_info.auth_status ==
			XHDCP2X_TX_AUTHENTICATED) ? 1 : 0);
}

const u8 *xlnx_hdcp2x_tx_get_publickey(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (!xhdcp2x_tx->xhdcp2x_info.is_enabled)
		return NULL;

	return hdcp2x_tx_kpubdpc;
}

int xlnx_hdcp2x_tx_init(struct xlnx_hdcp2x_config *xhdcp2x_tx, bool is_repeater)
{
	/**
	 * This array contains the capabilities of the HDCP2X TX core,
	 * and is transmitted during authentication as part of the AKE_Init message.
	 */
	u8 hdcp2x_txcaps[] = { 0x02, 0x00, 0x00 };

	int ret = 0;

	xhdcp2x_tx->txcaps = (u8 *)hdcp2x_txcaps;
	xhdcp2x_tx->is_hdmi = xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_HDMI ? 1 : 0;

	xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher.cipher_coreaddress =
			xhdcp2x_tx->xhdcp2x_hw.hdcp2xcore_address + XHDCP2X_CIPHER_OFFSET;
	xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng.rng_coreaddress =
			xhdcp2x_tx->xhdcp2x_hw.hdcp2xcore_address + XHDCP2X_RNG_OFFSET;

	ret = xlnx_hdcp2x_rng_cfg_init(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng);
	if (ret < 0)
		return -EINVAL;

	xlnx_hdcp2x_rng_enable(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng);
	xhdcp2x_tx->xhdcp2x_hw.tx_mode = is_repeater;

	ret = xlnx_hdcp2x_cipher_cfg_init(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher);
	if (ret < 0)
		return -EINVAL;

	xhdcp2x_tx->xhdcp2x_info.polling_value = 0;

	memcpy(xhdcp2x_tx->xhdcp2x_info.txcaps, (u8 *)xhdcp2x_tx->txcaps,
	       sizeof(xhdcp2x_tx->xhdcp2x_info.txcaps));

	xlnx_hdcp2x_cipher_init(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher);

	return ret;
}

int xlnx_hdcp2x_loadkeys(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 *srm_key, u8 *lc128_key)
{
	int ret;

	xhdcp2x_tx->srmkey = (u8 *)srm_key;
	xhdcp2x_tx->lc128key = (u8 *)lc128_key;

	xlnx_hdcp2x_cipher_set_keys(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher,
				    xhdcp2x_tx->lc128key, XHDCP2X_CIPHER_REG_LC128_1_OFFSET,
				    XHDCP2X_TX_LC128_SIZE);

	ret = xlnx_hdcp2x_loadsrm_revocation_table(xhdcp2x_tx, xhdcp2x_tx->srmkey);
	if (ret < 0)
		return -EINVAL;

	return ret;
}

u8 xlnx_hdcp2x_tx_is_device_revoked(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				    u8 *rcvid)
{
	struct hdcp2x_tx_revoclist *revoc_list = NULL;
	u32 result, i;

	revoc_list = &xhdcp2x_tx->xhdcp2x_revoc_list;

	for (i = 0; i < revoc_list->num_of_devices; i++) {
		result = memcmp(rcvid, revoc_list->rcvid[i], XHDCP2X_TX_SRM_RCVID_SIZE);
		if (!result)
			return 1;
	}

	return 0;
}

static void xlnx_hdcp2x_tx_enble(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xhdcp2x_tx->xhdcp2x_info.is_enabled = 1;
	xlnx_hdcp2x_cipher_enable(xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher.cipher_coreaddress);
	xlnx_hdcp2x_cipher_set_lanecount(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher,
					 xhdcp2x_tx->lane_count);
	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
			       XHDCP2X_TX_TIMER_CNTR_0);
}

static u8 xlnx_hdcp2x_tx_is_ds_repeater(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	return (xhdcp2x_tx->xhdcp2x_hw.tx_mode != XHDCP2X_TX_TRANSMITTER) ? 1 : 0;
}

static int xlnx_hdcp2x_tx_start_authenticate(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (!xhdcp2x_tx->xhdcp2x_info.is_enabled)
		return -EINVAL;

	xhdcp2x_tx->xhdcp2x_info.is_rcvr_hdcp2x_capable = 0;

	xhdcp2x_tx->xhdcp2x_info.auth_status =
			XHDCP2X_TX_AUTHENTICATION_BUSY;

	xhdcp2x_tx->xhdcp2x_info.curr_state = H0_HDCP2X_TX_NO_RX_ATTACHED;
	xhdcp2x_tx->xhdcp2x_info.prev_state = H0_HDCP2X_TX_NO_RX_ATTACHED;

	if (xlnx_hdcp2x_tx_is_ds_repeater(xhdcp2x_tx))
		xhdcp2x_tx->xhdcp2x_info.is_content_stream_type_set = 0;

	return 0;
}

void xlnx_start_hdcp2x_engine(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xlnx_hdcp2x_tx_enble(xhdcp2x_tx);
	xlnx_hdcp2x_tx_start_authenticate(xhdcp2x_tx);
}

static void xlnx_hdcp2x_tx_disable(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xhdcp2x_tx->xhdcp2x_info.is_enabled = 0;
	xlnx_hdcp2x_cipher_disable(xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher.cipher_coreaddress);
}

int xlnx_hdcp2x_tx_reset(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (!xhdcp2x_tx->xhdcp2x_info.is_enabled) {
		dev_dbg(xhdcp2x_tx->dev, "HDCP is not started");
		return -EINVAL;
	}
	xhdcp2x_tx->xhdcp2x_info.auth_status =	XHDCP2X_TX_UNAUTHENTICATED;

	xhdcp2x_tx->xhdcp2x_info.curr_state = H0_HDCP2X_TX_NO_RX_ATTACHED;
	xhdcp2x_tx->xhdcp2x_info.prev_state = H0_HDCP2X_TX_NO_RX_ATTACHED;
	xhdcp2x_tx->xhdcp2x_info.lc_counter = 0;

	xlnx_hdcp_tmrcntr_stop(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
			       XHDCP2X_TX_TIMER_CNTR_0);
	xlnx_hdcp_tmrcntr_reset(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr, 0);

	xhdcp2x_tx->xhdcp2x_info.content_stream_type = XHDCP2X_STREAMTYPE_0;
	xhdcp2x_tx->xhdcp2x_info.is_content_stream_type_set = 1;
	xhdcp2x_tx->xhdcp2x_revoc_list.num_of_devices = 0;
	xlnx_hdcp2x_tx_disable_encryption(xhdcp2x_tx);

	xlnx_hdcp2x_tx_disable(xhdcp2x_tx);

	return 0;
}

void xlnx_hdcp2x_tx_enable_encryption(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	if (xlnx_hdcp2x_tx_ds_authenticated(xhdcp2x_tx)) {
		xlnx_hdcp2x_tx_cipher_update_encryption(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher,
							1);
		dev_dbg(xhdcp2x_tx->dev, "enable encryption");
	}
}

void xlnx_hdcp2x_tx_disable_encryption(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xlnx_hdcp2x_tx_cipher_update_encryption(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher, 0);
	dev_dbg(xhdcp2x_tx->dev, "disable encryption");
}

bool xlnx_hdcp2x_downstream_capbility(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	u8 rxcaps[HDCP_2_2_RXCAPS_LEN] = {0};
	u8 hdcp2_version;

	if (xhdcp2x_tx->is_hdmi) {
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_VER_OFFSET,
						(void *)&hdcp2_version,
						sizeof(hdcp2_version));

		return (hdcp2_version & HDCP_2_2_HDMI_SUPPORT_MASK);
	}
	xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
					HDCP2X_TX_HDCPPORT_RX_CAPS_OFFSET,
					(void *)rxcaps,
					HDCP_2_2_RXCAPS_LEN);

	return ((rxcaps[0] == HDCP_2_2_RX_CAPS_VERSION_VAL) &&
		HDCP_2_2_DP_HDCP_CAPABLE(rxcaps[2]));
}

static u32 xlnx_hdcp2x_tx_get_timer_count(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	return xlnx_hdcp_tmrcntr_get_value(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
					   XHDCP2X_TX_TIMER_CNTR_0);
}

static int xlnx_hdcp2x_hdmitx_read_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 msg_id)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	int msg_read = 0;
	int status = -EINVAL;

	tx_msg->msg = msg_id;

	switch (msg_id) {
	case HDCP_2_2_AKE_SEND_CERT:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct hdcp2x_tx_ake_sendcert));

		if (msg_read == sizeof(struct hdcp2x_tx_ake_sendcert))
			status = 0;
		break;
	case HDCP_2_2_AKE_SEND_HPRIME:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct hdcp2x_tx_ake_sendprime));

		if (msg_read == sizeof(struct hdcp2x_tx_ake_sendprime))
			status = 0;
		break;
	case HDCP_2_2_AKE_SEND_PAIRING_INFO:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct
						hdcp2x_tx_ake_send_pairing_info));

		if (msg_read == sizeof(struct hdcp2x_tx_ake_send_pairing_info))
			status = 0;
		break;
	case HDCP_2_2_LC_SEND_LPRIME:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct hdcp2x_tx_lc_send_lc_prime));

		if (msg_read == sizeof(struct hdcp2x_tx_lc_send_lc_prime))
			status = 0;
		break;
	case HDCP_2_2_REP_SEND_RECVID_LIST:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct hdcp2x_tx_rpt_auth_send_rcvid_list));

		if (msg_read == sizeof(struct hdcp2x_tx_rpt_auth_send_rcvid_list))
			status = 0;
		break;
	case HDCP_2_2_REP_STREAM_READY:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RD_MSG_OFFSET,
						(u8 *)&tx_msg->msg_type.msg_id,
						sizeof(struct
						hdcp2x_tx_rpt_auth_stream_ready));

		if (msg_read == sizeof(struct hdcp2x_tx_rpt_auth_stream_ready))
			status = 0;
		break;
	default:
		status = -EINVAL;
		break;
	}

	return status;
}

int xlnx_hdcp2x_tx_read_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 msg_id)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	int msg_read = 0;
	int status = -EINVAL;

	if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_HDMI)
		return xlnx_hdcp2x_hdmitx_read_msg(xhdcp2x_tx, msg_id);

	switch (msg_id) {
	case HDCP_2_2_AKE_SEND_CERT:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_CERT_RX_OFFSET,
						tx_msg->msg_type.ake_send_cert.cert_rx.rcvid,
						HDCP2X_TX_CERT_SIZE);
		msg_read +=
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_R_RX_OFFSET,
						tx_msg->msg_type.ake_send_cert.r_rx,
						HDCP_2_2_RRX_LEN);
		msg_read +=
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_RX_CAPS_OFFSET,
						tx_msg->msg_type.ake_send_cert.rxcaps,
						HDCP_2_2_RXCAPS_LEN);
		if (msg_read == (HDCP2X_TX_CERT_SIZE +
				 HDCP_2_2_RRX_LEN +
				 HDCP_2_2_RXCAPS_LEN))
			status = 0;
		break;
	case HDCP_2_2_AKE_SEND_HPRIME:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_H_PRIME_OFFSET,
						tx_msg->msg_type.ake_send_prime.h_prime,
						HDCP_2_2_H_PRIME_LEN);
		if (msg_read == HDCP_2_2_H_PRIME_LEN)
			status = 0;
		break;
	case HDCP_2_2_AKE_SEND_PAIRING_INFO:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_E_KH_KM_PAIRING_OFFSET,
						tx_msg->msg_type.ake_send_pairing_info.ekh_km,
						HDCP_2_2_E_KH_KM_LEN);
		if (msg_read == HDCP_2_2_E_KH_KM_LEN)
			status = 0;
		break;
	case HDCP_2_2_LC_SEND_LPRIME:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_L_PRIME_OFFSET,
						tx_msg->msg_type.lcsend_lcprime.lprime,
						HDCP_2_2_L_PRIME_LEN);
		if (msg_read == HDCP_2_2_L_PRIME_LEN)
			status = 0;
		break;
	case HDCP_2_2_REP_SEND_RECVID_LIST:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_RX_INFO_OFFSET,
						tx_msg->msg_type.rpt_auth_send_rcvid.rxinfo,
						HDCP_2_2_RXINFO_LEN);
		msg_read +=
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_SEQ_NUM_V_OFFSET,
						tx_msg->msg_type.rpt_auth_send_rcvid.seq_num_v,
						HDCP_2_2_SEQ_NUM_LEN);
		msg_read +=
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_V_PRIME_OFFSET,
						tx_msg->msg_type.rpt_auth_send_rcvid.vprime,
						HDCP_2_2_V_PRIME_HALF_LEN);
		msg_read +=
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_RCVR_ID_LST_OFFSET, (u8 *)
						tx_msg->msg_type.rpt_auth_send_rcvid.rcvids,
						HDCP2X_TX_HDCPPORT_RCVR_ID_LST_MAX_SIZE);
		if (msg_read == HDCP_2_2_RXINFO_LEN +
				HDCP_2_2_SEQ_NUM_LEN +
				HDCP_2_2_V_PRIME_HALF_LEN +
				HDCP2X_TX_HDCPPORT_RCVR_ID_LST_MAX_SIZE)
			status = 0;
		break;
	case HDCP_2_2_REP_STREAM_READY:
		msg_read =
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_M_PRIME_OFFSET,
						tx_msg->msg_type.rpt_auth_stream_rdy.m_prime,
						HDCP_2_2_MPRIME_LEN);
		if (msg_read == HDCP_2_2_MPRIME_LEN)
			status = 0;
		break;
	default:
		status = -EINVAL;
		break;
	}

	return status;
}

static int xlnx_hdmi_hdcp2x_tx_write_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg tx_msg;
	int message_size = 0;
	int status = -EINVAL;

	memcpy(&tx_msg, xhdcp2x_tx->msg_buffer, sizeof(struct xhdcp2x_tx_msg));

	switch (tx_msg.msg_type.msg_id) {
	case HDCP_2_2_AKE_INIT:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_ake_init) + 1);
		if (message_size == sizeof(struct hdcp2x_tx_ake_init) + 1)
			status = 0;
		break;
	case HDCP_2_2_AKE_NO_STORED_KM:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_ake_no_stored_km)
							+ 1);

		if (message_size == sizeof(struct hdcp2x_tx_ake_no_stored_km) + 1)
			status = 0;
		break;
	case HDCP_2_2_AKE_STORED_KM:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_ake_stored_km)
							+ 1);

		if (message_size == (sizeof(struct hdcp2x_tx_ake_stored_km) + 1))
			status = 0;
		break;
	case HDCP_2_2_LC_INIT:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_lc_init) + 1);

		if (message_size == (sizeof(struct hdcp2x_tx_lc_init) + 1))
			status = 0;
		break;
	case HDCP_2_2_SKE_SEND_EKS:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_ske_send_eks)
							+ 1);

		if (message_size == (sizeof(struct hdcp2x_tx_ske_send_eks) + 1))
			status = 0;
		break;
	case HDCP2X_TX_TYPE_VALUE:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_TYPE_VALUE_OFFSET,
							(u8 *)&xhdcp2x_tx
							->xhdcp2x_info.content_stream_type,
							HDCP2X_TX_HDCPPORT_TYPE_VALUE_SIZE);
		if (message_size == HDCP2X_TX_HDCPPORT_TYPE_VALUE_SIZE)
			status = 0;
		break;
	case HDCP_2_2_REP_SEND_ACK:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
							(u8 *)&tx_msg,
							sizeof(struct hdcp2x_tx_rpt_auth_send_ack)
							+ 1);

		if (message_size == (sizeof(struct hdcp2x_tx_rpt_auth_send_ack) + 1))
			status = 0;
		break;
	case HDCP_2_2_REP_STREAM_MANAGE:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_WR_MSG_OFFSET,
						(u8 *)&tx_msg,
						sizeof(struct hdcp2x_tx_rpt_auth_stream_manage)
						+ 1);

		if (message_size == (sizeof(struct hdcp2x_tx_rpt_auth_stream_manage) + 1))
			status = 0;
		break;
	default:
		status = -EINVAL;
		break;
	}
	return status;
}

static int xlnx_hdcp2x_tx_write_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg buffer;
	int message_size = 0;
	int status = -EINVAL;

	memcpy(&buffer, xhdcp2x_tx->msg_buffer, sizeof(struct xhdcp2x_tx_msg));

	if (xhdcp2x_tx->is_hdmi)
		return xlnx_hdmi_hdcp2x_tx_write_msg(xhdcp2x_tx);

	switch (buffer.msg_type.msg_id) {
	case HDCP_2_2_AKE_INIT:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_R_TX_OFFSET,
							buffer.msg_type.ake_int.r_tx,
							HDCP_2_2_RTX_LEN);
		message_size +=
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_TX_CAPS_OFFSET,
							buffer.msg_type.ake_int.txcaps,
							HDCP2X_TX_TXCAPS_SIZE);

		if (message_size == (HDCP_2_2_RTX_LEN + HDCP2X_TX_TXCAPS_SIZE))
			status = 0;
		break;
	case HDCP_2_2_AKE_NO_STORED_KM:
		message_size =
		xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_E_KPUB_KM_OFFSET,
						buffer.msg_type.ake_nostored_km.ek_pubkm,
						HDCP2X_TX_HDCPPORT_E_KPUB_KM_SIZE);

		if (message_size == HDCP2X_TX_HDCPPORT_E_KPUB_KM_SIZE)
			status = 0;
		break;
	case HDCP_2_2_AKE_STORED_KM:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_E_KH_KM_OFFSET,
							buffer.msg_type.ake_stored_km.ekh_km,
							HDCP_2_2_E_KH_KM_LEN);
		message_size +=
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_M_OFFSET,
							buffer.msg_type.ake_stored_km.r_tx,
							HDCP_2_2_E_KH_KM_LEN);

		if (message_size == (HDCP_2_2_E_KH_KM_LEN + HDCP_2_2_E_KH_KM_LEN))
			status = 0;
		break;
	case HDCP_2_2_LC_INIT:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_R_N_OFFSET,
							buffer.msg_type.lcinit.rn,
							HDCP_2_2_RN_LEN);
		if (message_size == HDCP_2_2_RN_LEN)
			status = 0;
		break;
	case HDCP_2_2_SKE_SEND_EKS:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_E_DKEY_KS_OFFSET,
							buffer.msg_type.ske_send_eks.edkeys_ks,
							HDCP_2_2_E_DKEY_KS_LEN);
		message_size +=
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_R_IV_OFFSET,
							buffer.msg_type.ske_send_eks.riv,
							HDCP_2_2_RIV_LEN);

		if (message_size == (HDCP_2_2_E_DKEY_KS_LEN +
				HDCP_2_2_RIV_LEN))
			status = 0;
		break;
	case HDCP2X_TX_TYPE_VALUE:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_TYPE_VALUE_OFFSET,
							(u8 *)&xhdcp2x_tx
							->xhdcp2x_info.content_stream_type,
							HDCP2X_TX_HDCPPORT_TYPE_VALUE_SIZE);
		if (message_size == HDCP2X_TX_HDCPPORT_TYPE_VALUE_SIZE)
			status = 0;
		break;
	case HDCP_2_2_REP_SEND_ACK:
		message_size =
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_V_OFFSET,
							buffer.msg_type.rpt_auth_send_ack.V,
							HDCP_2_2_V_PRIME_HALF_LEN);
		if (message_size == HDCP_2_2_V_PRIME_HALF_LEN)
			status = 0;
		break;
	case HDCP_2_2_REP_STREAM_MANAGE:
		message_size =
		xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_SEQ_NUM_M_OFFSET,
						buffer.msg_type.rpt_auth_stream_mng.seq_num_m,
						HDCP_2_2_SEQ_NUM_LEN);
		message_size +=
			xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
							HDCP2X_TX_HDCPPORT_K_OFFSET,
							buffer.msg_type.rpt_auth_stream_mng.K,
							HDCP2X_TX_HDCPPORT_K_SIZE);
		message_size +=
		xhdcp2x_tx->handlers.wr_handler(xhdcp2x_tx->interface_ref,
						HDCP2X_TX_HDCPPORT_STREAM_ID_TYPE_OFFSET,
						buffer.msg_type.rpt_auth_stream_mng.streamid_type,
						HDCP2X_TX_HDCPPORT_STREAM_ID_TYPE_SIZE);
		if (message_size == (HDCP_2_2_SEQ_NUM_LEN +
				HDCP2X_TX_HDCPPORT_K_SIZE +
				HDCP2X_TX_HDCPPORT_STREAM_ID_TYPE_SIZE))
			status = 0;
		break;
	default:
		status = -EINVAL;
		break;
	}

	return status;
}

void xlnx_hdcp2x_tx_process_cp_irq(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	u8 rx_status = 0;

	xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
					HDCP2X_TX_HDCPPORT_RX_STATUS_OFFSET,
					&rx_status,
					HDCP_2_2_HDMI_RXSTATUS_LEN);
	xhdcp2x_tx->xhdcp2x_info.dp_rx_status = rx_status;
}

int xlnx_hdcp2x_task_monitor(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	enum hdcp2x_tx_state new_state;

	if (!xhdcp2x_tx->xhdcp2x_info.is_enabled)
		return (int)(xhdcp2x_tx->xhdcp2x_info.auth_status);

	new_state = hdcp2x_tx_protocol_authenticate_sm(xhdcp2x_tx);

	xhdcp2x_tx->xhdcp2x_info.prev_state =
			xhdcp2x_tx->xhdcp2x_info.curr_state;
	xhdcp2x_tx->xhdcp2x_info.curr_state = new_state;

	return xhdcp2x_tx->xhdcp2x_info.auth_status;
}

void xlnx_hdcp2x_tx_timer_init(struct xlnx_hdcp2x_config *xhdcp2x_tx,
			       struct xlnx_hdcp_timer_config *tmr_cntrl)
{
	xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr = *tmr_cntrl;

	xlnx_hdcp_tmrcntr_set_options(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
				      XHDCP2X_TX_TIMER_CNTR_0,
				      XTC_INT_MODE_OPTION | XTC_DOWN_COUNT_OPTION);

	xlnx_hdcp_tmrcntr_set_options(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
				      XHDCP2X_TX_TIMER_CNTR_1, XTC_AUTO_RELOAD_OPTION);
}

void xlnx_hdcp2x_tx_start_timer(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				u32 timeout, u8 reason_id)
{
	u32 ticks = (u32)(xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr.hw_config.sys_clock_freq
					/ XHDCP2X_TX_CLKDIV_MHZ) * timeout * XHDCP2X_TX_CLKDIV_HZ;

	xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired = 0;
	xhdcp2x_tx->xhdcp2x_internal_timer.reason_id = reason_id;
	xhdcp2x_tx->xhdcp2x_internal_timer.initial_ticks = ticks;

	if (reason_id != XHDCP2X_TX_TS_UNDEFINED &&
	    reason_id != XHDCP2X_TX_TS_RX_REAUTH_CHECK &&
	    reason_id != XHDCP2X_TX_TS_RX_REAUTH_CHECK)
		xhdcp2x_tx->xhdcp2x_info.msg_available = 0;

	xlnx_hdcp_tmrcntr_set_reset_value(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
					  XHDCP2X_TX_TIMER_CNTR_0, ticks);

	xlnx_hdcp_tmrcntr_start(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
				XHDCP2X_TX_TIMER_CNTR_0);
}

void xlnx_hdcp2x_tx_timer_handler(void *callbackref, u8 tmr_cnt_number)
{
	struct xlnx_hdcp2x_config *xhdcp2x_tx =	(struct xlnx_hdcp2x_config *)callbackref;

	if (tmr_cnt_number == XHDCP2X_TX_TIMER_CNTR_1)
		return;

	xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired = 1;
	if (xhdcp2x_tx->xhdcp2x_info.is_enabled)
		xlnx_hdcp2x_tx_read_rxstatus(xhdcp2x_tx);
}

void xlnx_hdcp2x_tx_read_rxstatus(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	u8 read_buffer[2];

	if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_HDMI) {
		xhdcp2x_tx->handlers.rd_handler(xhdcp2x_tx->interface_ref,
						HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET,
						(void *)read_buffer,
						sizeof(read_buffer));

		xhdcp2x_tx->xhdcp2x_info.rx_status = read_buffer[0]
					| (read_buffer[1] << BITS_PER_BYTE);
	}
}

int xlnx_hdcp2x_tx_write_type_value(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg_type.msg_id = HDCP2X_TX_TYPE_VALUE;

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_ake_init(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id  = HDCP_2_2_AKE_INIT;

	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng,
					  xhdcp2x_tx->xhdcp2x_info.r_tx,
					  HDCP_2_2_RTX_LEN, HDCP_2_2_RTX_LEN);

	memcpy(&tx_msg->msg_type.ake_int.r_tx, xhdcp2x_tx->xhdcp2x_info.r_tx,
	       sizeof(tx_msg->msg_type.ake_int.r_tx));
	memcpy(&tx_msg->msg_type.ake_int.txcaps, xhdcp2x_tx->xhdcp2x_info.txcaps,
	       sizeof(tx_msg->msg_type.ake_int.txcaps));

	dev_dbg(xhdcp2x_tx->dev, "write ake init");

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_ske_send_eks(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				      const u8 *edkey_ptr, const u8 *riv_ptr)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_SKE_SEND_EKS;

	memcpy(tx_msg->msg_type.ske_send_eks.edkeys_ks, edkey_ptr,
	       sizeof(tx_msg->msg_type.ske_send_eks.edkeys_ks));

	memcpy(tx_msg->msg_type.ske_send_eks.riv, riv_ptr,
	       sizeof(tx_msg->msg_type.ske_send_eks.riv));

	dev_dbg(xhdcp2x_tx->dev, "write ske send eks");

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_lcinit(struct xlnx_hdcp2x_config *xhdcp2x_tx, const u8 *rn_ptr)
{
	struct xhdcp2x_tx_msg *tx_msg =	(struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_LC_INIT;

	memcpy(tx_msg->msg_type.lcinit.rn, rn_ptr, sizeof(tx_msg->msg_type.lcinit.rn));

	dev_dbg(xhdcp2x_tx->dev, "write lc-init\r");

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_ake_storedkm(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				      const struct hdcp2x_tx_pairing_info  *hdcp2x_tx_pairing_info)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_AKE_STORED_KM;

	memcpy(tx_msg->msg_type.ake_stored_km.ekh_km, hdcp2x_tx_pairing_info->ekh_km,
	       sizeof(tx_msg->msg_type.ake_stored_km.ekh_km));
	memcpy(tx_msg->msg_type.ake_stored_km.r_tx, hdcp2x_tx_pairing_info->rtx,
	       sizeof(tx_msg->msg_type.ake_stored_km.r_tx));
	memcpy(tx_msg->msg_type.ake_stored_km.r_rx, hdcp2x_tx_pairing_info->rrx,
	       sizeof(tx_msg->msg_type.ake_stored_km.r_rx));

	dev_dbg(xhdcp2x_tx->dev, "write AKE stored km");

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_akenostored_km(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					const struct hdcp2x_tx_pairing_info *pairing_info,
					const struct hdcp2x_tx_cert_rx *cert_ptr)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;
	u8 masking_seed[HDCP2X_TX_KM_MSK_SEED_SIZE];
	u8 ek_pubkm[HDCP_2_2_E_KPUB_KM_LEN];

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_AKE_NO_STORED_KM;

	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng,
					  masking_seed, HDCP2X_TX_KM_MSK_SEED_SIZE,
					  HDCP2X_TX_KM_MSK_SEED_SIZE);
	xlnx_hdcp2x_tx_encryptedkm((const struct hdcp2x_tx_cert_rx *)cert_ptr,
				   pairing_info->km, masking_seed, ek_pubkm);

	memcpy(tx_msg->msg_type.ake_nostored_km.ek_pubkm, ek_pubkm,
	       sizeof(tx_msg->msg_type.ake_nostored_km.ek_pubkm));

	dev_dbg(xhdcp2x_tx->dev, "write AKE no stored km");

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

int xlnx_hdcp2x_tx_write_rptr_auth_send_ack(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					    const u8 *v_ptr)
{
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_REP_SEND_ACK;

	memcpy(tx_msg->msg_type.rpt_auth_send_ack.V, v_ptr,
	       sizeof(tx_msg->msg_type.rpt_auth_send_ack.V));

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

struct hdcp2x_tx_pairing_info *xlnx_hdcp2x_tx_get_pairing_info(struct xlnx_hdcp2x_config
							       *xhdcp2x_tx, const u8 *rcvid)
{
	struct hdcp2x_tx_pairing_info *pairing_info_ptr;
	u8 illegal_rcvd[] = {0x0, 0x0, 0x0, 0x0, 0x0};
	int i = 0;

	if (!memcmp(rcvid, illegal_rcvd, HDCP_2_2_RECEIVER_ID_LEN))
		return NULL;

	for (i = 0; i < XHDCP2X_TX_MAX_STORED_PAIRINGINFO; i++) {
		pairing_info_ptr = &xhdcp2x_tx->xhdcp2x_info.pairing_info[i];
		if (!memcmp(rcvid, pairing_info_ptr->rcvid, HDCP_2_2_RECEIVER_ID_LEN))
			return pairing_info_ptr;
	}

	return NULL;
}

void xlnx_hdcp2x_tx_invalidate_paring_info(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					   const u8 *rcvid)
{
	struct hdcp2x_tx_pairing_info *pairing_info_ptr =
			xlnx_hdcp2x_tx_get_pairing_info(xhdcp2x_tx, rcvid);

	if (!pairing_info_ptr)
		return;

	memset(pairing_info_ptr, 0, sizeof(struct hdcp2x_tx_pairing_info));
}

struct hdcp2x_tx_pairing_info *xlnx_hdcp2x_tx_update_pairinginfo(struct xlnx_hdcp2x_config
					*xhdcp2x_tx,
					struct hdcp2x_tx_pairing_info *pairing_info,
					u8 ready)
{
	struct hdcp2x_tx_pairing_info  *xhdcp2x_pairing_info_ptr;
	int i;
	int i_match = 0;
	u8 match = 0;

	for (i = 0; i < XHDCP2X_TX_MAX_STORED_PAIRINGINFO; i++) {
		xhdcp2x_pairing_info_ptr =
			&xhdcp2x_tx->xhdcp2x_info.pairing_info[i];
		if (!(xhdcp2x_pairing_info_ptr->ready) && !(match)) {
			i_match = i;
			match = 1;
		}
		if (!memcmp(pairing_info->rcvid, xhdcp2x_pairing_info_ptr->rcvid,
			    HDCP_2_2_RECEIVER_ID_LEN)) {
			i_match = i;
			break;
		}
	}
	xhdcp2x_pairing_info_ptr = &xhdcp2x_tx->xhdcp2x_info.pairing_info[i_match];

	memcpy(xhdcp2x_pairing_info_ptr, pairing_info, sizeof(struct hdcp2x_tx_pairing_info));
	xhdcp2x_pairing_info_ptr->ready = ready;

	return xhdcp2x_pairing_info_ptr;
}

int xlnx_hdcp2x_tx_rptr_auth_stream_mng(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	struct hdcp2x_tx_pairing_info  *xhdcp2x_pairing_info =
			(struct hdcp2x_tx_pairing_info *)
			xhdcp2x_tx->xhdcp2x_info.state_context;
	struct xhdcp2x_tx_msg *tx_msg = (struct xhdcp2x_tx_msg *)xhdcp2x_tx->msg_buffer;

	tx_msg->msg = (xhdcp2x_tx->xhdcp2x_hw.protocol != XHDCP2X_TX_DP) ?
				HDCP_2_2_HDMI_REG_WR_MSG_OFFSET :
				HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET;
	tx_msg->msg_type.msg_id = HDCP_2_2_REP_STREAM_MANAGE;

	drm_hdcp_cpu_to_be24(tx_msg->msg_type.rpt_auth_stream_mng.seq_num_m,
			     xhdcp2x_tx->xhdcp2x_info.seq_num_m);

	/* The parameter K is always set to 0x1 by the HDCP transmitter */
	/* Value is sent in big endian format */
	tx_msg->msg_type.rpt_auth_stream_mng.K[0] = 0x0;
	tx_msg->msg_type.rpt_auth_stream_mng.K[1] = 0x1;

	tx_msg->msg_type.rpt_auth_stream_mng.streamid_type[0] = HDCP_STREAM_TYPE0;
	tx_msg->msg_type.rpt_auth_stream_mng.streamid_type[1] =
			(u8)xhdcp2x_tx->xhdcp2x_info.content_stream_type;

	xlnx_hdcp2x_tx_compute_m(xhdcp2x_tx->xhdcp2x_info.rn,
				 xhdcp2x_tx->xhdcp2x_info.r_rx,
				 xhdcp2x_tx->xhdcp2x_info.r_tx,
				 tx_msg->msg_type.rpt_auth_stream_mng.streamid_type,
				 tx_msg->msg_type.rpt_auth_stream_mng.K,
				 tx_msg->msg_type.rpt_auth_stream_mng.seq_num_m,
				 xhdcp2x_pairing_info->km,
				 xhdcp2x_tx->xhdcp2x_info.M);

	/* Increment the M on every Stream message */
	xhdcp2x_tx->xhdcp2x_info.seq_num_m++;

	return xlnx_hdcp2x_tx_write_msg(xhdcp2x_tx);
}

void xlnx_hdcp2x_tx_generatekm(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 *kmptr)
{
	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_rng,
					  kmptr, HDCP2X_TX_KM_SIZE,
					  HDCP2X_TX_KM_SIZE);
}

int xlnx_hdcp2x_tx_wait_for_receiver(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				     int expected_size, u8 ready_bit)
{
	u32 timer_cnt = 0;
	u32 interval_cnt = xhdcp2x_tx->xhdcp2x_info.polling_value *
		((u32)(xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr.hw_config.sys_clock_freq)
		      / XHDCP2X_TX_CLKDIV_HZ);

	if (xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired) {
		if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_DP) {
			xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
			return 0;
		}
		if ((!ready_bit && ((xhdcp2x_tx->xhdcp2x_info.rx_status &
			XHDCP2X_TX_RXSTATUS_AVAIL_BYTES_MASK) == expected_size)) ||
			(ready_bit && (xhdcp2x_tx->xhdcp2x_info.rx_status &
			XHDCP2X_TX_RXSTATUS_READY_MASK))) {
			xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
			return 0;
		}

		return -EINVAL;
	}
	timer_cnt = xlnx_hdcp2x_tx_get_timer_count(xhdcp2x_tx);

	if (!xhdcp2x_tx->xhdcp2x_info.polling_value ||
	    ((xhdcp2x_tx->xhdcp2x_internal_timer.initial_ticks - timer_cnt)
	    >= interval_cnt)) {
		xhdcp2x_tx->xhdcp2x_internal_timer.initial_ticks = timer_cnt;
		if (xhdcp2x_tx->xhdcp2x_hw.protocol == XHDCP2X_TX_DP) {
			if (xhdcp2x_tx->xhdcp2x_internal_timer.reason_id ==
					HDCP_2_2_AKE_SEND_HPRIME) {
				if (xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
					XHDCP2X_RX_STATUS_H_PRIME_AVAILABLE) {
					xhdcp2x_tx->xhdcp2x_info.dp_rx_status &=
					~XHDCP2X_RX_STATUS_H_PRIME_AVAILABLE;
					xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
					dev_dbg(xhdcp2x_tx->dev,
						"HDCP2XTX: H' is Available through CP_IRQ\n\r");
				}
			} else if (xhdcp2x_tx->xhdcp2x_internal_timer.reason_id ==
					HDCP_2_2_AKE_SEND_PAIRING_INFO) {
				if (xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
						XHDCP2X_RX_STATUS_PAIRING_AVAILABLE) {
					xhdcp2x_tx->xhdcp2x_info.dp_rx_status &=
						~XHDCP2X_RX_STATUS_PAIRING_AVAILABLE;
					xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
				}
			} else if (xhdcp2x_tx->xhdcp2x_internal_timer.reason_id ==
					HDCP_2_2_REP_SEND_RECVID_LIST) {
				if (xhdcp2x_tx->xhdcp2x_info.dp_rx_status &
						XHDCP2X_RX_STATUS_RPTR_RDY) {
					xhdcp2x_tx->xhdcp2x_info.dp_rx_status =
							~XHDCP2X_RX_STATUS_RPTR_RDY;
					xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
				}
			}
			if (xhdcp2x_tx->xhdcp2x_info.msg_available) {
				xlnx_hdcp_tmrcntr_stop(&
					xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
					XHDCP2X_TX_TIMER_CNTR_0);
				xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired = 1;
			}
			return 0;
		}
		xlnx_hdcp2x_tx_read_rxstatus(xhdcp2x_tx);

		if ((!ready_bit && ((xhdcp2x_tx->xhdcp2x_info.rx_status &
		    XHDCP2X_TX_RXSTATUS_AVAIL_BYTES_MASK) == expected_size)) ||
		    ((ready_bit &&
		    (xhdcp2x_tx->xhdcp2x_info.rx_status &
		    XHDCP2X_TX_RXSTATUS_READY_MASK)) &&
		    ((xhdcp2x_tx->xhdcp2x_info.rx_status &
		    XHDCP2X_TX_RXSTATUS_AVAIL_BYTES_MASK) > 0))) {
			xlnx_hdcp_tmrcntr_stop(&xhdcp2x_tx->xhdcp2x_internal_timer.tmr_ctr,
					       XHDCP2X_TX_TIMER_CNTR_0);
			xhdcp2x_tx->xhdcp2x_internal_timer.timer_expired = 1;
			xhdcp2x_tx->xhdcp2x_info.msg_available = 1;
		}

		return 0;
	}

	return 0;
}

void xlnx_hdcp2x_handle_reauth_request(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xhdcp2x_tx->xhdcp2x_info.auth_status =
			XHDCP2X_TX_REAUTHENTICATE_REQUESTED;

	xlnx_hdcp2x_tx_disable_encryption(xhdcp2x_tx);
	xlnx_hdcp2x_cipher_disable(xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher.cipher_coreaddress);
	xlnx_hdcp2x_cipher_enable(xhdcp2x_tx->xhdcp2x_hw.xlnxhdcp2x_cipher.cipher_coreaddress);
}

void xlnx_hdcp2x_tx_auth_failed(struct xlnx_hdcp2x_config *xhdcp2x_tx)
{
	xhdcp2x_tx->xhdcp2x_info.auth_status =
			XHDCP2X_TX_AUTHENTICATION_BUSY;

	xhdcp2x_tx->xhdcp2x_info.is_rcvr_hdcp2x_capable =
			xlnx_hdcp2x_downstream_capbility(xhdcp2x_tx);
}
