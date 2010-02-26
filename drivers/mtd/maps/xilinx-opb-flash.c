/*
 * drivers/mtd/maps/xilinx-opb-flash.c
 *
 * MTD mapping driver for the OPB Flash device on Xilinx boards.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Copyright 2007 Xilinx, Inc.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/xparameters.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

static struct map_info map_bank = {
	.name = "OPB Flash on Xilinx board",
	.size = XPAR_FLASH_HIGHADDR - XPAR_FLASH_BASEADDR + 1,
	.bankwidth = XPAR_FLASH_BUSWIDTH,
	.phys = XPAR_FLASH_BASEADDR,
};

static struct mtd_info *mtd_bank;

static int __init init_opb_mtd(void) {

	map_bank.virt = ioremap(map_bank.phys, map_bank.size);
	if (!map_bank.virt) {
		printk("OPB Flash: failed to ioremap\n");
		return -EIO;
	}

	simple_map_init(&map_bank);
		
	mtd_bank = do_map_probe("cfi_probe", &map_bank);
	if (!mtd_bank) {
		printk("OPB Flash: failed to find a mapping\n");
		iounmap(map_bank.virt);
		map_bank.virt = 0;
		return -ENXIO;
	}

	mtd_bank->owner = THIS_MODULE;

	printk("Registering a %ldMB OPB Flash at 0x%lX\n",
	       map_bank.size >> 20, map_bank.phys);

	add_mtd_device(mtd_bank);

	return 0;
}

static void __exit cleanup_opb_mtd(void) {
	if (mtd_bank) {
		del_mtd_device(mtd_bank);
		map_destroy(mtd_bank);
	}
	if (map_bank.virt) {
		iounmap((void *)map_bank.virt);
		map_bank.virt = 0;
	}
}

module_init(init_opb_mtd);
module_exit(cleanup_opb_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("MTD map driver for OPB Flash on Xilinx boards");
