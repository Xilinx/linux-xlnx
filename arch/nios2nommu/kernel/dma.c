/*
 * arch/nios2nommu/kernel/dma.c
 *
 * Copyright (C) 2005 Microtronix Datacom Ltd
 *
 * PC like DMA API for Nios's DMAC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Written by Wentao Xu <wentao@microtronix.com>
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <asm/dma.h>

/* nios2 dma controller register map */
#define REG_DMA_STATUS		0
#define REG_DMA_READADDR	4
#define REG_DMA_WRITEADDR	8
#define REG_DMA_LENGTH		12
#define	REG_DMA_CONTROL		24

/* status register bits definition */
#define ST_DONE			0x01
#define ST_BUSY			0x02
#define ST_REOP			0x04
#define ST_WROP			0x08
#define ST_LEN			0x10

/* control register bits definition */
#define CT_BYTE			0x01
#define CT_HW			0x02
#define CT_WORD			0x04
#define CT_GO			0x08
#define CT_IEEN			0x10
#define CT_REEN			0x20
#define CT_WEEN			0x40
#define CT_LEEN			0x80
#define CT_RCON			0x100
#define CT_WCON			0x200
#define CT_DOUBLE		0x400
#define CT_QUAD			0x800

struct dma_channel {
	unsigned int addr;  /* control address */
	unsigned int irq;	/* interrupt number */
	atomic_t idle;
	unsigned int mode;  /* dma mode: width, stream etc */
	int (*handler)(void*, int );
	void*	user;
	
	char id[16];
	char dev_id[16];
};
static struct dma_channel	dma_channels[]={
#ifdef na_dma_0
	{
		.addr	= na_dma_0,
		.irq	= na_dma_0_irq,
		.idle	= ATOMIC_INIT(1),
	},
#endif
#ifdef na_dma_1
	{
		.addr	= na_dma_1,
		.irq	= na_dma_1_irq,
		.idle	= ATOMIC_INIT(1),
	},
#endif
};
#define MAX_DMA_CHANNELS	sizeof(dma_channels)/sizeof(struct dma_channel)

void enable_dma(unsigned int dmanr)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		unsigned int ctl = dma_channels[dmanr].mode;
		ctl |= CT_GO | CT_IEEN;
		outl(ctl, dma_channels[dmanr].addr+REG_DMA_CONTROL);
	}
}

void disable_dma(unsigned int dmanr)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		unsigned int ctl = dma_channels[dmanr].mode;
		ctl &= ~(CT_GO | CT_IEEN);
		outl(ctl, dma_channels[dmanr].addr+REG_DMA_CONTROL);
	}
}

void set_dma_count(unsigned int dmanr, unsigned int count)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		dma_channels[dmanr].mode |= CT_LEEN;
		outl(count, dma_channels[dmanr].addr+REG_DMA_LENGTH);
	}
}

int get_dma_residue(unsigned int dmanr)
{
	int result =-1;
	if (dmanr < MAX_DMA_CHANNELS) {
		result = inl(dma_channels[dmanr].addr+REG_DMA_LENGTH);
	}
	return result;
}

int request_dma(unsigned int chan, const char *dev_id)
{
	struct dma_channel *channel;

	if ( chan >= MAX_DMA_CHANNELS) {
		return -EINVAL;
	}

	channel = &dma_channels[chan];
	
	if (!atomic_dec_and_test(&channel->idle)) {
		return -EBUSY;
	}

	strlcpy(channel->dev_id, dev_id, sizeof(channel->dev_id));
	channel->handler=NULL;
	channel->user=NULL;
	channel->mode =0;

	return 0;
}

void free_dma(unsigned int chan)
{
	if ( chan < MAX_DMA_CHANNELS) {
		dma_channels[chan].handler=NULL;
		dma_channels[chan].user=NULL;
		atomic_set(&dma_channels[chan].idle, 1);
	}
}

int nios2_request_dma(const char *dev_id)
{
	int chann;

	for ( chann=0; chann < MAX_DMA_CHANNELS; chann++) {
		if (request_dma(chann, dev_id)==0)
			return chann;
	}

	return -EINVAL;
}
void nios2_set_dma_handler(unsigned int dmanr, int (*handler)(void*, int), void* user)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		dma_channels[dmanr].handler=handler;
		dma_channels[dmanr].user=user;
	}	
}
#define NIOS2_DMA_WIDTH_MASK	(CT_BYTE | CT_HW | CT_WORD | CT_DOUBLE | CT_QUAD)
#define NIOS2_MODE_MASK (NIOS2_DMA_WIDTH_MASK | CT_REEN | CT_WEEN | CT_LEEN | CT_RCON | CT_WCON)
void nios2_set_dma_data_width(unsigned int dmanr, unsigned int width)
{
	if (dmanr < MAX_DMA_CHANNELS) {		
		 dma_channels[dmanr].mode &= ~NIOS2_DMA_WIDTH_MASK;
		 switch (width) {
		 	case 1:
			dma_channels[dmanr].mode |= CT_BYTE;
			break;
			case 2:
			dma_channels[dmanr].mode |= CT_HW;
			break;
			case 8:
			dma_channels[dmanr].mode |= CT_DOUBLE;
			break;
			case 16:
			dma_channels[dmanr].mode |= CT_QUAD;
			break;
			case 4:
			default:
			dma_channels[dmanr].mode |= CT_WORD;
			break;			
		 }
	}
}

void nios2_set_dma_rcon(unsigned int dmanr,unsigned int set)
{
	if (dmanr < MAX_DMA_CHANNELS) {		
		 dma_channels[dmanr].mode &= ~(CT_REEN | CT_RCON);
		 if (set)
		 	dma_channels[dmanr].mode |= (CT_REEN | CT_RCON);
	}
}
void nios2_set_dma_wcon(unsigned int dmanr,unsigned int set)
{
	if (dmanr < MAX_DMA_CHANNELS) {		
		 dma_channels[dmanr].mode &= ~(CT_WEEN | CT_WCON);
		 if (set)
		 	dma_channels[dmanr].mode |= (CT_WEEN | CT_WCON);
	}
}
void nios2_set_dma_mode(unsigned int dmanr, unsigned int mode)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		/* set_dma_mode is only allowed to change the bus width,
		   stream setting, etc.
		 */
		 mode &= NIOS2_MODE_MASK;
		 dma_channels[dmanr].mode &= ~NIOS2_MODE_MASK;
		 dma_channels[dmanr].mode |= mode;
	}
}

void nios2_set_dma_raddr(unsigned int dmanr, unsigned int a)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		outl(a, dma_channels[dmanr].addr+REG_DMA_READADDR);
	}
}
void nios2_set_dma_waddr(unsigned int dmanr, unsigned int a)
{
	if (dmanr < MAX_DMA_CHANNELS) {
		outl(a, dma_channels[dmanr].addr+REG_DMA_WRITEADDR);
	}
}


static irqreturn_t dma_isr(int irq, void *dev_id)
{
	struct dma_channel	*chann=(struct dma_channel*)dev_id;
	
	if (chann) {		
		int status = inl(chann->addr+REG_DMA_STATUS);
		/* ack the interrupt, and clear the DONE bit */
		outl(0, chann->addr+REG_DMA_STATUS);
		/* call the peripheral callback */
		if (chann->handler)
			chann->handler(chann->user, status);
	}

	return IRQ_HANDLED;
}



#ifdef CONFIG_PROC_FS
static int proc_dma_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0 ; i < MAX_DMA_CHANNELS ; i++) {
		if (!atomic_read(&dma_channels[i].idle)) {
		    seq_printf(m, "%2d: %s\n", i,
			       dma_channels[i].dev_id);
		}
	}
	return 0;
}

static int proc_dma_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dma_show, NULL);
}
static struct file_operations proc_dma_operations = {
	.open		= proc_dma_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_dma_init(void)
{
	struct proc_dir_entry *e;

	e = create_proc_entry("dma", 0, NULL);
	if (e)
		e->proc_fops = &proc_dma_operations;

	return 0;
}

__initcall(proc_dma_init);

#endif /* CONFIG_PROC_FS */

int __init init_dma(void)
{
	int i;
		
	for (i = 0 ; i < MAX_DMA_CHANNELS ; i++) {
		sprintf(dma_channels[i].id, "dmac-%d", i);
		/* disable the dmac channel */
		disable_dma(i);
		/* request irq*/
		if (request_irq(dma_channels[i].irq, dma_isr, 0, dma_channels[i].id, (void*)&dma_channels[i])){
			printk("DMA controller %d failed to get irq %d\n", i, dma_channels[i].irq);
			atomic_set(&dma_channels[i].idle, 0);
		}
	}
	return 0;
}

static void __exit exit_dma(void)
{
	int i;
		
	for (i = 0 ; i < MAX_DMA_CHANNELS ; i++) {
		/* disable the dmac channel */
		disable_dma(i);
		free_irq(dma_channels[i].irq, dma_channels[i].id);
	}
}

module_init(init_dma);
module_exit(exit_dma);

MODULE_LICENSE("GPL");

//EXPORT_SYMBOL(claim_dma_lock);
//EXPORT_SYMBOL(release_dma_lock);
EXPORT_SYMBOL(enable_dma);
EXPORT_SYMBOL(disable_dma);
EXPORT_SYMBOL(set_dma_count);
EXPORT_SYMBOL(get_dma_residue);
EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
EXPORT_SYMBOL(nios2_request_dma);
EXPORT_SYMBOL(nios2_set_dma_handler);
EXPORT_SYMBOL(nios2_set_dma_data_width);
EXPORT_SYMBOL(nios2_set_dma_rcon);
EXPORT_SYMBOL(nios2_set_dma_wcon);
EXPORT_SYMBOL(nios2_set_dma_mode);
EXPORT_SYMBOL(nios2_set_dma_raddr);
EXPORT_SYMBOL(nios2_set_dma_waddr);

