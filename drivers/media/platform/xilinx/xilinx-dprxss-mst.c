// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx DP RX Subsystem MST Driver
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "xilinx-dprxss-hw.h"
#include "xilinx-dprxss-mst.h"

/*
 * Reference EDID used to answer REMOTE_I2C_READ transactions.
 *
 * The payload follows VESA Enhanced EDID (E-EDID) format and acts as a
 * stable sink capability blob for virtual MST downstream ports.
 * See DP 1.4a section 5.3.1 (DisplayID or Legacy EDID Access Handling).
 */
#define XLNX_DPRX_EDID_BLOCK_SIZE	128

static const u8 __maybe_unused edid[XLNX_DPRX_EDID_BLOCK_SIZE] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x61, 0x2c, 0x01, 0x00, 0x78, 0x56, 0x34, 0x12,
	0x01, 0x18, 0x01, 0x04, 0xa0, 0x2f, 0x1e, 0x78,
	0x00, 0xee, 0x95, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x21, 0x08, 0x00, 0x71, 0x4f,
	0x81, 0x80, 0xb3, 0x00, 0xd1, 0xc0, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a,
	0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0x40, 0x84, 0x63, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xff, 0x00, 0x58, 0x49, 0x4c,
	0x44, 0x50, 0x53, 0x49, 0x4e, 0x4b, 0x0a, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x58,
	0x49, 0x4c, 0x20, 0x44, 0x50, 0x0a, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfd,
	0x00, 0x38, 0x3c, 0x1e, 0x53, 0x10, 0x00, 0x0a,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x39
};

/*
 * GUID table used for LINK_ADDRESS replies.
 *
 * Entry 0 represents the local branch device GUID, while entries 1..N are
 * used as peer GUIDs for downstream virtual sink ports as defined by
 * DisplayPort MST topology discovery and device identification.
 * See DP 1.4a sections 2.5.3 and 2.11.9.
 */
static const u8 __maybe_unused guid_table[XLNX_DPRX_MAX_MST_PORTS][XLNX_DPRX_GUID_NBYTES] = {
	{0x78, 0x69, 0x6c, 0x61, 0x6e, 0x64, 0x72, 0x65,
	 0x69, 0x6c, 0x73, 0x69, 0x6d, 0x69, 0x6f, 0x6e},
	{0x12, 0x34, 0x12, 0x34, 0x43, 0x21, 0x43, 0x21,
	 0x56, 0x78, 0x56, 0x78, 0x87, 0x65, 0x87, 0x65},
	{0xde, 0xad, 0xbe, 0xef, 0xbe, 0xef, 0xde, 0xad,
	 0x10, 0x01, 0x10, 0x01, 0xda, 0xda, 0xda, 0xda},
	{0xda, 0xba, 0xda, 0xba, 0x10, 0x01, 0x10, 0x01,
	 0xba, 0xda, 0xba, 0xda, 0x5a, 0xd5, 0xad, 0x5a},
	{0x12, 0x34, 0x56, 0x78, 0x43, 0x21, 0x43, 0x21,
	 0xab, 0xcd, 0xef, 0x98, 0x87, 0x65, 0x87, 0x65},
	{0x12, 0x14, 0x12, 0x14, 0x41, 0x21, 0x41, 0x21,
	 0x56, 0x78, 0x56, 0x78, 0x87, 0x65, 0x87, 0x65},
	{0xd1, 0xcd, 0xb1, 0x1f, 0xb1, 0x1f, 0xd1, 0xcd,
	 0xfe, 0xbc, 0xda, 0x90, 0xdc, 0xdc, 0xdc, 0xdc},
	{0xdc, 0xbc, 0xdc, 0xbc, 0xe0, 0x00, 0xe0, 0x00,
	 0xbc, 0xdc, 0xbc, 0xdc, 0x5c, 0xd5, 0xcd, 0x5c},
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11},
	{0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
	 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22},
	{0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
	 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33},
	{0xaa, 0xaa, 0xaa, 0xaa, 0xff, 0xff, 0xff, 0xff,
	 0xfe, 0xbc, 0xda, 0x90, 0xdc, 0xdc, 0xdc, 0xdc},
	{0xbb, 0xbb, 0xbb, 0xbb, 0xe0, 0x00, 0xe0, 0x00,
	 0xff, 0xff, 0xff, 0xff, 0x5c, 0xd5, 0xcd, 0x5c},
	{0xcc, 0xcc, 0xcc, 0xcc, 0x11, 0x11, 0x11, 0x11,
	 0x11, 0x11, 0x11, 0x11, 0xff, 0xff, 0xff, 0xff},
	{0xdd, 0xdd, 0xdd, 0xdd, 0x22, 0x22, 0x22, 0x22,
	 0xff, 0xff, 0xff, 0xff, 0x22, 0x22, 0x22, 0x22},
	{0xee, 0xee, 0xee, 0xee, 0xff, 0xff, 0xff, 0xff,
	 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33}
};

/*
 * Default DPCD capability bytes returned by REMOTE_DPCD_READ.
 *
 * These bytes map to the receiver capability space at DPCD 0x00000 onward.
 * See DP 1.4a section 2.9.3 (DPCD Field Address Mapping).
 */
static const u8 __maybe_unused dpcd[] = {
	0x12, /* DPCD Rev 1.2 */
	0x0a, /* Max Link Rate 2.7 Gbps */
	0x84, /* Max Lane Count 4 + Enhanced Framing */
	0x01, /* Max Downspread 0.5% */
	0x01, /* Receiver Port 0 present */
	0x00, 0x00, 0x00,
	0x02  /* Receiver Port 0 buffer size */
};

/**
 * struct xdprxss_sideband_msg_hdr - Parsed MST sideband header metadata
 * @link_count_total: total hop count encoded in the sideband header
 * @link_count_remaining: remaining hop count while forwarding
 * @relative_address: relative address nibbles for MST path routing
 * @broadcast_msg: true when message is broadcast
 * @path_msg: true when message carries path information
 * @msg_body_len: sideband message body length including CRC nibble byte
 * @start_of_msg: true for first fragment
 * @end_of_msg: true for final fragment
 * @seq_num: two-bit sequence number used across sideband fragments
 * @crc4: CRC-4 nibble for header integrity
 *
 * This layout mirrors DisplayPort MST sideband header fields used by the
 * Messaging AUX Client sideband message layer.
 * See DP 1.4a section 2.11.3.
 */
struct xdprxss_sideband_msg_hdr {
	u8 link_count_total;
	u8 link_count_remaining;
	u8 relative_address[XLNX_DPRX_MST_MAX_REL_ADDR];
	bool broadcast_msg;
	bool path_msg;
	u8 msg_body_len;
	bool start_of_msg;
	bool end_of_msg;
	u8 seq_num;
	u8 crc4;
};

/**
 * struct xdprxss_sideband_msg_data - Sideband payload buffer and metadata
 * @msg: sideband message bytes (without header)
 * @len: valid payload byte count for current fragment
 * @crc4: CRC-4 byte associated with payload section
 */
struct xdprxss_sideband_msg_data {
	u8 msg[XLNX_DPRX_MAX_SBMSG_BODY];
	u8 len;
	u8 crc4;
};

/**
 * struct xdprxss_sideband_msg_rx - Full decoded sideband container
 * @hdr: decoded sideband header
 * @data: payload fragment content
 * @hdr_len: serialized header length in bytes
 * @fragment_num: running fragment index during TX/RX processing
 */
struct xdprxss_sideband_msg_rx {
	struct xdprxss_sideband_msg_hdr hdr;
	struct xdprxss_sideband_msg_data data;
	u8 hdr_len;
	u8 fragment_num;
};

/**
 * struct xdprx_mst_link_address_reply_port - LINK_ADDRESS port descriptor
 * @input_port: 1 for upstream-facing input port, 0 for downstream source port
 * @peer_device_type: DP peer device type advertised in LINK_ADDRESS
 * @port_number: logical MST port index
 * @msg_cap_status: messaging capability status bit (SST/MST)
 * @device_plug_status: plug status bit for downstream peer
 * @legacy_device_plug_status: legacy plug-status compatibility bit
 * @rsvd: reserved bit field
 * @dpcd_rev: DPCD revision for downstream peer capability reporting
 * @peer_guid: GUID of the connected peer
 * @num_sdp_streams: SDP stream count capability
 * @num_sdp_stream_sinks: SDP sink count capability
 */
struct xdprx_mst_link_address_reply_port {
	u8 input_port;
	u8 peer_device_type;
	u8 port_number;
	u8 msg_cap_status;
	u8 device_plug_status;
	u8 legacy_device_plug_status;
	u8 rsvd;
	u8 dpcd_rev;
	u8 peer_guid[XLNX_DPRX_GUID_NBYTES];
	u8 num_sdp_streams;
	u8 num_sdp_stream_sinks;
};

/**
 * struct xdprx_mst_link_address - Local LINK_ADDRESS reply state
 * @num_of_ports: currently enabled MST ports
 * @guid: local branch GUID
 * @ports: per-port LINK_ADDRESS descriptors
 */
struct xdprx_mst_link_address {
	u8 num_of_ports;
	u8 guid[XLNX_DPRX_GUID_NBYTES];
	struct xdprx_mst_link_address_reply_port ports[XLNX_DPRX_MAX_MST_PORTS];
};

/**
 * struct xlnx_dprx_i2c_map - Cached downstream I2C map entry
 * @i2c_dev_id: 7-bit I2C device address
 * @wdata: last write offset used before a read transaction
 * @num_bytes: byte count available in @bytes
 * @bytes: static response payload returned for REMOTE_I2C_READ
 */
struct xlnx_dprx_i2c_map {
	u8 i2c_dev_id;
	u8 wdata;
	u32 num_bytes;
	const u8 *bytes;
};

/**
 * struct xlnx_dprx_dpcd_map - Cached downstream DPCD map entry
 * @start_address: first DPCD address covered by @bytes
 * @num_bytes: number of DPCD bytes represented by @bytes
 * @bytes: static DPCD capability/data image returned to source
 */
struct xlnx_dprx_dpcd_map {
	u32 start_address;
	u32 num_bytes;
	const u8 *bytes;
};

/**
 * struct xlnx_dprx_mst_port - Per-port MST emulation state
 * @i2c: I2C address map for REMOTE_I2C_READ handling
 * @dpcd: DPCD map for REMOTE_DPCD_READ handling
 * @fullpbn: full payload bandwidth number
 * @availpbn: currently available payload bandwidth number
 * @enable: port enable state exposed via LINK_ADDRESS
 */
struct xlnx_dprx_mst_port {
	struct xlnx_dprx_i2c_map i2c[XLNX_DPRX_I2C_ENTRY_COUNT];
	struct xlnx_dprx_dpcd_map dpcd;
	u32 fullpbn;
	u32 availpbn;
	bool enable;
};

/**
 * struct xdprx_mst_config - Top-level MST topology and payload state
 * @link_address: LINK_ADDRESS reply topology data
 * @payload_id_table: VC payload allocation table image (timeslots)
 * @mst_port: per-port sideband emulation state
 */
struct xdprx_mst_config {
	struct xdprx_mst_link_address link_address;
	u8 payload_id_table[XLNX_DPRX_NO_OF_MST_TIMESLOTS];
	struct xlnx_dprx_mst_port mst_port[XLNX_DPRX_MAX_MST_PORTS];
};

/**
 * struct xdprxss_mst - Runtime MST sideband engine context
 * @dev: owning device for logging and managed allocations
 * @mst_work: deferred worker for DOWN_REQ processing
 * @rx_topology: emulated downstream topology and payload data
 * @dprx_mst_mutex: serialization for sideband state updates
 * @intf_base: mapped DPRX register base
 * @num_audio_channels: SDP stream/sink capability fields
 * @num_streams: configured number of downstream streams
 * @mst_enable: MST mode enable flag
 */
struct xdprxss_mst {
	struct device *dev;
	struct work_struct mst_work;
	struct xdprx_mst_config rx_topology;
	struct mutex dprx_mst_mutex; /* serializes sideband state updates */
	void __iomem *intf_base;
	u32 num_audio_channels;
	u8 num_streams;
	bool mst_enable;
};

/**
 * xdprxss_mst_read() - Read a DPRX register
 * @xdpmst: MST context
 * @addr: register offset
 *
 * Return: register value
 */
static inline u32 xdprxss_mst_read(struct xdprxss_mst *xdpmst, u32 addr)
{
	return ioread32(xdpmst->intf_base + addr);
}

/**
 * xdprxss_mst_write() - Write a DPRX register
 * @xdpmst: MST context
 * @addr: register offset
 * @value: register value to write
 */
static inline void xdprxss_mst_write(struct xdprxss_mst *xdpmst, u32 addr, u32 value)
{
	iowrite32(value, xdpmst->intf_base + addr);
}

/**
 * xdprxss_mst_set() - Set register bits
 * @xdpmst: MST context
 * @addr: register offset
 * @set: bit mask to set
 */
static inline void xdprxss_mst_set(struct xdprxss_mst *xdpmst, u32 addr, u32 set)
{
	xdprxss_mst_write(xdpmst, addr, xdprxss_mst_read(xdpmst, addr) | set);
}

/**
 * xget_i2c_device() - Find/allocate an I2C map slot for a stream port
 * @xdpmst: MST context
 * @port_number: downstream MST port number
 * @i2c_dev_id: I2C device address to lookup
 *
 * Return: valid map entry pointer or %NULL when table is full.
 */
static struct xlnx_dprx_i2c_map *xget_i2c_device(struct xdprxss_mst *xdpmst,
						 u8 port_number, u8 i2c_dev_id)
{
	struct xlnx_dprx_i2c_map *i2c_write;
	u8 idx;

	i2c_write = xdpmst->rx_topology.mst_port[port_number].i2c;

	for (idx = 0; idx < XLNX_DPRX_I2C_ENTRY_COUNT; idx++) {
		if (i2c_write[idx].i2c_dev_id == i2c_dev_id ||
		    i2c_write[idx].i2c_dev_id == 0)
			return &i2c_write[idx];
	}

	return NULL;
}

/**
 * xdprxss_enable_mst_stream() - Enable/disable one downstream virtual stream
 * @xdpmst: MST context
 * @stream: downstream stream/port index
 * @enable: true to enable, false to disable
 *
 * Updates local topology bookkeeping and sink-count register advertisement.
 * The function is idempotent: repeated calls with the same @enable value do
 * not change num_of_ports.
 */
static void xdprxss_enable_mst_stream(struct xdprxss_mst *xdpmst, u8 stream, bool enable)
{
	struct xlnx_dprx_mst_port *port = &xdpmst->rx_topology.mst_port[stream];

	if (port->enable == enable)
		return;

	port->enable = enable;

	if (enable)
		xdpmst->rx_topology.link_address.num_of_ports++;
	else if (xdpmst->rx_topology.link_address.num_of_ports > 0)
		xdpmst->rx_topology.link_address.num_of_ports--;

	xdprxss_mst_write(xdpmst, XDPRX_SINK_COUNT_REG,
			  xdpmst->rx_topology.link_address.num_of_ports);
}

/**
 * xdprxss_init_mst_i2c_port() - Initialize per-port I2C response map
 * @xdpmst: MST context
 * @stream: downstream stream/port index
 * @i2c_dev_id: I2C device address
 * @num_bytes: payload size
 * @data: payload bytes returned for remote reads
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int xdprxss_init_mst_i2c_port(struct xdprxss_mst *xdpmst, u8 stream,
				     u8 i2c_dev_id, u32 num_bytes,
				     const u8 *data)
{
	struct xlnx_dprx_i2c_map *i2c = xget_i2c_device(xdpmst, stream, i2c_dev_id);

	if (!i2c)
		return -ENODEV;

	i2c->i2c_dev_id = i2c_dev_id;
	i2c->wdata = 0;
	i2c->num_bytes = num_bytes;
	i2c->bytes = data;

	return 0;
}

/**
 * xdprxss_init_mst_stream_params() - Initialize one stream's EDID/DPCD model
 * @xdpmst: MST context
 * @stream: downstream stream index
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int xdprxss_init_mst_stream_params(struct xdprxss_mst *xdpmst, u8 stream)
{
	int ret;

	ret = xdprxss_init_mst_i2c_port(xdpmst, stream, XLNX_DPRXSS_EDID_IIC_ADDRESS,
					sizeof(edid), edid);
	if (ret)
		return ret;

	xdpmst->rx_topology.mst_port[stream].dpcd.bytes = dpcd;
	xdpmst->rx_topology.mst_port[stream].dpcd.num_bytes = sizeof(dpcd);
	xdpmst->rx_topology.mst_port[stream].dpcd.start_address = 0;

	xdpmst->rx_topology.mst_port[stream].fullpbn = XLNX_DPRX_MST_DEFAULT_PBN;
	xdpmst->rx_topology.mst_port[stream].availpbn = XLNX_DPRX_MST_DEFAULT_PBN;

	xdprxss_enable_mst_stream(xdpmst, stream, true);

	return 0;
}

/**
 * xdprxss_init_mst_input_port() - Initialize upstream-facing branch port
 * @xdpmst: MST context
 * @port_num: input port index
 */
static void xdprxss_init_mst_input_port(struct xdprxss_mst *xdpmst, u8 port_num)
{
	struct xdprx_mst_link_address *link_address_reply = &xdpmst->rx_topology.link_address;

	link_address_reply->ports[port_num].input_port = XLNX_DPRX_INPUT_PORT_MST_SINK;
	link_address_reply->ports[port_num].peer_device_type = XLNX_DPRX_MST_BRANCH;
	link_address_reply->ports[port_num].msg_cap_status = XLNX_DPRX_MST;
	link_address_reply->ports[port_num].device_plug_status = XLNX_DPRX_MST_DEVICE_CONNECTED;

	memcpy(link_address_reply->guid, guid_table[0], XLNX_DPRX_GUID_NBYTES);

	xdprxss_enable_mst_stream(xdpmst, port_num, true);
}

/**
 * xdprxss_init_mst_streams() - Build initial LINK_ADDRESS topology state
 * @xdpmst: MST context
 *
 * Return: 0 on success, negative errno if stream parameter setup fails.
 */
static int xdprxss_init_mst_streams(struct xdprxss_mst *xdpmst)
{
	struct xdprx_mst_link_address_reply_port *port;
	int i, ret;

	for (i = 1; i < XLNX_DPRX_MAX_MST_PORTS; i++) {
		port = &xdpmst->rx_topology.link_address.ports[i];

		port->port_number = i;

		port->input_port = XLNX_DPRX_INPUT_PORT_MST_SOURCE;
		port->peer_device_type = XLNX_DPRX_MST_STREAM_SINK;
		port->msg_cap_status = XLNX_DPRX_SST;
		port->device_plug_status = XLNX_DPRX_MST_DEVICE_CONNECTED;
		port->legacy_device_plug_status = XLNX_DPRX_MST_LEGACY_DEVICE;
		port->dpcd_rev = XLNX_DPRX_MST_DPCD_REVISION;
		port->num_sdp_streams = xdpmst->num_audio_channels;
		port->num_sdp_stream_sinks = xdpmst->num_audio_channels;

		memcpy(port->peer_guid, guid_table[i], XLNX_DPRX_GUID_NBYTES);
	}

	for (i = 1; i <= xdpmst->num_streams; i++) {
		ret = xdprxss_init_mst_stream_params(xdpmst, i);
		if (ret)
			return ret;
	}

	xdprxss_init_mst_input_port(xdpmst, XLNX_DPRX_MST_INPUT_PORT);

	return 0;
}

/**
 * xdprxss_init_mst_registers() - Program base MST registers
 * @xdpmst: MST context
 */
static void xdprxss_init_mst_registers(struct xdprxss_mst *xdpmst)
{
	xdprxss_mst_write(xdpmst, XDPRX_MST_CAP_REG, XDPRX_MST_CAP_ENABLE_MASK |
			  XDPRX_MST_CAP_SOFT_VCP_MASK |
			  XDPRX_MST_CAP_OVER_ACT_MASK);
	xdprxss_mst_write(xdpmst, XDPRX_INTR_MASK_1_REG, 0x0);
	xdprxss_mst_write(xdpmst, XDPRX_LOCAL_EDID_REG, 0x0);
	xdprxss_mst_set(xdpmst, XDPRX_CDRCTRL_CFG_REG, XDPRX_CDRCTRL_DIS_TIMEOUT);
}
