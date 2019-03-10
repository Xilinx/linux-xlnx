// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#include "xcommon.h"

#define DISPLAY_REGS_ON_DISCONNECT
#define	EXPERIMENTAL_CODE

struct xrnic_conn_param {
	const void *private_data;
	u8 private_data_len;
	u8 responder_resources;
	u8 initiator_depth;
	u8 flow_control;
	u8 retry_count;
	u8 rnr_retry_count;
	u8 srq;
	u8 qp_num;
};

/* EXTRA Bytes for Invariant CRC */
#define	ERNIC_INV_CRC	4
/* ERNIC Doesn't have Variant CRC for P2P */
#define	ERNIC_VAR_CRC	0
#define	EXTRA_PKT_LEN	(ERNIC_INV_CRC + ERNIC_VAR_CRC)

#define	cpu_to_be24(x)	((x) << 16)

#define	CMA_VERSION		0
#define QP_STAT_SQ_EMPTY_BIT_POS (9)
#define QP_STAT_OUTSTANDG_EMPTY_Q_BIT_POS (10)

int in_err_wr_ptr;
struct list_head cm_id_list;

/**
 * xrnic_set_qp_state() - Sets the qp state to the desired state
 * @qp_num: XRNIC QP number
 * @state: State to set
 *
 * @return: XRNIC_SUCCESS in case of success or a error representative value
 */
int xrnic_set_qp_state(int qp_num, int state)
{
	if (qp_num < 0)
		return -XRNIC_INVALID_QP_ID;

	if (state != XRNIC_QP_IN_USE && state != XRNIC_QP_FREE)
		return -XRNIC_INVALID_QP_STATUS;

	xrnic_dev->qp_attr[qp_num].qp_status = state;
	return XRNIC_SUCCESS;
}

/**
 * xrnic_find_free_qp() - Finds the free qp to use
 * @return: free QP Num or error value incase of no free QP
 */
int xrnic_find_free_qp(void)
{
	int i;

	for (i = 0 ; i < XRNIC_MAX_QP_SUPPORT ; i++) {
		/*Checking for QP with ZERO REMOTE and LOCAL cm id*/
		if (xrnic_dev->qp_attr[i].qp_status == XRNIC_QP_FREE)
			return i;
	}
	return XRNIC_FAILED;
}

/**
 * xrnic_rdma_create_qp() - Finds the free qp to use
 * @cm_id: CM ID to associate with QP
 * @pd: Protection domain to assosciate the QP with
 * @init_attr: QP attributes or config values
 * @return: XRNIC_SUCCESS if successful otherwise error representing code
 */
int xrnic_rdma_create_qp(struct xrnic_rdma_cm_id *cm_id, struct ernic_pd *pd,
			 struct xrnic_qp_init_attr *init_attr)
{
	struct xrnic_qp_attr *qp_attr;
	struct xrnic_qp_info *qp_info;
	int ret;

	if (init_attr->sq_depth > XRNIC_MAX_SQ_DEPTH		||
	    init_attr->rq_depth > XRNIC_MAX_RQ_DEPTH		||
	    init_attr->send_sge_size > XRNIC_MAX_SEND_SGL_SIZE	||
	    init_attr->send_pkt_size > XRNIC_MAX_SEND_PKT_SIZE) {
		return -XRNIC_INVALID_QP_INIT_ATTR;
	}

	qp_info = &cm_id->qp_info;

	qp_info->qp_num = xrnic_find_free_qp();
	qp_info->qp_num += 2;

	ret = xrnic_set_qp_state((qp_info->qp_num - 2), XRNIC_QP_IN_USE);
	if (ret < 0)
		return ret;

	qp_attr = &xrnic_dev->qp_attr[qp_info->qp_num - 2];

	if (qp_info->qp_num < 2 || qp_attr->qp_type != init_attr->qp_type)
		return -XRNIC_INVALID_QP_ID;

	cm_id->qp_type = init_attr->qp_type;
	cm_id->local_cm_id = (qp_info->qp_num);

	qp_info->xrnic_rq_event_handler = init_attr->xrnic_rq_event_handler;
	qp_info->rq_context = init_attr->rq_context;
	qp_info->xrnic_sq_event_handler = init_attr->xrnic_sq_event_handler;
	qp_info->sq_context = init_attr->sq_context;

	qp_info->rq_buf_ba_ca = init_attr->rq_buf_ba_ca;
	qp_info->rq_buf_ba_ca_phys = init_attr->rq_buf_ba_ca_phys;
	qp_info->sq_ba = init_attr->sq_ba;
	qp_info->sq_ba_phys = init_attr->sq_ba_phys;
	qp_info->cq_ba = init_attr->cq_ba;
	qp_info->cq_ba_phys = init_attr->cq_ba_phys;

	qp_info->sq_depth = init_attr->sq_depth;
	qp_info->rq_depth = init_attr->rq_depth;
	qp_info->send_sge_size = init_attr->send_sge_size;
	qp_info->send_pkt_size = init_attr->send_pkt_size;
	qp_info->recv_pkt_size = init_attr->recv_pkt_size;

	qp_attr->rq_buf_ba_ca = qp_info->rq_buf_ba_ca;
	qp_attr->rq_buf_ba_ca_phys = qp_info->rq_buf_ba_ca_phys;
	qp_attr->sq_ba = qp_info->sq_ba;
	qp_attr->sq_ba_phys = qp_info->sq_ba_phys;
	qp_attr->cq_ba = qp_info->cq_ba;
	qp_attr->cq_ba_phys = qp_info->cq_ba_phys;

	qp_attr->sq_depth = qp_info->sq_depth;
	qp_attr->rq_depth = qp_info->rq_depth;
	qp_attr->send_sge_size = qp_info->send_sge_size;
	qp_attr->send_pkt_size = qp_info->send_pkt_size;
	qp_attr->recv_pkt_size = qp_info->recv_pkt_size;
#ifdef ERNIC_MEM_REGISTER
	if (pd)
		qp_attr->pd = atomic_read(&pd->id);
#endif
	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_create_qp);

/**
 * xrnic_post_recv() - This function receives an incoming packet
 * @qp_info: QP info on which packet should be received
 * @rq_count: Number of packets to receive
 * @return: SUCCESS if received required number of packets else error
 * representative value
 */
int xrnic_post_recv(struct xrnic_qp_info *qp_info, u32 rq_count)
{
	struct xrnic_qp_attr *qp_attr;
	int ret = -XRNIC_INVALID_QP_ID;

	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	qp_attr = &xrnic_dev->qp_attr[qp_info->qp_num - 2];
	if (qp_attr->remote_cm_id)
		ret = xrnic_qp_recv_pkt(qp_attr, rq_count);

	return ret;
}
EXPORT_SYMBOL(xrnic_post_recv);

/**
 * xrnic_post_send() - This function post a SEND WR
 * @qp_info: QP info to post the request
 * @sq_count: SEND packet count
 * @return: SUCCESS if successfully posts a SEND,
 * otherwise error representative value
 */
int xrnic_post_send(struct xrnic_qp_info *qp_info, u32 sq_count)
{
	struct xrnic_qp_attr *qp_attr;
	int ret = -XRNIC_INVALID_QP_ID;

	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	qp_attr = &xrnic_dev->qp_attr[qp_info->qp_num - 2];
	if (qp_attr->remote_cm_id)
		ret = xrnic_qp_send_pkt(qp_attr, sq_count);

	return ret;
}
EXPORT_SYMBOL(xrnic_post_send);

/**
 * xrnic_destroy_qp() - Function destroys QP and reset the QP info
 * @qp_info: QP info or config
 * @return: XRNIC_SUCCESS if successfully destroys the QP,
 * otherwise error representative value
 */
int xrnic_destroy_qp(struct xrnic_qp_info *qp_info)
{
	u32 qp_num;
	struct xrnic_qp_attr *qp_attr;

	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	if (qp_info->qp_num >= 2) {
		qp_num = qp_info->qp_num;
		qp_attr = &xrnic_dev->qp_attr[qp_num - 2];
		xrnic_set_qp_state((qp_num - 2), XRNIC_QP_FREE);

		memset((void *)qp_info, 0, sizeof(struct xrnic_qp_info));

		qp_attr->rq_buf_ba_ca = qp_info->rq_buf_ba_ca;
		qp_attr->rq_buf_ba_ca_phys = qp_info->rq_buf_ba_ca_phys;
		qp_attr->sq_ba = qp_info->sq_ba;
		qp_attr->sq_ba_phys = qp_info->sq_ba_phys;
		qp_attr->cq_ba = qp_info->cq_ba;
		qp_attr->cq_ba_phys = qp_info->cq_ba_phys;

		qp_attr->sq_depth = qp_info->sq_depth;
		qp_attr->rq_depth = qp_info->rq_depth;
		qp_attr->send_sge_size = qp_info->send_sge_size;
		qp_attr->send_pkt_size = qp_info->send_pkt_size;
		qp_attr->recv_pkt_size = qp_info->recv_pkt_size;
		qp_attr->cm_id = NULL;
	} else {
		pr_err("Received invalid QP ID\n");
		return -XRNIC_INVALID_QP_ID;
	}

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_destroy_qp);

/**
 * xrnic_reset_io_qp() - This function reset the QP config
 * @qp_attr: QP memory map or config
 */
void xrnic_reset_io_qp(struct xrnic_qp_attr *qp_attr)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct xrnic_ctrl_config *xrnic_ctrl_config;
	struct xrnic_reg_map *reg_map;
	unsigned long timeout;
	u32 sq_pi_db_val, cq_head_val;
	u32 rq_ci_db_val, stat_rq_pi_db_val;
	u32 config_value;
	int qp_num = qp_attr->qp_num - 2;
	struct rdma_qp_attr *rdma_qp_attr;

	xrnic_mmap = (struct xrnic_memory_map *)qp_attr->xrnic_mmap;
	reg_map = xrnic_dev->xrnic_mmap.xrnic_regs;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];
	xrnic_ctrl_config = &reg_map->xrnic_ctrl_config;

	/* 1. WAIT FOR SQ/OSQ EMPTY TO BE SET */
	while (!((ioread32(&rdma_qp_attr->qp_status) >> 9) & 0x3))
		;

	/* 2 WAIT FOR register values SQ_PI_DB == CQ_HEAD */
	sq_pi_db_val = ioread32(((void *)(&rdma_qp_attr->sq_pi_db)));

	cq_head_val = ioread32(((void *)(&rdma_qp_attr->cq_head)));

	timeout = jiffies;
	while (!(sq_pi_db_val == cq_head_val)) {
		sq_pi_db_val = ioread32(((void *)(&rdma_qp_attr->sq_pi_db)));
		cq_head_val = ioread32(((void *)(&rdma_qp_attr->cq_head)));
		if (time_after(jiffies, (timeout + 1 * HZ)))
			break;
	}

	/* 3. WAIT FOR register values STAT_RQ_PI_DB == RQ_CI_DB */
	rq_ci_db_val = ioread32(((void *)(&rdma_qp_attr->rq_ci_db)));

	stat_rq_pi_db_val = ioread32(((void *)(&rdma_qp_attr->stat_rq_pi_db)));

	timeout = jiffies;
	while (!(rq_ci_db_val == stat_rq_pi_db_val)) {
		rq_ci_db_val = ioread32(((void *)(&rdma_qp_attr->rq_ci_db)));
		stat_rq_pi_db_val = ioread32(((void *)
					(&rdma_qp_attr->stat_rq_pi_db)));
		if (time_after(jiffies, (timeout + 1 * HZ)))
			break;
	}
	/* 4. SET QP_CONF register HW handshake disable to 1 */
	config_value = ioread32(((void *)(&rdma_qp_attr->qp_conf)));
	config_value = config_value | XRNIC_QP_CONFIG_HW_HNDSHK_DIS |
		XRNIC_QP_CONFIG_RQ_INTR_EN | XRNIC_QP_CONFIG_CQE_INTR_EN;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));
	DEBUG_LOG("QP config value is 0x%x\n", config_value);

	config_value = ioread32((char *)xrnic_mmap->rq_wrptr_db_add +
				(4 * (qp_attr->qp_num - 1)));
	config_value = (xrnic_mmap->rq_wrptr_db_add_phys +
			(4 * (qp_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_wrptr_db_add)));

	config_value = (xrnic_mmap->sq_cmpl_db_add_phys +
			(4 * (qp_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_cmpl_db_add)));

	/* 5. SET QP_CONF register QP ENABLE TO 0 and QP_ADV_CONF register
	 * SW OVERRIDE TO 1
	 */
	config_value = ioread32(((void *)(&rdma_qp_attr->qp_conf)));
	config_value = config_value & ~XRNIC_QP_CONFIG_QP_ENABLE;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));
	/* Enable SW override enable */
	config_value = 0x1;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	/* 6.	Initialized QP under reset: */
	config_value = 0x0;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->stat_rq_pi_db)));

	config_value = qp_attr->rq_buf_ba_ca_phys & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_buf_ba_ca)));

	config_value = 0x0;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_ci_db)));

	iowrite32(config_value,
		  ((void *)(&rdma_qp_attr->stat_curr_sqptr_pro)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_pi_db)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->cq_head)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_psn)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->last_rq_req)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->stat_msn)));

	/* 7.Initialized Ethernet side registers */
	/* NO need as we are doing during connect initiatlization */

	/* 8. Set QP_CONF register QP ENABLE TO 1 */

	config_value = ioread32(((void *)(&rdma_qp_attr->qp_conf)));
	config_value = config_value | XRNIC_QP_CONFIG_QP_ENABLE;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));

	config_value = ioread32((void *)&rdma_qp_attr->qp_conf);
	config_value = config_value & ~XRNIC_QP_CONFIG_UNDER_RECOVERY;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));

	/* 9.Set QP_ADV_CONF register SW_OVERRIDE SET TO 0 */
	/* Disable SW override enable */
	config_value = 0;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	qp_attr->rq_wrptr_db_local = 0;
	qp_attr->sq_cmpl_db_local = 0;
	qp_attr->rq_ci_db_local = 0;
	qp_attr->sq_pi_db_local = 0;
	qp_attr->sqhd = 0;
}

/**
 * xrnic_reset_io_qp_sq_cq_ptr() - This function resets SQ, CQ pointers of QP
 * @qp_attr: QP config
 * @hw_hs_info: QP HW handshake config
 */
void xrnic_reset_io_qp_sq_cq_ptr(struct xrnic_qp_attr *qp_attr,
				 struct xrnic_hw_handshake_info *hw_hs_info)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct xrnic_reg_map *reg_map;
	struct xrnic_ctrl_config *xrnic_ctrl_config;
	struct rdma_qp_attr *rdma_qp_attr;
	u32 config_value = 0;
	int qp_num = qp_attr->qp_num - 2;

	xrnic_mmap = qp_attr->xrnic_mmap;
	reg_map = xrnic_dev->xrnic_mmap.xrnic_regs;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];
	xrnic_ctrl_config = &reg_map->xrnic_ctrl_config;

	/* Enable SW override enable */
	config_value = 0x1;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	if (!hw_hs_info)
		goto enable_hw_hs;

	config_value = 0;

	iowrite32(config_value, ((void *)(&rdma_qp_attr->cq_head)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_pi_db)));

	iowrite32(config_value,
		  ((void *)(&rdma_qp_attr->stat_curr_sqptr_pro)));

	config_value = hw_hs_info->rq_wrptr_db_add;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_wrptr_db_add)));

	config_value = hw_hs_info->sq_cmpl_db_add;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_cmpl_db_add)));

	config_value = ioread32((void *)(&rdma_qp_attr->stat_rq_pi_db));

	config_value = hw_hs_info->cnct_io_conf_l_16b |
			((config_value & 0xFFFF) << 16);
	iowrite32(config_value,	((void *)(&xrnic_ctrl_config->cnct_io_conf)));
enable_hw_hs:
	config_value = XRNIC_QP_CONFIG_QP_ENABLE |
			xrnic_dev->pmtu |
			XRNIC_QP_CONFIG_RQ_BUFF_SZ(qp_attr->recv_pkt_size);

	if (qp_attr->ip_addr_type == AF_INET6)
		config_value = config_value | XRNIC_QP_CONFIG_IPV6_EN;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));

	/* Disable SW override enable */

	config_value = 0;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	config_value = ioread32(((void *)(&rdma_qp_attr->cq_head)));

	config_value = ioread32(((void *)(&rdma_qp_attr->sq_pi_db)));

	config_value = ioread32(((void *)
				(&rdma_qp_attr->stat_curr_sqptr_pro)));

	qp_attr->rq_wrptr_db_local = 0;
	qp_attr->sq_cmpl_db_local = 0;
	qp_attr->rq_ci_db_local = 0;
	qp_attr->sq_pi_db_local = 0;
	qp_attr->sqhd = 0;
}

/**
 * xrnic_reset_io_qp_rq_ptr() - This function resets RQ pointers of QP
 * @qp_attr: QP config
 */
void xrnic_reset_io_qp_rq_ptr(struct xrnic_qp_attr *qp_attr)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct xrnic_reg_map *reg_map;
	struct xrnic_ctrl_config *xrnic_ctrl_config;
	struct rdma_qp_attr *rdma_qp_attr;
	u32 config_value = 0;
	int qp_num = qp_attr->qp_num - 2;

	xrnic_mmap = (struct xrnic_memory_map *)qp_attr->xrnic_mmap;
	reg_map = xrnic_dev->xrnic_mmap.xrnic_regs;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];
	xrnic_ctrl_config = &reg_map->xrnic_ctrl_config;

	config_value = 0x1;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	config_value = 0x0;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_ci_db)));

	iowrite32(config_value, ((void *)(&rdma_qp_attr->stat_rq_pi_db)));

	config_value = qp_attr->rq_buf_ba_ca_phys & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_buf_ba_ca)));

	config_value =	XRNIC_QP_CONFIG_QP_ENABLE |
			XRNIC_QP_CONFIG_CQE_INTR_EN | xrnic_dev->pmtu |
			XRNIC_QP_CONFIG_RQ_BUFF_SZ(qp_attr->recv_pkt_size) |
			XRNIC_QP_CONFIG_HW_HNDSHK_DIS |
			XRNIC_QP_CONFIG_CQE_WRITE_EN;
	if (qp_attr->ip_addr_type == AF_INET6)
		config_value = config_value | XRNIC_QP_CONFIG_IPV6_EN;

	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));
	/* Disable SW override enable */
	config_value = 0x0;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_config->xrnic_adv_conf)));

	config_value = ioread32(((void *)(&rdma_qp_attr->rq_ci_db)));

	config_value = ioread32(((void *)(&rdma_qp_attr->stat_rq_buf_ca)));

	config_value = ioread32(((void *)(&rdma_qp_attr->stat_rq_pi_db)));
}

/**
 * xrnic_qp_send_pkt() - This function sends packets
 * @qp_attr: QP config
 * @sq_pkt_count: Number of packets to send
 * @return: XRNIC_SUCCESS if successful
 *		otherwise error representative value
 */
int xrnic_qp_send_pkt(struct xrnic_qp_attr *qp_attr, u32 sq_pkt_count)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct rdma_qp_attr *rdma_qp_attr;
	u32 config_value = 0, sq_pkt_count_tmp;
	int qp_num = qp_attr->qp_num - 2;

	xrnic_mmap = (struct xrnic_memory_map *)qp_attr->xrnic_mmap;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];

	config_value = ioread32((char *)xrnic_mmap->sq_cmpl_db_add +
				(4 * (qp_attr->qp_num - 1)));
	if (config_value == 0)
		sq_pkt_count_tmp = qp_attr->sq_depth;
	else if (qp_attr->sq_cmpl_db_local >= config_value)
		sq_pkt_count_tmp = (config_value + qp_attr->sq_depth) -
				qp_attr->sq_cmpl_db_local;
	else
		sq_pkt_count_tmp = config_value - qp_attr->sq_cmpl_db_local;
	if (sq_pkt_count_tmp < sq_pkt_count)
		return -XRNIC_INVALID_PKT_CNT;

	/* We need to maintain sq_cmpl_db_local as per hardware
	 * update for Queue spesific sq_cmpl_db_local register.
	 * Also in case of resend some packect
	 * we need to maintain this variable.
	 */

	qp_attr->sq_cmpl_db_local = qp_attr->sq_cmpl_db_local + sq_pkt_count;
	if (qp_attr->sq_cmpl_db_local > qp_attr->sq_depth)
		qp_attr->sq_cmpl_db_local = qp_attr->sq_cmpl_db_local
							 - qp_attr->sq_depth;
	config_value = qp_attr->sq_cmpl_db_local;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_pi_db)));

	return XRNIC_SUCCESS;
}

/**
 * xrnic_qp_recv_pkt() - This function receives packets
 * @qp_attr: QP config
 * @rq_pkt_count: receive packet count
 * @return: XRNIC_SUCCESS if successful
 *		otherwise error representative value
 */
int xrnic_qp_recv_pkt(struct xrnic_qp_attr *qp_attr, u32 rq_pkt_count)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct rdma_qp_attr *rdma_qp_attr;
	u32 config_value = 0, rq_pkt_count_tmp;
	int qp_num = qp_attr->qp_num - 2;

	xrnic_mmap = (struct xrnic_memory_map *)qp_attr->xrnic_mmap;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];

	config_value = ioread32((char *)xrnic_mmap->rq_wrptr_db_add +
				(4 * (qp_attr->qp_num - 1)));
	if (config_value == 0)
		rq_pkt_count_tmp = qp_attr->rq_depth;
	else if (qp_attr->rq_wrptr_db_local >= config_value)
		rq_pkt_count_tmp = (config_value + qp_attr->rq_depth) -
				qp_attr->rq_wrptr_db_local;
	else
		rq_pkt_count_tmp = config_value - qp_attr->rq_wrptr_db_local;

	if (rq_pkt_count_tmp < rq_pkt_count)
		return -XRNIC_INVALID_PKT_CNT;
	/* We need to maintain sq_cmpl_db_local as per hardware
	 * update for Queue spesific sq_cmpl_db_local register.
	 * Also in case of resend some packect
	 * we need to maintain this variable.
	 */

	qp_attr->rq_wrptr_db_local = qp_attr->rq_wrptr_db_local + rq_pkt_count;
	if (qp_attr->rq_wrptr_db_local > qp_attr->rq_depth)
		qp_attr->rq_wrptr_db_local = qp_attr->rq_wrptr_db_local
							 - qp_attr->rq_depth;

	config_value = qp_attr->rq_wrptr_db_local;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_ci_db)));

	return XRNIC_SUCCESS;
}

/**
 * xrnic_qp1_send_mad_pkt() - This function initiates sending a management
 *				datagram packet.
 * @send_sgl_temp: Scatter gather list
 * @qp1_attr: QP1 info
 * @send_pkt_size: Send packe size
 */
void xrnic_qp1_send_mad_pkt(void *send_sgl_temp,
			    struct xrnic_qp_attr *qp1_attr, u32 send_pkt_size)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct rdma_qp1_attr *rdma_qp1_attr;
	u32 config_value = 0;
	struct wr *sq_wr; /*sq_ba*/

	xrnic_mmap = (struct xrnic_memory_map *)qp1_attr->xrnic_mmap;
	rdma_qp1_attr = &xrnic_mmap->xrnic_regs->rdma_qp1_attr;

	/* We need to maintain sq_cmpl_db_local as per hardware
	 * update for Queue spesific sq_cmpl_db_local register.
	 * Also in case of resend some packect
	 * we need to maintain this variable.
	 */
	sq_wr = (struct wr *)qp1_attr->sq_ba + qp1_attr->sq_cmpl_db_local;
	/* All will be 4096 that is madatory.*/
	sq_wr->length = send_pkt_size;
	memcpy((void *)((char *)qp1_attr->send_sgl +
	       (qp1_attr->sq_cmpl_db_local * XRNIC_SEND_SGL_SIZE)),
	       (const void *)send_sgl_temp,
	       XRNIC_SEND_SGL_SIZE);
	qp1_attr->sq_cmpl_db_local = qp1_attr->sq_cmpl_db_local + 1;

	config_value = qp1_attr->sq_cmpl_db_local;
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->sq_pi_db)));

	if (qp1_attr->sq_cmpl_db_local == XRNIC_SQ_DEPTH)
		qp1_attr->sq_cmpl_db_local = 0;
}

/**
 * xrnic_qp_pkt_recv() - This function process received data packets
 * @qp_attr: QP info on which data packet has been received
 */
static void xrnic_qp_pkt_recv(struct xrnic_qp_attr *qp_attr)
{
	struct xrnic_memory_map *xrnic_mmap = (struct xrnic_memory_map *)
						qp_attr->xrnic_mmap;
	u32 config_value = 0;
	unsigned long flag;
	int rq_pkt_count = 0;
	struct xrnic_rdma_cm_id *cm_id = qp_attr->cm_id;

	spin_lock_irqsave(&qp_attr->qp_lock, flag);
	config_value = ioread32((char *)xrnic_mmap->rq_wrptr_db_add +
				(4 * (qp_attr->qp_num - 1)));
	if (qp_attr->rq_wrptr_db_local == config_value) {
		spin_unlock_irqrestore(&qp_attr->qp_lock, flag);
		return;
	}
	if (qp_attr->rq_wrptr_db_local > config_value) {
		rq_pkt_count = (config_value + qp_attr->rq_depth) -
				qp_attr->rq_wrptr_db_local;
	} else {
		rq_pkt_count = config_value - qp_attr->rq_wrptr_db_local;
	}

	cm_id->qp_info.xrnic_rq_event_handler(rq_pkt_count,
					      cm_id->qp_info.rq_context);

	spin_unlock_irqrestore(&qp_attr->qp_lock, flag);
}

/**
 * xrnic_wqe_completed() - This function process completion interrupts
 * @qp_attr: QP info for which completion is received
 */
static void xrnic_wqe_completed(struct xrnic_qp_attr *qp_attr)
{
	struct xrnic_memory_map *xrnic_mmap;
	struct rdma_qp_attr *rdma_qp_attr;
	u32 config_value = 0;
	unsigned long flag;
	struct xrnic_rdma_cm_id *cm_id = qp_attr->cm_id;
	int qp_num = qp_attr->qp_num;

	xrnic_mmap = (struct xrnic_memory_map *)qp_attr->xrnic_mmap;
	rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num - 2];
	/* We need to maintain sq_cmpl_db_local as per hardware update
	 * for Queue spesific sq_cmpl_db_local register.
	 * Also in case of resend some packect we
	 * need to maintain this variable.
	 */
	spin_lock_irqsave(&qp_attr->qp_lock, flag);
	config_value = ioread32((char *)&rdma_qp_attr->cq_head);
	cm_id->qp_info.xrnic_sq_event_handler(config_value,
					      cm_id->qp_info.sq_context);
	spin_unlock_irqrestore(&qp_attr->qp_lock, flag);
}

/**
 * xrnic_wqe_completed_intr_handler() - Interrupt handler for completion
 *					interrupt type
 * @data: XRNIC device info
 */
void xrnic_wqe_completed_intr_handler(unsigned long data)
{
	struct xrnic_dev_info *xrnic_dev = (struct xrnic_dev_info *)data;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct xrnic_qp_attr *qp_attr;
	struct xrnic_ctrl_config *xrnic_ctrl_config =
		&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	unsigned long cq_intr = 0, qp_num, i, j;
	unsigned long flag;

	for (i = 0 ; i < XRNIC_RQ_CQ_INTR_STS_REG_SUPPORTED ; i++) {
		cq_intr = ioread32((void __iomem *)
				    ((&xrnic_ctrl_config->cq_intr_sts_1) +
				    (i * 4)));

		if (!cq_intr)
			continue;

		for (j = find_first_bit(&cq_intr, XRNIC_REG_WIDTH);
		     j < XRNIC_REG_WIDTH;
		     j = find_next_bit(&cq_intr, XRNIC_REG_WIDTH, j + 1)) {
			qp_num = (i << 5) + j;
			iowrite32((1 << j), (void __iomem *)
				  ((&xrnic_ctrl_config->cq_intr_sts_1) +
				  (i * 4)));
			qp_attr = &xrnic_dev->qp_attr[qp_num - 2];
			if (qp_attr->cm_id)
				xrnic_wqe_completed(qp_attr);
			else
				pr_err("Received CM ID is NULL\n");
		}
	}
	spin_lock_irqsave(&qp1_attr->qp_lock, flag);
	xrnic_dev->xrnic_mmap.intr_en = xrnic_dev->xrnic_mmap.intr_en |
					WQE_COMPLETED_INTR_EN;
	iowrite32(xrnic_dev->xrnic_mmap.intr_en,
		  ((void *)(&xrnic_ctrl_config->intr_en)));
	spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
}

/**
 * xrnic_qp_pkt_recv_intr_handler() - Interrupt handler for data
 *					packet interrupt
 * @data: XRNIC device info
 */
void xrnic_qp_pkt_recv_intr_handler(unsigned long data)
{
	struct xrnic_dev_info *xrnic_dev = (struct xrnic_dev_info *)data;
	struct xrnic_memory_map *xrnic_mmap =
		(struct xrnic_memory_map *)&xrnic_dev->xrnic_mmap;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct xrnic_qp_attr *qp_attr;
	struct rdma_qp_attr *rdma_qp_attr;
	struct xrnic_reg_map *regs;
	struct xrnic_ctrl_config *xrnic_ctrl_config =
		&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	unsigned long rq_intr = 0, qp_num, i, j, config_value;
	unsigned long flag;

	for (i = 0 ; i < XRNIC_RQ_CQ_INTR_STS_REG_SUPPORTED ; i++) {
		rq_intr = ioread32((void __iomem *)
			(&xrnic_ctrl_config->rq_intr_sts_1 + (i * 4)));

		if (!rq_intr)
			continue;

		for (j = find_first_bit(&rq_intr, XRNIC_REG_WIDTH);
		j < XRNIC_REG_WIDTH; j = find_next_bit
		(&rq_intr, XRNIC_REG_WIDTH, j + 1)) {
			qp_num = (i << 5) + j;
			/* We need to change this with Work Request as
			 * for other Admin QP required wait events.
			 */
			iowrite32((1 << j), ((void __iomem *)
				  (&xrnic_ctrl_config->rq_intr_sts_1) +
				  (i * 4)));
			qp_attr = &xrnic_dev->qp_attr[qp_num - 2];
			regs = xrnic_mmap->xrnic_regs;
			rdma_qp_attr = &regs->rdma_qp_attr[qp_num - 2];
			config_value = ioread32((void *)
						(&rdma_qp_attr->qp_conf));
			if (qp_attr->cm_id &&
			    (config_value & XRNIC_QP_CONFIG_HW_HNDSHK_DIS)) {
				xrnic_qp_pkt_recv(qp_attr);
			} else {
				if (qp_attr->cm_id)
					pr_err("Received CM ID is NULL\n");
				else
					pr_err("HW handshake is enabled\n");
			}
		}
	}
	spin_lock_irqsave(&qp1_attr->qp_lock, flag);
	xrnic_dev->xrnic_mmap.intr_en = xrnic_dev->xrnic_mmap.intr_en |
					QP_PKT_RCVD_INTR_EN;
	iowrite32(xrnic_dev->xrnic_mmap.intr_en,
		  ((void *)(&xrnic_ctrl_config->intr_en)));
	spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
}

/**
 * xrnic_qp_fatal_handler() - Interrupt handler for QP fatal interrupt type
 * @data: XRNIC device info
 */
void xrnic_qp_fatal_handler(unsigned long data)
{
	struct xrnic_memory_map *xrnic_mmap =
		(struct xrnic_memory_map *)&xrnic_dev->xrnic_mmap;
	struct xrnic_ctrl_config *xrnic_conf =
		&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	struct rdma_qp_attr *rdma_qp_attr;
	int i, err_entries;
	unsigned long timeout;
	unsigned long	config_value, qp_num, qp, sq_pi_db_val, cq_head_val;
	struct xrnic_qp_attr *qp_attr;
	struct xrnic_rdma_cm_id_info *cm_id_info;

	err_entries = ioread32((void *)&xrnic_conf->in_errsts_q_wrptr);
	pr_info("No of QPs in Fatal: %d\r\n", err_entries - in_err_wr_ptr);
	for (i = 0; i < (err_entries - in_err_wr_ptr); i++) {
		qp_num = ioread32((char *)xrnic_mmap->in_errsts_q_ba +
				  ((8 * in_err_wr_ptr) + (8 * i)));
		qp_num = (qp_num & 0xFFFF0000) >> 16;
		qp = qp_num - 2;
		rdma_qp_attr = &xrnic_mmap->xrnic_regs->rdma_qp_attr[qp];
		if (rdma_qp_attr) {
			while (!((ioread32(&rdma_qp_attr->qp_status) >> 9) &
					   0x3))
				DEBUG_LOG("Fatal wait for SQ/OSQ empty\n");

			/* 2 WAIT FOR register values SQ_PI_DB == CQ_HEAD */
			sq_pi_db_val = ioread32(((void *)
					(&rdma_qp_attr->sq_pi_db)));

			cq_head_val = ioread32((void *)&rdma_qp_attr->cq_head);

			timeout = jiffies;
			while (!(sq_pi_db_val == cq_head_val)) {
				sq_pi_db_val = ioread32(((void *)
						(&rdma_qp_attr->sq_pi_db)));
				cq_head_val = ioread32(((void *)
						(&rdma_qp_attr->cq_head)));
				if (time_after(jiffies, (timeout + 1 * HZ))) {
					pr_info("SQ PI != CQ Head\n");
					break;
				}
			}

			/* Poll and wait for register value
			 * RESP_HNDL_STS.sq_pici_db_check_en == ‘1’
			 */
			while (!((ioread32(&xrnic_conf->resp_handler_status)
				 >> 16) & 0x1))
				DEBUG_LOG("waiting for RESP_HNDL_STS\n");

			config_value = ioread32((void *)
						&rdma_qp_attr->qp_conf);
			config_value = config_value &
					(~XRNIC_QP_CONFIG_QP_ENABLE);
			iowrite32(config_value,
				  ((void *)(&rdma_qp_attr->qp_conf)));

			config_value = ioread32((void *)
						&rdma_qp_attr->qp_conf);
			config_value = config_value |
					XRNIC_QP_CONFIG_UNDER_RECOVERY;
			iowrite32(config_value,
				  ((void *)(&rdma_qp_attr->qp_conf)));

			/* Calling CM Handler to disconnect QP.*/
			qp_attr = &xrnic_dev->qp_attr[qp_num - 2];
			if (qp_attr->cm_id) {
				cm_id_info = qp_attr->cm_id->cm_id_info;
				cm_id_info->conn_event_info.cm_event =
					XRNIC_DREQ_RCVD;
				cm_id_info->conn_event_info.status = 1;
				cm_id_info->conn_event_info.private_data_len =
					0;
				cm_id_info->conn_event_info.private_data =
					NULL;
				qp_attr->cm_id->xrnic_cm_handler
						(qp_attr->cm_id,
						&cm_id_info->conn_event_info);
				qp_attr->cm_id = NULL;
			} else {
				pr_err("Received CM ID is NULL\n");
			}
		}
		in_err_wr_ptr++;
	}
}

/**
 * xrnic_qp1_hw_configuration() - This function configures the QP1 registers
 * @return: 0 if successfully configures QP1
 */
int xrnic_qp1_hw_configuration(void)
{
	struct xrnic_memory_map *xrnic_mmap = (struct xrnic_memory_map *)
			&xrnic_dev->xrnic_mmap;
	struct xrnic_qp_attr *qp1_attr = (struct xrnic_qp_attr *)
			&xrnic_dev->qp1_attr;
	struct rdma_qp1_attr *rdma_qp1_attr;
	u32 config_value = 0;

	qp1_attr->qp_num = 1;
	rdma_qp1_attr = &xrnic_dev->xrnic_mmap.xrnic_regs->rdma_qp1_attr;
	config_value =	XRNIC_QP_CONFIG_QP_ENABLE | xrnic_dev->pmtu |
			XRNIC_QP1_CONFIG_RQ_BUFF_SZ |
			XRNIC_QP_CONFIG_RQ_INTR_EN |
			XRNIC_QP_CONFIG_HW_HNDSHK_DIS;

	iowrite32(config_value, ((void *)(&rdma_qp1_attr->qp_conf)));

	config_value = (xrnic_mmap->rq_buf_ba_ca_phys +
			((qp1_attr->qp_num - 1) * XRNIC_RECV_PKT_SIZE *
			XRNIC_RQ_DEPTH)) & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->rq_buf_ba_ca)));

	qp1_attr->rq_buf_ba_ca = xrnic_mmap->rq_buf_ba_ca +
				 ((qp1_attr->qp_num - 1) *
				 XRNIC_RECV_PKT_SIZE *
				  XRNIC_RQ_DEPTH);

	qp1_attr->rq_buf_ba_ca_phys = config_value;

	config_value = xrnic_mmap->sq_ba_phys + ((qp1_attr->qp_num - 1) *
			XRNIC_SEND_PKT_SIZE * XRNIC_SQ_DEPTH);
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->sq_ba)));

	qp1_attr->sq_ba = (struct wr *)((void *)xrnic_mmap->sq_ba +
					((qp1_attr->qp_num - 1) *
					XRNIC_SEND_PKT_SIZE *
					XRNIC_SQ_DEPTH));
	qp1_attr->sq_ba_phys = config_value;

	qp1_attr->send_sgl_phys = xrnic_mmap->send_sgl_phys +
					(XRNIC_SEND_SGL_SIZE *
					 XRNIC_SQ_DEPTH *
					 (qp1_attr->qp_num - 1));
	qp1_attr->send_sgl = xrnic_mmap->send_sgl +
				(XRNIC_SEND_SGL_SIZE *
				 XRNIC_SQ_DEPTH *
				 (qp1_attr->qp_num - 1));

	xrnic_fill_wr(qp1_attr, XRNIC_SQ_DEPTH);

	config_value = xrnic_mmap->cq_ba_phys + ((qp1_attr->qp_num - 1) *
			XRNIC_SQ_DEPTH * sizeof(struct cqe));
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->cq_ba)));

	qp1_attr->cq_ba = (struct cqe *)(xrnic_mmap->cq_ba +
					 ((qp1_attr->qp_num - 1) *
					 XRNIC_SQ_DEPTH *
					 sizeof(struct cqe)));
	config_value = (xrnic_mmap->rq_wrptr_db_add_phys +
			(4 * (qp1_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value,
		  ((void *)(&rdma_qp1_attr->rq_wrptr_db_add)));

	config_value = (xrnic_mmap->sq_cmpl_db_add_phys +
			(4 * (qp1_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value,
		  ((void *)(&rdma_qp1_attr->sq_cmpl_db_add)));

	config_value = XRNIC_SQ_DEPTH | (XRNIC_RQ_DEPTH << 16);
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->q_depth)));

	config_value = (xrnic_mmap->stat_rq_buf_ca_phys +
			(4 * (qp1_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value,
		  ((void *)(&rdma_qp1_attr->stat_rq_buf_ca)));

	config_value =	XRNIC_QP_TIMEOUT_CONFIG_TIMEOUT |
			XRNIC_QP_TIMEOUT_CONFIG_RETRY_CNT |
			XRNIC_QP_TIMEOUT_CONFIG_RNR_RETRY_CNT |
			XRNIC_QP_TIMEOUT_CONFIG_RNR_NAK_TVAL;
	iowrite32(config_value, ((void *)(&rdma_qp1_attr->timeout_conf)));
	qp1_attr->qp1_attr = (struct xrnic_qp_attr *)&xrnic_dev->qp1_attr;
	qp1_attr->rq_wrptr_db_local = 0;
	qp1_attr->sq_cmpl_db_local = 0;
	qp1_attr->rq_ci_db_local = 0;
	qp1_attr->sq_pi_db_local = 0;

	qp1_attr->resend_count = 0;
	qp1_attr->local_cm_id = htonl(qp1_attr->qp_num);
	qp1_attr->remote_cm_id = 0;

	qp1_attr->curr_state = XRNIC_LISTEN;

	qp1_attr->sqhd = 0;
	qp1_attr->qp_type = XRNIC_QPT_UC;
	qp1_attr->ip_addr_type = 0;

	qp1_attr->xrnic_mmap = &xrnic_dev->xrnic_mmap;

	spin_lock_init(&qp1_attr->qp_lock);
	return 0;
}

/**
 * xrnic_display_qp_reg() - This function displays qp register info
 * @qp_num: QP num for which register dump is required
 */
void xrnic_display_qp_reg(int qp_num)
{
	int i;
	struct xrnic_memory_map *xrnic_mmap = &xrnic_dev->xrnic_mmap;
	struct rdma_qp_attr *rdma_qp_attr =
			&xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num - 2];

	for (i = 0; i < 45; i++)
		pr_info("0x%X: 0x%08X\n",
			(0x84020000 + (0x100 * (qp_num + 1)) + (i * 4)),
			ioread32((void __iomem *)rdma_qp_attr + (i * 4)));
}

/**
 * xrnic_qp_timer() - This function configures QP timer
 * @data: QP attribute info
 */
void xrnic_qp_timer(struct timer_list *data)
{
	struct xrnic_qp_attr *qp_attr = (struct xrnic_qp_attr *)data;
	struct xrnic_qp_attr *qp1_attr = qp_attr->qp1_attr;
	enum xrnic_rej_reason reason;
	enum xrnic_msg_rej msg;
	unsigned long flag;
	int	qp1_send_pkt_size;

	spin_lock_irqsave(&qp1_attr->qp_lock, flag);
	if (qp_attr->ip_addr_type == AF_INET)
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv4);
	else
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv6);
	if (qp_attr->curr_state == XRNIC_REJ_SENT) {
		DEBUG_LOG("REJ SENT\n");
		if (qp_attr->resend_count < XRNIC_REJ_RESEND_COUNT) {
			xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
					       qp_attr->qp1_attr,
						qp1_send_pkt_size);
			qp_attr->resend_count = qp_attr->resend_count + 1;
			qp_attr->curr_state = XRNIC_REJ_SENT;
			qp_attr->qp_timer.expires = jiffies +
			usecs_to_jiffies(XRNIC_CM_TIMEOUT *
				(1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
		} else {
			qp_attr->resend_count = 0;
			qp_attr->remote_cm_id = 0;
			xrnic_reset_io_qp(qp_attr);
			memset((void *)&qp_attr->mac_addr, 0,
			       XRNIC_ETH_ALEN);
			qp_attr->ip_addr_type = 0;
			xrnic_qp_app_configuration(qp_attr->qp_num,
						   XRNIC_HW_QP_DISABLE);
			qp_attr->curr_state = XRNIC_LISTEN;
		}
	} else if (qp_attr->curr_state == XRNIC_REP_SENT) {
		DEBUG_LOG("REP SENT\n");
		if (qp_attr->resend_count < XRNIC_REJ_RESEND_COUNT) {
			qp_attr->curr_state = XRNIC_RTU_TIMEOUT;
			xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
					       qp_attr->qp1_attr,
						qp1_send_pkt_size);
			qp_attr->resend_count = qp_attr->resend_count + 1;
			qp_attr->curr_state = XRNIC_REP_SENT;
			qp_attr->qp_timer.expires = jiffies +
			usecs_to_jiffies(XRNIC_CM_TIMEOUT *
				(1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
		} else {
			reason = XRNIC_REJ_TIMEOUT;
			msg = XRNIC_REJ_REP;
			xrnic_cm_prepare_rej(qp_attr, msg, reason);
			xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
					       qp_attr->qp1_attr,
						qp1_send_pkt_size);

			qp_attr->resend_count = 0;
			qp_attr->curr_state = XRNIC_TIMEWAIT;
			qp_attr->qp_timer.expires = jiffies +
			usecs_to_jiffies(XRNIC_CM_TIMEOUT *
				(1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
		}
	} else if (qp_attr->curr_state == XRNIC_MRA_RCVD) {
		DEBUG_LOG("MRA Received\n");
		qp_attr->curr_state = XRNIC_RTU_TIMEOUT;

		reason = XRNIC_REJ_TIMEOUT;
		msg = XRNIC_REJ_TIMEOUT;
		xrnic_cm_prepare_rej(qp_attr, msg, reason);
		xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
				       qp_attr->qp1_attr,
					qp1_send_pkt_size);
		qp_attr->resend_count = 0;
		qp_attr->curr_state = XRNIC_TIMEWAIT;
		qp_attr->qp_timer.expires = jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
			(1 << XRNIC_CM_TIMER_TIMEOUT));
		add_timer(&qp_attr->qp_timer);
	} else if (qp_attr->curr_state == XRNIC_DREQ_SENT) {
		DEBUG_LOG("Disconnect Req Sent\n");
		if (qp_attr->resend_count < XRNIC_DREQ_RESEND_COUNT) {
			qp_attr->curr_state = XRNIC_DREP_TIMEOUT;
			xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
					       qp_attr->qp1_attr,
					       qp1_send_pkt_size);
			qp_attr->resend_count = qp_attr->resend_count + 1;
			qp_attr->curr_state = XRNIC_DREQ_SENT;
			qp_attr->qp_timer.expires = jiffies +
			usecs_to_jiffies(XRNIC_CM_TIMEOUT *
					 (1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
		} else {
			qp_attr->resend_count = 0;
			qp_attr->curr_state = XRNIC_TIMEWAIT;
			qp_attr->qp_timer.expires = jiffies +
				usecs_to_jiffies(XRNIC_CM_TIMEOUT *
				(1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
		}
	} else if (qp_attr->curr_state == XRNIC_TIMEWAIT) {
		DEBUG_LOG("In time wait state\n");
		qp_attr->resend_count = 0;
		qp_attr->remote_cm_id = 0;
#ifdef DISPLAY_REGS_ON_DISCONNECT
		xrnic_display_qp_reg(qp_attr->qp_num);
#endif
		xrnic_reset_io_qp(qp_attr);
		memset((void *)&qp_attr->mac_addr, 0, XRNIC_ETH_ALEN);
		qp_attr->ip_addr_type = 0;
		xrnic_qp_app_configuration(qp_attr->qp_num,
					   XRNIC_HW_QP_DISABLE);
		qp_attr->curr_state = XRNIC_LISTEN;
	} else {
		qp_attr->resend_count = 0;
		qp_attr->qp_timer.expires = 0;
	}
	spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
}

/**
 * xrnic_qp_app_configuration() - This function programs the QP registers
 * @qp_num: QP num to configure
 * @hw_qp_status: value to indicae HW QP or not
 */
void xrnic_qp_app_configuration(int qp_num,
				enum xrnic_hw_qp_status hw_qp_status)
{
	struct xrnic_memory_map *xrnic_mmap = &xrnic_dev->xrnic_mmap;
	struct xrnic_qp_attr *qp_attr = &xrnic_dev->qp_attr[qp_num - 2];
	struct rdma_qp_attr *rdma_qp_attr =
			&xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num - 2];
	u32 config_value = 0;
	int recv_pkt_size = qp_attr->recv_pkt_size;

	/* Host number will directly map to local cm id.*/
	if (hw_qp_status == XRNIC_HW_QP_ENABLE) {
		config_value =	XRNIC_QP_CONFIG_QP_ENABLE |
				XRNIC_QP_CONFIG_RQ_INTR_EN |
				XRNIC_QP_CONFIG_CQE_INTR_EN | xrnic_dev->pmtu |
				XRNIC_QP_CONFIG_RQ_BUFF_SZ(recv_pkt_size) |
				XRNIC_QP_CONFIG_HW_HNDSHK_DIS |
				XRNIC_QP_CONFIG_CQE_WRITE_EN;
	} else if (hw_qp_status == XRNIC_HW_QP_DISABLE) {
		config_value =	XRNIC_QP_CONFIG_RQ_INTR_EN |
				XRNIC_QP_CONFIG_CQE_INTR_EN | xrnic_dev->pmtu |
				XRNIC_QP_CONFIG_RQ_BUFF_SZ(recv_pkt_size) |
				XRNIC_QP_CONFIG_HW_HNDSHK_DIS |
				XRNIC_QP_CONFIG_CQE_WRITE_EN;
		config_value = 0;
	} else {
		DEBUG_LOG("Invalid HW QP status\n");
	}
	if (qp_attr->ip_addr_type == AF_INET6)
		config_value = config_value | XRNIC_QP_CONFIG_IPV6_EN;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));

	config_value = qp_attr->rq_buf_ba_ca_phys;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_buf_ba_ca)));

	config_value = qp_attr->sq_ba_phys;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_ba)));

	config_value = qp_attr->cq_ba_phys;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->cq_ba)));

	config_value = qp_attr->sq_depth | (qp_attr->rq_depth << 16);
	iowrite32(config_value, ((void *)(&rdma_qp_attr->q_depth)));

	config_value = (qp_attr->starting_psn |
			(IB_OPCODE_RC_SEND_ONLY << 24));
	iowrite32(config_value, (void *)&rdma_qp_attr->last_rq_req);

	config_value = be32_to_cpu(qp_attr->ipv4_addr);
	iowrite32(config_value, (void *)&rdma_qp_attr->ip_dest_addr1);
	config_value =	((qp_attr->mac_addr[2] << 24)	|
			 (qp_attr->mac_addr[3] << 16)	|
			 (qp_attr->mac_addr[4] << 8)	|
			  qp_attr->mac_addr[5]);
	iowrite32(config_value, (void *)&rdma_qp_attr->mac_dest_addr_lsb);

	config_value = ((qp_attr->mac_addr[0] << 8) | qp_attr->mac_addr[1]);
	iowrite32(config_value, (void *)&rdma_qp_attr->mac_dest_addr_msb);

	config_value = qp_attr->remote_qp;
	iowrite32(config_value, (void *)&rdma_qp_attr->dest_qp_conf);

	iowrite32(qp_attr->rem_starting_psn, (void *)&rdma_qp_attr->sq_psn);
#ifdef ERNIC_MEM_REGISTER
	if (qp_attr->pd)
		iowrite32(qp_attr->pd, ((void *)(&rdma_qp_attr->pd)));
#endif
}

/**
 * xrnic_qp_hw_configuration() - This function configures QP registers
 * @qp_num: QP num
 */
void xrnic_qp_hw_configuration(int qp_num)
{
	struct xrnic_memory_map *xrnic_mmap = &xrnic_dev->xrnic_mmap;
	struct xrnic_qp_attr *qp_attr = &xrnic_dev->qp_attr[qp_num];
	struct rdma_qp_attr *rdma_qp_attr =
			&xrnic_mmap->xrnic_regs->rdma_qp_attr[qp_num];
	u32 config_value = 0;

	/* As qp_num start from 0 and data QP start from 2 */
	qp_attr->qp_num = qp_num + 2;

	config_value =	XRNIC_QP_ADV_CONFIG_TRAFFIC_CLASS |
			XRNIC_QP_ADV_CONFIG_TIME_TO_LIVE |
			XRNIC_QP_ADV_CONFIG_PARTITION_KEY;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_adv_conf)));

	/*DDR address for RQ and SQ doorbell.*/

	config_value = xrnic_mmap->rq_wrptr_db_add_phys +
			(4 * (qp_attr->qp_num - 1));
	iowrite32(config_value, ((void *)(&rdma_qp_attr->rq_wrptr_db_add)));

	config_value = (xrnic_mmap->sq_cmpl_db_add_phys +
			(4 * (qp_attr->qp_num - 1)))
			& 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_cmpl_db_add)));

	config_value = (xrnic_mmap->stat_rq_buf_ca_phys +
			(4 * (qp_attr->qp_num - 1))) & 0xffffffff;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->stat_rq_buf_ca)));

	config_value =	XRNIC_QP_TIMEOUT_CONFIG_TIMEOUT |
			XRNIC_QP_TIMEOUT_CONFIG_RETRY_CNT	|
			XRNIC_QP_TIMEOUT_CONFIG_RNR_RETRY_CNT |
			XRNIC_QP_TIMEOUT_CONFIG_RNR_NAK_TVAL;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->timeout_conf)));
	qp_attr->qp1_attr = (struct xrnic_qp_attr *)&xrnic_dev->qp1_attr;
	qp_attr->rq_wrptr_db_local = 0;
	qp_attr->sq_cmpl_db_local = 0;
	qp_attr->rq_ci_db_local = 0;
	qp_attr->sq_pi_db_local = 0;
	qp_attr->cm_id = NULL;
	qp_attr->resend_count = 0;
	qp_attr->local_cm_id = qp_attr->qp_num;
	qp_attr->remote_cm_id = 0;
	memset((void *)&qp_attr->mac_addr, 0, XRNIC_ETH_ALEN);
	qp_attr->ip_addr_type = 0;
	qp_attr->sqhd = 0;
	qp_attr->qp_type = XRNIC_QPT_RC;
	qp_attr->ip_addr_type = 0;

	qp_attr->curr_state = XRNIC_LISTEN;

	qp_attr->xrnic_mmap = &xrnic_dev->xrnic_mmap;

	/* Intitialize State with XRNIC_LISTEN */
	timer_setup(&qp_attr->qp_timer, xrnic_qp_timer,
		    (unsigned long)qp_attr);

	spin_lock_init(&qp_attr->qp_lock);
}

#ifdef EXPERIMENTAL_CODE
#define XRNIC_REG_MAP_NODE	0
#define XRNIC_SEND_SGL_NODE		1
#define XRNIC_CQ_BA_NODE		1
#define XRNIC_RQ_BUF_NODE		1
#define XRNIC_SQ_BA_NODE		1
#define XRNIC_TX_HDR_BUF_NODE		1
#define XRNIC_TX_SGL_BUF_NODE		1
#define XRNIC_BYPASS_BUF_NODE		1
#define XRNIC_ERRPKT_BUF_NODE		1
#define XRNIC_OUTERR_STS_NODE		1

#define XRNIC_RQWR_PTR_NODE		1
#define XRNIC_SQ_CMPL_NODE		2
#define XRNIC_STAT_XRNIC_RQ_BUF_NODE	3
#else /* ! EXPERIMENTAL_CODE */
#define XRNIC_REG_MAP_NODE		0
#define XRNIC_SEND_SGL_NODE		1
#define XRNIC_CQ_BA_NODE		2
#define XRNIC_RQ_BUF_NODE		3
#define XRNIC_SQ_BA_NODE		4
#define XRNIC_TX_HDR_BUF_NODE		5
#define XRNIC_TX_SGL_BUF_NODE		6
#define XRNIC_BYPASS_BUF_NODE		7
#define XRNIC_ERRPKT_BUF_NODE		8
#define XRNIC_OUTERR_STS_NODE		9
#define XRNIC_INERR_STS_NODE		10
#define XRNIC_RQWR_PTR_NODE		11
#define XRNIC_SQ_CMPL_NODE		12
#define XRNIC_STAT_XRNIC_RQ_BUF_NODE	13
#define XRNIC_DATA_BUF_BA_NODE		14
#define XRNIC_RESP_ERR_PKT_BUF_BA	15
#endif /* EXPERIMENTAL_CODE */
