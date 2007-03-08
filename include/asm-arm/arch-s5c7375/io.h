/*
 *  linux/include/asm-arm/arch-s5c7375/io.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS
 *			Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff
	/* Used in kernel/resource.c */

/*
 * We have the this routine to use usb host device driver
 *
 */


#define PCI_IO_VADDR      (0x0)
#define PCI_MEMORY_VADDR  (0x0)

#define __io(a)			(PCI_IO_VADDR + (a))
#define __mem_pci(a)		((unsigned long)(a))
#define __mem_isa(a)		(PCI_MEMORY_VADDR + (unsigned long)(a))


/*
 * Generic virtual read/write
 */
#if 0
#define __arch_getw(a)		(*(volatile unsigned short *)(a))
#define __arch_putw(v,a)	(*(volatile unsigned short *)(a) = (v))
#endif
/*
 * Validate the pci memory address for ioremap.
 */
#define iomem_valid_addr(iomem,size)	(1)


/* 
 * For example, 
 * CS8900A Net Device Driver
 * for asm/io.h 
 */
/*
#define __io 
*/


/*
 * Convert PCI memory space to a CPU physical address
 */
#define iomem_to_phys(iomem)	(iomem)

#endif
