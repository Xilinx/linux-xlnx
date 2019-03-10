/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */
#ifndef _XRNIC_IOCTL_H_
#define _XRNIC_IOCTL_H_

#include <asm/ioctl.h>
#include "xlog.h"

#define XRNIC_MAGIC 'L'

#define XRNIC_DISPLAY_MMAP_ALL		_IOW(XRNIC_MAGIC, 1, uint)
#define XRNIC_DISPLAY_MMAP_CONFIG	_IOW(XRNIC_MAGIC, 2, uint)
#define XRNIC_DISPLAY_MMAP_QP1		_IOW(XRNIC_MAGIC, 3, uint)
#define XRNIC_DISPLAY_MMAP_QPX		_IOW(XRNIC_MAGIC, 4, uint)
#define XRNIC_DISPLAY_PKT		_IOW(XRNIC_MAGIC, 5, uint)

#define XRNIC_MAX_CMDS	5

#endif /* _XRNIC_IOCTL_H_ */
