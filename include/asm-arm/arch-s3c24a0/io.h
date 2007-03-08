/*
 * include/asm-arm/arch-s3c24a0/io.h
 *
 * $Id: io.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
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

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)         (PCIO_BASE + (a))
#define __mem_pci(a)    ((unsigned long)(a))
#define __mem_isa(a)    ((unsigned long)(a))

#if 0
/*
 * Generic virtual read/write
 */
#define __arch_getw(a)                  (*(volatile unsigned short *)(a))
#define __arch_putw(v,a)                (*(volatile unsigned short *)(a) = (v))
#endif

#define iomem_valid_addr(iomem,sz)      (1)
#define iomem_to_phys(iomem)            (iomem)

#endif /* __ASM_ARM_ARCH_IO_H */
