/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP 2X Montgomery Modular Multiplier Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Kunal Rane <kunal.rane@amd.com>
 */

#ifndef __XLNX_HDCP2X_MMULT_H__
#define __XLNX_HDCP2X_MMULT_H__

#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define XHDCP2X_MMULT_ADDR_AP		0x000
#define XHDCP2X_MMULT_ADDR_U_BASE	0x040
#define XHDCP2X_MMULT_ADDR_U_HIGH	0x07f
#define XHDCP2X_MMULT_A_BASE		0x080
#define XHDCP2X_MMULT_A_HIGH		0x0bf
#define XHDCP2X_MMULT_B_BASE		0x0c0
#define XHDCP2X_MMULT_B_HIGH		0x0ff
#define XHDCP2X_MMULT_N_BASE		0x100
#define XHDCP2X_MMULT_N_HIGH		0x13f
#define XHDCP2X_MMULT_NPRIME_BASE	0x140
#define XHDCP2X_MMULT_NPRIME_HIGH	0x17f
#define XHDCP2X_MMULT_OFFSET_MULT	0x4
#define XHDCP2X_MMULT_ADDR_AP_RD	0x80
#define XHDCP2X_MMULT_ADDR_AP_WR	0x01
#define XHDCP2X_MMULT_CHECK_CONST	0x1
#define XHDCP2X_MMULT_MAX_TYPES		4
#define XHDCP2X_MMULT_ADDR		2

#define XHDCP2X_MMULT_DONE		BIT(1)
#define XHDCP2X_MMULT_READY		BIT(0)

struct xlnx_hdcp2x_mmult_hw {
	void __iomem *mmult_coreaddress;
};

enum xhdcp2x_mmult_type {
	XHDCP2X_MMULT_A = 0,
	XHDCP2X_MMULT_B = 1,
	XHDCP2X_MMULT_N = 2,
	XHDCP2X_MMULT_NPRIME = 3
};

u32 xlnx_hdcp2x_mmult_is_done(struct xlnx_hdcp2x_mmult_hw *mmult_cfg);
u32 xlnx_hdcp2x_mmult_is_ready(struct xlnx_hdcp2x_mmult_hw *mmult_cfg);
u32 xlnx_hdcp2x_mmult_read_u_words(struct xlnx_hdcp2x_mmult_hw *mmult_cfg,
				   int offset, int *data, int length);
int xlnx_hdcp2x_mmult_write_type(struct xlnx_hdcp2x_mmult_hw *mmult_cfg,
				 int offset, int *data, int length, int type);
int xlnx_hdcp2x_mmult_cfginit(struct xlnx_hdcp2x_mmult_hw *mmult_cfg);
void xlnx_hdcp2x_mmult_enable(struct xlnx_hdcp2x_mmult_hw *mmult_cfg);

#endif  /* __XLNX_HDCP2X_MMULT_H__ */
