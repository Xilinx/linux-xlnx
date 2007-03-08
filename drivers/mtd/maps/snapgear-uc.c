/****************************************************************************/
/*
 * Flash memory access on uClinux SnapGear like devices
 * Copyright (C) 2001-2002, David McCullough <davidm@snapgear.com>
 */
/****************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/reboot.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/cfi.h>

#include <linux/fs.h>

#include <linux/major.h>
#include <linux/root_dev.h>

#include <linux/init.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/sections.h>

/****************************************************************************/

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

#define SIZE_128K	(1  *  128 * 1024)
#define SIZE_1MB	(1  * 1024 * 1024)
#define SIZE_2MB	(2  * 1024 * 1024)
#define SIZE_4MB	(4  * 1024 * 1024)
#define SIZE_8MB	(8  * 1024 * 1024)
#define SIZE_16MB	(16 * 1024 * 1024)

#ifdef CONFIG_COLDFIRE
#define FLASH_BASE	0xf0000000
#define	BUS_WIDTH	2
#endif

#ifdef CONFIG_SUPERH
#define FLASH_BASE	0x00000000
#define	BUS_WIDTH	1
#endif

/****************************************************************************/

static struct map_info nettel_flash_map = {
	    .name = "Flash",
};

static struct map_info nettel_ram_map = {
	    .name = "RAM",
};

static struct mtd_info *ram_mtdinfo;
static struct mtd_info *flash_mtdinfo;

/****************************************************************************/

static struct mtd_partition nettel_romfs[] = {
	{ .name = "Romfs" } 
};

#define NUM_PARTITIONS  (sizeof(nettel_romfs) / sizeof(nettel_romfs[0]))

/****************************************************************************/
/*
 *	The layout of our flash,  note the order of the names,  this
 *	means we use the same major/minor for the same purpose on all
 *	layouts (when possible)
 */

static struct mtd_partition nettel_128k[] = {
	{ .name = "Bootloader", .offset = 0x00000000, .size =   0x00004000 },
	{ .name = "Bootargs",   .offset = 0x00004000, .size =   0x00004000 },
	{ .name = "MAC",        .offset = 0x00008000, .size =   0x00004000 },
	{ .name = "Config",     .offset = 0x00010000, .size =   0x00010000 },
	{ .name = "Spare",      .offset = 0x0000c000, .size =   0x00004000 },
	{ .name = "Flash",      .offset = 0 }
};

static struct mtd_partition nettel_1mb[] = {
	{ .name = "Bootloader", .offset = 0x00000000, .size =   0x00004000 },
	{ .name = "Bootargs",   .offset = 0x00004000, .size =   0x00002000 },
	{ .name = "MAC",        .offset = 0x00006000, .size =   0x00002000 },
	{ .name = "Config",     .offset = 0x000f0000, .size =   0x00010000 },
	{ .name = "Spare",      .offset = 0x00008000, .size =   0x00008000 },
	{ .name = "Image",      .offset = 0x00010000, .size =   0x000e0000 },
	{ .name = "Flash",      .offset = 0 }
};

static struct mtd_partition nettel_2mb[] = {
	{ .name = "Bootloader", .offset = 0x00000000, .size =   0x00004000 },
	{ .name = "Bootargs",   .offset = 0x00004000, .size =   0x00002000 },
	{ .name = "MAC",        .offset = 0x00006000, .size =   0x00002000 },
	{ .name = "Config",     .offset = 0x00010000, .size =   0x00010000 },
	{ .name = "Spare",      .offset = 0x00008000, .size =   0x00008000 },
	{ .name = "Image",      .offset = 0x00020000, .size =   0x001e0000 },
	{ .name = "Flash",      .offset = 0 }
};

#ifdef CONFIG_SH_SECUREEDGE5410

static struct mtd_partition nettel_4mb[] = {
	{ .name = "Boot data",  .offset = 0x00000000, .size =   0x00020000 },
	{ .name = "Config",     .offset = 0x00020000, .size =   0x00040000 },
	{ .name = "Image",      .offset = 0x00060000, .size =   0x00000000 },
	{ .name = "Flash",      .offset = 0 }
};

static struct mtd_partition nettel_8mb[] = {
	{ .name = "Boot data",  .offset = 0x00000000, .size =   0x00020000 },
	{ .name = "Config",     .offset = 0x00020000, .size =   0x00080000 },
	{ .name = "Image",      .offset = 0x000a0000, .size =   0x00000000 },
	{ .name = "Flash",      .offset = 0 }
};

#else

static struct mtd_partition nettel_4mb[] = {
	{ .name = "Bootloader", .offset = 0x00000000, .size =   0x00004000 },
	{ .name = "Bootargs",   .offset = 0x00004000, .size =   0x00002000 },
	{ .name = "MAC",        .offset = 0x00006000, .size =   0x00002000 },
	{ .name = "Config",     .offset = 0x00010000, .size =   0x00010000 },
	{ .name = "Spare",      .offset = 0x00008000, .size =   0x00008000 },
	{ .name = "Image",      .offset = 0x00020000, .size =   0x001e0000 },
	{ .name = "Flash",      .offset = 0x00000000, .size =   0x00200000 },
	{ .name = "Image2",     .offset = 0x00220000, .size =   0x001e0000 },
	{ .name = "Flash2",     .offset = 0 }
};

static struct mtd_partition nettel_8mb[] = {
	{ .name = "Bootloader", .offset = 0x00000000, .size =   0x00020000 },
	{ .name = "Bootargs",   .offset = 0x00020000, .size =   0x00020000 },
	{ .name = "MAC",        .offset = 0x00040000, .size =   0x00020000 },
	{ .name = "Config",     .offset = 0x00080000, .size =   0x00080000 },
	{ .name = "Spare",      .offset = 0x00060000, .size =   0x00020000 },
	{ .name = "Image",      .offset = 0x00100000, .size =   0x00700000 },
	{ .name = "Flash",      .offset = 0 }
};

#endif

static struct mtd_partition nettel_16mb[] = {
	{ .name = "Boot data",  .offset = 0x00000000, .size =   0x00020000 },
	{ .name = "Config",     .offset = 0x00020000, .size =   0x00100000 },
	{ .name = "Image",      .offset = 0x00120000, .size =   0x00000000 },
	{ .name = "Flash",      .offset = 0 }
};

/****************************************************************************/
/*
 * Find the MTD device with the given name
 */

static struct mtd_info *get_mtd_named(char *name)
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

/****************************************************************************/
#ifdef CONFIG_MTD_CFI_INTELEXT
/*  
 *  Set the Intel flash back to read mode as MTD may leave it in command mode
 */

static int nettel_reboot_notifier(
    struct notifier_block *nb,
    unsigned long val,
    void *v)
{
    struct cfi_private *cfi = nettel_flash_map.fldrv_priv;
    int i;
 
    for (i = 0; cfi && i < cfi->numchips; i++)
        cfi_send_gen_cmd(0xff, 0x55, cfi->chips[i].start, &nettel_flash_map,
                cfi, cfi->device_type, NULL);
    
    return(NOTIFY_OK);
}

static struct notifier_block nettel_notifier_block = {
    nettel_reboot_notifier, NULL, 0 
};

#endif

/****************************************************************************/

static int
nettel_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char **mtdbuf)
{
	struct map_info *map = (struct map_info *) mtd->priv;
	*mtdbuf = (u_char *) (map->map_priv_1 + (int)from);
	*retlen = len;
	return(0);
}

/****************************************************************************/

static int __init
nettel_probe(int ram, unsigned long addr, int size, int buswidth)
{
	struct mtd_info *mymtd;
	struct map_info *map_ptr;

	if (ram)
		map_ptr = &nettel_ram_map;
	else
		map_ptr = &nettel_flash_map;

	map_ptr->bankwidth = buswidth;
	map_ptr->map_priv_2 = addr;
	map_ptr->phys = addr;
	map_ptr->size = size;

	printk(KERN_NOTICE "SnapGear %s probe(0x%lx,%d,%d): %lx at %lx\n",
			ram ? "ram" : "flash",
			addr, size, buswidth, map_ptr->size, map_ptr->map_priv_2);

	map_ptr->virt = ioremap_nocache(map_ptr->map_priv_2, map_ptr->size);

	if (!map_ptr->virt) {
		printk("Failed to ioremap_nocache\n");
		return -EIO;
	}

	simple_map_init(map_ptr);
	if (!ram) {
		mymtd = do_map_probe("cfi_probe", map_ptr);
		if (!mymtd)
			mymtd = do_map_probe("jedec_probe", map_ptr);
	} else
		mymtd = do_map_probe("map_ram", map_ptr);

	if (!mymtd) {
		iounmap((void *)map_ptr->map_priv_1);
		return -ENXIO;
	}
		
	mymtd->owner = THIS_MODULE;
	mymtd->point = nettel_point;
	mymtd->priv = map_ptr;

	if (ram) {
		ram_mtdinfo = mymtd;
		add_mtd_partitions(mymtd, nettel_romfs, NB_OF(nettel_romfs));
		return(0);
	}

	flash_mtdinfo = mymtd;
	switch (size) {
	case SIZE_128K:
		add_mtd_partitions(mymtd, nettel_128k, NB_OF(nettel_128k));
		break;
	case SIZE_1MB:
		add_mtd_partitions(mymtd, nettel_1mb, NB_OF(nettel_1mb));
		break;
	case SIZE_2MB:
		add_mtd_partitions(mymtd, nettel_2mb, NB_OF(nettel_2mb));
		break;
	case SIZE_4MB:
		add_mtd_partitions(mymtd, nettel_4mb, NB_OF(nettel_4mb));
		break;
	case SIZE_8MB:
		add_mtd_partitions(mymtd, nettel_8mb, NB_OF(nettel_8mb));
		break;
    case SIZE_16MB:
        add_mtd_partitions(mymtd, nettel_16mb, NB_OF(nettel_16mb));
        break;
	}

	return 0;
}

/****************************************************************************/

int __init nettel_mtd_init(void)
{
	int rc = -1;
	struct mtd_info *mtd;
#ifdef CONFIG_COLDFIRE
	extern char _ebss;
#endif

	/*
	 * I hate this ifdef stuff,  but our HW doesn't always have
	 * the same chipsize as the map that we use
	 */
#if defined(CONFIG_FLASH16MB) || defined(CONFIG_FLASHAUTO)
    if (rc != 0)
        rc = nettel_probe(0, FLASH_BASE, SIZE_16MB, BUS_WIDTH);
#endif

#if defined(CONFIG_FLASH8MB) || defined(CONFIG_FLASHAUTO)
	if (rc != 0)
		rc = nettel_probe(0, FLASH_BASE, SIZE_8MB, BUS_WIDTH);
#endif

#if defined(CONFIG_FLASH4MB) || defined(CONFIG_FLASHAUTO)
	if (rc != 0)
		rc = nettel_probe(0, FLASH_BASE, SIZE_4MB, BUS_WIDTH);
#endif

#if defined(CONFIG_FLASH2MB) || defined(CONFIG_FLASHAUTO)
	if (rc != 0)
		rc = nettel_probe(0, FLASH_BASE, SIZE_2MB, BUS_WIDTH);
#endif

#if defined(CONFIG_FLASH1MB) || defined(CONFIG_FLASHAUTO)
	if (rc != 0)
		rc = nettel_probe(0, FLASH_BASE, SIZE_1MB, BUS_WIDTH);
#endif

#if defined(CONFIG_FLASH128K) || defined(CONFIG_FLASHAUTO)
	if (rc != 0)
		rc = nettel_probe(0, FLASH_BASE, SIZE_128K, BUS_WIDTH);
#endif

#ifdef CONFIG_COLDFIRE
	/*
	 * Map in the filesystem from RAM last so that,  if the filesystem
	 * is not in RAM for some reason we do not change the minor/major
	 * for the flash devices
	 */
#ifndef CONFIG_ROMFS_FROM_ROM
	if (0 != nettel_probe(1, (unsigned long) &_ebss,
			PAGE_ALIGN(* (unsigned long *)((&_ebss) + 8)), 4))
		printk("Failed to probe RAM filesystem\n");
#else
	{
		unsigned long start_area;
		unsigned char *sp, *ep;
		size_t len;

		start_area = (unsigned long) &_ebss;

		if (strncmp((char *) start_area, "-rom1fs-", 8) != 0) {
			mtd = get_mtd_named("Image");
			if (mtd && mtd->point) {
				if ((*mtd->point)(mtd, 0, mtd->size, &len, &sp) == 0) {
					ep = sp + len;
					while (sp < ep && strncmp(sp, "-rom1fs-", 8) != 0)
						sp++;
					if (sp < ep)
						start_area = (unsigned long) sp;
				}
			}
			if (mtd)
				put_mtd_device(mtd);
		}
		if (0 != nettel_probe(1, start_area,
				PAGE_ALIGN(* (unsigned long *)(start_area + 8)), 4))
			printk("Failed to probe RAM filesystem\n");
	}
#endif
	
	mtd = get_mtd_named("Romfs");
	if (mtd) {
		ROOT_DEV = MKDEV(MTD_BLOCK_MAJOR, mtd->index);
		put_mtd_device(mtd);
	} else
		printk("%s: Failed to find & make root filesystem\n", __FUNCTION__);
#endif

#ifdef CONFIG_SH_SECUREEDGE5410
#if defined(CONFIG_NFTL) || defined(CONFIG_INFTL)
        ROOT_DEV = MKDEV(NFTL_MAJOR, 1); 
#else
        mtd = get_mtd_named("Image");
        if (mtd) {
            ROOT_DEV = MKDEV(MTD_BLOCK_MAJOR, mtd->index);
            put_mtd_device(mtd);
        }
#endif
#endif 

#ifdef CONFIG_MTD_CFI_INTELEXT
    register_reboot_notifier(&nettel_notifier_block);
#endif

	return(rc);
}

/****************************************************************************/

static void __exit nettel_mtd_cleanup(void)
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
	if (nettel_ram_map.map_priv_1) {
		iounmap((void *)nettel_ram_map.map_priv_1);
		nettel_ram_map.map_priv_1 = 0;
	}
	if (nettel_flash_map.map_priv_1) {
		iounmap((void *)nettel_flash_map.map_priv_1);
		nettel_flash_map.map_priv_1 = 0;
	}
}

/****************************************************************************/

module_init(nettel_mtd_init);
module_exit(nettel_mtd_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David McCullough <davidm@snapgear.com>");
MODULE_DESCRIPTION("SnapGear/SecureEdge FLASH support for uClinux");

/****************************************************************************/
