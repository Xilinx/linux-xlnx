/*
 * linux/include/asm-armnommu/arch-espd_4510b/dma.h
 *
 * Currently this is not used for the s3c4510b port
 *
 * Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 *
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_S3C4510B_ARCH_DMA_H
#define __ASM_S3C4510B_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x03000000
/*
 * The S3C4510B has 2 internal DMA channels.
 */
#define MAX_DMA_CHANNELS	2
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

#endif /* __ASM_S3C4510B_ARCH_DMA_H */

