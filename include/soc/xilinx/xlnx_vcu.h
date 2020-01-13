/*
 * Xilinx VCU Init
 *
 * Copyright (C) 2016 - 2017 Xilinx, Inc.
 *
 * Contacts   Dhaval Shah <dshah@xilinx.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef _XILINX_VCU_H_
#define _XILINX_VCU_H_

struct xvcu_device;

u32 xvcu_get_color_depth(struct xvcu_device *xvcu);
u32 xvcu_get_memory_depth(struct xvcu_device *xvcu);

#endif  /* _XILINX_VCU_H_ */
