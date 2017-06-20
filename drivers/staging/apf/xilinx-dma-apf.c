/*
 * Xilinx AXI DMA Engine support
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 *
 * Description:
 * This driver supports Xilinx AXI DMA engine:
 *  . Axi DMA engine, it does transfers between memory and device. It can be
 *    configured to have one channel or two channels. If configured as two
 *    channels, one is for transmit to device and another is for receive from
 *    device.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
#include <linux/dma-buf.h>

#include <linux/of.h>
#include <linux/irq.h>
#include <linux/of_irq.h>

#include "xilinx-dma-apf.h"

#include "xlnk.h"

static DEFINE_MUTEX(dma_list_mutex);
static LIST_HEAD(dma_device_list);
/* IO accessors */
#define DMA_OUT_64(addr, val)   (writeq(val, addr))
#define DMA_OUT(addr, val)      (iowrite32(val, addr))
#define DMA_IN(addr)            (ioread32(addr))

#define GET_LOW(x) ((u32)((x) & 0xFFFFFFFF))
#define GET_HI(x) ((u32)((x) / 0x100000000))

static int unpin_user_pages(struct scatterlist *sglist, unsigned int cnt);
/* Driver functions */
static void xdma_clean_bd(struct xdma_desc_hw *bd)
{
	bd->src_addr = 0x0;
	bd->control = 0x0;
	bd->status = 0x0;
	bd->app[0] = 0x0;
	bd->app[1] = 0x0;
	bd->app[2] = 0x0;
	bd->app[3] = 0x0;
	bd->app[4] = 0x0;
	bd->dmahead = 0x0;
	bd->sw_flag = 0x0;
}

static int dma_is_running(struct xdma_chan *chan)
{
	return !(DMA_IN(&chan->regs->sr) & XDMA_SR_HALTED_MASK) &&
		(DMA_IN(&chan->regs->cr) & XDMA_CR_RUNSTOP_MASK);
}

static int dma_is_idle(struct xdma_chan *chan)
{
	return DMA_IN(&chan->regs->sr) & XDMA_SR_IDLE_MASK;
}

static void dma_halt(struct xdma_chan *chan)
{
	DMA_OUT(&chan->regs->cr,
		(DMA_IN(&chan->regs->cr)  & ~XDMA_CR_RUNSTOP_MASK));
}

static void dma_start(struct xdma_chan *chan)
{
	DMA_OUT(&chan->regs->cr,
		(DMA_IN(&chan->regs->cr) | XDMA_CR_RUNSTOP_MASK));
}

static int dma_init(struct xdma_chan *chan)
{
	int loop = XDMA_RESET_LOOP;

	DMA_OUT(&chan->regs->cr,
		(DMA_IN(&chan->regs->cr) | XDMA_CR_RESET_MASK));

	/* Wait for the hardware to finish reset
	 */
	while (loop) {
		if (!(DMA_IN(&chan->regs->cr) & XDMA_CR_RESET_MASK))
			break;

		loop -= 1;
	}

	if (!loop)
		return 1;

	return 0;
}

static int xdma_alloc_chan_descriptors(struct xdma_chan *chan)
{
	int i;
	u8 *ptr;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx DMA specification requirement.
	 */
	ptr = (u8 *)dma_alloc_coherent(chan->dev,
				(sizeof(struct xdma_desc_hw) * XDMA_MAX_BD_CNT),
				&chan->bd_phys_addr,
				GFP_KERNEL);

	if (!ptr) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	memset(ptr, 0, (sizeof(struct xdma_desc_hw) * XDMA_MAX_BD_CNT));
	chan->bd_cur = 0;
	chan->bd_tail = 0;
	chan->bd_used = 0;
	chan->bd_chain_size = sizeof(struct xdma_desc_hw) * XDMA_MAX_BD_CNT;

	/*
	 * Pre allocate all the channels.
	 */
	for (i = 0; i < XDMA_MAX_BD_CNT; i++) {
		chan->bds[i] = (struct xdma_desc_hw *)
				(ptr + (sizeof(struct xdma_desc_hw) * i));
		chan->bds[i]->next_desc = chan->bd_phys_addr +
					(sizeof(struct xdma_desc_hw) *
						((i + 1) % XDMA_MAX_BD_CNT));
	}

	/* there is at least one descriptor free to be allocated */
	return 0;
}

static void xdma_free_chan_resources(struct xdma_chan *chan)
{
	dev_dbg(chan->dev, "Free all channel resources.\n");
	dma_free_coherent(chan->dev, (sizeof(struct xdma_desc_hw) *
			XDMA_MAX_BD_CNT), chan->bds[0], chan->bd_phys_addr);
}

static void xilinx_chan_desc_reinit(struct xdma_chan *chan)
{
	struct xdma_desc_hw *desc;
	unsigned int start, end;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	start = 0;
	end = XDMA_MAX_BD_CNT;

	while (start < end) {
		desc = chan->bds[start];
		xdma_clean_bd(desc);
		start++;
	}
	/* Re-initialize bd_cur and bd_tail values */
	chan->bd_cur = 0;
	chan->bd_tail = 0;
	chan->bd_used = 0;
	spin_unlock_irqrestore(&chan->lock, flags);
}

static void xilinx_chan_desc_cleanup(struct xdma_chan *chan)
{
	struct xdma_head *dmahead;
	struct xdma_desc_hw *desc;
	struct completion *cmp;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
#define XDMA_BD_STS_RXEOF_MASK 0x04000000
	desc = chan->bds[chan->bd_cur];
	while (desc->status & XDMA_BD_STS_ALL_MASK) {
		if ((desc->status & XDMA_BD_STS_RXEOF_MASK) &&
		    !(desc->dmahead)) {
			pr_info("ERROR: premature EOF on DMA\n");
			dma_init(chan); /* reset the dma HW */
			while (!(desc->dmahead)) {
				xdma_clean_bd(desc);
				chan->bd_used--;
				chan->bd_cur++;
				if (chan->bd_cur >= XDMA_MAX_BD_CNT)
					chan->bd_cur = 0;
				desc = chan->bds[chan->bd_cur];
			}
		}
		if (desc->dmahead) {
			if ((desc->sw_flag & XDMA_BD_SF_POLL_MODE_MASK))
				if (!(desc->sw_flag & XDMA_BD_SF_SW_DONE_MASK))
					break;

			dmahead = (struct xdma_head *)desc->dmahead;
			cmp = (struct completion *)&dmahead->cmp;
			if (dmahead->nappwords_o)
				memcpy(dmahead->appwords_o, desc->app,
				       dmahead->nappwords_o * sizeof(u32));

			if (chan->poll_mode)
				cmp->done = 1;
			else
				complete(cmp);
		}
		xdma_clean_bd(desc);
		chan->bd_used--;
		chan->bd_cur++;
		if (chan->bd_cur >= XDMA_MAX_BD_CNT)
			chan->bd_cur = 0;
		desc = chan->bds[chan->bd_cur];
	}
	spin_unlock_irqrestore(&chan->lock, flags);
}

static void xdma_err_tasklet(unsigned long data)
{
	struct xdma_chan *chan = (struct xdma_chan *)data;

	if (chan->err) {
		/* If reset failed, need to hard reset
		 * Channel is no longer functional
		 */
		if (!dma_init(chan))
			chan->err = 0;
		else
			dev_err(chan->dev, "DMA channel reset failed, please reset system\n");
	}

	/* Barrier to assert descriptor init is reaches memory */
	rmb();
	xilinx_chan_desc_cleanup(chan);

	xilinx_chan_desc_reinit(chan);
}

static void xdma_tasklet(unsigned long data)
{
	struct xdma_chan *chan = (struct xdma_chan *)data;

	xilinx_chan_desc_cleanup(chan);
}

static void dump_cur_bd(struct xdma_chan *chan)
{
	u32 index;

	index = (((u32)DMA_IN(&chan->regs->cdr)) - chan->bd_phys_addr) /
			sizeof(struct xdma_desc_hw);

	dev_err(chan->dev, "cur bd @ %08x\n",   (u32)DMA_IN(&chan->regs->cdr));
	dev_err(chan->dev, "  buf  = %p\n",
		(void *)chan->bds[index]->src_addr);
	dev_err(chan->dev, "  ctrl = 0x%08x\n", chan->bds[index]->control);
	dev_err(chan->dev, "  sts  = 0x%08x\n", chan->bds[index]->status);
	dev_err(chan->dev, "  next = %p\n",
		(void *)chan->bds[index]->next_desc);
}

static irqreturn_t xdma_rx_intr_handler(int irq, void *data)
{
	struct xdma_chan *chan = data;
	u32 stat;

	stat = DMA_IN(&chan->regs->sr);

	if (!(stat & XDMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	/* Ack the interrupts */
	DMA_OUT(&chan->regs->sr, (stat & XDMA_XR_IRQ_ALL_MASK));

	if (stat & XDMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev, "Channel %s has errors %x, cdr %x tdr %x\n",
			chan->name, (unsigned int)stat,
			(unsigned int)DMA_IN(&chan->regs->cdr),
			(unsigned int)DMA_IN(&chan->regs->tdr));

		dump_cur_bd(chan);

		chan->err = 1;
		tasklet_schedule(&chan->dma_err_tasklet);
	}

	if (!(chan->poll_mode) && ((stat & XDMA_XR_IRQ_DELAY_MASK) ||
				   (stat & XDMA_XR_IRQ_IOC_MASK)))
		tasklet_schedule(&chan->tasklet);

	return IRQ_HANDLED;
}

static irqreturn_t xdma_tx_intr_handler(int irq, void *data)
{
	struct xdma_chan *chan = data;
	u32 stat;

	stat = DMA_IN(&chan->regs->sr);

	if (!(stat & XDMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	/* Ack the interrupts */
	DMA_OUT(&chan->regs->sr, (stat & XDMA_XR_IRQ_ALL_MASK));

	if (stat & XDMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev, "Channel %s has errors %x, cdr %x tdr %x\n",
			chan->name, (unsigned int)stat,
			(unsigned int)DMA_IN(&chan->regs->cdr),
			(unsigned int)DMA_IN(&chan->regs->tdr));

		dump_cur_bd(chan);

		chan->err = 1;
		tasklet_schedule(&chan->dma_err_tasklet);
	}

	if (!(chan->poll_mode) && ((stat & XDMA_XR_IRQ_DELAY_MASK) ||
				   (stat & XDMA_XR_IRQ_IOC_MASK)))
		tasklet_schedule(&chan->tasklet);

	return IRQ_HANDLED;
}

static void xdma_chan_remove(struct xdma_chan *chan)
{
}

static void xdma_start_transfer(struct xdma_chan *chan,
				int start_index,
				int end_index)
{
	xlnk_intptr_type cur_phys;
	xlnk_intptr_type tail_phys;
	u32 regval;

	if (chan->err)
		return;

	cur_phys = chan->bd_phys_addr + (start_index *
					sizeof(struct xdma_desc_hw));
	tail_phys = chan->bd_phys_addr + (end_index *
					sizeof(struct xdma_desc_hw));
	/* If hardware is busy, move the tail & return */
	if (dma_is_running(chan) || dma_is_idle(chan)) {
#if XLNK_SYS_BIT_WIDTH == 32
		DMA_OUT(&chan->regs->tdr, tail_phys);
#else
		DMA_OUT_64(&chan->regs->tdr, tail_phys);
#endif
		return;
	}

#if XLNK_SYS_BIT_WIDTH == 32
	DMA_OUT(&chan->regs->cdr, cur_phys);
#else
	DMA_OUT_64(&chan->regs->cdr, cur_phys);
#endif

	dma_start(chan);

	/* Enable interrupts */
	regval = DMA_IN(&chan->regs->cr);
	regval |= (chan->poll_mode ? XDMA_XR_IRQ_ERROR_MASK
					: XDMA_XR_IRQ_ALL_MASK);
	DMA_OUT(&chan->regs->cr, regval);

	/* Update tail ptr register and start the transfer */
#if XLNK_SYS_BIT_WIDTH == 32
	DMA_OUT(&chan->regs->tdr, tail_phys);
#else
	DMA_OUT_64(&chan->regs->tdr, tail_phys);
#endif
}

static int xdma_setup_hw_desc(struct xdma_chan *chan,
			      struct xdma_head *dmahead,
			      struct scatterlist *sgl,
			      unsigned int sg_len,
			      enum dma_data_direction direction,
			      unsigned int nappwords_i,
			      u32 *appwords_i)
{
	struct xdma_desc_hw *bd = NULL;
	size_t copy;
	struct scatterlist *sg;
	size_t sg_used;
	dma_addr_t dma_src;
	int i, start_index = -1, end_index1 = 0, end_index2 = -1;
	int status;
	unsigned long flags;
	unsigned int bd_used_saved;

	if (!chan) {
		pr_err("Requested transfer on invalid channel\n");
		return -ENODEV;
	}

	/* if we almost run out of bd, try to recycle some */
	if ((chan->poll_mode) && (chan->bd_used >= XDMA_BD_CLEANUP_THRESHOLD))
		xilinx_chan_desc_cleanup(chan);

	spin_lock_irqsave(&chan->lock, flags);

	bd_used_saved = chan->bd_used;
	/*
	 * Build transactions using information in the scatter gather list
	 */
	for_each_sg(sgl, sg, sg_len, i) {
		sg_used = 0;

		/* Loop until the entire scatterlist entry is used */
		while (sg_used < sg_dma_len(sg)) {
			/* Allocate the link descriptor from DMA pool */
			bd = chan->bds[chan->bd_tail];
			if ((bd->control) & (XDMA_BD_STS_ACTUAL_LEN_MASK)) {
				end_index2 = chan->bd_tail;
				status = -ENOMEM;
				/* If first was not set, then we failed to
				 * allocate the very first descriptor,
				 * and we're done
				 */
				if (start_index == -1)
					goto out_unlock;
				else
					goto out_clean;
			}
			/*
			 * Calculate the maximum number of bytes to transfer,
			 * making sure it is less than the DMA controller limit
			 */
			copy = min((size_t)(sg_dma_len(sg) - sg_used),
				   (size_t)chan->max_len);
			/*
			 * Only the src address for DMA
			 */
			dma_src = sg_dma_address(sg) + sg_used;
			bd->src_addr = dma_src;

			/* Fill in the descriptor */
			bd->control = copy;

			/*
			 * If this is not the first descriptor, chain the
			 * current descriptor after the previous descriptor
			 *
			 * For the first DMA_TO_DEVICE transfer, set SOP
			 */
			if (start_index == -1) {
				start_index = chan->bd_tail;

				if (nappwords_i)
					memcpy(bd->app, appwords_i,
					       nappwords_i * sizeof(u32));

				if (direction == DMA_TO_DEVICE)
					bd->control |= XDMA_BD_SOP;
			}

			sg_used += copy;
			end_index2 = chan->bd_tail;
			chan->bd_tail++;
			chan->bd_used++;
			if (chan->bd_tail >= XDMA_MAX_BD_CNT) {
				end_index1 = XDMA_MAX_BD_CNT;
				chan->bd_tail = 0;
			}
		}
	}

	if (start_index == -1) {
		status = -EINVAL;
		goto out_unlock;
	}

	bd->dmahead = (xlnk_intptr_type)dmahead;
	bd->sw_flag = chan->poll_mode ? XDMA_BD_SF_POLL_MODE_MASK : 0;
	dmahead->last_bd_index = end_index2;

	if (direction == DMA_TO_DEVICE)
		bd->control |= XDMA_BD_EOP;

	/* Barrier to assert control word write commits */
	wmb();

	xdma_start_transfer(chan, start_index, end_index2);

	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;

out_clean:
	if (!end_index1) {
		for (i = start_index; i < end_index2; i++)
			xdma_clean_bd(chan->bds[i]);
	} else {
		/* clean till the end of bd list first, and then 2nd end */
		for (i = start_index; i < end_index1; i++)
			xdma_clean_bd(chan->bds[i]);

		end_index1 = 0;
		for (i = end_index1; i < end_index2; i++)
			xdma_clean_bd(chan->bds[i]);
	}
	/* Move the bd_tail back */
	chan->bd_tail = start_index;
	chan->bd_used = bd_used_saved;

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);

	return status;
}

/*
 *  create minimal length scatter gather list for physically contiguous buffer
 *  that starts at phy_buf and has length phy_buf_len bytes
 */
static unsigned int phy_buf_to_sgl(xlnk_intptr_type phy_buf,
				   unsigned int phy_buf_len,
				   struct scatterlist *sgl)
{
	unsigned int sgl_cnt = 0;
	struct scatterlist *sgl_head;
	unsigned int dma_len;
	unsigned int num_bd;

	if (!phy_buf || !phy_buf_len) {
		pr_err("phy_buf is NULL or phy_buf_len = 0\n");
		return sgl_cnt;
	}

	num_bd = (phy_buf_len + (XDMA_MAX_TRANS_LEN - 1))
		/ XDMA_MAX_TRANS_LEN;
	sgl_head = sgl;
	sg_init_table(sgl, num_bd);

	while (phy_buf_len > 0) {
		xlnk_intptr_type page_id = phy_buf >> PAGE_SHIFT;
		unsigned int offset = phy_buf - (page_id << PAGE_SHIFT);

		sgl_cnt++;
		if (sgl_cnt > XDMA_MAX_BD_CNT)
			return 0;

		dma_len = (phy_buf_len > XDMA_MAX_TRANS_LEN) ?
				XDMA_MAX_TRANS_LEN : phy_buf_len;

		sg_set_page(sgl_head, pfn_to_page(page_id), dma_len, offset);
		sg_dma_address(sgl_head) = (dma_addr_t)phy_buf;
		sg_dma_len(sgl_head) = dma_len;
		sgl_head = sg_next(sgl_head);

		phy_buf += dma_len;
		phy_buf_len -= dma_len;
	}

	return sgl_cnt;
}

/*  merge sg list, sgl, with length sgl_len, to sgl_merged, to save dma bds */
static unsigned int sgl_merge(struct scatterlist *sgl,
			      unsigned int sgl_len,
			      struct scatterlist *sgl_merged)
{
	struct scatterlist *sghead, *sgend, *sgnext, *sg_merged_head;
	unsigned int sg_visited_cnt = 0, sg_merged_num = 0;
	unsigned int dma_len = 0;

	sg_merged_head = sgl_merged;
	sghead = sgl;

	while (sghead && (sg_visited_cnt < sgl_len)) {
		dma_len = sg_dma_len(sghead);
		sgend = sghead;
		sg_visited_cnt++;
		sgnext = sg_next(sgend);

		while (sgnext && (sg_visited_cnt < sgl_len)) {
			if ((sg_dma_address(sgend) + sg_dma_len(sgend)) !=
				sg_dma_address(sgnext))
				break;

			if (dma_len + sg_dma_len(sgnext) >= XDMA_MAX_TRANS_LEN)
				break;

			sgend = sgnext;
			dma_len += sg_dma_len(sgend);
			sg_visited_cnt++;
			sgnext = sg_next(sgnext);
		}

		sg_merged_num++;
		if (sg_merged_num > XDMA_MAX_BD_CNT)
			return 0;

		memcpy(sg_merged_head, sghead, sizeof(struct scatterlist));

		sg_dma_len(sg_merged_head) = dma_len;

		sg_merged_head = sg_next(sg_merged_head);
		sghead = sg_next(sgend);
	}

	return sg_merged_num;
}

static int pin_user_pages(xlnk_intptr_type uaddr,
			  unsigned int ulen,
			  int write,
			  struct scatterlist **scatterpp,
			  unsigned int *cntp,
			  unsigned int user_flags)
{
	int status;
	struct mm_struct *mm = current->mm;
	unsigned int first_page;
	unsigned int last_page;
	unsigned int num_pages;
	struct scatterlist *sglist;
	struct page **mapped_pages;

	unsigned int pgidx;
	unsigned int pglen;
	unsigned int pgoff;
	unsigned int sublen;

	first_page = uaddr / PAGE_SIZE;
	last_page = (uaddr + ulen - 1) / PAGE_SIZE;
	num_pages = last_page - first_page + 1;
	mapped_pages = vmalloc(sizeof(*mapped_pages) * num_pages);
	if (!mapped_pages)
		return -ENOMEM;

	down_read(&mm->mmap_sem);
	status = get_user_pages(uaddr, num_pages,
				(write ? FOLL_WRITE : 0) | FOLL_FORCE,
				mapped_pages, NULL);
	up_read(&mm->mmap_sem);

	if (status == num_pages) {
		sglist = kcalloc(num_pages,
				 sizeof(struct scatterlist),
				 GFP_KERNEL);
		if (!sglist) {
			pr_err("%s: kcalloc failed to create sg list\n",
			       __func__);
			vfree(mapped_pages);
			return -ENOMEM;
		}
		sg_init_table(sglist, num_pages);
		sublen = 0;
		for (pgidx = 0; pgidx < status; pgidx++) {
			if (pgidx == 0 && num_pages != 1) {
				pgoff = uaddr & (~PAGE_MASK);
				pglen = PAGE_SIZE - pgoff;
			} else if (pgidx == 0 && num_pages == 1) {
				pgoff = uaddr & (~PAGE_MASK);
				pglen = ulen;
			} else if (pgidx == num_pages - 1) {
				pgoff = 0;
				pglen = ulen - sublen;
			} else {
				pgoff = 0;
				pglen = PAGE_SIZE;
			}

			sublen += pglen;

			sg_set_page(&sglist[pgidx],
				    mapped_pages[pgidx],
				    pglen, pgoff);

			sg_dma_len(&sglist[pgidx]) = pglen;
		}

		*scatterpp = sglist;
		*cntp = num_pages;

		vfree(mapped_pages);
		return 0;
	}
	pr_err("Failed to pin user pages\n");
	for (pgidx = 0; pgidx < status; pgidx++)
		put_page(mapped_pages[pgidx]);
	vfree(mapped_pages);
	return -ENOMEM;
}

static int unpin_user_pages(struct scatterlist *sglist, unsigned int cnt)
{
	struct page *pg;
	unsigned int i;

	if (!sglist)
		return 0;

	for (i = 0; i < cnt; i++) {
		pg = sg_page(sglist + i);
		if (pg)
			put_page(pg);
	}

	kfree(sglist);
	return 0;
}

struct xdma_chan *xdma_request_channel(char *name)
{
	int i;
	struct xdma_device *device, *tmp;

	list_for_each_entry_safe(device, tmp, &dma_device_list, node) {
		for (i = 0; i < device->channel_count; i++) {
			if (!strcmp(device->chan[i]->name, name))
				return device->chan[i];
		}
	}
	return NULL;
}
EXPORT_SYMBOL(xdma_request_channel);

void xdma_release_channel(struct xdma_chan *chan)
{ }
EXPORT_SYMBOL(xdma_release_channel);

void xdma_release_all_channels(void)
{
	int i;
	struct xdma_device *device, *tmp;

	list_for_each_entry_safe(device, tmp, &dma_device_list, node) {
		for (i = 0; i < device->channel_count; i++) {
			if (device->chan[i]->client_count) {
				dma_halt(device->chan[i]);
				xilinx_chan_desc_reinit(device->chan[i]);
				pr_info("%s: chan %s freed\n",
					__func__,
					device->chan[i]->name);
			}
		}
	}
}
EXPORT_SYMBOL(xdma_release_all_channels);

static void xdma_release(struct device *dev)
{
}

int xdma_submit(struct xdma_chan *chan,
		xlnk_intptr_type userbuf,
		void *kaddr,
		unsigned int size,
		unsigned int nappwords_i,
		u32 *appwords_i,
		unsigned int nappwords_o,
		unsigned int user_flags,
		struct xdma_head **dmaheadpp,
		struct xlnk_dmabuf_reg *dp)
{
	struct xdma_head *dmahead;
	struct scatterlist *sglist, *sglist_dma;
	unsigned int sgcnt, sgcnt_dma;
	enum dma_data_direction dmadir;
	int status;
	unsigned long attrs = 0;

	dmahead = kzalloc(sizeof(*dmahead), GFP_KERNEL);
	if (!dmahead)
		return -ENOMEM;

	dmahead->chan = chan;
	dmahead->userbuf = userbuf;
	dmahead->size = size;
	dmahead->dmadir = chan->direction;
	dmahead->userflag = user_flags;
	dmahead->dmabuf = dp;
	dmadir = chan->direction;

	if (!(user_flags & CF_FLAG_CACHE_FLUSH_INVALIDATE))
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (dp) {
		int i;
		int cpy_size;
		struct scatterlist *sg;
		unsigned int remaining_size = size;
		unsigned int observed_size = 0;

		dp->dbuf_attach = dma_buf_attach(dp->dbuf, chan->dev);
		dp->dbuf_sg_table = dma_buf_map_attachment(dp->dbuf_attach,
							   chan->direction);
		if (IS_ERR_OR_NULL(dp->dbuf_sg_table)) {
			pr_err("%s unable to map sg_table for dbuf: %p\n",
			       __func__, dp->dbuf_sg_table);
			return -EINVAL;
		}
		cpy_size = dp->dbuf_sg_table->nents *
			sizeof(struct scatterlist);
		dp->sg_list = kmalloc(cpy_size, GFP_KERNEL);
		if (!dp->sg_list)
			return -ENOMEM;
		dp->sg_list_cnt = 0;
		memcpy(dp->sg_list, dp->dbuf_sg_table->sgl, cpy_size);
		for_each_sg(dp->sg_list,
			    sg,
			    dp->dbuf_sg_table->nents,
			    i) {
			observed_size += sg_dma_len(sg);
			if (remaining_size == 0) {
				sg_dma_len(sg) = 0;
			} else if (sg_dma_len(sg) > remaining_size) {
				sg_dma_len(sg) = remaining_size;
				dp->sg_list_cnt++;
			} else {
				remaining_size -= sg_dma_len(sg);
				dp->sg_list_cnt++;
			}
		}

		sglist_dma = dp->sg_list;
		sglist = dp->sg_list;
		sgcnt = dp->sg_list_cnt;
		sgcnt_dma = dp->sg_list_cnt;
		dmahead->userbuf = (xlnk_intptr_type)sglist->dma_address;
	} else if (user_flags & CF_FLAG_PHYSICALLY_CONTIGUOUS) {
		sglist = chan->scratch_sglist;
		sgcnt = phy_buf_to_sgl(userbuf, size, sglist);
		if (!sgcnt)
			return -ENOMEM;

		sglist_dma = sglist;
		sgcnt_dma = sgcnt;

		status = get_dma_ops(chan->dev)->map_sg(chan->dev,
							sglist,
							sgcnt,
							dmadir,
							attrs);

		if (!status) {
			pr_err("sg contiguous mapping failed\n");
			return -ENOMEM;
		}
	} else {
		status = pin_user_pages(userbuf, size,
					dmadir != DMA_TO_DEVICE,
					&sglist, &sgcnt, user_flags);
		if (status < 0) {
			pr_err("pin_user_pages failed\n");
			return status;
		}

		status = get_dma_ops(chan->dev)->map_sg(chan->dev, sglist,
							sgcnt, dmadir, attrs);
		if (!status) {
			pr_err("dma_map_sg failed\n");
			unpin_user_pages(sglist, sgcnt);
			return -ENOMEM;
		}

		/* merge sg list to save dma bds */
		sglist_dma = chan->scratch_sglist;
		sgcnt_dma = sgl_merge(sglist, sgcnt, sglist_dma);
		if (!sgcnt_dma) {
			get_dma_ops(chan->dev)->unmap_sg(chan->dev, sglist,
							 sgcnt, dmadir, attrs);
			unpin_user_pages(sglist, sgcnt);
			return -ENOMEM;
		}
	}
	dmahead->sglist = sglist;
	dmahead->sgcnt = sgcnt;

	/* skipping config */
	init_completion(&dmahead->cmp);

	if (nappwords_i > XDMA_MAX_APPWORDS)
		nappwords_i = XDMA_MAX_APPWORDS;

	if (nappwords_o > XDMA_MAX_APPWORDS)
		nappwords_o = XDMA_MAX_APPWORDS;

	dmahead->nappwords_o = nappwords_o;

	status = xdma_setup_hw_desc(chan, dmahead, sglist_dma, sgcnt_dma,
				    dmadir, nappwords_i, appwords_i);
	if (status) {
		pr_err("setup hw desc failed\n");
		if (!(user_flags & CF_FLAG_PHYSICALLY_CONTIGUOUS)) {
			get_dma_ops(chan->dev)->unmap_sg(chan->dev, sglist,
							 sgcnt, dmadir, attrs);
			unpin_user_pages(sglist, sgcnt);
		}
		return -ENOMEM;
	}

	*dmaheadpp = dmahead;
	return 0;
}
EXPORT_SYMBOL(xdma_submit);

int xdma_wait(struct xdma_head *dmahead,
	      unsigned int user_flags,
	      unsigned int *operating_flags)
{
	struct xdma_chan *chan = dmahead->chan;
	unsigned long attrs = 0;

	if (chan->poll_mode) {
		xilinx_chan_desc_cleanup(chan);
		*operating_flags |= XDMA_FLAGS_WAIT_COMPLETE;
	} else {
		if (*operating_flags & XDMA_FLAGS_TRYWAIT) {
			if (!try_wait_for_completion(&dmahead->cmp))
				return 0;
			*operating_flags |= XDMA_FLAGS_WAIT_COMPLETE;
		} else {
			wait_for_completion(&dmahead->cmp);
			*operating_flags |= XDMA_FLAGS_WAIT_COMPLETE;
		}
	}

	if (dmahead->dmabuf) {
		dma_buf_unmap_attachment(dmahead->dmabuf->dbuf_attach,
					 dmahead->dmabuf->dbuf_sg_table,
					 dmahead->dmabuf->dma_direction);
		kfree(dmahead->dmabuf->sg_list);
		dma_buf_detach(dmahead->dmabuf->dbuf,
			       dmahead->dmabuf->dbuf_attach);
	} else {
		if (!(user_flags & CF_FLAG_CACHE_FLUSH_INVALIDATE))
			attrs |= DMA_ATTR_SKIP_CPU_SYNC;

		get_dma_ops(chan->dev)->unmap_sg(chan->dev,
						 dmahead->sglist,
						 dmahead->sgcnt,
						 dmahead->dmadir,
						 attrs);
		if (!(user_flags & CF_FLAG_PHYSICALLY_CONTIGUOUS))
			unpin_user_pages(dmahead->sglist, dmahead->sgcnt);
	}

	return 0;
}
EXPORT_SYMBOL(xdma_wait);

int xdma_getconfig(struct xdma_chan *chan,
		   unsigned char *irq_thresh,
		   unsigned char *irq_delay)
{
	*irq_thresh = (DMA_IN(&chan->regs->cr) >> XDMA_COALESCE_SHIFT) & 0xff;
	*irq_delay = (DMA_IN(&chan->regs->cr) >> XDMA_DELAY_SHIFT) & 0xff;
	return 0;
}
EXPORT_SYMBOL(xdma_getconfig);

int xdma_setconfig(struct xdma_chan *chan,
		   unsigned char irq_thresh,
		   unsigned char irq_delay)
{
	unsigned long val;

	if (dma_is_running(chan))
		return -EBUSY;

	val = DMA_IN(&chan->regs->cr);
	val &= ~((0xff << XDMA_COALESCE_SHIFT) |
				(0xff << XDMA_DELAY_SHIFT));
	val |= ((irq_thresh << XDMA_COALESCE_SHIFT) |
				(irq_delay << XDMA_DELAY_SHIFT));

	DMA_OUT(&chan->regs->cr, val);
	return 0;
}
EXPORT_SYMBOL(xdma_setconfig);

static const struct of_device_id gic_match[] = {
	{ .compatible = "arm,cortex-a9-gic", },
	{ .compatible = "arm,cortex-a15-gic", },
	{ },
};

static struct device_node *gic_node;

unsigned int xlate_irq(unsigned int hwirq)
{
	struct of_phandle_args irq_data;
	unsigned int irq;

	if (!gic_node)
		gic_node = of_find_matching_node(NULL, gic_match);

	if (WARN_ON(!gic_node))
		return hwirq;

	irq_data.np = gic_node;
	irq_data.args_count = 3;
	irq_data.args[0] = 0;
#if XLNK_SYS_BIT_WIDTH == 32
	irq_data.args[1] = hwirq - 32; /* GIC SPI offset */
#else
	irq_data.args[1] = hwirq;
#endif
	irq_data.args[2] = IRQ_TYPE_LEVEL_HIGH;

	irq = irq_create_of_mapping(&irq_data);
	if (WARN_ON(!irq))
		irq = hwirq;

	pr_info("%s: hwirq %d, irq %d\n", __func__, hwirq, irq);

	return irq;
}

/* Brute-force probing for xilinx DMA
 */
static int xdma_probe(struct platform_device *pdev)
{
	struct xdma_device *xdev;
	struct resource *res;
	int err, i, j;
	struct xdma_chan *chan;
	struct xdma_device_config *dma_config;
	int dma_chan_dir;
	int dma_chan_reg_offset;

	pr_info("%s: probe dma %p, nres %d, id %d\n", __func__,
		&pdev->dev, pdev->num_resources, pdev->id);

	xdev = devm_kzalloc(&pdev->dev, sizeof(struct xdma_device), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;
	xdev->dev = &pdev->dev;

	/* Set this as configurable once HPC works */
	arch_setup_dma_ops(&pdev->dev, 0, 0, NULL, false);
	dma_set_mask(&pdev->dev, 0xFFFFFFFFFFFFFFFFull);

	dma_config = (struct xdma_device_config *)xdev->dev->platform_data;
	if (dma_config->channel_count < 1 || dma_config->channel_count > 2)
		return -EFAULT;

	/* Get the memory resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!xdev->regs) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		return -EFAULT;
	}

	dev_info(&pdev->dev, "AXIDMA device %d physical base address=%pa\n",
		 pdev->id, &res->start);
	dev_info(&pdev->dev, "AXIDMA device %d remapped to %pa\n",
		 pdev->id, &xdev->regs);

	/* Allocate the channels */

	dev_info(&pdev->dev, "has %d channel(s)\n", dma_config->channel_count);
	for (i = 0; i < dma_config->channel_count; i++) {
		chan = devm_kzalloc(&pdev->dev, sizeof(*chan), GFP_KERNEL);
		if (!chan)
			return -ENOMEM;

		dma_chan_dir = strcmp(dma_config->channel_config[i].type,
				      "axi-dma-mm2s-channel") ?
					DMA_FROM_DEVICE :
					DMA_TO_DEVICE;
		dma_chan_reg_offset = (dma_chan_dir == DMA_TO_DEVICE) ?
					0 :
					0x30;

		/* Initialize channel parameters */
		chan->id = i;
		chan->regs = xdev->regs + dma_chan_reg_offset;
		/* chan->regs = xdev->regs; */
		chan->dev = xdev->dev;
		chan->max_len = XDMA_MAX_TRANS_LEN;
		chan->direction = dma_chan_dir;
		sprintf(chan->name, "%s:%d", dma_config->name, chan->id);
		pr_info("  chan %d name: %s\n", chan->id, chan->name);
		pr_info("  chan %d direction: %s\n", chan->id,
			dma_chan_dir == DMA_FROM_DEVICE ?
				"FROM_DEVICE" : "TO_DEVICE");

		spin_lock_init(&chan->lock);
		tasklet_init(&chan->tasklet,
			     xdma_tasklet,
			     (unsigned long)chan);
		tasklet_init(&chan->dma_err_tasklet,
			     xdma_err_tasklet,
			     (unsigned long)chan);

		xdev->chan[chan->id] = chan;

		/* The IRQ resource */
		chan->irq = xlate_irq(dma_config->channel_config[i].irq);
		if (chan->irq <= 0) {
			pr_err("get_resource for IRQ for dev %d failed\n",
			       pdev->id);
			return -ENODEV;
		}

		err = devm_request_irq(&pdev->dev,
				       chan->irq,
				       dma_chan_dir == DMA_TO_DEVICE ?
					xdma_tx_intr_handler :
					xdma_rx_intr_handler,
				       IRQF_SHARED,
				       pdev->name,
				       chan);
		if (err) {
			dev_err(&pdev->dev, "unable to request IRQ\n");
			return err;
		}
		pr_info("  chan%d irq: %d\n", chan->id, chan->irq);

		chan->poll_mode = dma_config->channel_config[i].poll_mode;
		pr_info("  chan%d poll mode: %s\n",
			chan->id,
			chan->poll_mode ? "on" : "off");

		/* Allocate channel BD's */
		err = xdma_alloc_chan_descriptors(xdev->chan[chan->id]);
		if (err) {
			dev_err(&pdev->dev, "unable to allocate BD's\n");
			return -ENOMEM;
		}
		pr_info("  chan%d bd ring @ 0x%08x (size: 0x%08x bytes)\n",
			chan->id,
			chan->bd_phys_addr,
			chan->bd_chain_size);

		err = dma_init(xdev->chan[chan->id]);
		if (err) {
			dev_err(&pdev->dev, "DMA init failed\n");
			/* FIXME Check this - unregister all chan resources */
			for (j = 0; j <= i; j++)
				xdma_free_chan_resources(xdev->chan[j]);
			return -EIO;
		}
	}
	xdev->channel_count = dma_config->channel_count;
	pdev->dev.release = xdma_release;
	/* Add the DMA device to the global list */
	mutex_lock(&dma_list_mutex);
	list_add_tail(&xdev->node, &dma_device_list);
	mutex_unlock(&dma_list_mutex);

	platform_set_drvdata(pdev, xdev);

	return 0;
}

static int xdma_remove(struct platform_device *pdev)
{
	int i;
	struct xdma_device *xdev = platform_get_drvdata(pdev);

	/* Remove the DMA device from the global list */
	mutex_lock(&dma_list_mutex);
	list_del(&xdev->node);
	mutex_unlock(&dma_list_mutex);

	for (i = 0; i < XDMA_MAX_CHANS_PER_DEVICE; i++) {
		if (xdev->chan[i])
			xdma_chan_remove(xdev->chan[i]);
	}

	return 0;
}

static struct platform_driver xdma_driver = {
	.probe = xdma_probe,
	.remove = xdma_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx-axidma",
	},
};

module_platform_driver(xdma_driver);

MODULE_DESCRIPTION("Xilinx DMA driver");
MODULE_LICENSE("GPL");
