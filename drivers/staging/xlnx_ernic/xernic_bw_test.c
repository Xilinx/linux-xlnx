// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx RDMA NIC perftest driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <net/addrconf.h>
#include "xcommon.h"
#include "xperftest.h"

/* Default Port Number for Perftest and Depths for XRNIC */
#define	PERFTEST_PORT			18515
#define	PERFTEST_SQ_DEPTH		0x80
#define	PERFTEST_RQ_DEPTH		0x40
/* Admin and IO QPs */
#define	PERFTEST_ADMIN_QPS		1
#define	PERFTEST_IO_QPS			1
#define	PERFTEST_MAX_QPS		(PERFTEST_ADMIN_QPS + PERFTEST_IO_QPS)
#define	PERFTEST_DEFAULT_MEM_SIZE	(4 * 1024 * 1024)

#define _1MB_BUF_SIZ			(1024 * 1024)
#define PERF_TEST_RQ_BUF_SIZ	((_1MB_BUF_SIZ + XRNIC_RECV_PKT_SIZE) *\
				PERFTEST_RQ_DEPTH)

struct xrnic_rdma_cm_id *cm_id;
static char server_ip[32] = "0.0.0.0";
struct ernic_pd *pd;
int prev_qpn;

/* TODO: currently, we have single instance.
 * Need to convert as per-instance context.
 */
struct perftest_ctx {
	struct xrnic_rdma_cm_id *cm_id;
	struct ernic_pd *pd;
	struct mr *reg_mr; /*registered MR */
};

phys_addr_t phys_mem[PERFTEST_MAX_QPS];
int io_mr_idx;
struct mr *perftest_io_mr[PERFTEST_IO_QPS];

struct perftest_ctx perf_context[PERFTEST_MAX_QPS];

struct perftest_wr {
	union ctx	ctx;
	__u8		reserved1[2];
	__u32		local_offset[2];
	__u32		length;
	__u8		opcode;
	__u8		reserved2[3];
	__u32		remote_offset[2];
	__u32		remote_tag;
	__u32		completion_info[4];
	__u8		reserved4[16];
} __packed;

struct xrnic_qp_init_attr qp_attr;

struct perftest_trinfo {
	phys_addr_t	rq_buf_ba_phys;
	phys_addr_t	send_sgl_phys;
	phys_addr_t	sq_ba_phys;
	phys_addr_t	cq_ba_phys;
	phys_addr_t	rq_wptr_db_phys;
	phys_addr_t	sq_cmpl_db_phys;
	void __iomem	*rq_buf_ba;
	void __iomem	*send_sgl;
	void __iomem	*sq_ba;
	void __iomem	*cq_ba;
};

struct perftest_trinfo trinfo;
struct xrnic_rdma_conn_param conn_param;
int rq_ci_db, sq_cmpl_db;

int port = -1;
module_param_string(server_ip, server_ip, sizeof(server_ip), 0444);
module_param(port, int, 0444);
MODULE_PARM_DESC(server_ip, "Target server ip address");

/**
 * perftest_parse_addr() - Parses the input IP address.
 * @s_addr: IP address structure.
 * @buf: Output IPV4 buffer pointer.
 * return: 0 If address is either IPv6 or IPv4.
 *		else, returns EINVAL.
 */
int perftest_parse_addr(struct sockaddr_storage *s_addr, char *buf)
{
	size_t buflen = strlen(buf);
	int ret;
	const char *delim;

	if (buflen <= INET_ADDRSTRLEN) {
		struct sockaddr_in *sin_addr = (struct sockaddr_in *)s_addr;

		ret = in4_pton(buf, buflen, (u8 *)&sin_addr->sin_addr.s_addr,
			       '\0', NULL);
		if (!ret)
			goto fail;

		sin_addr->sin_family = AF_INET;
		return 0;
	}
	if (buflen <= INET6_ADDRSTRLEN) {
		struct sockaddr_in6 *sin6_addr = (struct sockaddr_in6 *)s_addr;

		ret = in6_pton(buf, buflen,
			       (u8 *)&sin6_addr->sin6_addr.s6_addr,
			       -1, &delim);
		if (!ret)
			goto fail;

		sin6_addr->sin6_family = AF_INET6;
		return 0;
	}
fail:
	return -EINVAL;
}

/**
 * rq_handler() - receive packet callback routine.
 * @rq_count: Rx packet count.
 * @rq_context: context info.
 */
void rq_handler(u32 rq_count, void *rq_context)
{
	int i, qp_num, offset;
	struct ernic_bwtest_struct *rq_buf;
	struct xrnic_rdma_cm_id *cm_id;
	struct perftest_wr *sq_wr;
	struct mr *mem;
	struct perftest_ctx *ctx;

	ctx = (struct perftest_ctx *)rq_context;
	cm_id = ctx->cm_id;
	qp_num = cm_id->child_qp_num;
	offset = sq_cmpl_db * XRNIC_SEND_SGL_SIZE;
	for (i = 0; i < rq_count; i++) {
		if (qp_num == 1) {
			rq_buf = (struct ernic_bwtest_struct *)(char *)
				cm_id->qp_info.rq_buf_ba_ca +
				((qp_num - 1) * rq_ci_db *
				XRNIC_RECV_PKT_SIZE);
			if (io_mr_idx > PERFTEST_IO_QPS)
				goto done;
			mem = perftest_io_mr[io_mr_idx];

			rq_buf->rkey = htonl((unsigned int)mem->rkey);
			rq_buf->vaddr = cpu_to_be64(mem->vaddr);

			memcpy((u8 *)(trinfo.send_sgl + offset),
			       (u8 *)rq_buf,
			       sizeof(struct ernic_bwtest_struct));

			sq_wr = (struct perftest_wr *)trinfo.sq_ba +
							sq_cmpl_db;
			sq_wr->ctx.wr_id = sq_cmpl_db;
			sq_wr->length = sizeof(struct ernic_bwtest_struct);
			sq_wr->remote_tag = ntohl(0xDEAD);
			sq_wr->local_offset[0] = trinfo.send_sgl_phys + offset;
			sq_wr->local_offset[1] = 0;

			sq_wr->remote_offset[0] = 0x12345678;
			sq_wr->remote_offset[1] = 0xABCDABCD;
			sq_wr->completion_info[0] = htonl(0x11111111);
			sq_wr->completion_info[1] = htonl(0x22222222);
			sq_wr->completion_info[2] = htonl(0x33333333);
			sq_wr->completion_info[3] = htonl(0x44444444);
			sq_wr->opcode = XRNIC_SEND_ONLY;
		}
		xrnic_post_recv(&cm_id->qp_info, 1);
		if (qp_num == 1) {
			xrnic_post_send(&cm_id->qp_info, 1);
			if (prev_qpn != rq_buf->qp_number) {
				if (prev_qpn != 0)
					io_mr_idx++;
				prev_qpn = rq_buf->qp_number;
			}
		}

done:
		rq_ci_db++;

		if (rq_ci_db >= (PERFTEST_RQ_DEPTH - 20))
			rq_ci_db = 0;
		if (qp_num == 1) {
			sq_cmpl_db++;
			if (sq_cmpl_db >= PERFTEST_SQ_DEPTH)
				sq_cmpl_db = 0;
		}
	}
}

/**
 * sq_handler() - completion call back.
 * @sq_count: Tx packet count.
 * @sq_context: context info.
 */
void sq_handler(u32 sq_count, void *sq_context)
{
/* TODO: This function is just a place holder for now.
 * This function should handle completions for outgoing
 * RDMA_SEND, RDMA_READ and RDMA_WRITE.
 */
	pr_info("XLNX[%d:%s]\n", __LINE__, __func__);
}

/**
 * perftest_fill_wr() - Fills the workrequest in send queue base address.
 * @sq_ba: send queue base address of the QP.
 */
void perftest_fill_wr(void __iomem *sq_ba)
{
	struct perftest_wr *sq_wr;
	int i;

	for (i = 0; i < XRNIC_SQ_DEPTH; i++) {
		sq_wr = (struct perftest_wr *)sq_ba + i;
		sq_wr->ctx.wr_id = i;
		sq_wr->length = 16;
		sq_wr->completion_info[0] = 0xAAAAAAAA;
		sq_wr->completion_info[1] = 0xBBBBBBBB;
		sq_wr->completion_info[2] = 0xCCCCCCCC;
		sq_wr->completion_info[3] = 0xDDDDDDDD;
		sq_wr->opcode = XRNIC_SEND_ONLY;
	}
}

/**
 * perftest_cm_handler() - CM handler call back routine.
 * @cm_id: CM ID on which event received.
 * @conn_event: Event information on the CM.
 * @return: 0 on success or error code on failure.
 */
static int perftest_cm_handler(struct xrnic_rdma_cm_id *cm_id,
			       struct xrnic_rdma_cm_event_info *conn_event)
{
	int qp_num, per_qp_size;
	struct perftest_ctx *ctx;

	qp_num = cm_id->child_qp_num;
	memset(&qp_attr, 0, sizeof(struct xrnic_qp_init_attr));
	ctx = &perf_context[qp_num - 1];
	switch (conn_event->cm_event) {
	case XRNIC_REQ_RCVD:
		qp_attr.xrnic_rq_event_handler = rq_handler;
		qp_attr.xrnic_sq_event_handler = sq_handler;
		qp_attr.qp_type = XRNIC_QPT_RC;
		if (qp_num > 1) {
			qp_attr.recv_pkt_size = _1MB_BUF_SIZ;
				per_qp_size = (qp_num - 2) * _1MB_BUF_SIZ *
				PERFTEST_RQ_DEPTH + XRNIC_RECV_PKT_SIZE *
				PERFTEST_RQ_DEPTH;
		} else {
			qp_attr.recv_pkt_size = XRNIC_RECV_PKT_SIZE;
			per_qp_size = 0;
		}
		qp_attr.rq_buf_ba_ca_phys = trinfo.rq_buf_ba_phys +
							per_qp_size;
		qp_attr.rq_buf_ba_ca = (char *)trinfo.rq_buf_ba +
					per_qp_size;
		per_qp_size = (qp_num - 1) * sizeof(struct perftest_wr) *
				PERFTEST_SQ_DEPTH;
		qp_attr.sq_ba_phys = trinfo.sq_ba_phys + per_qp_size;
		qp_attr.sq_ba	= (char *)trinfo.sq_ba + per_qp_size;
		per_qp_size = (qp_num - 1) * (PERFTEST_SQ_DEPTH * 4);
		qp_attr.cq_ba_phys = trinfo.cq_ba_phys + per_qp_size;
		qp_attr.cq_ba	= (char *)trinfo.cq_ba + per_qp_size;
		qp_attr.rq_context = ctx;
		qp_attr.sq_context = ctx;
		ctx->cm_id = cm_id;
		qp_attr.sq_depth = PERFTEST_SQ_DEPTH;
		qp_attr.rq_depth = PERFTEST_RQ_DEPTH;
		ctx->reg_mr = reg_phys_mr(pd, phys_mem[qp_num - 1],
					  PERFTEST_DEFAULT_MEM_SIZE,
					  MR_ACCESS_RDWR, NULL);
		if (qp_num > 1)
			perftest_io_mr[qp_num - 2] = ctx->reg_mr;

		xrnic_rdma_create_qp(cm_id, ctx->reg_mr->pd,
				     &qp_attr);

		memset(&conn_param, 0, sizeof(conn_param));
		conn_param.initiator_depth = 16;
		conn_param.responder_resources = 16;
		xrnic_rdma_accept(cm_id, &conn_param);
		break;
	case XRNIC_ESTABLISHD:
		if (cm_id->child_qp_num > 1) {
			perftest_fill_wr((char *)trinfo.sq_ba +
					 ((qp_num - 1) *
					  sizeof(struct perftest_wr) *
					  PERFTEST_SQ_DEPTH));
			xrnic_hw_hs_reset_sq_cq(&cm_id->qp_info, NULL);
		}
		break;
	case XRNIC_DREQ_RCVD:
		xrnic_destroy_qp(&cm_id->qp_info);
		xrnic_rdma_disconnect(cm_id);
		xrnic_rdma_destroy_id(cm_id, 0);
		dereg_mr(ctx->reg_mr);
		io_mr_idx = 0;
		prev_qpn = 0;
		rq_ci_db = 0;
		sq_cmpl_db = 0;
		break;
	default:
		pr_info("Unhandled CM Event: %d\n",
			conn_event->cm_event);
	}
	return 0;
}

/**
 * perftest_init() - Perf test init function.
 * @return: 0 on success or error code on failure.
 */
static int __init perftest_init(void)
{
	int ret, i;
	struct sockaddr_storage s_addr;
	struct sockaddr_in *sin_addr;
	struct sockaddr_in6 *sin6_addr;

	if (strcmp(server_ip, "0.0.0.0") == 0) {
		pr_err("server ip module parameter not provided\n");
		return -EINVAL;
	}

	/* If port number is not set, then it should point to the default */
	if (-1 == port) {
		port = PERFTEST_PORT;
		pr_info("Using app default port number: %d\n", port);
	} else if (port < 0) {
		/* Any other -ve value */
		/* Some ports are reserved and few other may be use,
		 * we could add check here to validate given port number
		 * is free to use or not
		 */
		pr_err("port number should not be a negative value\n");
		return -EINVAL;
	}
	pr_info("Using port number %d\n", port);

	cm_id = xrnic_rdma_create_id(perftest_cm_handler, NULL, XRNIC_PS_TCP,
				     XRNIC_QPT_UC, PERFTEST_MAX_QPS);
	if (!cm_id)
		goto err;

	if (perftest_parse_addr(&s_addr, server_ip))
		goto err;

	if (s_addr.ss_family == AF_INET) {
		sin_addr = (struct sockaddr_in *)&s_addr;
		ret = xrnic_rdma_bind_addr(cm_id,
					   (u8 *)&sin_addr->sin_addr.s_addr,
					   port, AF_INET);
		if (ret < 0) {
			pr_err("RDMA BIND Failed for IPv4\n");
			goto err;
		}
	}
	if (s_addr.ss_family == AF_INET6) {
		sin6_addr = (struct sockaddr_in6 *)&s_addr;
		ret = xrnic_rdma_bind_addr(cm_id,
					   (u8 *)&sin6_addr->sin6_addr.s6_addr,
					   port, AF_INET6);
		if (ret < 0) {
			pr_err("RDMA BIND Failed for IPv6\n");
			goto err;
		}
	}

	if (xrnic_rdma_listen(cm_id, 1) != XRNIC_SUCCESS)
		goto err;

	trinfo.rq_buf_ba_phys = alloc_mem(NULL, PERF_TEST_RQ_BUF_SIZ);
	if (-ENOMEM == trinfo.rq_buf_ba_phys)
		goto err;
	trinfo.rq_buf_ba =
		(void __iomem *)(uintptr_t)get_virt_addr
					(trinfo.rq_buf_ba_phys);

	trinfo.send_sgl_phys = alloc_mem(NULL, 0x400000);
	if (-ENOMEM == trinfo.send_sgl_phys)
		goto err;
	trinfo.send_sgl =
		(void __iomem *)(uintptr_t)get_virt_addr(trinfo.send_sgl_phys);

	trinfo.sq_ba_phys = alloc_mem(NULL, 0x100000);
	if (-ENOMEM == trinfo.sq_ba_phys)
		goto err;
	trinfo.sq_ba =
		(void __iomem *)(uintptr_t)get_virt_addr(trinfo.sq_ba_phys);

	trinfo.cq_ba_phys = alloc_mem(NULL, 0x40000);
	if (-ENOMEM == trinfo.cq_ba_phys)
		goto err;
	trinfo.cq_ba =
		(void __iomem *)(uintptr_t)get_virt_addr(trinfo.cq_ba_phys);
	trinfo.rq_wptr_db_phys = alloc_mem(NULL, 8);
	trinfo.sq_cmpl_db_phys = alloc_mem(NULL, 8);
	pd = alloc_pd();
	for (i = 0; i < PERFTEST_MAX_QPS; i++) {
		phys_mem[i] = alloc_mem(pd, PERFTEST_DEFAULT_MEM_SIZE);
		if (IS_ERR_VALUE(phys_mem[i])) {
			pr_err("PERFTEST[%d:%s] Mem registration failed: %lld\n",
			       __LINE__, __func__, phys_mem[i]);
			goto err;
		}
	}

	return 0;

err:
/* free_mem() works on only valid physical address returned from alloc_mem(),
 * and ignores if NULL or invalid address is passed.
 * So, even if any of the above allocations fail in the middle,
 * we can safely call free_mem() on all addresses.
 *
 * we are using carve-out memory for the requirements of ERNIC.
 * so, we cannot use devm_kzalloc() as kernel cannot see these
 * memories until ioremapped.
 */
	free_mem(trinfo.rq_buf_ba_phys);
	free_mem(trinfo.send_sgl_phys);
	free_mem(trinfo.sq_ba_phys);
	free_mem(trinfo.cq_ba_phys);
	free_mem(trinfo.rq_wptr_db_phys);
	free_mem(trinfo.sq_cmpl_db_phys);
	for (i = 0; i < PERFTEST_MAX_QPS; i++)
		free_mem(phys_mem[i]);

	dealloc_pd(pd);

	return -EINVAL;
}

/**
 * perftest_exit() - perftest module exit function.
 */
static void __exit perftest_exit(void)
{
	int i;

	free_mem(trinfo.rq_buf_ba_phys);
	free_mem(trinfo.send_sgl_phys);
	free_mem(trinfo.sq_ba_phys);
	free_mem(trinfo.cq_ba_phys);
	free_mem(trinfo.rq_wptr_db_phys);
	free_mem(trinfo.sq_cmpl_db_phys);
	for (i = 0; i < PERFTEST_MAX_QPS; i++)
		free_mem(phys_mem[i]);

	dealloc_pd(pd);
}

/* This driver is an example driver, which uses the APIs exported in
 * ernic driver, to demonstrate the RDMA communication between peers
 * on the infiniband network. The remote peer can be any RDMA enbled NIC.
 * There is no real device for this driver and so, compatibility string and
 * probe function are not needed for this driver.
 */
module_init(perftest_init);
module_exit(perftest_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Perftest Example driver");
MODULE_AUTHOR("SDHANVAD");
