/*
 *  /arch/arm/mach-s3c24a0/generic.c
 *
 *      $Id: generic.c,v 1.3 2006/12/12 13:38:48 gerg Exp $
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/serial_core.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/*
 * Common I/O mapping:
 *
 * 0x4000.0000 ~ 0x4Aff.fffff
  (0xe000.0000 ~ 0xeAff.fffff)  GPIO
 */
static struct map_desc standard_io_desc[] __initdata = {
        /* virtual    physical    length */
        { 0xe0000000, 0x40000000, 0x10000000, MT_DEVICE},
};

void __init elfin_map_io(void)
{
        iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}
