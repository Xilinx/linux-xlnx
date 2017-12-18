/*
 * talitos - Freescale Integrated Security Engine (SEC) device driver
 *
 * Copyright (c) 2008-2011 Freescale Semiconductor, Inc.
 *
 * Scatterlist Crypto API glue code copied from files with the following:
 * Copyright (c) 2006-2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * Crypto algorithm registration code copied from hifn driver:
 * 2007+ Copyright (c) Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/crypto.h>
#include <linux/hw_random.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/sha.h>
#include <crypto/md5.h>
#include <crypto/internal/aead.h>
#include <crypto/authenc.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>

#include "talitos.h"

static void to_talitos_ptr(struct talitos_ptr *ptr, dma_addr_t dma_addr,
			   bool is_sec1)
{
	ptr->ptr = cpu_to_be32(lower_32_bits(dma_addr));
	if (!is_sec1)
		ptr->eptr = upper_32_bits(dma_addr);
}

static void copy_talitos_ptr(struct talitos_ptr *dst_ptr,
			     struct talitos_ptr *src_ptr, bool is_sec1)
{
	dst_ptr->ptr = src_ptr->ptr;
	if (!is_sec1)
		dst_ptr->eptr = src_ptr->eptr;
}

static void to_talitos_ptr_len(struct talitos_ptr *ptr, unsigned int len,
			       bool is_sec1)
{
	if (is_sec1) {
		ptr->res = 0;
		ptr->len1 = cpu_to_be16(len);
	} else {
		ptr->len = cpu_to_be16(len);
	}
}

static unsigned short from_talitos_ptr_len(struct talitos_ptr *ptr,
					   bool is_sec1)
{
	if (is_sec1)
		return be16_to_cpu(ptr->len1);
	else
		return be16_to_cpu(ptr->len);
}

static void to_talitos_ptr_ext_set(struct talitos_ptr *ptr, u8 val,
				   bool is_sec1)
{
	if (!is_sec1)
		ptr->j_extent = val;
}

static void to_talitos_ptr_ext_or(struct talitos_ptr *ptr, u8 val, bool is_sec1)
{
	if (!is_sec1)
		ptr->j_extent |= val;
}

/*
 * map virtual single (contiguous) pointer to h/w descriptor pointer
 */
static void map_single_talitos_ptr(struct device *dev,
				   struct talitos_ptr *ptr,
				   unsigned int len, void *data,
				   enum dma_data_direction dir)
{
	dma_addr_t dma_addr = dma_map_single(dev, data, len, dir);
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	to_talitos_ptr_len(ptr, len, is_sec1);
	to_talitos_ptr(ptr, dma_addr, is_sec1);
	to_talitos_ptr_ext_set(ptr, 0, is_sec1);
}

/*
 * unmap bus single (contiguous) h/w descriptor pointer
 */
static void unmap_single_talitos_ptr(struct device *dev,
				     struct talitos_ptr *ptr,
				     enum dma_data_direction dir)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	dma_unmap_single(dev, be32_to_cpu(ptr->ptr),
			 from_talitos_ptr_len(ptr, is_sec1), dir);
}

static int reset_channel(struct device *dev, int ch)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	unsigned int timeout = TALITOS_TIMEOUT;
	bool is_sec1 = has_ftr_sec1(priv);

	if (is_sec1) {
		setbits32(priv->chan[ch].reg + TALITOS_CCCR_LO,
			  TALITOS1_CCCR_LO_RESET);

		while ((in_be32(priv->chan[ch].reg + TALITOS_CCCR_LO) &
			TALITOS1_CCCR_LO_RESET) && --timeout)
			cpu_relax();
	} else {
		setbits32(priv->chan[ch].reg + TALITOS_CCCR,
			  TALITOS2_CCCR_RESET);

		while ((in_be32(priv->chan[ch].reg + TALITOS_CCCR) &
			TALITOS2_CCCR_RESET) && --timeout)
			cpu_relax();
	}

	if (timeout == 0) {
		dev_err(dev, "failed to reset channel %d\n", ch);
		return -EIO;
	}

	/* set 36-bit addressing, done writeback enable and done IRQ enable */
	setbits32(priv->chan[ch].reg + TALITOS_CCCR_LO, TALITOS_CCCR_LO_EAE |
		  TALITOS_CCCR_LO_CDWE | TALITOS_CCCR_LO_CDIE);

	/* and ICCR writeback, if available */
	if (priv->features & TALITOS_FTR_HW_AUTH_CHECK)
		setbits32(priv->chan[ch].reg + TALITOS_CCCR_LO,
		          TALITOS_CCCR_LO_IWSE);

	return 0;
}

static int reset_device(struct device *dev)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	unsigned int timeout = TALITOS_TIMEOUT;
	bool is_sec1 = has_ftr_sec1(priv);
	u32 mcr = is_sec1 ? TALITOS1_MCR_SWR : TALITOS2_MCR_SWR;

	setbits32(priv->reg + TALITOS_MCR, mcr);

	while ((in_be32(priv->reg + TALITOS_MCR) & mcr)
	       && --timeout)
		cpu_relax();

	if (priv->irq[1]) {
		mcr = TALITOS_MCR_RCA1 | TALITOS_MCR_RCA3;
		setbits32(priv->reg + TALITOS_MCR, mcr);
	}

	if (timeout == 0) {
		dev_err(dev, "failed to reset device\n");
		return -EIO;
	}

	return 0;
}

/*
 * Reset and initialize the device
 */
static int init_device(struct device *dev)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	int ch, err;
	bool is_sec1 = has_ftr_sec1(priv);

	/*
	 * Master reset
	 * errata documentation: warning: certain SEC interrupts
	 * are not fully cleared by writing the MCR:SWR bit,
	 * set bit twice to completely reset
	 */
	err = reset_device(dev);
	if (err)
		return err;

	err = reset_device(dev);
	if (err)
		return err;

	/* reset channels */
	for (ch = 0; ch < priv->num_channels; ch++) {
		err = reset_channel(dev, ch);
		if (err)
			return err;
	}

	/* enable channel done and error interrupts */
	if (is_sec1) {
		clrbits32(priv->reg + TALITOS_IMR, TALITOS1_IMR_INIT);
		clrbits32(priv->reg + TALITOS_IMR_LO, TALITOS1_IMR_LO_INIT);
		/* disable parity error check in DEU (erroneous? test vect.) */
		setbits32(priv->reg_deu + TALITOS_EUICR, TALITOS1_DEUICR_KPE);
	} else {
		setbits32(priv->reg + TALITOS_IMR, TALITOS2_IMR_INIT);
		setbits32(priv->reg + TALITOS_IMR_LO, TALITOS2_IMR_LO_INIT);
	}

	/* disable integrity check error interrupts (use writeback instead) */
	if (priv->features & TALITOS_FTR_HW_AUTH_CHECK)
		setbits32(priv->reg_mdeu + TALITOS_EUICR_LO,
		          TALITOS_MDEUICR_LO_ICE);

	return 0;
}

/**
 * talitos_submit - submits a descriptor to the device for processing
 * @dev:	the SEC device to be used
 * @ch:		the SEC device channel to be used
 * @desc:	the descriptor to be processed by the device
 * @callback:	whom to call when processing is complete
 * @context:	a handle for use by caller (optional)
 *
 * desc must contain valid dma-mapped (bus physical) address pointers.
 * callback must check err and feedback in descriptor header
 * for device processing status.
 */
int talitos_submit(struct device *dev, int ch, struct talitos_desc *desc,
		   void (*callback)(struct device *dev,
				    struct talitos_desc *desc,
				    void *context, int error),
		   void *context)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	struct talitos_request *request;
	unsigned long flags;
	int head;
	bool is_sec1 = has_ftr_sec1(priv);

	spin_lock_irqsave(&priv->chan[ch].head_lock, flags);

	if (!atomic_inc_not_zero(&priv->chan[ch].submit_count)) {
		/* h/w fifo is full */
		spin_unlock_irqrestore(&priv->chan[ch].head_lock, flags);
		return -EAGAIN;
	}

	head = priv->chan[ch].head;
	request = &priv->chan[ch].fifo[head];

	/* map descriptor and save caller data */
	if (is_sec1) {
		desc->hdr1 = desc->hdr;
		desc->next_desc = 0;
		request->dma_desc = dma_map_single(dev, &desc->hdr1,
						   TALITOS_DESC_SIZE,
						   DMA_BIDIRECTIONAL);
	} else {
		request->dma_desc = dma_map_single(dev, desc,
						   TALITOS_DESC_SIZE,
						   DMA_BIDIRECTIONAL);
	}
	request->callback = callback;
	request->context = context;

	/* increment fifo head */
	priv->chan[ch].head = (priv->chan[ch].head + 1) & (priv->fifo_len - 1);

	smp_wmb();
	request->desc = desc;

	/* GO! */
	wmb();
	out_be32(priv->chan[ch].reg + TALITOS_FF,
		 upper_32_bits(request->dma_desc));
	out_be32(priv->chan[ch].reg + TALITOS_FF_LO,
		 lower_32_bits(request->dma_desc));

	spin_unlock_irqrestore(&priv->chan[ch].head_lock, flags);

	return -EINPROGRESS;
}
EXPORT_SYMBOL(talitos_submit);

/*
 * process what was done, notify callback of error if not
 */
static void flush_channel(struct device *dev, int ch, int error, int reset_ch)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	struct talitos_request *request, saved_req;
	unsigned long flags;
	int tail, status;
	bool is_sec1 = has_ftr_sec1(priv);

	spin_lock_irqsave(&priv->chan[ch].tail_lock, flags);

	tail = priv->chan[ch].tail;
	while (priv->chan[ch].fifo[tail].desc) {
		__be32 hdr;

		request = &priv->chan[ch].fifo[tail];

		/* descriptors with their done bits set don't get the error */
		rmb();
		hdr = is_sec1 ? request->desc->hdr1 : request->desc->hdr;

		if ((hdr & DESC_HDR_DONE) == DESC_HDR_DONE)
			status = 0;
		else
			if (!error)
				break;
			else
				status = error;

		dma_unmap_single(dev, request->dma_desc,
				 TALITOS_DESC_SIZE,
				 DMA_BIDIRECTIONAL);

		/* copy entries so we can call callback outside lock */
		saved_req.desc = request->desc;
		saved_req.callback = request->callback;
		saved_req.context = request->context;

		/* release request entry in fifo */
		smp_wmb();
		request->desc = NULL;

		/* increment fifo tail */
		priv->chan[ch].tail = (tail + 1) & (priv->fifo_len - 1);

		spin_unlock_irqrestore(&priv->chan[ch].tail_lock, flags);

		atomic_dec(&priv->chan[ch].submit_count);

		saved_req.callback(dev, saved_req.desc, saved_req.context,
				   status);
		/* channel may resume processing in single desc error case */
		if (error && !reset_ch && status == error)
			return;
		spin_lock_irqsave(&priv->chan[ch].tail_lock, flags);
		tail = priv->chan[ch].tail;
	}

	spin_unlock_irqrestore(&priv->chan[ch].tail_lock, flags);
}

/*
 * process completed requests for channels that have done status
 */
#define DEF_TALITOS1_DONE(name, ch_done_mask)				\
static void talitos1_done_##name(unsigned long data)			\
{									\
	struct device *dev = (struct device *)data;			\
	struct talitos_private *priv = dev_get_drvdata(dev);		\
	unsigned long flags;						\
									\
	if (ch_done_mask & 0x10000000)					\
		flush_channel(dev, 0, 0, 0);			\
	if (priv->num_channels == 1)					\
		goto out;						\
	if (ch_done_mask & 0x40000000)					\
		flush_channel(dev, 1, 0, 0);			\
	if (ch_done_mask & 0x00010000)					\
		flush_channel(dev, 2, 0, 0);			\
	if (ch_done_mask & 0x00040000)					\
		flush_channel(dev, 3, 0, 0);			\
									\
out:									\
	/* At this point, all completed channels have been processed */	\
	/* Unmask done interrupts for channels completed later on. */	\
	spin_lock_irqsave(&priv->reg_lock, flags);			\
	clrbits32(priv->reg + TALITOS_IMR, ch_done_mask);		\
	clrbits32(priv->reg + TALITOS_IMR_LO, TALITOS1_IMR_LO_INIT);	\
	spin_unlock_irqrestore(&priv->reg_lock, flags);			\
}

DEF_TALITOS1_DONE(4ch, TALITOS1_ISR_4CHDONE)

#define DEF_TALITOS2_DONE(name, ch_done_mask)				\
static void talitos2_done_##name(unsigned long data)			\
{									\
	struct device *dev = (struct device *)data;			\
	struct talitos_private *priv = dev_get_drvdata(dev);		\
	unsigned long flags;						\
									\
	if (ch_done_mask & 1)						\
		flush_channel(dev, 0, 0, 0);				\
	if (priv->num_channels == 1)					\
		goto out;						\
	if (ch_done_mask & (1 << 2))					\
		flush_channel(dev, 1, 0, 0);				\
	if (ch_done_mask & (1 << 4))					\
		flush_channel(dev, 2, 0, 0);				\
	if (ch_done_mask & (1 << 6))					\
		flush_channel(dev, 3, 0, 0);				\
									\
out:									\
	/* At this point, all completed channels have been processed */	\
	/* Unmask done interrupts for channels completed later on. */	\
	spin_lock_irqsave(&priv->reg_lock, flags);			\
	setbits32(priv->reg + TALITOS_IMR, ch_done_mask);		\
	setbits32(priv->reg + TALITOS_IMR_LO, TALITOS2_IMR_LO_INIT);	\
	spin_unlock_irqrestore(&priv->reg_lock, flags);			\
}

DEF_TALITOS2_DONE(4ch, TALITOS2_ISR_4CHDONE)
DEF_TALITOS2_DONE(ch0_2, TALITOS2_ISR_CH_0_2_DONE)
DEF_TALITOS2_DONE(ch1_3, TALITOS2_ISR_CH_1_3_DONE)

/*
 * locate current (offending) descriptor
 */
static u32 current_desc_hdr(struct device *dev, int ch)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	int tail, iter;
	dma_addr_t cur_desc;

	cur_desc = ((u64)in_be32(priv->chan[ch].reg + TALITOS_CDPR)) << 32;
	cur_desc |= in_be32(priv->chan[ch].reg + TALITOS_CDPR_LO);

	if (!cur_desc) {
		dev_err(dev, "CDPR is NULL, giving up search for offending descriptor\n");
		return 0;
	}

	tail = priv->chan[ch].tail;

	iter = tail;
	while (priv->chan[ch].fifo[iter].dma_desc != cur_desc) {
		iter = (iter + 1) & (priv->fifo_len - 1);
		if (iter == tail) {
			dev_err(dev, "couldn't locate current descriptor\n");
			return 0;
		}
	}

	return priv->chan[ch].fifo[iter].desc->hdr;
}

/*
 * user diagnostics; report root cause of error based on execution unit status
 */
static void report_eu_error(struct device *dev, int ch, u32 desc_hdr)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	int i;

	if (!desc_hdr)
		desc_hdr = in_be32(priv->chan[ch].reg + TALITOS_DESCBUF);

	switch (desc_hdr & DESC_HDR_SEL0_MASK) {
	case DESC_HDR_SEL0_AFEU:
		dev_err(dev, "AFEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_afeu + TALITOS_EUISR),
			in_be32(priv->reg_afeu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_DEU:
		dev_err(dev, "DEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_deu + TALITOS_EUISR),
			in_be32(priv->reg_deu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_MDEUA:
	case DESC_HDR_SEL0_MDEUB:
		dev_err(dev, "MDEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_mdeu + TALITOS_EUISR),
			in_be32(priv->reg_mdeu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_RNG:
		dev_err(dev, "RNGUISR 0x%08x_%08x\n",
			in_be32(priv->reg_rngu + TALITOS_ISR),
			in_be32(priv->reg_rngu + TALITOS_ISR_LO));
		break;
	case DESC_HDR_SEL0_PKEU:
		dev_err(dev, "PKEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_pkeu + TALITOS_EUISR),
			in_be32(priv->reg_pkeu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_AESU:
		dev_err(dev, "AESUISR 0x%08x_%08x\n",
			in_be32(priv->reg_aesu + TALITOS_EUISR),
			in_be32(priv->reg_aesu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_CRCU:
		dev_err(dev, "CRCUISR 0x%08x_%08x\n",
			in_be32(priv->reg_crcu + TALITOS_EUISR),
			in_be32(priv->reg_crcu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL0_KEU:
		dev_err(dev, "KEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_pkeu + TALITOS_EUISR),
			in_be32(priv->reg_pkeu + TALITOS_EUISR_LO));
		break;
	}

	switch (desc_hdr & DESC_HDR_SEL1_MASK) {
	case DESC_HDR_SEL1_MDEUA:
	case DESC_HDR_SEL1_MDEUB:
		dev_err(dev, "MDEUISR 0x%08x_%08x\n",
			in_be32(priv->reg_mdeu + TALITOS_EUISR),
			in_be32(priv->reg_mdeu + TALITOS_EUISR_LO));
		break;
	case DESC_HDR_SEL1_CRCU:
		dev_err(dev, "CRCUISR 0x%08x_%08x\n",
			in_be32(priv->reg_crcu + TALITOS_EUISR),
			in_be32(priv->reg_crcu + TALITOS_EUISR_LO));
		break;
	}

	for (i = 0; i < 8; i++)
		dev_err(dev, "DESCBUF 0x%08x_%08x\n",
			in_be32(priv->chan[ch].reg + TALITOS_DESCBUF + 8*i),
			in_be32(priv->chan[ch].reg + TALITOS_DESCBUF_LO + 8*i));
}

/*
 * recover from error interrupts
 */
static void talitos_error(struct device *dev, u32 isr, u32 isr_lo)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	unsigned int timeout = TALITOS_TIMEOUT;
	int ch, error, reset_dev = 0;
	u32 v_lo;
	bool is_sec1 = has_ftr_sec1(priv);
	int reset_ch = is_sec1 ? 1 : 0; /* only SEC2 supports continuation */

	for (ch = 0; ch < priv->num_channels; ch++) {
		/* skip channels without errors */
		if (is_sec1) {
			/* bits 29, 31, 17, 19 */
			if (!(isr & (1 << (29 + (ch & 1) * 2 - (ch & 2) * 6))))
				continue;
		} else {
			if (!(isr & (1 << (ch * 2 + 1))))
				continue;
		}

		error = -EINVAL;

		v_lo = in_be32(priv->chan[ch].reg + TALITOS_CCPSR_LO);

		if (v_lo & TALITOS_CCPSR_LO_DOF) {
			dev_err(dev, "double fetch fifo overflow error\n");
			error = -EAGAIN;
			reset_ch = 1;
		}
		if (v_lo & TALITOS_CCPSR_LO_SOF) {
			/* h/w dropped descriptor */
			dev_err(dev, "single fetch fifo overflow error\n");
			error = -EAGAIN;
		}
		if (v_lo & TALITOS_CCPSR_LO_MDTE)
			dev_err(dev, "master data transfer error\n");
		if (v_lo & TALITOS_CCPSR_LO_SGDLZ)
			dev_err(dev, is_sec1 ? "pointeur not complete error\n"
					     : "s/g data length zero error\n");
		if (v_lo & TALITOS_CCPSR_LO_FPZ)
			dev_err(dev, is_sec1 ? "parity error\n"
					     : "fetch pointer zero error\n");
		if (v_lo & TALITOS_CCPSR_LO_IDH)
			dev_err(dev, "illegal descriptor header error\n");
		if (v_lo & TALITOS_CCPSR_LO_IEU)
			dev_err(dev, is_sec1 ? "static assignment error\n"
					     : "invalid exec unit error\n");
		if (v_lo & TALITOS_CCPSR_LO_EU)
			report_eu_error(dev, ch, current_desc_hdr(dev, ch));
		if (!is_sec1) {
			if (v_lo & TALITOS_CCPSR_LO_GB)
				dev_err(dev, "gather boundary error\n");
			if (v_lo & TALITOS_CCPSR_LO_GRL)
				dev_err(dev, "gather return/length error\n");
			if (v_lo & TALITOS_CCPSR_LO_SB)
				dev_err(dev, "scatter boundary error\n");
			if (v_lo & TALITOS_CCPSR_LO_SRL)
				dev_err(dev, "scatter return/length error\n");
		}

		flush_channel(dev, ch, error, reset_ch);

		if (reset_ch) {
			reset_channel(dev, ch);
		} else {
			setbits32(priv->chan[ch].reg + TALITOS_CCCR,
				  TALITOS2_CCCR_CONT);
			setbits32(priv->chan[ch].reg + TALITOS_CCCR_LO, 0);
			while ((in_be32(priv->chan[ch].reg + TALITOS_CCCR) &
			       TALITOS2_CCCR_CONT) && --timeout)
				cpu_relax();
			if (timeout == 0) {
				dev_err(dev, "failed to restart channel %d\n",
					ch);
				reset_dev = 1;
			}
		}
	}
	if (reset_dev || (is_sec1 && isr & ~TALITOS1_ISR_4CHERR) ||
	    (!is_sec1 && isr & ~TALITOS2_ISR_4CHERR) || isr_lo) {
		if (is_sec1 && (isr_lo & TALITOS1_ISR_TEA_ERR))
			dev_err(dev, "TEA error: ISR 0x%08x_%08x\n",
				isr, isr_lo);
		else
			dev_err(dev, "done overflow, internal time out, or "
				"rngu error: ISR 0x%08x_%08x\n", isr, isr_lo);

		/* purge request queues */
		for (ch = 0; ch < priv->num_channels; ch++)
			flush_channel(dev, ch, -EIO, 1);

		/* reset and reinitialize the device */
		init_device(dev);
	}
}

#define DEF_TALITOS1_INTERRUPT(name, ch_done_mask, ch_err_mask, tlet)	       \
static irqreturn_t talitos1_interrupt_##name(int irq, void *data)	       \
{									       \
	struct device *dev = data;					       \
	struct talitos_private *priv = dev_get_drvdata(dev);		       \
	u32 isr, isr_lo;						       \
	unsigned long flags;						       \
									       \
	spin_lock_irqsave(&priv->reg_lock, flags);			       \
	isr = in_be32(priv->reg + TALITOS_ISR);				       \
	isr_lo = in_be32(priv->reg + TALITOS_ISR_LO);			       \
	/* Acknowledge interrupt */					       \
	out_be32(priv->reg + TALITOS_ICR, isr & (ch_done_mask | ch_err_mask)); \
	out_be32(priv->reg + TALITOS_ICR_LO, isr_lo);			       \
									       \
	if (unlikely(isr & ch_err_mask || isr_lo & TALITOS1_IMR_LO_INIT)) {    \
		spin_unlock_irqrestore(&priv->reg_lock, flags);		       \
		talitos_error(dev, isr & ch_err_mask, isr_lo);		       \
	}								       \
	else {								       \
		if (likely(isr & ch_done_mask)) {			       \
			/* mask further done interrupts. */		       \
			setbits32(priv->reg + TALITOS_IMR, ch_done_mask);      \
			/* done_task will unmask done interrupts at exit */    \
			tasklet_schedule(&priv->done_task[tlet]);	       \
		}							       \
		spin_unlock_irqrestore(&priv->reg_lock, flags);		       \
	}								       \
									       \
	return (isr & (ch_done_mask | ch_err_mask) || isr_lo) ? IRQ_HANDLED :  \
								IRQ_NONE;      \
}

DEF_TALITOS1_INTERRUPT(4ch, TALITOS1_ISR_4CHDONE, TALITOS1_ISR_4CHERR, 0)

#define DEF_TALITOS2_INTERRUPT(name, ch_done_mask, ch_err_mask, tlet)	       \
static irqreturn_t talitos2_interrupt_##name(int irq, void *data)	       \
{									       \
	struct device *dev = data;					       \
	struct talitos_private *priv = dev_get_drvdata(dev);		       \
	u32 isr, isr_lo;						       \
	unsigned long flags;						       \
									       \
	spin_lock_irqsave(&priv->reg_lock, flags);			       \
	isr = in_be32(priv->reg + TALITOS_ISR);				       \
	isr_lo = in_be32(priv->reg + TALITOS_ISR_LO);			       \
	/* Acknowledge interrupt */					       \
	out_be32(priv->reg + TALITOS_ICR, isr & (ch_done_mask | ch_err_mask)); \
	out_be32(priv->reg + TALITOS_ICR_LO, isr_lo);			       \
									       \
	if (unlikely(isr & ch_err_mask || isr_lo)) {			       \
		spin_unlock_irqrestore(&priv->reg_lock, flags);		       \
		talitos_error(dev, isr & ch_err_mask, isr_lo);		       \
	}								       \
	else {								       \
		if (likely(isr & ch_done_mask)) {			       \
			/* mask further done interrupts. */		       \
			clrbits32(priv->reg + TALITOS_IMR, ch_done_mask);      \
			/* done_task will unmask done interrupts at exit */    \
			tasklet_schedule(&priv->done_task[tlet]);	       \
		}							       \
		spin_unlock_irqrestore(&priv->reg_lock, flags);		       \
	}								       \
									       \
	return (isr & (ch_done_mask | ch_err_mask) || isr_lo) ? IRQ_HANDLED :  \
								IRQ_NONE;      \
}

DEF_TALITOS2_INTERRUPT(4ch, TALITOS2_ISR_4CHDONE, TALITOS2_ISR_4CHERR, 0)
DEF_TALITOS2_INTERRUPT(ch0_2, TALITOS2_ISR_CH_0_2_DONE, TALITOS2_ISR_CH_0_2_ERR,
		       0)
DEF_TALITOS2_INTERRUPT(ch1_3, TALITOS2_ISR_CH_1_3_DONE, TALITOS2_ISR_CH_1_3_ERR,
		       1)

/*
 * hwrng
 */
static int talitos_rng_data_present(struct hwrng *rng, int wait)
{
	struct device *dev = (struct device *)rng->priv;
	struct talitos_private *priv = dev_get_drvdata(dev);
	u32 ofl;
	int i;

	for (i = 0; i < 20; i++) {
		ofl = in_be32(priv->reg_rngu + TALITOS_EUSR_LO) &
		      TALITOS_RNGUSR_LO_OFL;
		if (ofl || !wait)
			break;
		udelay(10);
	}

	return !!ofl;
}

static int talitos_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct device *dev = (struct device *)rng->priv;
	struct talitos_private *priv = dev_get_drvdata(dev);

	/* rng fifo requires 64-bit accesses */
	*data = in_be32(priv->reg_rngu + TALITOS_EU_FIFO);
	*data = in_be32(priv->reg_rngu + TALITOS_EU_FIFO_LO);

	return sizeof(u32);
}

static int talitos_rng_init(struct hwrng *rng)
{
	struct device *dev = (struct device *)rng->priv;
	struct talitos_private *priv = dev_get_drvdata(dev);
	unsigned int timeout = TALITOS_TIMEOUT;

	setbits32(priv->reg_rngu + TALITOS_EURCR_LO, TALITOS_RNGURCR_LO_SR);
	while (!(in_be32(priv->reg_rngu + TALITOS_EUSR_LO)
		 & TALITOS_RNGUSR_LO_RD)
	       && --timeout)
		cpu_relax();
	if (timeout == 0) {
		dev_err(dev, "failed to reset rng hw\n");
		return -ENODEV;
	}

	/* start generating */
	setbits32(priv->reg_rngu + TALITOS_EUDSR_LO, 0);

	return 0;
}

static int talitos_register_rng(struct device *dev)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	int err;

	priv->rng.name		= dev_driver_string(dev),
	priv->rng.init		= talitos_rng_init,
	priv->rng.data_present	= talitos_rng_data_present,
	priv->rng.data_read	= talitos_rng_data_read,
	priv->rng.priv		= (unsigned long)dev;

	err = hwrng_register(&priv->rng);
	if (!err)
		priv->rng_registered = true;

	return err;
}

static void talitos_unregister_rng(struct device *dev)
{
	struct talitos_private *priv = dev_get_drvdata(dev);

	if (!priv->rng_registered)
		return;

	hwrng_unregister(&priv->rng);
	priv->rng_registered = false;
}

/*
 * crypto alg
 */
#define TALITOS_CRA_PRIORITY		3000
/*
 * Defines a priority for doing AEAD with descriptors type
 * HMAC_SNOOP_NO_AFEA (HSNA) instead of type IPSEC_ESP
 */
#define TALITOS_CRA_PRIORITY_AEAD_HSNA	(TALITOS_CRA_PRIORITY - 1)
#define TALITOS_MAX_KEY_SIZE		96
#define TALITOS_MAX_IV_LENGTH		16 /* max of AES_BLOCK_SIZE, DES3_EDE_BLOCK_SIZE */

struct talitos_ctx {
	struct device *dev;
	int ch;
	__be32 desc_hdr_template;
	u8 key[TALITOS_MAX_KEY_SIZE];
	u8 iv[TALITOS_MAX_IV_LENGTH];
	unsigned int keylen;
	unsigned int enckeylen;
	unsigned int authkeylen;
};

#define HASH_MAX_BLOCK_SIZE		SHA512_BLOCK_SIZE
#define TALITOS_MDEU_MAX_CONTEXT_SIZE	TALITOS_MDEU_CONTEXT_SIZE_SHA384_SHA512

struct talitos_ahash_req_ctx {
	u32 hw_context[TALITOS_MDEU_MAX_CONTEXT_SIZE / sizeof(u32)];
	unsigned int hw_context_size;
	u8 buf[HASH_MAX_BLOCK_SIZE];
	u8 bufnext[HASH_MAX_BLOCK_SIZE];
	unsigned int swinit;
	unsigned int first;
	unsigned int last;
	unsigned int to_hash_later;
	unsigned int nbuf;
	struct scatterlist bufsl[2];
	struct scatterlist *psrc;
};

struct talitos_export_state {
	u32 hw_context[TALITOS_MDEU_MAX_CONTEXT_SIZE / sizeof(u32)];
	u8 buf[HASH_MAX_BLOCK_SIZE];
	unsigned int swinit;
	unsigned int first;
	unsigned int last;
	unsigned int to_hash_later;
	unsigned int nbuf;
};

static int aead_setkey(struct crypto_aead *authenc,
		       const u8 *key, unsigned int keylen)
{
	struct talitos_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_authenc_keys keys;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		goto badkey;

	if (keys.authkeylen + keys.enckeylen > TALITOS_MAX_KEY_SIZE)
		goto badkey;

	memcpy(ctx->key, keys.authkey, keys.authkeylen);
	memcpy(&ctx->key[keys.authkeylen], keys.enckey, keys.enckeylen);

	ctx->keylen = keys.authkeylen + keys.enckeylen;
	ctx->enckeylen = keys.enckeylen;
	ctx->authkeylen = keys.authkeylen;

	return 0;

badkey:
	crypto_aead_set_flags(authenc, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

/*
 * talitos_edesc - s/w-extended descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @icv_ool: whether ICV is out-of-line
 * @iv_dma: dma address of iv for checking continuity and link table
 * @dma_len: length of dma mapped link_tbl space
 * @dma_link_tbl: bus physical address of link_tbl/buf
 * @desc: h/w descriptor
 * @link_tbl: input and output h/w link tables (if {src,dst}_nents > 1) (SEC2)
 * @buf: input and output buffeur (if {src,dst}_nents > 1) (SEC1)
 *
 * if decrypting (with authcheck), or either one of src_nents or dst_nents
 * is greater than 1, an integrity check value is concatenated to the end
 * of link_tbl data
 */
struct talitos_edesc {
	int src_nents;
	int dst_nents;
	bool icv_ool;
	dma_addr_t iv_dma;
	int dma_len;
	dma_addr_t dma_link_tbl;
	struct talitos_desc desc;
	union {
		struct talitos_ptr link_tbl[0];
		u8 buf[0];
	};
};

static void talitos_sg_unmap(struct device *dev,
			     struct talitos_edesc *edesc,
			     struct scatterlist *src,
			     struct scatterlist *dst,
			     unsigned int len, unsigned int offset)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);
	unsigned int src_nents = edesc->src_nents ? : 1;
	unsigned int dst_nents = edesc->dst_nents ? : 1;

	if (is_sec1 && dst && dst_nents > 1) {
		dma_sync_single_for_device(dev, edesc->dma_link_tbl + offset,
					   len, DMA_FROM_DEVICE);
		sg_pcopy_from_buffer(dst, dst_nents, edesc->buf + offset, len,
				     offset);
	}
	if (src != dst) {
		if (src_nents == 1 || !is_sec1)
			dma_unmap_sg(dev, src, src_nents, DMA_TO_DEVICE);

		if (dst && (dst_nents == 1 || !is_sec1))
			dma_unmap_sg(dev, dst, dst_nents, DMA_FROM_DEVICE);
	} else if (src_nents == 1 || !is_sec1) {
		dma_unmap_sg(dev, src, src_nents, DMA_BIDIRECTIONAL);
	}
}

static void ipsec_esp_unmap(struct device *dev,
			    struct talitos_edesc *edesc,
			    struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int ivsize = crypto_aead_ivsize(aead);

	if (edesc->desc.hdr & DESC_HDR_TYPE_IPSEC_ESP)
		unmap_single_talitos_ptr(dev, &edesc->desc.ptr[6],
					 DMA_FROM_DEVICE);
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[3], DMA_TO_DEVICE);
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[2], DMA_TO_DEVICE);
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[0], DMA_TO_DEVICE);

	talitos_sg_unmap(dev, edesc, areq->src, areq->dst, areq->cryptlen,
			 areq->assoclen);

	if (edesc->dma_len)
		dma_unmap_single(dev, edesc->dma_link_tbl, edesc->dma_len,
				 DMA_BIDIRECTIONAL);

	if (!(edesc->desc.hdr & DESC_HDR_TYPE_IPSEC_ESP)) {
		unsigned int dst_nents = edesc->dst_nents ? : 1;

		sg_pcopy_to_buffer(areq->dst, dst_nents, ctx->iv, ivsize,
				   areq->assoclen + areq->cryptlen - ivsize);
	}
}

/*
 * ipsec_esp descriptor callbacks
 */
static void ipsec_esp_encrypt_done(struct device *dev,
				   struct talitos_desc *desc, void *context,
				   int err)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);
	struct aead_request *areq = context;
	struct crypto_aead *authenc = crypto_aead_reqtfm(areq);
	unsigned int authsize = crypto_aead_authsize(authenc);
	struct talitos_edesc *edesc;
	struct scatterlist *sg;
	void *icvdata;

	edesc = container_of(desc, struct talitos_edesc, desc);

	ipsec_esp_unmap(dev, edesc, areq);

	/* copy the generated ICV to dst */
	if (edesc->icv_ool) {
		if (is_sec1)
			icvdata = edesc->buf + areq->assoclen + areq->cryptlen;
		else
			icvdata = &edesc->link_tbl[edesc->src_nents +
						   edesc->dst_nents + 2];
		sg = sg_last(areq->dst, edesc->dst_nents);
		memcpy((char *)sg_virt(sg) + sg->length - authsize,
		       icvdata, authsize);
	}

	kfree(edesc);

	aead_request_complete(areq, err);
}

static void ipsec_esp_decrypt_swauth_done(struct device *dev,
					  struct talitos_desc *desc,
					  void *context, int err)
{
	struct aead_request *req = context;
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(authenc);
	struct talitos_edesc *edesc;
	struct scatterlist *sg;
	char *oicv, *icv;
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	edesc = container_of(desc, struct talitos_edesc, desc);

	ipsec_esp_unmap(dev, edesc, req);

	if (!err) {
		/* auth check */
		sg = sg_last(req->dst, edesc->dst_nents ? : 1);
		icv = (char *)sg_virt(sg) + sg->length - authsize;

		if (edesc->dma_len) {
			if (is_sec1)
				oicv = (char *)&edesc->dma_link_tbl +
					       req->assoclen + req->cryptlen;
			else
				oicv = (char *)
				       &edesc->link_tbl[edesc->src_nents +
							edesc->dst_nents + 2];
			if (edesc->icv_ool)
				icv = oicv + authsize;
		} else
			oicv = (char *)&edesc->link_tbl[0];

		err = crypto_memneq(oicv, icv, authsize) ? -EBADMSG : 0;
	}

	kfree(edesc);

	aead_request_complete(req, err);
}

static void ipsec_esp_decrypt_hwauth_done(struct device *dev,
					  struct talitos_desc *desc,
					  void *context, int err)
{
	struct aead_request *req = context;
	struct talitos_edesc *edesc;

	edesc = container_of(desc, struct talitos_edesc, desc);

	ipsec_esp_unmap(dev, edesc, req);

	/* check ICV auth status */
	if (!err && ((desc->hdr_lo & DESC_HDR_LO_ICCR1_MASK) !=
		     DESC_HDR_LO_ICCR1_PASS))
		err = -EBADMSG;

	kfree(edesc);

	aead_request_complete(req, err);
}

/*
 * convert scatterlist to SEC h/w link table format
 * stop at cryptlen bytes
 */
static int sg_to_link_tbl_offset(struct scatterlist *sg, int sg_count,
				 unsigned int offset, int cryptlen,
				 struct talitos_ptr *link_tbl_ptr)
{
	int n_sg = sg_count;
	int count = 0;

	while (cryptlen && sg && n_sg--) {
		unsigned int len = sg_dma_len(sg);

		if (offset >= len) {
			offset -= len;
			goto next;
		}

		len -= offset;

		if (len > cryptlen)
			len = cryptlen;

		to_talitos_ptr(link_tbl_ptr + count,
			       sg_dma_address(sg) + offset, 0);
		to_talitos_ptr_len(link_tbl_ptr + count, len, 0);
		to_talitos_ptr_ext_set(link_tbl_ptr + count, 0, 0);
		count++;
		cryptlen -= len;
		offset = 0;

next:
		sg = sg_next(sg);
	}

	/* tag end of link table */
	if (count > 0)
		to_talitos_ptr_ext_set(link_tbl_ptr + count - 1,
				       DESC_PTR_LNKTBL_RETURN, 0);

	return count;
}

int talitos_sg_map(struct device *dev, struct scatterlist *src,
		   unsigned int len, struct talitos_edesc *edesc,
		   struct talitos_ptr *ptr,
		   int sg_count, unsigned int offset, int tbl_off)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	to_talitos_ptr_len(ptr, len, is_sec1);
	to_talitos_ptr_ext_set(ptr, 0, is_sec1);

	if (sg_count == 1) {
		to_talitos_ptr(ptr, sg_dma_address(src) + offset, is_sec1);
		return sg_count;
	}
	if (is_sec1) {
		to_talitos_ptr(ptr, edesc->dma_link_tbl + offset, is_sec1);
		return sg_count;
	}
	sg_count = sg_to_link_tbl_offset(src, sg_count, offset, len,
					 &edesc->link_tbl[tbl_off]);
	if (sg_count == 1) {
		/* Only one segment now, so no link tbl needed*/
		copy_talitos_ptr(ptr, &edesc->link_tbl[tbl_off], is_sec1);
		return sg_count;
	}
	to_talitos_ptr(ptr, edesc->dma_link_tbl +
			    tbl_off * sizeof(struct talitos_ptr), is_sec1);
	to_talitos_ptr_ext_or(ptr, DESC_PTR_LNKTBL_JUMP, is_sec1);

	return sg_count;
}

/*
 * fill in and submit ipsec_esp descriptor
 */
static int ipsec_esp(struct talitos_edesc *edesc, struct aead_request *areq,
		     void (*callback)(struct device *dev,
				      struct talitos_desc *desc,
				      void *context, int error))
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	unsigned int authsize = crypto_aead_authsize(aead);
	struct talitos_ctx *ctx = crypto_aead_ctx(aead);
	struct device *dev = ctx->dev;
	struct talitos_desc *desc = &edesc->desc;
	unsigned int cryptlen = areq->cryptlen;
	unsigned int ivsize = crypto_aead_ivsize(aead);
	int tbl_off = 0;
	int sg_count, ret;
	int sg_link_tbl_len;
	bool sync_needed = false;
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	/* hmac key */
	map_single_talitos_ptr(dev, &desc->ptr[0], ctx->authkeylen, &ctx->key,
			       DMA_TO_DEVICE);

	sg_count = edesc->src_nents ?: 1;
	if (is_sec1 && sg_count > 1)
		sg_copy_to_buffer(areq->src, sg_count, edesc->buf,
				  areq->assoclen + cryptlen);
	else
		sg_count = dma_map_sg(dev, areq->src, sg_count,
				      (areq->src == areq->dst) ?
				      DMA_BIDIRECTIONAL : DMA_TO_DEVICE);

	/* hmac data */
	ret = talitos_sg_map(dev, areq->src, areq->assoclen, edesc,
			     &desc->ptr[1], sg_count, 0, tbl_off);

	if (ret > 1) {
		tbl_off += ret;
		sync_needed = true;
	}

	/* cipher iv */
	if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP) {
		to_talitos_ptr(&desc->ptr[2], edesc->iv_dma, is_sec1);
		to_talitos_ptr_len(&desc->ptr[2], ivsize, is_sec1);
		to_talitos_ptr_ext_set(&desc->ptr[2], 0, is_sec1);
	} else {
		to_talitos_ptr(&desc->ptr[3], edesc->iv_dma, is_sec1);
		to_talitos_ptr_len(&desc->ptr[3], ivsize, is_sec1);
		to_talitos_ptr_ext_set(&desc->ptr[3], 0, is_sec1);
	}

	/* cipher key */
	if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP)
		map_single_talitos_ptr(dev, &desc->ptr[3], ctx->enckeylen,
				       (char *)&ctx->key + ctx->authkeylen,
				       DMA_TO_DEVICE);
	else
		map_single_talitos_ptr(dev, &desc->ptr[2], ctx->enckeylen,
				       (char *)&ctx->key + ctx->authkeylen,
				       DMA_TO_DEVICE);

	/*
	 * cipher in
	 * map and adjust cipher len to aead request cryptlen.
	 * extent is bytes of HMAC postpended to ciphertext,
	 * typically 12 for ipsec
	 */
	to_talitos_ptr_len(&desc->ptr[4], cryptlen, is_sec1);
	to_talitos_ptr_ext_set(&desc->ptr[4], 0, is_sec1);

	sg_link_tbl_len = cryptlen;

	if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP) {
		to_talitos_ptr_ext_set(&desc->ptr[4], authsize, is_sec1);

		if (edesc->desc.hdr & DESC_HDR_MODE1_MDEU_CICV)
			sg_link_tbl_len += authsize;
	}

	sg_count = talitos_sg_map(dev, areq->src, cryptlen, edesc,
				  &desc->ptr[4], sg_count, areq->assoclen,
				  tbl_off);

	if (sg_count > 1) {
		tbl_off += sg_count;
		sync_needed = true;
	}

	/* cipher out */
	if (areq->src != areq->dst) {
		sg_count = edesc->dst_nents ? : 1;
		if (!is_sec1 || sg_count == 1)
			dma_map_sg(dev, areq->dst, sg_count, DMA_FROM_DEVICE);
	}

	sg_count = talitos_sg_map(dev, areq->dst, cryptlen, edesc,
				  &desc->ptr[5], sg_count, areq->assoclen,
				  tbl_off);

	if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP)
		to_talitos_ptr_ext_or(&desc->ptr[5], authsize, is_sec1);

	if (sg_count > 1) {
		edesc->icv_ool = true;
		sync_needed = true;

		if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP) {
			struct talitos_ptr *tbl_ptr = &edesc->link_tbl[tbl_off];
			int offset = (edesc->src_nents + edesc->dst_nents + 2) *
				     sizeof(struct talitos_ptr) + authsize;

			/* Add an entry to the link table for ICV data */
			tbl_ptr += sg_count - 1;
			to_talitos_ptr_ext_set(tbl_ptr, 0, is_sec1);
			tbl_ptr++;
			to_talitos_ptr_ext_set(tbl_ptr, DESC_PTR_LNKTBL_RETURN,
					       is_sec1);
			to_talitos_ptr_len(tbl_ptr, authsize, is_sec1);

			/* icv data follows link tables */
			to_talitos_ptr(tbl_ptr, edesc->dma_link_tbl + offset,
				       is_sec1);
		}
	} else {
		edesc->icv_ool = false;
	}

	/* ICV data */
	if (!(desc->hdr & DESC_HDR_TYPE_IPSEC_ESP)) {
		to_talitos_ptr_len(&desc->ptr[6], authsize, is_sec1);
		to_talitos_ptr(&desc->ptr[6], edesc->dma_link_tbl +
			       areq->assoclen + cryptlen, is_sec1);
	}

	/* iv out */
	if (desc->hdr & DESC_HDR_TYPE_IPSEC_ESP)
		map_single_talitos_ptr(dev, &desc->ptr[6], ivsize, ctx->iv,
				       DMA_FROM_DEVICE);

	if (sync_needed)
		dma_sync_single_for_device(dev, edesc->dma_link_tbl,
					   edesc->dma_len,
					   DMA_BIDIRECTIONAL);

	ret = talitos_submit(dev, ctx->ch, desc, callback, areq);
	if (ret != -EINPROGRESS) {
		ipsec_esp_unmap(dev, edesc, areq);
		kfree(edesc);
	}
	return ret;
}

/*
 * allocate and map the extended descriptor
 */
static struct talitos_edesc *talitos_edesc_alloc(struct device *dev,
						 struct scatterlist *src,
						 struct scatterlist *dst,
						 u8 *iv,
						 unsigned int assoclen,
						 unsigned int cryptlen,
						 unsigned int authsize,
						 unsigned int ivsize,
						 int icv_stashing,
						 u32 cryptoflags,
						 bool encrypt)
{
	struct talitos_edesc *edesc;
	int src_nents, dst_nents, alloc_len, dma_len, src_len, dst_len;
	dma_addr_t iv_dma = 0;
	gfp_t flags = cryptoflags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
		      GFP_ATOMIC;
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);
	int max_len = is_sec1 ? TALITOS1_MAX_DATA_LEN : TALITOS2_MAX_DATA_LEN;
	void *err;

	if (cryptlen + authsize > max_len) {
		dev_err(dev, "length exceeds h/w max limit\n");
		return ERR_PTR(-EINVAL);
	}

	if (ivsize)
		iv_dma = dma_map_single(dev, iv, ivsize, DMA_TO_DEVICE);

	if (!dst || dst == src) {
		src_len = assoclen + cryptlen + authsize;
		src_nents = sg_nents_for_len(src, src_len);
		if (src_nents < 0) {
			dev_err(dev, "Invalid number of src SG.\n");
			err = ERR_PTR(-EINVAL);
			goto error_sg;
		}
		src_nents = (src_nents == 1) ? 0 : src_nents;
		dst_nents = dst ? src_nents : 0;
		dst_len = 0;
	} else { /* dst && dst != src*/
		src_len = assoclen + cryptlen + (encrypt ? 0 : authsize);
		src_nents = sg_nents_for_len(src, src_len);
		if (src_nents < 0) {
			dev_err(dev, "Invalid number of src SG.\n");
			err = ERR_PTR(-EINVAL);
			goto error_sg;
		}
		src_nents = (src_nents == 1) ? 0 : src_nents;
		dst_len = assoclen + cryptlen + (encrypt ? authsize : 0);
		dst_nents = sg_nents_for_len(dst, dst_len);
		if (dst_nents < 0) {
			dev_err(dev, "Invalid number of dst SG.\n");
			err = ERR_PTR(-EINVAL);
			goto error_sg;
		}
		dst_nents = (dst_nents == 1) ? 0 : dst_nents;
	}

	/*
	 * allocate space for base edesc plus the link tables,
	 * allowing for two separate entries for AD and generated ICV (+ 2),
	 * and space for two sets of ICVs (stashed and generated)
	 */
	alloc_len = sizeof(struct talitos_edesc);
	if (src_nents || dst_nents) {
		if (is_sec1)
			dma_len = (src_nents ? src_len : 0) +
				  (dst_nents ? dst_len : 0);
		else
			dma_len = (src_nents + dst_nents + 2) *
				  sizeof(struct talitos_ptr) + authsize * 2;
		alloc_len += dma_len;
	} else {
		dma_len = 0;
		alloc_len += icv_stashing ? authsize : 0;
	}

	edesc = kmalloc(alloc_len, GFP_DMA | flags);
	if (!edesc) {
		dev_err(dev, "could not allocate edescriptor\n");
		err = ERR_PTR(-ENOMEM);
		goto error_sg;
	}

	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;
	edesc->iv_dma = iv_dma;
	edesc->dma_len = dma_len;
	if (dma_len)
		edesc->dma_link_tbl = dma_map_single(dev, &edesc->link_tbl[0],
						     edesc->dma_len,
						     DMA_BIDIRECTIONAL);

	return edesc;
error_sg:
	if (iv_dma)
		dma_unmap_single(dev, iv_dma, ivsize, DMA_TO_DEVICE);
	return err;
}

static struct talitos_edesc *aead_edesc_alloc(struct aead_request *areq, u8 *iv,
					      int icv_stashing, bool encrypt)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(areq);
	unsigned int authsize = crypto_aead_authsize(authenc);
	struct talitos_ctx *ctx = crypto_aead_ctx(authenc);
	unsigned int ivsize = crypto_aead_ivsize(authenc);

	return talitos_edesc_alloc(ctx->dev, areq->src, areq->dst,
				   iv, areq->assoclen, areq->cryptlen,
				   authsize, ivsize, icv_stashing,
				   areq->base.flags, encrypt);
}

static int aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	struct talitos_ctx *ctx = crypto_aead_ctx(authenc);
	struct talitos_edesc *edesc;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, req->iv, 0, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* set encrypt */
	edesc->desc.hdr = ctx->desc_hdr_template | DESC_HDR_MODE0_ENCRYPT;

	return ipsec_esp(edesc, req, ipsec_esp_encrypt_done);
}

static int aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *authenc = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(authenc);
	struct talitos_ctx *ctx = crypto_aead_ctx(authenc);
	struct talitos_private *priv = dev_get_drvdata(ctx->dev);
	struct talitos_edesc *edesc;
	struct scatterlist *sg;
	void *icvdata;

	req->cryptlen -= authsize;

	/* allocate extended descriptor */
	edesc = aead_edesc_alloc(req, req->iv, 1, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	if ((priv->features & TALITOS_FTR_HW_AUTH_CHECK) &&
	    ((!edesc->src_nents && !edesc->dst_nents) ||
	     priv->features & TALITOS_FTR_SRC_LINK_TBL_LEN_INCLUDES_EXTENT)) {

		/* decrypt and check the ICV */
		edesc->desc.hdr = ctx->desc_hdr_template |
				  DESC_HDR_DIR_INBOUND |
				  DESC_HDR_MODE1_MDEU_CICV;

		/* reset integrity check result bits */
		edesc->desc.hdr_lo = 0;

		return ipsec_esp(edesc, req, ipsec_esp_decrypt_hwauth_done);
	}

	/* Have to check the ICV with software */
	edesc->desc.hdr = ctx->desc_hdr_template | DESC_HDR_DIR_INBOUND;

	/* stash incoming ICV for later cmp with ICV generated by the h/w */
	if (edesc->dma_len)
		icvdata = (char *)&edesc->link_tbl[edesc->src_nents +
						   edesc->dst_nents + 2];
	else
		icvdata = &edesc->link_tbl[0];

	sg = sg_last(req->src, edesc->src_nents ? : 1);

	memcpy(icvdata, (char *)sg_virt(sg) + sg->length - authsize, authsize);

	return ipsec_esp(edesc, req, ipsec_esp_decrypt_swauth_done);
}

static int ablkcipher_setkey(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	struct talitos_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	memcpy(&ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static void common_nonsnoop_unmap(struct device *dev,
				  struct talitos_edesc *edesc,
				  struct ablkcipher_request *areq)
{
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[5], DMA_FROM_DEVICE);

	talitos_sg_unmap(dev, edesc, areq->src, areq->dst, areq->nbytes, 0);
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[2], DMA_TO_DEVICE);
	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[1], DMA_TO_DEVICE);

	if (edesc->dma_len)
		dma_unmap_single(dev, edesc->dma_link_tbl, edesc->dma_len,
				 DMA_BIDIRECTIONAL);
}

static void ablkcipher_done(struct device *dev,
			    struct talitos_desc *desc, void *context,
			    int err)
{
	struct ablkcipher_request *areq = context;
	struct talitos_edesc *edesc;

	edesc = container_of(desc, struct talitos_edesc, desc);

	common_nonsnoop_unmap(dev, edesc, areq);

	kfree(edesc);

	areq->base.complete(&areq->base, err);
}

static int common_nonsnoop(struct talitos_edesc *edesc,
			   struct ablkcipher_request *areq,
			   void (*callback) (struct device *dev,
					     struct talitos_desc *desc,
					     void *context, int error))
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	struct device *dev = ctx->dev;
	struct talitos_desc *desc = &edesc->desc;
	unsigned int cryptlen = areq->nbytes;
	unsigned int ivsize = crypto_ablkcipher_ivsize(cipher);
	int sg_count, ret;
	bool sync_needed = false;
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	/* first DWORD empty */
	desc->ptr[0] = zero_entry;

	/* cipher iv */
	to_talitos_ptr(&desc->ptr[1], edesc->iv_dma, is_sec1);
	to_talitos_ptr_len(&desc->ptr[1], ivsize, is_sec1);
	to_talitos_ptr_ext_set(&desc->ptr[1], 0, is_sec1);

	/* cipher key */
	map_single_talitos_ptr(dev, &desc->ptr[2], ctx->keylen,
			       (char *)&ctx->key, DMA_TO_DEVICE);

	sg_count = edesc->src_nents ?: 1;
	if (is_sec1 && sg_count > 1)
		sg_copy_to_buffer(areq->src, sg_count, edesc->buf,
				  cryptlen);
	else
		sg_count = dma_map_sg(dev, areq->src, sg_count,
				      (areq->src == areq->dst) ?
				      DMA_BIDIRECTIONAL : DMA_TO_DEVICE);
	/*
	 * cipher in
	 */
	sg_count = talitos_sg_map(dev, areq->src, cryptlen, edesc,
				  &desc->ptr[3], sg_count, 0, 0);
	if (sg_count > 1)
		sync_needed = true;

	/* cipher out */
	if (areq->src != areq->dst) {
		sg_count = edesc->dst_nents ? : 1;
		if (!is_sec1 || sg_count == 1)
			dma_map_sg(dev, areq->dst, sg_count, DMA_FROM_DEVICE);
	}

	ret = talitos_sg_map(dev, areq->dst, cryptlen, edesc, &desc->ptr[4],
			     sg_count, 0, (edesc->src_nents + 1));
	if (ret > 1)
		sync_needed = true;

	/* iv out */
	map_single_talitos_ptr(dev, &desc->ptr[5], ivsize, ctx->iv,
			       DMA_FROM_DEVICE);

	/* last DWORD empty */
	desc->ptr[6] = zero_entry;

	if (sync_needed)
		dma_sync_single_for_device(dev, edesc->dma_link_tbl,
					   edesc->dma_len, DMA_BIDIRECTIONAL);

	ret = talitos_submit(dev, ctx->ch, desc, callback, areq);
	if (ret != -EINPROGRESS) {
		common_nonsnoop_unmap(dev, edesc, areq);
		kfree(edesc);
	}
	return ret;
}

static struct talitos_edesc *ablkcipher_edesc_alloc(struct ablkcipher_request *
						    areq, bool encrypt)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	unsigned int ivsize = crypto_ablkcipher_ivsize(cipher);

	return talitos_edesc_alloc(ctx->dev, areq->src, areq->dst,
				   areq->info, 0, areq->nbytes, 0, ivsize, 0,
				   areq->base.flags, encrypt);
}

static int ablkcipher_encrypt(struct ablkcipher_request *areq)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	struct talitos_edesc *edesc;

	/* allocate extended descriptor */
	edesc = ablkcipher_edesc_alloc(areq, true);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* set encrypt */
	edesc->desc.hdr = ctx->desc_hdr_template | DESC_HDR_MODE0_ENCRYPT;

	return common_nonsnoop(edesc, areq, ablkcipher_done);
}

static int ablkcipher_decrypt(struct ablkcipher_request *areq)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	struct talitos_edesc *edesc;

	/* allocate extended descriptor */
	edesc = ablkcipher_edesc_alloc(areq, false);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	edesc->desc.hdr = ctx->desc_hdr_template | DESC_HDR_DIR_INBOUND;

	return common_nonsnoop(edesc, areq, ablkcipher_done);
}

static void common_nonsnoop_hash_unmap(struct device *dev,
				       struct talitos_edesc *edesc,
				       struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);

	unmap_single_talitos_ptr(dev, &edesc->desc.ptr[5], DMA_FROM_DEVICE);

	talitos_sg_unmap(dev, edesc, req_ctx->psrc, NULL, 0, 0);

	/* When using hashctx-in, must unmap it. */
	if (from_talitos_ptr_len(&edesc->desc.ptr[1], is_sec1))
		unmap_single_talitos_ptr(dev, &edesc->desc.ptr[1],
					 DMA_TO_DEVICE);

	if (from_talitos_ptr_len(&edesc->desc.ptr[2], is_sec1))
		unmap_single_talitos_ptr(dev, &edesc->desc.ptr[2],
					 DMA_TO_DEVICE);

	if (edesc->dma_len)
		dma_unmap_single(dev, edesc->dma_link_tbl, edesc->dma_len,
				 DMA_BIDIRECTIONAL);

}

static void ahash_done(struct device *dev,
		       struct talitos_desc *desc, void *context,
		       int err)
{
	struct ahash_request *areq = context;
	struct talitos_edesc *edesc =
		 container_of(desc, struct talitos_edesc, desc);
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	if (!req_ctx->last && req_ctx->to_hash_later) {
		/* Position any partial block for next update/final/finup */
		memcpy(req_ctx->buf, req_ctx->bufnext, req_ctx->to_hash_later);
		req_ctx->nbuf = req_ctx->to_hash_later;
	}
	common_nonsnoop_hash_unmap(dev, edesc, areq);

	kfree(edesc);

	areq->base.complete(&areq->base, err);
}

/*
 * SEC1 doesn't like hashing of 0 sized message, so we do the padding
 * ourself and submit a padded block
 */
void talitos_handle_buggy_hash(struct talitos_ctx *ctx,
			       struct talitos_edesc *edesc,
			       struct talitos_ptr *ptr)
{
	static u8 padded_hash[64] = {
		0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	};

	pr_err_once("Bug in SEC1, padding ourself\n");
	edesc->desc.hdr &= ~DESC_HDR_MODE0_MDEU_PAD;
	map_single_talitos_ptr(ctx->dev, ptr, sizeof(padded_hash),
			       (char *)padded_hash, DMA_TO_DEVICE);
}

static int common_nonsnoop_hash(struct talitos_edesc *edesc,
				struct ahash_request *areq, unsigned int length,
				void (*callback) (struct device *dev,
						  struct talitos_desc *desc,
						  void *context, int error))
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ahash_ctx(tfm);
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct device *dev = ctx->dev;
	struct talitos_desc *desc = &edesc->desc;
	int ret;
	bool sync_needed = false;
	struct talitos_private *priv = dev_get_drvdata(dev);
	bool is_sec1 = has_ftr_sec1(priv);
	int sg_count;

	/* first DWORD empty */
	desc->ptr[0] = zero_entry;

	/* hash context in */
	if (!req_ctx->first || req_ctx->swinit) {
		map_single_talitos_ptr(dev, &desc->ptr[1],
				       req_ctx->hw_context_size,
				       (char *)req_ctx->hw_context,
				       DMA_TO_DEVICE);
		req_ctx->swinit = 0;
	} else {
		desc->ptr[1] = zero_entry;
		/* Indicate next op is not the first. */
		req_ctx->first = 0;
	}

	/* HMAC key */
	if (ctx->keylen)
		map_single_talitos_ptr(dev, &desc->ptr[2], ctx->keylen,
				       (char *)&ctx->key, DMA_TO_DEVICE);
	else
		desc->ptr[2] = zero_entry;

	sg_count = edesc->src_nents ?: 1;
	if (is_sec1 && sg_count > 1)
		sg_copy_to_buffer(areq->src, sg_count, edesc->buf, length);
	else
		sg_count = dma_map_sg(dev, req_ctx->psrc, sg_count,
				      DMA_TO_DEVICE);
	/*
	 * data in
	 */
	sg_count = talitos_sg_map(dev, req_ctx->psrc, length, edesc,
				  &desc->ptr[3], sg_count, 0, 0);
	if (sg_count > 1)
		sync_needed = true;

	/* fifth DWORD empty */
	desc->ptr[4] = zero_entry;

	/* hash/HMAC out -or- hash context out */
	if (req_ctx->last)
		map_single_talitos_ptr(dev, &desc->ptr[5],
				       crypto_ahash_digestsize(tfm),
				       areq->result, DMA_FROM_DEVICE);
	else
		map_single_talitos_ptr(dev, &desc->ptr[5],
				       req_ctx->hw_context_size,
				       req_ctx->hw_context, DMA_FROM_DEVICE);

	/* last DWORD empty */
	desc->ptr[6] = zero_entry;

	if (is_sec1 && from_talitos_ptr_len(&desc->ptr[3], true) == 0)
		talitos_handle_buggy_hash(ctx, edesc, &desc->ptr[3]);

	if (sync_needed)
		dma_sync_single_for_device(dev, edesc->dma_link_tbl,
					   edesc->dma_len, DMA_BIDIRECTIONAL);

	ret = talitos_submit(dev, ctx->ch, desc, callback, areq);
	if (ret != -EINPROGRESS) {
		common_nonsnoop_hash_unmap(dev, edesc, areq);
		kfree(edesc);
	}
	return ret;
}

static struct talitos_edesc *ahash_edesc_alloc(struct ahash_request *areq,
					       unsigned int nbytes)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ahash_ctx(tfm);
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	return talitos_edesc_alloc(ctx->dev, req_ctx->psrc, NULL, NULL, 0,
				   nbytes, 0, 0, 0, areq->base.flags, false);
}

static int ahash_init(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	/* Initialize the context */
	req_ctx->nbuf = 0;
	req_ctx->first = 1; /* first indicates h/w must init its context */
	req_ctx->swinit = 0; /* assume h/w init of context */
	req_ctx->hw_context_size =
		(crypto_ahash_digestsize(tfm) <= SHA256_DIGEST_SIZE)
			? TALITOS_MDEU_CONTEXT_SIZE_MD5_SHA1_SHA256
			: TALITOS_MDEU_CONTEXT_SIZE_SHA384_SHA512;

	return 0;
}

/*
 * on h/w without explicit sha224 support, we initialize h/w context
 * manually with sha224 constants, and tell it to run sha256.
 */
static int ahash_init_sha224_swinit(struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	ahash_init(areq);
	req_ctx->swinit = 1;/* prevent h/w initting context with sha256 values*/

	req_ctx->hw_context[0] = SHA224_H0;
	req_ctx->hw_context[1] = SHA224_H1;
	req_ctx->hw_context[2] = SHA224_H2;
	req_ctx->hw_context[3] = SHA224_H3;
	req_ctx->hw_context[4] = SHA224_H4;
	req_ctx->hw_context[5] = SHA224_H5;
	req_ctx->hw_context[6] = SHA224_H6;
	req_ctx->hw_context[7] = SHA224_H7;

	/* init 64-bit count */
	req_ctx->hw_context[8] = 0;
	req_ctx->hw_context[9] = 0;

	return 0;
}

static int ahash_process_req(struct ahash_request *areq, unsigned int nbytes)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct talitos_ctx *ctx = crypto_ahash_ctx(tfm);
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct talitos_edesc *edesc;
	unsigned int blocksize =
			crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int nbytes_to_hash;
	unsigned int to_hash_later;
	unsigned int nsg;
	int nents;

	if (!req_ctx->last && (nbytes + req_ctx->nbuf <= blocksize)) {
		/* Buffer up to one whole block */
		nents = sg_nents_for_len(areq->src, nbytes);
		if (nents < 0) {
			dev_err(ctx->dev, "Invalid number of src SG.\n");
			return nents;
		}
		sg_copy_to_buffer(areq->src, nents,
				  req_ctx->buf + req_ctx->nbuf, nbytes);
		req_ctx->nbuf += nbytes;
		return 0;
	}

	/* At least (blocksize + 1) bytes are available to hash */
	nbytes_to_hash = nbytes + req_ctx->nbuf;
	to_hash_later = nbytes_to_hash & (blocksize - 1);

	if (req_ctx->last)
		to_hash_later = 0;
	else if (to_hash_later)
		/* There is a partial block. Hash the full block(s) now */
		nbytes_to_hash -= to_hash_later;
	else {
		/* Keep one block buffered */
		nbytes_to_hash -= blocksize;
		to_hash_later = blocksize;
	}

	/* Chain in any previously buffered data */
	if (req_ctx->nbuf) {
		nsg = (req_ctx->nbuf < nbytes_to_hash) ? 2 : 1;
		sg_init_table(req_ctx->bufsl, nsg);
		sg_set_buf(req_ctx->bufsl, req_ctx->buf, req_ctx->nbuf);
		if (nsg > 1)
			sg_chain(req_ctx->bufsl, 2, areq->src);
		req_ctx->psrc = req_ctx->bufsl;
	} else
		req_ctx->psrc = areq->src;

	if (to_hash_later) {
		nents = sg_nents_for_len(areq->src, nbytes);
		if (nents < 0) {
			dev_err(ctx->dev, "Invalid number of src SG.\n");
			return nents;
		}
		sg_pcopy_to_buffer(areq->src, nents,
				      req_ctx->bufnext,
				      to_hash_later,
				      nbytes - to_hash_later);
	}
	req_ctx->to_hash_later = to_hash_later;

	/* Allocate extended descriptor */
	edesc = ahash_edesc_alloc(areq, nbytes_to_hash);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	edesc->desc.hdr = ctx->desc_hdr_template;

	/* On last one, request SEC to pad; otherwise continue */
	if (req_ctx->last)
		edesc->desc.hdr |= DESC_HDR_MODE0_MDEU_PAD;
	else
		edesc->desc.hdr |= DESC_HDR_MODE0_MDEU_CONT;

	/* request SEC to INIT hash. */
	if (req_ctx->first && !req_ctx->swinit)
		edesc->desc.hdr |= DESC_HDR_MODE0_MDEU_INIT;

	/* When the tfm context has a keylen, it's an HMAC.
	 * A first or last (ie. not middle) descriptor must request HMAC.
	 */
	if (ctx->keylen && (req_ctx->first || req_ctx->last))
		edesc->desc.hdr |= DESC_HDR_MODE0_MDEU_HMAC;

	return common_nonsnoop_hash(edesc, areq, nbytes_to_hash,
				    ahash_done);
}

static int ahash_update(struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	req_ctx->last = 0;

	return ahash_process_req(areq, areq->nbytes);
}

static int ahash_final(struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	req_ctx->last = 1;

	return ahash_process_req(areq, 0);
}

static int ahash_finup(struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);

	req_ctx->last = 1;

	return ahash_process_req(areq, areq->nbytes);
}

static int ahash_digest(struct ahash_request *areq)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(areq);

	ahash->init(areq);
	req_ctx->last = 1;

	return ahash_process_req(areq, areq->nbytes);
}

static int ahash_export(struct ahash_request *areq, void *out)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct talitos_export_state *export = out;

	memcpy(export->hw_context, req_ctx->hw_context,
	       req_ctx->hw_context_size);
	memcpy(export->buf, req_ctx->buf, req_ctx->nbuf);
	export->swinit = req_ctx->swinit;
	export->first = req_ctx->first;
	export->last = req_ctx->last;
	export->to_hash_later = req_ctx->to_hash_later;
	export->nbuf = req_ctx->nbuf;

	return 0;
}

static int ahash_import(struct ahash_request *areq, const void *in)
{
	struct talitos_ahash_req_ctx *req_ctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	const struct talitos_export_state *export = in;

	memset(req_ctx, 0, sizeof(*req_ctx));
	req_ctx->hw_context_size =
		(crypto_ahash_digestsize(tfm) <= SHA256_DIGEST_SIZE)
			? TALITOS_MDEU_CONTEXT_SIZE_MD5_SHA1_SHA256
			: TALITOS_MDEU_CONTEXT_SIZE_SHA384_SHA512;
	memcpy(req_ctx->hw_context, export->hw_context,
	       req_ctx->hw_context_size);
	memcpy(req_ctx->buf, export->buf, export->nbuf);
	req_ctx->swinit = export->swinit;
	req_ctx->first = export->first;
	req_ctx->last = export->last;
	req_ctx->to_hash_later = export->to_hash_later;
	req_ctx->nbuf = export->nbuf;

	return 0;
}

struct keyhash_result {
	struct completion completion;
	int err;
};

static void keyhash_complete(struct crypto_async_request *req, int err)
{
	struct keyhash_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static int keyhash(struct crypto_ahash *tfm, const u8 *key, unsigned int keylen,
		   u8 *hash)
{
	struct talitos_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));

	struct scatterlist sg[1];
	struct ahash_request *req;
	struct keyhash_result hresult;
	int ret;

	init_completion(&hresult.completion);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	/* Keep tfm keylen == 0 during hash of the long key */
	ctx->keylen = 0;
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   keyhash_complete, &hresult);

	sg_init_one(&sg[0], key, keylen);

	ahash_request_set_crypt(req, sg, hash, keylen);
	ret = crypto_ahash_digest(req);
	switch (ret) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		ret = wait_for_completion_interruptible(
			&hresult.completion);
		if (!ret)
			ret = hresult.err;
		break;
	default:
		break;
	}
	ahash_request_free(req);

	return ret;
}

static int ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	struct talitos_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	unsigned int blocksize =
			crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int keysize = keylen;
	u8 hash[SHA512_DIGEST_SIZE];
	int ret;

	if (keylen <= blocksize)
		memcpy(ctx->key, key, keysize);
	else {
		/* Must get the hash of the long key */
		ret = keyhash(tfm, key, keylen, hash);

		if (ret) {
			crypto_ahash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
			return -EINVAL;
		}

		keysize = digestsize;
		memcpy(ctx->key, hash, digestsize);
	}

	ctx->keylen = keysize;

	return 0;
}


struct talitos_alg_template {
	u32 type;
	u32 priority;
	union {
		struct crypto_alg crypto;
		struct ahash_alg hash;
		struct aead_alg aead;
	} alg;
	__be32 desc_hdr_template;
};

static struct talitos_alg_template driver_algs[] = {
	/* AEAD algorithms.  These use a single-pass ipsec_esp descriptor */
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_AESU |
		                     DESC_HDR_MODE0_AESU_CBC |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_SHA1_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA1_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_SHA1_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha1-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_CBC |
				     DESC_HDR_MODE0_DEU_3DES |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA1_HMAC,
	},
	{       .type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA224_HMAC,
	},
	{       .type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA224_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_SHA224_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha224),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha224-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA224_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_CBC |
				     DESC_HDR_MODE0_DEU_3DES |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA224_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_AESU |
		                     DESC_HDR_MODE0_AESU_CBC |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_SHA256_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA256_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_SHA256_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha256-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_CBC |
				     DESC_HDR_MODE0_DEU_3DES |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_SHA256_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_AESU |
		                     DESC_HDR_MODE0_AESU_CBC |
		                     DESC_HDR_SEL1_MDEUB |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEUB_SHA384_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha384),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha384-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA384_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUB |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEUB_SHA384_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),cbc(aes))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_AESU |
		                     DESC_HDR_MODE0_AESU_CBC |
		                     DESC_HDR_SEL1_MDEUB |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEUB_SHA512_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha512),"
					    "cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-sha512-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUB |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEUB_SHA512_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_AESU |
		                     DESC_HDR_MODE0_AESU_CBC |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_MD5_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(aes))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-aes-talitos",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_MD5_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_IPSEC_ESP |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES |
		                     DESC_HDR_SEL1_MDEUA |
		                     DESC_HDR_MODE1_MDEU_INIT |
		                     DESC_HDR_MODE1_MDEU_PAD |
		                     DESC_HDR_MODE1_MDEU_MD5_HMAC,
	},
	{	.type = CRYPTO_ALG_TYPE_AEAD,
		.priority = TALITOS_CRA_PRIORITY_AEAD_HSNA,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
				.cra_driver_name = "authenc-hmac-md5-"
						   "cbc-3des-talitos",
				.cra_blocksize = DES3_EDE_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_ASYNC,
			},
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = MD5_DIGEST_SIZE,
		},
		.desc_hdr_template = DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_CBC |
				     DESC_HDR_MODE0_DEU_3DES |
				     DESC_HDR_SEL1_MDEUA |
				     DESC_HDR_MODE1_MDEU_INIT |
				     DESC_HDR_MODE1_MDEU_PAD |
				     DESC_HDR_MODE1_MDEU_MD5_HMAC,
	},
	/* ABLKCIPHER algorithms. */
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-talitos",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
				.ivsize = AES_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-talitos",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
                                     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
				.ivsize = AES_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CBC,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "ctr(aes)",
			.cra_driver_name = "ctr-aes-talitos",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
				.ivsize = AES_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_AESU |
				     DESC_HDR_MODE0_AESU_CTR,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "ecb(des)",
			.cra_driver_name = "ecb-des-talitos",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = DES_KEY_SIZE,
				.max_keysize = DES_KEY_SIZE,
				.ivsize = DES_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "cbc(des)",
			.cra_driver_name = "cbc-des-talitos",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = DES_KEY_SIZE,
				.max_keysize = DES_KEY_SIZE,
				.ivsize = DES_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_CBC,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-3des-talitos",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
				     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = DES3_EDE_KEY_SIZE,
				.max_keysize = DES3_EDE_KEY_SIZE,
				.ivsize = DES3_EDE_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_DEU |
				     DESC_HDR_MODE0_DEU_3DES,
	},
	{	.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.alg.crypto = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-3des-talitos",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
                                     CRYPTO_ALG_ASYNC,
			.cra_ablkcipher = {
				.min_keysize = DES3_EDE_KEY_SIZE,
				.max_keysize = DES3_EDE_KEY_SIZE,
				.ivsize = DES3_EDE_BLOCK_SIZE,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
			             DESC_HDR_SEL0_DEU |
		                     DESC_HDR_MODE0_DEU_CBC |
		                     DESC_HDR_MODE0_DEU_3DES,
	},
	/* AHASH algorithms. */
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = MD5_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-talitos",
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_MD5,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA1_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-talitos",
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA1,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-talitos",
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA224,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-talitos",
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA256,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "sha384",
				.cra_driver_name = "sha384-talitos",
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUB |
				     DESC_HDR_MODE0_MDEUB_SHA384,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "sha512",
				.cra_driver_name = "sha512-talitos",
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUB |
				     DESC_HDR_MODE0_MDEUB_SHA512,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = MD5_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(md5)",
				.cra_driver_name = "hmac-md5-talitos",
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_MD5,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA1_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "hmac-sha1-talitos",
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA1,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "hmac-sha224-talitos",
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA224,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "hmac-sha256-talitos",
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUA |
				     DESC_HDR_MODE0_MDEU_SHA256,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(sha384)",
				.cra_driver_name = "hmac-sha384-talitos",
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUB |
				     DESC_HDR_MODE0_MDEUB_SHA384,
	},
	{	.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.hash = {
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.statesize = sizeof(struct talitos_export_state),
			.halg.base = {
				.cra_name = "hmac(sha512)",
				.cra_driver_name = "hmac-sha512-talitos",
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					     CRYPTO_ALG_ASYNC,
			}
		},
		.desc_hdr_template = DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
				     DESC_HDR_SEL0_MDEUB |
				     DESC_HDR_MODE0_MDEUB_SHA512,
	}
};

struct talitos_crypto_alg {
	struct list_head entry;
	struct device *dev;
	struct talitos_alg_template algt;
};

static int talitos_init_common(struct talitos_ctx *ctx,
			       struct talitos_crypto_alg *talitos_alg)
{
	struct talitos_private *priv;

	/* update context with ptr to dev */
	ctx->dev = talitos_alg->dev;

	/* assign SEC channel to tfm in round-robin fashion */
	priv = dev_get_drvdata(ctx->dev);
	ctx->ch = atomic_inc_return(&priv->last_chan) &
		  (priv->num_channels - 1);

	/* copy descriptor header template value */
	ctx->desc_hdr_template = talitos_alg->algt.desc_hdr_template;

	/* select done notification */
	ctx->desc_hdr_template |= DESC_HDR_DONE_NOTIFY;

	return 0;
}

static int talitos_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct talitos_crypto_alg *talitos_alg;
	struct talitos_ctx *ctx = crypto_tfm_ctx(tfm);

	if ((alg->cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_AHASH)
		talitos_alg = container_of(__crypto_ahash_alg(alg),
					   struct talitos_crypto_alg,
					   algt.alg.hash);
	else
		talitos_alg = container_of(alg, struct talitos_crypto_alg,
					   algt.alg.crypto);

	return talitos_init_common(ctx, talitos_alg);
}

static int talitos_cra_init_aead(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct talitos_crypto_alg *talitos_alg;
	struct talitos_ctx *ctx = crypto_aead_ctx(tfm);

	talitos_alg = container_of(alg, struct talitos_crypto_alg,
				   algt.alg.aead);

	return talitos_init_common(ctx, talitos_alg);
}

static int talitos_cra_init_ahash(struct crypto_tfm *tfm)
{
	struct talitos_ctx *ctx = crypto_tfm_ctx(tfm);

	talitos_cra_init(tfm);

	ctx->keylen = 0;
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct talitos_ahash_req_ctx));

	return 0;
}

/*
 * given the alg's descriptor header template, determine whether descriptor
 * type and primary/secondary execution units required match the hw
 * capabilities description provided in the device tree node.
 */
static int hw_supports(struct device *dev, __be32 desc_hdr_template)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	int ret;

	ret = (1 << DESC_TYPE(desc_hdr_template) & priv->desc_types) &&
	      (1 << PRIMARY_EU(desc_hdr_template) & priv->exec_units);

	if (SECONDARY_EU(desc_hdr_template))
		ret = ret && (1 << SECONDARY_EU(desc_hdr_template)
		              & priv->exec_units);

	return ret;
}

static int talitos_remove(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct talitos_private *priv = dev_get_drvdata(dev);
	struct talitos_crypto_alg *t_alg, *n;
	int i;

	list_for_each_entry_safe(t_alg, n, &priv->alg_list, entry) {
		switch (t_alg->algt.type) {
		case CRYPTO_ALG_TYPE_ABLKCIPHER:
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			crypto_unregister_aead(&t_alg->algt.alg.aead);
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&t_alg->algt.alg.hash);
			break;
		}
		list_del(&t_alg->entry);
		kfree(t_alg);
	}

	if (hw_supports(dev, DESC_HDR_SEL0_RNG))
		talitos_unregister_rng(dev);

	for (i = 0; priv->chan && i < priv->num_channels; i++)
		kfree(priv->chan[i].fifo);

	kfree(priv->chan);

	for (i = 0; i < 2; i++)
		if (priv->irq[i]) {
			free_irq(priv->irq[i], dev);
			irq_dispose_mapping(priv->irq[i]);
		}

	tasklet_kill(&priv->done_task[0]);
	if (priv->irq[1])
		tasklet_kill(&priv->done_task[1]);

	iounmap(priv->reg);

	kfree(priv);

	return 0;
}

static struct talitos_crypto_alg *talitos_alg_alloc(struct device *dev,
						    struct talitos_alg_template
						           *template)
{
	struct talitos_private *priv = dev_get_drvdata(dev);
	struct talitos_crypto_alg *t_alg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(struct talitos_crypto_alg), GFP_KERNEL);
	if (!t_alg)
		return ERR_PTR(-ENOMEM);

	t_alg->algt = *template;

	switch (t_alg->algt.type) {
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		alg = &t_alg->algt.alg.crypto;
		alg->cra_init = talitos_cra_init;
		alg->cra_type = &crypto_ablkcipher_type;
		alg->cra_ablkcipher.setkey = ablkcipher_setkey;
		alg->cra_ablkcipher.encrypt = ablkcipher_encrypt;
		alg->cra_ablkcipher.decrypt = ablkcipher_decrypt;
		alg->cra_ablkcipher.geniv = "eseqiv";
		break;
	case CRYPTO_ALG_TYPE_AEAD:
		alg = &t_alg->algt.alg.aead.base;
		t_alg->algt.alg.aead.init = talitos_cra_init_aead;
		t_alg->algt.alg.aead.setkey = aead_setkey;
		t_alg->algt.alg.aead.encrypt = aead_encrypt;
		t_alg->algt.alg.aead.decrypt = aead_decrypt;
		break;
	case CRYPTO_ALG_TYPE_AHASH:
		alg = &t_alg->algt.alg.hash.halg.base;
		alg->cra_init = talitos_cra_init_ahash;
		alg->cra_type = &crypto_ahash_type;
		t_alg->algt.alg.hash.init = ahash_init;
		t_alg->algt.alg.hash.update = ahash_update;
		t_alg->algt.alg.hash.final = ahash_final;
		t_alg->algt.alg.hash.finup = ahash_finup;
		t_alg->algt.alg.hash.digest = ahash_digest;
		t_alg->algt.alg.hash.setkey = ahash_setkey;
		t_alg->algt.alg.hash.import = ahash_import;
		t_alg->algt.alg.hash.export = ahash_export;

		if (!(priv->features & TALITOS_FTR_HMAC_OK) &&
		    !strncmp(alg->cra_name, "hmac", 4)) {
			kfree(t_alg);
			return ERR_PTR(-ENOTSUPP);
		}
		if (!(priv->features & TALITOS_FTR_SHA224_HWINIT) &&
		    (!strcmp(alg->cra_name, "sha224") ||
		     !strcmp(alg->cra_name, "hmac(sha224)"))) {
			t_alg->algt.alg.hash.init = ahash_init_sha224_swinit;
			t_alg->algt.desc_hdr_template =
					DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU |
					DESC_HDR_SEL0_MDEUA |
					DESC_HDR_MODE0_MDEU_SHA256;
		}
		break;
	default:
		dev_err(dev, "unknown algorithm type %d\n", t_alg->algt.type);
		kfree(t_alg);
		return ERR_PTR(-EINVAL);
	}

	alg->cra_module = THIS_MODULE;
	if (t_alg->algt.priority)
		alg->cra_priority = t_alg->algt.priority;
	else
		alg->cra_priority = TALITOS_CRA_PRIORITY;
	alg->cra_alignmask = 0;
	alg->cra_ctxsize = sizeof(struct talitos_ctx);
	alg->cra_flags |= CRYPTO_ALG_KERN_DRIVER_ONLY;

	t_alg->dev = dev;

	return t_alg;
}

static int talitos_probe_irq(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct talitos_private *priv = dev_get_drvdata(dev);
	int err;
	bool is_sec1 = has_ftr_sec1(priv);

	priv->irq[0] = irq_of_parse_and_map(np, 0);
	if (!priv->irq[0]) {
		dev_err(dev, "failed to map irq\n");
		return -EINVAL;
	}
	if (is_sec1) {
		err = request_irq(priv->irq[0], talitos1_interrupt_4ch, 0,
				  dev_driver_string(dev), dev);
		goto primary_out;
	}

	priv->irq[1] = irq_of_parse_and_map(np, 1);

	/* get the primary irq line */
	if (!priv->irq[1]) {
		err = request_irq(priv->irq[0], talitos2_interrupt_4ch, 0,
				  dev_driver_string(dev), dev);
		goto primary_out;
	}

	err = request_irq(priv->irq[0], talitos2_interrupt_ch0_2, 0,
			  dev_driver_string(dev), dev);
	if (err)
		goto primary_out;

	/* get the secondary irq line */
	err = request_irq(priv->irq[1], talitos2_interrupt_ch1_3, 0,
			  dev_driver_string(dev), dev);
	if (err) {
		dev_err(dev, "failed to request secondary irq\n");
		irq_dispose_mapping(priv->irq[1]);
		priv->irq[1] = 0;
	}

	return err;

primary_out:
	if (err) {
		dev_err(dev, "failed to request primary irq\n");
		irq_dispose_mapping(priv->irq[0]);
		priv->irq[0] = 0;
	}

	return err;
}

static int talitos_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct talitos_private *priv;
	const unsigned int *prop;
	int i, err;
	int stride;

	priv = kzalloc(sizeof(struct talitos_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->alg_list);

	dev_set_drvdata(dev, priv);

	priv->ofdev = ofdev;

	spin_lock_init(&priv->reg_lock);

	priv->reg = of_iomap(np, 0);
	if (!priv->reg) {
		dev_err(dev, "failed to of_iomap\n");
		err = -ENOMEM;
		goto err_out;
	}

	/* get SEC version capabilities from device tree */
	prop = of_get_property(np, "fsl,num-channels", NULL);
	if (prop)
		priv->num_channels = *prop;

	prop = of_get_property(np, "fsl,channel-fifo-len", NULL);
	if (prop)
		priv->chfifo_len = *prop;

	prop = of_get_property(np, "fsl,exec-units-mask", NULL);
	if (prop)
		priv->exec_units = *prop;

	prop = of_get_property(np, "fsl,descriptor-types-mask", NULL);
	if (prop)
		priv->desc_types = *prop;

	if (!is_power_of_2(priv->num_channels) || !priv->chfifo_len ||
	    !priv->exec_units || !priv->desc_types) {
		dev_err(dev, "invalid property data in device tree node\n");
		err = -EINVAL;
		goto err_out;
	}

	if (of_device_is_compatible(np, "fsl,sec3.0"))
		priv->features |= TALITOS_FTR_SRC_LINK_TBL_LEN_INCLUDES_EXTENT;

	if (of_device_is_compatible(np, "fsl,sec2.1"))
		priv->features |= TALITOS_FTR_HW_AUTH_CHECK |
				  TALITOS_FTR_SHA224_HWINIT |
				  TALITOS_FTR_HMAC_OK;

	if (of_device_is_compatible(np, "fsl,sec1.0"))
		priv->features |= TALITOS_FTR_SEC1;

	if (of_device_is_compatible(np, "fsl,sec1.2")) {
		priv->reg_deu = priv->reg + TALITOS12_DEU;
		priv->reg_aesu = priv->reg + TALITOS12_AESU;
		priv->reg_mdeu = priv->reg + TALITOS12_MDEU;
		stride = TALITOS1_CH_STRIDE;
	} else if (of_device_is_compatible(np, "fsl,sec1.0")) {
		priv->reg_deu = priv->reg + TALITOS10_DEU;
		priv->reg_aesu = priv->reg + TALITOS10_AESU;
		priv->reg_mdeu = priv->reg + TALITOS10_MDEU;
		priv->reg_afeu = priv->reg + TALITOS10_AFEU;
		priv->reg_rngu = priv->reg + TALITOS10_RNGU;
		priv->reg_pkeu = priv->reg + TALITOS10_PKEU;
		stride = TALITOS1_CH_STRIDE;
	} else {
		priv->reg_deu = priv->reg + TALITOS2_DEU;
		priv->reg_aesu = priv->reg + TALITOS2_AESU;
		priv->reg_mdeu = priv->reg + TALITOS2_MDEU;
		priv->reg_afeu = priv->reg + TALITOS2_AFEU;
		priv->reg_rngu = priv->reg + TALITOS2_RNGU;
		priv->reg_pkeu = priv->reg + TALITOS2_PKEU;
		priv->reg_keu = priv->reg + TALITOS2_KEU;
		priv->reg_crcu = priv->reg + TALITOS2_CRCU;
		stride = TALITOS2_CH_STRIDE;
	}

	err = talitos_probe_irq(ofdev);
	if (err)
		goto err_out;

	if (of_device_is_compatible(np, "fsl,sec1.0")) {
		tasklet_init(&priv->done_task[0], talitos1_done_4ch,
			     (unsigned long)dev);
	} else {
		if (!priv->irq[1]) {
			tasklet_init(&priv->done_task[0], talitos2_done_4ch,
				     (unsigned long)dev);
		} else {
			tasklet_init(&priv->done_task[0], talitos2_done_ch0_2,
				     (unsigned long)dev);
			tasklet_init(&priv->done_task[1], talitos2_done_ch1_3,
				     (unsigned long)dev);
		}
	}

	priv->chan = kzalloc(sizeof(struct talitos_channel) *
			     priv->num_channels, GFP_KERNEL);
	if (!priv->chan) {
		dev_err(dev, "failed to allocate channel management space\n");
		err = -ENOMEM;
		goto err_out;
	}

	priv->fifo_len = roundup_pow_of_two(priv->chfifo_len);

	for (i = 0; i < priv->num_channels; i++) {
		priv->chan[i].reg = priv->reg + stride * (i + 1);
		if (!priv->irq[1] || !(i & 1))
			priv->chan[i].reg += TALITOS_CH_BASE_OFFSET;

		spin_lock_init(&priv->chan[i].head_lock);
		spin_lock_init(&priv->chan[i].tail_lock);

		priv->chan[i].fifo = kzalloc(sizeof(struct talitos_request) *
					     priv->fifo_len, GFP_KERNEL);
		if (!priv->chan[i].fifo) {
			dev_err(dev, "failed to allocate request fifo %d\n", i);
			err = -ENOMEM;
			goto err_out;
		}

		atomic_set(&priv->chan[i].submit_count,
			   -(priv->chfifo_len - 1));
	}

	dma_set_mask(dev, DMA_BIT_MASK(36));

	/* reset and initialize the h/w */
	err = init_device(dev);
	if (err) {
		dev_err(dev, "failed to initialize device\n");
		goto err_out;
	}

	/* register the RNG, if available */
	if (hw_supports(dev, DESC_HDR_SEL0_RNG)) {
		err = talitos_register_rng(dev);
		if (err) {
			dev_err(dev, "failed to register hwrng: %d\n", err);
			goto err_out;
		} else
			dev_info(dev, "hwrng\n");
	}

	/* register crypto algorithms the device supports */
	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		if (hw_supports(dev, driver_algs[i].desc_hdr_template)) {
			struct talitos_crypto_alg *t_alg;
			struct crypto_alg *alg = NULL;

			t_alg = talitos_alg_alloc(dev, &driver_algs[i]);
			if (IS_ERR(t_alg)) {
				err = PTR_ERR(t_alg);
				if (err == -ENOTSUPP)
					continue;
				goto err_out;
			}

			switch (t_alg->algt.type) {
			case CRYPTO_ALG_TYPE_ABLKCIPHER:
				err = crypto_register_alg(
						&t_alg->algt.alg.crypto);
				alg = &t_alg->algt.alg.crypto;
				break;

			case CRYPTO_ALG_TYPE_AEAD:
				err = crypto_register_aead(
					&t_alg->algt.alg.aead);
				alg = &t_alg->algt.alg.aead.base;
				break;

			case CRYPTO_ALG_TYPE_AHASH:
				err = crypto_register_ahash(
						&t_alg->algt.alg.hash);
				alg = &t_alg->algt.alg.hash.halg.base;
				break;
			}
			if (err) {
				dev_err(dev, "%s alg registration failed\n",
					alg->cra_driver_name);
				kfree(t_alg);
			} else
				list_add_tail(&t_alg->entry, &priv->alg_list);
		}
	}
	if (!list_empty(&priv->alg_list))
		dev_info(dev, "%s algorithms registered in /proc/crypto\n",
			 (char *)of_get_property(np, "compatible", NULL));

	return 0;

err_out:
	talitos_remove(ofdev);

	return err;
}

static const struct of_device_id talitos_match[] = {
#ifdef CONFIG_CRYPTO_DEV_TALITOS1
	{
		.compatible = "fsl,sec1.0",
	},
#endif
#ifdef CONFIG_CRYPTO_DEV_TALITOS2
	{
		.compatible = "fsl,sec2.0",
	},
#endif
	{},
};
MODULE_DEVICE_TABLE(of, talitos_match);

static struct platform_driver talitos_driver = {
	.driver = {
		.name = "talitos",
		.of_match_table = talitos_match,
	},
	.probe = talitos_probe,
	.remove = talitos_remove,
};

module_platform_driver(talitos_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kim Phillips <kim.phillips@freescale.com>");
MODULE_DESCRIPTION("Freescale integrated security engine (SEC) driver");
