/*
 * linux/include/asm-armnommu/arch-p2001/io.h
 *
 *  Copyright (C) 2004 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff
	/* Used in kernel/resource.c */


#define PCI_IO_VADDR      (0x0)
#define PCI_MEMORY_VADDR  (0x0)

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __mem_pci(a)            (a)

#define __io(a) ((unsigned long)(a))

/*
 * Defining these two gives us ioremap for free. See asm/io.h.
 * --gmcnutt
 */
#define iomem_valid_addr(iomem,size) (1)
#define iomem_to_phys(iomem) (iomem)

#endif
