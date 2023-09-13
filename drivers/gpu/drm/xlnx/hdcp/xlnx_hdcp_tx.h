/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HDCP Interface driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_HDCP_TX_H_
#define _XLNX_HDCP_TX_H_

#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/xlnx/xlnx_timer.h>
#include <linux/xlnx/xilinx-hdcp1x-cipher.h>
#include "xlnx_hdcp2x_tx.h"

#define XHDCP_KEY_WRITE_PERMISSION 0220

enum xlnx_hdcptx_callback_type {
	XHDCPTX_HANDLER_AUX_READ = 0,
	XHDCPTX_HANDLER_AUX_WRITE = 1,
	XHDCPTX_HANDLER_HDCP_STATUS = 2,
	XHDCPTX_HANDLER_INVALID = 3
};

enum xlnx_hdcptx_protocol_type {
	XHDCPTX_HDCP_NONE = 0,
	XHDCPTX_HDCP_1X = 1,
	XHDCPTX_HDCP_2X = 2,
	XHDCPTX_HDCP_BOTH = 3
};

enum xlnx_hdcptx_authstatus {
	XHDCPTX_INCOMPATIBLE_RX = 0,
	XHDCPTX_AUTHENTICATION_BUSY = 1,
	XHDCPTX_AUTHENTICATED = 2,
	XHDCPTX_UNAUTHENTICATED = 3,
	XHDCPTX_REAUTHENTICATE_REQUESTED = 4,
	XHDCPTX_DEVICE_IS_REVOKED = 5,
	XHDCPTX_NO_SRM_LOADED = 6
};

/**
 * struct xlnx_hdcptx - This structure contains hardware subcore configuration
 * information about HDCP protocol hardware engine.
 * @dev: platform device
 * @xhdcp2x: HDCP2X configuration structure
 * @xhdcp1x: HDCP1X configuration structure
 * @xhdcptmr: Axi timer for HDCP module
 * @hdcptx_mutex: Mutex for HDCP state machine
 * @hdcp_task_monitor: Work function for HDCP
 * @hdcp_protocol: Protocol type, HDCP1x, HDCP2X or supports both
 * @auth_status: Authentication status
 * @hdcp2xenable: HDCP2X protocol is enabled
 * @hdcp1xenable: HDCP1X protocol is enabled
 * @is_enckey_available: Availability of encryption keys
 * @is_hdcp_initialized: Flag to check whether HDCP driver is initialized or not
 */
struct xlnx_hdcptx {
	struct device *dev;
	struct xlnx_hdcp2x_config	*xhdcp2x;
	struct xlnx_hdcp1x_config	*xhdcp1x;
	struct xlnx_hdcp_timer_config	*xhdcptmr;
	struct mutex hdcptx_mutex; /* Mutex for HDCP state machine */
	struct delayed_work  hdcp_task_monitor;
	enum xlnx_hdcptx_protocol_type hdcp_protocol;
	enum xlnx_hdcptx_authstatus auth_status;
	bool hdcp2xenable;
	bool hdcp1xenable;
	bool is_enckey_available;
	bool is_hdcp_initialized;
};

int xlnx_hdcp_tx_reset(struct xlnx_hdcptx *xtxhdcp);
int xlnx_start_hdcp_engine(struct xlnx_hdcptx *xtxhdcp, u8 lanecount);
int xlnx_hdcp_tx_exit(struct xlnx_hdcptx *xtxhdcp);
int xlnx_hdcp_tx_set_callback(void *ref,
			      enum xlnx_hdcptx_callback_type callback_type,
			      void *callbackfunc);
int xlnx_hdcp_tx_set_keys(struct xlnx_hdcptx *xtxhdcp, const u8 *data);

void xlnx_hdcp1x_interrupt_handler(struct xlnx_hdcptx *xtxhdcp);
void *xlnx_hdcp_tx_init(struct device *dev, void *protocol_ref,
			struct xlnx_hdcptx *xtxhdcp, void __iomem *hdcp_base_address,
			u8 is_repeater,	enum xlnx_hdcptx_protocol_type, u8 lane_count,
			int hw_protocol, struct regmap *key_base_address);
void *xlnx_hdcp_timer_init(struct device *dev, void __iomem *interface_base);
void xlnx_hdcp_tx_process_cp_irq(struct xlnx_hdcptx *xhdcptx);
void xlnx_hdcp_tx_timer_exit(struct xlnx_hdcptx *xtxhdcp);
void xlnx_hdcptx_read_ds_sink_capability(struct xlnx_hdcptx *xtxhdcp);

#endif
