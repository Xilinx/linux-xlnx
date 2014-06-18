/*
 * Xilinx DMA Engine drivers support header file
 *
 * Copyright (C) 2010-2014 Xilinx, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DMA_XILINX_DMA_H
#define __DMA_XILINX_DMA_H

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

/* Specific hardware configuration-related constants */
#define XILINX_DMA_NO_CHANGE	0xFFFF;

/* DMA IP masks */
#define XILINX_DMA_IP_DMA	0x00100000	/* A DMA IP */
#define XILINX_DMA_IP_CDMA	0x00200000	/* A Central DMA IP */
#define XILINX_DMA_IP_VDMA	0x00400000	/* A Video DMA IP */
#define XILINX_DMA_IP_MASK	0x00700000	/* DMA IP MASK */

/* Device Id in the private structure */
#define XILINX_DMA_DEVICE_ID_SHIFT	28

/*
 * Device configuration structure
 *
 * If used to start/stop parking mode for Xilinx VDMA, vsize must be -1
 * If used to set interrupt coalescing and delay counter only for
 * Xilinx VDMA, hsize must be -1
 */
struct xilinx_vdma_config {
	int frm_dly;			/* Frame delay */
	int gen_lock;			/* Whether in gen-lock mode */
	int master;			/* Master that it syncs to */
	int frm_cnt_en;			/* Enable frame count enable */
	int park;			/* Whether wants to park */
	int park_frm;			/* Frame to park on */
	int coalesc;			/* Interrupt coalescing threshold */
	int delay;			/* Delay counter */
	int reset;			/* Reset Channel */
	int ext_fsync;			/* External Frame Sync */
};

/* Device configuration structure for DMA */
struct xilinx_dma_config {
	enum dma_transfer_direction direction;
					/* Channel direction */
	int coalesc;			/* Interrupt coalescing threshold */
	int delay;			/* Delay counter */
	int reset;			/* Reset Channel */
};

/* Device configuration structure for CDMA */
struct xilinx_cdma_config {
	enum dma_transfer_direction direction;
					/* Channel direction */
	int coalesc;			/* Interrupt coalescing threshold */
	int delay;			/* Delay counter */
	int reset;			/* Reset Channel */
};


int xilinx_vdma_channel_set_config(struct dma_chan *dchan,
					struct xilinx_vdma_config *cfg);

#endif
