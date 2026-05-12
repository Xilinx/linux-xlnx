/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared definitions for Xilinx video-aware DMA engines (framebuffer,
 * AI layout formatter, and similar IPs using the same control-register
 * conventions).
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 */
#ifndef __XILINX_VIDEO_DMA_COMMON_H__
#define __XILINX_VIDEO_DMA_COMMON_H__

#include <linux/bits.h>

/* Compatible for AI Layout Formatter Write (binding: xlnx,ai-layout-formatter-wr.yaml). */
#define XILINX_AI_LAYOUT_FORMATTER_DT_COMPAT	"xlnx,ai-layout-formatter-wr-v1"

/**
 * enum xilinx_vid_dma_mode - Video-aware DMA control register mode field
 * @XILINX_VID_DMA_DEFAULT: Default mode; no explicit mode bits set in the control register.
 * @XILINX_VID_DMA_AUTO_RESTART: Auto-restart by setting BIT(7) in the control register.
 *
 * Used by framebuffer DMA and AI layout formatter DMA clients that program
 * the same control-register encoding.
 */
enum xilinx_vid_dma_mode {
	XILINX_VID_DMA_DEFAULT = 0x0,
	XILINX_VID_DMA_AUTO_RESTART = BIT(7),
};

#endif /* __XILINX_VIDEO_DMA_COMMON_H__ */
