// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Multimedia Integrated Display Controller DMA Engine Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma/xilinx_dpdma.h>
#include <linux/dmapool.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

#define MMI_DCDMA_NUM_CHAN		8

/* DCDMA registers */
#define MMI_DCDMA_WPROTS		0x0000
#define MMI_DCDMA_ISR			0x0050
#define MMI_DCDMA_IEN			0x0058
#define MMI_DCDMA_IDS			0x005c
#define MMI_DCDMA_MISC_ISR		0x0070
#define MMI_DCDMA_MISC_IEN		0x0078
#define MMI_DCDMA_MISC_IDS		0x007c
#define MMI_DCDMA_CH0_CH5_EISR		0x0090
#define MMI_DCDMA_CH0_CH5_EIEN		0x0098
#define MMI_DCDMA_CH0_CH5_EIDS		0x009c
#define MMI_DCDMA_CH6_CH7_EISR		0x00a4
#define MMI_DCDMA_CH6_CH7_EIEN		0x00ac
#define MMI_DCDMA_CH6_CH7_EIDS		0x00b0
#define MMI_DCDMA_BRDY_CNT_EISR		0x00c0
#define MMI_DCDMA_BRDY_CNT_EIEN		0x00c8
#define MMI_DCDMA_BRDY_CNT_EIDS		0x00cc
#define MMI_DCDMA_GBL			0x0104

#define MMI_DCDMA_IRQ_ALL		GENMASK(31, 0)
#define MMI_DCDMA_IRQ_VSYNC		GENMASK(3, 2)
#define MMI_DCDMA_RETRIGGER_SHIFT	8

/* Channel registers */
#define MMI_DCDMA_CH_BASE		0x0200
#define MMI_DCDMA_CH_OFFSET		0x0100
#define MMI_DCDMA_CH_DSCR_STRT_ADDRE	0x0000
#define MMI_DCDMA_CH_DSCR_STRT_ADDR	0x0004
#define MMI_DCDMA_CH_CNTL		0x0018
#define MMI_DCDMA_CH_STATUS		0x001c

#define MMI_DCDMA_CH_ENABLE		BIT(0)
#define MMI_DCDMA_CH_PAUSE		BIT(1)
#define MMI_DCDMA_ERR_DESC(ch)		BIT(3 * MMI_DCDMA_NUM_CHAN + (ch))
#define MMI_DCDMA_ERR_DATA_AXI(ch)	BIT(2 * MMI_DCDMA_NUM_CHAN + (ch))
#define MMI_DCDMA_NO_OSTAND_TRAN(ch)	BIT(1 * MMI_DCDMA_NUM_CHAN + (ch))
#define MMI_DCDMA_DESC_DONE(ch)		BIT(0 * MMI_DCDMA_NUM_CHAN + (ch))
#define MMI_DCDMA_ERR_RD_AXI_05(ch)	BIT(0 * 6 + (ch) % 6)
#define MMI_DCDMA_ERR_PRE_05(ch)	BIT(1 * 6 + (ch) % 6)
#define MMI_DCDMA_ERR_CRC_05(ch)	BIT(2 * 6 + (ch) % 6)
#define MMI_DCDMA_ERR_WR_AXI_05(ch)	BIT(3 * 6 + (ch) % 6)
#define MMI_DCDMA_ERR_DONE_05(ch)	BIT(4 * 6 + (ch) % 6)
#define MMI_DCDMA_ERR_RD_AXI_67(ch)	BIT(0 * 2 + (ch) % 6)
#define MMI_DCDMA_ERR_PRE_67(ch)	BIT(1 * 2 + (ch) % 6)
#define MMI_DCDMA_ERR_CRC_67(ch)	BIT(2 * 2 + (ch) % 6)
#define MMI_DCDMA_ERR_WR_AXI_67(ch)	BIT(3 * 2 + (ch) % 6)
#define MMI_DCDMA_ERR_DONE_67(ch)	BIT(4 * 2 + (ch) % 6)
#define MMI_DCDMA_ERR_OVERFLOW(ch)	BIT((ch))
#define MMI_DCDMA_STATUS_OTRAN_MASK	GENMASK(28, 20)
#define MMI_DCDMA_CH_VIDEO_GROUP	3
#define MMI_DCDMA_CH_STATUS_ERR_ALL(ch)	({ typeof(ch) __ch = (ch);	 \
					   MMI_DCDMA_ERR_DESC(__ch)	 |\
					   MMI_DCDMA_ERR_DATA_AXI(__ch); })
#define MMI_DCDMA_CH_05_ERR_ALL(ch)	({ typeof(ch) __ch = (ch);	 \
					   MMI_DCDMA_ERR_RD_AXI_05(__ch) |\
					   MMI_DCDMA_ERR_PRE_05(__ch)	 |\
					   MMI_DCDMA_ERR_CRC_05(__ch)	 |\
					   MMI_DCDMA_ERR_WR_AXI_05(__ch) |\
					   MMI_DCDMA_ERR_DONE_05(__ch); })
#define MMI_DCDMA_CH_67_ERR_ALL(ch)	({ typeof(ch) __ch = (ch);	 \
					   MMI_DCDMA_ERR_RD_AXI_67(__ch) |\
					   MMI_DCDMA_ERR_PRE_67(__ch)	 |\
					   MMI_DCDMA_ERR_CRC_67(__ch)	 |\
					   MMI_DCDMA_ERR_WR_AXI_67(__ch) |\
					   MMI_DCDMA_ERR_DONE_67(__ch); })
#define MMI_DCDMA_CH_PER_IRQ_REG_05	6

/* DCDMA descriptor fields */
#define MMI_DCDMA_ALIGN_BYTES		256
#define MMI_DCDMA_LINESIZE_ALIGN_BITS	128
#define MMI_DCDMA_DESC_CTRL_PREAMBLE	0xa5

/**
 * struct mmi_dcdma_desc_ctrl - DCDMA hardware descriptor control
 * @preamble: descriptor preamble, predefined value of 0xa5
 * @update_en: enable descriptor timestamp and status update
 * @ignore_done: ignore the done status when processing the descriptor
 * @last_descriptor: the current descriptor is the last one in the chain
 * @last_descriptor_frame: the last descriptor of a frame
 * @crc_en: enable CRC check
 * @axi_burst: AXI burst type; 0: incremental, 1: fixed (should be 0)
 * @axi_cache: descriptor read/write cache bits from APB register
 * @axi_prot: descriptor protect bits from APB register
 * @axi_awcache: cache bits for data write
 * @axi_awqos: QoS bits for data write
 * @reserved: reserved bits [31..28]
 */
struct mmi_dcdma_desc_ctrl {
	u32 preamble			: 8;
	u32 update_en			: 1;
	u32 ignore_done			: 1;
	u32 last_descriptor		: 1;
	u32 last_descriptor_frame	: 1;
	u32 crc_en			: 1;
	u32 axi_burst			: 1;
	u32 axi_cache			: 4;
	u32 axi_prot			: 2;
	u32 axi_awcache			: 4;
	u32 axi_awqos			: 4;
	u32 reserved			: 4;
} __packed;

/**
 * struct mmi_dcdma_hw_desc - DCDMA hardware descriptor
 * @desc_id: descriptor identifier, DMA cookie
 * @ctrl: descriptor control
 * @data_size: number of bytes to be fetched
 * @src_addr: start address of the payload
 * @next_desc: next in the chain descriptor address
 * @tlb_prefetch_en: enable TLB prefetch
 * @tlb_prefetch_blk_size: TLB prefetch address block size, 16-byte resolution
 * @tlb_prefetch_blk_offset: TLB prefetch address block offset, 16-byte
 *                           resolution; should be less than
 *                           @tlb_prefetch_blk_size
 * @line_or_tile: data layout; 0: scan lines, 1: tiles
 * @line_size: number of bytes per line
 * @line_stride: line stride in 16-byte resolution; should be greater or equal
 *               than @line_size, has to be integer multiple of 16 * AXI burst
 *               length
 * @tile_type: tile type; 0 for 32x4, 1 for 64x4
 * @tile_pitch: address offset between two rows of tiles in 32-byte units
 * @target_addr: target address; 0 for DP(SDP), 1 for cursor RAM (channel 7)
 * @irq_en: enable done interrupt upon DMA transfer completion
 * @reserved0: reserved bit 7 @0x1f
 * @presentation_ts: presentation timestamp; DC writes back into this field
 * @reserved1: reserved 4-byte pad @0x28
 * @checksum: 32-bit CRC checksum
 */
struct mmi_dcdma_hw_desc {
	u32 desc_id			: 16;
	struct mmi_dcdma_desc_ctrl ctrl;
	u32 data_size			: 32;
	u64 src_addr			: 48;
	u64 next_desc			: 48;
	u32 tlb_prefetch_en		:  1;
	u32 tlb_prefetch_blk_size	: 14;
	u32 tlb_prefetch_blk_offset	: 14;
	u32 line_or_tile		:  1;
	u32 line_size			: 18;
	u32 line_stride			: 14;
	u32 tile_type			:  1;
	u32 tile_pitch			: 14;
	u32 target_addr			:  1;
	u32 irq_en			:  1;
	u32 reserved0			:  1;
	u64 presentation_ts		: 64;
	u32 reserved1			: 32;
	u32 checksum			: 32;
} __aligned(MMI_DCDMA_ALIGN_BYTES) __packed;

/**
 * struct mmi_dcdma_sw_desc - DCDMA software descriptor
 * @hw: hardware descriptor
 * @vdesc: virtual DMA descriptor
 * @dma_addr: descriptor DMA address
 * @dma_pool: DMA pool this descriptor allocated from
 * @error: error reported by hardware while running this descriptor
 */
struct mmi_dcdma_sw_desc {
	struct mmi_dcdma_hw_desc hw;
	struct virt_dma_desc vdesc;
	dma_addr_t dma_addr;
	struct dma_pool *dma_pool;
	u32 error;
};

struct mmi_dcdma_device;

/**
 * struct mmi_dcdma_chan - DCDMA channel
 * @vchan: virtual DMA channel
 * @reg: channel registers base address
 * @id: channel id [0..7]
 * @desc_pool: descriptor allocation pool
 * @mdev: DCDMA device
 * @active_desc: descriptor currently running by the hardware
 * @wait_to_stop: queue to wait for outstanding transactions before the stop
 * @video_group: flag if multi-channel operations are requested for a video
 */
struct mmi_dcdma_chan {
	struct virt_dma_chan vchan;
	void __iomem *reg;
	unsigned int id;
	struct dma_pool *desc_pool;
	struct mmi_dcdma_device *mdev;

	struct mmi_dcdma_sw_desc *active_desc;
	wait_queue_head_t wait_to_stop;
	bool video_group;
};

/**
 * struct mmi_dcdma_device - DCDMA device
 * @base: generic DMA device
 * @reg: device registers base address
 * @irq: device assigned interrupt number
 * @axi_clk: AXI clock
 * @chan: DMA channels
 */
struct mmi_dcdma_device {
	struct dma_device base;
	void __iomem *reg;
	int irq;
	struct clk *axi_clk;
	struct mmi_dcdma_chan chan[MMI_DCDMA_NUM_CHAN];
};

/**
 * enum mmi_dcdma_error - DCDMA transfer errors
 * @DCDMA_ERR_NONE: no error registered
 * @DCDMA_ERR_DESC: descriptor error
 * @DCDMA_ERR_DATA_AXI: AXI data error
 * @DCDMA_ERR_RD_AXI: AXI read error
 * @DCDMA_ERR_PRE: preamble mismatch error
 * @DCDMA_ERR_CRC: CRC mismatch error
 * @DCDMA_ERR_WR_AXI: AXI write error
 * @DCDMA_ERR_DONE: already processed descriptor error
 * @DCDMA_ERR_OVERFLOW: channel overflow error
 */
enum mmi_dcdma_error {
	DCDMA_ERR_NONE		= 0,
	DCDMA_ERR_DESC		= BIT(0),
	DCDMA_ERR_DATA_AXI	= BIT(1),
	DCDMA_ERR_RD_AXI	= BIT(2),
	DCDMA_ERR_PRE		= BIT(3),
	DCDMA_ERR_CRC		= BIT(4),
	DCDMA_ERR_WR_AXI	= BIT(5),
	DCDMA_ERR_DONE		= BIT(6),
	DCDMA_ERR_OVERFLOW	= BIT(7),
};

/* DCDMA Registers Accessors */

/**
 * dcdma_read - Read dcdma register
 * @base: base register address
 * @offset: register offset
 *
 * Return: value stored in the hardware register
 */
static inline u32 dcdma_read(void __iomem *base, u32 offset)
{
	return ioread32(base + offset);
}

/**
 * dcdma_write - Write the value into dcdma register
 * @base: base register address
 * @offset: register offset
 * @val: value to write
 */
static inline void dcdma_write(void __iomem *base, u32 offset, u32 val)
{
	iowrite32(val, base + offset);
}

/**
 * dcdma_clr - Clear bits in dcdma register
 * @base: base register address
 * @offset: register offset
 * @clr: bits to clear
 */
static inline void dcdma_clr(void __iomem *base, u32 offset, u32 clr)
{
	dcdma_write(base, offset, dcdma_read(base, offset) & ~clr);
}

/**
 * dcdma_set - Set bits in dcdma register
 * @base: base register address
 * @offset: register offset
 * @set: bits to set
 */
static inline void dcdma_set(void __iomem *base, u32 offset, u32 set)
{
	dcdma_write(base, offset, dcdma_read(base, offset) | set);
}

/* DCDMA Descriptors */

/**
 * mmi_dcdma_chan_alloc_sw_desc - Allocate software descriptor
 * @chan: DMA channel
 *
 * Allocate software descriptor from the channel DMA pool.
 *
 * Return: allocated software descriptor or NULL
 */
static struct mmi_dcdma_sw_desc *
mmi_dcdma_chan_alloc_sw_desc(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_sw_desc *desc;
	dma_addr_t dma_addr;

	desc = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &dma_addr);
	if (!desc)
		return NULL;

	desc->dma_addr = dma_addr;
	desc->dma_pool = chan->desc_pool;

	return desc;
}

/**
 * mmi_dcdma_free_sw_desc - Free software descriptor
 * @desc: sw descriptor to free
 *
 * Free previously allocated software descriptor.
 */
static void mmi_dcdma_free_sw_desc(struct mmi_dcdma_sw_desc *desc)
{
	dma_pool_free(desc->dma_pool, desc, desc->dma_addr);
}

/**
 * mmi_dcdma_sw_desc_set_dma_addr - Set DMA addresses in the descriptor
 * @desc: current DMA descriptor
 * @prev: previous DMA descriptor, could be the same as @desc
 * @dma_addr: payload DMA address to set
 *
 * Set source DMA address in the current descriptor. Link previous descriptor
 * to the current one if @prev has been provided.
 */
static void mmi_dcdma_sw_desc_set_dma_addr(struct mmi_dcdma_sw_desc *desc,
					   struct mmi_dcdma_sw_desc *prev,
					   dma_addr_t dma_addr)
{
	struct mmi_dcdma_hw_desc *hw_desc = &desc->hw, *hw_prev;

	hw_desc->src_addr = dma_addr;

	if (prev) {
		hw_prev = &prev->hw;
		hw_prev->next_desc = desc->dma_addr;
	}
}

/**
 * mmi_dcdma_chan_prep_interleaved_dma - Prepare an interleaved DMA descriptor
 * @chan: DMA channel
 * @xt: interleaved DMA transfer template
 *
 * Prepare DCDMA descriptor for an interleaved DMA transfer.
 *
 * Return: Prepared DCDMA software descriptor on success or NULL otherwise.
 */
static struct mmi_dcdma_sw_desc *
mmi_dcdma_chan_prep_interleaved_dma(struct mmi_dcdma_chan *chan,
				    struct dma_interleaved_template *xt)
{
	struct mmi_dcdma_sw_desc *sw_desc;
	struct mmi_dcdma_hw_desc *hw_desc;
	size_t line_size, stride, data_size;

	if (!IS_ALIGNED(xt->src_start, MMI_DCDMA_ALIGN_BYTES)) {
		dev_err(chan->mdev->base.dev,
			"chan%u: buffer should be aligned at %d B\n",
			chan->id, MMI_DCDMA_ALIGN_BYTES);
		return NULL;
	}

	sw_desc = mmi_dcdma_chan_alloc_sw_desc(chan);
	if (!sw_desc)
		return NULL;

	mmi_dcdma_sw_desc_set_dma_addr(sw_desc, sw_desc, xt->src_start);

	hw_desc = &sw_desc->hw;
	line_size = ALIGN(xt->sgl[0].size, MMI_DCDMA_LINESIZE_ALIGN_BITS >> 3);
	if (line_size != xt->sgl[0].size)
		dev_warn(chan->mdev->base.dev,
			 "chan%u: line size not aligned: %zd != %zu\n",
			 chan->id, xt->sgl[0].size, line_size);
	stride = (line_size + xt->sgl[0].icg);
	data_size = line_size * xt->numf;

	hw_desc->ctrl.preamble = MMI_DCDMA_DESC_CTRL_PREAMBLE;
	hw_desc->ctrl.update_en = 0; /* set 1 to receive PTS */
	hw_desc->ctrl.ignore_done = 1;
	hw_desc->ctrl.last_descriptor = 0;
	hw_desc->ctrl.last_descriptor_frame = 1;
	hw_desc->data_size = data_size;
	hw_desc->line_or_tile = 0;
	hw_desc->line_size = line_size;
	hw_desc->line_stride = stride >> 4; /* 16 bytes blocks */
	hw_desc->irq_en = 0;

	return sw_desc;
}

/**
 * to_dcdma_sw_desc - Convert virtual descriptor to DCDMA software descriptor
 * @vdesc: virtual DMA descriptor
 *
 * Return: Corresponding DCDMA software descriptor.
 */
static inline struct mmi_dcdma_sw_desc *
to_dcdma_sw_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct mmi_dcdma_sw_desc, vdesc);
}

/**
 * mmi_dcdma_free_virt_desc - Free virtual DMA descriptor
 * @vdesc: virtual DMA descriptor
 */
static void mmi_dcdma_free_virt_desc(struct virt_dma_desc *vdesc)
{
	struct mmi_dcdma_sw_desc *desc;

	if (!vdesc)
		return;

	desc = to_dcdma_sw_desc(vdesc);
	mmi_dcdma_free_sw_desc(desc);
}

/**
 * mmi_dcdma_dump_desc - Dump DCDMA descriptor content
 * @chan: DCDMA channel
 * @desc: DCDMA software descriptor to dump
 */
static void mmi_dcdma_dump_desc(struct mmi_dcdma_chan *chan,
				struct mmi_dcdma_sw_desc *desc)
{
	struct mmi_dcdma_hw_desc *hw_desc = &desc->hw;

	dev_err(chan->mdev->base.dev,
		"chan%u: desc %llx: buf %llx, sz %d, ln %d, strd %d, err %x\n",
		chan->id, (u64)desc->dma_addr, (u64)hw_desc->src_addr,
		hw_desc->data_size, hw_desc->line_size, hw_desc->line_stride,
		desc->error);
}

/* DMA DMA Channel IRQ Handling */

/**
 * mmi_dcdma_chan_video_group_start - Get video group start DMA channel id
 * @chan: DCDMA channel
 *
 * Return: First channel id of the video group, the given channel belongs to.
 */
static unsigned int
mmi_dcdma_chan_video_group_start(struct mmi_dcdma_chan *chan)
{
	return (chan->id / MMI_DCDMA_CH_VIDEO_GROUP)
			 * MMI_DCDMA_CH_VIDEO_GROUP;
}

/**
 * mmi_dcdma_chan_video_group_end - Get video group end DMA channel id
 * @chan: DCDMA channel
 *
 * Return: Next after the last channel id of the video group, the given channel
 * belongs to.
 */
static unsigned int
mmi_dcdma_chan_video_group_end(struct mmi_dcdma_chan *chan)
{
	return mmi_dcdma_chan_video_group_start(chan) +
		MMI_DCDMA_CH_VIDEO_GROUP;
}

/**
 * mmi_dcdma_chan_video_group_first - Get the first channel of the video group
 * @chan: DCDMA channel
 *
 * Return: The first channel of the video group, the given channel belongs to.
 */
static struct mmi_dcdma_chan *
mmi_dcdma_chan_video_group_first(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;

	return &mdev->chan[mmi_dcdma_chan_video_group_start(chan)];
}

static void mmi_dcdma_chan_start_transfer(struct mmi_dcdma_chan *chan);

/**
 * mmi_dcdma_chan_enable_error_irq - Enable all error irq
 * @chan: DCDMA channel
 *
 * Enable all error irq associated with the given channel.
 */
static void mmi_dcdma_chan_enable_error_irq(struct mmi_dcdma_chan *chan)
{
	dcdma_write(chan->mdev->reg, MMI_DCDMA_IEN,
		    MMI_DCDMA_CH_STATUS_ERR_ALL(chan->id));
	if (chan->id < 2 * MMI_DCDMA_CH_VIDEO_GROUP)
		dcdma_write(chan->mdev->reg, MMI_DCDMA_CH0_CH5_EIEN,
			    MMI_DCDMA_CH_05_ERR_ALL(chan->id));
	else
		dcdma_write(chan->mdev->reg, MMI_DCDMA_CH6_CH7_EIEN,
			    MMI_DCDMA_CH_67_ERR_ALL(chan->id));
	dcdma_write(chan->mdev->reg, MMI_DCDMA_BRDY_CNT_EIEN,
		    MMI_DCDMA_ERR_OVERFLOW(chan->id));
}

/**
 * mmi_dcdma_chan_disable_error_irq - Disable all error irq
 * @chan: DCDMA channel
 *
 * Disable all error irq associated with the given channel.
 */
static void mmi_dcdma_chan_disable_error_irq(struct mmi_dcdma_chan *chan)
{
	dcdma_write(chan->mdev->reg, MMI_DCDMA_IDS,
		    MMI_DCDMA_CH_STATUS_ERR_ALL(chan->id));
	if (chan->id < 2 * MMI_DCDMA_CH_VIDEO_GROUP)
		dcdma_write(chan->mdev->reg, MMI_DCDMA_CH0_CH5_EIDS,
			    MMI_DCDMA_CH_05_ERR_ALL(chan->id));
	else
		dcdma_write(chan->mdev->reg, MMI_DCDMA_CH6_CH7_EIDS,
			    MMI_DCDMA_CH_67_ERR_ALL(chan->id));
	dcdma_write(chan->mdev->reg, MMI_DCDMA_BRDY_CNT_EIDS,
		    MMI_DCDMA_ERR_OVERFLOW(chan->id));
}

/**
 * mmi_dcdma_chan_handle_error - Handle channel error(s)
 * @chan: DCDMA channel
 * @error: combined errors reported against the channel
 */
static void mmi_dcdma_chan_handle_error(struct mmi_dcdma_chan *chan, u32 error)
{
	struct mmi_dcdma_sw_desc *active;
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	active = chan->active_desc;
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	if (!active)
		return;

	if (error & ~active->error) {
		active->error |= error;
		mmi_dcdma_dump_desc(chan, active);
	}
}

/**
 * mmi_dcdma_chan_handle_done - Handle done interrupt
 * @chan: DCDMA channel
 */
static void mmi_dcdma_chan_handle_done(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;

	dev_err(mdev->base.dev, "chan%u: done reported\n", chan->id);
}

/**
 * mmi_dcdma_chan_handle_no_ostand - Handle no outstanding interrupt
 * @chan: DCDMA channel
 *
 * Handle no outstanding transactions interrupt. Here we should wake up the
 * thread waiting to stop the channel.
 */
static void mmi_dcdma_chan_handle_no_ostand(struct mmi_dcdma_chan *chan)
{
	wait_queue_head_t *wait_to_stop = &chan->wait_to_stop;

	if (chan->video_group)
		wait_to_stop = &mmi_dcdma_chan_video_group_first(chan)->wait_to_stop;

	wake_up(wait_to_stop);
}

/**
 * mmi_dcdma_chan_handle_vsync - Handle VSYNC interrupt
 * @chan: DCDMA channel
 *
 * Handle vertical sync interrupt. Here we should check if we have a pending
 * DMA request and start it.
 */
static void mmi_dcdma_chan_handle_vsync(struct mmi_dcdma_chan *chan)
{
	struct virt_dma_desc *pending;
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	pending = vchan_next_desc(&chan->vchan);
	if (pending) {
		if (chan->active_desc) {
			vchan_cookie_complete(&chan->active_desc->vdesc);
			chan->active_desc = NULL;
		}
		mmi_dcdma_chan_start_transfer(chan);
	}
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

/* DCDMA Channels */

/**
 * mmi_dcdma_chan_init - Initialize DMA channel
 * @mdev: DCDMA device
 * @id: index of a channel to initialize
 */
static void mmi_dcdma_chan_init(struct mmi_dcdma_device *mdev, unsigned int id)
{
	struct mmi_dcdma_chan *chan = &mdev->chan[id];

	chan->mdev = mdev;
	chan->id = id;
	chan->reg = mdev->reg + MMI_DCDMA_CH_BASE + MMI_DCDMA_CH_OFFSET * id;

	chan->vchan.desc_free = mmi_dcdma_free_virt_desc;
	vchan_init(&chan->vchan, &chan->mdev->base);

	init_waitqueue_head(&chan->wait_to_stop);
}

/**
 * mmi_dcdma_chan_remove - Remove DMA channel
 * @chan: DCDMA channel to remove
 */
static void mmi_dcdma_chan_remove(struct mmi_dcdma_chan *chan)
{
	dcdma_write(chan->reg, MMI_DCDMA_CH_CNTL, 0);
}

/**
 * mmi_dcdma_chan_pause - Pause DMA channel
 * @chan: DCDMA channel to pause
 *
 * Pause given channel transfers. The paused channel should report the moment
 * when no outstanding transactions remain.
 */
static void mmi_dcdma_chan_pause(struct mmi_dcdma_chan *chan)
{
	dcdma_set(chan->reg, MMI_DCDMA_CH_CNTL, MMI_DCDMA_CH_PAUSE);
}

/**
 * mmi_dcdma_chan_resume - Resume DMA channel
 * @chan: DCDMA channel to resume
 *
 * Resume given channel normal operations.
 */
static void mmi_dcdma_chan_resume(struct mmi_dcdma_chan *chan)
{
	dcdma_clr(chan->reg, MMI_DCDMA_CH_CNTL, MMI_DCDMA_CH_PAUSE);
}

/**
 * mmi_dcdma_chan_enable - Enable DMA channel
 * @chan: DCDMA channel to enable
 *
 * Enable the channel. Get ready to start the DMA transfer.
 */
static void mmi_dcdma_chan_enable(struct mmi_dcdma_chan *chan)
{
	dcdma_set(chan->reg, MMI_DCDMA_CH_CNTL, MMI_DCDMA_CH_ENABLE);
}

/**
 * mmi_dcdma_chan_disable - Disable DMA channel
 * @chan: DCDMA channel to disable
 */
static void mmi_dcdma_chan_disable(struct mmi_dcdma_chan *chan)
{
	dcdma_clr(chan->reg, MMI_DCDMA_CH_CNTL, MMI_DCDMA_CH_ENABLE);
}

/**
 * mmi_dcdma_chan_enabled - Check if the current DMA channel is enabled
 * @chan: DCDMA channel to check
 *
 * Return: true if this channel is enabled, or false otherwise
 */
static bool mmi_dcdma_chan_enabled(struct mmi_dcdma_chan *chan)
{
	return !!(dcdma_read(chan->reg, MMI_DCDMA_CH_CNTL) &
		  MMI_DCDMA_CH_ENABLE);
}

/**
 * mmi_dcdma_chan_done - Check if the current DMA channel is done
 * @chan: DCDMA channel to check
 *
 * Return: true if the given channel has no outstanding transactions or false
 * otherwise.
 */
static bool mmi_dcdma_chan_done(struct mmi_dcdma_chan *chan)
{
	return 0 == FIELD_GET(MMI_DCDMA_STATUS_OTRAN_MASK,
			      dcdma_read(chan->reg, MMI_DCDMA_CH_STATUS));
}

/**
 * mmi_dcdma_chan_video_group_done - Check if all channels in the video group
 * are done
 * @chan: DCDMA channel within the video group
 *
 * Return: true if all channels in the video group finished all transactions or
 * false otherwise.
 */
static bool mmi_dcdma_chan_video_group_done(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;
	unsigned int ch;

	for (ch = mmi_dcdma_chan_video_group_start(chan);
	     ch < mmi_dcdma_chan_video_group_end(chan); ++ch) {
		struct mmi_dcdma_chan *video_chan = &mdev->chan[ch];

		if (video_chan->video_group &&
		    !mmi_dcdma_chan_done(video_chan))
			return false;
	}

	return true;
}

/**
 * mmi_dcdma_chan_pause_video_group - Pause the video group
 * @chan: DCDMA channel from the video group
 *
 * Pause all active channels in the video group.
 */
static void mmi_dcdma_chan_pause_video_group(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;
	unsigned int ch;

	for (ch = mmi_dcdma_chan_video_group_start(chan);
	     ch < mmi_dcdma_chan_video_group_end(chan); ++ch) {
		struct mmi_dcdma_chan *video_chan = &mdev->chan[ch];

		if (video_chan->video_group) {
			mmi_dcdma_chan_disable_error_irq(video_chan);
			mmi_dcdma_chan_pause(video_chan);
		}
	}
}

/**
 * mmi_dcdma_chan_video_group_ready - Check if the video group is ready
 * @chan: DCDMA channel from the video group
 *
 * Return: The bitset of all channel ids in the given video group, or 0 if some
 * channels in the video group aren't ready yet.
 */
static u32 mmi_dcdma_chan_video_group_ready(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;
	u32 channels = 0;
	unsigned int ch;

	for (ch = mmi_dcdma_chan_video_group_start(chan);
	     ch < mmi_dcdma_chan_video_group_end(chan); ++ch) {
		struct mmi_dcdma_chan *video_chan = &mdev->chan[ch];

		if (video_chan->video_group &&
		    !mmi_dcdma_chan_enabled(video_chan))
			return 0;
		if (video_chan->video_group)
			channels |= BIT(ch);
	}

	return channels;
}

/**
 * mmi_dcdma_chan_start_transfer - Start channel transfer
 * @chan: DCDMA channel to start the transfer on
 *
 * Peek the next pending descriptor from the queue and commit it to the
 * hardware.
 */
static void mmi_dcdma_chan_start_transfer(struct mmi_dcdma_chan *chan)
{
	bool first_frame = false;
	struct virt_dma_desc *vdesc;
	struct mmi_dcdma_sw_desc *desc;
	u32 trigger;

	lockdep_assert_held(&chan->vchan.lock);

	if (chan->active_desc)
		return;

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc)
		return;

	list_del(&vdesc->node);

	if (!mmi_dcdma_chan_enabled(chan)) {
		mmi_dcdma_chan_enable(chan);
		first_frame = true;
	}

	desc = to_dcdma_sw_desc(vdesc);
	chan->active_desc = desc;

	desc->hw.desc_id = desc->vdesc.tx.cookie;

	dcdma_write(chan->reg, MMI_DCDMA_CH_DSCR_STRT_ADDR,
		    lower_32_bits(desc->dma_addr));
	dcdma_write(chan->reg, MMI_DCDMA_CH_DSCR_STRT_ADDRE,
		    upper_32_bits(desc->dma_addr));

	trigger = chan->video_group ? mmi_dcdma_chan_video_group_ready(chan)
				    : BIT(chan->id);
	if (!trigger)
		return;

	if (!first_frame)
		trigger <<= MMI_DCDMA_RETRIGGER_SHIFT;

	dcdma_write(chan->mdev->reg, MMI_DCDMA_GBL, trigger);
}

/**
 * mmi_dcdma_chan_stop_video_group - Stop the video group
 * @chan: DCDMA channel from the video group
 *
 * Wait until all channels in the video group are done and disable them.
 */
static void mmi_dcdma_chan_stop_video_group(struct mmi_dcdma_chan *chan)
{
	struct mmi_dcdma_device *mdev = chan->mdev;
	unsigned int ch;
	int ret;

	ret = wait_event_timeout
		(mmi_dcdma_chan_video_group_first(chan)->wait_to_stop,
		 mmi_dcdma_chan_video_group_done(chan), msecs_to_jiffies(50));
	if (ret <= 0)
		dev_warn(chan->mdev->base.dev,
			 "chan%u: video group not ready to stop: %d\n",
			 chan->id, ret);

	for (ch = mmi_dcdma_chan_video_group_start(chan);
	     ch < mmi_dcdma_chan_video_group_end(chan); ++ch) {
		struct mmi_dcdma_chan *video_chan = &mdev->chan[ch];

		mmi_dcdma_chan_disable(video_chan);
		mmi_dcdma_chan_resume(video_chan);
		mmi_dcdma_chan_enable_error_irq(video_chan);
	}
}

/**
 * mmi_dcdma_chan_stop - Stop the channel
 * @chan: DCDMA channel to stop
 *
 * Wait until the channel is done and disable it.
 */
static void mmi_dcdma_chan_stop(struct mmi_dcdma_chan *chan)
{
	int ret;

	ret = wait_event_timeout(chan->wait_to_stop, mmi_dcdma_chan_done(chan),
				 msecs_to_jiffies(50));
	if (ret <= 0)
		dev_warn(chan->mdev->base.dev,
			 "chan%u: not ready to stop: %d\n", chan->id, ret);

	mmi_dcdma_chan_disable(chan);
	mmi_dcdma_chan_resume(chan);
	mmi_dcdma_chan_enable_error_irq(chan);
}

/* DMA Engine Interface */

/**
 * to_dcdma_chan - Convert to DCDMA channel
 * @dchan: generic DMA channel to convert
 *
 * Return: the DCDMA channel corresponding to the given generic DMA channel.
 */
static inline struct mmi_dcdma_chan *to_dcdma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct mmi_dcdma_chan, vchan.chan);
}

/**
 * of_mmi_dcdma_xlate - Discover the DMA channel from the OF info
 * @dma_args: OF phandle arguments
 * @ofdma: OF DMA device
 *
 * Return: generic DMA channel corresponding to the given OF info or NULL if the
 * channel cannot be found.
 */
static struct dma_chan *of_mmi_dcdma_xlate(struct of_phandle_args *dma_args,
					   struct of_dma *ofdma)
{
	struct mmi_dcdma_device *mdev = ofdma->of_dma_data;
	u32 chan_id = dma_args->args[0];

	if (chan_id >= ARRAY_SIZE(mdev->chan))
		return NULL;

	return dma_get_slave_channel(&mdev->chan[chan_id].vchan.chan);
}

/**
 * mmi_dcdma_alloc_chan_resources - Allocated DMA channel resources
 * @dchan: generic DMA channel
 *
 * Allocate resources required for the DCDMA channel to operate.
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dcdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);
	struct mmi_dcdma_device *mdev = chan->mdev;
	size_t align = __alignof__(struct mmi_dcdma_sw_desc);

	chan->desc_pool = dma_pool_create(dev_name(mdev->base.dev),
					  mdev->base.dev,
					  sizeof(struct mmi_dcdma_sw_desc),
					  align, 0);

	if (!chan->desc_pool) {
		dev_err(mdev->base.dev,
			"chan%u: failed to allocate descriptor pool",
			chan->id);
		return -ENOMEM;
	}

	return 0;
}

/**
 * mmi_dcdma_free_chan_resources - Free DMA channel resources
 * @dchan: generic DMA channel
 *
 * Free all allocated previously channel resources.
 */
static void mmi_dcdma_free_chan_resources(struct dma_chan *dchan)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);

	vchan_free_chan_resources(&chan->vchan);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

/**
 * mmi_dcdma_prep_interleaved_dma - Prepare interleaved DMA transfer
 * @dchan: generic DMA channel
 * @xt: interleaved DMA transfer template
 * @flags: transfer flags
 *
 * Return: Allocated async DMA descriptor on success or NULL otherwise.
 */
static struct dma_async_tx_descriptor *
mmi_dcdma_prep_interleaved_dma(struct dma_chan *dchan,
			       struct dma_interleaved_template *xt,
			       unsigned long flags)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);
	struct mmi_dcdma_sw_desc *desc;

	if (xt->dir != DMA_MEM_TO_DEV)
		return NULL;

	if (!xt->numf || !xt->sgl[0].size)
		return NULL;

	if (!(flags & DMA_PREP_REPEAT) || !(flags & DMA_PREP_LOAD_EOT))
		return NULL;

	desc = mmi_dcdma_chan_prep_interleaved_dma(chan, xt);
	if (!desc)
		return NULL;

	vchan_tx_prep(&chan->vchan, &desc->vdesc, flags | DMA_CTRL_ACK);

	return &desc->vdesc.tx;
}

/**
 * mmi_dcdma_issue_pending - Issue pending transfer
 * @dchan: generic DMA channel
 *
 * Peek the next pending descriptor from the queue and issue it to the
 * hardware.
 */
static void mmi_dcdma_issue_pending(struct dma_chan *dchan)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (vchan_issue_pending(&chan->vchan))
		mmi_dcdma_chan_start_transfer(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

/**
 * mmi_dcdma_config - Configure DMA channel
 * @dchan: generic DMA channel
 * @config: channel config
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dcdma_config(struct dma_chan *dchan,
			    struct dma_slave_config *config)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);
	struct xilinx_dpdma_peripheral_config *pconfig;
	unsigned long flags;

	pconfig = config->peripheral_config;
	if (WARN_ON(pconfig && config->peripheral_size != sizeof(*pconfig)))
		return -EINVAL;

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (pconfig)
		chan->video_group = pconfig->video_group;
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	return 0;
}

/**
 * mmi_dcdma_terminate_all - Terminate DMA channel
 * @dchan: generic DMA channel
 *
 * Terminate all current channel transactions.
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dcdma_terminate_all(struct dma_chan *dchan)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);

	if (chan->video_group) {
		mmi_dcdma_chan_pause_video_group(chan);
	} else {
		mmi_dcdma_chan_disable_error_irq(chan);
		mmi_dcdma_chan_pause(chan);
	}

	return 0;
}

/**
 * mmi_dcdma_synchronize - Synchronize DMA channel termination
 * @dchan: generic DMA channel
 *
 * Wait until the given channel is done, stop it. Clean up all channel
 * descriptors.
 */
static void mmi_dcdma_synchronize(struct dma_chan *dchan)
{
	struct mmi_dcdma_chan *chan = to_dcdma_chan(dchan);
	unsigned long flags;
	LIST_HEAD(descriptors);

	if (chan->video_group)
		mmi_dcdma_chan_stop_video_group(chan);
	else
		mmi_dcdma_chan_stop(chan);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (chan->active_desc) {
		vchan_terminate_vdesc(&chan->active_desc->vdesc);
		chan->active_desc = NULL;
	}
	chan->video_group = false;
	vchan_get_all_descriptors(&chan->vchan, &descriptors);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &descriptors);
}

/* DCDMA IRQ Handling */

/**
 * mmi_dcdma_enable_irq - Enable all DCDMA IRQ
 * @mdev: DCDMA device
 */
static void mmi_dcdma_enable_irq(struct mmi_dcdma_device *mdev)
{
	dcdma_write(mdev->reg, MMI_DCDMA_IEN, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_MISC_IEN, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH0_CH5_EIEN, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH6_CH7_EIEN, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_BRDY_CNT_EIEN, MMI_DCDMA_IRQ_ALL);
}

/**
 * mmi_dcdma_disable_irq - Disable all DCDMA IRQ
 * @mdev: DCDMA device
 */
static void mmi_dcdma_disable_irq(struct mmi_dcdma_device *mdev)
{
	dcdma_write(mdev->reg, MMI_DCDMA_IDS, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_MISC_IDS, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH0_CH5_EIDS, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH6_CH7_EIDS, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_BRDY_CNT_EIDS, MMI_DCDMA_IRQ_ALL);
}

/**
 * mmi_dcdma_clear_irq - Clear all IRQ status registers
 * @mdev: DCDMA device
 */
static void mmi_dcdma_clear_irq(struct mmi_dcdma_device *mdev)
{
	dcdma_write(mdev->reg, MMI_DCDMA_ISR, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_MISC_ISR, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH0_CH5_EISR, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_CH6_CH7_EISR, MMI_DCDMA_IRQ_ALL);
	dcdma_write(mdev->reg, MMI_DCDMA_BRDY_CNT_EISR, MMI_DCDMA_IRQ_ALL);
}

/**
 * mmi_dcdma_irq_handler - DCDMA irq handler
 * @irq: IRQ number
 * @data: data pointer (struct mmi_dcdma_device *) associated with this handler
 *
 * Return: IRQ handling status.
 */
static irqreturn_t mmi_dcdma_irq_handler(int irq, void *data)
{
	struct mmi_dcdma_device *mdev = data;
	u32 status, misc_status, ch05_status, ch67_status, brdy_status;
	u32 error[MMI_DCDMA_NUM_CHAN] = {0}, ch;

	status = dcdma_read(mdev->reg, MMI_DCDMA_ISR);
	misc_status = dcdma_read(mdev->reg, MMI_DCDMA_MISC_ISR);
	ch05_status = dcdma_read(mdev->reg, MMI_DCDMA_CH0_CH5_EISR);
	ch67_status = dcdma_read(mdev->reg, MMI_DCDMA_CH6_CH7_EISR);
	brdy_status = dcdma_read(mdev->reg, MMI_DCDMA_BRDY_CNT_EISR);

	mmi_dcdma_clear_irq(mdev);

	if (!status && !misc_status && !ch05_status && !ch67_status &&
	    !brdy_status)
		return IRQ_NONE;

	/* TODO: should we check status against mask? */

	for (ch = 0; ch < MMI_DCDMA_NUM_CHAN; ++ch) {
		struct mmi_dcdma_chan *chan = &mdev->chan[ch];

		if (status & MMI_DCDMA_ERR_DESC(ch))
			error[ch] |= DCDMA_ERR_DESC;
		if (status & MMI_DCDMA_ERR_DATA_AXI(ch))
			error[ch] |= DCDMA_ERR_DATA_AXI;
		if (ch < MMI_DCDMA_CH_PER_IRQ_REG_05) {
			if (ch05_status & MMI_DCDMA_ERR_RD_AXI_05(ch))
				error[ch] |= DCDMA_ERR_RD_AXI;
			if (ch05_status & MMI_DCDMA_ERR_PRE_05(ch))
				error[ch] |= DCDMA_ERR_PRE;
			if (ch05_status & MMI_DCDMA_ERR_CRC_05(ch))
				error[ch] |= DCDMA_ERR_CRC;
			if (ch05_status & MMI_DCDMA_ERR_WR_AXI_05(ch))
				error[ch] |= DCDMA_ERR_WR_AXI;
			if (ch05_status & MMI_DCDMA_ERR_DONE_05(ch))
				error[ch] |= DCDMA_ERR_DONE;
		} else {
			if (ch67_status & MMI_DCDMA_ERR_RD_AXI_67(ch))
				error[ch] |= DCDMA_ERR_RD_AXI;
			if (ch67_status & MMI_DCDMA_ERR_PRE_67(ch))
				error[ch] |= DCDMA_ERR_PRE;
			if (ch67_status & MMI_DCDMA_ERR_CRC_67(ch))
				error[ch] |= DCDMA_ERR_CRC;
			if (ch67_status & MMI_DCDMA_ERR_WR_AXI_67(ch))
				error[ch] |= DCDMA_ERR_WR_AXI;
			if (ch67_status & MMI_DCDMA_ERR_DONE_67(ch))
				error[ch] |= DCDMA_ERR_DONE;
		}
		if (brdy_status & MMI_DCDMA_ERR_OVERFLOW(ch))
			error[ch] |= DCDMA_ERR_OVERFLOW;

		if (error[ch] != DCDMA_ERR_NONE)
			mmi_dcdma_chan_handle_error(chan, error[ch]);

		if (status & MMI_DCDMA_DESC_DONE(ch))
			mmi_dcdma_chan_handle_done(chan);

		if (status & MMI_DCDMA_NO_OSTAND_TRAN(ch))
			mmi_dcdma_chan_handle_no_ostand(chan);

		if (misc_status & MMI_DCDMA_IRQ_VSYNC &&
		    mmi_dcdma_chan_enabled(chan))
			mmi_dcdma_chan_handle_vsync(chan);
	}

	return IRQ_HANDLED;
}

/* DCDMA Device */

/**
 * mmi_dcdma_write_protect - Enable/disable DCDMA registers write protection
 * @mdev: DCDMA device
 * @protect: Enable or disable write protection
 */
static void mmi_dcdma_write_protect(struct mmi_dcdma_device *mdev,
				    bool protect)
{
	dcdma_write(mdev->reg, MMI_DCDMA_WPROTS, protect);
}

/**
 * mmi_dcdma_probe - Probe DCDMA device
 * @pdev: platform device
 *
 * Return: 0 on success or error code otherwise.
 */
static int mmi_dcdma_probe(struct platform_device *pdev)
{
	struct mmi_dcdma_device *mdev;
	struct dma_device *ddev;
	int ret;
	unsigned int ch;

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	ddev = &mdev->base;
	ddev->dev = &pdev->dev;

	INIT_LIST_HEAD(&ddev->channels);

	platform_set_drvdata(pdev, mdev);

	mdev->axi_clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(mdev->axi_clk))
		return PTR_ERR(mdev->axi_clk);

	mdev->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdev->reg))
		return PTR_ERR(mdev->reg);

	mdev->irq = platform_get_irq(pdev, 0);
	if (mdev->irq < 0)
		return mdev->irq;

	ret = devm_request_threaded_irq(mdev->base.dev, mdev->irq, NULL,
					mmi_dcdma_irq_handler,
					IRQF_SHARED | IRQF_ONESHOT,
					dev_name(mdev->base.dev), mdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup irq: %d\n", ret);
		return ret;
	}

	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	dma_cap_set(DMA_PRIVATE, ddev->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, ddev->cap_mask);
	dma_cap_set(DMA_REPEAT, ddev->cap_mask);
	dma_cap_set(DMA_LOAD_EOT, ddev->cap_mask);
	ddev->copy_align = fls(MMI_DCDMA_ALIGN_BYTES - 1);
	ddev->device_alloc_chan_resources = mmi_dcdma_alloc_chan_resources;
	ddev->device_free_chan_resources = mmi_dcdma_free_chan_resources;
	ddev->device_prep_interleaved_dma = mmi_dcdma_prep_interleaved_dma;
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = mmi_dcdma_issue_pending;
	ddev->device_config = mmi_dcdma_config;
	ddev->device_terminate_all = mmi_dcdma_terminate_all;
	ddev->device_synchronize = mmi_dcdma_synchronize;
	ddev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED);
	ddev->directions = BIT(DMA_MEM_TO_DEV);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	mmi_dcdma_write_protect(mdev, false);

	for (ch = 0; ch < ARRAY_SIZE(mdev->chan); ++ch)
		mmi_dcdma_chan_init(mdev, ch);

	ret = dma_async_device_register(ddev);
	if (ret)
		goto err_dma_register;

	ret = of_dma_controller_register(ddev->dev->of_node,
					 of_mmi_dcdma_xlate, ddev);
	if (ret)
		goto err_ctrl_register;

	mmi_dcdma_enable_irq(mdev);

	return 0;

err_ctrl_register:
	dma_async_device_unregister(ddev);

err_dma_register:
	for (ch = 0; ch < ARRAY_SIZE(mdev->chan); ++ch)
		mmi_dcdma_chan_remove(&mdev->chan[ch]);

	mmi_dcdma_write_protect(mdev, true);

	return ret;
}

/**
 * mmi_dcdma_remove - Remove DCDMA device
 * @pdev: platform device
 */
static void mmi_dcdma_remove(struct platform_device *pdev)
{
	struct mmi_dcdma_device *mdev = platform_get_drvdata(pdev);
	int ch;

	mmi_dcdma_disable_irq(mdev);
	mmi_dcdma_clear_irq(mdev);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&mdev->base);

	for (ch = 0; ch < ARRAY_SIZE(mdev->chan); ++ch)
		mmi_dcdma_chan_remove(&mdev->chan[ch]);

	mmi_dcdma_write_protect(mdev, true);
}

static const struct of_device_id mmi_dcdma_of_match[] = {
	{ .compatible = "amd,mmi-dcdma-1.0",},
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, mmi_dcdma_of_match);

static struct platform_driver mmi_dcdma_driver = {
	.probe			= mmi_dcdma_probe,
	.remove			= mmi_dcdma_remove,
	.driver			= {
		.name		= "mmi-dcdma",
		.of_match_table	= mmi_dcdma_of_match,
	},
};

module_platform_driver(mmi_dcdma_driver);

MODULE_AUTHOR("AMD, Inc.");
MODULE_DESCRIPTION("AMD MMI DCDMA Driver");
MODULE_LICENSE("GPL");
