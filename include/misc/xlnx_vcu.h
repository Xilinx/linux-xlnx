/*
 * Xilinx VCU Init
 *
 * Copyright (C) 2016-2017 Xilinx, Inc.
 *
 * Contacts   Dhaval Shah <dshah@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _XILINX_VCU_H_
#define _XILINX_VCU_H_

struct xvcu_device;

u32 xvcu_get_memory_depth(struct xvcu_device *xvcu);

#endif  /* _XILINX_VCU_H_ */
