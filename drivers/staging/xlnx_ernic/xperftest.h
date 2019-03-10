/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 */

#ifndef _PERF_TEST_H
#define _PERF_TEST_H

#ifdef __cplusplus
	extern "C" {
#endif

struct ernic_bwtest_struct {
	u64			reserved1;
	int			qp_number;
	int			reserved2;
	unsigned long long	rkey;
	unsigned long long	vaddr;
	char			reserved3[24];
};

int perftest_parse_addr(struct sockaddr_storage *s_addr, char *buf);
void rq_handler(u32 rq_count, void *rq_context);
void sq_handler(u32 rq_count, void *sq_context);
void perftest_fill_wr(void __iomem *sq_ba);

#ifdef __cplusplus
	}
#endif

#endif /* _PERF_TEST_H*/
