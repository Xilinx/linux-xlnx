#ifndef _XLNK_OS_H
#define _XLNK_OS_H

#include <linux/stddef.h>

#define XLNK_FLAG_COHERENT		0x00000001
#define XLNK_FLAG_KERNEL_BUFFER		0x00000002
#define XLNK_FLAG_DMAPOLLING		0x00000004
#define XLNK_FLAG_PHYSICAL_ADDR		0x00000100
#define XLNK_FLAG_VIRTUAL_ADDR		0x00000200

#define CF_FLAG_CACHE_FLUSH_INVALIDATE	0x00000001
#define CF_FLAG_PHYSICALLY_CONTIGUOUS	0x00000002
#define CF_FLAG_DMAPOLLING		0x00000004

enum xlnk_dma_direction {
	XLNK_DMA_BI = 0,
	XLNK_DMA_TO_DEVICE = 1,
	XLNK_DMA_FROM_DEVICE = 2,
	XLNK_DMA_NONE = 3,
};

struct dmabuf_args {
	int dmabuf_fd;
	void *user_vaddr;
};

struct xlnk_dmabuf_reg {
	int dmabuf_fd;
	void *user_vaddr;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *dbuf_attach;
	struct sg_table *dbuf_sg_table;
	int is_mapped;
	int dma_direction;
	struct list_head list;
};

union xlnk_args {
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int len;
		__aligned(4) int id;
		__aligned(4) unsigned int phyaddr;
		unsigned char cacheable;
	} allocbuf;
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int id;
		__aligned(4) void *buf;
	} freebuf;
	struct __attribute__ ((__packed__))  {
		__aligned(4) int dmabuf_fd;
		__aligned(4) void *user_addr;
	} dmabuf;
	struct __attribute__ ((__packed__))  {
		char name[64]; /* max length of 64 */
		/* return value */
		__aligned(4) unsigned int dmachan;
		/*for bd chain used by dmachan*/
		__aligned(4) unsigned int bd_space_phys_addr;
		/* bd chain size in bytes */
		__aligned(4) unsigned int bd_space_size;
	} dmarequest;
#define XLNK_MAX_APPWORDS 5
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int dmachan;
		/* buffer base address */
		__aligned(4) void *buf;
		/* used to point src_buf in cdma case */
		__aligned(4) void *buf2;
		/* used on kernel allocated buffers */
		__aligned(4) unsigned int buf_offset;
		__aligned(4) unsigned int len;
		/* zero all the time so far */
		__aligned(4) unsigned int bufflag;
		__aligned(4) unsigned int sglist; /* ignored */
		__aligned(4) unsigned int sgcnt; /* ignored */
		__aligned(4) int dmadir;
		__aligned(4) unsigned int nappwords_i;
		__aligned(4) unsigned int appwords_i[XLNK_MAX_APPWORDS];
		__aligned(4) unsigned int nappwords_o;
		/* appwords array we only accept 5 max */
		__aligned(4) unsigned int flag;
		/* return value */
		__aligned(4) unsigned int dmahandle;
		/*index of last bd used by request*/
		__aligned(4) unsigned int last_bd_index;
	} dmasubmit;
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int dmahandle;
		__aligned(4) unsigned int nappwords;
		__aligned(4) unsigned int appwords[XLNK_MAX_APPWORDS];
		/* appwords array we only accept 5 max */
	} dmawait;
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int dmachan;
	} dmarelease;
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int base;
		__aligned(4) unsigned int size;
		__aligned(4) unsigned int irqs[8];
		char name[32];
		__aligned(4) unsigned int id;
	} devregister;
	struct __attribute__ ((__packed__))  {
		__aligned(4) unsigned int base;
	} devunregister;
	struct __attribute__ ((__packed__))  {
		char name[32];
		__aligned(4) unsigned int id;
		__aligned(4) unsigned int base;
		__aligned(4) unsigned int size;
		__aligned(4) unsigned int chan_num;
		__aligned(4) unsigned int chan0_dir;
		__aligned(4) unsigned int chan0_irq;
		__aligned(4) unsigned int chan0_poll_mode;
		__aligned(4) unsigned int chan0_include_dre;
		__aligned(4) unsigned int chan0_data_width;
		__aligned(4) unsigned int chan1_dir;
		__aligned(4) unsigned int chan1_irq;
		__aligned(4) unsigned int chan1_poll_mode;
		__aligned(4) unsigned int chan1_include_dre;
		__aligned(4) unsigned int chan1_data_width;
	} dmaregister;
	struct __attribute__ ((__packed__))  {
		char name[32];
		__aligned(4) unsigned int id;
		__aligned(4) unsigned int base;
		__aligned(4) unsigned int size;
		__aligned(4) unsigned int mm2s_chan_num;
		__aligned(4) unsigned int mm2s_chan_irq;
		__aligned(4) unsigned int s2mm_chan_num;
		__aligned(4) unsigned int s2mm_chan_irq;
	} mcdmaregister;
	struct __attribute__ ((__packed__))  {
		__aligned(4) void *phys_addr;
		__aligned(4) int size;
		__aligned(4) int action;
	} cachecontrol;
};


#endif
