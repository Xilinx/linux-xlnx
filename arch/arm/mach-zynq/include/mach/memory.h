/* arch/arm/mach-zynq/include/mach/memory.h
 *
 *  Copyright (C) 2011 Xilinx
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

#ifndef __MACH_MEMORY_H__
#define __MACH_MEMORY_H__

#include <asm/sizes.h>

#define PLAT_PHYS_OFFSET	UL(0x0)

#if !defined(__ASSEMBLY__) && defined(CONFIG_ZONE_DMA)
extern void xilinx_adjust_zones(unsigned long *, unsigned long *);
#define arch_adjust_zones(size, holes) \
	xilinx_adjust_zones(size, holes)
#endif

#endif
