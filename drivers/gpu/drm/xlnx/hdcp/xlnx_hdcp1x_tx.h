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
#define XHDCP1X_TX_REVOCLIST_MAX_DEVICES	944
#define XHDCP1X_TX_ENCRYPTION_KEY_SIZE		336
#define XHDCP1X_PORT_MIN_BYTES 3
/* These constants specify the offsets for the various fields and/or
 * attributes within the hdcp port
 */
#define XDPTX_HDCP1X_DPCD_OFFSET	0x68000
#define XHDCP1X_PORT_OFFSET_BKSV	(0x00 + XDPTX_HDCP1X_DPCD_OFFSET)   /* Bksv Offset        */
#define XHDCP1X_PORT_OFFSET_RO		(0x05 + XDPTX_HDCP1X_DPCD_OFFSET)   /* R0' Offset         */
#define XHDCP1X_PORT_OFFSET_AKSV	(0x07 + XDPTX_HDCP1X_DPCD_OFFSET)   /* Aksv Offset        */
#define XHDCP1X_PORT_OFFSET_AN		(0x0C + XDPTX_HDCP1X_DPCD_OFFSET)   /* An Offset          */
#define XHDCP1X_PORT_OFFSET_VH0		(0x14 + XDPTX_HDCP1X_DPCD_OFFSET)   /* V'.H0 Offset       */
#define XHDCP1X_PORT_OFFSET_VH1		(0x18 + XDPTX_HDCP1X_DPCD_OFFSET)   /* V'.H1 Offset       */
#define XHDCP1X_PORT_OFFSET_VH2		(0x1C + XDPTX_HDCP1X_DPCD_OFFSET)   /* V'.H2 Offset       */
#define XHDCP1X_PORT_OFFSET_VH3		(0x20 + XDPTX_HDCP1X_DPCD_OFFSET)   /* V'.H3 Offset       */
#define XHDCP1X_PORT_OFFSET_VH4		(0x24 + XDPTX_HDCP1X_DPCD_OFFSET)   /* V'.H4 Offset       */
#define XHDCP1X_PORT_OFFSET_BCAPS	(0x28 + XDPTX_HDCP1X_DPCD_OFFSET)   /* Bcaps Offset       */
#define XHDCP1X_PORT_OFFSET_BSTATUS	(0x29 + XDPTX_HDCP1X_DPCD_OFFSET)   /* Bstatus Offset     */
#define XHDCP1X_PORT_OFFSET_BINFO	(0x2A + XDPTX_HDCP1X_DPCD_OFFSET)   /* Binfo Offset       */
#define XHDCP1X_PORT_OFFSET_KSVFIFO	(0x2C + XDPTX_HDCP1X_DPCD_OFFSET)   /* KSV FIFO Offset    */
#define XHDCP1X_PORT_OFFSET_AINFO	(0x3B + XDPTX_HDCP1X_DPCD_OFFSET)   /* Ainfo Offset       */
#define XHDCP1X_PORT_OFFSET_DBG		(0xC0 + XDPTX_HDCP1X_DPCD_OFFSET)   /* Debug Space Offset */
#define XHDCP1X_PORT_HDCP_RESET_KSV	(0xD0 + XDPTX_HDCP1X_DPCD_OFFSET)   /* KSV FIFO Read pointer
									     * reset Offset
									     */
/*
 * These constants specify the sizes for the various fields and/or
 * attributes within the hdcp port for HDMI Interface
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
#define XHDCP1X_PORT_BINFO_DEPTH_MASK		0x0700 /* BIndo Depth Mask */
#define XHDCP1X_PORT_BINFO_BIT_DEPTH_ERR	BIT(11) /* BInfo Depth Error Mask    */
/* BInfo Depth Error for No Error Mask */
#define XHDCP1X_PORT_BINFO_BIT_DEPTH_NO_ERR	(0u << 11)
#define XHDCP1X_PORT_BINFO_DEV_CNT_ERR_SHIFT	(7) /* BStatus Device Count Error Shift Mask */
#define XHDCP1X_PORT_BINFO_DEPTH_ERR_SHIFT	(11) /* BStatus Depth Error Shift Mask */
#define XHDCP1X_PORT_BINFO_DEPTH_SHIFT		(8) /* BInfo Device Count Error Mask */
#define XHDCP1X_PORT_BINFO_VALUE		GENMASK(12, 0)
/*
 * These constants specify the sizes for the various fields and/or
 * attributes within the hdcp port for HDMI interface
 */
#define XHDMI_HDCP1X_PORT_SIZE_BKSV		(0x05u)   /**< Bksv Size          */
#define XHDMI_HDCP1X_PORT_SIZE_RO		(0x02u)   /**< Ri' Size           */
#define XHDMI_HDCP1X_PORT_SIZE_PJ		(0x01u)   /**< Pj' Size           */
#define XHDMI_HDCP1X_PORT_SIZE_AKSV		(0x05u)   /**< Aksv Size          */
#define XHDMI_HDCP1X_PORT_SIZE_AINFO		(0x01u)   /**< Ainfo Size         */
#define XHDMI_HDCP1X_PORT_SIZE_AN		(0x08u)   /**< An Size            */
#define XHDMI_HDCP1X_PORT_SIZE_VH0		(0x04u)   /**< V'.H0 Size         */
#define XHDMI_HDCP1X_PORT_SIZE_VH1		(0x04u)   /**< V'.H1 Size         */
#define XHDMI_HDCP1X_PORT_SIZE_VH2		(0x04u)   /**< V'.H2 Size         */
#define XHDMI_HDCP1X_PORT_SIZE_VH3		(0x04u)   /**< V'.H3 Size         */
#define XHDMI_HDCP1X_PORT_SIZE_VH4		(0x04u)   /**< V'.H4 Size         */
#define XHDMI_HDCP1X_PORT_SIZE_BCAPS		(0x01u)   /**< Bcaps Size         */
#define XHDMI_HDCP1X_PORT_SIZE_BSTATUS		(0x02u)   /**< Bstatus Size       */
#define XHDMI_HDCP1X_PORT_SIZE_KSVFIFO		(0x01u)   /**< KSV FIFO Size      */
#define XHDMI_HDCP1X_PORT_SIZE_DBG		(0xC0u)   /**< Debug Space Size   */
/*
 * These constants specify the offsets for the various fields and/or
 * attributes within the hdcp port for HDMI interface
 */
#define XHDMI_HDCP1X_PORT_OFFSET_BKSV		(0x00u)   /**< Bksv Offset        */
#define XHDMI_HDCP1X_PORT_OFFSET_RO		(0x08u)   /**< Ri'/Ro' Offset     */
#define XHDMI_HDCP1X_PORT_OFFSET_PJ		(0x0Au)   /**< Pj' Offset         */
#define XHDMI_HDCP1X_PORT_OFFSET_AKSV		(0x10u)   /**< Aksv Offset        */
#define XHDMI_HDCP1X_PORT_OFFSET_AINFO		(0x15u)   /**< Ainfo Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_AN		(0x18u)   /**< An Offset          */
#define XHDMI_HDCP1X_PORT_OFFSET_VH0		(0x20u)   /**< V'.H0 Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_VH1		(0x24u)   /**< V'.H1 Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_VH2		(0x28u)   /**< V'.H2 Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_VH3		(0x2Cu)   /**< V'.H3 Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_VH4		(0x30u)   /**< V'.H4 Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_BCAPS		(0x40u)   /**< Bcaps Offset       */
#define XHDMI_HDCP1X_PORT_OFFSET_BSTATUS	(0x41u)   /**< Bstatus Offset     */
#define XHDMI_HDCP1X_OFFSET_KSVFIFO		(0x43u)   /**< KSV FIFO Offset    */
#define XHDMI_HDCP1X_PORT_OFFSET_DBG		(0xC0u)   /**< Debug Space Offset */

#define XHDMI_HDCP1X_PORT_BIT_BSTATUS_HDMI_MODE         BIT(12)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_FAST_REAUTH         BIT(0)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_1d1_FEATURES        BIT(1)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_FAST                BIT(4)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_READY               BIT(5)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_REPEATER            BIT(6)
#define XHDMI_HDCP1X_PORT_BIT_BCAPS_HDMI                BIT(7)
#define XHDMI_HDCP1X_PORT_BIT_AINFO_ENABLE_1d1_FEATURES BIT(1)
#define XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEV_CNT_ERR       BIT(7)
#define XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEV_CNT_NO_ERR    (0u << 7)
#define XHDMI_HDCP1X_PORT_BSTATUS_DEV_CNT_MASK          (0x7F)
#define XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEPTH_ERR         BIT(11)
#define XHDMI_HDCP1X_PORT_BSTATUS_BIT_DEPTH_NO_ERR      (0u << 11)
#define XHDMI_HDCP1X_PORT_BSTATUS_DEV_CNT_ERR_SHIFT     (7)
#define XHDMI_HDCP1X_PORT_BSTATUS_DEPTH_ERR_SHIFT       (11)
#define XHDMI_HDCP1X_PORT_BSTATUS_DEPTH_SHIFT           (8)
#define XHDMI_HDCP1X_PORT_BINFO_VALUE			GENMASK(13, 0)
#define XHDMI_HDCP1X_PORT_MAX_DEV_CNT			(0x7F)
#define XHDMI_HDCP1X_READY_TIMEOUT			25
#define XHDMI_HDCP1X_WAIT_FOR_READY_TIMEOUT		200
#define XHDMI_HDCP1X_WAIT_FOR_ACTIVE_RECEIVER		2000
#define XHDCP1X_TX_CLKDIV_MHZ			1000000
#define XHDCP1X_TX_CLKDIV_HZ			1000

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
	XHDCP1X_TX_HANDLER_AUX_READ = 0,
	XHDCP1X_TX_HANDLER_AUX_WRITE = 1,
	XHDCP1X_TX_HANDLER_HDCP_STATUS = 2,
	XHDCP1X_TX_HANDLER_INVALID = 3
};

enum xhdcp1x_tx_authtype {
	XHDCP1X_TX_AUTHENTICATED = 0,
	XHDCP1X_TX_UNAUTHENTICATED = 1,
	XHDCP1X_TX_INCOMPATIBLE_RX = 2,
	XHDCP1X_TX_AUTHENTICATION_BUSY = 3,
	XHDCP1X_TX_REAUTHENTICATE_REQUESTED = 4,
	XHDCP1X_TX_DEVICE_IS_REVOKED = 5,
};

enum xhdcp1x_tx_protocol {
	XHDCP1X_TX_DP = 0,
	XHDCP1X_TX_HDMI = 1,
};

/*
 * struct hdcp1x_tx_revoclist - This structure contains the HDCP
 * keys revocation list information
 * @rcvid: The array contains the revocation keys list of information
 * @num_of_devices: Number of devices in the revocated list count
 */
struct hdcp1x_tx_revoclist {
	u8  rcvid[XHDCP1X_TX_REVOCLIST_MAX_DEVICES][DRM_HDCP_KSV_LEN];
	u32 num_of_devices;
};

/*
 * struct xhdcp1x_repeater_exchange - This structure contains an instance of the HDCP
 * Repeater values to exchanged between HDCP Tx and HDCP Rx
 * @v[5]: The 20 byte value of SHA1 hash, v'h0,v'h1,v'h2,v'h3,v'h4
 * read from downstream repeater.
 * @ksvlist[32]: An array of 32 elements each of 64 bits to store the KSVs
 * for the KSV FIFO
 * @depth: depeth of downstream topology
 * @device_count: Number of downstream devices attached to the repeater
 * @hdcp14_propagatetopo_errupstream: propagate the topology error to upstream
 */
struct xhdcp1x_repeater_exchange {
	u32 v[DRM_HDCP_KSV_LEN];
	u64 ksvlist[HDCP_2_2_MAX_DEVICE_COUNT + 1];
	u8 depth;
	u8 device_count;
	u8 hdcp14_propagatetopo_errupstream;
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
 * struct xhdcp1x_tx_internal_timer - Current state and data used for internal timer
 * @tmr_ctr: hardware timer configuration structure
 * @initial_ticks: Keep track of the start value of the timer.
 * @timer_expired: Expiration flag set when the hardware timer has interrupted.
 * @reason_id: Keep track of why the timer was started (message or status checking)
 */
struct xhdcp1x_tx_internal_timer {
	struct xlnx_hdcp_timer_config tmr_ctr;
	u32 initial_ticks;
	u8 timer_expired;
	u8 reason_id;
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
	struct hdcp1x_tx_revoclist xhdcp1x_revoc_list;
	struct xhdcp1x_tx_internal_timer xhdcp1x_internal_timer;
	struct delayed_work	sm_work;
	enum hdcp1x_tx_state	curr_state;
	enum hdcp1x_tx_state	prev_state;
	struct xhdcp1x_repeater_exchange repeatervalues;
	struct xhdcp1x_tx_status stats;
	struct regmap *hdcp1x_keymgmt_base;
	void	*cipher;
	void	*interface_ref;
	void __iomem	*interface_base;
	u32	pending_events;
	u32	downstreamready;
	u32	tmr_cnt;
	bool	is_repeater;
	bool	hdcp1x_key_available;
	u8	lane_count;
	u8	*hdcp1x_key;
	u8	auth_status;
	u8	keyinit;
	u8	is_encryption_en;
	u8	is_enabled;
	u8	is_cipher;
	u8	is_hdmi;
	u8	protocol;
	u8	is_riupdate;
	u64	state_helper;
	u64	encryption_map;
	u8	*xlnx_hdcp1x_key;
};

void xlnx_start_hdcp1x_engine(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, bool is_repeater);
int xlnx_hdcp1x_task_monitor(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_downstream_capbility(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_check_rxcapable(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_read_bksv_from_remote(struct xlnx_hdcp1x_config *xhdcp1x_tx, u32 offset,
				       void *buf, u32 buf_size);
bool xlnx_hdcp1x_exchangeksvs(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_computationsstate(struct xlnx_hdcp1x_config *xhdcp1x_tx);
bool xlnx_hdcp1x_tx_validaterxstate(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_check_link_integrity(struct xlnx_hdcp1x_config *xhdcp1x_tx);
void xhdcp1x_tx_set_check_linkstate(struct xlnx_hdcp1x_config *xhdcp1x_tx, int is_enabled);
void xlnx_hdcp1x_tx_process_ri_event(struct xlnx_hdcp1x_config *xhdcp1x_tx);
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
enum hdcp1x_tx_state xlnx_hdcp1x_tx_wait_for_ready(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_get_repeater_info(struct xlnx_hdcp1x_config *xhdcp1x_tx, u16 *info);
int xlnx_hdcp1x_gettopology_maxcascadeexceeded(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_gettopology_maxdevsexceeded(struct xlnx_hdcp1x_config *xhdcp1x_tx);
int xlnx_hdcp1x_set_keys(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data);
int xlnx_hdcp1x_keymngt_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data);
void xlnx_hdcp1x_tx_timer_init(struct xlnx_hdcp1x_config *xhdcp1x_tx,
			       struct xlnx_hdcp_timer_config *tmr_cntrl);
void xlnx_hdcp1x_tx_timer_handler(void *callbackref, u8 tmr_cnt_number);
void xlnx_hdcp1x_tx_start_timer(struct xlnx_hdcp1x_config *xhdcp1x_tx,
				u32 timeout, u8 reason_id);
