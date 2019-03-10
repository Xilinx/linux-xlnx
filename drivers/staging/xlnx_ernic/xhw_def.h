/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef _XRNIC_HW_DEF_H
#define _XRNIC_HW_DEF_H

#ifdef __cplusplus
	extern "C" {
#endif

#include <linux/types.h>
#include "xhw_config.h"

#define XRNIC_MAX_QP_ENABLE		XRNIC_HW_MAX_QP_ENABLE
#define XRNIC_MAX_QP_SUPPORT		XRNIC_HW_MAX_QP_SUPPORT
#define XRNIC_MAX_PORT_SUPPORT		0xFFFE
#define	XRNIC_REG_WIDTH			32
#define XRNIC_QPS_ENABLED		XRNIC_MAX_QP_ENABLE
#define XRNIC_QP1_SEND_PKT_SIZE		512
#define XRNIC_FLOW_CONTROL_VALUE	XRNIC_HW_FLOW_CONTROL_VALUE
#define XRNIC_CONFIG_XRNIC_EN		0x1
#define XRNIC_UDP_SRC_PORT		0x12B7
#define XRNIC_CONFIG_IP_VERSION		(0x1 << 1)
#define XRNIC_CONFIG_DEPKT_BYPASS_EN	(0x1 << 2)
#define XRNIC_CONFIG_ERR_BUF_EN		(0x1 << 5)
#define XRNIC_CONFIG_FLOW_CONTROL_EN	(XRNIC_FLOW_CONTROL_VALUE << 6)
#define XRNIC_CONFIG_NUM_QPS_ENABLED	(XRNIC_QPS_ENABLED << 8)
#define XRNIC_CONFIG_UDP_SRC_PORT	(XRNIC_UDP_SRC_PORT << 16)

#define XRNIC_RQ_CQ_INTR_STS_REG_SUPPORTED	1

/* Clear the the interrupt writing that bit to interrupt status register.*/
#define RDMA_READ	4
#define RDMA_SEND	2
#define RDMA_WRITE	0

#define XRNIC_QP_TIMEOUT_RETRY_CNT		0x3	/*0x3*/
#define XRNIC_QP_TIMEOUT_RNR_NAK_TVAL		0x1F	/*MAX*/
#define XRNIC_QP_TIMEOUT_CONFIG_TIMEOUT		0x1F	/*MAX 0x1f*/
#define XRNIC_QP_TIMEOUT_CONFIG_RETRY_CNT	\
				(XRNIC_QP_TIMEOUT_RETRY_CNT << 8)
#define XRNIC_QP_TIMEOUT_CONFIG_RNR_RETRY_CNT	\
				(XRNIC_QP_TIMEOUT_RETRY_CNT << 11)
#define XRNIC_QP_TIMEOUT_CONFIG_RNR_NAK_TVAL	\
				(XRNIC_QP_TIMEOUT_RNR_NAK_TVAL << 16)

#define XRNIC_QP_PMTU				0x4
#define XRNIC_QP_MAX_RD_OS			0xFF
#define XRNIC_QP_RQ_BUFF_SZ			0x2
#define XRNIC_QP1_RQ_BUFF_SZ			0x02
#define XRNIC_QP_CONFIG_QP_ENABLE		0x1
#define XRNIC_QP_CONFIG_ACK_COALSE_EN		BIT(1)
#define XRNIC_QP_CONFIG_RQ_INTR_EN		BIT(2)
#define XRNIC_QP_CONFIG_CQE_INTR_EN		BIT(3)
#define XRNIC_QP_CONFIG_HW_HNDSHK_DIS		BIT(4)
#define XRNIC_QP_CONFIG_CQE_WRITE_EN		BIT(5)
#define XRNIC_QP_CONFIG_UNDER_RECOVERY		BIT(6)
#define XRNIC_QP_CONFIG_IPV6_EN			BIT(7)
#define XRNIC_QP_CONFIG_PMTU			(0x4 << 8)
#define XRNIC_QP_CONFIG_PMTU_256		(0x0 << 8)
#define XRNIC_QP_CONFIG_PMTU_512		(0x1 << 8)
#define XRNIC_QP_CONFIG_PMTU_1024		(0x2 << 8)
#define XRNIC_QP_CONFIG_PMTU_2048		(0x3 << 8)
#define XRNIC_QP_CONFIG_PMTU_4096		(0x4 << 8)
#define XRNIC_QP_RQ_BUF_SIZ_DIV			(256)
#define XRNIC_QP_RQ_BUF_CFG_REG_BIT_OFS		(16)
#define XRNIC_QP_CONFIG_RQ_BUFF_SZ(x)	(((x) / XRNIC_QP_RQ_BUF_SIZ_DIV)\
					 << XRNIC_QP_RQ_BUF_CFG_REG_BIT_OFS)
#define XRNIC_QP1_CONFIG_RQ_BUFF_SZ	(XRNIC_QP1_RQ_BUFF_SZ << 16)

#define XRNIC_QP_PARTITION_KEY			0xFFFF
#define XRNIC_QP_TIME_TO_LIVE			0x40

#define XRNIC_QP_ADV_CONFIG_TRAFFIC_CLASS	0x3F
#define XRNIC_QP_ADV_CONFIG_TIME_TO_LIVE	(XRNIC_QP_TIME_TO_LIVE << 8)
#define XRNIC_QP_ADV_CONFIG_PARTITION_KEY	(XRNIC_QP_PARTITION_KEY << 16)

#define XRNIC_REJ_RESEND_COUNT	3
#define XRNIC_REP_RESEND_COUNT	3
#define XRNIC_DREQ_RESEND_COUNT	3

#define XNVEMEOF_RNIC_IF_RHOST_BASE_ADDRESS 0x8c000000
#define XRNIC_CONFIG_ENABLE		1
#define XRNIC_RESERVED_SPACE		0x4000
#define XRNIC_NUM_OF_TX_HDR		128
#define XRNIC_SIZE_OF_TX_HDR		128
#define XRNIC_NUM_OF_TX_SGL		256
#define XRNIC_SIZE_OF_TX_SGL		64
#define XRNIC_NUM_OF_BYPASS_BUF		32
#define XRNIC_SIZE_OF_BYPASS_BUF	512
#define XRNIC_NUM_OF_ERROR_BUF		64
#define XRNIC_SIZE_OF_ERROR_BUF		256
#define XRNIC_OUT_ERRST_Q_NUM_ENTRIES	0x40
#define XRNIC_OUT_ERRST_Q_WRPTR		0x0
#define XRNIC_IN_ERRST_Q_NUM_ENTRIES	0x40
#define XRNIC_IN_ERRST_Q_WRPTR		0x0
#define XRNIC_NUM_OF_DATA_BUF		4096
#define XRNIC_SIZE_OF_DATA_BUF		4096
#define XRNIC_NUM_OF_RESP_ERR_BUF	64
#define XRNIC_SIZE_OF_RESP_ERR_BUF	256
#define XRNIC_MAD_HEADER		24
#define XRNIC_MAD_DATA			232
#define XRNIC_RECV_PKT_SIZE		512
#define XRNIC_SEND_PKT_SIZE		64
#define XRNIC_SEND_SGL_SIZE		4096
#define XRNIC_MAX_SEND_SGL_SIZE		4096
#define XRNIC_MAX_SEND_PKT_SIZE		4096
#define XRNIC_MAX_RECV_PKT_SIZE		4096
#define XRNIC_MAX_SQ_DEPTH		256
#define XRNIC_MAX_RQ_DEPTH		256
#define XRNIC_SQ_DEPTH			128
#define XRNIC_RQ_DEPTH			64
#define XRNIC_RQ_WRPTR_DBL		0xBC004000
#define XRNIC_BYPASS_BUF_WRPTR		0xBC00C000
#define XRNIC_ERROR_BUF_WRPTR		0xBC010000

#define	PKT_VALID_ERR_INTR_EN		0x1
#define	MAD_PKT_RCVD_INTR_EN		(0x1 << 1)
#define	BYPASS_PKT_RCVD_INTR_EN		(0x1 << 2)
#define	RNR_NACK_GEN_INTR_EN		(0x1 << 3)
#define	WQE_COMPLETED_INTR_EN		(0x1 << 4)
#define	ILL_OPC_SENDQ_INTR_EN		(0x1 << 5)
#define	QP_PKT_RCVD_INTR_EN		(0x1 << 6)
#define	FATAL_ERR_INTR_EN		(0x1 << 7)
#define	ERNIC_MEM_REGISTER

#define XRNIC_INTR_ENABLE_DEFAULT 0x000000FF
#define XRNIC_VALID_INTR_ENABLE		0

/* XRNIC Controller global configuration registers */

struct xrnic_conf {
	__u32 xrnic_en:1;
	__u32 ip_version:1;	//IPv6 or IPv4
	__u32 depkt_bypass_en:1;
	__u32 reserved:5;
	__u32 num_qps_enabled:8;
	__u32 udp_src_port:16;
} __packed;

struct tx_hdr_buf_sz {
	__u32 num_hdrs:16;
	__u32 buffer_sz:16;	//in bytes
} __packed;

struct tx_sgl_buf_sz {
	__u32 num_sgls:16;
	__u32 buffer_sz:16;	//in bytes
} __packed;

struct bypass_buf_sz {
	__u32 num_bufs:16;
	__u32 buffer_sz:16;
} __packed;

struct err_pkt_buf_sz {
	__u32 num_bufs:16;
	__u32 buffer_sz:16;
} __packed;

struct timeout_conf {
	__u32 timeout:5;
	__u32 reserved:3;
	__u32 retry_cnt:3;
	__u32 retry_cnt_rnr:3;
	__u32 reserved1:2;
	__u32 rnr_nak_tval:5;
	__u32 reserved2:11;

} __packed;

struct out_errsts_q_sz {
	__u32 num_entries:16;
	__u32 reserved:16;
} __packed;

struct in_errsts_q_sz {
	__u32 num_entries:16;
	__u32 reserved:16;
} __packed;

struct inc_sr_pkt_cnt {
	__u32	inc_send_cnt:16;
	__u32	inc_rresp_cnt:16;
} __packed;

struct inc_am_pkt_cnt {
	__u32	inc_acknack_cnt:16;
	__u32	inc_mad_cnt:16;
} __packed;

struct out_io_pkt_cnt {
	__u32	inc_send_cnt:16;
	__u32	inc_rw_cnt:16;
} __packed;

struct out_am_pkt_cnt {
	__u32	inc_acknack_cnt:16;
	__u32	inc_mad_cnt:16;
} __packed;

struct last_in_pkt {
	__u32	opcode:8;
	__u32	qpid:8;
	__u32	psn_lsb:16;
} __packed;

struct last_out_pkt {
	__u32	opcode:8;
	__u32	qpid:8;
	__u32	psn_lsb:16;
} __packed;

/*Interrupt register definition.*/
struct intr_en {
	__u32 pkt_valdn_err_intr_en:1;
	__u32 mad_pkt_rcvd_intr_en:1;
	__u32 bypass_pkt_rcvd_intr_en:1;
	__u32 rnr_nack_gen_intr_en:1;
	__u32 wqe_completed_i:1;
	__u32 ill_opc_in_sq_intr_en:1;
	__u32 qp_pkt_rcvd_intr_en:1;
	__u32 fatal_err_intr_en:1;
	__u32 reverved:24;
} __packed;

struct data_buf_sz {
	__u16 num_bufs;
	__u16 buffer_sz;
};

struct resp_err_buf_sz {
	__u16 num_bufs;
	__u16 buffer_sz;
};

/*Global register configuration*/
struct xrnic_ctrl_config {
	struct xrnic_conf	xrnic_conf;
	__u32 xrnic_adv_conf;
	__u32 reserved1[2];
	__u32 mac_xrnic_src_addr_lsb;
	__u32 mac_xrnic_src_addr_msb;
	__u32 reserved2[2];
	__u32 ip_xrnic_addr1;	//0x0020
	__u32 ip_xrnic_addr2;	//0x0024
	__u32 ip_xrnic_addr3;	//0x0028
	__u32 ip_xrnic_addr4;	//0x002C
	__u32 tx_hdr_buf_ba;	//0x0030
	__u32 reserved_0x34;	//0x0034
	struct tx_hdr_buf_sz tx_hdr_buf_sz; //0x0038
	__u32 reserved_0x3c;

	__u32 tx_sgl_buf_ba;	//0x0040
	__u32 reserved_0x44;	//0x0044
	struct tx_sgl_buf_sz tx_sgl_buf_sz; //0x0048
	__u32 reserved_0x4c;

	__u32 bypass_buf_ba;	//0x0050
	__u32 reserved_0x54;	//0x0054
	struct bypass_buf_sz bypass_buf_sz; //0x0058
	__u32 bypass_buf_wrptr;	//0x005C
	__u32 err_pkt_buf_ba;	//0x0060
	__u32 reserved_0x64;	//0x0064
	struct err_pkt_buf_sz err_pkt_buf_sz;	//0x0068
	__u32 err_buf_wrptr;	//0x006C
	__u32 ipv4_address;	//0x0070
	__u32 reserved_0x74;

	__u32 out_errsts_q_ba;	//0x0078
	__u32 reserved_0x7c;
	struct out_errsts_q_sz out_errsts_q_sz;	//0x0080
	__u32 out_errsts_q_wrptr; //0x0084

	__u32 in_errsts_q_ba;	//0x0088
	__u32 reserved_0x8c;
	struct in_errsts_q_sz in_errsts_q_sz;	//0x0090
	__u32  in_errsts_q_wrptr;	//0x0094

	__u32 reserved_0x98;	//0x0098
	__u32 reserved_0x9c;	//0x009C

	__u32 data_buf_ba;	//0x00A0
	__u32 reserved_0xa4;	//0x00A4
	struct data_buf_sz data_buf_sz; //0x00A8

	__u32	cnct_io_conf;	//0x00AC

	__u32 resp_err_pkt_buf_ba;	//0x00B0
	__u32 reserved_0xb4;	//0x00B4
	struct resp_err_buf_sz resp_err_buf_sz; //0x00B8

	__u32 reserved3[17];	//0x0095

	struct inc_sr_pkt_cnt inc_sr_pkt_cnt;//0x0100
	struct inc_am_pkt_cnt inc_am_pkt_cnt;//0x0104
	struct out_io_pkt_cnt out_io_pkt_cnt;//0x108
	struct out_am_pkt_cnt out_am_pkt_cnt;//0x010c
	struct last_in_pkt last_in_pkt;	//0x0110
	struct last_out_pkt last_out_pkt;	//0x0114

	__u32 inv_dup_pkt_cnt;	//0x0118 incoming invalid duplicate

	__u32 rnr_in_pkt_sts;	//0x011C
	__u32 rnr_out_pkt_sts;	//0x0120

	__u32 wqe_proc_sts;	//0x0124

	__u32 pkt_hdr_vld_sts;	//0x0128
	__u32 qp_mgr_sts;	//0x012C

	__u32 incoming_all_drop_count; //0x130
	__u32 incoming_nack_pkt_count; //0x134
	__u32 outgoing_nack_pkt_count; //0x138
	__u32 resp_handler_status; //0x13C

	__u32 reserved4[16];

	struct intr_en intr_en;	//0x0180
	__u32 intr_sts;		//0x0184
	__u32 reserved5[2];
	__u32 rq_intr_sts_1;	//0x0190
	__u32 rq_intr_sts_2;	//0x0194
	__u32 rq_intr_sts_3;	//0x0198
	__u32 rq_intr_sts_4;	//0x019C
	__u32 rq_intr_sts_5;	//0x01A0
	__u32 rq_intr_sts_6;	//0x01A4
	__u32 rq_intr_sts_7;	//0x01A8
	__u32 rq_intr_sts_8;	//0x01AC

	__u32 cq_intr_sts_1;	//0x01B0
	__u32 cq_intr_sts_2;	//0x01B4
	__u32 cq_intr_sts_3;	//0x01B8
	__u32 cq_intr_sts_4;	//0x01BC
	__u32 cq_intr_sts_5;	//0x01B0
	__u32 cq_intr_sts_6;	//0x01B4
	__u32 cq_intr_sts_7;	//0x01B8
	__u32 cq_intr_sts_8;	//0x01BC

	__u32 reserved6[12];
};

struct qp_conf {
	__u32 qp_enable:1;
	__u32 ack_coalsc_en:1;
	__u32 rq_intr_en:1;
	__u32 cq_intr_en:1;
	__u32 hw_hndshk_dis:1;
	__u32 cqe_write_en:1;
	__u32 qp_under_recovery:1;
	__u32 ip_version:1;
	__u32 pmtu :3;
	__u32 reserved2:5;
	__u32 rq_buf_sz:16; //RQ buffer size (in multiples of 256B)
} __packed;

struct qp_adv_conf {
	__u32 traffic_class:6;
	__u32 reserved1 :2;
	__u32 time_to_live:8;
	__u32 partition_key:16;
} __packed;

struct time_out {
	__u32 timeout:5;
	__u32 reserved1:3;
	__u32 retry_cnt:3;
	__u32 reserved2:5;
	__u32 rnr_nak_tval:5;
	__u32 reserved3:3;
	__u32 curr_retry_cnt:3;
	__u32 reserved4:2;
	__u32 curr_rnr_nack_cnt:3;
	__u32 reserved:1;
} __packed;

struct qp_status {
	__u32	qp_fatal:1;
	__u32	rq_ovfl:1;
	__u32	sq_full:1;
	__u32	osq_full:1;
	__u32	cq_full:1;
	__u32	reserved1:4;
	__u32	sq_empty:1;
	__u32	osq_empty:1;
	__u32	qp_retried:1;
	__u32	reserved2:4;
	__u32	nak_syndr_rcvd:7;
	__u32	reserved3:1;
	__u32	curr_retry_cnt:3;
	__u32	reserved4:1;
	__u32	curr_rnr_nack_cnt:3;
	__u32	reserved5:1;
} __packed;

//This structure is applicable to the rdma queue pair other than QP1.
struct rq_buf_ba_ca {
	__u32	reserved:8;		//0x308
	__u32	rq_buf_ba:24;
} __packed;

struct sq_ba {
	__u32 reserved1:5;		//0x310
	__u32 sq_ba:27;
} __packed;

struct cq_ba {
	__u32 reserved2:5;		//0x318
	__u32 cq_ba:27;
} __packed;

struct cq_head {
	__u32 cq_head:16;	//0x330
	__u32 reserved5:16;
} __packed;

struct rq_ci_db {
	__u32 rq_ci_db:16;	//0x334
	__u32 reserved6:16;
} __packed;

struct sq_pi_db {
	__u32 sq_pi_db:16;	//0x338
	__u32 reserved7:16;
} __packed;

struct q_depth {
	__u32 sq_depth:16;	//0x33c
	__u32 cq_depth:16;
} __packed;

struct sq_psn {
	__u32 sq_psn:24;	//0x340
	__u32 reserved8:8;
} __packed;

struct last_rq_req {
	__u32 rq_psn:24;	//0x344
	__u32 rq_opcode:8;
} __packed;

struct dest_qp_conf {
	__u32 dest_qpid:24;	//0x348
	__u32 reserved9:8;
} __packed;

struct stat_ssn {
	__u32 exp_ssn:24;	//0x380
	__u32 reserved10:8;
} __packed;

struct stat_msn {
	__u32 curr_msn:24;	//0x384
	__u32 reserved11:8;

} __packed;

struct stat_curr_sqptr_pro {
	__u32 curr_sqptr_proc:16;
	__u32 reserved12:16;
} __packed;

struct stat_resp_psn {
	__u32 exp_resp_psn:24;
	__u32 reserved:8;
} __packed;

struct stat_rq_buf_ca {
	__u32 reserved:8;
	__u32 rq_buf_ca:24;
} __packed;

/*QP1 is special attribue for all the management packets as per ROCEv2 spec */
struct rdma_qp1_attr {
	struct qp_conf qp_conf;	//0x200
	struct qp_adv_conf qp_adv_conf; //0x204
	struct rq_buf_ba_ca rq_buf_ba_ca; //0x208
	__u32 reserved_0x20c;	//0x20c
	struct sq_ba sq_ba;	//0x210
	__u32 reserved_0x214;	//0x214
	struct cq_ba cq_ba;	//0x218
	__u32 reserved_0x21c;	//0x2c0
	__u32 rq_wrptr_db_add;	//0x220
	__u32 reserved_0x224;	//0x224
	__u32 sq_cmpl_db_add;	//0x228
	__u32 reserved_0x22c;	//0x22c
	struct cq_head cq_head; //0x230
	struct rq_ci_db rq_ci_db; //0x234
	struct sq_pi_db sq_pi_db; //0x238
	struct q_depth q_depth; //0x23c
	__u32 reserved1[2];	//0x240
	struct dest_qp_conf dest_qp_conf; //0x248
	struct timeout_conf timeout_conf; //0x24C
	__u32 mac_dest_addr_lsb; //0x250
	__u32 mac_dest_addr_msb; //0x254
	__u32 reserved2[2];
	__u32 ip_dest_addr1; //0x260
	__u32 ip_dest_addr2; //0x264
	__u32 ip_dest_addr3; //0x268
	__u32 ip_dest_addr4; //0x26C
	__u32 reserved3[6]; //0x270-287(inclusive)
	struct qp_status qp_status; //0x288
	__u32 reserved4[2]; //0x240-287(inclusive)
	struct stat_rq_buf_ca stat_rq_buf_ca;//0x294
	__u32 reserved5[26]; //0x298-2Ff(inclusive)
};

/* General RDMA QP attribute*/
struct rdma_qp_attr {
	struct qp_conf qp_conf;	//0x300
	struct qp_adv_conf qp_adv_conf; //0x304
	struct rq_buf_ba_ca rq_buf_ba_ca;//0x308
	__u32 reserved_0x30c;	//0x30c
	struct sq_ba sq_ba; //0x310
	__u32 reserved_0x314;	//0x214
	struct cq_ba cq_ba;	//0x318
	__u32 reserved_0x31c;	//0x31c
	__u32 rq_wrptr_db_add;	//0x320
	__u32 reserved_0x324;	//0x324
	__u32 sq_cmpl_db_add;	//0x328
	__u32 reserved_0x32c;	//0x22c
	struct cq_head cq_head;	//0x330
	struct rq_ci_db rq_ci_db;//0x334
	struct sq_pi_db sq_pi_db; //0x338
	struct q_depth q_depth;//0x33c
	struct sq_psn sq_psn; //0x340
	struct last_rq_req last_rq_req;//0x344
	struct dest_qp_conf dest_qp_conf; //0x348
	struct timeout_conf timeout_conf; //0x34C
	__u32 mac_dest_addr_lsb; //0x350
	__u32 mac_dest_addr_msb; //0x354
	__u32 reserved1[2];	//0x358
	__u32 ip_dest_addr1;	//0x360
	__u32 ip_dest_addr2;	//0x364
	__u32 ip_dest_addr3;	//0x368
	__u32 ip_dest_addr4;	//0x36C
	__u32 reserved2[4];
	struct stat_ssn stat_ssn;//0x380
	struct stat_msn stat_msn;//0x384
	struct qp_status qp_status; //0x388
	struct stat_curr_sqptr_pro stat_curr_sqptr_pro;//0x38C
	struct stat_resp_psn stat_resp_psn; //0x0390
	struct stat_rq_buf_ca stat_rq_buf_ca;//0x0394
	__u32 stat_wqe; //0x398
	__u32 stat_rq_pi_db; //0x39C
#ifdef ERNIC_MEM_REGISTER
	__u32 reserved3[4];
	__u32	pd;
	__u32	reserved[19];
#else
	__u32 reserved3[24];
#endif
};

union ctx {	// 2 Byte
	__u16 context;
	__u16 wr_id;
} __packed;

//Work request 64Byte size
struct wr {
	union ctx ctx;		// 2 Byte
	__u8 reserved1[2];
	__u32 local_offset[2];
	__u32 length;
	__u8 opcode;
	__u8 reserved2[3];
	__u32 remote_offset[2];
	__u32 remote_tag;
	__u32 completion_info[4];
	__u8 reserved4[16];
} __packed;

union ctxe {
	__u16 context :16;
	__u16 wr_id:16;
} __packed;

//Completion Queue Entry 16 Byte
struct cqe {
	union ctxe ctxe;	// 2 Byte
	__u8 opcode;
	__u8 err_flag;
} __packed;

struct xrnic_reg_map {
	struct xrnic_ctrl_config xrnic_ctrl_config;
	struct rdma_qp1_attr rdma_qp1_attr;
	struct rdma_qp_attr rdma_qp_attr[255];

};

struct xrnic_memory_map {
	struct xrnic_reg_map	*xrnic_regs;
	u64			xrnic_regs_phys;
	void			*send_sgl;
	u64			send_sgl_phys;
	void			*cq_ba;
	u64			cq_ba_phys;
	void			*rq_buf_ba_ca;
	u64			rq_buf_ba_ca_phys;
	struct wr		*sq_ba;
	u64			sq_ba_phys;
	void			*tx_hdr_buf_ba;
	u64			tx_hdr_buf_ba_phys;
	void			*tx_sgl_buf_ba;
	u64			tx_sgl_buf_ba_phys;
	void			*bypass_buf_ba;
	u64			bypass_buf_ba_phys;
	void			*err_pkt_buf_ba;
	u64			err_pkt_buf_ba_phys;
	void			*out_errsts_q_ba;
	u64			out_errsts_q_ba_phys;
	void			*in_errsts_q_ba;
	u64			in_errsts_q_ba_phys;
	void			*rq_wrptr_db_add;
	u64			rq_wrptr_db_add_phys;
	void			*sq_cmpl_db_add;
	u64			sq_cmpl_db_add_phys;
	void			*stat_rq_buf_ca;
	u64			stat_rq_buf_ca_phys;
	void			*data_buf_ba;
	u64			data_buf_ba_phys;
	u64			resp_err_pkt_buf_ba_phys;
	void			*resp_err_pkt_buf_ba;
	u32			intr_en;
	u32			cq_intr[8];
	u32			rq_intr[8];
	u64			xrnicif_phys;
};

#ifdef __cplusplus
	}
#endif

#endif /* _XRNIC_HW_DEF_H*/
