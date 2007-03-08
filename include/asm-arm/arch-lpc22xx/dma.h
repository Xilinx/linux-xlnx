/*
 * linux/include/asm-armnommu/arch-lpc22xx/dma.h
 *
 * Copyright (C) 2004 Philips Semiconductors
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_LPC22xx_ARCH_DMA_H
#define __ASM_LPC22xx_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x03000000  /* used in alloc_bootmem,see linux/bootmem.h*/

/*
 * The LPC22xx has no DMA channels.
 */
#undef MAX_DMA_CHANNELS		/* lpc22xx has no dma */    

#endif /* _ASM_LPC22xx_ARCH_DMA_H */

