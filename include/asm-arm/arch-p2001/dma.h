/*
 * linux/include/asm-armnommu/arch-p2001/dma.h
 *
 *  Copyright (C) 2004 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x01000000
#define MAX_DMA_TRANSFER_SIZE   0x100000
/*  TODO TODO TODO TODO TODO  */

/***************************************************************************/
/* this means that We will use arch/arm/mach/dma.h i.e generic dma module  */
#define MAX_DMA_CHANNELS	0
/***************************************************************************/

#define arch_dma_init(dma_chan) 
#endif /* _ASM_ARCH_DMA_H */
