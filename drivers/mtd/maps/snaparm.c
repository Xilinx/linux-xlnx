/****************************************************************************/

/*
 *      snaparm.c -- mappings for SnapGear ARM based boards
 *
 *      (C) Copyright 2000-2005, Greg Ungerer (gerg@snapgear.com)
 *      (C) Copyright 2001-2005, SnapGear (www.snapgear.com)
 *
 *	I expect most SnapGear ARM based boards will have similar
 *	flash arrangements. So this map driver can handle them all.
 */

/****************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/cfi.h>
#include <linux/reboot.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/mach-types.h>

/****************************************************************************/

static struct mtd_info *sg_mtd;
static struct resource *sg_res;

/*
 *	This is the ARM method of setting up the initrd memory region now.
 */
extern unsigned long phys_initrd_start;

/****************************************************************************/

/* First the fixed-configuration platforms */
#if defined(CONFIG_MACH_SE5100)
#define FLASH_ADDR 0x50000000
#define FLASH_SIZE 0x02000000
#define FLASH_WIDTH 2

#define	BOOT_OFFSET	0x00000000
#define	BOOT_SIZE	0x00040000

#define	RECOVER_OFFSET	0x00040000
#define	RECOVER_SIZE	0x00800000

#define	KERNEL_OFFSET	(BOOT_SIZE + RECOVER_SIZE)
#define	KERNEL_SIZE	0x00180000
#define CONFIG_SIZE	0x00020000
#define	NG_CONFIG_SIZE	0x00200000
#define	NG_VAR_SIZE	0x00200000

#define	ROOTFS_SIZE	(FLASH_SIZE - BOOT_SIZE - KERNEL_SIZE - CONFIG_SIZE - \
			 NG_CONFIG_SIZE - NG_VAR_SIZE - RECOVER_SIZE)

static struct mtd_partition sg_partitions[] = {
	/*
	 * if you change the names of these,  check the table below
	 * for unlocking the flash as well
	 */
	{
		name: "SnapGear kernel",
		offset: KERNEL_OFFSET,
		size: KERNEL_SIZE,
	},
	{
		name: "SnapGear filesystem",
		offset: KERNEL_OFFSET + KERNEL_SIZE,
		size: ROOTFS_SIZE,
	},
	{
		name: "SnapGear config",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE,
		size: CONFIG_SIZE
	},
	{
		name: "SnapGear Extra config",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE + CONFIG_SIZE,
		size: NG_CONFIG_SIZE
	},
	{
		name: "SnapGear Extra var",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE + CONFIG_SIZE + NG_CONFIG_SIZE,
		size: NG_VAR_SIZE
	},
	{
		name: "SnapGear image partition",
		offset: KERNEL_OFFSET,
		size: KERNEL_SIZE + ROOTFS_SIZE,
	},
	{
		name: "SnapGear BIOS config",
		offset: BOOT_SIZE / 2,
		size: BOOT_SIZE / 2,
	},
	{
		name: "SnapGear BIOS",
		offset: 0,
		size: BOOT_SIZE,
	},
	{
		name: "SnapGear Recover",
		offset: RECOVER_OFFSET,
		size: RECOVER_SIZE,
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
};

#elif defined(CONFIG_MACH_IPD)

#define FLASH_ADDR 0x00000000
#define FLASH_SIZE 0x01000000
#define FLASH_WIDTH 2

static struct mtd_partition sg_partitions[] = {
	{
		name: "SnapGear Boot Loader",
		offset: 0,
		size: 0x00020000
	},
	{
		name: "SnapGear System Data",
		offset: 0x00020000,
		size: 0x00020000
	},
	{
		name: "SnapGear non-volatile configuration",
		offset: 0x00040000,
		size: 0x00020000
	},
	{
		name: "SnapGear image",
		offset: 0x00060000,
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
};

#elif defined(CONFIG_MACH_CM4008)

#define FLASH_ADDR 0x02000000
#define FLASH_SIZE 0x00800000
#define FLASH_WIDTH 1

#elif defined(CONFIG_MACH_CM41xx)

#define FLASH_ADDR 0x02000000
#define FLASH_SIZE 0x01000000
#define FLASH_WIDTH 1

#else

/* Now the dynamic-configuration platforms (based on machine_arch_type) */
#define DYNAMIC_SGARM_CONFIG

typedef struct {
	unsigned long type;			/* machine_arch_type */
	unsigned long addr;			/* Physical flash address */
	unsigned long size;			/* Maximum flash size */
	unsigned long configsize;	/* Size of the config partition */
	unsigned width;				/* Flash bus width */
} flash_layout_t;

static const flash_layout_t flash_layout[] = {
#if defined(CONFIG_MACH_SE4000)
	{ .type = MACH_TYPE_SE4000, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x20000 },
#endif
#if defined(CONFIG_MACH_IVPN)
	{ .type = MACH_TYPE_IVPN, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x20000 },
#endif
#if defined(CONFIG_MACH_SG560) || defined (CONFIG_MACH_SGARMAUTO)
	{ .type = MACH_TYPE_SG560, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x80000 },
#endif
#if defined(CONFIG_MACH_SG580) || defined (CONFIG_MACH_SGARMAUTO)
	{ .type = MACH_TYPE_SG580, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x100000 },
#endif
#if defined(CONFIG_MACH_SG590) || defined (CONFIG_MACH_SGARMAUTO)
	{ .type = MACH_TYPE_SG590, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x100000 },
#endif
#if defined(CONFIG_MACH_SG640) || defined (CONFIG_MACH_SGARMAUTO)
	{ .type = MACH_TYPE_SG640, .addr = 0x50000000, .size = 0x01000000, .width = 2, .configsize = 0x100000 },
#endif
#if defined(CONFIG_MACH_SG565) || defined (CONFIG_MACH_SGARMAUTO)
	{ .type = MACH_TYPE_SG565, .addr = 0x50000000, .size = 0x01000000, .width = 1, .configsize = 0x100000 },
#endif
#if defined(CONFIG_MACH_SG720)
	{ .type = MACH_TYPE_SG720, .addr = 0x50000000, .size = 0x01000000, .width = 1, .configsize = 0 },
#endif
#if defined(CONFIG_MACH_SG8100)
	{ .type = MACH_TYPE_SG8100, .addr = 0x50000000, .size = 0x02000000, .width = 2, .configsize = 0x100000 },
#endif
#if defined(CONFIG_MACH_SHIVA1100)
	{ .type = MACH_TYPE_SHIVA1100, .addr = 0x50000000, .size = 0x01000000, .width = 1, .configsize = 0x20000 },
#endif
#if defined(CONFIG_MACH_LITE300)
	{ .type = MACH_TYPE_LITE300, .addr = 0x02000000, .size = 0x00800000, .width = 1, .configsize = 0x20000 },
#endif
#if defined(CONFIG_MACH_SE4200)
	{ .type = MACH_TYPE_SE4200, .addr = 0x02000000, .size = 0x00800000, .width = 1, .configsize = 0x20000 },
#endif
#if defined(CONFIG_MACH_EP9312)
	{ .type = MACH_TYPE_EP9312, .addr = 0x60000000, .size = 0x00800000, .width = 2, .configsize = 0x20000 },
#endif
};

#endif

/****************************************************************************/

//#define SNAPARM_DEBUG 1
#ifdef SNAPARM_DEBUG
#define DPRINTK printk
#else
#define DPRINTK(...)
#endif

/****************************************************************************/

/*
 *	Define some access helper macros. On different architectures
 *	we have to deal with multi-byte quanitites, read/write buffers,
 *	and other architectural details a little differently. These
 *	macros try to abstract that as much as possible to keep the
 *	code clean.
 */

#ifdef CONFIG_ARCH_KS8695
#include <asm/cacheflush.h>
/*
 *	The bus read and write buffers can potenitially coalesce read and
 *	write bus cycles to the same address, thus dropping real cycles
 *	when talking to IO type devices. We need to flush those	buffers 
 *	when doing flash reading/writing.
 *
 *	Walk through a small section of memory avoiding the cache so that we
 *	can keep the flash running smoothly. Using the write buffer enable
 *	disable seems tocause nasty bus junk, sodon't use them.
 */
static void invalidate_buffer(void)
{
	static unsigned char buf[32];
	unsigned char cpy[sizeof(buf)];

	memset(buf, 0, sizeof(buf));
	clean_dcache_area(buf, sizeof(buf));
	memcpy(cpy, buf, sizeof(buf));
	clean_dcache_area(buf, sizeof(buf));
}
#define	readpreflush(x)		invalidate_buffer()
#define	writepreflush(x)	invalidate_buffer()

#define	CONFIG_LOCK_MULTIBYTE
static DEFINE_SPINLOCK(multibyte_lock);
#define	initlock()		unsigned long flags;
#define	getlock()		spin_lock_irqsave(&multibyte_lock, flags)
#define	releaselock()		spin_unlock_irqrestore(&multibyte_lock, flags)
#endif	/* CONFIG_ARCH_KS8695 */


/*
 *	We are not entirely sure why, but on the iVPN the timing _between_
 *	access to the flash causes problems with other bus activity on the
 *	expansion bus. Namely the CompactFlash WiFi card. Delaying 1us
 *	is enough to clean up the cycles.
 */
#ifdef CONFIG_MACH_IVPN
#define	readpreflush(x)		udelay(1)
#define	readpostflush(x)	udelay(1)
#define	writepreflush(x)	udelay(1)
#define	writepostflush(x)	udelay(1)

#define	CONFIG_LOCK_MULTIBYTE
static DEFINE_SPINLOCK(multibyte_lock);
#define	initlock()		unsigned long flags;
#define	getlock()		spin_lock_irqsave(&multibyte_lock, flags)
#define	releaselock()		spin_unlock_irqrestore(&multibyte_lock, flags)
#endif	/* CONFIG_MACH_IVPN */


/*
 *	Now default any macros that are not used.
 */
#ifndef readpreflush
#define	readpreflush(x)
#endif
#ifndef readpostflush
#define	readpostflush(x)
#endif
#ifndef writepreflush
#define	writepreflush(x)
#endif
#ifndef writepostflush
#define	writepostflush(x)
#endif
#ifndef initlock
#define	initlock()
#endif
#ifndef getlock
#define	getlock()
#endif
#ifndef releaselock
#define	releaselock()
#endif

/****************************************************************************/

static map_word sg_read(struct map_info *map, unsigned long ofs)
{
	map_word res;
	readpreflush(map->virt + ofs);
	if (map_bankwidth(map) == 1)
		res.x[0] = __raw_readb(map->virt + ofs);
	else
		res.x[0] = __raw_readw(map->virt + ofs);
	readpostflush(map->virt + ofs);
	DPRINTK("%s(0x%x) = 0x%x\n", __FUNCTION__, (u32)ofs, (u32)res.x[0]);
	return res;
}

static void sg_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	void __iomem *p8;
	union {
		__u32 l;
		__u8  c[4];
	} data;
	__u8 *dp;
	initlock();

	DPRINTK("%s(to=0x%x, from=0x%x, len=%d)\n", __FUNCTION__,
		(__u32)to, (__u32)from, (__u32)len);

	if (len <= 0)
		return;

	getlock();

	p8 = (map->virt + from);
	dp = (__u8 *) to;

	/*
	 * read until the pointer to flash is on a 32 bit boundary
	 */
	while (len > 0 && (((unsigned long) p8) & 3)) {
		readpreflush(p8);
		*dp++ = __raw_readb(p8);
		readpostflush(p8);
		p8++;
		len--;
	}
	/*
	 * The Xscale will do a back-to-back cycle on flash if we read
	 * 2 16bit values as a single 32 bit quantity,  this is much faster
	 * than two normal 16bit cycles
	 */
	while (len & ~3) {
		readpreflush(p8);
		data.l = __raw_readl(p8);
		*dp++ = data.c[0];
		*dp++ = data.c[1];
		*dp++ = data.c[2];
		*dp++ = data.c[3];
		readpostflush(p8);
		p8  += sizeof(__u32);
		len -= sizeof(__u32);
	}
	/*
	 * clean up and non-aligned reads at the end
	 */
	while (len > 0) {
		readpreflush(p8);
		*dp++ = __raw_readb(p8);
		readpostflush(p8);
		p8++;
		len--;
	}

	releaselock();
}

static void sg_write(struct map_info *map, map_word d, unsigned long adr)
{
	DPRINTK("%s(0x%x,0x%x)\n", __FUNCTION__, (u32)d.x[0], (u32)adr);
	writepreflush(map->virt + adr);
	if (map_bankwidth(map) == 1)
		__raw_writeb(d.x[0], map->virt + adr);
	else
		__raw_writew(d.x[0], map->virt + adr);
	writepostflush(map->virt + adr);
}

static void sg_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	unsigned int i;
	initlock();

	DPRINTK("%s(to=0x%x,from=0x%x,len=%d)\n", __FUNCTION__,
		(u32)to, (u32)from, len);

	getlock();
	if (map_bankwidth(map) == 1) {
		u8 *src8;
		for (src8 = (u8 *) from, i = 0; (i < len); i++) {
			writepreflush(map->virt + to + i);
			__raw_writeb(*src8++, map->virt + to + i);
			writepostflush(map->virt + to + i);
		}
	} else {
		u16 *src16;
		for (src16 = (u16 *) from, i = 0; (i < len); i += 2) {
			writepreflush(map->virt + to + i);
			__raw_writew(*src16++, map->virt + to + i);
			writepostflush(map->virt + to + i);
		}
	}
	releaselock();
}

/****************************************************************************/
/*                           OPENGEAR FLASH                                 */
/****************************************************************************/
#if defined(CONFIG_MACH_CM4008) || defined(CONFIG_MACH_CM41xx)

#define	VENDOR	"OpenGear"

/*
 *	Intel FLASH setup. This is the only flash device, it is the entire
 *	non-volatile storage (no IDE CF or hard drive or anything else).
 */
static struct map_info sg_map = {
	.name =		 "OpenGear Intel/StrataFlash",
	.size =		 FLASH_SIZE,
	.bankwidth =	 FLASH_WIDTH,
	.read =		 sg_read,
	.copy_from =	 sg_copy_from,
	.write =	 sg_write,
	.copy_to =	 sg_copy_to
};

static struct mtd_partition sg_partitions[] = {
	{
		.name	= "U-Boot Loader",
		.offset	= 0,
		.size	= 0x00020000
	},
	{
		.name	= "OpenGear non-volatile configuration",
		.offset	= 0x00020000,
		.size	= 0x001e0000
	},
	{
		.name	= "OpenGear image",
		.offset	= 0x200000,
	},
	{
		.name	= "OpenGear Intel/StrataFlash",
		.offset = 0
	},
};

#else
/****************************************************************************/
/*                           SNAPGEAR FLASH                                 */
/****************************************************************************/

#define	VENDOR	"SnapGear"

#if defined(CONFIG_MACH_SE5100)
#define	VENDOR_ROOTFS	"SnapGear filesystem"
#else
#define	VENDOR_ROOTFS	"SnapGear image"
#endif

/*
 *	Intel FLASH setup. This is the only flash device, it is the entire
 *	non-volatile storage (no IDE CF or hard drive or anything else).
 */
static struct map_info sg_map = {
	name: "SnapGear Intel/StrataFlash",
#ifndef DYNAMIC_SGARM_CONFIG
	size: FLASH_SIZE,
	bankwidth: FLASH_WIDTH,
#endif
	read: sg_read,
	copy_from: sg_copy_from,
	write: sg_write,
	copy_to: sg_copy_to
};

#ifdef DYNAMIC_SGARM_CONFIG
static unsigned long flash_addr;
#define FLASH_ADDR flash_addr
#endif

/* Define the flash layout */
#if defined(CONFIG_MACH_SE5100)
static struct mtd_partition sg_partitions[] = {
	/*
	 * if you change the names of these,  check the table below
	 * for unlocking the flash as well
	 */
	{
		name: "SnapGear kernel",
		offset: KERNEL_OFFSET,
		size: KERNEL_SIZE,
	},
	{
		name: "SnapGear filesystem",
		offset: KERNEL_OFFSET + KERNEL_SIZE,
		size: ROOTFS_SIZE,
	},
	{
		name: "SnapGear config",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE,
		size: CONFIG_SIZE
	},
	{
		name: "SnapGear Extra config",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE + CONFIG_SIZE,
		size: NG_CONFIG_SIZE
	},
	{
		name: "SnapGear Extra var",
		offset: KERNEL_OFFSET + KERNEL_SIZE + ROOTFS_SIZE + CONFIG_SIZE + NG_CONFIG_SIZE,
		size: NG_VAR_SIZE
	},
	{
		name: "SnapGear image partition",
		offset: KERNEL_OFFSET,
		size: KERNEL_SIZE + ROOTFS_SIZE,
	},
	{
		name: "SnapGear BIOS config",
		offset: BOOT_SIZE / 2,
		size: BOOT_SIZE / 2,
	},
	{
		name: "SnapGear BIOS",
		offset: 0,
		size: BOOT_SIZE,
	},
	{
		name: "SnapGear Recover",
		offset: RECOVER_OFFSET,
		size: RECOVER_SIZE,
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
};
#elif defined(CONFIG_MACH_IPD)
static struct mtd_partition sg_partitions[] = {
	{
		name: "SnapGear Boot Loader",
		offset: 0,
		size: 0x00020000
	},
	{
		name: "SnapGear System Data",
		offset: 0x00020000,
		size: 0x00020000
	},
	{
		name: "SnapGear non-volatile configuration",
		offset: 0x00040000,
		size: 0x00020000
	},
	{
		name: "SnapGear image",
		offset: 0x00060000,
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
};
#elif defined(CONFIG_MACH_SG720)
static struct mtd_partition sg_partitions[] = {
	{
		name: "SnapGear Boot Loader",
		offset: 0,
		size: 0x00080000
	},
	{
		name: "SnapGear Tags",
		offset: 0x00080000,
		size: 0x00080000
	},
	{
		name: "SnapGear Log",
		offset: 0x00100000,
		size: 0x00100000
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
	{
		name: "SnapGear Unused",
		offset: 0x00200000,
	},
};
#else
/* We use a dynamic structure */
static struct mtd_partition sg_partitions[] = {
	{
		name: "SnapGear Boot Loader",
		offset: 0,
		size: 0x00020000
	},
	{
		name: "SnapGear non-volatile configuration",
		offset: 0x00020000,
		/*size: CONFIG_SIZE -- filled in when we know the config size */
	},
	{
		name: "SnapGear image",
		offset: 0x00020000, /* +CONFIG_SIZE, -- filled in when we know the config size */
	},
	{
		name: "SnapGear Intel/StrataFlash",
		offset: 0
	},
};
#endif

/****************************************************************************/
#endif
/****************************************************************************/

#define NUM_PARTITIONS	(sizeof(sg_partitions)/sizeof(sg_partitions[0]))

/****************************************************************************/

/*
 *	Set the Intel flash back to read mode. Sometimes MTD leaves the
 *	flash in status mode, and if you reboot there is no code to
 *	execute (the flash devices do not get a RESET) :-(
 */
static int sg_reboot_notifier(struct notifier_block *nb, unsigned long val, void *v)
{
	struct cfi_private *cfi = sg_map.fldrv_priv;
	int i;

	/* Make sure all FLASH chips are put back into read mode */
	for (i = 0; cfi && i < cfi->numchips; i++) {
		cfi_send_gen_cmd(0xff, 0x55, cfi->chips[i].start, &sg_map,
			cfi, cfi->device_type, NULL);
	}
	return NOTIFY_OK;
}

static struct notifier_block sg_notifier_block = {
	sg_reboot_notifier, NULL, 0
};

/****************************************************************************/

/*
 *	Find the MTD device with the given name.
 */

static int sg_getmtdindex(char *name)
{
	struct mtd_info *mtd;
	int i, index;

	index = -1;
	for (i = 0; (i < MAX_MTD_DEVICES); i++) {
		mtd = get_mtd_device(NULL, i);
		if (mtd) {
			if (strcmp(mtd->name, name) == 0)
				index = mtd->index;
			put_mtd_device(mtd);
			if (index >= 0)
				break;
		}
	}
	return index;
}

/****************************************************************************/

int __init sg_init(void)
{
	int index, rc;

	printk(VENDOR ": MTD flash setup\n");


#ifdef DYNAMIC_SGARM_CONFIG
	/* Find the matching entry in the flash_layout table.
	 * Note that for almost *all* devices, there will be only 1
	 */
	for (index = 0; index < sizeof(flash_layout) / sizeof(*flash_layout); index++) {
		if (flash_layout[index].type == machine_arch_type) {
			break;
		}
	}
	if (index == sizeof(flash_layout) / sizeof(*flash_layout)) {
		printk(KERN_WARNING VENDOR ": No matching flash layout for mach type %d, using mach type %lu\n",
			machine_arch_type, flash_layout[0].type);
		index = 0;
	}

	/* Fix up the entries in sg_map */
	sg_map.size = flash_layout[index].size;
	sg_map.bankwidth = flash_layout[index].width;
	flash_addr = flash_layout[index].addr;

	/* And also fix up the partition table if we have a config partition */
	if (flash_layout[index].configsize) {
		sg_partitions[1].size += flash_layout[index].configsize;
		sg_partitions[2].offset += flash_layout[index].configsize;
	}
#endif

#if defined(CONFIG_ARCH_IXP4XX)
{
	u32 val;
	/*
	 * enable fast CS0 (Intel flash J3 and P30 compatible values)
	 * T1=0, T2=2, T3=1, T4=0, T5=0
	 * NOTE: a value of "0" implies 1 cycle
	 * we preserve all the bootloader set values for size etc of the CS
	 * and only change T1-5
	 */
	val = *IXP4XX_EXP_CS0;
	val = (val & 0xffff) | 0x80c00000;
	/* Enable flash writes */
	val |= IXP4XX_FLASH_WRITABLE;
	*IXP4XX_EXP_CS0 = val;
}
#endif

	sg_res = request_mem_region(FLASH_ADDR, sg_map.size, VENDOR " FLASH");
	if (sg_res == NULL) {
		printk(VENDOR ": failed memory resource request?\n");
		return -EIO;
	}

	/*
	 *	Map flash into our virtual address space.
	 */
	sg_map.virt = ioremap(FLASH_ADDR, sg_map.size);
	if (!sg_map.virt) {
		release_mem_region(FLASH_ADDR, sg_map.size);
		sg_res = NULL;
		printk(VENDOR ": failed to ioremap() flash\n");
		return -EIO;
	}

	if ((sg_mtd = do_map_probe("cfi_probe", &sg_map)) == NULL) {
		iounmap(sg_map.virt);
		release_mem_region(FLASH_ADDR, sg_map.size);
		sg_res = NULL;
		sg_map.virt = NULL;
		printk(VENDOR ": probe failed\n");
		return -ENXIO;
	}

	printk(KERN_NOTICE VENDOR ": %s device size = %dK\n",
		sg_mtd->name, sg_mtd->size>>10);

	sg_mtd->owner = THIS_MODULE;
	sg_mtd->priv = &sg_map;
	register_reboot_notifier(&sg_notifier_block);
	rc = add_mtd_partitions(sg_mtd, sg_partitions, NUM_PARTITIONS);
	if (rc < 0)
		printk(KERN_NOTICE VENDOR ": add_mtd_partitions() failed?\n");

#ifdef CONFIG_BLK_DEV_INITRD
	if (phys_initrd_start == 0)
#endif
	{
		/* Mark mtd partition as root device */
		index = sg_getmtdindex(VENDOR " image");
		if (index >= 0)
			ROOT_DEV = MKDEV(MTD_BLOCK_MAJOR, index);
	}

	return rc;
}

/****************************************************************************/

void __exit sg_cleanup(void)
{
	unregister_reboot_notifier(&sg_notifier_block);
	if (sg_mtd) {
		del_mtd_partitions(sg_mtd);
		map_destroy(sg_mtd);
	}
	if (sg_map.virt) {
		iounmap(sg_map.virt);
		sg_map.virt = NULL;
	}
	if (sg_res) {
		release_mem_region(FLASH_ADDR, sg_map.size);
		sg_res = NULL;
	}
}

/****************************************************************************/

module_init(sg_init);
module_exit(sg_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_DESCRIPTION("SnapGear/ARM flash support");

/****************************************************************************/
