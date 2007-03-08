/*
 *  include/asm-arm/arch-s3c24a0/dma.h
 *  
 *  $Id: dma.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on linux/include/asm-arm/arch-s3c2410/dma.h
 *
 */

#ifndef __ASM_ARCH_DMA_H__
#define __ASM_ARCH_DMA_H__

#include "hardware.h"

#define MAX_DMA_ADDRESS	0xffffffff

/*
 * NB: By nandy
 * If MAX_DMA_CHANNELS is zero, It means that this architecuture not use 
 * the regular generic DMA interface provided by kernel.
 * Why? I don't know. I will investigate S3C24A0 DMA model and generic
 * DMA interface. But not yet.
 */
#define MAX_DMA_CHANNELS	0

/* The S3C24A0 has four internal DMA channels. */
#define S3C24A0_DMA_CHANNELS	4

#define MAX_S3C24A0_DMA_CHANNELS	S3C24A0_DMA_CHANNELS

#define DMA_CH0			0
#define DMA_CH1			1
#define DMA_CH2			2
#define DMA_CH3			3

#define DMA_BUF_WR		1
#define DMA_BUF_RD		0

typedef void (*dma_callback_t)(void *buf_id, int size);

/* S3C24A0 DMA API */
extern int elfin_request_dma(const char *device_id, dmach_t channel,
				dma_callback_t write_cb, dma_callback_t read_cb); 
extern int elfin_dma_queue_buffer(dmach_t channel, void *buf_id, 
					dma_addr_t data, int size, int write);
extern int elfin_dma_flush_all(dmach_t channel);
extern void elfin_free_dma(dmach_t channel);
extern int elfin_dma_get_current(dmach_t channel, void **buf_id, dma_addr_t *addr);
extern int elfin_dma_stop(dmach_t channel);
    
#endif /* __ASM_ARCH_DMA_H__ */
