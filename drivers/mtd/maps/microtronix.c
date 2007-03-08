/*
 * Normal mappings of Microtronix ukit flash in physical memory
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
#define BUSWIDTH 2

static struct mtd_info *mymtd;


struct map_info microtronix_map = {
	.name = "Microtronix map",
	.size = WINDOW_SIZE,
	.bankwidth = BUSWIDTH,
	.phys = WINDOW_ADDR,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition *mtd_parts;
static int                   mtd_parts_nb;

static struct mtd_partition microtronix_partitions[] = {
	{
		.name =		"romfs",
		.size =		0x600000,
		.offset =	0x200000,
	},{
		.name =		"loader/kernel",
		.size =		0x200000,
		.offset =	0,
	}
};

#define NUM_PARTITIONS	(sizeof(microtronix_partitions)/sizeof(struct mtd_partition))
const char *part_probes[] = {"cmdlinepart", "RedBoot", NULL};

#endif /* CONFIG_MTD_PARTITIONS */

int __init init_microtronix_map(void)
{
	static const char *flash_probe_types[] = {"cfi_probe", "jedec_probe", 0 };
	const char **type;

 	microtronix_map.virt = (unsigned long *)ioremap_nocache(WINDOW_ADDR, WINDOW_SIZE);
/*
	if (!microtronix_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
*/
	simple_map_init(&microtronix_map);

	mymtd = 0;
	type = flash_probe_types;
	for(; !mymtd && *type; type++) {
		mymtd = do_map_probe(*type, &microtronix_map);
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
			       "Using Microtronix development partition definition\n");
			add_mtd_partitions (mymtd, microtronix_partitions, NUM_PARTITIONS);
			return 0;
		}

#endif
		add_mtd_device(mymtd);

		return 0;
	}

	iounmap((void *)microtronix_map.virt);
	return -ENXIO;
}

static void __exit cleanup_microtronix_map(void)
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

	iounmap((void *)microtronix_map.virt);
	microtronix_map.virt = 0;
}

module_init(init_microtronix_map);
module_exit(cleanup_microtronix_map);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microtronix Datacom");
MODULE_DESCRIPTION("MTD map driver for Microtronix ukit");
