/*
 * Copyright (c) 2006, emlix, Thomas Brinker <tb@emlix.com>
 *
 * Handle mapping of the flash on the COBRA5329 boards
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

#define WINDOW_ADDR 0x00000000
#define WINDOW_SIZE 0x01000000

static struct mtd_info *mymtd;

static struct map_info cobra5329_map = {
	.name = "COBRA5329Flash",
	.size = WINDOW_SIZE,
	.bankwidth = 2,
	.phys = WINDOW_ADDR,
};

static struct mtd_partition cobra5329_partitions[] =
{
	{
		.name		= "bootloader",
		.size		= 1*1024*1024,
		.offset		= 0x00000000,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "kernel",
		.size		= 6*1024*1024,
		.offset		= MTDPART_OFS_APPEND,
		//.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "data",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND
	}
};

int __init init_cobra5329mtd(void)
{
	printk(KERN_NOTICE "Cobra5329 flash device: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);
	cobra5329_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
/*
Because of the odd place of flash we cannot check if ioremap has succeded
	if (!cobra5329_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
*/
	simple_map_init(&cobra5329_map);
	mymtd = do_map_probe("cfi_probe", &cobra5329_map);

	if (mymtd) {
		mymtd->owner = THIS_MODULE;
		add_mtd_partitions(mymtd, cobra5329_partitions, NB_OF(cobra5329_partitions));
		return 0;
	} else
		printk("%s: do_map_probe() returned 0\n",__FUNCTION__);

//	iounmap((void *)cobra5329_map.virt);
	return -ENXIO;
}

static void __exit cleanup_cobra5329mtd(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (cobra5329_map.virt) {
		iounmap((void *)cobra5329_map.virt);
		cobra5329_map.virt = 0;
	}
}

module_init(init_cobra5329mtd);
module_exit(cleanup_cobra5329mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Brinker <tb@emlix.com>");
MODULE_DESCRIPTION("MTD map driver for Cobra5329 boards");
