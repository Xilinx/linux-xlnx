/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx specific HDCP2X protocol driver.
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Kunal Vasant Rane <kunal.rane@amd.com>
 */

#ifndef __XILINX_HDCP2X_RX_H__
#define __XILINX_HDCP2X_RX_H__

#include <drm/display/drm_hdcp.h>
#include <linux/platform_device.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_hdcp2x_mmult.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_timer.h>

#define XHDCP2X_RX_MAX_LCINIT			1024
#define XHDCP2X_RX_MAX_MESSAGE_SIZE		534
#define XHDCP2X_RX_CERT_SIZE			522
#define XHDCP2X_RX_PRIVATEKEY_SIZE		320
#define XHDCP2X_RX_LOG_BUFFER_SIZE		256
#define XHDCP2X_RX_N_SIZE			128
#define XHDCP2X_RX_P_SIZE			64
#define XHDCP2X_RX_HASH_SIZE			32
#define XHDCP2X_RX_KD_SIZE			32
#define XHDCP2X_RX_HPRIME_SIZE			32
#define XHDCP2X_RX_LPRIME_SIZE			32
#define XHDCP2X_RX_MPRIME_SIZE			32
#define XHDCP2X_RX_VPRIME_SIZE			32
#define XHDCP2X_RX_MAX_DEVICE_COUNT		31
#define XHDCP2X_RX_KM_SIZE			16
#define XHDCP2X_RX_EKH_SIZE			16
#define XHDCP2X_RX_KS_SIZE			16
#define XHDCP2X_RX_AES_SIZE			16
#define XHDCP2X_RX_LC128_SIZE			16
#define XHDCP2X_RX_RN_SIZE			8
#define XHDCP2X_RX_RIV_SIZE			8
#define XHDCP2X_RX_RTX_SIZE			8
#define XHDCP2X_RX_RRX_SIZE			8
#define XHDCP2X_RX_RCVID_SIZE			5
#define XHDCP2X_RX_MAX_DEPTH			4
#define XHDCP2X_RX_TXCAPS_SIZE			3
#define XHDCP2X_RX_RXCAPS_SIZE			3
#define XHDCP2X_RX_SEQNUMM_SIZE			3
#define XHDCP2X_RX_STREAMID_SIZE		2
#define XHDCP2X_RX_TMR_CNTR_1			1
#define XHDCP2X_RX_TMR_CNTR_0			0

#define	HDCP_2_2_CERTRX			522
#define HDCP_2_2_K_PRIV_RX_LEN		64
#define	HDCP_2_2_TX_CAPS		3
#define HDCP_2_2_CERTRX_RESERVED	2

#define XHDCP2X_RX_ENCRYPTION_STATUS_INTERVAL	1000

#define R_TX_OFFSET			0x000
#define R_TX_SIZE			8
#define TX_CAPS_OFFSET			0x008
#define TX_CAPS_SIZE			3
#define CERT_RX_OFFSET			0x00B
#define CERT_RX_SIZE			522
#define R_RX_OFFSET			0x215
#define R_RX_SIZE			8
#define RX_CAPS_OFFSET			0x21D
#define RX_CAPS_SIZE			3
#define E_KPUB_KM_OFFSET		0x220
#define E_KPUB_KM_SIZE			128
#define E_KH_KM_OFFSET			0x2A0
#define E_KH_KM_SIZE			16
#define R_N_OFFSET			0x2F0
#define R_N_SIZE			8
#define M_OFFSET			0x2B0
#define M_SIZE				16
#define H_PRIME_OFFSET			0x2C0
#define H_PRIME_SIZE			32
#define E_KH_KM_PAIRING_OFFSET		0x2E0
#define E_KH_KM_PAIRING_SIZE		16
#define L_PRIME_OFFSET			0x2F8
#define L_PRIME_SIZE			32
#define E_DKEY_KS_OFFSET		0x318
#define E_DKEY_KS_SIZE			16
#define R_IV_OFFSET			0x328
#define R_IV_SIZE			8
#define M_PRIME_OFFSET			0x473
#define M_PRIME_SIZE			32
#define RX_STATUS_OFFSET		0x493
#define RX_STREAM_TYPE_OFFSET		0x494
#define RX_STREAM_TYPE_SIZE		1

#define RX_STATUS_LINK_INTEGRITY_FAILURE	0x10
#define RX_STATUS_REAUTH_REQ			0x08
#define RX_STATUS_PAIRING_AVAILABLE		0x04
#define RX_STATUS_H_PRIME_AVAILABLE		0x02
#define RXCAPS_HDCP_ENABLE			0x02
#define RXCAPS_REPEATER				0x01
#define XHDCP2X_KEY_SIZE			4

enum xhdcp2x_rx_message_ids {
	XHDCP2X_RX_MSG_ID_AKEINIT		= 0,
	XHDCP2X_RX_MSG_ID_AKESENDCERT		= 1,
	XHDCP2X_RX_MSG_ID_AKENOSTOREDKM		= 2,
	XHDCP2X_RX_MSG_ID_AKESTOREDKM		= 3,
	XHDCP2X_RX_MSG_ID_AKESENDHPRIME		= 4,
	XHDCP2X_RX_MSG_ID_AKESENDPAIRINGINFO	= 5,
	XHDCP2X_RX_MSG_ID_LCINIT		= 6,
	XHDCP2X_RX_MSG_ID_LCSENDLPRIME		= 7,
	XHDCP2X_RX_MSG_ID_SKESENDEKS		= 8,
};

enum xhdcp2x_rx_error_flags {
	XHDCP2X_RX_ERROR_FLAG_NONE			= 0,
	XHDCP2X_RX_ERROR_FLAG_MESSAGE_SIZ		= 1,
	XHDCP2X_RX_ERROR_FLAG_FORCE_RESET		= 2,
	XHDCP2X_RX_ERROR_FLAG_PROCESSING_AKEINIT	= 4,
	XHDCP2X_RX_ERROR_FLAG_PROCESSING_AKENOSTOREDKM	= 8,
	XHDCP2X_RX_ERROR_FLAG_PROCESSING_AKESTOREDKM	= 16,
	XHDCP2X_RX_ERROR_FLAG_PROCESSING_LCINIT		= 32,
	XHDCP2X_RX_ERROR_FLAG_PROCESSING_SKESENDEKS	= 64,
	XHDCP2X_RX_ERROR_FLAG_LINK_INTEGRITY		= 512,
	XHDCP2X_RX_ERROR_FLAG_MAX_LCINIT_ATTEMPTS	= 2048,
};

enum xhdcp2x_rx_dpcd_flag {
	XHDCP2X_RX_DPCD_FLAG_NONE,
	XHDCP2X_RX_DPCD_AKE_INIT_RCVD		= 0x001,
	XHDCP2X_RX_DPCD_AKE_NO_STORED_KM_RCVD	= 0x002,
	XHDCP2X_RX_DPCD_AKE_STORED_KM_RCVD	= 0x004,
	XHDCP2X_RX_DPCD_LC_INIT_RCVD		= 0x008,
	XHDCP2X_RX_DPCD_SKE_SEND_EKS_RCVD	= 0x010,
	XHDCP2X_RX_DPCD_HPRIME_READ_DONE_RCVD	= 0x020,
	XHDCP2X_RX_DPCD_PAIRING_DONE_RCVD	= 0x040,
	XHDCP2X_RX_TIMER_EVENT			= 0x200,
};

struct xhdcp2x_rx_kpriv_rx {
	u8 p[HDCP_2_2_K_PRIV_RX_LEN];
	u8 q[HDCP_2_2_K_PRIV_RX_LEN];
	u8 dp[HDCP_2_2_K_PRIV_RX_LEN];
	u8 dq[HDCP_2_2_K_PRIV_RX_LEN];
	u8 qinv[HDCP_2_2_K_PRIV_RX_LEN];
};

struct xhdcp2x_rx_kpub_rx {
	u8 n[HDCP_2_2_E_KPUB_KM_LEN];
	u8 e[HDCP_2_2_K_PUB_RX_EXP_E_LEN];
};

struct xhdcp2x_rx_certrx {
	u8 receiverid[HDCP_2_2_RECEIVER_ID_LEN];
	u8 kpubrx[HDCP_2_2_K_PUB_RX_LEN];
	u8 reserved[HDCP_2_2_CERTRX_RESERVED];
	u8 signature[HDCP_2_2_DCP_LLC_SIG_LEN];
};

struct xhdcp2x_rx_ake_init {
	u8 msgid;
	u8 rtx[HDCP_2_2_RTX_LEN];
	u8 txcaps[HDCP_2_2_TX_CAPS];
};

struct xhdcp2x_rx_ake_send_cert {
	u8 msgid;
	u8 certrx[HDCP_2_2_CERTRX];
	u8 rrx[HDCP_2_2_RRX_LEN];
	u8 rxcaps[HDCP_2_2_RXCAPS_LEN];
};

struct xhdcp2x_rx_ake_no_stored_km {
	u8 msgid;
	u8 ekpubkm[HDCP_2_2_E_KPUB_KM_LEN];
};

struct xhdcp2x_rx_ake_stored_km {
	u8 msgid;
	u8 ekhkm[HDCP_2_2_E_KH_KM_LEN];
	u8 m[HDCP_2_2_E_KH_KM_LEN];
};

struct xhdcp2x_rx_ake_send_hprime {
	u8 msgid;
	u8 hprime[HDCP_2_2_H_PRIME_LEN];
};

struct xhdcp2x_rx_ake_send_pairing_info {
	u8 msgid;
	u8 ekhkm[HDCP_2_2_E_KH_KM_LEN];
};

struct xhdcp2x_rx_lc_init {
	u8 msgid;
	u8 rn[HDCP_2_2_RN_LEN];
};

struct xhdcp2x_rx_lc_send_lprime {
	u8 msgid;
	u8 lprime[HDCP_2_2_L_PRIME_LEN];
};

struct xhdcp2x_rx_ske_send_eks {
	u8 msgid;
	u8 edkeyks[HDCP_2_2_E_DKEY_KS_LEN];
	u8 riv[HDCP_2_2_RIV_LEN];
};

union xhdcp2x_rx_message {
	u8 msgid;
	struct xhdcp2x_rx_ake_init			ake_init;
	struct xhdcp2x_rx_ake_send_cert			ake_send_cert;
	struct xhdcp2x_rx_ake_no_stored_km		ake_no_storedkm;
	struct xhdcp2x_rx_ake_stored_km			ake_storedkm;
	struct xhdcp2x_rx_ake_send_hprime		ake_send_hprime;
	struct xhdcp2x_rx_ake_send_pairing_info		ake_send_pairinginfo;
	struct xhdcp2x_rx_lc_init			lc_init;
	struct xhdcp2x_rx_lc_send_lprime		lc_send_lprime;
	struct xhdcp2x_rx_ske_send_eks			ske_sendeks;
};

enum xdprxss_hdcp_protocol {
	XDPRXSS_HDCP_NONE = 0,
	XDPRXSS_HDCP_14 = 1,
	XDPRXSS_HDCP_22 = 2,
	XDPRXSS_HDCP_BOTH = 3
};

enum xhdcp2x_rx_protocol {
	XHDCP2X_NONE = 0,
	XHDCP2X_RX_DP = 1,
	XHDCP2X_RX_HDMI = 2
};

enum xhdcp2x_rx_mode {
	xhdcp2x_rx_receiver = 0,
	xhdcp2x_rx_repeater = 1,
	xhdcp2x_rx_converter = 2
};

struct xhdcp2x_rx_callbacks {
	int (*rd_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	int (*wr_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	int (*cp_irq_handler)(void *interface_ref);
	void (*notify_handler)(void *interface_ref, u32 notification);
};

enum xhdcp2x_rx_notification_type {
	XHDCP2X_RX_NOTIFY_AUTHENTICATED = 1,
	XHDCP2X_RX_NOTIFY_UN_AUTHENTICATED = 2,
	XHDCP2X_RX_NOTIFY_RE_AUTHENTICATE = 3,
	XHDCP2X_RX_NOTIFY_ENCRYPTION_DONE = 4,
	XHDCP2X_RX_NOTIFY_SKE_SEND_EKS = 5

};

enum xhdcp2x_rx_handler_type {
	XHDCP2X_RX_HANDLER_DP_AUX_READ = 1,
	XHDCP2X_RX_HANDLER_DP_AUX_WRITE = 2,
	XHDCP2X_RX_HANDLER_DP_CP_IRQ_SET = 3,
	XHDCP2X_RX_NOTIFICATION_HANDLER = 4
};

/**
 * struct xlnx_hdcp2x_hw - HDCP2X subsystem configuration structure
 * @cipher_inst: HDCP2X cipher engine configuration
 * @rng_inst: HDCP2X random number generator configuration
 * @mmult_inst: HDCP2X montgomery multiplier configuration
 * @hdcp2xcore_address: HDCP2X core address
 * @hdcprx_mutex: mutex for hdcp state machine
 * @rx_mode: HDCP receiver
 * @protocol: Protocol type, DP or HDMI
 */
struct xlnx_hdcp2x_hw {
	struct xlnx_hdcp2x_cipher_hw cipher_inst;
	struct xlnx_hdcp2x_rng_hw rng_inst;
	struct xlnx_hdcp2x_mmult_hw mmult_inst;
	void __iomem *hdcp2xcore_address;
/* mutex for hdcp state machine */
	struct mutex hdcprx_mutex;
	u8 rx_mode;
	u8 protocol;
};

enum xhdcp2x_rx_state {
	XHDCP2X_STATE_B0 = 0,
	XHDCP2X_STATE_B1 = 1,
	XHDCP2X_STATE_B2 = 2,
	XHDCP2X_STATE_B3 = 3,
	XHDCP2X_STATE_B4 = 4,
	XHDCP2X_RX_NUM_STATES = 5
};

enum xhdcp2x_rx_state_type {
	XHDCP2X_RX_STATE_UNDEFINED			= 0x000,
	XHDCP2X_RX_STATE_B0_WAIT_AKEINIT		= 0xB00,
	XHDCP2X_RX_STATE_B1_SEND_AKESENDCERT		= 0xB10,
	XHDCP2X_RX_STATE_B1_WAIT_AKEKM			= 0xB11,
	XHDCP2X_RX_STATE_B1_SEND_AKESENDHPRIME		= 0xB12,
	XHDCP2X_RX_STATE_B1_SEND_AKESENDPAIRINGINFO	= 0xB13,
	XHDCP2X_RX_STATE_B1_WAIT_LCINIT			= 0xB14,
	XHDCP2X_RX_STATE_B2_SEND_LCSENDLPRIME		= 0xB20,
	XHDCP2X_RX_STATE_B2_WAIT_SKESENDEKS		= 0xB21,
	XHDCP2X_RX_STATE_B3_COMPUTE_KS			= 0xB30,
	XHDCP2X_RX_STATE_B4_AUTHENTICATED		= 0xB40,
	XHDCP2X_RX_STATE_INVALID
};

enum xhdcp2x_rx_authentication_type {
	XHDCP2X_RX_UNAUTHENTICATED = 0,
	XHDCP2X_RX_AUTHENTICATION_BUSY = 1,
	XHDCP2X_RX_AUTHENTICATED = 2,
	XHDCP2X_RX_REAUTH_REQUESTED = 3
};

struct xhdcp2x_rx_parameters {
	u8 hprime[XHDCP2X_RX_HPRIME_SIZE];
	u8 lprime[XHDCP2X_RX_LPRIME_SIZE];
	u8 vprime[XHDCP2X_RX_VPRIME_SIZE];
	u8 mprime[XHDCP2X_RX_MPRIME_SIZE];
	u8 ekh[XHDCP2X_RX_EKH_SIZE];
	u8 km[XHDCP2X_RX_KM_SIZE];
	u8 ks[XHDCP2X_RX_KS_SIZE];
	u8 rtx[R_TX_SIZE];
	u8 rrx[R_RX_SIZE];
	u8 rn[R_N_SIZE];
	u8 riv[R_IV_SIZE];
	u8 txcaps[XHDCP2X_RX_TXCAPS_SIZE];
	u8 rxcaps[XHDCP2X_RX_RXCAPS_SIZE];
	u8 seqnumm[XHDCP2X_RX_SEQNUMM_SIZE];
	u8 streamidtype[XHDCP2X_RX_STREAMID_SIZE];
};

struct xhdcp2x_rx_info {
	u32 error_flag;
	u32 error_flag_sticky;
	u32 timer_initial_ticks;
	u32 seq_numv;
	u32 auth_request_cnt;
	u32 reauth_request_cnt;
	u32 link_error_cnt;
	u32 msg_event;
	u16 lc_init_attempts;
	u8 is_enabled;
	u8 is_no_storedkm;
	u8 reauth_req;
	u8 timer_expired;
	u8 timer_reason_id;
	u8 has_stream_management_info;
	u8 skipread;
	u8 is_encrypted;
	enum xhdcp2x_rx_state_type sub_state;
	enum xhdcp2x_rx_state_type return_state;
	enum xhdcp2x_rx_authentication_type authentication_status;
};

/**
 * @dev: device information.
 * @xhdcp2x_hw: HDCP2x hardware configuration.
 * @info: information.
 * @param: HDCP2x Rx parameters.
 * @tmr_config: timer configuration.
 * @sm_work: state machine workqueue.
 * @hdcprx_mutex: mutex for hdcp state machine.
 * @handlers: callback handlers.
 * @nprimep: primep key size.
 * @nprimeq: primeq key size.
 * @rx_caps: rx caps.
 * @msg_buffer: message buffer used during authentication.
 * @lc128key: user shared key.
 * @is_repeater: repeater functionality.
 * @lane_count: number of protocol lanes.
 * @publiccertptr: public certificate.
 * @keys_loaded: user key status flag.
 * @privatekeypre: private key provided by user.
 * @msg_size: message size during authentication.
 * @error_flags: error flags used during authentication.
 * @protocol: Interface protocol HDMI or DP.
 * @mode: different modes of operation for HDCP.
 * @auth_status: status flag used by state machine.
 * @hdcp_protocol: hdcp protocol support
 * @curr_state: current state defined for state machine
 * @prev_state: previous state defined for state machine
 * @next_state: next state defined for state machine
 * @interface_ref: Interface reference
 * @interface_base: Interface base
 */
struct xlnx_hdcp2x_config {
	struct device *dev;
	struct xlnx_hdcp2x_hw xhdcp2x_hw;
	struct xhdcp2x_rx_info info;
	struct xhdcp2x_rx_parameters param;
	struct xlnx_hdcp_timer_config tmr_config;
	struct delayed_work sm_work;
/* mutex for hdcp state machine */
	struct mutex hdcprx_mutex;
	struct xhdcp2x_rx_callbacks handlers;
	u8 nprimep[XHDCP2X_RX_P_SIZE];
	u8 nprimeq[XHDCP2X_RX_P_SIZE];
	u8 rx_caps[XHDCP2X_RX_RXCAPS_SIZE];
	u8 msg_buffer[XHDCP2X_RX_MAX_MESSAGE_SIZE];
	u8 *lc128key;
	u8 is_repeater;
	u8 lane_count;
	u8 *publiccertptr;
	u8 keys_loaded;
	const u8 *privatekeyptr;
	int msg_size;
	enum xhdcp2x_rx_error_flags error_flags;
	enum xhdcp2x_rx_protocol protocol;
	enum xhdcp2x_rx_mode mode;
	enum xhdcp2x_rx_authentication_type auth_status;
	enum xdprxss_hdcp_protocol hdcp_protocol;
	enum xhdcp2x_rx_state curr_state;
	enum xhdcp2x_rx_state prev_state;
	enum xhdcp2x_rx_state next_state;
	void *interface_ref;
	void __iomem *interface_base;
	void *mmult;
};

struct xlnx_hdcp_timer_config *xhdcp2x_rx_get_timer(struct xlnx_hdcp2x_config *xhdcp2x_rx);

u8 xhdcp2x_rx_get_content_stream_type(struct xlnx_hdcp2x_config *xhdcp2x_rx);

int *xhdcp2x_rx_init(struct device *dev, void *protocol_ref, void __iomem *hdcp_base_address,
		     enum xhdcp2x_rx_protocol protocol_rx, bool is_repeater,
		     u8 lane_count);
int xhdcp2x_rx_enable(struct xlnx_hdcp2x_config *xhdcp2x_rx, u8 lane_count);
int xhdcp2x_rx_disable(struct xlnx_hdcp2x_config *xhdcp2x_rx);
int xhdcp2x_rx_reset(struct xlnx_hdcp2x_config *xhdcp2x_rx);
int xhdcp2x_rx_poll(struct xlnx_hdcp2x_config *xhdcp2x_rx);
int xhdcp2x_rx_set_callback(void *ref, u32 handlertype, void *callback_func);
int xhdcp2x_rx_rsaes_oaep_decrypt(struct xlnx_hdcp2x_config *xhdcp2x_rx,
				  struct xhdcp2x_rx_kpriv_rx *kprivrx,
				  u8 *encryptedmessage, u8 *message, int *messagelen);
int xhdcp2x_rx_push_events(void *ref, u32 events);
int xhdcp2x_rx_set_key(void *ref, void *hdcp2x_lc128, void *hdcp2x_private);
int xhdcp2x_rx_calc_mont_nprime(void *ref, u8 *nprime, const u8 *n, int ndigits);

void xhdcp2x_rx_generate_random(struct xlnx_hdcp2x_config *xhdcp2x_rx,
				int num_octets, u8 *random_number_ptr);
void xhdcp2x_timer_attach(struct xlnx_hdcp2x_config *xhdcp2x_rx,
			  struct xlnx_hdcp_timer_config *tmrcntr);
void *xhdcp2x_timer_init(struct device *dev, void __iomem *interface_base);
void xhdcp2x_rx_timer_handler(void *callbackref, u8 tmrcntnumber);
void xhdcp2x_rx_set_stream_type(struct xlnx_hdcp2x_config *xhdcp2x_rx);
void xhdcp2x_rx_compute_hprime(const u8 *rrx, const u8 *rxcaps,
			       const u8 *rtx,  const u8 *txcaps, const u8 *km, u8 *hprime);
void xhdcp2x_rx_compute_ekh(const u8 *kprivrx, const u8 *km, const u8 *m, u8 *ekh);
void xhdcp2x_rx_compute_lprime(const u8 *rn, const u8 *km, const u8 *rrx,
			       const u8 *rtx, u8 *lprime);
void xhdcp2x_rx_compute_ks(const u8 *rrx, const u8 *rtx, const u8 *km,
			   const u8 *rn, const u8 *eks, u8 *ks);
void xhdcp2x_rx_compute_vprime(const u8 *receiveridlist, u32 receiveridlistsize, const u8 *rxinfo,
			       const u8 *seqnumv, const u8 *km, const u8 *rrx,
			       const u8 *rtx, u8 *vprime);
void xhdcp2x_rx_compute_mprime(const u8 *streamidtype, const u8 *seqnumm, const u8 *km,
			       const u8 *rrx, const u8 *rtx, u8 *mprime);

#endif /* __XILINX_HDCP2X_RX_H__ */
