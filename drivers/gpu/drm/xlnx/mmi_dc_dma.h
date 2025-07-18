/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMA Interface for Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DC_DMA_H__
#define __MMI_DC_DMA_H__

#include <linux/device.h>

struct mmi_dc_dma_chan;

struct mmi_dc_dma_chan *mmi_dc_dma_request_channel(struct device *dev,
						   const char *name);
void mmi_dc_dma_release_channel(struct mmi_dc_dma_chan *chan);
unsigned int mmi_dc_dma_copy_align(struct mmi_dc_dma_chan *chan);
void mmi_dc_dma_config_channel(struct mmi_dc_dma_chan *chan,
			       dma_addr_t target_addr, bool video_group);
void mmi_dc_dma_start_transfer(struct mmi_dc_dma_chan *chan,
			       dma_addr_t buffer_addr, size_t line_size,
			       size_t line_stride, size_t num_lines,
			       bool auto_repeat);
void mmi_dc_dma_stop_transfer(struct mmi_dc_dma_chan *chan);

#endif /* __MMI_DC_DMA_H__ */
