/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */
#ifndef _CM_H
#define _CM_H

#ifdef __cplusplus
	extern "C" {
#endif

/***************************** Include Files ********************************/
#include <linux/types.h>
#include <linux/interrupt.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_cm.h>

/************************** Constant Definitions *****************************/

/* EXTRA Bytes for Invariant CRC */
#define	ERNIC_INV_CRC	4
/* ERNIC Doesn't have Variant CRC for P2P */
#define	ERNIC_VAR_CRC	0
#define	EXTRA_PKT_LEN	(ERNIC_INV_CRC + ERNIC_VAR_CRC)
/* As per RoCEv2 Annex17, SRC PORT can be Fixed for ordering issues.
 * So, to make things simple, ERNIC also uses constant udp source port
 */
#define ERNIC_UDP_SRC_PORT	0xA000

#define SET_VAL(start, size, val) ((((val) & ((1U << (size)) - 1)) << (start)))
#define GET_VAL(start, size, val) (((val) >> (start)) & ((1U << (size)) - 1))
#define	BTH_SET(FIELD, v)	SET_VAL(BTH_##FIELD##_OFF, \
						BTH_##FIELD##_SZ, v)
#define	DETH_SET(FIELD, v)	SET_VAL(DETH_##FIELD##_OFF, \
						DETH_##FIELD##_SZ, v)

#define SET_HDR_OFFSET(ptr, off) ((ptr) += off)
#define	SET_CM_HDR(ptr)		SET_HDR_OFFSET(ptr, sizeof(struct ib_mad_hdr))
#define	SET_ETH_HDR(ptr)	SET_HDR_OFFSET(ptr, 0)
#define	SET_IP_HDR(ptr)		SET_HDR_OFFSET(ptr, sizeof(struct ethhdr))
#define	SET_NET_HDR(ptr)	SET_HDR_OFFSET(ptr, sizeof(struct iphdr))
#define	SET_BTH_HDR(ptr)	SET_HDR_OFFSET(ptr, sizeof(struct udphdr))
#define	SET_DETH_HDR(ptr)	SET_HDR_OFFSET(ptr, IB_BTH_BYTES)
#define	SET_MAD_HDR(ptr)	SET_HDR_OFFSET(ptr, IB_DETH_BYTES)

#define	CMA_VERSION		0
#define	IB_ENFORCED_QEY		0x80010000
#define IB_CM_CLASS_VER		2
/*****************************************************************************/
struct ib_bth {
	__be32	offset0;
#define	BTH_PKEY_OFF	 0
#define	BTH_PKEY_SZ	16
#define	BTH_TVER_OFF	16
#define	BTH_TVER_SZ	 4
#define	BTH_PAD_OFF	20
#define	BTH_PAD_SZ	 2
#define	BTH_MIG_OFF	22
#define	BTH_MIG_SZ	 1
#define	BTH_SE_OFF	23
#define	BTH_SE_SZ	 1
#define	BTH_OPCODE_OFF	24
#define	BTH_OPCODE_SZ	 8
	__be32	offset4;
#define	BTH_DEST_QP_OFF	 0
#define	BTH_DEST_QP_SZ	24
	__be32	offset8;
#define	BTH_PSN_OFF	 0
#define	BTH_PSN_SZ	24
#define	BTH_ACK_OFF	31
#define	BTH_ACK_SZ	 1
};

struct ib_deth {
	__be32	offset0;
#define	DETH_QKEY_OFF	 0
#define	DETH_QKEY_SZ	32
	__be32	offset4;
#define	DETH_SQP_OFF	 0
#define	DETH_SQP_SZ	24
};

struct cma_rtu {
	u32	local_comm_id;
	u32	remote_comm_id;
	u8	private_data[224];
};

union cma_ip_addr {
	struct in6_addr ip6;
	struct {
		__be32 pad[3];
		__be32 addr;
	} ip4;
};

/* CA11-1: IP Addressing CM REQ Message Private Data Format */
struct cma_hdr {
	u8 cma_version;
	u8 ip_version;	/* IP version: 7:4 */
	__be16 port;
	union cma_ip_addr src_addr;
	union cma_ip_addr dst_addr;
};

enum transport_svc_type {
	XRNIC_SVC_TYPE_RC = 0,
	XRNIC_SVC_TYPE_UC,
	XRNIC_SVC_TYPE_RD,
	XRNIC_SVC_TYPE_RSVD,
};

extern struct list_head cm_id_list;

void xrnic_qp1_send_mad_pkt(void *send_sgl_temp,
			    struct xrnic_qp_attr *qp1_attr, u32 send_pkt_size);
void xrnic_reset_io_qp(struct xrnic_qp_attr *qp_attr);
void fill_ipv4_cm_req(struct xrnic_rdma_cm_id *cm_id,
		      char *send_sgl_qp1, int cm_req_size);
char *fill_ipv4_headers(struct xrnic_rdma_cm_id *cm_id,
			char *send_sgl_qp1, int cm_req_size);
int xrnic_cm_establishment_handler(void *rq_buf);
char *fill_mad_common_header(struct xrnic_rdma_cm_id *cm_id,
			     char *send_sgl_qp1, int cm_req_size,
			     int cm_attr);
void xrnic_prepare_initial_headers(struct xrnic_qp_attr *qp_attr,
				   void *rq_buf);
void xrnic_cm_msg_rsp_ack_handler(struct xrnic_qp_attr *qp_attr, void *rq_buf);
void xrnic_cm_disconnect_send_handler(struct xrnic_qp_attr *qp_attr);
void xrnic_cm_prepare_rej(struct xrnic_qp_attr *qp_attr,
			  enum xrnic_rej_reason reason,
			  enum xrnic_msg_rej msg);
void xrnic_send_mad(void *send_buf, u32 size);
int xrnic_identify_remote_host(void *rq_buf, int qp_num);
void xrnic_mad_pkt_recv_intr_handler(unsigned long data);

struct ernic_cm_req {
	u32 local_comm_id;
	u32 rsvd1;
	__u64 service_id;
	__u64 local_ca_guid;
	u32 rsvd2;
	u32 local_qkey;
	u32 offset32;
	u32 offset36;
	u32 offset40;
	u32 offset44;
	u16 pkey;
	u8 offset50;
	u8 offset51;
	u16 local_lid;
	u16 remote_lid;
	union ib_gid local_gid;
	union ib_gid remote_gid;
	u32 offset88;
	u8 traffic_class;
	u8 hop_limit;
	u8 offset94;
	u8 offset95;
	u8 rsvd3[45];
	u8 private_data[IB_CM_REQ_PRIVATE_DATA_SIZE];
} __packed;
#ifdef __cplusplus
	}
#endif

#endif /* _CM_H*/
