/*
 * Xilinx AXI DMA Engine support
 *
 * Copyright (C) 2010 Xilinx, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __XILINX_DMA_APF_H
#define __XILINX_DMA_APF_H

/* ioctls */
#include <linux/ioctl.h>

/* tasklet */
#include <linux/interrupt.h>

/* dma stuff */
#include <linux/dma-mapping.h>

/* xlnk structures */
#include "xlnk.h"
#include "xlnk-sysdef.h"

#define XDMA_IOC_MAGIC 'X'
#define XDMA_IOCRESET		_IO(XDMA_IOC_MAGIC, 0)
#define XDMA_IOCREQUEST		_IOWR(XDMA_IOC_MAGIC, 1, unsigned long)
#define XDMA_IOCRELEASE		_IOWR(XDMA_IOC_MAGIC, 2, unsigned long)
#define XDMA_IOCSUBMIT		_IOWR(XDMA_IOC_MAGIC, 3, unsigned long)
#define XDMA_IOCWAIT		_IOWR(XDMA_IOC_MAGIC, 4, unsigned long)
#define XDMA_IOCGETCONFIG	_IOWR(XDMA_IOC_MAGIC, 5, unsigned long)
#define XDMA_IOCSETCONFIG	_IOWR(XDMA_IOC_MAGIC, 6, unsigned long)
#define XDMA_IOC_MAXNR		6

/* Specific hardware configuration-related constants
 */
#define XDMA_RESET_LOOP            1000000
#define XDMA_HALT_LOOP             1000000
#define XDMA_NO_CHANGE             0xFFFF

/* General register bits definitions
 */
#define XDMA_CR_RESET_MASK    0x00000004  /* Reset DMA engine */
#define XDMA_CR_RUNSTOP_MASK  0x00000001  /* Start/stop DMA engine */

#define XDMA_SR_HALTED_MASK   0x00000001  /* DMA channel halted */
#define XDMA_SR_IDLE_MASK     0x00000002  /* DMA channel idle */

#define XDMA_SR_ERR_INTERNAL_MASK 0x00000010/* Datamover internal err */
#define XDMA_SR_ERR_SLAVE_MASK    0x00000020 /* Datamover slave err */
#define XDMA_SR_ERR_DECODE_MASK   0x00000040 /* Datamover decode err */
#define XDMA_SR_ERR_SG_INT_MASK   0x00000100 /* SG internal err */
#define XDMA_SR_ERR_SG_SLV_MASK   0x00000200 /* SG slave err */
#define XDMA_SR_ERR_SG_DEC_MASK   0x00000400 /* SG decode err */
#define XDMA_SR_ERR_ALL_MASK      0x00000770 /* All errors */

#define XDMA_XR_IRQ_IOC_MASK	0x00001000 /* Completion interrupt */
#define XDMA_XR_IRQ_DELAY_MASK	0x00002000 /* Delay interrupt */
#define XDMA_XR_IRQ_ERROR_MASK	0x00004000 /* Error interrupt */
#define XDMA_XR_IRQ_ALL_MASK	    0x00007000 /* All interrupts */

#define XDMA_XR_DELAY_MASK    0xFF000000 /* Delay timeout counter */
#define XDMA_XR_COALESCE_MASK 0x00FF0000 /* Coalesce counter */

#define XDMA_DELAY_SHIFT    24
#define XDMA_COALESCE_SHIFT 16

#define XDMA_DELAY_MAX     0xFF /**< Maximum delay counter value */
#define XDMA_COALESCE_MAX  0xFF /**< Maximum coalescing counter value */

/* BD definitions for Axi DMA
 */
#define XDMA_BD_STS_ACTUAL_LEN_MASK	0x007FFFFF
#define XDMA_BD_STS_COMPL_MASK 0x80000000
#define XDMA_BD_STS_ERR_MASK   0x70000000
#define XDMA_BD_STS_ALL_MASK   0xF0000000

/* DMA BD special bits definitions
 */
#define XDMA_BD_SOP       0x08000000    /* Start of packet bit */
#define XDMA_BD_EOP       0x04000000    /* End of packet bit */

/* BD Software Flag definitions for Axi DMA
 */
#define XDMA_BD_SF_POLL_MODE_MASK	0x00000002
#define XDMA_BD_SF_SW_DONE_MASK		0x00000001

/* driver defines */
#define XDMA_MAX_BD_CNT			16384
#define XDMA_MAX_CHANS_PER_DEVICE	2
#define XDMA_MAX_TRANS_LEN		0x7FF000
#define XDMA_MAX_APPWORDS		5
#define XDMA_BD_CLEANUP_THRESHOLD	((XDMA_MAX_BD_CNT * 8) / 10)

#define XDMA_FLAGS_WAIT_COMPLETE 1
#define XDMA_FLAGS_TRYWAIT 2

/* Platform data definition until ARM supports device tree */
struct xdma_channel_config {
	char *type;
	unsigned int include_dre;
	unsigned int datawidth;
	unsigned int max_burst_len;
	unsigned int irq;
	unsigned int poll_mode;
	unsigned int lite_mode;
};

struct xdma_device_config {
	char *type;
	char *name;
	unsigned int include_sg;
	unsigned int sg_include_stscntrl_strm;  /* dma only */
	unsigned int channel_count;
	struct xdma_channel_config *channel_config;
};

struct xdma_desc_hw {
	xlnk_intptr_type next_desc;	/* 0x00 */
#if XLNK_SYS_BIT_WIDTH == 32
	u32 pad1;       /* 0x04 */
#endif
	xlnk_intptr_type src_addr;   /* 0x08 */
#if XLNK_SYS_BIT_WIDTH == 32
	u32 pad2;       /* 0x0c */
#endif
	u32 addr_vsize; /* 0x10 */
	u32 hsize;       /* 0x14 */
	u32 control;    /* 0x18 */
	u32 status;     /* 0x1c */
	u32 app[5];      /* 0x20 */
	xlnk_intptr_type dmahead;
#if XLNK_SYS_BIT_WIDTH == 32
	u32 Reserved0;
#endif
	u32 sw_flag;	/* 0x3C */
} __aligned(64);

/* shared by all Xilinx DMA engines */
struct xdma_regs {
	u32 cr;        /* 0x00 Control Register */
	u32 sr;        /* 0x04 Status Register */
	u32 cdr;       /* 0x08 Current Descriptor Register */
	u32 cdr_hi;
	u32 tdr;       /* 0x10 Tail Descriptor Register */
	u32 tdr_hi;
	u32 src;       /* 0x18 Source Address Register (cdma) */
	u32 src_hi;
	u32 dst;       /* 0x20 Destination Address Register (cdma) */
	u32 dst_hi;
	u32 btt_ref;   /* 0x28 Bytes To Transfer (cdma) or
			*		park_ref (vdma)
			*/
	u32 version;   /* 0x2c version (vdma) */
};

/* Per DMA specific operations should be embedded in the channel structure */
struct xdma_chan {
	char name[64];
	struct xdma_regs __iomem *regs;
	struct device *dev;			/* The dma device */
	struct xdma_desc_hw *bds[XDMA_MAX_BD_CNT];
	dma_addr_t bd_phys_addr;
	u32 bd_chain_size;
	int bd_cur;
	int bd_tail;
	unsigned int bd_used;			/* # of BDs passed to hw chan */
	enum dma_data_direction direction;	/* Transfer direction */
	int id;					/* Channel ID */
	int irq;				/* Channel IRQ */
	int poll_mode;				/* Poll mode turned on? */
	spinlock_t lock;			/* Descriptor operation lock */
	struct tasklet_struct tasklet;		/* Cleanup work after irq */
	struct tasklet_struct dma_err_tasklet;	/* Cleanup work after irq */
	int    max_len;				/* Maximum len per transfer */
	int    err;				/* Channel has errors */
	int    client_count;
};

struct xdma_device {
	void __iomem *regs;
	struct device *dev;
	struct list_head node;
	struct xdma_chan *chan[XDMA_MAX_CHANS_PER_DEVICE];
	u8 channel_count;
};

struct xdma_head {
	xlnk_intptr_type userbuf;
	unsigned int size;
	unsigned int dmaflag;
	enum dma_data_direction dmadir;
	struct scatterlist *sglist;
	unsigned int sgcnt;
	struct scatterlist *pagelist;
	unsigned int pagecnt;
	struct completion cmp;
	struct xdma_chan *chan;
	unsigned int nappwords_o;
	u32 appwords_o[XDMA_MAX_APPWORDS];
	unsigned int userflag;
	u32 last_bd_index;
	struct xlnk_dmabuf_reg *dmabuf;
};

struct xdma_chan *xdma_request_channel(char *name);
void xdma_release_channel(struct xdma_chan *chan);
void xdma_release_all_channels(void);
int xdma_submit(struct xdma_chan *chan,
		xlnk_intptr_type userbuf,
		void *kaddr,
		unsigned int size,
		unsigned int nappwords_i,
		u32 *appwords_i,
		unsigned int nappwords_o,
		unsigned int user_flags,
		struct xdma_head **dmaheadpp,
		struct xlnk_dmabuf_reg *dp);
int xdma_wait(struct xdma_head *dmahead,
	      unsigned int user_flags,
	      unsigned int *operating_flags);
int xdma_getconfig(struct xdma_chan *chan,
		   unsigned char *irq_thresh,
		   unsigned char *irq_delay);
int xdma_setconfig(struct xdma_chan *chan,
		   unsigned char irq_thresh,
		   unsigned char irq_delay);

#endif
