/*
 * arch/arm/kernel/initrd-mtd.c
 *
 * MTD RAM platform device for the initrd
 *
 * Copyright (C) 2006 Philip Craig <philipc@snapgear.com>
 *
 */

#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/map.h>
#include <linux/mtd/plat-ram.h>

extern unsigned long phys_initrd_start;
extern unsigned long phys_initrd_size;

static struct resource initrd_mtd_ram_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platdata_mtd_ram initrd_mtd_ram_data = {
	.mapname	= "Romfs",
	.root_dev	= 1,
};

static struct platform_device initrd_mtd_ram_device = {
	.name                   = "mtd-ram",
	.id                     = 0,
	.dev.platform_data      = &initrd_mtd_ram_data,
	.num_resources          = 1,
	.resource               = &initrd_mtd_ram_resource,
};

static int __init initrd_device_setup(void)
{
	int ret = 0;

	if (phys_initrd_start) {
		if (map_bankwidth_supported(4))
			initrd_mtd_ram_data.bankwidth = 4;
		else if (map_bankwidth_supported(2))
			initrd_mtd_ram_data.bankwidth = 2;
		else
			initrd_mtd_ram_data.bankwidth = 1;
		initrd_mtd_ram_resource.start = phys_initrd_start;
		initrd_mtd_ram_resource.end = phys_initrd_start
			+ phys_initrd_size - 1;
		ret = platform_device_register(&initrd_mtd_ram_device);
	}

	return ret;
}

__initcall(initrd_device_setup);
