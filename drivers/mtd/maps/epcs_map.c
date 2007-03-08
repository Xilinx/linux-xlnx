/*
 *
 * Mappings into EPCS Configuration Flash device by Altera
 *
 * Copyright (C) 2006 FPS-Tech (http://www.fps-tech.net)
 * Author: Jai Dhar, contact@fps-tech.net (jdhar)
 *
 * Adapted from physmap.c
 *
 * - Driver to map partitions into EPCS Configuration device
 * - Map Size should get set by epcs_probe when it detects a chip
 * - IOREMAP is done for maximum size possible (64Mbit)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include "../chips/epcs.h"

#define BUSWIDTH 1

static struct mtd_info *mymtd;

struct map_info alt_epcs_map = {
	.name = "Altera EPCS Flash",
	.phys = na_epcs_controller,
	.size = 0,
	.bankwidth = BUSWIDTH,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition *mtd_parts;
static int                   mtd_parts_nb;

static int num_physmap_partitions;
static struct mtd_partition epcs_partitions[] =
{
	{
		.name = "small_part",
		.size = 0x200000,
		.offset = 0x400000,
	},
	{
		.name = "big_part",
		.size = 0x200000,
		.offset = 0x600000,
	}
};

static const char *part_probes[] __initdata = {"cmdlinepart", "RedBoot", NULL};

#define NUM_PARTITIONS	(sizeof(epcs_partitions)/sizeof(struct mtd_partition))

#endif /* CONFIG_MTD_PARTITIONS */

static int __init init_epcsmap(void)
{
	static const char *rom_probe_types[] = { "epcs", NULL };
	const char **type;

    /* Not sure about this, but since IOREMAP looks like it needs to happen before the chip is probed,
	 * the maximum space (64Mbit) must be allocated */
	alt_epcs_map.virt = (unsigned long *)ioremap_nocache(na_epcs_controller, na_epcs_controller_size);

	/*
	if (!physmap_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}*/

	simple_map_init(&alt_epcs_map);

	mymtd = NULL;
	type = rom_probe_types;
	for(; !mymtd && *type; type++) {

		#if EPCS_DEBUG1
		printk(KERN_NOTICE "Probing for %s\n",*type);
		#endif

		mymtd = do_map_probe(*type, &alt_epcs_map);
	}
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

		#if EPCS_DEBUG1
		printk(KERN_NOTICE "alt_epcs flash device: %d Kbytes at 0x%X\n", (u_int) alt_epcs_map.size/1024, (u_int) alt_epcs_map.phys);
		#endif

#ifdef CONFIG_MTD_PARTITIONS
		mtd_parts_nb = parse_mtd_partitions(mymtd, part_probes, 
						    &mtd_parts, 0);

		if (mtd_parts_nb > 0)
		{
			add_mtd_partitions (mymtd, mtd_parts, mtd_parts_nb);
			return 0;
		}

		if (NUM_PARTITIONS != 0) 
		{
			#if EPCS_DEBUG1
			printk(KERN_NOTICE "Using Altera EPCS partition definition\n");
			#endif

			add_mtd_partitions (mymtd, epcs_partitions, NUM_PARTITIONS);
			return 0;
		}

#endif
		add_mtd_device(mymtd);

		return 0;
	}
	else
	{
		printk(KERN_NOTICE "No Partitions found on EPCS Device\n");
	}

	iounmap(alt_epcs_map.virt);
	return -ENXIO;
}

static void __exit cleanup_epcsmap(void)
{
#ifdef CONFIG_MTD_PARTITIONS
	if (mtd_parts_nb) {
		del_mtd_partitions(mymtd);
		kfree(mtd_parts);
	} else if (num_physmap_partitions) {
		del_mtd_partitions(mymtd);
	} else {
		del_mtd_device(mymtd);
	}
#else
	del_mtd_device(mymtd);
#endif
	map_destroy(mymtd);

	iounmap(alt_epcs_map.virt);
	alt_epcs_map.virt = NULL;
}

module_init(init_epcsmap);
module_exit(cleanup_epcsmap);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jai Dhar <contact@fps-tech.net>");
MODULE_DESCRIPTION("Altera EPCS Map Device");
