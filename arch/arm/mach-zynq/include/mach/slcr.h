/* arch/arm/mach-zynq/include/mach/slcr.h
 *
 *  Copyright (C) 2012 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_SLCR_H__
#define __MACH_SLCR_H__

extern void xslcr_write(u32 offset, u32 val);
extern u32 xslcr_read(u32 offset);

extern void xslcr_system_reset(void);

extern void xslcr_init_preload_fpga(void);
extern void xslcr_init_postload_fpga(void);

#endif /* __MACH_SLCR_H__ */

