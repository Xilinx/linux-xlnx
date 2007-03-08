/*
 *  linux/arch/armnommu/mm/proc-lpc22xx.S
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
 *  Modified for lpc22xx cache
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/pgtable.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/hardware.h>
	
ENTRY(lpc22xx_flush_user_cache_all)
ENTRY(lpc22xx_flush_kern_cache_all)
ENTRY(lpc22xx_flush_user_cache_range)
ENTRY(lpc22xx_coherent_kern_range)
ENTRY(lpc22xx_coherent_user_range)
ENTRY(lpc22xx_flush_kern_dcache_page)
ENTRY(lpc22xx_dma_inv_range)
ENTRY(lpc22xx_dma_flush_range)
ENTRY(lpc22xx_dma_clean_range)
	mov	pc, lr

ENTRY(cpu_arm7_dcache_clean_area)
	mov	pc, lr  

/*
 * Function: arm6_7_data_abort ()
 *
 * Params  : r2 = address of aborted instruction
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 */

ENTRY(cpu_arm7_data_abort)
@FIXME
	ldr     r8, [r2]                        @ read arm instruction
	tst     r8, #1 << 20                    @ L = 1 -> write?
	and	r7, r8, #15 << 24
	orr	r1, r7, #1
	orreq   r1, r1, #1 << 8                 @ yes.
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

ENTRY(cpu_arm6_data_abort)
@FIXME
	ldr	r8, [r2]			@ read arm instruction
	tst	r8, #1 << 20			@ L = 1 -> write?
	orreq	r1, r1, #1 << 8			@ yes.
	and	r7, r8, #14 << 24
	teq	r7, #8 << 24			@ was it ldm/stm
	movne	pc, lr

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
 * Function: arm6_7_proc_init (void)
 *	   : arm6_7_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm7_proc_init)
		mov	pc, lr

ENTRY(cpu_arm7_proc_fin)
		mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, r0
		mov	pc, lr

ENTRY(cpu_arm7_do_idle)
		mov	r0, #-EINVAL
		mov	pc, lr

/*
 * Function: arm6_7_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old processes state, and restoring
 *	     the new.
 */
ENTRY(cpu_arm7_switch_mm)
		mov	pc, lr

/*
 * Function: _arm6_7_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm7_reset)
		mov	pc, r0


		__INIT

		.type	__arm7_setup, #function
__arm7_setup:
		mov	pc, lr
		.size	__arm7_setup, . - __arm7_setup
	
		__INITDATA

		.type	lpc22xx_cache_fns, #object
ENTRY(lpc22xx_cache_fns)
		.long	lpc22xx_flush_kern_cache_all
		.long	lpc22xx_flush_user_cache_all
		.long	lpc22xx_flush_user_cache_range
		.long	lpc22xx_coherent_kern_range
		.long	lpc22xx_coherent_user_range
		.long	lpc22xx_flush_kern_dcache_page
		.long	lpc22xx_dma_inv_range
		.long	lpc22xx_dma_clean_range
		.long	lpc22xx_dma_flush_range
		.size	lpc22xx_cache_fns, . - lpc22xx_cache_fns
	
/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm7_processor_functions, #object
ENTRY(arm7_processor_functions)
		.word	cpu_arm7_data_abort
		.word	cpu_arm7_proc_init
		.word	cpu_arm7_proc_fin
		.word	cpu_arm7_reset
		.word	cpu_arm7_do_idle
		.word	cpu_arm7_dcache_clean_area
		.word	cpu_arm7_switch_mm
		.size	arm7_processor_functions, . - arm7_processor_functions

		.section ".rodata"

		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4t"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name

		.type	cpu_lpc22xx, #object
cpu_lpc22xx_name:
		.asciz	"Philips-LPC22xx"
		.size	cpu_lpc22xx_name, . - cpu_lpc22xx_name

		.align

		.section ".proc.info.init", #alloc, #execinstr

		.type	__lpc22xx_proc_info, #object
__lpc22xx_proc_info:
		.long	0x22000000
		.long	0xffff0000
		.long	0
		.long	0
		b	__arm7_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_lpc22xx_name
		.long	arm7_processor_functions
		.long	0
		.long	0
		.long	lpc22xx_cache_fns
		.size	__lpc22xx_proc_info, . - __lpc22xx_proc_info

