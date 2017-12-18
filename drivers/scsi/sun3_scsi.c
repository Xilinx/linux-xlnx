/*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Sun3 DMA routines added by Sam Creasey (sammy@sammy.net)
 *
 * VME support added by Sam Creasey
 *
 * TODO: modify this driver to support multiple Sun3 SCSI VME boards
 *
 * Adapted from mac_scsinew.c:
 */
/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/dvma.h>

#include <scsi/scsi_host.h>
#include "sun3_scsi.h"

/* minimum number of bytes to do dma on */
#define DMA_MIN_SIZE                    129

/* Definitions for the core NCR5380 driver. */

#define NCR5380_implementation_fields   /* none */

#define NCR5380_read(reg)               sun3scsi_read(reg)
#define NCR5380_write(reg, value)       sun3scsi_write(reg, value)

#define NCR5380_queue_command           sun3scsi_queue_command
#define NCR5380_bus_reset               sun3scsi_bus_reset
#define NCR5380_abort                   sun3scsi_abort
#define NCR5380_info                    sun3scsi_info

#define NCR5380_dma_recv_setup(instance, data, count) (count)
#define NCR5380_dma_send_setup(instance, data, count) (count)
#define NCR5380_dma_residual(instance) \
        sun3scsi_dma_residual(instance)
#define NCR5380_dma_xfer_len(instance, cmd, phase) \
        sun3scsi_dma_xfer_len(cmd->SCp.this_residual, cmd)

#define NCR5380_acquire_dma_irq(instance)    (1)
#define NCR5380_release_dma_irq(instance)

#include "NCR5380.h"


extern int sun3_map_test(unsigned long, char *);

static int setup_can_queue = -1;
module_param(setup_can_queue, int, 0);
static int setup_cmd_per_lun = -1;
module_param(setup_cmd_per_lun, int, 0);
static int setup_sg_tablesize = -1;
module_param(setup_sg_tablesize, int, 0);
static int setup_hostid = -1;
module_param(setup_hostid, int, 0);

/* ms to wait after hitting dma regs */
#define SUN3_DMA_DELAY 10

/* dvma buffer to allocate -- 32k should hopefully be more than sufficient */
#define SUN3_DVMA_BUFSIZE 0xe000

static struct scsi_cmnd *sun3_dma_setup_done;
static unsigned char *sun3_scsi_regp;
static volatile struct sun3_dma_regs *dregs;
static struct sun3_udc_regs *udc_regs;
static unsigned char *sun3_dma_orig_addr;
static unsigned long sun3_dma_orig_count;
static int sun3_dma_active;
static unsigned long last_residual;

/*
 * NCR 5380 register access functions
 */

static inline unsigned char sun3scsi_read(int reg)
{
	return in_8(sun3_scsi_regp + reg);
}

static inline void sun3scsi_write(int reg, int value)
{
	out_8(sun3_scsi_regp + reg, value);
}

#ifndef SUN3_SCSI_VME
/* dma controller register access functions */

static inline unsigned short sun3_udc_read(unsigned char reg)
{
	unsigned short ret;

	dregs->udc_addr = UDC_CSR;
	udelay(SUN3_DMA_DELAY);
	ret = dregs->udc_data;
	udelay(SUN3_DMA_DELAY);
	
	return ret;
}

static inline void sun3_udc_write(unsigned short val, unsigned char reg)
{
	dregs->udc_addr = reg;
	udelay(SUN3_DMA_DELAY);
	dregs->udc_data = val;
	udelay(SUN3_DMA_DELAY);
}
#endif

// safe bits for the CSR
#define CSR_GOOD 0x060f

static irqreturn_t scsi_sun3_intr(int irq, void *dev)
{
	struct Scsi_Host *instance = dev;
	unsigned short csr = dregs->csr;
	int handled = 0;

#ifdef SUN3_SCSI_VME
	dregs->csr &= ~CSR_DMA_ENABLE;
#endif

	if(csr & ~CSR_GOOD) {
		if (csr & CSR_DMA_BUSERR)
			shost_printk(KERN_ERR, instance, "bus error in DMA\n");
		if (csr & CSR_DMA_CONFLICT)
			shost_printk(KERN_ERR, instance, "DMA conflict\n");
		handled = 1;
	}

	if(csr & (CSR_SDB_INT | CSR_DMA_INT)) {
		NCR5380_intr(irq, dev);
		handled = 1;
	}

	return IRQ_RETVAL(handled);
}

/* sun3scsi_dma_setup() -- initialize the dma controller for a read/write */
static unsigned long sun3scsi_dma_setup(struct Scsi_Host *instance,
                                void *data, unsigned long count, int write_flag)
{
	void *addr;

	if(sun3_dma_orig_addr != NULL)
		dvma_unmap(sun3_dma_orig_addr);

#ifdef SUN3_SCSI_VME
	addr = (void *)dvma_map_vme((unsigned long) data, count);
#else
	addr = (void *)dvma_map((unsigned long) data, count);
#endif
		
	sun3_dma_orig_addr = addr;
	sun3_dma_orig_count = count;

#ifndef SUN3_SCSI_VME
	dregs->fifo_count = 0;
	sun3_udc_write(UDC_RESET, UDC_CSR);
	
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
#endif
	
	/* set direction */
	if(write_flag)
		dregs->csr |= CSR_SEND;
	else
		dregs->csr &= ~CSR_SEND;
	
#ifdef SUN3_SCSI_VME
	dregs->csr |= CSR_PACK_ENABLE;

	dregs->dma_addr_hi = ((unsigned long)addr >> 16);
	dregs->dma_addr_lo = ((unsigned long)addr & 0xffff);

	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;
	dregs->fifo_count_hi = 0;
	dregs->fifo_count = 0;
#else
	/* byte count for fifo */
	dregs->fifo_count = count;

	sun3_udc_write(UDC_RESET, UDC_CSR);
	
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
	
	if(dregs->fifo_count != count) { 
		shost_printk(KERN_ERR, instance, "FIFO mismatch %04x not %04x\n",
		             dregs->fifo_count, (unsigned int) count);
		NCR5380_dprint(NDEBUG_DMA, instance);
	}

	/* setup udc */
	udc_regs->addr_hi = (((unsigned long)(addr) & 0xff0000) >> 8);
	udc_regs->addr_lo = ((unsigned long)(addr) & 0xffff);
	udc_regs->count = count/2; /* count in words */
	udc_regs->mode_hi = UDC_MODE_HIWORD;
	if(write_flag) {
		if(count & 1)
			udc_regs->count++;
		udc_regs->mode_lo = UDC_MODE_LSEND;
		udc_regs->rsel = UDC_RSEL_SEND;
	} else {
		udc_regs->mode_lo = UDC_MODE_LRECV;
		udc_regs->rsel = UDC_RSEL_RECV;
	}
	
	/* announce location of regs block */
	sun3_udc_write(((dvma_vtob(udc_regs) & 0xff0000) >> 8),
		       UDC_CHN_HI); 

	sun3_udc_write((dvma_vtob(udc_regs) & 0xffff), UDC_CHN_LO);

	/* set dma master on */
	sun3_udc_write(0xd, UDC_MODE);

	/* interrupt enable */
	sun3_udc_write(UDC_INT_ENABLE, UDC_CSR);
#endif
	
       	return count;

}

static inline unsigned long sun3scsi_dma_residual(struct Scsi_Host *instance)
{
	return last_residual;
}

static inline unsigned long sun3scsi_dma_xfer_len(unsigned long wanted_len,
                                                  struct scsi_cmnd *cmd)
{
	if (wanted_len < DMA_MIN_SIZE || cmd->request->cmd_type != REQ_TYPE_FS)
		return 0;

	return wanted_len;
}

static inline int sun3scsi_dma_start(unsigned long count, unsigned char *data)
{
#ifdef SUN3_SCSI_VME
	unsigned short csr;

	csr = dregs->csr;

	dregs->dma_count_hi = (sun3_dma_orig_count >> 16);
	dregs->dma_count_lo = (sun3_dma_orig_count & 0xffff);

	dregs->fifo_count_hi = (sun3_dma_orig_count >> 16);
	dregs->fifo_count = (sun3_dma_orig_count & 0xffff);

/*	if(!(csr & CSR_DMA_ENABLE))
 *		dregs->csr |= CSR_DMA_ENABLE;
 */
#else
    sun3_udc_write(UDC_CHN_START, UDC_CSR);
#endif
    
    return 0;
}

/* clean up after our dma is done */
static int sun3scsi_dma_finish(int write_flag)
{
	unsigned short __maybe_unused count;
	unsigned short fifo;
	int ret = 0;
	
	sun3_dma_active = 0;

#ifdef SUN3_SCSI_VME
	dregs->csr &= ~CSR_DMA_ENABLE;

	fifo = dregs->fifo_count;
	if (write_flag) {
		if ((fifo > 0) && (fifo < sun3_dma_orig_count))
			fifo++;
	}

	last_residual = fifo;
	/* empty bytes from the fifo which didn't make it */
	if ((!write_flag) && (dregs->csr & CSR_LEFT)) {
		unsigned char *vaddr;

		vaddr = (unsigned char *)dvma_vmetov(sun3_dma_orig_addr);

		vaddr += (sun3_dma_orig_count - fifo);
		vaddr--;

		switch (dregs->csr & CSR_LEFT) {
		case CSR_LEFT_3:
			*vaddr = (dregs->bpack_lo & 0xff00) >> 8;
			vaddr--;

		case CSR_LEFT_2:
			*vaddr = (dregs->bpack_hi & 0x00ff);
			vaddr--;

		case CSR_LEFT_1:
			*vaddr = (dregs->bpack_hi & 0xff00) >> 8;
			break;
		}
	}
#else
	// check to empty the fifo on a read
	if(!write_flag) {
		int tmo = 20000; /* .2 sec */
		
		while(1) {
			if(dregs->csr & CSR_FIFO_EMPTY)
				break;

			if(--tmo <= 0) {
				printk("sun3scsi: fifo failed to empty!\n");
				return 1;
			}
			udelay(10);
		}
	}

	dregs->udc_addr = 0x32;
	udelay(SUN3_DMA_DELAY);
	count = 2 * dregs->udc_data;
	udelay(SUN3_DMA_DELAY);

	fifo = dregs->fifo_count;
	last_residual = fifo;

	/* empty bytes from the fifo which didn't make it */
	if((!write_flag) && (count - fifo) == 2) {
		unsigned short data;
		unsigned char *vaddr;

		data = dregs->fifo_data;
		vaddr = (unsigned char *)dvma_btov(sun3_dma_orig_addr);
		
		vaddr += (sun3_dma_orig_count - fifo);

		vaddr[-2] = (data & 0xff00) >> 8;
		vaddr[-1] = (data & 0xff);
	}
#endif

	dvma_unmap(sun3_dma_orig_addr);
	sun3_dma_orig_addr = NULL;

#ifdef SUN3_SCSI_VME
	dregs->dma_addr_hi = 0;
	dregs->dma_addr_lo = 0;
	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;

	dregs->fifo_count = 0;
	dregs->fifo_count_hi = 0;

	dregs->csr &= ~CSR_SEND;
/*	dregs->csr |= CSR_DMA_ENABLE; */
#else
	sun3_udc_write(UDC_RESET, UDC_CSR);
	dregs->fifo_count = 0;
	dregs->csr &= ~CSR_SEND;

	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
#endif
	
	sun3_dma_setup_done = NULL;

	return ret;

}
	
#include "NCR5380.c"

#ifdef SUN3_SCSI_VME
#define SUN3_SCSI_NAME          "Sun3 NCR5380 VME SCSI"
#define DRV_MODULE_NAME         "sun3_scsi_vme"
#else
#define SUN3_SCSI_NAME          "Sun3 NCR5380 SCSI"
#define DRV_MODULE_NAME         "sun3_scsi"
#endif

#define PFX                     DRV_MODULE_NAME ": "

static struct scsi_host_template sun3_scsi_template = {
	.module			= THIS_MODULE,
	.proc_name		= DRV_MODULE_NAME,
	.name			= SUN3_SCSI_NAME,
	.info			= sun3scsi_info,
	.queuecommand		= sun3scsi_queue_command,
	.eh_abort_handler	= sun3scsi_abort,
	.eh_bus_reset_handler	= sun3scsi_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_NONE,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
	.cmd_size		= NCR5380_CMD_SIZE,
};

static int __init sun3_scsi_probe(struct platform_device *pdev)
{
	struct Scsi_Host *instance;
	int error;
	struct resource *irq, *mem;
	unsigned char *ioaddr;
	int host_flags = 0;
#ifdef SUN3_SCSI_VME
	int i;
#endif

	if (setup_can_queue > 0)
		sun3_scsi_template.can_queue = setup_can_queue;
	if (setup_cmd_per_lun > 0)
		sun3_scsi_template.cmd_per_lun = setup_cmd_per_lun;
	if (setup_sg_tablesize >= 0)
		sun3_scsi_template.sg_tablesize = setup_sg_tablesize;
	if (setup_hostid >= 0)
		sun3_scsi_template.this_id = setup_hostid & 7;

#ifdef SUN3_SCSI_VME
	ioaddr = NULL;
	for (i = 0; i < 2; i++) {
		unsigned char x;

		irq = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		mem = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!irq || !mem)
			break;

		ioaddr = sun3_ioremap(mem->start, resource_size(mem),
		                      SUN3_PAGE_TYPE_VME16);
		dregs = (struct sun3_dma_regs *)(ioaddr + 8);

		if (sun3_map_test((unsigned long)dregs, &x)) {
			unsigned short oldcsr;

			oldcsr = dregs->csr;
			dregs->csr = 0;
			udelay(SUN3_DMA_DELAY);
			if (dregs->csr == 0x1400)
				break;

			dregs->csr = oldcsr;
		}

		iounmap(ioaddr);
		ioaddr = NULL;
	}
	if (!ioaddr)
		return -ENODEV;
#else
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!irq || !mem)
		return -ENODEV;

	ioaddr = ioremap(mem->start, resource_size(mem));
	dregs = (struct sun3_dma_regs *)(ioaddr + 8);

	udc_regs = dvma_malloc(sizeof(struct sun3_udc_regs));
	if (!udc_regs) {
		pr_err(PFX "couldn't allocate DVMA memory!\n");
		iounmap(ioaddr);
		return -ENOMEM;
	}
#endif

	sun3_scsi_regp = ioaddr;

	instance = scsi_host_alloc(&sun3_scsi_template,
	                           sizeof(struct NCR5380_hostdata));
	if (!instance) {
		error = -ENOMEM;
		goto fail_alloc;
	}

	instance->io_port = (unsigned long)ioaddr;
	instance->irq = irq->start;

	error = NCR5380_init(instance, host_flags);
	if (error)
		goto fail_init;

	error = request_irq(instance->irq, scsi_sun3_intr, 0,
	                    "NCR5380", instance);
	if (error) {
		pr_err(PFX "scsi%d: IRQ %d not free, bailing out\n",
		       instance->host_no, instance->irq);
		goto fail_irq;
	}

	dregs->csr = 0;
	udelay(SUN3_DMA_DELAY);
	dregs->csr = CSR_SCSI | CSR_FIFO | CSR_INTR;
	udelay(SUN3_DMA_DELAY);
	dregs->fifo_count = 0;
#ifdef SUN3_SCSI_VME
	dregs->fifo_count_hi = 0;
	dregs->dma_addr_hi = 0;
	dregs->dma_addr_lo = 0;
	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;

	dregs->ivect = VME_DATA24 | (instance->irq & 0xff);
#endif

	NCR5380_maybe_reset_bus(instance);

	error = scsi_add_host(instance, NULL);
	if (error)
		goto fail_host;

	platform_set_drvdata(pdev, instance);

	scsi_scan_host(instance);
	return 0;

fail_host:
	free_irq(instance->irq, instance);
fail_irq:
	NCR5380_exit(instance);
fail_init:
	scsi_host_put(instance);
fail_alloc:
	if (udc_regs)
		dvma_free(udc_regs);
	iounmap(sun3_scsi_regp);
	return error;
}

static int __exit sun3_scsi_remove(struct platform_device *pdev)
{
	struct Scsi_Host *instance = platform_get_drvdata(pdev);

	scsi_remove_host(instance);
	free_irq(instance->irq, instance);
	NCR5380_exit(instance);
	scsi_host_put(instance);
	if (udc_regs)
		dvma_free(udc_regs);
	iounmap(sun3_scsi_regp);
	return 0;
}

static struct platform_driver sun3_scsi_driver = {
	.remove = __exit_p(sun3_scsi_remove),
	.driver = {
		.name	= DRV_MODULE_NAME,
	},
};

module_platform_driver_probe(sun3_scsi_driver, sun3_scsi_probe);

MODULE_ALIAS("platform:" DRV_MODULE_NAME);
MODULE_LICENSE("GPL");
