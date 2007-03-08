/*
 *  linux/arch/arm/mach-espd_4510b/arch.c
 *
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
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

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

extern void __init s3c4510b_init_irq(void);
extern struct sys_timer s3c4510b_timer;

void __init s3c4510b_init_machine(void) {
	/* enable LED 0 */
	outl( 0xFE, REG_IOPDATA);
}

MACHINE_START(ESPD_4510B, "ESPD 4510B(S3C4510B)")
	/* Maintainer: Curt Brune <curt@cucy.com> */
	.init_irq	= s3c4510b_init_irq,
	.init_machine = s3c4510b_init_machine,
	.boot_params	= 0x00000800,
	.timer = &s3c4510b_timer,
MACHINE_END
