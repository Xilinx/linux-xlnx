/*
 *  linux/arch/arm/mm/proc-arm7tdmi.S
 *
 *  Copyright (C) 1997-2000 Russell King
 *  Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  for ARM7TDMI and the compatibles.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/elf.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

ENTRY(cpu_arm7tdmi_dcache_clean_area)
		mov	pc, lr

/*
 * Function: arm7tdmi_data_abort ()
 *
 * Params  : r2 = address of aborted instruction
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 */

ENTRY(cpu_arm7tdmi_data_abort)
#ifdef CONFIG_CPU_CP15
	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
#endif
	ldr	r8, [r0]			@ read arm instruction
	tst	r8, #1 << 20			@ L = 1 -> write?
	orreq	r1, r1, #1 << 8			@ yes.
	and	r7, r8, #15 << 24
	add	pc, pc, r7, lsr #22		@ Now branch to the relevant processing routine
	nop

/* 0 */	b	.data_unknown
/* 1 */	mov	pc, lr				@ swp
/* 2 */	b	.data_unknown
/* 3 */	b	.data_unknown
/* 4 */	b	.data_arm_lateldrpostconst	@ ldr	rd, [rn], #m
/* 5 */	b	.data_arm_lateldrpreconst	@ ldr	rd, [rn, #m]
/* 6 */	b	.data_arm_lateldrpostreg	@ ldr	rd, [rn], rm
/* 7 */	b	.data_arm_lateldrprereg		@ ldr	rd, [rn, rm]
/* 8 */	b	.data_arm_ldmstm		@ ldm*a	rn, <rlist>
/* 9 */	b	.data_arm_ldmstm		@ ldm*b	rn, <rlist>
/* a */	b	.data_unknown
/* b */	b	.data_unknown
/* c */	mov	pc, lr				@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
/* d */	mov	pc, lr				@ ldc	rd, [rn, #m]
/* e */	b	.data_unknown
/* f */
.data_unknown:	@ Part of jumptable
	mov	r0, r2
	mov	r1, r8
	mov	r2, sp
	bl	baddataabort
	b	ret_from_exception


.data_arm_ldmstm:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
	mov	r7, #0x11
	orr	r7, r7, #0x1100
	and	r6, r8, r7
	and	r2, r8, r7, lsl #1
	add	r6, r6, r2, lsr #1
	and	r2, r8, r7, lsl #2
	add	r6, r6, r2, lsr #2
	and	r2, r8, r7, lsl #3
	add	r6, r6, r2, lsr #3
	add	r6, r6, r6, lsr #8
	add	r6, r6, r6, lsr #4
	and	r6, r6, #15			@ r6 = no. of registers to transfer.
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6, lsl #2		@ Undo increment
	addeq	r7, r7, r6, lsl #2		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_apply_r6_and_rn:
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r6			@ Undo incrmenet
	addeq	r7, r7, r6			@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrpreconst:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostconst:
	movs	r2, r8, lsl #20			@ Get offset
	moveq	pc, lr				@ zero -> no fixup
	and	r5, r8, #15 << 16		@ Extract 'n' from instruction
	ldr	r7, [sp, r5, lsr #14]		@ Get register 'Rn'
	tst	r8, #1 << 23			@ Check U bit
	subne	r7, r7, r2, lsr #20		@ Undo increment
	addeq	r7, r7, r2, lsr #20		@ Undo decrement
	str	r7, [sp, r5, lsr #14]		@ Put register 'Rn'
	mov	pc, lr

.data_arm_lateldrprereg:
	tst	r8, #1 << 21			@ check writeback bit
	moveq	pc, lr				@ no writeback -> no fixup
.data_arm_lateldrpostreg:
	and	r7, r8, #15			@ Extract 'm' from instruction
	ldr	r6, [sp, r7, lsl #2]		@ Get register 'Rm'
	mov	r5, r8, lsr #7			@ get shift count
	ands	r5, r5, #31
	and	r7, r8, #0x70			@ get shift type
	orreq	r7, r7, #8			@ shift count = 0
	add	pc, pc, r7
	nop

	mov	r6, r6, lsl r5			@ 0: LSL #!0
	b	.data_arm_apply_r6_and_rn
	b	.data_arm_apply_r6_and_rn	@ 1: LSL #0
	nop
	b	.data_unknown			@ 2: MUL?
	nop
	b	.data_unknown			@ 3: MUL?
	nop
	mov	r6, r6, lsr r5			@ 4: LSR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, lsr #32			@ 5: LSR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ 6: MUL?
	nop
	b	.data_unknown			@ 7: MUL?
	nop
	mov	r6, r6, asr r5			@ 8: ASR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, asr #32			@ 9: ASR #32
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ A: MUL?
	nop
	b	.data_unknown			@ B: MUL?
	nop
	mov	r6, r6, ror r5			@ C: ROR #!0
	b	.data_arm_apply_r6_and_rn
	mov	r6, r6, rrx			@ D: RRX
	b	.data_arm_apply_r6_and_rn
	b	.data_unknown			@ E: MUL?
	nop
	b	.data_unknown			@ F: MUL?

/*
 * Function: arm7tdmi_proc_init (void)
 *	   : arm7tdmi_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm7tdmi_proc_init)
		mov	pc, lr

ENTRY(cpu_arm7tdmi_proc_fin)
		mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, r0
#ifdef CONFIG_CPU_CP15
		mov	r0, #0x31			@ ....S..DP...M
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
#endif
		mov	pc, lr

ENTRY(cpu_arm7tdmi_do_idle)
		mov	pc, lr

/*
 * Function: arm7tdmi_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 */
ENTRY(cpu_arm7tdmi_switch_mm)
		mov	pc, lr

/*
 * Function: _arm7tdmi_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm7tdmi_reset)
#ifdef CONFIG_CPU_CP15
		mov	r1, #0
		mcr	p15, 0, r1, c7, c0, 0		@ flush cache
		mov	r1, #0x30
		mcr	p15, 0, r1, c1, c0, 0		@ turn off MMU etc
#endif
		mov	pc, r0

		__INIT

		.type	__arm7tdmi_setup, #function
__arm7tdmi_setup:
#ifdef CONFIG_CPU_CP15
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0		@ flush caches on v3
#endif
		mov	r0, #0x7c			@ . .... .LDP WC..
		mov	pc, lr
		.size	__arm7tdmi_setup, . - __arm7tdmi_setup

		__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm7tdmi_processor_functions, #object
ENTRY(arm7tdmi_processor_functions)
		.word	cpu_arm7tdmi_data_abort
		.word	cpu_arm7tdmi_proc_init
		.word	cpu_arm7tdmi_proc_fin
		.word	cpu_arm7tdmi_reset
		.word	cpu_arm7tdmi_do_idle
		.word	cpu_arm7tdmi_dcache_clean_area
		.word	cpu_arm7tdmi_switch_mm
		.size	arm7tdmi_processor_functions, . - arm7tdmi_processor_functions

		.section ".rodata"

		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4t"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name

		.type	cpu_arm7tdmi_name, #object
cpu_arm7tdmi_name:
		.asciz	"ARM7TDMI"
		.size	cpu_arm7tdmi_name, . - cpu_arm7tdmi_name

		.type	cpu_triscenda7_name, #object
cpu_triscenda7_name:
#if defined(CONFIG_ARCH_TA7S)
		.asciz	"Triscend-A7S"
#elif defined(CONFIG_ARCH_TA7V)
		.asciz	"Triscend-A7V"
#endif
		.size	cpu_triscenda7_name, . - cpu_triscenda7_name

		.type	cpu_at91_name, #object
cpu_at91_name:
		.asciz	"Atmel-AT91M40xxx"
		.size	cpu_at91_name, . - cpu_at91_name

		.type	cpu_s3c3410_name, #object
cpu_s3c3410_name:
		.asciz	"Samsung-S3C3410"
		.size	cpu_s3c3410_name, . - cpu_s3c3410_name

		.type	cpu_s3c44b0x_name, #object
cpu_s3c44b0x_name:
		.asciz	"Samsung-S3C44B0x"
		.size	cpu_s3c44b0x_name, . - cpu_s3c44b0x_name

		.type	cpu_s3c4510b, #object
cpu_s3c4510b_name:
		.asciz	"Samsung-S3C4510B"
		.size	cpu_s3c4510b_name, . - cpu_s3c4510b_name

		.type	cpu_s3c4530_name, #object
cpu_s3c4530_name:
		.asciz	"Samsung-S3C4530"
		.size	cpu_s3c4530_name, . - cpu_s3c4530_name

		.type	cpu_netarm_name, #object
cpu_netarm_name:
		.asciz	"NETARM"
		.size	cpu_netarm_name, . - cpu_netarm_name

		.align

		.section ".proc.info.init", #alloc, #execinstr

		.type	__arm7tdmi_proc_info, #object
__arm7tdmi_proc_info:
		.long	0x41007700
		.long	0xfff8ff00
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_arm7tdmi_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__arm7tdmi_proc_info, . - __arm7dmi_proc_info

		.type	__triscenda7_proc_info, #object
__triscenda7_proc_info:
		.long	0x0001d2ff
		.long	0x0001ffff
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_triscenda7_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__triscenda7_proc_info, . - __triscenda7_proc_info

		.type	__at91_proc_info, #object
__at91_proc_info:
		.long	0x14000040
		.long	0xfff000e0
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_at91_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__at91_proc_info, . - __at91_proc_info

		.type	__s3c4510b_proc_info, #object
__s3c4510b_proc_info:
		.long	0x36365000
		.long	0xfffff000
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_s3c4510b_name
		.long	arm7tdmi_processor_functions
		.long	v4_cache_fns
		.long	0
		.long	0
		.size	__s3c4510b_proc_info, . - __s3c4510b_proc_info

		.type	__s3c4530_proc_info, #object
__s3c4530_proc_info:
		.long	0x4c000000
		.long	0xfff000e0
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_s3c4530_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__s3c4530_proc_info, . - __s3c4530_proc_info

		.type	__s3c3410_proc_info, #object
__s3c3410_proc_info:
		.long	0x34100000
		.long	0xffff0000
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_s3c3410_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__s3c3410_proc_info, . - __s3c3410_proc_info

		.type	__s3c44b0x_proc_info, #object
__s3c44b0x_proc_info:
		.long	0x44b00000
		.long	0xffff0000
		.long	0
		.long	0
		b	__arm7tdmi_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	cpu_s3c44b0x_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__s3c44b0x_proc_info, . - __s3c44b0x_proc_info
