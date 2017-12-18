/*
 * Generic Generic NCR5380 driver
 *	
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * NCR53C400 extensions (c) 1994,1995,1996, Kevin Lentin
 *    K.Lentin@cs.monash.edu.au
 *
 * NCR53C400A extensions (c) 1996, Ingmar Baumgart
 *    ingmar@gonzo.schwaben.de
 *
 * DTC3181E extensions (c) 1997, Ronald van Cuijlenborg
 * ronald.van.cuijlenborg@tip.nl or nutty@dds.nl
 *
 * Added ISAPNP support for DTC436 adapters,
 * Thomas Sailer, sailer@ife.ee.ethz.ch
 *
 * See Documentation/scsi/g_NCR5380.txt for more info.
 */

#include <asm/io.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <scsi/scsi_host.h>
#include "g_NCR5380.h"
#include "NCR5380.h"
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/interrupt.h>

#define MAX_CARDS 8

/* old-style parameters for compatibility */
static int ncr_irq;
static int ncr_addr;
static int ncr_5380;
static int ncr_53c400;
static int ncr_53c400a;
static int dtc_3181e;
static int hp_c2502;
module_param(ncr_irq, int, 0);
module_param(ncr_addr, int, 0);
module_param(ncr_5380, int, 0);
module_param(ncr_53c400, int, 0);
module_param(ncr_53c400a, int, 0);
module_param(dtc_3181e, int, 0);
module_param(hp_c2502, int, 0);

static int irq[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "IRQ number(s)");

static int base[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
module_param_array(base, int, NULL, 0);
MODULE_PARM_DESC(base, "base address(es)");

static int card[] = { -1, -1, -1, -1, -1, -1, -1, -1 };
module_param_array(card, int, NULL, 0);
MODULE_PARM_DESC(card, "card type (0=NCR5380, 1=NCR53C400, 2=NCR53C400A, 3=DTC3181E, 4=HP C2502)");

MODULE_LICENSE("GPL");

#ifndef SCSI_G_NCR5380_MEM
/*
 * Configure I/O address of 53C400A or DTC436 by writing magic numbers
 * to ports 0x779 and 0x379.
 */
static void magic_configure(int idx, u8 irq, u8 magic[])
{
	u8 cfg = 0;

	outb(magic[0], 0x779);
	outb(magic[1], 0x379);
	outb(magic[2], 0x379);
	outb(magic[3], 0x379);
	outb(magic[4], 0x379);

	/* allowed IRQs for HP C2502 */
	if (irq != 2 && irq != 3 && irq != 4 && irq != 5 && irq != 7)
		irq = 0;
	if (idx >= 0 && idx <= 7)
		cfg = 0x80 | idx | (irq << 4);
	outb(cfg, 0x379);
}
#endif

static int generic_NCR5380_init_one(struct scsi_host_template *tpnt,
			struct device *pdev, int base, int irq, int board)
{
	unsigned int *ports;
	u8 *magic = NULL;
#ifndef SCSI_G_NCR5380_MEM
	int i;
	int port_idx = -1;
	unsigned long region_size;
#endif
	static unsigned int ncr_53c400a_ports[] = {
		0x280, 0x290, 0x300, 0x310, 0x330, 0x340, 0x348, 0x350, 0
	};
	static unsigned int dtc_3181e_ports[] = {
		0x220, 0x240, 0x280, 0x2a0, 0x2c0, 0x300, 0x320, 0x340, 0
	};
	static u8 ncr_53c400a_magic[] = {	/* 53C400A & DTC436 */
		0x59, 0xb9, 0xc5, 0xae, 0xa6
	};
	static u8 hp_c2502_magic[] = {	/* HP C2502 */
		0x0f, 0x22, 0xf0, 0x20, 0x80
	};
	int flags, ret;
	struct Scsi_Host *instance;
	struct NCR5380_hostdata *hostdata;
#ifdef SCSI_G_NCR5380_MEM
	void __iomem *iomem;
	resource_size_t iomem_size;
#endif

	ports = NULL;
	flags = 0;
	switch (board) {
	case BOARD_NCR5380:
		flags = FLAG_NO_PSEUDO_DMA | FLAG_DMA_FIXUP;
		break;
	case BOARD_NCR53C400A:
		ports = ncr_53c400a_ports;
		magic = ncr_53c400a_magic;
		break;
	case BOARD_HP_C2502:
		ports = ncr_53c400a_ports;
		magic = hp_c2502_magic;
		break;
	case BOARD_DTC3181E:
		ports = dtc_3181e_ports;
		magic = ncr_53c400a_magic;
		break;
	}

#ifndef SCSI_G_NCR5380_MEM
	if (ports && magic) {
		/* wakeup sequence for the NCR53C400A and DTC3181E */

		/* Disable the adapter and look for a free io port */
		magic_configure(-1, 0, magic);

		region_size = 16;
		if (base)
			for (i = 0; ports[i]; i++) {
				if (base == ports[i]) {	/* index found */
					if (!request_region(ports[i],
							    region_size,
							    "ncr53c80"))
						return -EBUSY;
					break;
				}
			}
		else
			for (i = 0; ports[i]; i++) {
				if (!request_region(ports[i], region_size,
						    "ncr53c80"))
					continue;
				if (inb(ports[i]) == 0xff)
					break;
				release_region(ports[i], region_size);
			}
		if (ports[i]) {
			/* At this point we have our region reserved */
			magic_configure(i, 0, magic); /* no IRQ yet */
			outb(0xc0, ports[i] + 9);
			if (inb(ports[i] + 9) != 0x80) {
				ret = -ENODEV;
				goto out_release;
			}
			base = ports[i];
			port_idx = i;
		} else
			return -EINVAL;
	}
	else
	{
		/* NCR5380 - no configuration, just grab */
		region_size = 8;
		if (!base || !request_region(base, region_size, "ncr5380"))
			return -EBUSY;
	}
#else
	iomem_size = NCR53C400_region_size;
	if (!request_mem_region(base, iomem_size, "ncr5380"))
		return -EBUSY;
	iomem = ioremap(base, iomem_size);
	if (!iomem) {
		release_mem_region(base, iomem_size);
		return -ENOMEM;
	}
#endif
	instance = scsi_host_alloc(tpnt, sizeof(struct NCR5380_hostdata));
	if (instance == NULL) {
		ret = -ENOMEM;
		goto out_release;
	}
	hostdata = shost_priv(instance);

#ifndef SCSI_G_NCR5380_MEM
	instance->io_port = base;
	instance->n_io_port = region_size;
	hostdata->io_width = 1; /* 8-bit PDMA by default */

	/*
	 * On NCR53C400 boards, NCR5380 registers are mapped 8 past
	 * the base address.
	 */
	switch (board) {
	case BOARD_NCR53C400:
		instance->io_port += 8;
		hostdata->c400_ctl_status = 0;
		hostdata->c400_blk_cnt = 1;
		hostdata->c400_host_buf = 4;
		break;
	case BOARD_DTC3181E:
		hostdata->io_width = 2;	/* 16-bit PDMA */
		/* fall through */
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		hostdata->c400_ctl_status = 9;
		hostdata->c400_blk_cnt = 10;
		hostdata->c400_host_buf = 8;
		break;
	}
#else
	instance->base = base;
	hostdata->iomem = iomem;
	hostdata->iomem_size = iomem_size;
	switch (board) {
	case BOARD_NCR53C400:
		hostdata->c400_ctl_status = 0x100;
		hostdata->c400_blk_cnt = 0x101;
		hostdata->c400_host_buf = 0x104;
		break;
	case BOARD_DTC3181E:
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		pr_err(DRV_MODULE_NAME ": unknown register offsets\n");
		ret = -EINVAL;
		goto out_unregister;
	}
#endif

	ret = NCR5380_init(instance, flags | FLAG_LATE_DMA_SETUP);
	if (ret)
		goto out_unregister;

	switch (board) {
	case BOARD_NCR53C400:
	case BOARD_DTC3181E:
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		NCR5380_write(hostdata->c400_ctl_status, CSR_BASE);
	}

	NCR5380_maybe_reset_bus(instance);

	if (irq != IRQ_AUTO)
		instance->irq = irq;
	else
		instance->irq = NCR5380_probe_irq(instance, 0xffff);

	/* Compatibility with documented NCR5380 kernel parameters */
	if (instance->irq == 255)
		instance->irq = NO_IRQ;

	if (instance->irq != NO_IRQ) {
#ifndef SCSI_G_NCR5380_MEM
		/* set IRQ for HP C2502 */
		if (board == BOARD_HP_C2502)
			magic_configure(port_idx, instance->irq, magic);
#endif
		if (request_irq(instance->irq, generic_NCR5380_intr,
				0, "NCR5380", instance)) {
			printk(KERN_WARNING "scsi%d : IRQ%d not free, interrupts disabled\n", instance->host_no, instance->irq);
			instance->irq = NO_IRQ;
		}
	}

	if (instance->irq == NO_IRQ) {
		printk(KERN_INFO "scsi%d : interrupts not enabled. for better interactive performance,\n", instance->host_no);
		printk(KERN_INFO "scsi%d : please jumper the board for a free IRQ.\n", instance->host_no);
	}

	ret = scsi_add_host(instance, pdev);
	if (ret)
		goto out_free_irq;
	scsi_scan_host(instance);
	dev_set_drvdata(pdev, instance);
	return 0;

out_free_irq:
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);
out_unregister:
	scsi_host_put(instance);
out_release:
#ifndef SCSI_G_NCR5380_MEM
	release_region(base, region_size);
#else
	iounmap(iomem);
	release_mem_region(base, iomem_size);
#endif
	return ret;
}

static void generic_NCR5380_release_resources(struct Scsi_Host *instance)
{
	scsi_remove_host(instance);
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);
#ifndef SCSI_G_NCR5380_MEM
	release_region(instance->io_port, instance->n_io_port);
#else
	{
		struct NCR5380_hostdata *hostdata = shost_priv(instance);

		iounmap(hostdata->iomem);
		release_mem_region(instance->base, hostdata->iomem_size);
	}
#endif
	scsi_host_put(instance);
}

/**
 *	generic_NCR5380_pread - pseudo DMA read
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */
 
static inline int generic_NCR5380_pread(struct Scsi_Host *instance,
                                        unsigned char *dst, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int blocks = len / 128;
	int start = 0;

	NCR5380_write(hostdata->c400_ctl_status, CSR_BASE | CSR_TRANS_DIR);
	NCR5380_write(hostdata->c400_blk_cnt, blocks);
	while (1) {
		if (NCR5380_read(hostdata->c400_blk_cnt) == 0)
			break;
		if (NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400r: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; /* FIXME - no timeout */

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			insw(instance->io_port + hostdata->c400_host_buf,
							dst + start, 64);
		else
			insb(instance->io_port + hostdata->c400_host_buf,
							dst + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start,
		              hostdata->iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (blocks) {
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; /* FIXME - no timeout */

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			insw(instance->io_port + hostdata->c400_host_buf,
							dst + start, 64);
		else
			insb(instance->io_port + hostdata->c400_host_buf,
							dst + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start,
		              hostdata->iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ))
		printk("53C400r: no 53C80 gated irq after transfer");

	/* wait for 53C80 registers to be available */
	while (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_53C80_REG))
		;

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER))
		printk(KERN_ERR "53C400r: no end dma signal\n");
		
	return 0;
}

/**
 *	generic_NCR5380_pwrite - pseudo DMA write
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */

static inline int generic_NCR5380_pwrite(struct Scsi_Host *instance,
                                         unsigned char *src, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int blocks = len / 128;
	int start = 0;

	NCR5380_write(hostdata->c400_ctl_status, CSR_BASE);
	NCR5380_write(hostdata->c400_blk_cnt, blocks);
	while (1) {
		if (NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400w: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}

		if (NCR5380_read(hostdata->c400_blk_cnt) == 0)
			break;
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - timeout
#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			outsw(instance->io_port + hostdata->c400_host_buf,
							src + start, 64);
		else
			outsb(instance->io_port + hostdata->c400_host_buf,
							src + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(hostdata->iomem + NCR53C400_host_buffer,
		            src + start, 128);
#endif
		start += 128;
		blocks--;
	}
	if (blocks) {
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - no timeout

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			outsw(instance->io_port + hostdata->c400_host_buf,
							src + start, 64);
		else
			outsb(instance->io_port + hostdata->c400_host_buf,
							src + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(hostdata->iomem + NCR53C400_host_buffer,
		            src + start, 128);
#endif
		start += 128;
		blocks--;
	}

	/* wait for 53C80 registers to be available */
	while (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_53C80_REG)) {
		udelay(4); /* DTC436 chip hangs without this */
		/* FIXME - no timeout */
	}

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER)) {
		printk(KERN_ERR "53C400w: no end dma signal\n");
	}

	while (!(NCR5380_read(TARGET_COMMAND_REG) & TCR_LAST_BYTE_SENT))
		; 	// TIMEOUT
	return 0;
}

static int generic_NCR5380_dma_xfer_len(struct Scsi_Host *instance,
                                        struct scsi_cmnd *cmd)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int transfersize = cmd->transfersize;

	if (hostdata->flags & FLAG_NO_PSEUDO_DMA)
		return 0;

	/* Limit transfers to 32K, for xx400 & xx406
	 * pseudoDMA that transfers in 128 bytes blocks.
	 */
	if (transfersize > 32 * 1024 && cmd->SCp.this_residual &&
	    !(cmd->SCp.this_residual % transfersize))
		transfersize = 32 * 1024;

	/* 53C400 datasheet: non-modulo-128-byte transfers should use PIO */
	if (transfersize % 128)
		transfersize = 0;

	return transfersize;
}

/*
 *	Include the NCR5380 core code that we build our driver around	
 */
 
#include "NCR5380.c"

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= DRV_MODULE_NAME,
	.name			= "Generic NCR5380/NCR53C400 SCSI",
	.info			= generic_NCR5380_info,
	.queuecommand		= generic_NCR5380_queue_command,
	.eh_abort_handler	= generic_NCR5380_abort,
	.eh_bus_reset_handler	= generic_NCR5380_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
	.cmd_size		= NCR5380_CMD_SIZE,
	.max_sectors		= 128,
};


static int generic_NCR5380_isa_match(struct device *pdev, unsigned int ndev)
{
	int ret = generic_NCR5380_init_one(&driver_template, pdev, base[ndev],
					  irq[ndev], card[ndev]);
	if (ret) {
		if (base[ndev])
			printk(KERN_WARNING "Card not found at address 0x%03x\n",
			       base[ndev]);
		return 0;
	}

	return 1;
}

static int generic_NCR5380_isa_remove(struct device *pdev,
				   unsigned int ndev)
{
	generic_NCR5380_release_resources(dev_get_drvdata(pdev));
	dev_set_drvdata(pdev, NULL);
	return 0;
}

static struct isa_driver generic_NCR5380_isa_driver = {
	.match		= generic_NCR5380_isa_match,
	.remove		= generic_NCR5380_isa_remove,
	.driver		= {
		.name	= DRV_MODULE_NAME
	},
};

#if !defined(SCSI_G_NCR5380_MEM) && defined(CONFIG_PNP)
static struct pnp_device_id generic_NCR5380_pnp_ids[] = {
	{ .id = "DTC436e", .driver_data = BOARD_DTC3181E },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, generic_NCR5380_pnp_ids);

static int generic_NCR5380_pnp_probe(struct pnp_dev *pdev,
			       const struct pnp_device_id *id)
{
	int base, irq;

	if (pnp_activate_dev(pdev) < 0)
		return -EBUSY;

	base = pnp_port_start(pdev, 0);
	irq = pnp_irq(pdev, 0);

	return generic_NCR5380_init_one(&driver_template, &pdev->dev, base, irq,
				       id->driver_data);
}

static void generic_NCR5380_pnp_remove(struct pnp_dev *pdev)
{
	generic_NCR5380_release_resources(pnp_get_drvdata(pdev));
	pnp_set_drvdata(pdev, NULL);
}

static struct pnp_driver generic_NCR5380_pnp_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= generic_NCR5380_pnp_ids,
	.probe		= generic_NCR5380_pnp_probe,
	.remove		= generic_NCR5380_pnp_remove,
};
#endif /* !defined(SCSI_G_NCR5380_MEM) && defined(CONFIG_PNP) */

static int pnp_registered, isa_registered;

static int __init generic_NCR5380_init(void)
{
	int ret = 0;

	/* compatibility with old-style parameters */
	if (irq[0] == 0 && base[0] == 0 && card[0] == -1) {
		irq[0] = ncr_irq;
		base[0] = ncr_addr;
		if (ncr_5380)
			card[0] = BOARD_NCR5380;
		if (ncr_53c400)
			card[0] = BOARD_NCR53C400;
		if (ncr_53c400a)
			card[0] = BOARD_NCR53C400A;
		if (dtc_3181e)
			card[0] = BOARD_DTC3181E;
		if (hp_c2502)
			card[0] = BOARD_HP_C2502;
	}

#if !defined(SCSI_G_NCR5380_MEM) && defined(CONFIG_PNP)
	if (!pnp_register_driver(&generic_NCR5380_pnp_driver))
		pnp_registered = 1;
#endif
	ret = isa_register_driver(&generic_NCR5380_isa_driver, MAX_CARDS);
	if (!ret)
		isa_registered = 1;

	return (pnp_registered || isa_registered) ? 0 : ret;
}

static void __exit generic_NCR5380_exit(void)
{
#if !defined(SCSI_G_NCR5380_MEM) && defined(CONFIG_PNP)
	if (pnp_registered)
		pnp_unregister_driver(&generic_NCR5380_pnp_driver);
#endif
	if (isa_registered)
		isa_unregister_driver(&generic_NCR5380_isa_driver);
}

module_init(generic_NCR5380_init);
module_exit(generic_NCR5380_exit);
