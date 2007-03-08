/*
 *  linux/arch/armnommu/mm/proc-s3c4510b.S
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
 *  Modified for s3c4510b cache
 *  Copyright (c) 2005 Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/pgtable.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/hardware.h>
	
/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 *
 *	- mm	- mm_struct describing address space
 */
ENTRY(s3c4510b_flush_user_cache_all)
	/* FALLTHROUGH */
/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(s3c4510b_flush_kern_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start - start address (may not be aligned)
 *	- end	- end address (exclusive, may not be aligned)
 *	- flags	- vma_area_struct flags describing address space
 */
ENTRY(s3c4510b_flush_user_cache_range)
	stmfd	sp!, {r5-r8, lr}
	/* save current cache state */
	ldr	r7, =REG_SYSCFG
	ldr     r8, [r7]
	/* disable cache */
        ldr	r6, =0x83ffffa0
	str	r6, [r7]
	/* zero out the 1KB TAG RAM area starting @ 0x11000000 */
	ldr	r7, =0x11000000
	ldr     r6, =(0x11000000 + 1024)
	ldr	r5, =0x0
tag_zero:		
	str     r5, [r7]
	add     r7, r7, #4
	cmp	r6, r7
	bne     tag_zero
	
	/* restore previoux cache state */
	ldr	r7, =REG_SYSCFG
	str	r8, [r7]

	ldmfd	sp!, {r5-r8, pc}
	
/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(s3c4510b_coherent_kern_range)
	/* FALLTHROUGH */

                                                                                                                                           
/*
 *      coherent_user_range(start, end)
 *
 *      Ensure coherency between the Icache and the Dcache in the
 *      region described by start, end.  If you have non-snooping
 *      Harvard caches, you need to implement this function.
 *
 *      - start - virtual start address
 *      - end   - virtual end address
 */
ENTRY(s3c4510b_coherent_user_range)

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 */
ENTRY(s3c4510b_flush_kern_dcache_page)
	/* FALLTHROUGH */

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(s3c4510b_dma_inv_range)
	/* FALLTHROUGH */

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(s3c4510b_dma_flush_range)
	/* FALLTHROUGH */

/*
 *	dma_clean_range(start, end)
 *
 *	Clean (write back) the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(s3c4510b_dma_clean_range)
	/* FALLTHROUGH */

ENTRY(cpu_s3c4510b_dcache_clean_area)
	mov	pc, lr  

/*
 * Function: cpu_s3c4510b_data_abort ()
 *
 * Params  : r2 = address of aborted instruction
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR
 */

ENTRY(cpu_s3c4510b_data_abort)
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


ENTRY(cpu_s3c4510b_proc_init)
		mov	pc, lr

ENTRY(cpu_s3c4510b_proc_fin)
		mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, r0
		mov     r0, #0
		mov	pc, lr

ENTRY(cpu_s3c4510b_do_idle)
		mov	r0, #-EINVAL
		mov	pc, lr

ENTRY(cpu_s3c4510b_switch_mm)
		mov	pc, lr

ENTRY(cpu_s3c4510b_reset)
		mov	r1, #0
		mov	pc, r0


		__INIT

		.type	__s3c4510b_setup, #function
__s3c4510b_setup:
		mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, r0
		mov	r0, #0
		mov	pc, lr
		.size	__s3c4510b_setup, . - __s3c4510b_setup
	
		__INITDATA

		.type	s3c4510b_cache_fns, #object
ENTRY(s3c4510b_cache_fns)
		.long	s3c4510b_flush_kern_cache_all
		.long	s3c4510b_flush_user_cache_all
		.long	s3c4510b_flush_user_cache_range
		.long	s3c4510b_coherent_kern_range
		.long	s3c4510b_coherent_user_range
		.long	s3c4510b_flush_kern_dcache_page
		.long	s3c4510b_dma_inv_range
		.long	s3c4510b_dma_clean_range
		.long	s3c4510b_dma_flush_range
		.size	s3c4510b_cache_fns, . - s3c4510b_cache_fns
	
/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	s3c4510b_processor_functions, #object
ENTRY(s3c4510b_processor_functions)
		.word	cpu_s3c4510b_data_abort
		.word	cpu_s3c4510b_proc_init
		.word	cpu_s3c4510b_proc_fin
		.word	cpu_s3c4510b_reset
		.word	cpu_s3c4510b_do_idle
		.word	cpu_s3c4510b_dcache_clean_area
		.word	cpu_s3c4510b_switch_mm
		.size	s3c4510b_processor_functions, . - s3c4510b_processor_functions

		.section ".rodata"

		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4t"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name

		.type	cpu_s3c4510b, #object
cpu_s3c4510b_name:
		.asciz	"Samsung-S3C4510B"
		.size	cpu_s3c4510b_name, . - cpu_s3c4510b_name

		.align

		.section ".proc.info.init", #alloc, #execinstr

		.type	__s3c4510b_proc_info, #object
__s3c4510b_proc_info:
		.long	0x36807000
		.long	0xfffff000
		.long	0
		.long	0
		b	__s3c4510b_setup
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT
		.long	cpu_s3c4510b_name
		.long	s3c4510b_processor_functions
		.long	0
		.long	0
		.long	s3c4510b_cache_fns
		.size	__s3c4510b_proc_info, . - __s3c4510b_proc_info

