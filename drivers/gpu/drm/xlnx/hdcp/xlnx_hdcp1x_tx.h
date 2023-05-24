/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Specific HDCP1X driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 *
 */
#include <drm/display/drm_hdcp.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xhdcp1x_tx.h"

/* Basic configuration enable/disable list of macros */
#define XHDCP1X_ENABLE			1
#define XHDCP1X_DISABLE			0
#define XHDCP1X_DEFAULT_INIT		0
#define XHDCP1X_ENCRYPTION_EN		1
#define XHDCP1X_ENCRYPTION_DISABLE	0
#define XHDCP1X_MAX_RETRIES		3
#define XHDCP1X_BYTE_IN_BITS		8
#define XHDCP1X_REMOTE_BKSV_SIZE	5
#define XHDCP1X_REMOTE_INFO_BCAPS_VAL	1
#define XHDCP1X_REMOTE_RO_SIZE		2
#define XHDCP1X_KSV_NUM_OF_1S		20
#define XHDCP1X_RO_AVILABLE_DELAY	100
#define XHDCP1X_VERSION			(0x01u)
#define XHDCP1X_STREAM_MAP		(0x01)
#define XHDCP1X_BUF_OFFSET_LEN		(0x100)
/* These constants specify the offsets for the various fields and/or
 * attributes within the hdcp port
 */
#define XHDCP1X_PORT_OFFSET_BKSV	(0x00u)   /* Bksv Offset        */
#define XHDCP1X_PORT_OFFSET_RO		(0x05u)   /* R0' Offset         */
#define XHDCP1X_PORT_OFFSET_AKSV	(0x07u)   /* Aksv Offset        */
#define XHDCP1X_PORT_OFFSET_AN		(0x0Cu)   /* An Offset          */
#define XHDCP1X_PORT_OFFSET_VH0		(0x14u)   /* V'.H0 Offset       */
#define XHDCP1X_PORT_OFFSET_VH1		(0x18u)   /* V'.H1 Offset       */
#define XHDCP1X_PORT_OFFSET_VH2		(0x1Cu)   /* V'.H2 Offset       */
#define XHDCP1X_PORT_OFFSET_VH3		(0x20u)   /* V'.H3 Offset       */
#define XHDCP1X_PORT_OFFSET_VH4		(0x24u)   /* V'.H4 Offset       */
#define XHDCP1X_PORT_OFFSET_BCAPS	(0x28u)   /* Bcaps Offset       */
#define XHDCP1X_PORT_OFFSET_BSTATUS	(0x29u)   /* Bstatus Offset     */
#define XHDCP1X_PORT_OFFSET_BINFO	(0x2Au)   /* Binfo Offset       */
#define XHDCP1X_PORT_OFFSET_KSVFIFO	(0x2Cu)   /* KSV FIFO Offset    */
#define XHDCP1X_PORT_OFFSET_AINFO	(0x3Bu)   /* Ainfo Offset       */
#define XHDCP1X_PORT_OFFSET_DBG		(0xC0u)   /* Debug Space Offset */
#define XHDCP1X_PORT_HDCP_RESET_KSV	(0xD0u)   /* KSV FIFO Read pointer reset Offset */
/*
 * These constants specify the sizes for the various fields and/or
 * attributes within the hdcp port
 */
#define XHDCP1X_PORT_SIZE_BKSV		(0x05u)   /* Bksv Size          */
#define XHDCP1X_PORT_SIZE_RO		(0x02u)   /* R0' Size           */
#define XHDCP1X_PORT_SIZE_AKSV		(0x05u)   /* Aksv Size          */
#define XHDCP1X_PORT_SIZE_AN		(0x08u)   /* An Size            */
#define XHDCP1X_PORT_SIZE_VH0		(0x04u)   /* V'.H0 Size         */
#define XHDCP1X_PORT_SIZE_VH1		(0x04u)   /* V'.H1 Size         */
#define XHDCP1X_PORT_SIZE_VH2		(0x04u)   /* V'.H2 Size         */
#define XHDCP1X_PORT_SIZE_VH3		(0x04u)   /* V'.H3 Size         */
#define XHDCP1X_PORT_SIZE_VH4		(0x04u)   /* V'.H4 Size         */
#define XHDCP1X_PORT_SIZE_BCAPS		(0x01u)   /* Bcaps Size         */
#define XHDCP1X_PORT_SIZE_BSTATUS	(0x01u)   /* Bstatus Size       */
#define XHDCP1X_PORT_SIZE_BINFO		(0x02u)   /* Binfo Size         */
#define XHDCP1X_PORT_SIZE_KSVFIFO	(0x0Fu)   /* KSV FIFO Size      */
#define XHDCP1X_PORT_SIZE_AINFO		(0x01u)   /* Ainfo Offset       */
#define XHDCP1X_PORT_SIZE_DBG		(0x40u)   /* Debug Space Size   */
#define XHDCP1X_PORT_SIZE_HDCP_RESET_KSV (0x40u)  /* KSV FIFO pointer reset Size  */
/*
 * These constants specify the bit definitions within the various fields
 * and/or attributes within the hdcp port
 */
#define XHDCP1X_PORT_BIT_BSTATUS_READY		BIT(0) /* BStatus Ready Mask */
#define XHDCP1X_PORT_BIT_BSTATUS_RO_AVAILABLE	BIT(1) /* BStatus Ro available Mask */
#define XHDCP1X_PORT_BIT_BSTATUS_LINK_FAILURE	BIT(2) /* BStatus Link Failure Mask  */
#define XHDCP1X_PORT_BIT_BSTATUS_REAUTH_REQUEST	BIT(3) /* BStatus Reauth Request Mask  */
#define XHDCP1X_PORT_BIT_BCAPS_HDCP_CAPABLE	BIT(0) /* BCaps HDCP Capable Mask  */
#define XHDCP1X_PORT_BIT_BCAPS_REPEATER		BIT(1) /* BCaps HDCP Repeater Mask */
#define XHDCP1X_PORT_BIT_AINFO_REAUTH_ENABLE_IRQ	BIT(0) /**< Ainfo Reauth Enable Mask  */
#define XHDCP1X_PORT_HDCP_RESET_KSV_RST		BIT(0) /* KSV FIFO pointer Reset Mask    */
#define XHDCP1X_PORT_BINFO_BIT_DEV_CNT_ERR	BIT(7) /* BInfo Device Count Error Mask */
/* BInfo Device Count for No Error Mask */
#define XHDCP1X_PORT_BINFO_BIT_DEV_CNT_NO_ERR	(0u << 7)
#define XHDCP1X_PORT_BINFO_DEV_CNT_MASK		(0x7F) /* BInfo Device Count Error Mask */
#define XHDCP1X_PORT_BINFO_BIT_DEPTH_ERR	BIT(11) /* BInfo Depth Error Mask    */
/* BInfo Depth Error for No Error Mask */
#define XHDCP1X_PORT_BINFO_BIT_DEPTH_NO_ERR	(0u << 11)
#define XHDCP1X_PORT_BINFO_DEV_CNT_ERR_SHIFT	(7) /* BStatus Device Count Error Shift Mask */
#define XHDCP1X_PORT_BINFO_DEPTH_ERR_SHIFT	(11) /* BStatus Depth Error Shift Mask */
#define XHDCP1X_PORT_BINFO_DEPTH_SHIFT		(8) /* BInfo Device Count Error Mask */
/*
 * This constant defines the base address of the hdcp port within the DPCD
 * address space
 */
#define XHDCP1X_PORT_DPCD_BASE		(0x68000u)   /* Base Addr in DPCD */

/*
 * struct xhdcp1x_tx_callbacks - This structure contains the callback handlers.
 * @rd_handler: The DP/HDMI read handler call for the aux channels
 * @wr_handler: The DP/HDMI write handler call for the aux channels
 * @notify_handler: The DP/HDMI notify handler call for the aux channels
 */
struct xhdcp1x_tx_callbacks {
	int (*rd_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	int (*wr_handler)(void *interface_ref, u32 offset, u8 *buf, u32 size);
	void (*notify_handler)(void *interface_ref, u32 notification);
};

enum xlnx_hdcp1x_tx_callback_type {
	XHDCP1X_TX_HANDLER_DP_AUX_READ = 0,
	XHDCP1X_TX_HANDLER_DP_AUX_WRITE = 1,
	XHDCP1X_TX_HANDLER_HDCP_STATUS = 2,
	XHDCP1X_TX_HANDLER_INVALID = 3
};

enum xhdcp1x_tx_authtype {
	XHDCP1X_TX_AUTHENTICATED = 0,
	XHDCP1X_TX_UNAUTHENTICATED,
	XHDCP1X_TX_INCOMPATIBLE_RX,
	XHDCP1X_TX_AUTHENTICATION_BUSY,
	XHDCP1X_TX_REAUTHENTICATE_REQUESTED,
	XHDCP1X_TX_DEVICE_IS_REVOKED,
};

enum xhdcp1x_tx_protocol {
	XHDCP1X_TX_DP = 0,
	XHDCP1X_TX_HDMI = 1
};

/*
 * struct xhdcp1x_tx_status - This structure contains Hdcp1x driver
 * status information fields
 * @auth_failed: Authentication failures count
 * @auth_passed: Authentication passed status/count
 * @reauth_requested: Re-Authentication request if any link failures..etc
 * @read_failure: remote device read failures
 * @link_checkpassed: Link verification that is passed
 * @link_checkfailed: Link verification that is failure
 *
 */
struct xhdcp1x_tx_status {
	u32 auth_status;
	u32 auth_failed;
	u32 auth_passed;
	u32 reauth_requested;
	u32 read_failure;
	u32 link_checkpassed;
	u32 link_checkfailed;
};

/*
 * struct xlnx_hdcp1x_config - This structure contains Hdcp1x driver
 * configuration information
 * @dev: device information
 * @handlers: Callback handlers
 * @sm_work: state machine worker
 * @curr_state: current authentication state
 * @prev_sate: Previous Authentication State
 * @repeatervalues: The downstream repeater capabilities
 * @stats: authentication status
 * @hdcp1x_keymgmt_base: Key management base address
 * @cipher: Pointer to cipher driver instance
 * @interface_ref: Pointer to interface(DP/HDMI) driver instance
 * @interface_base: Pointer to instance iomem base
 * @pending_events: Evenets that are set by interface driver
 * @downstreamready: To check the downstream device status ready or not
 * @is_repeater: says whether downstream is repeater or receiver
 * @hdcp1x_key_availble: The KMS block has key exists or not.
 * @lane_count: number of lanes data to be encrypted
 * @hdcp1x_key: hdcp1x key pointer
 * @auth_status: first stage authentication status
 * @keyinit: Key Management Block with key initiliazed properly or not
 * @is_encryption_en: Encryption enbalemnet is done or not
 * @is_cipher: is cipher init is done or not
 * @state_helper: to store the An value temp basis
 * @encryption_map: To check the encryption progress
 *
 */
struct xlnx_hdcp1x_config {
	struct device *dev;
	struct xhdcp1x_tx_callbacks handlers;
	struct delayed_work	sm_work;
	enum hdcp1x_tx_state	curr_state;
	enum hdcp1x_tx_state	prev_state;
	struct xhdcp1x_tx_status stats;
	struct regmap *hdcp1x_keymgmt_base;
	void	*cipher;
	void	*interface_ref;
	void __iomem	*interface_base;
	u32	pending_events;
	u32	downstreamready;
	bool	is_repeater;
	bool	hdcp1x_key_available;
	u8	lane_count;
	u8	*hdcp1x_key;
	u8	auth_status;
	u8	keyinit;
	u8	is_encryption_en;
	u8	is_enabled;
	u8	is_cipher;
	u64	state_helper;
	u64	encryption_map;
};

void xlnx_start_hdcp1x_engine(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, bool is_repeater);
int xlnx_hdcp1x_task_monitor(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_downstream_capbility(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_check_rxcapable(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_read_bksv_from_remote(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 offset,
				       void *buf, u32 buf_size);
bool xlnx_hdcp1x_exchangeksvs(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_computationsstate(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_validaterxstate(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_is_ksvvalid(u64 ksv);
u64 xlnx_hdcp1x_tx_generate_an(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xhdcp1x_tx_set_keyselect(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 keyselect);
int xhdcp1x_tx_load_aksv(struct xlnx_hdcp1x_config *xhdcp1x_tx);
void xlnx_hdcp1x_tx_disable(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_tx_reset(struct xlnx_hdcp1x_config *xhdcp1x_tx);
void xlnx_hdcp1x_tx_enable_encryption(struct xlnx_hdcp1x_config *xhdcp1x_tx);
void xlnx_hdcp1x_tx_disable_encryption(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				       u64 stream_map);
int hdcp1x_tx_protocol_authenticate_sm(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_tx_test_for_repeater(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_tx_read_ksv_list(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_setrepeaterinfo(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_tx_validate_ksv_list(struct xlnx_hdcp1x_config *xhdcp1x_tx, u16 repeaterinfo);
int xlnx_hdcp1x_tx_wait_for_ready(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_get_repeater_info(struct xlnx_hdcp1x_config *xhdcp1x_tx, u16 *info);
int xlnx_hdcp1x_set_keys(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data);
int xlnx_hdcp1x_keymngt_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data);
