/*
 * drivers/mtd/maps/dm270-flash.c
 *
 * Flash memory access on TI TMS320DM270 based devices
 *
 * Derived from drivers/mtd/maps/omap-toto-flash.c
 *
 * Copyright (C) 2004 Chee Tim Loh <lohct@pacific.net.sg>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/errno.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/io.h>

#ifndef CONFIG_MACH_DM270
# error This is for DM270 architecture only!
#endif

#define DM270_FLASH_BUSWIDTH	2

static const char *dm270_partition_types[] = {
#ifdef CONFIG_MTD_CMDLINE_PARTS
	"cmdlinepart",
#endif
#ifdef CONFIG_MTD_REDBOOT_PARTS
	"RedBoot",
#endif
	NULL,
};

static struct map_info dm270_map_flash = {
	.name =		"DM270 flash",
	.size =		CONFIG_FLASH_SIZE,
	.phys =		CONFIG_FLASH_MEM_BASE,
	.bankwidth =	DM270_FLASH_BUSWIDTH,
};

/*
 * Here are partition information for all known DM270-based devices.
 * See include/linux/mtd/partitions.h for definition of the mtd_partition
 * structure.
 *
 * The *_max_flash_size is the maximum possible mapped flash size which
 * is not necessarily the actual flash size.  It must be no more than
 * the value specified in the "struct map_desc *_io_desc" mapping
 * definition for the corresponding machine.
 */

/*
 * 1xToshiba TC58FVB160AFT-70 16-MBIT (2Mx8 bits/1Mx16 bits) CMOS FLASH MEMORY
 *   Block erase architecture:
 *   1x16 Kbytes / 2x8 Kbytes / 1x32 Kbytes / 31x64 Kbytes
 */
#ifdef CONFIG_BOARD_XEVMDM270GHK
static struct mtd_partition dm270_partitions[] = {
	{
		.name =		"bootloader",
		.size =		0x20000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"kernel",
		.size =		0xc0000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"rootfs",
		.size =		0x110000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	0,		/* read-write */
	}, {
		.name =		"bootloader params",
		.size =		0x10000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,	/* force read-only */
	} 
};
#elif defined(CONFIG_BOARD_IMPLDM270VP4)
static struct mtd_partition dm270_partitions[] = {
	{
		.name =		"bootloader",
		.size =		0x30000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"kernel",
		.size =		0xa0000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"rootfs",
		.size =		0x1b0000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	0,		/* read-write */
	}, {
		.name =		"data",
		.size =		0x570000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	0,		/* read-write */
	}, {
		.name =		"bootloader params",
		.size =		0x10000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,	/* force read-only */
	} 
};
#else
# error You have not specified your target board!
#endif

static struct mtd_partition *parsed_parts;

static struct mtd_info *dm270_flash_mtd;
 
static int __init
dm270_init_flash (void)   
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	int parsed_nr_parts = 0;
	const char *part_type;
 
	/*
	 * Static partition definition selection
	 */
	part_type = "static";

 	parts = dm270_partitions;
	nb_parts = ARRAY_SIZE(dm270_partitions);
	dm270_map_flash.virt = phys_to_virt(dm270_map_flash.phys);

	simple_map_init(&dm270_map_flash);
	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "DM270 flash: probing %d-bit flash bus\n",
		dm270_map_flash.bankwidth*8);
	dm270_flash_mtd = do_map_probe("cfi_probe", &dm270_map_flash);
	if (!dm270_flash_mtd) {
		return -ENXIO;
	}
	dm270_flash_mtd->owner = THIS_MODULE;
 
	/*
	 * Dynamic partition selection stuff (might override the static ones)
	 */
	if (dm270_partition_types[0]) {
		parsed_nr_parts = parse_mtd_partitions(dm270_flash_mtd,
				dm270_partition_types, &parsed_parts,
				CONFIG_FLASH_MEM_BASE);
	}
 	if (parsed_nr_parts > 0) {
		part_type = "dynamic";
		parts = parsed_parts;
		nb_parts = parsed_nr_parts;
	}

	if (nb_parts == 0) {
		printk(KERN_NOTICE "DM270 flash: no partition info available,"
			"registering whole flash at once\n");
		if (add_mtd_device(dm270_flash_mtd)) {
			return -ENXIO;
		}
	} else {
		printk(KERN_NOTICE "Using %s partition definition\n",
			part_type);
		return add_mtd_partitions(dm270_flash_mtd, parts, nb_parts);
	}
	return 0;
}
 
static int __init
dm270_mtd_init(void)  
{
	int status;

 	if ((status = dm270_init_flash())) {
		printk(KERN_ERR "DM270 Flash: unable to init map for DM270 flash\n");
	}
	return status;
}

static void  __exit
dm270_mtd_cleanup(void)  
{
	if (dm270_flash_mtd) {
		del_mtd_partitions(dm270_flash_mtd);
		map_destroy(dm270_flash_mtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
}

module_init(dm270_mtd_init);
module_exit(dm270_mtd_cleanup);

MODULE_AUTHOR("Chee Tim Loh <lohct@pacific.net.sg>");
MODULE_DESCRIPTION("DM270 CFI map driver");
MODULE_LICENSE("GPL");
