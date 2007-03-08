/*
 *  linux/arch/armnommu/mm/arm740.S: MPU functions for ARM740
 * 
 *  Copyright (C) 1997-2000 Russell King
 *  Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *                     Rob Scott (rscott@mtrob.fdns.net)
 *  Copyright (C) 2000 ARM Limited, Deep Blue Solutions Ltd.
 *  Copyright (C) 2004 Hyok S. Choi (hyok.choi@samsung.com)
 */
	
#include <linux/linkage.h>
#include <linux/init.h>	
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/elf.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>


/*
 * cpu_arm740_data_abort()
 *
 * obtain information about current aborted instruction
 *
 * r0 = address of aborted instruction
 *
 * Returns:
 *  r0 = address of abort
 *  r1 != 0 if writing
 *  r3 = FSR
 */
	.align	5
Ldata_ldmstm:
	tst	r4, #1 << 21			@ check writeback bit
	beq	Ldata_simple
	mov	r7, #0x11
	orr	r7, r7, r7, lsl #8
	and	r0, r4, r7
	and	r2, r4, r7, lsl #1
	add	r0, r0, r2, lsr #1
	and	r2, r4, r7, lsl #2
	add	r0, r0, r2, lsr #2
	and	r2, r4, r7, lsl #3
	add	r0, r0, r2, lsr #3
	add	r0, r0, r0, lsr #8
	add	r0, r0, r0, lsr #4
	and	r7, r0, #15			@ r7 = no. of registers to transfer.
	and	r5, r4, #15 << 16		@ Get Rn
	ldr	r0, [sp, r5, lsr #14]		@ Get register
	tst	r4, #1 << 23			@ U bit
	subne	r7, r0, r7, lsl #2
	addeq	r7, r0, r7, lsl #2		@ Do correction (signed)
Ldata_saver7:
	str	r7, [sp, r5, lsr #14]		@ Put register
Ldata_simple:
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
	mrc	p15, 0, r3, c5, c0, 0		@ get FSR
	and	r3, r3, #255
	mov	pc, lr

ENTRY(cpu_arm740_data_abort)
	ldr	r4, [r0]			@ read instruction causing problem
	tst	r4, r4, lsr #21			@ C = bit 20
	sbc	r1, r1, r1			@ r1 = C - 1
	and	r2, r4, #15 << 24
	add	pc, pc, r2, lsr #22		@ Now branch to the relevent processing routine
	movs	pc, lr

	b	Ldata_lateldrhpost		@ ldrh	rd, [rn], #m/rm
	b	Ldata_lateldrhpre		@ ldrh	rd, [rn, #m/rm]
	b	Ldata_unknown
	b	Ldata_unknown
	b	Ldata_lateldrpostconst		@ ldr	rd, [rn], #m
	b	Ldata_lateldrpreconst		@ ldr	rd, [rn, #m] 
	b	Ldata_lateldrpostreg		@ ldr	rd, [rn], rm
	b	Ldata_lateldrprereg		@ ldr	rd, [rn, rm]
	b	Ldata_ldmstm			@ ldm*a	rn, <rlist>
	b	Ldata_ldmstm			@ ldm*b	rn, <rlist>
	b	Ldata_unknown
	b	Ldata_unknown
	b	Ldata_simple			@ ldc	rd, [rn], #m	@ Same as ldr	rd, [rn], #m
	b	Ldata_simple			@ ldc	rd, [rn, #m]
	b	Ldata_unknown

Ldata_unknown:	@ Part of jumptable
	mov	r0, r2
	mov	r1, r4
	mov	r2, r3
	bl	baddataabort
	b	ret_from_exception

Ldata_lateldrhpre:
	tst	r4, #1 << 21			@ check writeback bit
	beq	Ldata_simple
Ldata_lateldrhpost:
	and	r5, r4, #0x00f			@ get Rm / low nibble of immediate value
	tst	r4, #1 << 22			@ if (immediate offset)
	andne	r2, r4, #0xf00			@ { immediate high nibble
	orrne	r2, r5, r2, lsr #4		@   combine nibbles } else
	ldreq	r2, [sp, r5, lsl #2]		@ { load Rm value }
	and	r5, r4, #15 << 16		@ get Rn
	ldr	r0, [sp, r5, lsr #14]		@ load Rn value
	tst	r4, #1 << 23			@ U bit
	subne	r7, r0, r2
	addeq	r7, r0, r2
	b	Ldata_saver7

Ldata_lateldrpreconst:
	tst	r4, #1 << 21			@ check writeback bit
	beq	Ldata_simple
Ldata_lateldrpostconst:
	movs	r2, r4, lsl #20			@ Get offset
	beq	Ldata_simple
	and	r5, r4, #15 << 16		@ Get Rn
	ldr	r0, [sp, r5, lsr #14]
	tst	r4, #1 << 23			@ U bit
	subne	r7, r0, r2, lsr #20
	addeq	r7, r0, r2, lsr #20
	b	Ldata_saver7

Ldata_lateldrprereg:
	tst	r4, #1 << 21			@ check writeback bit
	beq	Ldata_simple
Ldata_lateldrpostreg:
	and	r5, r4, #15
	ldr	r2, [sp, r5, lsl #2]		@ Get Rm
	mov	r3, r4, lsr #7
	ands	r3, r3, #31
	and	r6, r4, #0x70
	orreq	r6, r6, #8
	add	pc, pc, r6
	mov	r0, r0

	mov	r2, r2, lsl r3			@ 0: LSL #!0
	b	1f
	b	1f				@ 1: LSL #0
	mov	r0, r0
	b	1f				@ 2: MUL?
	mov	r0, r0
	b	1f				@ 3: MUL?
	mov	r0, r0
	mov	r2, r2, lsr r3			@ 4: LSR #!0
	b	1f
	mov	r2, r2, lsr #32			@ 5: LSR #32
	b	1f
	b	1f				@ 6: MUL?
	mov	r0, r0
	b	1f				@ 7: MUL?
	mov	r0, r0
	mov	r2, r2, asr r3			@ 8: ASR #!0
	b	1f
	mov	r2, r2, asr #32			@ 9: ASR #32
	b	1f
	b	1f				@ A: MUL?
	mov	r0, r0
	b	1f				@ B: MUL?
	mov	r0, r0
	mov	r2, r2, ror r3			@ C: ROR #!0
	b	1f
	mov	r2, r2, rrx			@ D: RRX
	b	1f
	mov	r0, r0				@ E: MUL?
	mov	r0, r0
	mov	r0, r0				@ F: MUL?


1:	and	r5, r4, #15 << 16		@ Get Rn
	ldr	r0, [sp, r5, lsr #14]
	tst	r4, #1 << 23			@ U bit
	subne	r7, r0, r2
	addeq	r7, r0, r2
	b	Ldata_saver7


/*
 * cpu_arm740_check_bugs()
 */
ENTRY(cpu_arm740_check_bugs)
	mrs	ip, cpsr
	bic	ip, ip, #PSR_F_BIT
	msr	cpsr, ip
	mov	pc, lr

/*
 * cpu_arm740_proc_init()
 */
ENTRY(cpu_arm740_proc_init)
	mov	pc, lr

/*
 * cpu_arm740_proc_fin()
 */
ENTRY(cpu_arm740_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
	ldmfd	sp!, {pc}

/*
 * cpu_arm740_reset(loc)
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm740_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate cache
	mrc	p15, 0, ip, c1, c0, 0		@ get ctrl register
	bic	ip, ip, #0x000e				@ ............wcam
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * idle mode processing 
 */
ENTRY(cpu_arm740_do_idle)  
	mov	pc, lr

/* 
 * *FIXME* 
 */ 	
ENTRY(cpu_arm740_dcache_clean_area)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ flush cache
	mov	pc, lr
	
/*
 * Function: arm740_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, 
 */	
ENTRY(cpu_arm740_switch_mm)
	mov	pc, lr	

	
	/* .section ".text.init", #alloc, #execinstr */ 
	__INIT
	
__arm740_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0		@ invalidate caches

	mov r0, #0x0					@disable region 3-7
	mcr p15, 0, r0, c6, c3
	mcr p15, 0, r0, c6, c4
	mcr p15, 0, r0, c6, c5
	mcr p15, 0, r0, c6, c6
	mcr p15, 0, r0, c6, c7

	mov r0, #0x0000003F			@ (base = 0, size = 4GB, non cacheable, no write buffer)
	mcr p15, 0, r0, c6,	c0		@ enable region 0, default
	mov r0, #0x00000037			@ (base = 0, size = 256MB, cacheable, write buffered)
	mcr p15, 0, r0, c6,	c1		@enable region 1, RAM
	mov r0, #0x20000000
	add r0, r0, #0x37			@ (base = 512MB, size = 256MB, cacheable, write buffered)
	mcr p15, 0, r0, c6,	c2		@enable region 2, ROM/Flash

	mov r0, #0x06				
	mcr p15, 0, r0, c2, c0		@ Region 1&2 cacheable
	mov r0, #0x02			
	mcr p15, 0, r0, c3, c0		@ Region 1 write buferred

	mov r0, #0xff00
	add r0, r0, #0x00ff
	mcr p15, 0, r0, c5, c0		@ all read/write access

	mrc	p15, 0, r0, c1, c0		@ get control register
	orr r0, r0, #0x00030000		@ Split cache mode
	orr	r0, r0, #0x0000000d		@ MPU, Cache, Write Buffer on
	
	mov	pc, lr				@ __ret (head-armv.S)
		
	.size	__arm740_setup, . - __arm740_setup	

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	arm740_processor_functions, #object
ENTRY(arm740_processor_functions)
	.word	cpu_arm740_data_abort
	.word	cpu_arm740_proc_init
	.word	cpu_arm740_proc_fin
	.word	cpu_arm740_reset
	.word   cpu_arm740_do_idle

	.word	cpu_arm740_dcache_clean_area
	.word	cpu_arm740_switch_mm
	.size	arm740_processor_functions, . - arm740_processor_functions

	.section ".rodata"	
	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_arm740_name, #object
cpu_arm740_name:
	.ascii	"ARM740T"
	.size	cpu_arm740_name, . - cpu_arm740_name

	.align


	.section ".proc.info.init", #alloc, #execinstr
	.type	__arm740_proc_info,#object
__arm740_proc_info:
	.long	0x41807400
	.long	0xfffffff0
	.long	0
	.long	0
	b	__arm740_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT
	.long	cpu_arm740_name  
	.long	arm740_processor_functions 
	.long	0
	.long	0
	.long	v4_cache_fns			@ cache model
	.size	__arm740_proc_info, . - __arm740_proc_info


