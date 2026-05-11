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
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "xilinx-dprxss-hw.h"
#include "xilinx-dprxss-mst.h"

/*
 * HW requires a short settle time between fragment assembly and
 * DOWN_REP write trigger to avoid sporadic sideband handshaking loss.
 */
#define XDPRX_SBMSG_TX_DELAY_MS		20

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

/**
 * xdprxss_msg_crc4() - Calculate MST CRC-4/CRC-8 remainder
 * @data: input bytes/nibbles
 * @number_of_bits: bit count to process
 * @crc_width: CRC width selector; XLNX_DPRX_CRC_WIDTH_4 for header CRC-4,
 *             XLNX_DPRX_CRC_WIDTH_8 for payload CRC-8
 *
 * Return: computed remainder.
 */
static u8 xdprxss_msg_crc4(const u8 *data, u32 number_of_bits, u8 crc_width)
{
	u8 bit_shift = (crc_width == XLNX_DPRX_CRC_WIDTH_4) ? 0x03 : 0x07;
	u8 bit_mask = (crc_width == XLNX_DPRX_CRC_WIDTH_4) ? 0x08 : 0x80;
	u32 remainder = 0;
	u32 index = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[index] & bit_mask) >> bit_shift;
		bit_mask >>= 1;
		bit_shift--;
		if (bit_mask == 0) {
			bit_mask = (crc_width == XLNX_DPRX_CRC_WIDTH_4) ? 0x08 : 0x80;
			bit_shift = (crc_width == XLNX_DPRX_CRC_WIDTH_4) ? 0x03 : 0x07;
			index++;
		}
		if ((remainder & BIT(crc_width)) != 0)
			remainder ^= (crc_width == XLNX_DPRX_CRC_WIDTH_4) ?
					XLNX_DPRX_CRC4_POLY : XLNX_DPRX_CRC8_POLY;
	}

	number_of_bits = crc_width;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & BIT(crc_width)) != 0)
			remainder ^= (crc_width == XLNX_DPRX_CRC_WIDTH_4) ?
					XLNX_DPRX_CRC4_POLY : XLNX_DPRX_CRC8_POLY;
	}

	return remainder;
}

/**
 * xdprxss_sbmsg_data_crc8() - Compute payload CRC-8 for current fragment
 * @sb_msg: sideband message context
 *
 * Return: computed CRC-8 checksum.
 */
static u8 xdprxss_sbmsg_data_crc8(struct xdprxss_sideband_msg_rx *sb_msg)
{
	struct xdprxss_sideband_msg_data *rxdata = &sb_msg->data;
	u32 idx = sb_msg->fragment_num * (XLNX_DPRX_MAX_SBMSG_LEN - sb_msg->hdr_len - 1);

	return xdprxss_msg_crc4(&rxdata->msg[idx],
				BITS_PER_BYTE * rxdata->len,
				XLNX_DPRX_CRC_WIDTH_8);
}

/**
 * xdprxss_msg_header_crc4() - Compute sideband header CRC-4
 * @hdr: parsed header fields
 *
 * Return: computed CRC nibble (0 on invalid header input).
 */
static u8 xdprxss_msg_header_crc4(const struct xdprxss_sideband_msg_hdr *hdr)
{
	u8 nibbles[XLNX_DPRX_MST_HDR_NIBBLE_BUFSZ];
	u8 rel_addr_nibbles;
	u8 offset = 0;

	if (!hdr->link_count_total || hdr->link_count_total > XLNX_DPRX_MAX_LINK_COUNT)
		return 0;

	rel_addr_nibbles = hdr->link_count_total - 1;
	if (rel_addr_nibbles > ARRAY_SIZE(hdr->relative_address))
		return 0;

	nibbles[0] = hdr->link_count_total;
	nibbles[1] = hdr->link_count_remaining;

	for (offset = 0; offset < rel_addr_nibbles; offset += 2) {
		nibbles[2 + offset] = hdr->relative_address[offset];

		if ((offset + 1) < rel_addr_nibbles)
			nibbles[2 + offset + 1] = hdr->relative_address[offset + 1];
		else
			nibbles[2 + offset + 1] = 0;
	}

	nibbles[2 + offset] = (hdr->broadcast_msg << 3) |
		(hdr->path_msg << 2) | ((hdr->msg_body_len & 0x30) >> 4);
	nibbles[3 + offset] = hdr->msg_body_len & 0x0f;
	nibbles[4 + offset] = (hdr->start_of_msg << 3) |
		(hdr->end_of_msg << 2) | hdr->seq_num;

	return xdprxss_msg_crc4(nibbles, 4 * (5 + offset), XLNX_DPRX_CRC_WIDTH_4);
}

/**
 * xdprxss_sideband_parse_message() - Parse one incoming sideband fragment
 * @sb_msg: output parsed message
 * @rxdata: raw bytes read from DOWN_REQ FIFO window
 *
 * Return: 0 on success, negative errno on parse/CRC failure.
 */
static int xdprxss_sideband_parse_message(struct xdprxss_sideband_msg_rx *sb_msg,
					  const u8 *rxdata)
{
	struct xdprxss_sideband_msg_data *data = &sb_msg->data;
	struct xdprxss_sideband_msg_hdr *hdr = &sb_msg->hdr;
	u8 rel_addr_nibbles;
	u8 idx = 0;
	u8 len = 0;
	u8 crc4;

	hdr->link_count_total = rxdata[len] >> 4;
	hdr->link_count_remaining = rxdata[len] & 0x0f;
	if (!hdr->link_count_total || hdr->link_count_total > XLNX_DPRX_MAX_LINK_COUNT)
		return -EINVAL;

	len++;
	rel_addr_nibbles = hdr->link_count_total - 1;
	if (rel_addr_nibbles > ARRAY_SIZE(hdr->relative_address))
		return -EINVAL;

	memset(hdr->relative_address, 0, sizeof(hdr->relative_address));
	for (idx = 0; idx < DIV_ROUND_UP(rel_addr_nibbles, 2); idx++) {
		u8 byte = rxdata[len++];

		hdr->relative_address[idx * 2] = (byte >> 4) & 0x0f;
		if (idx * 2 + 1 < rel_addr_nibbles)
			hdr->relative_address[idx * 2 + 1] = byte & 0x0f;
	}

	hdr->broadcast_msg = (rxdata[len] >> 7) & 0x1;
	hdr->path_msg = (rxdata[len] >> 6) & 0x1;
	hdr->msg_body_len = rxdata[len] & 0x3f;
	len++;

	hdr->start_of_msg = (rxdata[len] >> 7) & 0x1;
	hdr->end_of_msg = (rxdata[len] >> 6) & 0x1;
	hdr->seq_num = (rxdata[len] >> 4) & 0x3;
	hdr->crc4 = rxdata[len] & 0x0f;
	len++;
	sb_msg->hdr_len = len;

	crc4 = xdprxss_msg_header_crc4(hdr);
	if (crc4 != hdr->crc4)
		return -EINVAL;

	if (!hdr->msg_body_len || (sb_msg->hdr_len + hdr->msg_body_len) > XLNX_DPRX_MAX_SBMSG_LEN)
		return -EINVAL;

	data->len = hdr->msg_body_len - 1;
	memcpy(data->msg, &rxdata[len], data->len);

	data->crc4 = rxdata[sb_msg->hdr_len + data->len];

	crc4 = xdprxss_sbmsg_data_crc8(sb_msg);
	if (crc4 != data->crc4)
		return -EINVAL;

	return 0;
}

/**
 * xdprxss_dp_mst_read_down_req() - Read and parse DOWN_REQ sideband buffer
 * @xdpmst: MST context
 * @sb_msg: parsed output message
 *
 * Return: 0 on success, negative errno on failure.
 */
static int xdprxss_dp_mst_read_down_req(struct xdprxss_mst *xdpmst,
					struct xdprxss_sideband_msg_rx *sb_msg)
{
	u8 data[XLNX_DPRX_MAX_SBMSG_LEN];
	u8 i;

	for (i = 0; i < XLNX_DPRX_MAX_SBMSG_LEN; i++)
		data[i] = xdprxss_mst_read(xdpmst, XDPRX_DOWN_REQ + (i * 4));

	sb_msg->fragment_num = 0;

	return xdprxss_sideband_parse_message(sb_msg, data);
}

/**
 * xdprxss_dp_write_sideband_msg() - Write one reply fragment to DOWN_REP
 * @xdpmst: MST context
 * @buf: serialized sideband bytes
 * @len: number of bytes to write
 *
 * Return: 0 on success, negative errno on timeout/failure.
 */
static int xdprxss_dp_write_sideband_msg(struct xdprxss_mst *xdpmst,
					 const u8 *buf, u8 len)
{
	void __iomem *address = xdpmst->intf_base + XDPRX_DEVICE_SERVICE_IRQ;
	u32 status;
	u8 idx;

	for (idx = 0; idx < len; idx++)
		xdprxss_mst_write(xdpmst, XDPRX_DOWN_REP + (idx * 4), buf[idx]);

	xdprxss_mst_write(xdpmst, XDPRX_DEVICE_SERVICE_IRQ,
			  XDPRX_DEVICE_SERVICE_IRQ_NEW_DOWN_REPLY_MASK);
	xdprxss_mst_write(xdpmst, XDPRX_HPD_INTR_REG, XDPRX_HPD_INTR_MASK);

	return readx_poll_timeout(ioread32, address, status,
				  status & XDPRX_DEVICE_SERVICE_IRQ_NEW_DOWN_REPLY_MASK,
				  USEC_PER_MSEC,
				  XLNX_DPRX_SB_MSG_REPLY_TIMEOUT_MS * USEC_PER_MSEC);
}

/**
 * xdprxss_serialized_hdr_len() - Compute on-wire MST sideband header length
 * @link_count_total: total link/hop count carried in the sideband header
 *
 * The serialized header is one LCT/LCR byte plus ceil((LCT - 1) / 2) bytes
 * carrying relative-address nibbles plus two trailing fixed bytes
 * (broadcast/path/msg_body_len and SoM/EoM/seq_num/crc4). For LCT <= 1 the
 * length collapses to XLNX_DPRX_MST_HEADER_LENGTH; multi-hop topologies
 * extend it by one byte for every two additional hops.
 *
 * Return: serialized header length in bytes.
 */
static u8 xdprxss_serialized_hdr_len(u8 link_count_total)
{
	return XLNX_DPRX_MST_HEADER_LENGTH + (link_count_total / 2);
}

/**
 * xdprxss_dp_send_sideband_msg() - Serialize and send one sideband fragment
 * @xdpmst: MST context
 * @sb_msg: sideband reply fragment metadata/payload
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int xdprxss_dp_send_sideband_msg(struct xdprxss_mst *xdpmst,
					struct xdprxss_sideband_msg_rx *sb_msg)
{
	struct xdprxss_sideband_msg_data *data = &sb_msg->data;
	struct xdprxss_sideband_msg_hdr *hdr = &sb_msg->hdr;
	u8 buf[XLNX_DPRX_MAX_SBMSG_LEN];
	int idx = 0, i;
	u8 full_len;

	msleep(XDPRX_SBMSG_TX_DELAY_MS);

	buf[idx++] = ((hdr->link_count_total & 0xf) << 4) | (hdr->link_count_remaining & 0xf);

	for (i = 0; i < hdr->link_count_total - 1; i += 2) {
		buf[idx] = (hdr->relative_address[i] << 4);
		if ((i + 1) < (hdr->link_count_total - 1))
			buf[idx] |= hdr->relative_address[i + 1];
		idx++;
	}

	buf[idx++] = (hdr->broadcast_msg << 7) | (hdr->path_msg << 6) | (hdr->msg_body_len & 0x3f);
	buf[idx++] = (hdr->start_of_msg << 7) | (hdr->end_of_msg << 6) | (hdr->seq_num << 4) |
		     (hdr->crc4);

	sb_msg->hdr_len = idx;

	full_len = XLNX_DPRX_MAX_SBMSG_LEN - sb_msg->hdr_len - 1;

	for (i = 0; i < data->len; i++)
		buf[i + sb_msg->hdr_len] = data->msg[i + (sb_msg->fragment_num * full_len)];

	buf[sb_msg->hdr_len + i] = data->crc4;

	return xdprxss_dp_write_sideband_msg(xdpmst, buf, sb_msg->hdr_len + hdr->msg_body_len);
}

/**
 * xdprxss_dp_process_sideband_msg() - Fragment and transmit full sideband reply
 * @xdpmst: MST context
 * @sb_msg: sideband reply container
 *
 * Return: 0 on success, negative errno on TX failure.
 */
static int
xdprxss_dp_process_sideband_msg(struct xdprxss_mst *xdpmst,
				struct xdprxss_sideband_msg_rx *sb_msg)
{
	struct xdprxss_sideband_msg_data *data = &sb_msg->data;
	struct xdprxss_sideband_msg_hdr *hdr = &sb_msg->hdr;
	u32 data_remaining = data->len;
	u32 data_len = data->len;
	u8 full_len;
	int ret;

	sb_msg->fragment_num = 0;
	hdr->seq_num = 0;

	/*
	 * Per-fragment payload capacity must match what the serializer will
	 * actually emit. The reply builders pre-set hdr_len to the constant
	 * XLNX_DPRX_MST_HEADER_LENGTH, but multi-hop topologies grow the
	 * on-wire header by one byte per two relative-address nibble pairs.
	 * Recompute hdr_len here from link_count_total so the per-fragment
	 * capacity (full_len) cannot exceed what xdprxss_dp_send_sideband_msg
	 * has room to write into the wire buffer.
	 */
	sb_msg->hdr_len = xdprxss_serialized_hdr_len(hdr->link_count_total);
	full_len = XLNX_DPRX_MAX_SBMSG_LEN - sb_msg->hdr_len - 1;

	while (data_remaining) {
		hdr->start_of_msg = (data_remaining == data_len) ? 1 : 0;
		data->len = (data_remaining > full_len) ? full_len : data_remaining;
		data_remaining -= data->len;
		hdr->msg_body_len = data->len + 1;
		hdr->end_of_msg = data_remaining ? 0 : 1;
		hdr->crc4 = xdprxss_msg_header_crc4(hdr);
		data->crc4 = xdprxss_sbmsg_data_crc8(sb_msg);

		ret = xdprxss_dp_send_sideband_msg(xdpmst, sb_msg);
		if (ret)
			return ret;

		sb_msg->fragment_num++;
		hdr->seq_num = (hdr->seq_num + 1) & 0x3;
	}

	return 0;
}

/**
 * xdprxss_build_mst_header() - Fill common MST reply header fields
 * @reply_msg: reply container
 * @hdr_data: template header bytes
 */
static void
xdprxss_build_mst_header(struct xdprxss_sideband_msg_rx *reply_msg,
			 const u8 *hdr_data)
{
	reply_msg->hdr.link_count_remaining = reply_msg->hdr.link_count_total - 1;
	reply_msg->hdr.broadcast_msg = *hdr_data++;
	reply_msg->hdr.path_msg = *hdr_data++;
	reply_msg->hdr_len = XLNX_DPRX_MST_HEADER_LENGTH;
}

/**
 * xdprxss_build_nack_reply() - Build a NACK sideband reply
 * @xdpmst: MST context
 * @reply_msg: reply container
 *
 * Assembles a generic NACK reply carrying the local branch GUID and a
 * WRITE_FAILURE reason code, used when an incoming DOWN_REQ cannot be
 * processed.
 */
static void
xdprxss_build_nack_reply(struct xdprxss_mst *xdpmst,
			 struct xdprxss_sideband_msg_rx *reply_msg)
{
	u8 guid_idx;
	u8 idx = 0;

	reply_msg->data.msg[idx++] = BIT(7);
	for (guid_idx = 0; guid_idx < XLNX_DPRX_GUID_NBYTES; guid_idx++)
		reply_msg->data.msg[idx++] = xdpmst->rx_topology.link_address.guid[guid_idx];

	reply_msg->data.msg[idx++] = XLNX_DPRX_SBMSG_NAK_REASON_WRITE_FAILURE;
	reply_msg->data.msg[idx++] = 0x00;
	reply_msg->hdr.link_count_remaining = 0;
	reply_msg->hdr.broadcast_msg = 0;
	reply_msg->hdr.path_msg = 0;
	reply_msg->hdr_len = XLNX_DPRX_MST_HEADER_LENGTH;

	reply_msg->data.len = idx;
}

/**
 * xdprxss_build_enum_path_resource_reply() - Build ENUM_PATH_RESOURCES reply
 * @xdpmst: MST context
 * @reply_msg: reply container
 */
static void
xdprxss_build_enum_path_resource_reply(struct xdprxss_mst *xdpmst,
				       struct xdprxss_sideband_msg_rx *reply_msg)
{
	u8 header[4] = { XLNX_DPRX_MST_NON_BROADCAST, XLNX_DPRX_MST_END_PATH, 0, 0 };
	struct xlnx_dprx_mst_port *port;
	u8 port_number;
	u8 idx = 0;

	xdprxss_build_mst_header(reply_msg, header);

	port_number = reply_msg->data.msg[1] >> 4;
	if (port_number >= XLNX_DPRX_MAX_MST_PORTS) {
		xdprxss_build_nack_reply(xdpmst, reply_msg);
		return;
	}

	port = &xdpmst->rx_topology.mst_port[port_number];
	reply_msg->data.msg[idx++] = XLNX_DPRX_SBMSG_ENUM_PATH_RESOURCES;
	reply_msg->data.msg[idx++] = port_number << 4;

	if (!port->fullpbn || !port->availpbn) {
		port->fullpbn = XLNX_DPRX_MST_DEFAULT_PBN;
		port->availpbn = XLNX_DPRX_MST_DEFAULT_PBN;
	}

	reply_msg->data.msg[idx++] = port->fullpbn >> 8;
	reply_msg->data.msg[idx++] = port->fullpbn & 0xff;
	reply_msg->data.msg[idx++] = port->availpbn >> 8;
	reply_msg->data.msg[idx++] = port->availpbn & 0xff;

	reply_msg->data.len = idx;
}

/**
 * xdprxss_build_allocate_payload_reply() - Build ALLOCATE_PAYLOAD reply
 * @xdpmst: MST context
 * @reply_msg: reply container
 */
static void
xdprxss_build_allocate_payload_reply(struct xdprxss_mst *xdpmst,
				     struct xdprxss_sideband_msg_rx *reply_msg)
{
	u8 header[4] = { XLNX_DPRX_MST_NON_BROADCAST, XLNX_DPRX_MST_END_PATH, 0, 0 };
	u8 port_number;
	u8 idx = 0;
	u8 vc_id;
	u32 pbn;

	xdprxss_build_mst_header(reply_msg, header);

	port_number = reply_msg->data.msg[1] >> 4;
	if (port_number >= XLNX_DPRX_MAX_MST_PORTS) {
		xdprxss_build_nack_reply(xdpmst, reply_msg);
		return;
	}

	reply_msg->data.msg[idx++] = XLNX_DPRX_SBMSG_ALLOCATE_PAYLOAD;
	vc_id = reply_msg->data.msg[2];
	pbn = (reply_msg->data.msg[3] << 8) | reply_msg->data.msg[4];

	reply_msg->data.msg[idx++] = port_number << 4;
	reply_msg->data.msg[idx++] = vc_id;
	reply_msg->data.msg[idx++] = pbn >> 8;
	reply_msg->data.msg[idx++] = pbn & 0xff;

	reply_msg->data.len = idx;
}
