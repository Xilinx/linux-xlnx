/*
 * linux/include/asm-arm/arch-atmel/dma.h
 *
 * Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_ATMEL_ARCH_DMA_H
#define __ASM_ATMEL_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x01000000
/*
 * The atmel has 13 internal DMA channels.
 */
#define MAX_DMA_CHANNELS        13
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

#define arch_dma_init(dma_chan) 
#endif /* _ASM_ATMEL_ARCH_DMA_H */
