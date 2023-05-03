/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Specific HDCP2X driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_HDCP2X_TX_H_
#define _XLNX_HDCP2X_TX_H_

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xhdcp2x_tx.h"

#define XHDCP2X_TX_MAX_ALLOWED_STREAM_MANAGE_CHECKS	128
#define XHDCP2X_TX_LC128_SIZE				16
#define XHDCP2X_TX_MAX_STORED_PAIRINGINFO		2
#define XHDCP2X_TX_TS_WAIT_FOR_STREAM_TYPE		0xFD
#define XHDCP2X_TX_TS_WAIT_FOR_CIPHER			GENMASK(7, 1)
#define XHDCP2X_TX_TS_RX_REAUTH_CHECK			GENMASK(7, 0)
#define XHDCP2X_TX_RXSTATUS_REAUTH_REQ_MASK		BIT(11)
#define XHDCP2X_TX_RXSTATUS_READY_MASK			BIT(10)
#define XHDCP2X_TX_RXSTATUS_AVAIL_BYTES_MASK		GENMASK(9, 0)
#define XHDCP2X_TX_SRM_RCVID_SIZE		HDCP_2_2_RECEIVER_ID_LEN
#define XHDCP2X_TX_SRM_SIGNATURE_SIZE		384
#define XHDCP2X_TX_MAX_MESSAGE_SIZE		(1 + 534)
#define XHDCP2X_TX_INVALID_RXSTATUS		GENMASK(15, 0)
#define XHDCP2X_TX_KPUB_DCP_LLC_N_SIZE		384
#define XHDCP2X_TX_KPUB_DCP_LLC_E_SIZE		1
#define XHDCP2X_TX_LC128_SIZE			16
#define XHDCP2X_TX_SRM_SIZE			396
#define XHDCP2X_TX_SHA_SIZE			256
#define XHDCP2X_TX_SHA_KEY_LENGTH		64
#define XHDCP2X_TX_RXCAPS_MASK			0x02
#define XHDCP2X_TX_CLKDIV_MHZ			1000000
#define XHDCP2X_TX_CLKDIV_HZ			1000

#define XHDCP2X_TX_TIMER_CNTR_0			0
#define XHDCP2X_TX_TIMER_CNTR_1			1
#define XHDCP2X_TX_TS_UNDEFINED			0

typedef u32 (*xhdcp2x_notify_msg) (void *callbackref, u32 offset,
		u8 *buf, u32 size);
typedef void (*xhdcp2x_callback_msg)(void *callbackref);

enum xhdcp2x_tx_protocol {
	XHDCP2X_TX_DP = 0,
	XHDCP2X_TX_HDMI = 1
};

enum xhdcp2x_tx_mode {
	XHDCP2X_TX_TRANSMITTER = 0,
	XHDCP2X_TX_REPEATER = 1,
};

/**
 * struct xlnx_hdcp2x_hw - HDCP2X subsystem configuration structure
 * @xlnxhdcp2x_cipher: HDCP2X cipher engine configuration
 * @xlnxhdcp2x_rng: HDCP2X random number generator configuration
 * @hdcp2xcore_address: HDCP2X core address
 * @tx_mode: HDCP Transmitter or Repeater
 * @protocol: Protocol type, DP or HDMI
 */
struct xlnx_hdcp2x_hw {
	struct xlnx_hdcp2x_cipher_hw   xlnxhdcp2x_cipher;
	struct xlnx_hdcp2x_rng_hw  xlnxhdcp2x_rng;
	void __iomem *hdcp2xcore_address;
	enum xhdcp2x_tx_mode tx_mode;
	enum xhdcp2x_tx_protocol  protocol;
};

enum xhdcp2x_tx_content_stream_type {
	XHDCP2X_STREAMTYPE_0 = 0,
	XHDCP2X_STREAMTYPE_1 = 1,
};

enum xhdcp2x_tx_authtype {
	XHDCP2X_TX_INCOMPATIBLE_RX = 0,
	XHDCP2X_TX_AUTHENTICATION_BUSY = 1,
	XHDCP2X_TX_AUTHENTICATED = 2,
	XHDCP2X_TX_UNAUTHENTICATED = 3,
	XHDCP2X_TX_REAUTHENTICATE_REQUESTED = 4,
	XHDCP2X_TX_DEVICE_IS_REVOKED = 5,
	XHDCP2X_TX_NO_SRM_LOADED = 6
};

enum xhdcp2x_tx_rx_status {
	XHDCP2X_RX_STATUS_RPTR_RDY = 0x01,
	XHDCP2X_RX_STATUS_H_PRIME_AVAILABLE = 0x02,
	XHDCP2X_RX_STATUS_PAIRING_AVAILABLE = 0x04,
	XHDCP2X_RX_STATUS_REAUTH_REQ = 0x08,
	XHDCP2X_RX_STATUS_LINK_INTEGRITY_FAIL = 0x10
};

/**
 * struct xhdcp2x_tx_internal_timer - Current state and data used for internal timer
 * @tmr_ctr: Hardware timer configuration structure
 * @initial_ticks: Keep track of the start value of the timer.
 * @reason_id: Keep track of why the timer was started (message or status checking)
 * @timer_expired: Expiration flag set when the hardware timer has interrupted.
 */
struct xhdcp2x_tx_internal_timer {
	struct xlnx_hdcp_timer_config tmr_ctr;
	u32 initial_ticks;
	u8 reason_id;
	bool timer_expired;

};

struct xhdcp2x_tx_msg {
	u8 msg;
	union hdcp2x_tx_msg_type msg_type;
};

/**
 * struct xhdcp2x_tx_internal_info - This structure contains configuration information
 * for the device.
 * @pairing_info: HDCP2X pairing info
 * @curr_state: Current state of internal state machine
 * @prev_state: Previous state of internal state machine
 * @auth_status: The result of internal state machine transaction
 * @content_stream_type: Content stream type used with Content Stream Management
 * @state_context: Context used internally by the state machine
 * @M: Calculated M value
 * @r_tx: Internal used rtx
 * @r_rx: Internal used rrx
 * @rn: Internal used rn
 * @txcaps: HDCP tx capabilities
 * @seq_num_v: Sequence number V used with Received Id list
 * @seq_num_m: Sequence number M used with Content Stream Management
 * @prev_seq_num_m: Previous sequence number M used with Content Stream Management
 * @polling_value: The currently used polling interval value for a message
 * @content_strm_mng_chk_cntr: Keeps track of Content Stream Management checks performed
 * @rx_status: HDCP RX status read on timer interrupt
 * @lc_counter: Locality may attempt 1024 times
 * @dp_rx_status: HDCP RX status read on CP_IRQ interrupt
 * @is_content_stream_type_set:Content stream type is set
 * @is_enabled:Is HDCP TX enabled state machine is active
 * @is_rcvr_hdcp2x_capable: Is receiver a HDCP2x capable
 * @is_rcvr_repeater: Is the receiver a HDCP repeater
 * @is_revoc_list_valid: Is revocation list valid
 * @is_device_revoked: Is a device listed in the revocation list
 * @msg_available: Message is available for reading
 */
struct xhdcp2x_tx_internal_info {
	struct hdcp2x_tx_pairing_info pairing_info[XHDCP2X_TX_MAX_STORED_PAIRINGINFO];
	enum hdcp2x_tx_state curr_state;
	enum hdcp2x_tx_state prev_state;
	enum xhdcp2x_tx_authtype auth_status;
	enum xhdcp2x_tx_content_stream_type content_stream_type;
	void *state_context;
	u8 M[32];
	u8 r_tx[8];
	u8 r_rx[8];
	u8 rn[8];
	u8 txcaps[3];
	u32 seq_num_v;
	u32 seq_num_m;
	u32 prev_seq_num_m;
	u32 polling_value;
	u16 content_strm_mng_chk_cntr;
	u16 rx_status;
	u16 lc_counter;
	u8 dp_rx_status;
	bool is_content_stream_type_set;
	bool is_enabled;
	bool is_rcvr_hdcp2x_capable;
	bool is_rcvr_repeater;
	bool is_revoc_list_valid;
	bool is_device_revoked;
	bool msg_available;
};

struct xhdcp2x_tx_callbacks {
	int (*rd_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	int (*wr_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	void (*notify_handler)(void *interface_ref, u32 notification);
};

/**
 * struct xlnx_hdcp2x_config - This structure contains HDCP2X driver
 * configuration information
 * @dev: device information
 * @xhdcp2x_hw: Configuration HDCP2x hardware
 * @xhdcp2x_pairing_info: HDCP2X pairing information
 * @xhdcp2x_revoc_list: HDCP2X revocation list
 * @xhdcp2x_topology: HDCP2x topology information
 * @xhdcp2x_info: Provides the control and status of internal driver parameters
 * @xhdcp2x_internal_timer: Internal timer parameters
 * @handlers: Callback handlers
 * @interface_ref: Interface reference
 * @interface_base: Interface base
 * @msg_buffer: Message buffer for messages that are sent/received
 * @lc128key: LC128 encryption key
 * @msg_buffer: Message buffer to store HDCP input/output messages
 * @srmkey: SRM encryption key
 * @is_hdmi: Interface type HDMI or DP
 * @txcaps: transmitter capabilities
 * @lane_count: Number of lanes data to be encrypted
 * @is_repeater: Says whether downstream is repeater or receiver
 */
struct xlnx_hdcp2x_config {
	struct device *dev;
	struct xlnx_hdcp2x_hw  xhdcp2x_hw;
	struct hdcp2x_tx_pairing_info xhdcp2x_pairing_info;
	struct hdcp2x_tx_revoclist xhdcp2x_revoc_list;
	struct hdcp2x_tx_topology xhdcp2x_topology;
	struct xhdcp2x_tx_internal_info xhdcp2x_info;
	struct xhdcp2x_tx_internal_timer xhdcp2x_internal_timer;
	struct xhdcp2x_tx_callbacks handlers;
	void *interface_ref;
	void __iomem *interface_base;
	u8 msg_buffer[XHDCP2X_TX_MAX_MESSAGE_SIZE];
	u8 *lc128key;
	u8 *srmkey;
	u8 is_hdmi;
	u8 *txcaps;
	u8 lane_count;
	bool is_repeater;
};

struct hdcp2x_tx_pairing_info
		*xlnx_hdcp2x_tx_update_pairinginfo(struct xlnx_hdcp2x_config *xhdcp2x_tx,
		struct hdcp2x_tx_pairing_info *pairing_info, u8 ready);
struct hdcp2x_tx_pairing_info *xlnx_hdcp2x_tx_get_pairing_info(struct xlnx_hdcp2x_config
				*xhdcp2x_tx, const u8 *rcvid);
int xlnx_hdcp2x_tx_init(struct xlnx_hdcp2x_config *xhdcp2x_tx, bool is_repeater);
int xlnx_hdcp2x_task_monitor(struct xlnx_hdcp2x_config *xhdcp2x_tx);
int xlnx_hdcp2x_tx_reset(struct xlnx_hdcp2x_config *xhdcp2x_tx);
int xlnx_hdcp2x_tx_verify_certificate(const struct hdcp2x_tx_cert_rx *rx_certificate,
				      const u8 *dcp_cert_nvalue, int dcp_cert_nsize,
				      const u8 *dcp_cert_evalue, int dcp_cert_esize);
int xlnx_hdcp2x_tx_write_akenostored_km(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					const struct hdcp2x_tx_pairing_info *pairing_info,
					const struct hdcp2x_tx_cert_rx *cert_ptr);
int xlnx_hdcp2x_tx_write_rptr_auth_send_ack(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					    const u8 *v_ptr);
int xlnx_hdcp2x_tx_write_ake_storedkm(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				      const struct hdcp2x_tx_pairing_info *hdcp2x_tx_pairing_info);
int xlnx_hdcp2x_tx_read_msg(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 msg_id);
int xlnx_hdcp2x_tx_write_lcinit(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				const u8 *rn_ptr);
int xlnx_hdcp2x_tx_write_type_value(struct xlnx_hdcp2x_config *xhdcp2x_tx);
int xlnx_hdcp2x_tx_write_ske_send_eks(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				      const u8 *edkey_ptr, const u8 *riv_ptr);
int xlnx_hdcp2x_tx_write_ake_init(struct xlnx_hdcp2x_config *xhdcp2x_tx);
int xlnx_hdcp2x_tx_encryptedkm(const struct hdcp2x_tx_cert_rx *rx_certificate,
			       const u8 *km_ptr, u8 *masking_seed, u8 *encrypted_km);
int xlnx_hdcp2x_tx_wait_for_receiver(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				     int expected_size, u8 ready_bit);
int xlnx_hdcp2x_tx_rptr_auth_stream_mng(struct xlnx_hdcp2x_config *xhdcp2x_tx);
int hdcp2x_tx_protocol_authenticate_sm(struct xlnx_hdcp2x_config *hdcp2x_tx);
int xlnx_hdcp2x_verify_srm(const u8 *srm, int srm_size, const u8 *dcp_cert_nvalue,
			   int dcp_cert_nsize, const u8 *dcp_cert_evalue, int dcp_cert_esize);
int xlnx_hdcp2x_loadkeys(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 *srm, u8 *lc128);
void xlnx_start_hdcp2x_engine(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_tx_timer_init(struct xlnx_hdcp2x_config *xhdcp2x_tx,
			       struct xlnx_hdcp_timer_config *tmr_cntrl);
void xlnx_hdcp2x_tx_auth_failed(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_handle_reauth_request(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_tx_generatekm(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 *kmptr);
void xlnx_hdcp2x_tx_invalidate_paring_info(struct xlnx_hdcp2x_config *xhdcp2x_tx,
					   const u8 *rcvid);
void xlnx_hdcp2x_tx_read_rxstatus(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_tx_process_cp_irq(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_tx_timer_handler(void *callbackref, u8 tmr_cnt_number);
void xlnx_hdcp2x_tx_compute_edkey_ks(const u8 *rn, const u8 *km, const u8 *ks, const u8 *r_rx,
				     const u8 *r_tx, u8 *encrypted_ks);
void xlnx_hdcp2x_tx_compute_lprime(const u8 *rn, const u8 *km, const u8 *r_rx, const u8 *r_tx,
				   u8 *lprime);
void xlnx_hdcp2x_tx_compute_v(const u8 *rn, const u8 *r_rx, const u8 *rx_info,
			      const u8 *r_tx, const u8 *rcvid_list, const u8 rcvid_count,
			      const u8 *seq_num_v, const u8 *km, u8 *hash_v);
void xlnx_hdcp2x_tx_compute_m(const u8 *rn, const u8 *r_rx, const u8 *r_tx,
			      const u8 *stream_id_type, const u8 *k,
			      const u8 *seq_num_m, const u8 *km, u8 *m_hash);
void xlnx_hdcp2x_tx_compute_hprime(const u8 *r_rx, const u8 *rxcaps,
				   const u8 *r_tx, const u8 *txcaps,
				   const u8 *km, u8 *hprime);
void xlnx_hdcp2x_tx_disable_encryption(struct xlnx_hdcp2x_config *xhdcp2x_tx);
void xlnx_hdcp2x_tx_start_timer(struct xlnx_hdcp2x_config *xhdcp2x_tx,
				u32 timeout, u8 reason_id);
void xlnx_hdcp2x_tx_enable_encryption(struct xlnx_hdcp2x_config *xhdcp2x_tx);
u8 xlnx_hdcp2x_tx_is_device_revoked(struct xlnx_hdcp2x_config *xhdcp2x_tx, u8 *rcvid);
const u8 *xlnx_hdcp2x_tx_get_publickey(struct xlnx_hdcp2x_config *xhdcp2x_tx);
bool xlnx_hdcp2x_downstream_capbility(struct xlnx_hdcp2x_config
				*xhdcp2x_tx);
#endif
