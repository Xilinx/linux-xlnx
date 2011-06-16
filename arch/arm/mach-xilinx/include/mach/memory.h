/* arch/arm/mach-xilinx/include/mach/memory.h
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_MEMORY_H__
#define __ASM_ARCH_MEMORY_H__

#include <mach/hardware.h>

#if !defined(__ASSEMBLY__) && defined(CONFIG_ZONE_DMA)
extern void xilinx_adjust_zones(unsigned long *, unsigned long *);
#define arch_adjust_zones(size, holes) \
	xilinx_adjust_zones(size, holes)
#endif

/*
 * Virtual view <-> DMA view memory address translations
 * This macro is used to translate the virtual address to an address
 * suitable to be passed to set_dma_addr()
 */
#define __virt_to_bus(a)	__virt_to_phys(a)

/*
 * Used to convert an address for DMA operations to an address that the
 * kernel can use.
 */
#define __bus_to_virt(a)	__phys_to_virt(a)

#define __pfn_to_bus(p)		__pfn_to_phys(p)
#define __bus_to_pfn(b)		__phys_to_pfn(b)

#endif /* __ASM_ARCH_MXC_MEMORY_H__ */
