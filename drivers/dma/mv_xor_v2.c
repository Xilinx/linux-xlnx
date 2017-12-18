/*
 * Copyright (C) 2015-2016 Marvell International Ltd.

 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "dmaengine.h"

/* DMA Engine Registers */
#define MV_XOR_V2_DMA_DESQ_BALR_OFF			0x000
#define MV_XOR_V2_DMA_DESQ_BAHR_OFF			0x004
#define MV_XOR_V2_DMA_DESQ_SIZE_OFF			0x008
#define MV_XOR_V2_DMA_DESQ_DONE_OFF			0x00C
#define   MV_XOR_V2_DMA_DESQ_DONE_PENDING_MASK		0x7FFF
#define   MV_XOR_V2_DMA_DESQ_DONE_PENDING_SHIFT		0
#define   MV_XOR_V2_DMA_DESQ_DONE_READ_PTR_MASK		0x1FFF
#define   MV_XOR_V2_DMA_DESQ_DONE_READ_PTR_SHIFT	16
#define MV_XOR_V2_DMA_DESQ_ARATTR_OFF			0x010
#define   MV_XOR_V2_DMA_DESQ_ATTR_CACHE_MASK		0x3F3F
#define   MV_XOR_V2_DMA_DESQ_ATTR_OUTER_SHAREABLE	0x202
#define   MV_XOR_V2_DMA_DESQ_ATTR_CACHEABLE		0x3C3C
#define MV_XOR_V2_DMA_IMSG_CDAT_OFF			0x014
#define MV_XOR_V2_DMA_IMSG_THRD_OFF			0x018
#define   MV_XOR_V2_DMA_IMSG_THRD_MASK			0x7FFF
#define   MV_XOR_V2_DMA_IMSG_THRD_SHIFT			0x0
#define MV_XOR_V2_DMA_DESQ_AWATTR_OFF			0x01C
  /* Same flags as MV_XOR_V2_DMA_DESQ_ARATTR_OFF */
#define MV_XOR_V2_DMA_DESQ_ALLOC_OFF			0x04C
#define   MV_XOR_V2_DMA_DESQ_ALLOC_WRPTR_MASK		0xFFFF
#define   MV_XOR_V2_DMA_DESQ_ALLOC_WRPTR_SHIFT		16
#define MV_XOR_V2_DMA_IMSG_BALR_OFF			0x050
#define MV_XOR_V2_DMA_IMSG_BAHR_OFF			0x054
#define MV_XOR_V2_DMA_DESQ_CTRL_OFF			0x100
#define	  MV_XOR_V2_DMA_DESQ_CTRL_32B			1
#define   MV_XOR_V2_DMA_DESQ_CTRL_128B			7
#define MV_XOR_V2_DMA_DESQ_STOP_OFF			0x800
#define MV_XOR_V2_DMA_DESQ_DEALLOC_OFF			0x804
#define MV_XOR_V2_DMA_DESQ_ADD_OFF			0x808

/* XOR Global registers */
#define MV_XOR_V2_GLOB_BW_CTRL				0x4
#define   MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_RD_SHIFT	0
#define   MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_RD_VAL	64
#define   MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_WR_SHIFT	8
#define   MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_WR_VAL	8
#define   MV_XOR_V2_GLOB_BW_CTRL_RD_BURST_LEN_SHIFT	12
#define   MV_XOR_V2_GLOB_BW_CTRL_RD_BURST_LEN_VAL	4
#define   MV_XOR_V2_GLOB_BW_CTRL_WR_BURST_LEN_SHIFT	16
#define	  MV_XOR_V2_GLOB_BW_CTRL_WR_BURST_LEN_VAL	4
#define MV_XOR_V2_GLOB_PAUSE				0x014
#define   MV_XOR_V2_GLOB_PAUSE_AXI_TIME_DIS_VAL		0x8
#define MV_XOR_V2_GLOB_SYS_INT_CAUSE			0x200
#define MV_XOR_V2_GLOB_SYS_INT_MASK			0x204
#define MV_XOR_V2_GLOB_MEM_INT_CAUSE			0x220
#define MV_XOR_V2_GLOB_MEM_INT_MASK			0x224

#define MV_XOR_V2_MIN_DESC_SIZE				32
#define MV_XOR_V2_EXT_DESC_SIZE				128

#define MV_XOR_V2_DESC_RESERVED_SIZE			12
#define MV_XOR_V2_DESC_BUFF_D_ADDR_SIZE			12

#define MV_XOR_V2_CMD_LINE_NUM_MAX_D_BUF		8

/*
 * Descriptors queue size. With 32 bytes descriptors, up to 2^14
 * descriptors are allowed, with 128 bytes descriptors, up to 2^12
 * descriptors are allowed. This driver uses 128 bytes descriptors,
 * but experimentation has shown that a set of 1024 descriptors is
 * sufficient to reach a good level of performance.
 */
#define MV_XOR_V2_DESC_NUM				1024

/**
 * struct mv_xor_v2_descriptor - DMA HW descriptor
 * @desc_id: used by S/W and is not affected by H/W.
 * @flags: error and status flags
 * @crc32_result: CRC32 calculation result
 * @desc_ctrl: operation mode and control flags
 * @buff_size: amount of bytes to be processed
 * @fill_pattern_src_addr: Fill-Pattern or Source-Address and
 * AW-Attributes
 * @data_buff_addr: Source (and might be RAID6 destination)
 * addresses of data buffers in RAID5 and RAID6
 * @reserved: reserved
 */
struct mv_xor_v2_descriptor {
	u16 desc_id;
	u16 flags;
	u32 crc32_result;
	u32 desc_ctrl;

	/* Definitions for desc_ctrl */
#define DESC_NUM_ACTIVE_D_BUF_SHIFT	22
#define DESC_OP_MODE_SHIFT		28
#define DESC_OP_MODE_NOP		0	/* Idle operation */
#define DESC_OP_MODE_MEMCPY		1	/* Pure-DMA operation */
#define DESC_OP_MODE_MEMSET		2	/* Mem-Fill operation */
#define DESC_OP_MODE_MEMINIT		3	/* Mem-Init operation */
#define DESC_OP_MODE_MEM_COMPARE	4	/* Mem-Compare operation */
#define DESC_OP_MODE_CRC32		5	/* CRC32 calculation */
#define DESC_OP_MODE_XOR		6	/* RAID5 (XOR) operation */
#define DESC_OP_MODE_RAID6		7	/* RAID6 P&Q-generation */
#define DESC_OP_MODE_RAID6_REC		8	/* RAID6 Recovery */
#define DESC_Q_BUFFER_ENABLE		BIT(16)
#define DESC_P_BUFFER_ENABLE		BIT(17)
#define DESC_IOD			BIT(27)

	u32 buff_size;
	u32 fill_pattern_src_addr[4];
	u32 data_buff_addr[MV_XOR_V2_DESC_BUFF_D_ADDR_SIZE];
	u32 reserved[MV_XOR_V2_DESC_RESERVED_SIZE];
};

/**
 * struct mv_xor_v2_device - implements a xor device
 * @lock: lock for the engine
 * @dma_base: memory mapped DMA register base
 * @glob_base: memory mapped global register base
 * @irq_tasklet:
 * @free_sw_desc: linked list of free SW descriptors
 * @dmadev: dma device
 * @dmachan: dma channel
 * @hw_desq: HW descriptors queue
 * @hw_desq_virt: virtual address of DESCQ
 * @sw_desq: SW descriptors queue
 * @desc_size: HW descriptor size
 * @npendings: number of pending descriptors (for which tx_submit has
 * been called, but not yet issue_pending)
 */
struct mv_xor_v2_device {
	spinlock_t lock;
	void __iomem *dma_base;
	void __iomem *glob_base;
	struct clk *clk;
	struct tasklet_struct irq_tasklet;
	struct list_head free_sw_desc;
	struct dma_device dmadev;
	struct dma_chan	dmachan;
	dma_addr_t hw_desq;
	struct mv_xor_v2_descriptor *hw_desq_virt;
	struct mv_xor_v2_sw_desc *sw_desq;
	int desc_size;
	unsigned int npendings;
};

/**
 * struct mv_xor_v2_sw_desc - implements a xor SW descriptor
 * @idx: descriptor index
 * @async_tx: support for the async_tx api
 * @hw_desc: assosiated HW descriptor
 * @free_list: node of the free SW descriprots list
*/
struct mv_xor_v2_sw_desc {
	int idx;
	struct dma_async_tx_descriptor async_tx;
	struct mv_xor_v2_descriptor hw_desc;
	struct list_head free_list;
};

/*
 * Fill the data buffers to a HW descriptor
 */
static void mv_xor_v2_set_data_buffers(struct mv_xor_v2_device *xor_dev,
					struct mv_xor_v2_descriptor *desc,
					dma_addr_t src, int index)
{
	int arr_index = ((index >> 1) * 3);

	/*
	 * Fill the buffer's addresses to the descriptor.
	 *
	 * The format of the buffers address for 2 sequential buffers
	 * X and X + 1:
	 *
	 *  First word:  Buffer-DX-Address-Low[31:0]
	 *  Second word: Buffer-DX+1-Address-Low[31:0]
	 *  Third word:  DX+1-Buffer-Address-High[47:32] [31:16]
	 *		 DX-Buffer-Address-High[47:32] [15:0]
	 */
	if ((index & 0x1) == 0) {
		desc->data_buff_addr[arr_index] = lower_32_bits(src);

		desc->data_buff_addr[arr_index + 2] &= ~0xFFFF;
		desc->data_buff_addr[arr_index + 2] |=
			upper_32_bits(src) & 0xFFFF;
	} else {
		desc->data_buff_addr[arr_index + 1] =
			lower_32_bits(src);

		desc->data_buff_addr[arr_index + 2] &= ~0xFFFF0000;
		desc->data_buff_addr[arr_index + 2] |=
			(upper_32_bits(src) & 0xFFFF) << 16;
	}
}

/*
 * Return the next available index in the DESQ.
 */
static int mv_xor_v2_get_desq_write_ptr(struct mv_xor_v2_device *xor_dev)
{
	/* read the index for the next available descriptor in the DESQ */
	u32 reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_ALLOC_OFF);

	return ((reg >> MV_XOR_V2_DMA_DESQ_ALLOC_WRPTR_SHIFT)
		& MV_XOR_V2_DMA_DESQ_ALLOC_WRPTR_MASK);
}

/*
 * notify the engine of new descriptors, and update the available index.
 */
static void mv_xor_v2_add_desc_to_desq(struct mv_xor_v2_device *xor_dev,
				       int num_of_desc)
{
	/* write the number of new descriptors in the DESQ. */
	writel(num_of_desc, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_ADD_OFF);
}

/*
 * free HW descriptors
 */
static void mv_xor_v2_free_desc_from_desq(struct mv_xor_v2_device *xor_dev,
					  int num_of_desc)
{
	/* write the number of new descriptors in the DESQ. */
	writel(num_of_desc, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_DEALLOC_OFF);
}

/*
 * Set descriptor size
 * Return the HW descriptor size in bytes
 */
static int mv_xor_v2_set_desc_size(struct mv_xor_v2_device *xor_dev)
{
	writel(MV_XOR_V2_DMA_DESQ_CTRL_128B,
	       xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_CTRL_OFF);

	return MV_XOR_V2_EXT_DESC_SIZE;
}

/*
 * Set the IMSG threshold
 */
static inline
void mv_xor_v2_set_imsg_thrd(struct mv_xor_v2_device *xor_dev, int thrd_val)
{
	u32 reg;

	reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_IMSG_THRD_OFF);

	reg &= (~MV_XOR_V2_DMA_IMSG_THRD_MASK << MV_XOR_V2_DMA_IMSG_THRD_SHIFT);
	reg |= (thrd_val << MV_XOR_V2_DMA_IMSG_THRD_SHIFT);

	writel(reg, xor_dev->dma_base + MV_XOR_V2_DMA_IMSG_THRD_OFF);
}

static irqreturn_t mv_xor_v2_interrupt_handler(int irq, void *data)
{
	struct mv_xor_v2_device *xor_dev = data;
	unsigned int ndescs;
	u32 reg;

	reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_DONE_OFF);

	ndescs = ((reg >> MV_XOR_V2_DMA_DESQ_DONE_PENDING_SHIFT) &
		  MV_XOR_V2_DMA_DESQ_DONE_PENDING_MASK);

	/* No descriptors to process */
	if (!ndescs)
		return IRQ_NONE;

	/*
	 * Update IMSG threshold, to disable new IMSG interrupts until
	 * end of the tasklet
	 */
	mv_xor_v2_set_imsg_thrd(xor_dev, MV_XOR_V2_DESC_NUM);

	/* schedule a tasklet to handle descriptors callbacks */
	tasklet_schedule(&xor_dev->irq_tasklet);

	return IRQ_HANDLED;
}

/*
 * submit a descriptor to the DMA engine
 */
static dma_cookie_t
mv_xor_v2_tx_submit(struct dma_async_tx_descriptor *tx)
{
	int desq_ptr;
	void *dest_hw_desc;
	dma_cookie_t cookie;
	struct mv_xor_v2_sw_desc *sw_desc =
		container_of(tx, struct mv_xor_v2_sw_desc, async_tx);
	struct mv_xor_v2_device *xor_dev =
		container_of(tx->chan, struct mv_xor_v2_device, dmachan);

	dev_dbg(xor_dev->dmadev.dev,
		"%s sw_desc %p: async_tx %p\n",
		__func__, sw_desc, &sw_desc->async_tx);

	/* assign coookie */
	spin_lock_bh(&xor_dev->lock);
	cookie = dma_cookie_assign(tx);

	/* get the next available slot in the DESQ */
	desq_ptr = mv_xor_v2_get_desq_write_ptr(xor_dev);

	/* copy the HW descriptor from the SW descriptor to the DESQ */
	dest_hw_desc = xor_dev->hw_desq_virt + desq_ptr;

	memcpy(dest_hw_desc, &sw_desc->hw_desc, xor_dev->desc_size);

	xor_dev->npendings++;

	spin_unlock_bh(&xor_dev->lock);

	return cookie;
}

/*
 * Prepare a SW descriptor
 */
static struct mv_xor_v2_sw_desc	*
mv_xor_v2_prep_sw_desc(struct mv_xor_v2_device *xor_dev)
{
	struct mv_xor_v2_sw_desc *sw_desc;

	/* Lock the channel */
	spin_lock_bh(&xor_dev->lock);

	if (list_empty(&xor_dev->free_sw_desc)) {
		spin_unlock_bh(&xor_dev->lock);
		/* schedule tasklet to free some descriptors */
		tasklet_schedule(&xor_dev->irq_tasklet);
		return NULL;
	}

	/* get a free SW descriptor from the SW DESQ */
	sw_desc = list_first_entry(&xor_dev->free_sw_desc,
				   struct mv_xor_v2_sw_desc, free_list);
	list_del(&sw_desc->free_list);

	/* Release the channel */
	spin_unlock_bh(&xor_dev->lock);

	/* set the async tx descriptor */
	dma_async_tx_descriptor_init(&sw_desc->async_tx, &xor_dev->dmachan);
	sw_desc->async_tx.tx_submit = mv_xor_v2_tx_submit;
	async_tx_ack(&sw_desc->async_tx);

	return sw_desc;
}

/*
 * Prepare a HW descriptor for a memcpy operation
 */
static struct dma_async_tx_descriptor *
mv_xor_v2_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest,
			  dma_addr_t src, size_t len, unsigned long flags)
{
	struct mv_xor_v2_sw_desc *sw_desc;
	struct mv_xor_v2_descriptor *hw_descriptor;
	struct mv_xor_v2_device	*xor_dev;

	xor_dev = container_of(chan, struct mv_xor_v2_device, dmachan);

	dev_dbg(xor_dev->dmadev.dev,
		"%s len: %zu src %pad dest %pad flags: %ld\n",
		__func__, len, &src, &dest, flags);

	sw_desc = mv_xor_v2_prep_sw_desc(xor_dev);

	sw_desc->async_tx.flags = flags;

	/* set the HW descriptor */
	hw_descriptor = &sw_desc->hw_desc;

	/* save the SW descriptor ID to restore when operation is done */
	hw_descriptor->desc_id = sw_desc->idx;

	/* Set the MEMCPY control word */
	hw_descriptor->desc_ctrl =
		DESC_OP_MODE_MEMCPY << DESC_OP_MODE_SHIFT;

	if (flags & DMA_PREP_INTERRUPT)
		hw_descriptor->desc_ctrl |= DESC_IOD;

	/* Set source address */
	hw_descriptor->fill_pattern_src_addr[0] = lower_32_bits(src);
	hw_descriptor->fill_pattern_src_addr[1] =
		upper_32_bits(src) & 0xFFFF;

	/* Set Destination address */
	hw_descriptor->fill_pattern_src_addr[2] = lower_32_bits(dest);
	hw_descriptor->fill_pattern_src_addr[3] =
		upper_32_bits(dest) & 0xFFFF;

	/* Set buffers size */
	hw_descriptor->buff_size = len;

	/* return the async tx descriptor */
	return &sw_desc->async_tx;
}

/*
 * Prepare a HW descriptor for a XOR operation
 */
static struct dma_async_tx_descriptor *
mv_xor_v2_prep_dma_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
		       unsigned int src_cnt, size_t len, unsigned long flags)
{
	struct mv_xor_v2_sw_desc *sw_desc;
	struct mv_xor_v2_descriptor *hw_descriptor;
	struct mv_xor_v2_device	*xor_dev =
		container_of(chan, struct mv_xor_v2_device, dmachan);
	int i;

	if (src_cnt > MV_XOR_V2_CMD_LINE_NUM_MAX_D_BUF || src_cnt < 1)
		return NULL;

	dev_dbg(xor_dev->dmadev.dev,
		"%s src_cnt: %d len: %zu dest %pad flags: %ld\n",
		__func__, src_cnt, len, &dest, flags);

	sw_desc = mv_xor_v2_prep_sw_desc(xor_dev);

	sw_desc->async_tx.flags = flags;

	/* set the HW descriptor */
	hw_descriptor = &sw_desc->hw_desc;

	/* save the SW descriptor ID to restore when operation is done */
	hw_descriptor->desc_id = sw_desc->idx;

	/* Set the XOR control word */
	hw_descriptor->desc_ctrl =
		DESC_OP_MODE_XOR << DESC_OP_MODE_SHIFT;
	hw_descriptor->desc_ctrl |= DESC_P_BUFFER_ENABLE;

	if (flags & DMA_PREP_INTERRUPT)
		hw_descriptor->desc_ctrl |= DESC_IOD;

	/* Set the data buffers */
	for (i = 0; i < src_cnt; i++)
		mv_xor_v2_set_data_buffers(xor_dev, hw_descriptor, src[i], i);

	hw_descriptor->desc_ctrl |=
		src_cnt << DESC_NUM_ACTIVE_D_BUF_SHIFT;

	/* Set Destination address */
	hw_descriptor->fill_pattern_src_addr[2] = lower_32_bits(dest);
	hw_descriptor->fill_pattern_src_addr[3] =
		upper_32_bits(dest) & 0xFFFF;

	/* Set buffers size */
	hw_descriptor->buff_size = len;

	/* return the async tx descriptor */
	return &sw_desc->async_tx;
}

/*
 * Prepare a HW descriptor for interrupt operation.
 */
static struct dma_async_tx_descriptor *
mv_xor_v2_prep_dma_interrupt(struct dma_chan *chan, unsigned long flags)
{
	struct mv_xor_v2_sw_desc *sw_desc;
	struct mv_xor_v2_descriptor *hw_descriptor;
	struct mv_xor_v2_device	*xor_dev =
		container_of(chan, struct mv_xor_v2_device, dmachan);

	sw_desc = mv_xor_v2_prep_sw_desc(xor_dev);

	/* set the HW descriptor */
	hw_descriptor = &sw_desc->hw_desc;

	/* save the SW descriptor ID to restore when operation is done */
	hw_descriptor->desc_id = sw_desc->idx;

	/* Set the INTERRUPT control word */
	hw_descriptor->desc_ctrl =
		DESC_OP_MODE_NOP << DESC_OP_MODE_SHIFT;
	hw_descriptor->desc_ctrl |= DESC_IOD;

	/* return the async tx descriptor */
	return &sw_desc->async_tx;
}

/*
 * push pending transactions to hardware
 */
static void mv_xor_v2_issue_pending(struct dma_chan *chan)
{
	struct mv_xor_v2_device *xor_dev =
		container_of(chan, struct mv_xor_v2_device, dmachan);

	spin_lock_bh(&xor_dev->lock);

	/*
	 * update the engine with the number of descriptors to
	 * process
	 */
	mv_xor_v2_add_desc_to_desq(xor_dev, xor_dev->npendings);
	xor_dev->npendings = 0;

	/* Activate the channel */
	writel(0, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_STOP_OFF);

	spin_unlock_bh(&xor_dev->lock);
}

static inline
int mv_xor_v2_get_pending_params(struct mv_xor_v2_device *xor_dev,
				 int *pending_ptr)
{
	u32 reg;

	reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_DONE_OFF);

	/* get the next pending descriptor index */
	*pending_ptr = ((reg >> MV_XOR_V2_DMA_DESQ_DONE_READ_PTR_SHIFT) &
			MV_XOR_V2_DMA_DESQ_DONE_READ_PTR_MASK);

	/* get the number of descriptors pending handle */
	return ((reg >> MV_XOR_V2_DMA_DESQ_DONE_PENDING_SHIFT) &
		MV_XOR_V2_DMA_DESQ_DONE_PENDING_MASK);
}

/*
 * handle the descriptors after HW process
 */
static void mv_xor_v2_tasklet(unsigned long data)
{
	struct mv_xor_v2_device *xor_dev = (struct mv_xor_v2_device *) data;
	int pending_ptr, num_of_pending, i;
	struct mv_xor_v2_descriptor *next_pending_hw_desc = NULL;
	struct mv_xor_v2_sw_desc *next_pending_sw_desc = NULL;

	dev_dbg(xor_dev->dmadev.dev, "%s %d\n", __func__, __LINE__);

	/* get the pending descriptors parameters */
	num_of_pending = mv_xor_v2_get_pending_params(xor_dev, &pending_ptr);

	/* next HW descriptor */
	next_pending_hw_desc = xor_dev->hw_desq_virt + pending_ptr;

	/* loop over free descriptors */
	for (i = 0; i < num_of_pending; i++) {

		if (pending_ptr > MV_XOR_V2_DESC_NUM)
			pending_ptr = 0;

		if (next_pending_sw_desc != NULL)
			next_pending_hw_desc++;

		/* get the SW descriptor related to the HW descriptor */
		next_pending_sw_desc =
			&xor_dev->sw_desq[next_pending_hw_desc->desc_id];

		/* call the callback */
		if (next_pending_sw_desc->async_tx.cookie > 0) {
			/*
			 * update the channel's completed cookie - no
			 * lock is required the IMSG threshold provide
			 * the locking
			 */
			dma_cookie_complete(&next_pending_sw_desc->async_tx);

			if (next_pending_sw_desc->async_tx.callback)
				next_pending_sw_desc->async_tx.callback(
				next_pending_sw_desc->async_tx.callback_param);

			dma_descriptor_unmap(&next_pending_sw_desc->async_tx);
		}

		dma_run_dependencies(&next_pending_sw_desc->async_tx);

		/* Lock the channel */
		spin_lock_bh(&xor_dev->lock);

		/* add the SW descriptor to the free descriptors list */
		list_add(&next_pending_sw_desc->free_list,
			 &xor_dev->free_sw_desc);

		/* Release the channel */
		spin_unlock_bh(&xor_dev->lock);

		/* increment the next descriptor */
		pending_ptr++;
	}

	if (num_of_pending != 0) {
		/* free the descriptores */
		mv_xor_v2_free_desc_from_desq(xor_dev, num_of_pending);
	}

	/* Update IMSG threshold, to enable new IMSG interrupts */
	mv_xor_v2_set_imsg_thrd(xor_dev, 0);
}

/*
 *	Set DMA Interrupt-message (IMSG) parameters
 */
static void mv_xor_v2_set_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct mv_xor_v2_device *xor_dev = dev_get_drvdata(desc->dev);

	writel(msg->address_lo,
	       xor_dev->dma_base + MV_XOR_V2_DMA_IMSG_BALR_OFF);
	writel(msg->address_hi & 0xFFFF,
	       xor_dev->dma_base + MV_XOR_V2_DMA_IMSG_BAHR_OFF);
	writel(msg->data,
	       xor_dev->dma_base + MV_XOR_V2_DMA_IMSG_CDAT_OFF);
}

static int mv_xor_v2_descq_init(struct mv_xor_v2_device *xor_dev)
{
	u32 reg;

	/* write the DESQ size to the DMA engine */
	writel(MV_XOR_V2_DESC_NUM,
	       xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_SIZE_OFF);

	/* write the DESQ address to the DMA enngine*/
	writel(xor_dev->hw_desq & 0xFFFFFFFF,
	       xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_BALR_OFF);
	writel((xor_dev->hw_desq & 0xFFFF00000000) >> 32,
	       xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_BAHR_OFF);

	/* enable the DMA engine */
	writel(0, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_STOP_OFF);

	/*
	 * This is a temporary solution, until we activate the
	 * SMMU. Set the attributes for reading & writing data buffers
	 * & descriptors to:
	 *
	 *  - OuterShareable - Snoops will be performed on CPU caches
	 *  - Enable cacheable - Bufferable, Modifiable, Other Allocate
	 *    and Allocate
	 */
	reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_ARATTR_OFF);
	reg &= ~MV_XOR_V2_DMA_DESQ_ATTR_CACHE_MASK;
	reg |= MV_XOR_V2_DMA_DESQ_ATTR_OUTER_SHAREABLE |
		MV_XOR_V2_DMA_DESQ_ATTR_CACHEABLE;
	writel(reg, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_ARATTR_OFF);

	reg = readl(xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_AWATTR_OFF);
	reg &= ~MV_XOR_V2_DMA_DESQ_ATTR_CACHE_MASK;
	reg |= MV_XOR_V2_DMA_DESQ_ATTR_OUTER_SHAREABLE |
		MV_XOR_V2_DMA_DESQ_ATTR_CACHEABLE;
	writel(reg, xor_dev->dma_base + MV_XOR_V2_DMA_DESQ_AWATTR_OFF);

	/* BW CTRL - set values to optimize the XOR performance:
	 *
	 *  - Set WrBurstLen & RdBurstLen - the unit will issue
	 *    maximum of 256B write/read transactions.
	 * -  Limit the number of outstanding write & read data
	 *    (OBB/IBB) requests to the maximal value.
	*/
	reg = ((MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_RD_VAL <<
		MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_RD_SHIFT) |
	       (MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_WR_VAL  <<
		MV_XOR_V2_GLOB_BW_CTRL_NUM_OSTD_WR_SHIFT) |
	       (MV_XOR_V2_GLOB_BW_CTRL_RD_BURST_LEN_VAL <<
		MV_XOR_V2_GLOB_BW_CTRL_RD_BURST_LEN_SHIFT) |
	       (MV_XOR_V2_GLOB_BW_CTRL_WR_BURST_LEN_VAL <<
		MV_XOR_V2_GLOB_BW_CTRL_WR_BURST_LEN_SHIFT));
	writel(reg, xor_dev->glob_base + MV_XOR_V2_GLOB_BW_CTRL);

	/* Disable the AXI timer feature */
	reg = readl(xor_dev->glob_base + MV_XOR_V2_GLOB_PAUSE);
	reg |= MV_XOR_V2_GLOB_PAUSE_AXI_TIME_DIS_VAL;
	writel(reg, xor_dev->glob_base + MV_XOR_V2_GLOB_PAUSE);

	return 0;
}

static int mv_xor_v2_probe(struct platform_device *pdev)
{
	struct mv_xor_v2_device *xor_dev;
	struct resource *res;
	int i, ret = 0;
	struct dma_device *dma_dev;
	struct mv_xor_v2_sw_desc *sw_desc;
	struct msi_desc *msi_desc;

	BUILD_BUG_ON(sizeof(struct mv_xor_v2_descriptor) !=
		     MV_XOR_V2_EXT_DESC_SIZE);

	xor_dev = devm_kzalloc(&pdev->dev, sizeof(*xor_dev), GFP_KERNEL);
	if (!xor_dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xor_dev->dma_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xor_dev->dma_base))
		return PTR_ERR(xor_dev->dma_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	xor_dev->glob_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xor_dev->glob_base))
		return PTR_ERR(xor_dev->glob_base);

	platform_set_drvdata(pdev, xor_dev);

	xor_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(xor_dev->clk) && PTR_ERR(xor_dev->clk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!IS_ERR(xor_dev->clk)) {
		ret = clk_prepare_enable(xor_dev->clk);
		if (ret)
			return ret;
	}

	ret = platform_msi_domain_alloc_irqs(&pdev->dev, 1,
					     mv_xor_v2_set_msi_msg);
	if (ret)
		goto disable_clk;

	msi_desc = first_msi_entry(&pdev->dev);
	if (!msi_desc)
		goto free_msi_irqs;

	ret = devm_request_irq(&pdev->dev, msi_desc->irq,
			       mv_xor_v2_interrupt_handler, 0,
			       dev_name(&pdev->dev), xor_dev);
	if (ret)
		goto free_msi_irqs;

	tasklet_init(&xor_dev->irq_tasklet, mv_xor_v2_tasklet,
		     (unsigned long) xor_dev);

	xor_dev->desc_size = mv_xor_v2_set_desc_size(xor_dev);

	dma_cookie_init(&xor_dev->dmachan);

	/*
	 * allocate coherent memory for hardware descriptors
	 * note: writecombine gives slightly better performance, but
	 * requires that we explicitly flush the writes
	 */
	xor_dev->hw_desq_virt =
		dma_alloc_coherent(&pdev->dev,
				   xor_dev->desc_size * MV_XOR_V2_DESC_NUM,
				   &xor_dev->hw_desq, GFP_KERNEL);
	if (!xor_dev->hw_desq_virt) {
		ret = -ENOMEM;
		goto free_msi_irqs;
	}

	/* alloc memory for the SW descriptors */
	xor_dev->sw_desq = devm_kzalloc(&pdev->dev, sizeof(*sw_desc) *
					MV_XOR_V2_DESC_NUM, GFP_KERNEL);
	if (!xor_dev->sw_desq) {
		ret = -ENOMEM;
		goto free_hw_desq;
	}

	spin_lock_init(&xor_dev->lock);

	/* init the free SW descriptors list */
	INIT_LIST_HEAD(&xor_dev->free_sw_desc);

	/* add all SW descriptors to the free list */
	for (i = 0; i < MV_XOR_V2_DESC_NUM; i++) {
		xor_dev->sw_desq[i].idx = i;
		list_add(&xor_dev->sw_desq[i].free_list,
			 &xor_dev->free_sw_desc);
	}

	dma_dev = &xor_dev->dmadev;

	/* set DMA capabilities */
	dma_cap_zero(dma_dev->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_XOR, dma_dev->cap_mask);
	dma_cap_set(DMA_INTERRUPT, dma_dev->cap_mask);

	/* init dma link list */
	INIT_LIST_HEAD(&dma_dev->channels);

	/* set base routines */
	dma_dev->device_tx_status = dma_cookie_status;
	dma_dev->device_issue_pending = mv_xor_v2_issue_pending;
	dma_dev->dev = &pdev->dev;

	dma_dev->device_prep_dma_memcpy = mv_xor_v2_prep_dma_memcpy;
	dma_dev->device_prep_dma_interrupt = mv_xor_v2_prep_dma_interrupt;
	dma_dev->max_xor = 8;
	dma_dev->device_prep_dma_xor = mv_xor_v2_prep_dma_xor;

	xor_dev->dmachan.device = dma_dev;

	list_add_tail(&xor_dev->dmachan.device_node,
		      &dma_dev->channels);

	mv_xor_v2_descq_init(xor_dev);

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto free_hw_desq;

	dev_notice(&pdev->dev, "Marvell Version 2 XOR driver\n");

	return 0;

free_hw_desq:
	dma_free_coherent(&pdev->dev,
			  xor_dev->desc_size * MV_XOR_V2_DESC_NUM,
			  xor_dev->hw_desq_virt, xor_dev->hw_desq);
free_msi_irqs:
	platform_msi_domain_free_irqs(&pdev->dev);
disable_clk:
	if (!IS_ERR(xor_dev->clk))
		clk_disable_unprepare(xor_dev->clk);
	return ret;
}

static int mv_xor_v2_remove(struct platform_device *pdev)
{
	struct mv_xor_v2_device *xor_dev = platform_get_drvdata(pdev);

	dma_async_device_unregister(&xor_dev->dmadev);

	dma_free_coherent(&pdev->dev,
			  xor_dev->desc_size * MV_XOR_V2_DESC_NUM,
			  xor_dev->hw_desq_virt, xor_dev->hw_desq);

	platform_msi_domain_free_irqs(&pdev->dev);

	clk_disable_unprepare(xor_dev->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mv_xor_v2_dt_ids[] = {
	{ .compatible = "marvell,xor-v2", },
	{},
};
MODULE_DEVICE_TABLE(of, mv_xor_v2_dt_ids);
#endif

static struct platform_driver mv_xor_v2_driver = {
	.probe		= mv_xor_v2_probe,
	.remove		= mv_xor_v2_remove,
	.driver		= {
		.name	= "mv_xor_v2",
		.of_match_table = of_match_ptr(mv_xor_v2_dt_ids),
	},
};

module_platform_driver(mv_xor_v2_driver);

MODULE_DESCRIPTION("DMA engine driver for Marvell's Version 2 of XOR engine");
MODULE_LICENSE("GPL");
