/*
 * drivers/ide/arm/adi-coyote.c
 *
 * IDE hooks for ADI Engineering Coyote platform
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (c) MontaVista, Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/mach-types.h>

static u16 coyote_inw(unsigned long p)
{
	return *((volatile u16 *)(COYOTE_IDE_BASE_VIRT + p));
}

static u8 coyote_inb(unsigned long p)
{
	return (u8) coyote_inw(p);
}

static void coyote_insw(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*(u16 *)addr = *(volatile u16 *)(COYOTE_IDE_BASE_VIRT + port);
		addr += 2;
	}
}
static void coyote_outw(u16 v, unsigned long p)
{
	*((volatile u16 *)(COYOTE_IDE_BASE_VIRT + p)) = v;
}

static void coyote_outb(u8 v, unsigned long p)
{
	coyote_outw(v, p);
}

static void coyote_outbsync(ide_drive_t *drive, u8 v, unsigned long p)
{
	coyote_outw(v, p);
}

static void coyote_outsw(unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*(volatile u16 *)(COYOTE_IDE_BASE_VIRT + (port)) = *(u16 *)addr;
		addr += 2;
	}
}

static int __init coyote_ide_init(void)
{
	int i;
	struct hwif_s *hwifp;
	hw_regs_t coyote_ide;
	ide_ioreg_t reg = (ide_ioreg_t) COYOTE_IDE_DATA_PORT;

	if(!machine_is_adi_coyote())
		return -EIO;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		coyote_ide.io_ports[i] = reg;
		reg += 2;
	}
	coyote_ide.irq	= IRQ_COYOTE_IDE;
	coyote_ide.io_ports[IDE_CONTROL_OFFSET] = COYOTE_IDE_CTRL_PORT;
	coyote_ide.chipset = ide_generic;

	printk("Registering IDE HW: %d\n", 
		ide_register_hw(&coyote_ide, &hwifp));

	/*
	 * Override the generic functions with our own implementation
	 */
	hwifp->OUTB	= coyote_outb;
	hwifp->OUTBSYNC	= coyote_outbsync;
	hwifp->OUTW	= coyote_outw;
	hwifp->OUTSW	= coyote_outsw;
	hwifp->INB	= coyote_inb;
	hwifp->INW	= coyote_inw;
	hwifp->INSW	= coyote_insw;

	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("ADI Coyote IDE driver");

module_init(coyote_ide_init);

