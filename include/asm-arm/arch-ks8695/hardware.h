/*
 *  linux/include/asm-arm/arch-ks8695/hardware.h
 *
 *  This file contains the hardware definitions of the KS8695.
 *
 *  Copyright (C) 2002 Micrel Inc.
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * Virtual memory mapping of the KS8695 internal register area.
 * This is a static mapping, set up early in kernel startup.
 */
#define	KS8695_IO_VIRT		0xFF000000
#define	KS8695_REG(x)		((void __iomem *)(KS8695_IO_VIRT + (x)))

#define	pcibios_assign_all_busses()	1
#define	PCIBIOS_MIN_IO		0x00000100
#define	PCIBIOS_MIN_MEM		0x00010000
#define	PCI_MEMORY_VADDR	KS8695P_PCIBG_MEM_BASE
#define	PCI_IO_VADDR		KS8695P_PCIBG_IO_BASE

#endif /* __ASM_ARCH_HARDWARE_H */
