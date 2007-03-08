/*
 *  drivers/mtd/nand/m5329.c
 *
 *  Yaroslav Vinogradov (Yaroslav.Vinogradov@freescale.com)
 *  
 *  Copyright Freescale Semiconductor, Inc 2006
 *
 *  Using as template code from:
 *    drivers/mtd/nand/spia.c  Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or any later
 * version (at your option) as published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   M5329EVB board which utilizes the Toshiba part. This is
 *   a 16MB NAND Flash device
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/mcfsim.h>

/*
 * MTD structure for M5329EVB board
 */
static struct mtd_info *m5329_mtd = NULL;

/*
 * Values specific to the SPIA board (used with EP7212 processor)
 */
#define NAND_FLASH_ADDRESS	0xd0000000	/* Fash address mapping */

#define CLE_ADDR_BIT		4
#define ALE_ADDR_BIT		3
#define NCE_ADDR_BIT		19

/*
 * Module stuff
 */

static unsigned int m5329_fio_base = NAND_FLASH_ADDRESS;

module_param(m5329_fio_base, int, 0);

/*
 * Define partitions for flash device
 */
const static struct mtd_partition partition_info[] = {
	{
		.name	= "M5329 flash partition 1",
		.offset	= 0,
		.size	= 16*1024*1024
	},
};
#define NUM_PARTITIONS 1


/* 
 *	hardware specific access to control-lines
 */
static void m5329_hwcontrol(struct mtd_info *mtd, int cmd){
	struct nand_chip *this;
	unsigned int base;


	/* Get pointer to private data */
	this = (struct nand_chip *) (&m5329_mtd[1]);
	m5329_fio_base = (unsigned int)this->IO_ADDR_R;
	base = m5329_fio_base;

	switch(cmd){
	case NAND_CTL_SETCLE: 
		m5329_fio_base |= 1<<CLE_ADDR_BIT;
		break;
	case NAND_CTL_CLRCLE: 
		m5329_fio_base &= ~(1<<CLE_ADDR_BIT);
		break;
	case NAND_CTL_SETALE: 
		m5329_fio_base |= 1<<ALE_ADDR_BIT;
		break;
	case NAND_CTL_CLRALE: 
		m5329_fio_base &= ~(1<<ALE_ADDR_BIT);
		break;
	case NAND_CTL_SETNCE: 
		m5329_fio_base &= ~(1<<NCE_ADDR_BIT);
		break;
	case NAND_CTL_CLRNCE: 
		m5329_fio_base |= 1<NCE_ADDR_BIT;
		break;
	}
	/* Set address of NAND IO lines */
	this->IO_ADDR_R = (void __iomem *) m5329_fio_base;
	this->IO_ADDR_W = (void __iomem *) m5329_fio_base;
}

/*
 * Main initialization routine
 */
int __init m5329_init (void)
{
	struct nand_chip *this;

	/* Setup NAND flash chip select signals */
	MCF_FBCS2_CSAR = NAND_FLASH_ADDRESS;
	MCF_FBCS2_CSCR = (MCF_FBCS_CSCR_PS_8
			| MCF_FBCS_CSCR_BEM
			| MCF_FBCS_CSCR_AA
			| MCF_FBCS_CSCR_SBM
			| MCF_FBCS_CSCR_WS(7));
	MCF_FBCS2_CSMR = (MCF_FBCS_CSMR_BAM_16M
			| MCF_FBCS_CSMR_V);

	/* Allocate memory for MTD device structure and private data */
	m5329_mtd = kmalloc (sizeof(struct mtd_info)+sizeof (struct nand_chip),
				GFP_KERNEL);
	if (!m5329_mtd) {
		printk ("Unable to allocate M5329 NAND MTD device structure\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&m5329_mtd[1]);

	/* Initialize structures */
	memset((char *) m5329_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	m5329_mtd->priv = this;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = (void __iomem *) m5329_fio_base;
	this->IO_ADDR_W = (void __iomem *) m5329_fio_base;
	/* Set address of hardware control function */
	this->hwcontrol = m5329_hwcontrol;
	/* 50 us command delay time */
	this->chip_delay = 50;
	this->eccmode = NAND_ECC_SOFT;

	/* Scan to find existence of the device */
	if (nand_scan (m5329_mtd, 1)) {
		kfree (m5329_mtd);
		return -ENXIO;
	}

	/* Register the partitions */
	add_mtd_partitions(m5329_mtd, partition_info, NUM_PARTITIONS);

	/* Return happy */
	return 0;
}
module_init(m5329_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit m5329_cleanup (void)
{
	/* Release resources, unregister device */
	nand_release (m5329_mtd);

	/* Free the MTD device structure */
	kfree (m5329_mtd);
}
module_exit(m5329_cleanup);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yaroslav Vinogradov <Yaroslav.Vinogradov@freescale.com>");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on M5329 board");
