/*
 * Secondary CPU startup routine source file.
 *
 * Copyright (C) 2010 Xilinx, Inc.
 *
 * This file is based on arm omap smp platform file and arm
 * realview smp platform.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 * Copyright (c) 2003 ARM Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <mach/hardware.h>

	__INIT

/*
 * Xilinx specific entry point for secondary CPU to jump from ROM
 * code.  This routine also provides a holding flag into which
 * secondary core is held until we're ready for it to initialise.
 * The primary core will update the this flag using a hardware
 * register BOOT_REG1.
 */
ENTRY(xilinx_secondary_startup)
	mrc	p15, 0, r0, c0, c0, 5
	and	r0, r0, #0x0f
hold:	ldr	r1, =(BOOT_REG_BASE + BOOT_REG1_OFFSET)	
	ldr	r2, [r1]
	cmp	r2, r0
	bne	hold

	/*
	 * we've been released from the cpu_release,secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup

