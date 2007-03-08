/*
 * linux/include/asm-armnommu/arch-s3c3410/dma.h
 *
 * Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_S3C3410_ARCH_DMA_H
#define __ASM_S3C3410_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x03000000
/*
 * The S3C3410 has 2 internal DMA channels.
 */
#define MAX_DMA_CHANNELS	2
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

#endif /* _ASM_S3C3410_ARCH_DMA_H */

