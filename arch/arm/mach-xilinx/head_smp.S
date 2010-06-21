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
 * code.  This routine provides a wait loop in which secondary core
 * is held until we're ready for it to initialise. The primary core 
 * will update the boot lock with the boot key when it's ready for 
 * the secondary CPU to boot.
 *
 * The WFE must be in the loop when using this code with a probe 
 * because any operations cause debug events when wake up CPU1 falsely.
 */
ENTRY(xilinx_secondary_startup)
	mov	r0, #0
	ldr	r1, =(BOOT_REG_BASE + BOOT_LOCKREG_OFFSET)	
	ldr	r0, =BOOT_LOCK_KEY
	mov	r3, #0
hold:	
	wfe
	add	r3, #1			@ only track number of wakeups 
					@ for diagnostics
	ldr	r2, [r1]
	cmp	r2, r0
	bne	hold

	/*
	 * CPU0 released CPU1 by writing the key to the lock, go ahead
	 * and start CPU1 running the kernel
	 */
	b	secondary_startup

