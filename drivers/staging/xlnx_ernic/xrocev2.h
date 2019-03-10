/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef _XRNIC_ROCEV2_H
#define _XRNIC_ROCEV2_H

#ifdef __cplusplus
	extern "C" {
#endif

#include <linux/types.h>
#include <linux/udp.h>
#include <rdma/ib_pack.h>

#define XRNIC_REQ_QPN			0x1
#define XRNIC_RESPONDER_RESOURCES	0x10
#define XRNIC_INITIATOR_DEPTH		0x10
#define XRNIC_REQ_LOCAL_CM_RESP_TOUT	0x11
#define XRNIC_REQ_REMOTE_CM_RESP_TOUT	0x14
#define XRNIC_REQ_PATH_PKT_PAYLOAD_MTU	92
#define XRNIC_REQ_RETRY_COUNT		0x7
#define XRNIC_REQ_RDC_EXISTS		1
#define XRNIC_REQ_SRQ			0

#define XRNIC_REJ_INFO_LEN		0

#define XRNIC_MRA_SERVICE_TIMEOUT	0x11

#define XRNIC_REP_END_END_FLOW_CONTROL	0x0
#define XRNIC_REP_FAIL_OVER_ACCEPTED	0x3
#define XRNIC_REP_TARGET_ACK_DELAY	0x1F
#define XRNIC_REP_RNR_RETRY_COUNT	0x7

#define XRNIC_CM_TIMEOUT		0x4
#define XRNIC_CM_TIMER_TIMEOUT		0x11

enum xrnic_wc_opcod {
	XRNIC_RDMA_WRITE	= 0x0,
	XRNIC_SEND_ONLY		= 0x2,
	XRNIC_RDMA_READ		= 0x4
};

enum xrnic_msg_rej {
	XRNIC_REJ_REQ		= 0x0,
	XRNIC_REJ_REP		= 0x1,
	XRNIC_REJ_OTHERS	= 0x2,
};

enum xrnic_msg_mra {
	XRNIC_MRA_REQ = 0x0,
	XRNIC_MRA_REP = 0x1,
	XRNIC_MRA_LAP = 0x2,
};

enum xrnic_rej_reason {
	XRNIC_REJ_NO_QP_AVAILABLE		= 1,
	XRNIC_REJ_NO_EE_AVAILABLE		= 2,
	XRNIC_REJ_NO_RESOURCE_AVAILABLE		= 3,
	XRNIC_REJ_TIMEOUT			= 4,
	XRNIC_REJ_UNSUPPORTED_REQ		= 5,
	XRNIC_REJ_INVALID_CM_ID			= 6,
	XRNIC_REJ_INVALID_QPN			= 7,
	XRNIC_REJ_RDC_NOT_EXIST			= 11,
	XRNIC_REJ_PRIM_LID_PORT_NOT_EXIST	= 13,
	XRNIC_REJ_INVALID_MTU			= 26,
	XRNIC_REJ_INSUFFICIENT_RESP_RESOURCE	= 27,
	XRNIC_REJ_CONSUMER_REJECT		= 28,
	XRNIC_REJ_DUPLICATE_LOCAL_CM_ID		= 30,
	XRNIC_REJ_UNSUPPORTED_CLASS_VERSION	= 31,
};

//mad common status field
struct mad_comm_status {
	__u8 busy:1;
	__u8 redir_reqd:1;
	__u8 invalid_field_code:3;
	__u8 reserved:3;
	__u8 class_specific;
} __packed;

#define XRNIC_MAD_BASE_VER	1
#define XRNIC_MAD_MGMT_CLASS	0x07
#define XRNIC_MAD_RESP_BIT	0x0
#define XRNIC_MAD_COMM_SEND	0x3
#define XRNIC_MAD_RESERVED	0x0

/* Management data gram (MAD's) */
struct mad //Size 256Byte
{
	__u8	base_ver;
	__u8	mgmt_class;
	__u8	class_version;
	__u8	resp_bit_method;
	struct mad_comm_status status;// 2 bytes
	__be16 class_specific;
	__be64 transaction_id;
	__be16 attribute_id;
	__be16 reserved;
	__be32 attrb_modifier;
	__be32 data[58];
} __packed;

struct req {
	__u32	local_cm_id;
	__u32	reserved1;
	__u8	service_id[8];
	__u8	local_ca_guid[8];
	__u32	reserved2;
	__u32	local_q_key;
	__u32	local_qpn:24;
	__u8	responder_resources:8;
	__u32	local_eecn:24;
	__u32	initiator_depth:8;
	__u32	remote_eecn:24;

	__u32	remote_cm_resp_tout:5;
	__u32	transport_svc_type:2;
	__u32	e2e_flow_control:1;
	__u8	start_psn[3];
	__u8	local_cm_resp_tout:5;
	__u8	retry_count: 3;
	__u16	p_key;
	__u8	path_packet_payload_mtu:4;
	__u8	rdc_exists:1;
	__u8	rnr_retry_count:3;
	__u8	max_cm_retries:4;
	__u8	srq:1;
	__u8	reserved3:3;
	__u16	primary_local_port_lid;
	__u16	primary_remote_port_lid;
	__u64	primary_local_port_gid[2];
	__u64	primary_remote_port_gid[2];
	__u32	primary_flow_label:20;
	__u32	reserved4:6;
	__u32	primary_packet_rate:6;
	__u32	primary_traffic_class:8;
	__u32	primary_hop_limit:8;
	__u32	primary_sl:4;
	__u32	primary_subnet_local:1;
	__u32	reserved5:3;
	__u32	primary_local_ack_tout:5;
	__u32	reserved6:3;
	__u32	alternate_local_port_lid:16;
	__u32	alternate_remote_port_lid:16;
	__u64	alternate_local_port_gid[2];
	__u64	alternate_remote_port_gid[2];
	__u32	alternate_flow_labe:20;
	__u32	reserved7:6;
	__u32	alternate_packet_rate:6;
	__u32	alternate_traffic_class:8;
	__u32	alternate_hop_limit:8;
	__u32	alternate_sl:4;
	__u32	alternate_subnet_local:1;
	__u32	reserved8:3;
	__u32	alternate_local_ack_timeout: 5;
	__u32	reserved9:3;
	__u8	private_data[92];
} __packed;

/* MRA Message contents */
/* Message Receipt Acknoldgement */
struct mra {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u8	message_mraed:2;
	__u8	reserved1:6;
	__u8	service_timeout:5;
	__u8	reserved2:3;
	__u8	private_data[222];
} __packed;

/* REJ Message contents */
struct rej {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u8	message_rejected:2;
	__u8	reserved1:6;
	__u8	reject_info_length:7;
	__u8	reserved2:1;
	__u16	reason;
	__u8	additional_reject_info[72];
	__u8	private_data[148];
} __packed;

/* REP Message contents */
struct rep {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u32	local_q_key;
	__u32	local_qpn:24;
	__u8	reserved1:8;
	__u32	local_ee_context:24;
	__u32	reserved2:8;
	__u8	start_psn[3];
	__u8	reserved3;
	__u8	responder_resources;
	__u8	initiator_depth;
	union {
	__u8	target_fail_end;
	__u8	target_ack_delay:5;
	__u8	fail_over_accepted:2;
	};
	__u8	end_end_flow_control:1;
	__u8	rnr_retry_count:3;
	__u8	sqr:1;
	__u8	reserved4:4;
	__u8	local_ca_guid[8];
	__u8	private_data[196];
} __packed;

/* RTU indicates that the connection is established,
 * and that the recipient
 * may begin transmitting
 */
struct rtu {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u8	private_data[224];
} __packed;

#define XRNIC_SEND_UD			0x64
#define XRNIC_SET_SOLICT_EVENT		0x0
#define XRNIC_RESET_SOLICT_EVENT	0x0
#define XRNIC_MIGRATION_REQ		0x0
#define XRNIC_PAD_COUNT			0x0
#define XRNIC_TRANSPORT_HDR_VER		0x0
#define XRNIC_DESTINATION_QP		0x1
#define XRNIC_RESERVED1			0x0
#define XRNIC_ACK_REQ			0x0
#define XRNIC_RESERVED2			0x0

struct bth {
	__u8	opcode;
	__u8	solicited_event:1;
	__u8	migration_req:1;
	__u8	pad_count:2;
	__u8	transport_hdr_ver:4;
	__be16	partition_key;
	__u8	reserved1;
	__u8	destination_qp[3];
	__u32	ack_request:1;
	__u32	reserved2:7;
	__u32	pkt_seq_num:24;
} __packed;

#define XRNIC_DETH_RESERVED 0
struct deth {
	__be32	q_key;
	__u8	reserved;
	__be32	src_qp:24;
} __packed;

/* DREQ request for communication release*/
struct dreq {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u32	remote_qpn_eecn:24;
	__u32	reserved:8;
	__u8	private_data[220];
} __packed;

/* DREP - reply to request for communication release */
struct drep {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u8	private_data[228];
} __packed;

/* LAP - load alternate path */
struct lap {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u32	reserved1;
	__u32	remote_QPN_EECN:24;
	__u32	remote_cm_response_timeout:5;
	__u32	reserved2:3;
	__u32	reserved3;
	__u32	alt_local_port_id:16;
	__u32	alt_remote_port_id:16;
	__u64	alt_local_port_gid[2];
	__u64	alt_remote_port_gid[2];
	__u32	alt_flow_label:20;
	__u32	reserved4:4;
	__u32	alt_traffic_class:8;
	__u32	alt_hope_limit:8;
	__u32	reserved5:2;
	__u32	alt_pkt_rate:6;
	__u32	alt_sl:4;
	__u32	alt_subnet_local:1;
	__u32	reserved6:3;
	__u32	alt_local_ack_timeout:5;
	__u32	reserved7:3;
	__u8	private_data[168];
} __packed;

/* APR - alternate path response */
struct apr {
	__u32	local_cm_id;
	__u32	remote_comm_id;
	__u8	additional_info_length;
	__u8	ap_status;
	__u8	reserved1[2];
	__u8	additional_info[72];
	__u8	private_data[148];
} __packed;

enum cm_establishment_states {
	CLASS_PORT_INFO		= 0x1,
	CONNECT_REQUEST		= 0x10, /* Request for connection */
	MSG_RSP_ACK		= 0x11, /* Message Response Ack */
	CONNECT_REJECT		= 0x12, /* Connect Reject */
	CONNECT_REPLY		= 0x13, /* Reply for request communication */
	READY_TO_USE		= 0x14, /* Ready to use */
	DISCONNECT_REQUEST	= 0x15, /* Receive Disconnect req */
	DISCONNECT_REPLY	= 0x16, /* Send Disconnect reply */
	SERVICE_ID_RESOLUTION_REQ	= 0x17,
	SERVICE_ID_RESOLUTION_REQ_REPLY	= 0x18,
	LOAD_ALTERNATE_PATH	= 0x19,
	ALTERNATE_PATH_RESPONSE	= 0x1a,
};

#define XRNIC_ETH_ALEN		6
#define XRNIC_ETH_P_IP		0x0800
#define XRNIC_ETH_P_ARP		0x0806
#define XRNIC_ETH_HLEN		14
#define XRNIC_ICRC_SIZE		4

//Ethernet header
struct ethhdr_t {
	unsigned char	h_dest[XRNIC_ETH_ALEN];
	unsigned char	h_source[XRNIC_ETH_ALEN];
	__be16		eth_type; /*< packet type ID field */
} __packed;

struct ipv4hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	ihl:4,
	version:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8	version:4, /*< Internet Header Length */
	ihl:4;	/*< Version */
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__u8 tos; /*< Type of service */
	__be16 total_length; /*< Total length */
	__be16 id; /*< Identification */
	u16	frag_off;	/*< Fragment offset */
	__u8	time_to_live;	/*< Time to live */
	__u8	protocol;	/*< Protocol */
	__be16	hdr_chksum;	/*< Header checksum */
	__be32	src_addr;	/*< Source address */
	__be32 dest_addr;	/*< Destination address */
} __packed;

struct qp_cm_pkt {
	struct ethhdr_t	eth; //14 Byte
	union {
		struct ipv4hdr ipv4;	//20 bytes
		struct ipv4hdr ipv6;	//20 bytes
	} ip;
	struct udphdr	udp;	//8 Byte
	struct bth	bth;	//12 Bytes
	struct deth	deth;	//8 Byte
	struct mad	mad;	//[XRNIC_MAD_HEADER + XRNIC_MAD_DATA]
} __packed;

/*
 * RoCEv2 packet for receiver. Duplicated for ease of code readability.
 */
struct qp_cm_pkt_hdr_ipv4 {
	struct ethhdr_t	eth;	//14 Byte
	struct ipv4hdr	ipv4;
	struct udphdr	udp;	//8 Byte
	struct bth	bth;
	struct deth	deth;	//8 Byte
	struct mad	mad;	//[XRNIC_MAD_HEADER + XRNIC_MAD_DATA]
} __packed;

struct qp_cm_pkt_hdr_ipv6 {
	struct ethhdr_t	eth;	//14 Byte
	struct ipv6hdr	ipv6;
	struct udphdr	udp;	//8 Byte
	struct bth	bth;
	struct deth	deth;	//8 Byte
	struct mad	mad;	//[XRNIC_MAD_HEADER + XRNIC_MAD_DATA]
} __packed;

/* MAD Packet validation defines */
#define MAD_BASIC_VER	1
#define OPCODE_SEND_UD	0x64

#define MAD_SUBNET_CLASS	0x1
#define MAD_DIRECT_SUBNET_CLASS	0x81

#define MAD_SEND_CM_MSG		0x03
#define MAD_VERF_FAILED -1
#define MAD_VERF_SUCCESS 0

#ifdef __cplusplus
	}
#endif

#endif /* _XRNIC_ROCEV2_H*/
