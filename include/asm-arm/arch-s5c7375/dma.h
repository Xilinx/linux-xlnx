/*
 * linux/include/asm-arm/arch-s5c7375/dma-s5c7375.h
 * Generic S5C7375  DMA support
 * Copyright (C) 2002 SW.LEE <hitchcar@sec.samsung.com>
 * Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 */

#include <asm/hardware.h>
#include <linux/wait.h>

#ifndef __ASM_S5C7375_ARCH_DMA_H
#define __ASM_S5C7375_ARCH_DMA_H

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 * 
 */
#define MAX_DMA_ADDRESS		0x20000000
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

/*
 * The regular generic DMA interface is inappropriate for the
 * S5C7375 DMA model.  None of the S5C7375 specific drivers using
 * DMA are portable anyway so it's pointless to try to twist the
 * regular DMA API to accommodate them.
 */

/***************************************************************************/
/* this means that We will use arch/arm/mach/dma.h i.e generic dma module  */
#define MAX_DMA_CHANNELS	0
/****************************************************************************/

/*
 * The S5C7375 has four internal DMA channels.
 */
#define S5C7375_DMA_CHANNELS     6

#define MAX_S5C7375_DMA_CHANNELS S5C7375_DMA_CHANNELS


/*
 * All possible S5C7375 devices the specific DMA channel can be attached to.
 * I'm sorry for only DMA Device Address
 * DMA request sources would be controlled by H/W DMA mode selected by DCON register
 */

typedef enum {
      DMA0_SOURCE0 ,		/* EXT device 0 */
      DMA0_SOURCE1 ,		/* UART1  */
      DMA0_SOURCE2 ,		/* USB  */
      DMA0_SOURCE3 ,		/* PPIC  */
      DMA0_SOURCE4 ,		/* UART0 */
      DMA0_SOURCE5 ,		/* EXT device 1 */
} dma_device_t;


/*
 * DMA buffer structure
 */

typedef struct dma_buf_s {
	int size;		/* buffer size */
	dma_addr_t dma_start;	/* starting DMA address */
	dma_addr_t dma_ptr;	/* next DMA pointer to use */
	int ref;		/* number of DMA references */
	void *id;		/* to identify buffer from outside */
	struct dma_buf_s *next;	/* next buffer to process */
} dma_buf_t;


/*
 * DMA channel structure.
 */

/*
 * DMA control register structure
 * one channel S5C7375 DMA control register is 0x40 byte
 *
 */
 
typedef void (*dma_callback_t)( void *buf_id, int size);

typedef struct {
	volatile u_long DISRC;
	volatile u_long DISRCC;
	volatile u_long DIDST;
	volatile u_long DIDSTC;
	volatile u_long DCON;
	volatile u_long DSTAT;
	volatile u_long DCSRC;
	volatile u_long DCDST;
	volatile u_long DMASKTRIG;
} dma_regs_t;

#define DOUBLE_BUFFER_COUNT 3
typedef struct {
	unsigned int in_use;	/* Device is allocated */
	const char *device_id;	/* Device name */
	dma_device_t device;	/* ... to which this channel is attached */
	dma_buf_t *head;	/* where to insert buffers */
	dma_buf_t *tail;	/* where to remove buffers */
	dma_buf_t *curr;	/* buffer currently DMA'ed */
	int stopped;		/* 1 if DMA is stalled */
	dma_regs_t *regs;	/* points to appropriate DMA registers */
	int irq;		/* IRQ used by the channel */
	dma_callback_t callback; /* ... to call when buffers are done */

	unsigned int queueCnt;
	unsigned int usedQueueCnt;	
	int	isSleeping;
	int spin_size;		/* > 0 when DMA should spin when no more buffer */
	dma_addr_t spin_addr;	/* DMA address to spin onto */
	int spin_ref;		/* number of spinning references */

        unsigned char already_init;	/* S5C7375 specific  */
} s5c7375_dma_t;

/* S5C7375 DMA API */
extern int s5c7375_request_dma( dmach_t channel, const char *device_id,
			       dma_device_t device );
extern int s5c7375_dma_set_callback( dmach_t channel, dma_callback_t cb );
extern int s5c7375_dma_set_spin( dmach_t channel, dma_addr_t addr, int size );
extern int s5c7375_dma_queue_buffer( dmach_t channel, void *buf_id,
				    dma_addr_t data, int size );
extern int s5c7375_dma_get_current( dmach_t channel, void **buf_id, dma_addr_t *addr );
extern int s5c7375_dma_stop( dmach_t channel );
extern int s5c7375_dma_resume( dmach_t channel );
extern int s5c7375_dma_flush_all( dmach_t channel );
extern void s5c7375_free_dma( dmach_t channel );
extern int s5c7375_dma_sleep( dmach_t channel );
extern int s5c7375_dma_wakeup( dmach_t channel );
extern void s5c7375_dma_done (s5c7375_dma_t *dma);

#endif /* _ASM_S5C7375_ARCH_DMA_H */






