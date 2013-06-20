#ifndef _XLNK_OS_H
#define _XLNK_OS_H

#include <linux/stddef.h>
#include <linux/dmaengine.h>
#include "xilinx-dma-apf.h"
#include "xlnk-sysdef.h"

#define XLNK_FLAG_COHERENT		0x00000001
#define XLNK_FLAG_KERNEL_BUFFER		0x00000002
#define XLNK_FLAG_DMAPOLLING		0x00000004
#define XLNK_FLAG_IOMMU_VALID		0x00000008
#define XLNK_FLAG_PHYSICAL_ADDR		0x00000100
#define XLNK_FLAG_VIRTUAL_ADDR		0x00000200
#define XLNK_FLAG_MEM_ACQUIRE		0x00001000
#define XLNK_FLAG_MEM_RELEASE		0x00002000
#define CF_FLAG_CACHE_FLUSH_INVALIDATE	0x00000001
#define CF_FLAG_PHYSICALLY_CONTIGUOUS	0x00000002
#define CF_FLAG_DMAPOLLING		0x00000004
#define XLNK_IRQ_LEVEL			0x00000001
#define XLNK_IRQ_EDGE			0x00000002
#define XLNK_IRQ_ACTIVE_HIGH		0x00000004
#define XLNK_IRQ_ACTIVE_LOW		0x00000008
#define XLNK_IRQ_RESET_REG_VALID	0x00000010

enum xlnk_dma_direction {
	XLNK_DMA_BI = 0,
	XLNK_DMA_TO_DEVICE = 1,
	XLNK_DMA_FROM_DEVICE = 2,
	XLNK_DMA_NONE = 3,
};

struct xlnk_dma_transfer_handle {
	dma_addr_t dma_addr;
	unsigned long transfer_length;
	void *kern_addr;
	unsigned long user_addr;
	enum dma_data_direction transfer_direction;
	int sg_effective_length;
	int flags;
	struct dma_chan *channel;
	dma_cookie_t dma_cookie;
	struct dma_async_tx_descriptor *async_desc;
	struct completion completion_handle;
};

struct xlnk_dmabuf_reg {
	xlnk_int_type dmabuf_fd;
	xlnk_intptr_type user_vaddr;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *dbuf_attach;
	struct sg_table *dbuf_sg_table;
	int is_mapped;
	int dma_direction;
	struct list_head list;
};

struct xlnk_irq_control {
	int irq;
	int enabled;
	struct completion cmp;
};

/* CROSSES KERNEL-USER BOUNDARY */
union xlnk_args {
	struct __attribute__ ((__packed__)) {
		xlnk_uint_type len;
		xlnk_int_type id;
		xlnk_intptr_type phyaddr;
		xlnk_byte_type cacheable;
	} allocbuf;
	struct __attribute__ ((__packed__)) {
		xlnk_uint_type id;
		xlnk_intptr_type buf;
	} freebuf;
	struct __attribute__ ((__packed__)) {
		xlnk_int_type dmabuf_fd;
		xlnk_intptr_type user_addr;
	} dmabuf;
	struct __attribute__ ((__packed__)) {
		xlnk_char_type name[64];
		xlnk_intptr_type dmachan;
		xlnk_uint_type bd_space_phys_addr;
		xlnk_uint_type bd_space_size;
	} dmarequest;
#define XLNK_MAX_APPWORDS 5
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type dmachan;
		xlnk_intptr_type buf;
		xlnk_intptr_type buf2;
		xlnk_uint_type buf_offset;
		xlnk_uint_type len;
		xlnk_uint_type bufflag;
		xlnk_intptr_type sglist;
		xlnk_uint_type sgcnt;
		xlnk_enum_type dmadir;
		xlnk_uint_type nappwords_i;
		xlnk_uint_type appwords_i[XLNK_MAX_APPWORDS];
		xlnk_uint_type nappwords_o;
		xlnk_uint_type flag;
		xlnk_intptr_type dmahandle; /* return value */
		xlnk_uint_type last_bd_index;
	} dmasubmit;
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type dmahandle;
		xlnk_uint_type nappwords;
		xlnk_uint_type appwords[XLNK_MAX_APPWORDS];
		/* appwords array we only accept 5 max */
		xlnk_uint_type flags;
	} dmawait;
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type dmachan;
	} dmarelease;
	struct __attribute__ ((__packed__))  {
		xlnk_intptr_type base;
		xlnk_uint_type size;
		xlnk_uint_type irqs[8];
		xlnk_char_type name[32];
		xlnk_uint_type id;
	} devregister;
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type base;
	} devunregister;
	struct __attribute__ ((__packed__)) {
		xlnk_char_type name[32];
		xlnk_uint_type id;
		xlnk_intptr_type base;
		xlnk_uint_type size;
		xlnk_uint_type chan_num;
		xlnk_uint_type chan0_dir;
		xlnk_uint_type chan0_irq;
		xlnk_uint_type chan0_poll_mode;
		xlnk_uint_type chan0_include_dre;
		xlnk_uint_type chan0_data_width;
		xlnk_uint_type chan1_dir;
		xlnk_uint_type chan1_irq;
		xlnk_uint_type chan1_poll_mode;
		xlnk_uint_type chan1_include_dre;
		xlnk_uint_type chan1_data_width;
	} dmaregister;
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type phys_addr;
		xlnk_uint_type size;
		xlnk_int_type action;
	} cachecontrol;
	struct __attribute__ ((__packed__)) {
		xlnk_intptr_type virt_addr;
		xlnk_int_type size;
		xlnk_enum_type dir;
		xlnk_int_type flags;
		xlnk_intptr_type phys_addr;
		xlnk_intptr_type token;
	} memop;
	struct __attribute__ ((__packed__)) {
		xlnk_int_type irq;
		xlnk_int_type subirq;
		xlnk_uint_type type;
		xlnk_intptr_type control_base;
		xlnk_intptr_type reset_reg_base;
		xlnk_uint_type reset_offset;
		xlnk_uint_type reset_valid_high;
		xlnk_uint_type reset_valid_low;
		xlnk_int_type irq_id;
	} irqregister;
	struct __attribute__ ((__packed__)) {
		xlnk_int_type irq_id;
	} irqunregister;
	struct __attribute__ ((__packed__)) {
		xlnk_int_type irq_id;
		xlnk_int_type polling;
		xlnk_int_type success;
	} irqwait;
};

#endif
