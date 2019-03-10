/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef QP_H
#define QP_H

#ifdef __cplusplus
	extern "C" {
#endif

#include <linux/interrupt.h>
enum qp_type {
	XRNIC_NOT_ALLOCATED	= 1,
	XRNIC_DISC_CTRL_QP	= 2,
	XRNIC_NVMEOF_CTRL_QP	= 3,
	XRNIC_NVMEOF_IO_QP	= 4,
};

enum ernic_qp_status {
	XRNIC_QP_FREE,
	XRNIC_QP_IN_USE,
};

struct xrnic_qp_attr {
	struct xrnic_memory_map *xrnic_mmap;
	struct xrnic_qp_attr	*qp1_attr;
	struct xrnic_rdma_cm_id	*cm_id;
	void			*send_sgl;
	u64			send_sgl_phys;
	void			*rq_buf_ba_ca;
	u64			rq_buf_ba_ca_phys;
	void			*sq_ba;
	u64			sq_ba_phys;
	void			*cq_ba;
	u64			cq_ba_phys;
	u32			sq_depth;
	u32			rq_depth;
	u32			send_sge_size;
	u32			send_pkt_size;
	u32			recv_pkt_size;
	u32			qp_num;
	u32			local_cm_id;
	u32			remote_cm_id;
	u32			remote_qpn;
	u32			qp_status;
	u32			starting_psn;
	u32			rem_starting_psn;
	u8			send_sgl_temp[XRNIC_QP1_SEND_PKT_SIZE];
	u32			resend_count;
	u32			rq_wrptr_db_local;
	u32			sq_cmpl_db_local;
	u32			rq_ci_db_local;
	u32			sq_pi_db_local;
	u16			ip_addr_type;	/* DESTINATION ADDR_FAMILY */
	u32			ipv4_addr;	/* DESTINATION IP addr */
	u8			ipv6_addr[16];
	u8			mac_addr[6];
	u32			source_qp_num;
	/* remote qpn used in Active CM. source_qp_num is the source
	 * queue pair in deth
	 */
	u32			remote_qp;
	enum xrnic_rdma_cm_event_type curr_state;
	/* DISC or NVMECTRL Its direct mapping to host ID to
	 * particular host_no.
	 */
	enum xrnic_qp_type	qp_type;
	u16			sqhd;
	/*Its direct mapping to host ID to access particular host_no.*/
	u16			nvmeof_cntlid;
	u32			nvmeof_qp_id;
	struct timer_list qp_timer;
	struct tasklet_struct qp_task;
	/* kernel locking primitive */
	spinlock_t qp_lock;
	char irq_name[32];
	u32 irq_vect;
	u32 pd;
};

enum xrnic_hw_qp_status {
	XRNIC_HW_QP_ENABLE,
	XRNIC_HW_QP_DISABLE,
};

void xrnic_display_qp_reg(int qp_num);
void xrnic_qp_fatal_handler(unsigned long data);
void xrnic_qp_timer(struct timer_list *data);
void xrnic_qp_pkt_recv_intr_handler(unsigned long data);
void xrnic_qp_task_handler(unsigned long data);
void xrnic_wqe_completed_intr_handler(unsigned long data);

/* QP Specific function templates */
int xrnic_qp_recv_pkt(struct xrnic_qp_attr *qp_attr, u32 rq_pkt_count);
int xrnic_qp_send_pkt(struct xrnic_qp_attr *qp_attr, u32 sq_pkt_count);
void xrnic_reset_io_qp_rq_ptr(struct xrnic_qp_attr *qp_attr);
void xrnic_reset_io_qp_sq_cq_ptr(struct xrnic_qp_attr *qp_attr,
				 struct xrnic_hw_handshake_info *hw_hs_info);
void xrnic_qp_hw_configuration(int qp_num);
int xrnic_qp1_hw_configuration(void);
void xrnic_qp_app_configuration(int qp_num,
				enum xrnic_hw_qp_status hw_qp_status);
int xrnic_find_free_qp(void);
int xrnic_set_qp_state(int qp_num, int state);

#ifdef __cplusplus
	}
#endif
#endif
