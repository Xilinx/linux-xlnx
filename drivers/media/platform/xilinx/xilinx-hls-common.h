/*
 * Xilinx HLS common header
 *
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Radhey Shyam Pandey <radheys@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XILINX_HLS_COMMON_H__
#define __XILINX_HLS_COMMON_H__


#define XHLS_DEF_WIDTH                          1920
#define XHLS_DEF_HEIGHT                         1080

#define XHLS_REG_CTRL_DONE                      (1 << 1)
#define XHLS_REG_CTRL_IDLE                      (1 << 2)
#define XHLS_REG_CTRL_READY                     (1 << 3)
#define XHLS_REG_CTRL_AUTO_RESTART              (1 << 7)
#define XHLS_REG_GIE                            0x04
#define XHLS_REG_GIE_GIE                        (1 << 0)
#define XHLS_REG_IER                            0x08
#define XHLS_REG_IER_DONE                       (1 << 0)
#define XHLS_REG_IER_READY                      (1 << 1)
#define XHLS_REG_ISR                            0x0c
#define XHLS_REG_ISR_DONE                       (1 << 0)
#define XHLS_REG_ISR_READY                      (1 << 1)
#define XHLS_REG_ROWS                           0x10
#define XHLS_REG_COLS                           0x18

#endif /* __XILINX_HLS_COMMON_H__ */
