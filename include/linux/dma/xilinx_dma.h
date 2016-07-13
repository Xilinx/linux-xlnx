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

/* DMA IP masks */
#define XILINX_DMA_IP_DMA	0x00100000	/* A DMA IP */

/* Device Id in the private structure */
#define XILINX_DMA_DEVICE_ID_SHIFT	28

/**
 * struct xilinx_vdma_config - VDMA Configuration structure
 * @frm_dly: Frame delay
 * @gen_lock: Whether in gen-lock mode
 * @master: Master that it syncs to
 * @frm_cnt_en: Enable frame count enable
 * @park: Whether wants to park
 * @park_frm: Frame to park on
 * @coalesc: Interrupt coalescing threshold
 * @delay: Delay counter
 * @reset: Reset Channel
 * @ext_fsync: External Frame Sync source
 */
struct xilinx_vdma_config {
	int frm_dly;
	int gen_lock;
	int master;
	int frm_cnt_en;
	int park;
	int park_frm;
	int coalesc;
	int delay;
	int reset;
	int ext_fsync;
};

/**
 * struct xilinx_mcdma_config - DMA Multi channel configuration structure
 * @tdest: Channel to operate on
 * @tid:   Channel configuration
 * @tuser: Tuser configuration
 * @ax_user: ax_user value
 * @ax_cache: ax_cache value
 */
struct xilinx_mcdma_config {
	u8 tdest;
	u8 tid;
	u8 tuser;
	u8 ax_user;
	u8 ax_cache;
};
int xilinx_vdma_channel_set_config(struct dma_chan *dchan,
					struct xilinx_vdma_config *cfg);
int xilinx_dma_channel_mcdma_set_config(struct dma_chan *dchan,
					struct xilinx_mcdma_config *cfg);
#endif
