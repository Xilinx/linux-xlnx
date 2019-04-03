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

#include <linux/bitops.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <media/v4l2-subdev.h>

struct clk;
struct device;
struct device_node;
struct gpio_desc;

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

struct xscd_device;

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
 * @xscd: SCD device
 * @iomem: I/O memory address of the channel registers
 * @id: scene change channel ID
 * @common: DMA common channel
 * @tasklet: Cleanup work after irq
 * @lock: Protects pending_list, done_list, active_desc, enabled and running
 * @pending_list: Descriptors waiting
 * @done_list: Complete descriptors
 * @active_desc: Currently active buffer being read/written to
 * @enabled: Channel is enabled
 * @running: Channel is running
 * @wait: Wait queue to wait for the channel to stop
 */
struct xscd_dma_chan {
	struct xscd_device *xscd;
	void __iomem *iomem;
	unsigned int id;

	struct dma_chan common;
	struct tasklet_struct tasklet;

	spinlock_t lock;
	struct list_head pending_list;
	struct list_head done_list;
	struct xscd_dma_tx_descriptor *active_desc;
	unsigned int enabled;
	unsigned int running;
	wait_queue_head_t wait;
};

static inline struct xscd_dma_chan *to_xscd_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct xscd_dma_chan, common);
}

/**
 * struct xscd_chan - Video Stream structure
 * @id: scene change channel ID
 * @iomem: I/O memory address of the channel registers
 * @xscd: SCD device
 * @subdev: V4L2 subdevice
 * @pads: media pads
 * @format: active V4L2 media bus format for the pad
 * @event: scene change event
 * @dmachan: dma channel part of the scenechange stream
 * @lock: lock to protect active stream count variable
 */
struct xscd_chan {
	int id;
	void __iomem *iomem;
	struct xscd_device *xscd;
	struct v4l2_subdev subdev;
	struct media_pad pads[2];
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
 * struct xscd_device - Xilinx Scene Change Detection device structure
 * @dev: (OF) device
 * @iomem: device I/O register space remapped to kernel virtual memory
 * @rst_gpio: reset GPIO
 * @clk: video core clock
 * @irq: Device IRQ
 * @memory_based: Flag to identify memory based mode
 * @num_streams: Number of streams in the design
 * @chans: video stream instances
 * @dma_device: DMA device structure
 * @channels: DMA channels
 * @lock: Protects the running field
 * @running: True when the SCD core is running
 */
struct xscd_device {
	struct device *dev;
	void __iomem *iomem;
	struct gpio_desc *rst_gpio;
	struct clk *clk;
	int irq;

	u8 memory_based;
	int num_streams;

	struct xscd_chan *chans;

	struct dma_device dma_device;
	struct xscd_dma_chan *channels[XSCD_MAX_CHANNELS];

	/* This lock is to protect the running field */
	spinlock_t lock;
	u8 running;
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

void xscd_dma_enable_channel(struct xscd_dma_chan *chan, bool enable);
void xscd_dma_irq_handler(struct xscd_device *xscd);
int xscd_dma_init(struct xscd_device *xscd);
void xscd_dma_cleanup(struct xscd_device *xscd);

void xscd_chan_event_notify(struct xscd_chan *chan);
int xscd_chan_init(struct xscd_device *xscd, unsigned int chan_id,
		   struct device_node *node);
#endif
