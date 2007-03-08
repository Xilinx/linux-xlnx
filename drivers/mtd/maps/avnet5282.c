/*
 *
 * Normal mappings of chips in physical memory
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#define WINDOW_ADDR 0xff800000	// Flash Start addr.
#define WINDOW_SIZE 0x00800000	// Flash Size: 8 MB
#define BUSWIDTH 2

#define NUM_PARTITIONS (sizeof(avnet5282_partitions) / sizeof (avnet5282_partitions[0]))

/*************************************************************************************/

static struct mtd_partition avnet5282_partitions[] = {
        {
		.name		=	"uboot (256 KB)",
		.size		=	0x40000,
		.offset		=	0x0,
		.mask_flags	=	MTD_WRITEABLE
        },
        {
		.name		=	"kernel (3 MB)",
		.size		=	0x300000,
		.offset		=	0x40000
        },
        {
		.name		=	"rootfs (4,75 MB)",
		.size		=	0x4C0000,
		.offset		=	0x340000
        }
};

/*************************************************************************************/

struct map_info avnet5282_map = {
	.name		=	"MCF5282 flash",
	.size		=	WINDOW_SIZE,
        .phys           =       WINDOW_ADDR,
	.bankwidth	=	BUSWIDTH
};

static struct mtd_info *mymtd;

/*************************************************************************************/

static int __init init_avnet5282(void)
{
        avnet5282_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
	
        if (avnet5282_map.virt == 0) {
		printk(KERN_NOTICE "Failed to ioremap FLASH memory area.\n");
                return -EIO;
        }
        simple_map_init(&avnet5282_map);

        mymtd = do_map_probe("cfi_probe", &avnet5282_map);
        
	if(!mymtd){
		printk(KERN_NOTICE "Flash 5282 error, can't map");
		iounmap((void *)avnet5282_map.virt);
		return -ENXIO;
	}
        printk(KERN_NOTICE "MCF5282 flash device: %dMiB at 0x%08x\n", mymtd->size >> 20, WINDOW_ADDR);

	mymtd->owner = THIS_MODULE;
	mymtd->erasesize = 0x40000;

	add_mtd_partitions(mymtd, avnet5282_partitions, NUM_PARTITIONS);
        return 0;
}

/*************************************************************************************/

static void __exit cleanup_avnet5282(void)
{
        if (mymtd) {
                del_mtd_partitions(mymtd);
                map_destroy(mymtd);
        }
        if (avnet5282_map.virt) {
                iounmap((void *)avnet5282_map.virt);
                avnet5282_map.virt = 0;
        }
	return;
}

/*************************************************************************************/

module_init(init_avnet5282);
module_exit(cleanup_avnet5282);

MODULE_AUTHOR("Daniel Alomar i Claramonte");
MODULE_DESCRIPTION("Mapejat Xip Flash 28F640JA");
MODULE_LICENSE("GPL");

/*************************************************************************************/
