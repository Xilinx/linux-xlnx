/*
 *  linux/arch/arm/mach-s5c7375/arch.c
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS Co.,Ltd.
 *			      Hyok S. Choi (hyok.choi@samsung.com)
 *
 *  Architecture specific fixups.  This is where any
 *  parameters in the params struct are fixed up, or
 *  any additional architecture specific information
 *  is pulled from the params struct.
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <linux/tty.h>
#include <asm/elf.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>


extern void __init s3c3410_init_irq(void);
extern void s3c3410_time_init(void);
                                                                                                                                           
static void __init
fixup_s3c3410(struct machine_desc *desc, struct param_struct *params,
        char **cmdline, struct meminfo *mi)
{
}

MACHINE_START(S3C3410, "S3C3410, SAMSUNG ELECTRONICS Co., Ltd.")
	MAINTAINER("Hyok S. Choi <hyok.choi@samsung.com>")
	FIXUP(fixup_s3c3410)
	INITIRQ(s3c3410_init_irq)
	INITTIME(s3c3410_time_init)
MACHINE_END
