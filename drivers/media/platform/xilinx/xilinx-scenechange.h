/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Scene Change Detection driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Authors: Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>
 *          Satish Kumar Nagireddy <satish.nagireddy.nagireddy@xilinx.com>
 */

#ifndef _XILINX_SCENECHANGE_H_
#define _XILINX_SCENECHANGE_H_

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include "../../../dma/dmaengine.h"

/* Register/Descriptor Offsets */
#define XSCD_CTRL_OFFSET		0x000
#define XSCD_CTRL_AP_START		BIT(0)
#define XSCD_CTRL_AP_DONE		BIT(1)
#define XSCD_CTRL_AP_IDLE		BIT(2)
#define XSCD_CTRL_AP_READY		BIT(3)
#define XSCD_CTRL_AUTO_RESTART		BIT(7)

#define XSCD_GIE_OFFSET			0x004
#define XSCD_GIE_EN			BIT(0)

#define XSCD_IE_OFFSET			0x008
#define XSCD_IE_AP_DONE			BIT(0)
#define XSCD_IE_AP_READY		BIT(1)

#define XSCD_ISR_OFFSET			0x00c
#define XSCD_WIDTH_OFFSET		0x010
#define XSCD_HEIGHT_OFFSET		0x018
#define XSCD_STRIDE_OFFSET		0x020
#define XSCD_VID_FMT_OFFSET		0x028
#define XSCD_VID_FMT_RGB		0
#define XSCD_VID_FMT_YUV_444		1
#define XSCD_VID_FMT_YUV_422		2
#define XSCD_VID_FMT_YUV_420		3
#define XSCD_VID_FMT_Y8			24
#define XSCD_VID_FMT_Y10		25

#define XSCD_SUBSAMPLE_OFFSET		0x030
#define XSCD_SAD_OFFSET			0x038
#define XSCD_ADDR_OFFSET		0x040
#define XSCD_CHAN_OFFSET		0x100
#define XSCD_CHAN_EN_OFFSET		0x780

#define XSCD_MAX_CHANNELS		8

/****************************** PROTOTYPES ************************************/

/**
 * struct xscd_shared_data - Data to be shared among v4l subdev and DMA engine
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @dma_chan_list: List of DMA channels available
 * @active_streams: Number of active streams
 * @memory_based: Flag to identify memory based mode
 */
struct xscd_shared_data {
	void __iomem *iomem;
	struct xscd_dma_chan *dma_chan_list[XSCD_MAX_CHANNELS];
	u8 active_streams;
	bool memory_based;
};

/**
 * struct xscd_device - Xilinx Scene Change Detection device structure
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @numstreams: Number of streams in the design
 * @irq: Device IRQ
 * @dev: (OF) device
 * @rst_gpio: reset GPIO
 * @clk: video core clock
 * @dma_device: DMA device pointer
 * @shared_data: Data Shared across devices
 * @dma_node: DMA device node
 * @subdevs: subdev device instance
 */
struct xscd_device {
	void __iomem *iomem;
	int numstreams;
	int irq;
	struct device *dev;
	struct gpio_desc *rst_gpio;
	struct clk *clk;
	struct platform_device *dma_device;
	struct xscd_shared_data shared_data;
	struct device_node *dma_node;
	struct platform_device *subdevs[XSCD_MAX_CHANNELS];
};

/**
 * struct xscd_dma_desc - DMA channel
 * @luma_plane_addr: Luma plane buffer address
 * @vsize: width of the luma frame
 * @hsize: height of the luma frame
 * @stride: stride of the luma frame
 */
struct xscd_dma_desc {
	dma_addr_t luma_plane_addr;
	u32 vsize;
	u32 hsize;
	u32 stride;
};

/**
 * struct xscd_dma_tx_descriptor - Per Transaction structure
 * @async_tx: Async transaction descriptor
 * @sw: Software Descriptor
 * @node: Node in the channel descriptor list
 */
struct xscd_dma_tx_descriptor {
	struct dma_async_tx_descriptor async_tx;
	struct xscd_dma_desc sw;
	struct list_head node;
};

static inline struct xscd_dma_tx_descriptor *
to_xscd_dma_tx_descriptor(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct xscd_dma_tx_descriptor, async_tx);
}

/**
 * struct xscd_dma_chan - DMA Channel structure
 * @xdev: DMA engine driver specific device structure
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @lock: Descriptor operation lock
 * @chan_node: Member of a list of framebuffer channel instances
 * @pending_list: Descriptors waiting
 * @done_list: Complete descriptors
 * @staged_desc: Next buffer to be programmed
 * @active_desc: Currently active buffer being read/written to
 * @common: DMA common channel
 * @dev: The dma device
 * @idle: Channel idle state
 * @tasklet: Cleanup work after irq
 * @id: scene change channel ID
 * @en: Channel is enabled
 * @valid_interrupt: Valid interrupt for the channel
 */
struct xscd_dma_chan {
	struct xscd_dma_device *xdev;
	void __iomem *iomem;

	/* Descriptor operation Lock */
	spinlock_t lock;
	struct list_head chan_node;
	struct list_head pending_list;
	struct list_head done_list;
	struct xscd_dma_tx_descriptor *staged_desc;
	struct xscd_dma_tx_descriptor *active_desc;
	struct dma_chan common;
	struct device *dev;
	bool idle;
	struct tasklet_struct tasklet;
	u8 id;
	bool en;
	bool valid_interrupt;
};

static inline struct xscd_dma_chan *to_xscd_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct xscd_dma_chan, common);
}

/**
 * struct xscd_chan - Video Stream structure
 * @irq: device IRQ
 * @id: scene change channel ID
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @dev: (OF) device
 * @subdev: V4L2 subdevice
 * @pad: media pads
 * @format: active V4L2 media bus format for the pad
 * @event: scene change event
 * @dmachan: dma channel part of the scenechange stream
 * @lock: lock to protect active stream count variable
 */
struct xscd_chan {
	int irq;
	int id;
	void __iomem *iomem;
	struct device *dev;
	struct v4l2_subdev subdev;
	struct media_pad *pad;
	struct v4l2_mbus_framefmt format;
	struct v4l2_event event;
	struct xscd_dma_chan dmachan;

	/* Lock to protect active stream count */
	struct mutex lock;
};

static inline struct xscd_chan *to_xscd_chan(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xscd_chan, subdev);
}

/**
 * struct xscd_dma_device - Scene Change DMA device
 * @regs: I/O mapped base address
 * @dev: Device Structure
 * @common: DMA device structure
 * @chan: Driver specific DMA channel
 * @numchannels: Total number of channels
 * @memory_based: Memory based or streaming based
 */
struct xscd_dma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xscd_dma_chan **chan;
	u32 numchannels;
	u8 memory_based;
};

/*
 * Register related operations
 */
static inline u32 xscd_read(void __iomem *iomem, u32 addr)
{
	return ioread32(iomem + addr);
}

static inline void xscd_write(void __iomem *iomem, u32 addr, u32 value)
{
	iowrite32(value, iomem + addr);
}

static inline void xscd_clr(void __iomem *iomem, u32 addr, u32 clr)
{
	xscd_write(iomem, addr, xscd_read(iomem, addr) & ~clr);
}

static inline void xscd_set(void __iomem *iomem, u32 addr, u32 set)
{
	xscd_write(iomem, addr, xscd_read(iomem, addr) | set);
}

struct xscd_dma_tx_descriptor *
xscd_dma_alloc_tx_descriptor(struct xscd_dma_chan *chan);
void xscd_dma_start_transfer(struct xscd_dma_chan *chan);
void xscd_dma_start(struct xscd_dma_chan *chan);
void xscd_dma_chan_enable(struct xscd_dma_chan *chan, int chan_en);
void xscd_dma_reset(struct xscd_dma_chan *chan);
void xscd_dma_halt(struct xscd_dma_chan *chan);
void xscd_dma_free_desc_list(struct xscd_dma_chan *chan,
			     struct list_head *list);
void xscd_dma_free_descriptors(struct xscd_dma_chan *chan);
dma_cookie_t xscd_dma_tx_submit(struct dma_async_tx_descriptor *tx);
void xscd_dma_chan_desc_cleanup(struct xscd_dma_chan *chan);
void xscd_dma_chan_remove(struct xscd_dma_chan *chan);
#endif
