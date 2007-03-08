/*
 * Altera EPCS Configuration device MTD Driver
 * MTD Routines - read/write/erase etc...
 *
 * Jai Dhar, FPS-Tech <contact@fps-tech.net>
 * 
 * Currently works with 1,4, 16 and 64 MBit EPCS Devices
 * Module features:
 * - Module detects the presence of an EPCS Device, between 1,4,16 and 64 Mbit EPCS chips
 * - Automatically sets up EPCS Map physical size and erase block size
 * - Important: The correct base address for the EPCS Avalon component must be specified
 *				in the Kernel configuration. This will not search for the component. The best way
 *				to find the base address is to look at the base address in SOPC Builder, and then 
 *				in the SOPC Shell, use "nios2-flash-programmer --debug --epcs --base=<base>"
 *				It will then tell you the base address which the registers were found. This
 *				isn't necessarily the same as <base>.
 *
 * - Module tested with JFFS2 and ROMFS, both in RW and RO modes
 *
 * - TODO: Add transaction stats
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/compatmac.h>

#include "epcs.h"

static int epcs_read(struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int epcs_write(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int epcs_erase(struct mtd_info *, struct erase_info *);
static void epcs_nop(struct mtd_info *);
static struct mtd_info *epcs_probe(struct map_info *map);


static struct mtd_chip_driver epcs_chipdrv = {
	.probe	= epcs_probe,
	.name	= "epcs",
	.module	= THIS_MODULE
};

static struct mtd_info *epcs_probe(struct map_info *map)
{
	struct mtd_info *mtd;
	u_char x;

	printk(KERN_NOTICE "FPS-Tech EPCS MTD Driver (fps-tech.net)\n");

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	memset(mtd, 0, sizeof(*mtd));

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "Resetting EPCS\n");
	#endif

	epcs_reset();

	#if EPCS_DEBUG1
	printk(KERN_NOTICE "Using Avalon address: 0x%X\n",(u_int) map->phys);
	#endif

	#if EPCS_DEBUG2
	epcs_print_regs();
	#endif


	/* Check for EPCS Signature */
	switch (x=epcs_dev_find())
	{
		case EPCS_SIG_1MBIT:
			printk(KERN_NOTICE "1 Mbit EPCS Chip found\n");
			map->size = EPCS_SIZE_1MBIT;
			mtd->erasesize = EPCS_SECSIZE_32KB;
			break;

		case EPCS_SIG_4MBIT:
			printk(KERN_NOTICE "4 Mbit EPCS Chip found\n");
			map->size = EPCS_SIZE_4MBIT;
			mtd->erasesize = EPCS_SECSIZE_64KB;
			break;

		case EPCS_SIG_16MBIT:
			printk(KERN_NOTICE "16 Mbit EPCS Chip found\n");
			map->size = EPCS_SIZE_16MBIT;
			mtd->erasesize = EPCS_SECSIZE_64KB;
			break;

		case EPCS_SIG_64MBIT:
			printk(KERN_NOTICE "64 Mbit EPCS Chip found\n");
			map->size = EPCS_SIZE_64MBIT;
			mtd->erasesize = EPCS_SECSIZE_64KB;
			break;

		default:
			printk(KERN_NOTICE "No EPCS Chip found with ID: %d\n",x);
			return NULL;

	}


	map->fldrv = &epcs_chipdrv;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_NORFLASH;
	mtd->size = map->size;
	mtd->erase = epcs_erase;
	mtd->read = epcs_read;
	mtd->write = epcs_write;
	mtd->sync = epcs_nop;
	mtd->flags = MTD_CAP_NORFLASH;
	
	
	#if EPCS_DEBUG1
	printk(KERN_NOTICE "Setting EPCS Page size to %d bytes\n",mtd->erasesize);
	#endif

	__module_get(THIS_MODULE);
	return mtd;
}


static int epcs_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_read, len: 0x%lx, from: 0x%lx\n",(u_long) len,(u_long) from);
	#endif

	epcs_buf_read(buf,from,len);
	*retlen = len;
	return 0;
}

static int epcs_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_write, off: 0x%X, len: 0x%X\n",(u_int) to, (u_int) len);
	#endif

	epcs_buf_write (buf, (u_int) to, (u_int) len);
	
	*retlen = len;
	return 0;
}

static int epcs_erase (struct mtd_info *mtd, struct erase_info *instr)
{

	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_erase: off: 0x%X, len: 0x%X\n",(u_int) instr->addr, instr->len);
	#endif

	epcs_buf_erase(instr->addr, instr->len, mtd->erasesize);
	instr->state = MTD_ERASE_DONE;

	mtd_erase_callback(instr);

	return 0;
}

static void epcs_nop(struct mtd_info *mtd)
{
	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_nop\n");
	#endif
}


int __init epcs_init(void)
{
	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_init registering driver\n");
	#endif

	register_mtd_chip_driver(&epcs_chipdrv);
	return 0;
}

void __exit epcs_exit(void)
{
	unregister_mtd_chip_driver(&epcs_chipdrv);
	#if EPCS_DEBUG2
	printk(KERN_NOTICE "epcs_init un-registering driver\n");
	#endif
}

module_init(epcs_init);
module_exit(epcs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jai Dhar <contact@fps-tech.net>");
MODULE_DESCRIPTION("MTD chip driver for EPCS Chips");
