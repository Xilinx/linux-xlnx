/*
 * arch/microblaze/kernel/head.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Portions Copyright 2007 Xilinx, Inc.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>

	.text
ENTRY(_start)
	mfs	r1, rmsr
	andi	r1, r1, ~2
	mts	rmsr, r1

	/* Initialize small data anchors */
	la	r13, r0, _KERNEL_SDA_BASE_
	la	r2, r0, _KERNEL_SDA2_BASE_

	/* Initialize stack pointer */
	la	r1, r0, init_thread_union + THREAD_SIZE - 4

	/* Initialize r31 with current task address */
	la	r31, r0, init_task

	/* Call platform dependent initialize function.
	 * Please see $(ARCH)/mach-$(SUBARCH)/setup.c for
	 * the function. */
//	la	r5, r0, 0 /* no command line */
	la	r6, r0, machine_early_init
	brald	r15, r6
	nop

	la	r15, r0, machine_halt
	braid	start_kernel
	nop

