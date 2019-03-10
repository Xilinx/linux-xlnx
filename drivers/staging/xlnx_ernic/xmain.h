/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#ifndef _XLNX_MAIN_H_
#define _XLNX_MAIN_H_

#ifdef __cplusplus
	extern "C" {
#endif

#define XRNIC_VERSION	"1.2"
#define NUM_XRNIC_DEVS	1
#define DEVICE_NAME	"xrnic"
#define DRIVER_NAME	"xrnic"

int xrnic_open(struct inode *inode, struct file *file);
int xrnic_release(struct inode *inode, struct file *file);
long xrnic_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
ssize_t xrnic_read(struct file *file, char *buf,
		   size_t count, loff_t *ppos);
ssize_t xrnic_write(struct file *file, const char *buf,
		    size_t count, loff_t *ppos);
void xrnic_fill_wr(struct xrnic_qp_attr *qp_attr, u32 qp_depth);
#ifdef __cplusplus
	}
#endif

#endif
