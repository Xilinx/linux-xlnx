/*
 * Normal mappings of altera Nios II development kit flash in physical memory
 * Derived from physmap.c, by Microtronix Datacom Ltd.
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
#include <asm/nios.h>

/* map solutions */
#define WINDOW_ADDR na_flash_kernel
#define WINDOW_SIZE na_flash_kernel_size
#define BUSWIDTH 1

static struct mtd_info *mymtd;


struct map_info ndk_amd_map = {
	.name = "Altera NDK flash (AMD)",
	.size = WINDOW_SIZE,
	.bankwidth = BUSWIDTH,
	.phys = WINDOW_ADDR,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition *mtd_parts;
static int                   mtd_parts_nb;

static struct mtd_partition alteramap_partitions[] = {
#ifdef CONFIG_ALTERA_STRATIX_II
	{
		.name =		"romfs/jffs2",
		.size =		0x600000,
		.offset =	0x200000,
	},{
		.name =		"loader/kernel",
		.size =		0x200000,
		.offset =	0,
	}, {
		.name =		"User configuration",
		.size =		0x400000,
		.offset =	0x800000,
	}, {
		.name =		"safe configuration",
		.size =		0x400000,
		.offset =	0xc00000,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}
#elif defined(CONFIG_ALTERA_STRATIX_PRO)
	{
		.name =		"romfs/jffs2",
		.size =		0x200000,
		.offset =	0x200000,
	},{
		.name =		"loader/kernel",
		.size =		0x200000,
		.offset =	0,
	}, {
		.name =		"User configuration",
		.size =		0x200000,
		.offset =	0x400000,
	}, {
		.name =		"safe configuration",
		.size =		0x200000,
		.offset =	0x600000,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}
#else
	{
		.name =		"romfs/jffs2",
		.size =		0x400000,
		.offset =	0x200000,
	},{
		.name =		"loader/kernel",
		.size =		0x200000,
		.offset =	0,
	}, {
		.name =		"User configuration",
		.size =		0x100000,
		.offset =	0x600000,
	}, {
		.name =		"safe configuration",
		.size =		0x100000,
		.offset =	0x700000,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}
#endif
};

#define NUM_PARTITIONS	(sizeof(alteramap_partitions)/sizeof(struct mtd_partition))
const char *part_probes[] = {"cmdlinepart", "RedBoot", NULL};

#endif /* CONFIG_MTD_PARTITIONS */

int __init init_alteramap(void)
{
	static const char *rom_probe_types[] = {"cfi_probe", "jedec_probe", 0 };
	const char **type;

 	ndk_amd_map.virt = (unsigned long *)ioremap_nocache(WINDOW_ADDR, WINDOW_SIZE);
/*
	if (!ndk_amd_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
*/
	simple_map_init(&ndk_amd_map);

	mymtd = 0;
	type = rom_probe_types;
	for(; !mymtd && *type; type++) {
		mymtd = do_map_probe(*type, &ndk_amd_map);
	}
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

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
			printk(KERN_NOTICE 
			       "Using Altera NDK partition definition\n");
			add_mtd_partitions (mymtd, alteramap_partitions, NUM_PARTITIONS);
			return 0;
		}

#endif
		add_mtd_device(mymtd);

		return 0;
	}

	iounmap((void *)ndk_amd_map.virt);
	return -ENXIO;
}

static void __exit cleanup_alteramap(void)
{
#ifdef CONFIG_MTD_PARTITIONS
	if (mtd_parts_nb) {
		del_mtd_partitions(mymtd);
		kfree(mtd_parts);
	} else if (NUM_PARTITIONS) {
		del_mtd_partitions(mymtd);
	} else {
		del_mtd_device(mymtd);
	}
#else
	del_mtd_device(mymtd);
#endif
	map_destroy(mymtd);

	iounmap((void *)ndk_amd_map.virt);
	ndk_amd_map.virt = 0;
}

module_init(init_alteramap);
module_exit(cleanup_alteramap);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microtronix Datacom <www.microtronix.com>");
MODULE_DESCRIPTION("MTD map driver for Altera Nios Development Kit");
