/*
 *  linux/arch/arm/mach-p2001/head.S
 *
 *  Copyright (C) 2004-2005 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Kernel startup code for P2001
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/constants.h>

#define MACHINFO_TYPE           0
#define MACHINFO_PHYSRAM        4
#define MACHINFO_PHYSIO         8
#define MACHINFO_PGOFFIO        12
#define MACHINFO_NAME           16

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
 * r1 = machine nr.
 *
 * See linux/arch/arm/tools/mach-types for the complete list of machine
 * numbers for r1.
 *
 */
	__INIT
	.type	stext, #function
ENTRY(stext)
	mov	r0, #0
	ldr	r1, P2001_MACH_TYPE

	mov	r12, r0
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE	@ make sure svc mode
	msr	cpsr_c, r0			@ and all irqs disabled
	ldr	r9, P2001_PROCESSOR_TYPE

/*
 * Set the Control Register and Read the process ID.
 */
 	mov	r0, #0

/*
 *  r0  = processor control register
 *  r1  = machine ID
 *  r9  = processor ID
 *  r12 = value of r0 when kernel was called (currently always zero)
 */
        adr	r5, LC0
        ldmia	r5, {r5, r6, r7, r8, sp}			@ Setup stack

        /*  Clear BSS */
        mov	r4, #0
1:      cmp	r5, r7					
        strcc	r4, [r5],#4
	bcc	1b

	str	r9, [r6]			@ Save processor ID
	str	r1, [r8]			@ Save machine type

	b	start_kernel

LC0:	.long	__bss_start			@ r5
        .long	processor_id			@ r6
        .long	_end				@ r7
	.long   __machine_arch_type		@ r8
        .long	init_thread_union+8192		@ sp

/*
 *	arm_id/PROCESSOR_TYPE:
 *	31:24:	Implementer		(ARM Ltd)
 *	23:20:	Variant			(0)
 *	19:16:	Architecture		(4T)
 *	15:4:	Primary Part Number	(0)
 *	3:0	Revision		(0)
 */
P2001_PROCESSOR_TYPE:
	.long	0x41029000
P2001_MACH_TYPE:
	.long	MACH_TYPE_P2001

#include "../kernel/head-common.S"
