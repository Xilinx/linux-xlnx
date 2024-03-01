/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP2X Protocol Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XHDCP2X_TX_H_
#define _XHDCP2X_TX_H_

#include <drm/display/drm_hdcp.h>
#include <linux/types.h>

#define HDCP2X_TX_SRM_ID			0x91
#define HDCP2X_TX_REPEATERAUTH_STREAM_READY_SIZE	33

#define HDCP2X_TX_V_SIZE			32
#define HDCP2X_TX_MAX_DEV_COUNT			32
#define HDCP2X_TX_K_SIZE			2
#define HDCP2X_TX_STREAMID_TYPE_SIZE		2
#define HDCP2X_TX_SHA256_HASH_SIZE		32
#define HDCP2X_TX_AES128_SIZE			16
#define HDCP2X_TX_KM_SIZE			HDCP2X_TX_AES128_SIZE
#define HDCP2X_TX_KM_MSK_SEED_SIZE		HDCP2X_TX_SHA256_HASH_SIZE
#define HDCP2X_TX_KS_SIZE			16

#define HDCP2X_TX_CERT_PUB_KEY_N_SIZE		128
#define HDCP2X_TX_CERT_PUB_KEY_E_SIZE		3
#define HDCP2X_TX_CERT_RSVD_SIZE		2
#define HDCP2X_TX_CERT_RSA_PARAMETER_SIZE		384
#define HDCP2X_TX_CERT_SIGNATURE_SIZE		384
#define HDCP2X_TX_CERT_PADDING_BYTES		330
#define HDCP2X_TX_CERT_PADDING_END_DELIMITER	332
#define HDCP2X_TX_CERT_PADDING_TI_IDENTIFIER	333
#define HDCP2X_TX_CERT_PADDING_T_HASH		352
#define HDCP2X_TX_SRM_SIGNATURE_SIZE		384
#define HDCP2X_TX_CERT_TI_IDENTIFIER_SIZE 19
#define HDCP2X_TX_CERT_T_HASH_SIZE 19
#define HDCP2X_TX_CERT_SIZE		(HDCP_2_2_RECEIVER_ID_LEN +	\
					 HDCP2X_TX_CERT_PUB_KEY_N_SIZE +	\
					 HDCP2X_TX_CERT_PUB_KEY_E_SIZE +	\
					 HDCP2X_TX_CERT_RSVD_SIZE +	\
					 HDCP2X_TX_CERT_SIGNATURE_SIZE)
#define HDCP2X_TX_CERT_PUBLIC_EXPONENT_E	4
#define HDCP2X_TX_DKEY				15
#define HDCP2X_TX_DKEY_CTR1			1
#define HDCP2X_TX_DKEY_CTR2			2
#define HDCP2X_TX_DKEY_SIZE			2

#define HDCP2X_TX_TXCAPS_SIZE			3
#define HDCP2X_TX_KPUB_DCP_LLC_N_SIZE		384
#define HDCP2X_TX_KPUB_DCP_LLC_E_SIZE		1

#define HDCP2X_TX_HDCPPORT_E_KPUB_KM_SIZE	128
#define HDCP2X_TX_HDCPPORT_CERT_RX_SIZE		522
#define HDCP2X_TX_HDCPPORT_K_SIZE		2
#define HDCP2X_TX_HDCPPORT_TYPE_VALUE_SIZE	1

#define XDPTX_HDCP2X_DPCD_OFFSET			0x69000
#define HDCP2X_TX_HDCPPORT_M_OFFSET			(0x2B0 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_R_TX_OFFSET			(0x000 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_TX_CAPS_OFFSET		(0x008 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_CERT_RX_OFFSET		(0x00B + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_R_RX_OFFSET			(0x215 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_RX_CAPS_OFFSET		(0x21D + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_E_KPUB_KM_OFFSET		(0x220 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_E_KH_KM_OFFSET		(0x2A0 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_H_PRIME_OFFSET		(0x2C0 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_E_KH_KM_PAIRING_OFFSET	(0x2E0 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_R_N_OFFSET			(0x2F0 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_L_PRIME_OFFSET		(0x2F8 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_E_DKEY_KS_OFFSET		(0x318 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_R_IV_OFFSET			(0x328 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_RX_INFO_OFFSET		(0x330 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_SEQ_NUM_V_OFFSET		(0x332 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_V_PRIME_OFFSET		(0x335 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_RCVR_ID_LST_OFFSET		(0x345 + XDPTX_HDCP2X_DPCD_OFFSET)

#define HDCP2X_TX_HDCPPORT_RCVR_ID_LST_MAX_SIZE		155
#define HDCP2X_TX_HDCPPORT_V_OFFSET			(GENMASK(9, 5) + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_SEQ_NUM_M_OFFSET		(GENMASK(9, 4) + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_K_OFFSET			(0x3F3 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_STREAM_ID_TYPE_OFFSET	(0x3F5 + XDPTX_HDCP2X_DPCD_OFFSET)

#define HDCP2X_TX_HDCPPORT_STREAM_ID_TYPE_SIZE		2
#define HDCP2X_TX_HDCPPORT_M_PRIME_OFFSET		(0x473 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_RX_STATUS_OFFSET		(0x493 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_TYPE_VALUE_OFFSET		(0x494 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_VERSION_OFFSET		(0x50 + XDPTX_HDCP2X_DPCD_OFFSET)
#define HDCP2X_TX_HDCPPORT_RX_CAPS_OFFSET		(0x21D + XDPTX_HDCP2X_DPCD_OFFSET)

#define HDCP2X_TX_HDCPPORT_WRITE_MSG_OFFSET		BIT(5)
#define HDCP2X_TX_HDCPPORT_RXSTATUS_OFFSET		BIT(6)
#define HDCP2X_TX_HDCPPORT_READ_MSG_OFFSET		BIT(7)

#define HDCP2x_TX_REPEATER_MAX_CASCADE_DEPTH		4
#define HDCP2X_TX_REVOCATION_LIST_MAX_DEVICES		944
#define HDCP2X_TX_MAX_ALLOWED_LOCALITY_CHECKS		8
#define HDCP2X_TX_TYPE_VALUE				18

#define HDCP2X_TX_WAIT_REAUTH_CHECK_TIMEOUT		1000
#define HDCP2X_TX_WAIT_FOR_ENCRYPTION_TIMEOUT		200
#define HDCP2X_TX_WAIT_FOR_STREAM_TYPE_TIMEOUT		50

#define HDCP2X_TX_LEGACY2X_DEVICE_DOWNSTREAM(x)	((x) & BIT(1))
#define HDCP2X_TX_LEGACY1X_DEVICE_DOWNSTREAM(x)	((x) & BIT(0))

/*
 * HDCP Authentication Protocol messages in
 * HDCP2.3 specification. Section 4.1
 * https://www.digital-cp.com/sites/default/files/
 * HDCP%20Interface%20Independent%20Adaptation%20Specification%20Rev2_3.pdf
 */
struct hdcp2x_tx_ake_init {
	u8 msg_id;
	u8 r_tx[HDCP_2_2_RTX_LEN];
	u8 txcaps[HDCP2X_TX_TXCAPS_SIZE];
} __packed;

struct hdcp2x_tx_ake_no_stored_km {
	u8 msg_id;
	u8 ek_pubkm[HDCP_2_2_E_KPUB_KM_LEN];
} __packed;

struct hdcp2x_tx_ake_stored_km {
	u8 msg_id;
	u8 ekh_km[HDCP_2_2_E_KH_KM_LEN];
	u8 r_tx[HDCP_2_2_RTX_LEN];
	u8 r_rx[HDCP_2_2_RRX_LEN];
} __packed;

struct hdcp2x_tx_lc_init {
	u8 msg_id;
	u8 rn[HDCP_2_2_RN_LEN];
} __packed;

struct hdcp2x_tx_ske_send_eks {
	u8 msg_id;
	u8 edkeys_ks[HDCP_2_2_E_DKEY_KS_LEN];
	u8 riv[HDCP_2_2_RIV_LEN];
} __packed;

struct hdcp2x_tx_rpt_auth_send_ack {
	u8 msg_id;
	u8 V[HDCP_2_2_V_PRIME_HALF_LEN];
} __packed;

struct hdcp2x_tx_rpt_auth_stream_manage {
	u8 msg_id;
	u8 seq_num_m[HDCP_2_2_SEQ_NUM_LEN];
	u8 K[HDCP2X_TX_K_SIZE];
	u8 streamid_type[HDCP2X_TX_STREAMID_TYPE_SIZE];
} __packed;

struct hdcp2x_tx_cert_rx {
	u8 rcvid[HDCP_2_2_RECEIVER_ID_LEN];
	u8 N[HDCP2X_TX_CERT_PUB_KEY_N_SIZE];
	u8 e[HDCP2X_TX_CERT_PUB_KEY_E_SIZE];
	u8 reserved[HDCP2X_TX_CERT_RSVD_SIZE];
	u8 signature[HDCP2X_TX_CERT_SIGNATURE_SIZE];
} __packed;

struct hdcp2x_tx_ake_sendcert {
	u8 msg_id;
	struct hdcp2x_tx_cert_rx  cert_rx;
	u8 r_rx[HDCP_2_2_RRX_LEN];
	u8 rxcaps[HDCP_2_2_RXCAPS_LEN];
} __packed;

struct hdcp2x_tx_ake_send_pairing_info {
	u8 msg_id;
	u8 ekh_km[HDCP_2_2_E_KH_KM_LEN];
} __packed;

struct hdcp2x_tx_lc_send_lc_prime {
	u8 msg_id;
	u8 lprime[HDCP_2_2_L_PRIME_LEN];
} __packed;

struct hdcp2x_tx_rpt_auth_send_rcvid_list {
	u8 msg_id;
	u8 rxinfo[HDCP_2_2_RXINFO_LEN];
	u8 seq_num_v[HDCP_2_2_SEQ_NUM_LEN];
	u8 vprime[HDCP_2_2_V_PRIME_HALF_LEN];
	u8 rcvids[HDCP_2_2_MAX_DEVICE_COUNT][HDCP_2_2_RECEIVER_ID_LEN];
} __packed;

struct hdcp2x_tx_rpt_auth_stream_ready {
	u8 msg_id;
	u8 m_prime[HDCP_2_2_MPRIME_LEN];
} __packed;

struct hdcp2x_tx_ake_sendprime {
	u8 msg_id;
	u8 h_prime[HDCP_2_2_H_PRIME_LEN];
} __packed;

struct hdcp2x_tx_certrx {
	u8 rcvid[HDCP_2_2_RECEIVER_ID_LEN];
	u8 N[HDCP2X_TX_CERT_PUB_KEY_N_SIZE];
	u8 e[HDCP2X_TX_CERT_PUB_KEY_E_SIZE];
	u8 rsvd[HDCP2X_TX_CERT_RSVD_SIZE];
	u8 signature[HDCP2X_TX_CERT_SIGNATURE_SIZE];
} __packed;

struct hdcp2x_tx_pairing_info {
	u8 rcvid[HDCP_2_2_RECEIVER_ID_LEN];
	u8 rxcaps[HDCP_2_2_RXCAPS_LEN];
	u8 rtx[HDCP_2_2_RTX_LEN];
	u8 rrx[HDCP_2_2_RRX_LEN];
	u8 km[HDCP_2_2_E_KH_KM_LEN];
	u8 ekh_km[HDCP_2_2_E_KH_KM_LEN];
	u8 ready;
} __packed;

struct hdcp2x_tx_revoclist {
	u8  rcvid[HDCP2X_TX_REVOCATION_LIST_MAX_DEVICES][HDCP_2_2_RECEIVER_ID_LEN];
	u32 num_of_devices;
} __packed;

struct hdcp2x_tx_topology {
	u8  rcvid[HDCP2X_TX_MAX_DEV_COUNT][HDCP_2_2_RECEIVER_ID_LEN];
	u8  depth;
	u8  devicecount;
	u8  max_dev_exceeded;
	u8  max_cascaded_exceeded;
	u8  hdcp2x_legacy_ds;
	u8  hdcp1x_legacy_ds;
} __packed;

/**
 * union hdcp2x_tx_msg_type - HDCP 2X authentication protocol message buffers
 * @ake_send_cert: Reads CertRx message
 * @ake_send_prime: Reads HPrime message
 * @ake_send_pairing_info: Reads Ekh_km message
 * @lcsend_lcprime: Reads L` prime message
 * @rpt_auth_send_rcvid: Reads receiver-id list message
 * @rpt_auth_stream_rdy: Reads M` message
 * @ake_int: Writes Txcaps and RTx message
 * @ake_nostored_km: Writes Ekubkm message
 * @ake_stored_km: Writes Ekh_km message
 * @lcinit: Writes Rn message
 * @ske_send_eks: Writes Edkey(ks) and riv message
 * @rpt_auth_send_ack: Writes acknowledgment to the rcv-id list message
 * @rpt_auth_stream_mng: Writes content type value to the HDCP receiver
 * @msg_id: Identification id for messages
 */
union hdcp2x_tx_msg_type {
	u8 msg_id;
	struct hdcp2x_tx_ake_sendcert		ake_send_cert;
	struct hdcp2x_tx_ake_sendprime		ake_send_prime;
	struct hdcp2x_tx_ake_send_pairing_info	ake_send_pairing_info;
	struct hdcp2x_tx_lc_send_lc_prime	lcsend_lcprime;
	struct hdcp2x_tx_rpt_auth_send_rcvid_list	rpt_auth_send_rcvid;
	struct hdcp2x_tx_rpt_auth_stream_ready	rpt_auth_stream_rdy;
	struct hdcp2x_tx_ake_init		ake_int;
	struct hdcp2x_tx_ake_no_stored_km	ake_nostored_km;
	struct hdcp2x_tx_ake_stored_km		ake_stored_km;
	struct hdcp2x_tx_lc_init		lcinit;
	struct hdcp2x_tx_ske_send_eks		ske_send_eks;
	struct hdcp2x_tx_rpt_auth_send_ack	rpt_auth_send_ack;
	struct hdcp2x_tx_rpt_auth_stream_manage	rpt_auth_stream_mng;
};

/*
 * HDCP Transmitter State Diagram available in
 * HDCP2.3 specification. Section 2.8
 * https://www.digital-cp.com/sites/default/files/
 * HDCP%20Interface%20Independent%20Adaptation%20Specification%20Rev2_3.pdf
 */
/* HDCP 2X authentication protocol states */
enum hdcp2x_tx_state {
	H0_HDCP2X_TX_NO_RX_ATTACHED	= 0x00,
	H1_HDCP2X_TX_WAIT_FOR_TX_ENABLE	= 0x01,
	A0_HDCP2X_TX_AKE_INIT		= 0x02,
	A1_HDCP2X_TX_EXCHANGE_KM	= 0x03,
	A1_HDCP2X_TX_WAIT_FOR_ACK	= 0x04,
	A1_HDCP2X_TX_WAIT_FOR_HPRIME	= 0x05,
	A1_HDCP2X_TX_WAIT_FOR_PAIRING	= 0x06,
	A1_HDCP2X_TX_VERIFY_HPRIME	= 0x07,
	A2_HDCP2X_TX_LC_CHECK		= 0x08,
	A2_HDCP2X_TX_VERIFY_LPRIME	= 0x09,
	A3_HDCP2X_TX_EXCHANGE_KS	= 0x0A,
	A4_HDCP2X_TX_REPEATER_CHECK	= 0x0B,
	A5_HDCP2X_TX_AUTHENTICATED	= 0x0C,
	A6_HDCP2X_TX_WAIT_FOR_RCVID	= 0x0D,
	A7_HDCP2X_TX_VERIFY_RCVID	= 0x0E,
	A8_HDCP2X_TX_SEND_RCVID_ACK	= 0x0F,
	A9_HDCP2X_TX_STREAM_MANAGE	= 0x10,
	A9_HDCP2X_TX_VERIFY_MPRIME	= 0x11,
	HDCP2X_TX_NUM_STATES		= 0x12
};

#endif
