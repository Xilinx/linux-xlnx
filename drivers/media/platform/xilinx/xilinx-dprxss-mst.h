/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The driver handles MST mode functionality in RX mode of operation.
 * Driver parses the sideband messages that were requested, formats a reply,
 * and sends the reply.
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 */

#ifndef __XILINX_DPRXSS_MST_H__
#define __XILINX_DPRXSS_MST_H__

#include <linux/types.h>

struct device;

#define XLNX_DPRX_MAX_MST_PORTS		16
#define XLNX_DPRX_GUID_NBYTES		16
#define XLNX_DPRX_I2C_ENTRY_COUNT	3
#define XLNX_DPRX_MST_TIMESLOT_COUNT	64

#define XLNX_DPRXSS_EDID_IIC_ADDRESS	0x50
#define XLNX_DPRX_MST_DEFAULT_PBN	2560

#define XLNX_DPRX_MST_INPUT_PORT	0
#define XLNX_DPRX_INPUT_PORT_MST_SOURCE	0
#define XLNX_DPRX_INPUT_PORT_MST_SINK	1

#define XLNX_DPRX_MST_STREAM_SINK	0x03
#define XLNX_DPRX_MST_BRANCH		0x01

#define XLNX_DPRX_MAX_SBMSG_LEN		48
#define XLNX_DPRX_MAX_SBMSG_BODY	256
#define XLNX_DPRX_MST_MAX_REL_ADDR		15
#define XLNX_DPRX_MAX_LINK_COUNT		15
#define XLNX_DPRX_MST_HDR_NIBBLE_BUFSZ	21
#define XLNX_DPRX_CRC_WIDTH_4		4
#define XLNX_DPRX_CRC_WIDTH_8		8
#define XLNX_DPRX_CRC4_POLY			0x13
#define XLNX_DPRX_CRC8_POLY			0xd5

/*
 * Sideband LINK_ADDRESS uses a 1-bit Messaging_Capability_Status field.
 * Keep these as bit values (0/1), not DP peer-device enum values.
 */
#define XLNX_DPRX_SST		0
#define XLNX_DPRX_MST		1

#define XLNX_DPRX_MST_STREAM_ENABLE	1
#define XLNX_DPRX_MST_STREAM_DISABLE	0

#define XLNX_DPRX_MST_DEVICE_CONNECTED	1
#define XLNX_DPRX_MST_DEVICE_DISCONNECTED	0
#define XLNX_DPRX_MST_DPCD_REVISION	0x11
#define XLNX_DPRX_MST_LEGACY_DEVICE	0

#define XLNX_DPRX_MST_NON_BROADCAST	0
#define XLNX_DPRX_MST_BROADCAST		1
#define XLNX_DPRX_MST_END_PATH		0
#define XLNX_DPRX_MST_HEADER_LENGTH	3
#define XLNX_DPRX_SB_MSG_REPLY_TIMEOUT_MS	5000

#define XLNX_DPRX_SBMSG_LINK_ADDRESS		0x01
#define XLNX_DPRX_SBMSG_ENUM_PATH_RESOURCES	0x10
#define XLNX_DPRX_SBMSG_ALLOCATE_PAYLOAD	0x11
#define XLNX_DPRX_SBMSG_CLEAR_PAYLOAD_ID_TABLE	0x14
#define XLNX_DPRX_SBMSG_REMOTE_DPCD_READ	0x20
#define XLNX_DPRX_SBMSG_REMOTE_I2C_READ		0x22

#define XLNX_DPRX_SBMSG_NAK_REASON_WRITE_FAILURE	0x01

struct xdprxss_mst;

struct xdprxss_mst *xdprxss_init_mst(struct device *dev,
				     void __iomem *interface_base,
				     bool mst_enable, u32 num_audio_channels,
				     u8 num_streams);
void xdprxss_dp_mst_handle_down_req(struct xdprxss_mst *xdpmst);
void xdprxss_dp_mst_payload_allocation(struct xdprxss_mst *xdpmst);
void xdprxss_deinit_mst(struct xdprxss_mst *xdpmst);

#endif /* __XILINX_DPRXSS_MST_H__ */
