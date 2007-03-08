/*
 *  linux/arch/arm/mach-s3c3410/head.S
 *
 *  Copyright (C) 2003 Hyok S. Choi
 *  <hyok.choi@samsung.com>
 *
 *  uClinux kernel startup code for s3c3410
 *  which has no proper bootloader for linux startup
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/mach/arch.h>

#define MACHINFO_TYPE           0
#define MACHINFO_PHYSRAM        4
#define MACHINFO_PHYSIO         8
#define MACHINFO_PGOFFIO        12
#define MACHINFO_NAME           16

/*
 * Kernel startup entry point.
 */
	__INIT
	.type	stext, #function
ENTRY(stext)
	mov	r12, r0
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE	@ make sure svc mode
	msr	cpsr_c, r0			@ and all irqs disabled

        adr	r5, LC0
        ldmia	r5, {r5, r6, r8, r9, sp}			@ Setup stack

        /*  Copy data sections to their new home.  */


        /*  Clear BSS */
        mov	r4, #0
1:      cmp	r5, r8					
        strcc	r4, [r5],#4
        bcc	1b

        /*  Pretend we know what our processor code is (for arm_id)   */

	ldr	r2, S3C3410_PROCESSOR_TYPE

	str     r2, [r6]
	ldr     r2, S3C3410_MACH_TYPE
	str     r2, [r9]

        mov fp, #0
        b	start_kernel
        
LC0:	.long	__bss_start
        .long	processor_id
        .long	_end
	.long   __machine_arch_type
        .long	init_thread_union+8192
	
S3C3410_PROCESSOR_TYPE:
	.long	0x34107700
S3C3410_MACH_TYPE:
	.long	MACH_TYPE_S3C3410

#include "../kernel/head-common.S"
