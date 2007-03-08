/*
 * linux/include/asm-arm/arch-atmel/io.h
 *
 * Copyright (C) 1997-1999 Russell King
 * modified for 2.6 by Hyok S. Choi
 *
 * Modifications:
 *  06-12-1997	RMK	Created.
 *  07-04-1999	RMK	Major cleanup
 *  02-19-2001  gjm     Leveraged for armnommu/dsc21
 *  03-15-2004  hsc     modified 
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff
	/* Used in kernel/resource.c */


#define PCI_IO_VADDR      (0x0)
#define PCI_MEMORY_VADDR  (0x0)

#define __io(a) (CONFIG_IO16_BASE + (a))
#define __iob(a) (CONFIG_IO8_BASE + (a))	// byte io address
#define __mem_pci(a)	((unsigned long)(a))	 

/*
 * Defining these two gives us ioremap for free. See asm/io.h.
 * --gmcnutt
 */
#define iomem_valid_addr(iomem,sz) (1)
#define iomem_to_phys(iomem) (iomem)

#ifdef CONFIG_CPU_BIG_ENDIAN
# error not_yet_supported
#define __io_noswap 1
#endif

#endif

