/*
 *
 * Normal mappings of chips in physical memory.
 *
 * Copyright (C) 2005,  Freescale Semiconductor (Matt.Waddel@freescale.com)
 * Copyright (C) 2005,  Intec Automation Inc. (mike@steroidmicros.com)
 * Copyright (C) 2001-2002, David McCullough <davidm@snapgear.com>
 *
 * Based on snapgear-uc.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/kdev_t.h>
#include <linux/ioport.h>
#include <linux/devfs_fs_kernel.h>

extern dev_t ROOT_DEV;

#define WINDOW_ADDR 0x00000000
#define WINDOW_SIZE 0x00200000
#define BANKWIDTH 2

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

static struct mtd_info *ram_mtdinfo;
static struct map_info m520x_ram_map = {
	.name		= "RAM",
};
static struct mtd_partition m520x_romfs[] = {
	{
		.name	= "Romfs"
	}
};

static struct mtd_info *flash_mtdinfo;
struct map_info m520x_flash_map = {
	.name		= "Am29BDD160G 2.5v flash device (2MB)",
	.size		= WINDOW_SIZE,
	.bankwidth	= BANKWIDTH
};
static struct mtd_partition m520x_partitions[] = {
        {
                .name	= "dBUG (256K)",
                .size	= 0x40000,
                .offset	= 0x00000
        },
        {
                .name	= "User FS (1792K)",
                .size	= 0x1C0000,
                .offset	= 0x40000
        }
};

/****************************************************************************
 *
 * Find the MTD device with the given name
 *
 ****************************************************************************/

static struct mtd_info 
*get_mtd_named(char *name)
{
	int i;
	struct mtd_info *mtd;

	for (i = 0; i < MAX_MTD_DEVICES; i++) {
		mtd = get_mtd_device(NULL, i);

		if (mtd) {
			if (strcmp(mtd->name, name) == 0)
				return(mtd);
			put_mtd_device(mtd);
		}
	}
	return(NULL);
}


static int
m520x_point(struct mtd_info *mtd, loff_t from, size_t len,
            size_t *retlen, u_char **mtdbuf)
{
	struct map_info *map = (struct map_info *) mtd->priv;
	*mtdbuf = (u_char *) (map->map_priv_1 + (int)from);
	*retlen = len;
	return(0);
}


static int __init
m520x_probe(int type, unsigned long addr, int size, int bankwidth)
{
	static struct mtd_info *mymtd;
	struct map_info *map_ptr;

	if (type)
		map_ptr = &m520x_ram_map;
	else
		map_ptr = &m520x_flash_map;

	map_ptr->bankwidth = bankwidth;
	map_ptr->map_priv_2 = addr;
	map_ptr->phys = addr;
	map_ptr->size = size;

	printk(KERN_NOTICE "m520xevb %s probe(0x%lx,%x,%x): %lx at %lx\n",
			   type ? "ram":"flash", addr, size, bankwidth, 
			   map_ptr->size, map_ptr->map_priv_2);

	map_ptr->virt = (unsigned long)
			ioremap_nocache(map_ptr->map_priv_2, map_ptr->size);

	simple_map_init(map_ptr);
	if (type)
		mymtd = do_map_probe("map_ram", map_ptr);
	else
		mymtd = do_map_probe("cfi_probe", map_ptr);

	if (!mymtd) {
		iounmap((void *)map_ptr->map_priv_1);
		return -ENXIO;
	}

	mymtd->owner  = THIS_MODULE;
	mymtd->point  = m520x_point;
	mymtd->priv   = map_ptr;

	if (type) {
		ram_mtdinfo = mymtd;
		add_mtd_partitions(mymtd, m520x_romfs, NB_OF(m520x_romfs));
	} else {
		flash_mtdinfo = mymtd;
		add_mtd_partitions(mymtd, m520x_partitions, 
			sizeof(m520x_partitions) / sizeof(struct mtd_partition));
	}
	return(0);
}


/*
 * Initialize the mtd devices
 */

int __init init_m520x(void)
{
	int rc = -1;
	struct mtd_info *mtd;
	extern char _ebss;

	rc = m520x_probe( 0, WINDOW_ADDR, WINDOW_SIZE, BANKWIDTH);

	/* Map in the filesystem from RAM last so that, if the filesystem
	 * is not in RAM for some reason we do not change the minor/major
	 * for the flash devices
	 */
#ifndef CONFIG_ROMFS_FROM_ROM
	if (0 != m520x_probe( 1, (unsigned long)&_ebss,
			      PAGE_ALIGN(*(unsigned long *)(&_ebss + 8)), 4))
		printk("Failed to probe RAM filesystem\n");
#else
	{
		unsigned long start_area;
		unsigned char *sp, *ep;
		size_t len;

		start_area = (unsigned long) &_ebss;

		/* If romfs is in flash use it to boot */
		if (strncmp((char *) start_area, "-rom1fs-", 8) != 0) {
			mtd = get_mtd_named("Image");
			if (mtd && mtd->point) {
				if ((*mtd->point)(mtd, 0, mtd->size, &len, &sp) == 0){
					ep = sp + len;
					while (sp < ep && strncmp(sp,"-rom1fs-",8) != 0)
						sp++;
					if (sp < ep)
						start_area = (unsigned long) sp;
				}
			}
			if (mtd)
				put_mtd_device(mtd);
		}
		if (0 != m520x_probe(1, start_area, 
				     PAGE_ALIGN(*(unsigned long *)(start_area + 8)), 4))
			printk("Failed to probe RAM filesystem\n");
	}
#endif

	mtd = get_mtd_named("Romfs");
	if (mtd) {
		ROOT_DEV = MKDEV(MTD_BLOCK_MAJOR, mtd->index);
		put_mtd_device(mtd);
	} else
		printk("%s: Failed to make root filesystem\n", __FUNCTION__);

	mtd = get_mtd_named("User FS (1792K)");
	if (mtd) {
		blk_register_region(MKDEV(MTD_BLOCK_MAJOR, mtd->index), MAX_MTD_DEVICES,
				THIS_MODULE, m520x_probe, NULL, NULL);
		put_mtd_device(mtd);
	} else
		printk("%s: Failed to flash filesystem\n", __FUNCTION__);

	return(rc);
}

static void __exit cleanup_m520x(void)
{
	if (flash_mtdinfo) {
		del_mtd_partitions(flash_mtdinfo);
		map_destroy(flash_mtdinfo);
		flash_mtdinfo = NULL;
	}
	if (ram_mtdinfo) {
		del_mtd_partitions(ram_mtdinfo);
		map_destroy(ram_mtdinfo);
		ram_mtdinfo = NULL;
	}
	if (m520x_ram_map.map_priv_1) {
		iounmap((void *)m520x_ram_map.map_priv_1);
		m520x_ram_map.map_priv_1 = 0;
	}
	if (m520x_flash_map.map_priv_1) {
		iounmap((void *)m520x_flash_map.map_priv_1);
		m520x_flash_map.map_priv_1 = 0;
	}

}

module_init(init_m520x);
module_exit(cleanup_m520x);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("<Matt.Waddel@freescale.com>");
MODULE_DESCRIPTION("MTD map for M520xEVB");
