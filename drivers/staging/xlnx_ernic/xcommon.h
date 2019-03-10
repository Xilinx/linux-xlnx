/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef COMMOM_INCL_H
#define COMMOM_INCL_H
#ifdef __cplusplus
	extern "C" {
#endif

#include <linux/types.h>
#include <linux/cdev.h>
#include "xif.h"
#include "xrocev2.h"
#include "xhw_def.h"
#include "xqp.h"
#include "xcm.h"
#include "xmr.h"
#include "xmain.h"

#define XRNIC_FAILED	-1
#define XRNIC_SUCCESS	0
#define DEBUG_LOG(x, ...)	do { \
					if (debug)\
						pr_info(x, ##__VA_ARGS__); \
				} while (0)

extern int debug;

struct xrnic_dev_info {
	struct xrnic_memory_map		xrnic_mmap;
	struct xrnic_qp_attr		qp1_attr;
	/* TODO: Need to allocate qp_attr on heap.
	 * when max Queue Pairs increases in the design, static memory
	 * requirement will be huge.
	 */
	struct xrnic_qp_attr		qp_attr[XRNIC_MAX_QP_SUPPORT];
	/* DESTINATION ADDR_FAMILY - IPv4/V6 */
	u16				ip_addr_type;
	/* DESTINATION addr in NBO */
	u8				ipv6_addr[16];
	u32				pmtu;
	/* IPV4 address */
	u8				ipv4_addr[4];
	u32				qp_falat_local_ptr;
	struct xrnic_rdma_cm_id_info	*curr_cm_id_info;
	/* TODO: Need to allocate cm_id_info and port_status on heap. */
	struct xrnic_rdma_cm_id_info	*cm_id_info[XRNIC_MAX_PORT_SUPPORT];
	enum xrnic_port_qp_status	port_status[XRNIC_MAX_PORT_SUPPORT];
	/* Interrupt for RNIC */
	u32				xrnic_irq;
	struct tasklet_struct		mad_pkt_recv_task;
	struct tasklet_struct		qp_pkt_recv_task;
	struct tasklet_struct		qp_fatal_task;
	struct tasklet_struct		wqe_completed_task;
	u32				io_qp_count;
	/*Character Driver Interface*/
	struct device_node		*dev_node;
	struct resource			resource;
	struct cdev			cdev;
	char				pkt_buffer[512];
	struct device			*dev;
};

extern struct xrnic_dev_info *xrnic_dev;
#ifdef __cplusplus
	}
#endif
#endif
