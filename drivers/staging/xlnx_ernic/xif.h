/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef _XRNIC_IF_H
#define _XRNIC_IF_H

#ifdef __cplusplus
	extern "C" {
#endif

#include <linux/types.h>
#include <linux/udp.h>

#define XRNIC_MAX_CHILD_CM_ID		255
#define XRNIC_CM_PRVATE_DATA_LENGTH	32

enum xrnic_wc_event {
	XRNIC_WC_RDMA_WRITE = 0x0,
	XRNIC_WC_SEND = 0x2,
	XRNIC_WC_RDMA_READ = 0x4,
};

union xrnic_ctxe {	// 2 Byte
	__u16 context :16;
	__u16 wr_id:16;
} __packed;

struct xrnic_cqe {
	union xrnic_ctxe ctxe;	// 2 Byte
	__u8 opcode;		// 1 Byte
	__u8 err_flag;		// 1 Byte
} __packed;

enum xrnic_port_space {
	XRNIC_PS_SDP	= 0x0001,
	XRNIC_PS_IPOIB	= 0x0002,
	XRNIC_PS_IB	= 0x013F,
	XRNIC_PS_TCP	= 0x0106,
	XRNIC_PS_UDP	= 0x0111,
};

enum xrnic_cm_error {
	XRNIC_INVALID_CM_ID		= 2,
	XRNIC_INVALID_CM_OUTSTANDING	= 3,
	XRNIC_INVALID_QP_ID		= 4,
	XRNIC_INVALID_QP_INIT_ATTR	= 5,
	XRNIC_INVALID_NUM_CHILD		= 6,
	XRNIC_INVALID_CHILD_ID		= 7,
	XRNIC_INVALID_CHILD_NUM		= 8,
	XRNIC_INVALID_QP_TYPE		= 9,
	XRNIC_INVALID_PORT		= 10,
	XRNIC_INVALID_ADDR		= 11,
	XRNIC_INVALID_PKT_CNT		= 12,
	XRNIC_INVALID_ADDR_TYPE		= 13,
	XRNIC_INVALID_QP_CONN_PARAM	= 14,
	XRNIC_INVALID_QP_STATUS		= 15,
};

enum xrnic_qp_type {
	XRNIC_QPT_RC,
	XRNIC_QPT_UC,
	XRNIC_QPT_UD,
};

enum xrnic_rdma_cm_event_type {
	XRNIC_LISTEN = 1,
	XRNIC_REQ_RCVD,
	XRNIC_MRA_SENT,
	XRNIC_REJ_SENT,
	XRNIC_REJ_RECV,
	XRNIC_REP_SENT,
	XRNIC_MRA_RCVD,
	XRNIC_ESTABLISHD,
	XRNIC_DREQ_RCVD,
	XRNIC_DREQ_SENT,
	XRNIC_RTU_TIMEOUT,
	XRNIC_TIMEWAIT,
	XRNIC_DREP_TIMEOUT,
	XRNIC_REP_RCVD,
	XRNIC_CM_EVENT_ADDR_ERROR,
	XRNIC_CM_EVENT_ADDR_RESOLVED,
	XRNIC_CM_EVENT_ROUTE_RESOLVED,
};

struct xrnic_hw_handshake_info {
	u32				rq_wrptr_db_add;
	u32				sq_cmpl_db_add;
	u32				cnct_io_conf_l_16b;
};

struct xrnic_qp_info {
	void	(*xrnic_rq_event_handler)(u32 rq_count, void *rp_context);
	void	*rq_context;
	void	(*xrnic_sq_event_handler)(u32 cq_head, void *sp_context);
	void	*sq_context;
	void	*rq_buf_ba_ca;
	u64	rq_buf_ba_ca_phys;
	void	*sq_ba;
	u64	sq_ba_phys;
	void	*cq_ba;
	u64	cq_ba_phys;
	u32	sq_depth;
	u32	rq_depth;
	u32	send_sge_size;
	u32	send_pkt_size;
	u32	recv_pkt_size;
	u32	qp_num;
	u32	starting_psn;
	struct	ernic_pd *pd;
};

struct xrnic_qp_init_attr {
	void	(*xrnic_rq_event_handler)(u32 rq_count, void *rp_context);
	void	*rq_context;
	void	(*xrnic_sq_event_handler)(u32 cq_head, void *sp_context);
	void	*sq_context;
	enum xrnic_qp_type	qp_type;
	void	*rq_buf_ba_ca;
	u64	rq_buf_ba_ca_phys;
	void	*sq_ba;
	u64	sq_ba_phys;
	void	*cq_ba;
	u64	cq_ba_phys;
	u32	sq_depth;
	u32	rq_depth;
	u32	send_sge_size;
	u32	send_pkt_size;
	u32	recv_pkt_size;
};

struct xrnic_rdma_route {
	u8		src_addr[16];
	u8		dst_addr[16];
	u16		ip_addr_type;
	u8		smac[ETH_ALEN];
	u8		dmac[ETH_ALEN];
	struct sockaddr_storage s_addr;
	struct sockaddr_storage d_addr;
};

enum xrnic_port_qp_status {
	XRNIC_PORT_QP_FREE,
	XRNIC_PORT_QP_IN_USE,
};

struct xrnic_rdma_cm_event_info {
	enum xrnic_rdma_cm_event_type cm_event;
	int	status;
	void	*private_data;
	u32	private_data_len;
};

struct xrnic_rdma_conn_param {
	u8 private_data[XRNIC_CM_PRVATE_DATA_LENGTH];
	u8 private_data_len;
	u8 responder_resources;
	u8 initiator_depth;
	u8 flow_control;
	u8 retry_count;
	u8 rnr_retry_count;
	u32 qp_num;
	u32 srq;
};

enum xrnic_cm_state {
	XRNIC_CM_REQ_SENT = 0,
	XRNIC_CM_REP_RCVD,
	XRNIC_CM_ESTABLISHED,
};

struct xrnic_rdma_cm_id {
	int	(*xrnic_cm_handler)(struct xrnic_rdma_cm_id *cm_id,
				    struct xrnic_rdma_cm_event_info *event);
	void	*cm_context;
	u32	local_cm_id;
	u32	remote_cm_id;
	struct xrnic_qp_info		qp_info;
	struct xrnic_rdma_route		route;
	struct xrnic_rdma_cm_id_info	*cm_id_info;
	enum xrnic_port_space		ps;
	enum xrnic_qp_type		qp_type;
	u16				port_num;
	u16				child_qp_num;
	struct xrnic_rdma_conn_param	conn_param;
	enum xrnic_port_qp_status	qp_status;
	int				cm_state;
	struct list_head		list;
};

struct xrnic_rdma_cm_id_info {
	struct xrnic_rdma_cm_id			parent_cm_id;
	struct xrnic_rdma_cm_id			*child_cm_id;
	u32					num_child;
	struct xrnic_rdma_cm_event_info		conn_event_info;
};

void xrnic_rq_event_handler (u32 rq_count, void *user_arg);
void xrnic_sq_event_handler (u32 cq_head, void *user_arg);
int xrnic_cm_handler (struct xrnic_rdma_cm_id *cm_id,
		      struct xrnic_rdma_cm_event_info *conn_event_info);

struct xrnic_rdma_cm_id *xrnic_rdma_create_id
	(int (*xrnic_cm_handler)(struct xrnic_rdma_cm_id *cm_id,
	 struct xrnic_rdma_cm_event_info *conn_event_info), void *cm_context,
	 enum xrnic_port_space ps, enum xrnic_qp_type qp_type,
	 int num_child_qp);

int xrnic_rdma_bind_addr(struct xrnic_rdma_cm_id *cm_id,
			 u8 *addr, u16 port_num, u16 ip_addr_type);

int xrnic_rdma_listen(struct xrnic_rdma_cm_id *cm_id, int outstanding);
int xrnic_rdma_create_qp(struct xrnic_rdma_cm_id *cm_id, struct ernic_pd *pd,
			 struct xrnic_qp_init_attr *init_attr);
int xrnic_rdma_accept(struct xrnic_rdma_cm_id *cm_id,
		      struct xrnic_rdma_conn_param *conn_param);
int xrnic_post_recv(struct xrnic_qp_info *qp_info, u32 rq_count);
int xrnic_post_send(struct xrnic_qp_info *qp_info, u32 sq_count);
int xrnic_destroy_qp(struct xrnic_qp_info *qp_info);
int xrnic_rdma_disconnect(struct xrnic_rdma_cm_id *cm_id);
int xrnic_rdma_destroy_id(struct xrnic_rdma_cm_id *cm_id, int flag);
int xrnic_hw_hs_reset_sq_cq(struct xrnic_qp_info *qp_info,
			    struct xrnic_hw_handshake_info *hw_hs_info);
int xrnic_hw_hs_reset_rq(struct xrnic_qp_info *qp_info);

int xrnic_rdma_resolve_addr(struct xrnic_rdma_cm_id *cm_id,
			    struct sockaddr *src_addr,
			    struct sockaddr *dst_addr, int timeout);
int xrnic_rdma_connect(struct xrnic_rdma_cm_id *cm_id,
		       struct xrnic_rdma_conn_param *conn_param);
#ifdef __cplusplus
	}
#endif

#endif /* _XRNIC_IF_H*/
