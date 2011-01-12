/*
 *  linux/arch/arm/kernel/head.S
 *
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Kernel startup code for all 32-bit CPUs
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/domain.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/memory.h>
#include <asm/thread_info.h>
#include <asm/system.h>
#include <asm/mach-types.h>

#ifdef CONFIG_DEBUG_LL
#include <mach/debug-macro.S>
#endif

#if (PHYS_OFFSET & 0x001fffff)
#error "PHYS_OFFSET must be at an even 2MiB boundary!"
#endif

#define KERNEL_RAM_VADDR	(PAGE_OFFSET + TEXT_OFFSET)
#define KERNEL_RAM_PADDR	(PHYS_OFFSET + TEXT_OFFSET)


/*
 * swapper_pg_dir is the virtual address of the initial page table.
 * We place the page tables 16K below KERNEL_RAM_VADDR.  Therefore, we must
 * make sure that KERNEL_RAM_VADDR is correctly set.  Currently, we expect
 * the least significant 16 bits to be 0x8000, but we could probably
 * relax this restriction to KERNEL_RAM_VADDR >= PAGE_OFFSET + 0x4000.
 */
#if (KERNEL_RAM_VADDR & 0xffff) != 0x8000
#error KERNEL_RAM_VADDR must start at 0xXXXX8000
#endif

	.globl	swapper_pg_dir
	.equ	swapper_pg_dir, KERNEL_RAM_VADDR - 0x4000

	.macro	pgtbl, rd
	ldr	\rd, =(KERNEL_RAM_PADDR - 0x4000)
	.endm

#ifdef CONFIG_XIP_KERNEL
#define KERNEL_START	XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR)
#define KERNEL_END	_edata_loc
#else
#define KERNEL_START	KERNEL_RAM_VADDR
#define KERNEL_END	_end
#endif

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
 * r1 = machine nr, r2 = atags pointer.
 *
 * This code is mostly position independent, so if you link the kernel at
 * 0xc0008000, you call this at __pa(0xc0008000).
 *
 * See linux/arch/arm/tools/mach-types for the complete list of machine
 * numbers for r1.
 *
 * We're trying to keep crap to a minimum; DO NOT add any machine specific
 * crap here - that's what the boot loader (or in extreme, well justified
 * circumstances, zImage) is for.
 */
	__HEAD
ENTRY(stext)

#ifdef CONFIG_XILINX_AMP_CPU0_MASTER

	/* For AMP code, in the master, to allow an application on the master to 
	   reset CPU1 and cause it to come here, then to CPU1 kernel code.

	   Determine which CPU is executing this code as CPU0 is the primary CPU
	   and should execute the kernel as normal, CPU1 just jumps to the kernel
	   as this just servers as an intermediate jump for now, once the boot rom
	   has the new handshake protocol in it this will go away
	*/

	mrc	p15,0,r0,c0,c0,5
	and	r0,r0,#0x0f
	cmp	r0,#0
	beq	normal_startup

	ldr	r1, =0x4208000			@ load CPU1 boot address
	mov	pc,r1				@ jump to the boot address for CPU1
#endif

#if defined(CONFIG_ARCH_XILINX) && defined(CONFIG_SMP)

#ifdef NO_BOOT_ROM_MULTICORE_PROTOCOL

	/* Determine which CPU is executing this code as CPU0 is the primary CPU
	   and should execute the kernel as normal, CPU1 would normally be held
	   in a WFE state by a boot loader.
	*/

	mrc	p15,0,r0,c0,c0,5
	and	r0,r0,#0x0f
	cmp	r0,#0
	beq	primary_startup

	/* This code is a mailbox type system so that CPU1 is held waiting for 
	   CPU0 to turn it lose. CPU0 will write the boot address into the boot
	   address register which CPU1 is waiting for. CPU0 must also do an sev
	   instruction to get CPU0 out of it's sleep state.
	*/

	mov	r0, #0			
	ldr	r1, =(BOOT_REG_BASE + BOOT_ADDRREG_OFFSET)	
	ldr	r2, =(BOOT_REG_BASE + BOOT_LOCKREG_OFFSET)	
	str	r0, [r2]			@ initialize the boot lock
	str	r0, [r1]			@ initialize the boot address, this has to 
						@ get done before CPU0 uses it 

	/* CPU1 needs to wait for CPU0 to be ready for it to execute, so wait
	   here. Don't spin waiting for the address as it seems to prevent CPU0 from
	   getting any throughput and it won't boot.
 
	   Wait for an event from CPU0 but not just once as events happen many
	   times when using the JTAG TCL environment, like targeting the other CPU,
	   because of debug events. CPU0 will flush it's D cache with the address before
	   it does a sev instruction so that after the correct wfe wakeup CPU1 will
	   see the boot key.
	*/
	mov 	r3, #0				@ keep a count of number of wakeups for diag

wait4address:
	wfe					@ wait for an event so CPU0 gets thruput
	add	r3, #1				@ count # of times wakeup happens
	ldr	r2,[r1]				@ get the boot address register contents
	cmp	r2,r0				@ if it's zero then wait for a non-zero value
	beq	wait4address

	mov	pc,r2				@ jump to the boot address specified in the 
						@ boot address register from CPU0

primary_startup:

	# there's not a reset vector at 0 and the 2nd CPU needs one to get to the Linux
	# kernel, so make it for now with a b to 0x8000 which is where the kernel is
	# in ram, this assumes low vectors for CPU1 and the kernel is at 0x8000
	# this assumes DDR is mapped to 0 and the MMU is off coming into Linux

	ldr r1, =0x0
	ldr r2, =0xE3A0F902
	str r2, [r1]

	# turn CPU1 lose so it transitions from the boot rom to the kernel
	# do this by resetting CPU1 (set reset register bit, then clear it)

	# unlock SLCR
	ldr r1, =(SLC_REG + 8)
	ldr r2, =0xDF0D
	str r2, [r1]

	#reset CPU1
	ldr r1, =(SLC_REG + 0x244)
	ldr r2, =0x2
	str r2, [r1]
	ldr r2, =0x0
	str r2, [r1]

	# relock SLCR
	ldr r1, =(SLC_REG + 0x4)
	ldr r2, =0x767B
	str r2, [r1]
#endif

#endif

normal_startup:

	# Normal kernel startup starts here (non-Xilinx)

	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, r9 @ ensure svc mode
						@ and irqs disabled
	mrc	p15, 0, r9, c0, c0		@ get processor id
	bl	__lookup_processor_type		@ r5=procinfo r9=cpuid
	movs	r10, r5				@ invalid processor (r5=0)?
 THUMB( it	eq )		@ force fixup-able long branch encoding
	beq	__error_p			@ yes, error 'p'

	/* the machine has no way to get setup when we're using a pod so setup it
	 * up for now this way
	 */
#ifdef CONFIG_ARCH_XILINX
	mov	r0,#0x0
	ldr	r1,=MACH_TYPE_XILINX
	mov	r2,#0x0
#endif

	bl	__lookup_machine_type		@ r5=machinfo
	movs	r8, r5				@ invalid machine (r5=0)?
 THUMB( it	eq )		@ force fixup-able long branch encoding
	beq	__error_a			@ yes, error 'a'
	bl	__vet_atags
#ifdef CONFIG_SMP_ON_UP
	bl	__fixup_smp
#endif
	bl	__create_page_tables

	/*
	 * The following calls CPU specific code in a position independent
	 * manner.  See arch/arm/mm/proc-*.S for details.  r10 = base of
	 * xxx_proc_info structure selected by __lookup_machine_type
	 * above.  On return, the CPU will be ready for the MMU to be
	 * turned on, and r0 will hold the CPU control register value.
	 */
	ldr	r13, =__mmap_switched		@ address to jump to after
						@ mmu has been enabled
	adr	lr, BSYM(1f)			@ return (PIC) address
 ARM(	add	pc, r10, #PROCINFO_INITFUNC	)
 THUMB(	add	r12, r10, #PROCINFO_INITFUNC	)
 THUMB(	mov	pc, r12				)
1:	b	__enable_mmu
ENDPROC(stext)
	.ltorg

/*
 * Setup the initial page tables.  We only setup the barest
 * amount which are required to get the kernel running, which
 * generally means mapping in the kernel code.
 *
 * r8  = machinfo
 * r9  = cpuid
 * r10 = procinfo
 *
 * Returns:
 *  r0, r3, r5-r7 corrupted
 *  r4 = physical page table address
 */
__create_page_tables:
	pgtbl	r4				@ page table address

	/*
	 * Clear the 16K level 1 swapper page table
	 */
	mov	r0, r4
	mov	r3, #0
	add	r6, r0, #0x4000
1:	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	teq	r0, r6
	bne	1b

	ldr	r7, [r10, #PROCINFO_MM_MMUFLAGS] @ mm_mmuflags

	/*
	 * Create identity mapping to cater for __enable_mmu.
	 * This identity mapping will be removed by paging_init().
	 */
	adr	r0, __enable_mmu_loc
	ldmia	r0, {r3, r5, r6}
	sub	r0, r0, r3			@ virt->phys offset
	add	r5, r5, r0			@ phys __enable_mmu
	add	r6, r6, r0			@ phys __enable_mmu_end
	mov	r5, r5, lsr #20
	mov	r6, r6, lsr #20

1:	orr	r3, r7, r5, lsl #20		@ flags + kernel base
	str	r3, [r4, r5, lsl #2]		@ identity mapping
	teq	r5, r6
	addne	r5, r5, #1			@ next section
	bne	1b

	/*
	 * Now setup the pagetables for our kernel direct
	 * mapped region.
	 */
	mov	r3, pc
	mov	r3, r3, lsr #20
	orr	r3, r7, r3, lsl #20
	add	r0, r4,  #(KERNEL_START & 0xff000000) >> 18
	str	r3, [r0, #(KERNEL_START & 0x00f00000) >> 18]!
	ldr	r6, =(KERNEL_END - 1)
	add	r0, r0, #4
	add	r6, r4, r6, lsr #18
1:	cmp	r0, r6
	add	r3, r3, #1 << 20
	strls	r3, [r0], #4
	bls	1b

#ifdef CONFIG_XIP_KERNEL
	/*
	 * Map some ram to cover our .data and .bss areas.
	 */
	orr	r3, r7, #(KERNEL_RAM_PADDR & 0xff000000)
	.if	(KERNEL_RAM_PADDR & 0x00f00000)
	orr	r3, r3, #(KERNEL_RAM_PADDR & 0x00f00000)
	.endif
	add	r0, r4,  #(KERNEL_RAM_VADDR & 0xff000000) >> 18
	str	r3, [r0, #(KERNEL_RAM_VADDR & 0x00f00000) >> 18]!
	ldr	r6, =(_end - 1)
	add	r0, r0, #4
	add	r6, r4, r6, lsr #18
1:	cmp	r0, r6
	add	r3, r3, #1 << 20
	strls	r3, [r0], #4
	bls	1b
#endif

	/*
	 * Then map first 1MB of ram in case it contains our boot params.
	 */
	add	r0, r4, #PAGE_OFFSET >> 18
	orr	r6, r7, #(PHYS_OFFSET & 0xff000000)
	.if	(PHYS_OFFSET & 0x00f00000)
	orr	r6, r6, #(PHYS_OFFSET & 0x00f00000)
	.endif
	str	r6, [r0]

#ifdef CONFIG_DEBUG_LL
#ifndef CONFIG_DEBUG_ICEDCC
	/*
	 * Map in IO space for serial debugging.
	 * This allows debug messages to be output
	 * via a serial console before paging_init.
	 */
	addruart r7, r3

	mov	r3, r3, lsr #20
	mov	r3, r3, lsl #2

	add	r0, r4, r3
	rsb	r3, r3, #0x4000			@ PTRS_PER_PGD*sizeof(long)
	cmp	r3, #0x0800			@ limit to 512MB
	movhi	r3, #0x0800
	add	r6, r0, r3
	mov	r3, r7, lsr #20
	ldr	r7, [r10, #PROCINFO_IO_MMUFLAGS] @ io_mmuflags
	orr	r3, r7, r3, lsl #20
1:	str	r3, [r0], #4
	add	r3, r3, #1 << 20
	teq	r0, r6
	bne	1b

#else /* CONFIG_DEBUG_ICEDCC */
	/* we don't need any serial debugging mappings for ICEDCC */
	ldr	r7, [r10, #PROCINFO_IO_MMUFLAGS] @ io_mmuflags
#endif /* !CONFIG_DEBUG_ICEDCC */

#if defined(CONFIG_ARCH_NETWINDER) || defined(CONFIG_ARCH_CATS)
	/*
	 * If we're using the NetWinder or CATS, we also need to map
	 * in the 16550-type serial port for the debug messages
	 */
	add	r0, r4, #0xff000000 >> 18
	orr	r3, r7, #0x7c000000
	str	r3, [r0]
#endif
#ifdef CONFIG_ARCH_RPC
	/*
	 * Map in screen at 0x02000000 & SCREEN2_BASE
	 * Similar reasons here - for debug.  This is
	 * only for Acorn RiscPC architectures.
	 */
	add	r0, r4, #0x02000000 >> 18
	orr	r3, r7, #0x02000000
	str	r3, [r0]
	add	r0, r4, #0xd8000000 >> 18
	str	r3, [r0]
#endif
#endif
	mov	pc, lr
ENDPROC(__create_page_tables)
	.ltorg
	.align
__enable_mmu_loc:
	.long	.
	.long	__enable_mmu
	.long	__enable_mmu_end

#if defined(CONFIG_SMP)
	__CPUINIT
ENTRY(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 *
	 * Ensure that we're in SVC mode, and IRQs are disabled.  Lookup
	 * the processor type - there is no need to check the machine type
	 * as it has already been validated by the primary processor.
	 */
	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, r9
	mrc	p15, 0, r9, c0, c0		@ get processor id
	bl	__lookup_processor_type
	movs	r10, r5				@ invalid processor?
	moveq	r0, #'p'			@ yes, error 'p'
 THUMB( it	eq )		@ force fixup-able long branch encoding
	beq	__error_p

	/*
	 * Use the page tables supplied from  __cpu_up.
	 */
	adr	r4, __secondary_data
	ldmia	r4, {r5, r7, r12}		@ address to jump to after
	sub	r4, r4, r5			@ mmu has been enabled
	ldr	r4, [r7, r4]			@ get secondary_data.pgdir
	adr	lr, BSYM(__enable_mmu)		@ return address
	mov	r13, r12			@ __secondary_switched address
 ARM(	add	pc, r10, #PROCINFO_INITFUNC	) @ initialise processor
						  @ (return control reg)
 THUMB(	add	r12, r10, #PROCINFO_INITFUNC	)
 THUMB(	mov	pc, r12				)
ENDPROC(secondary_startup)

	/*
	 * r6  = &secondary_data
	 */
ENTRY(__secondary_switched)
	ldr	sp, [r7, #4]			@ get secondary_data.stack
	mov	fp, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)

	.align

	.type	__secondary_data, %object
__secondary_data:
	.long	.
	.long	secondary_data
	.long	__secondary_switched
#endif /* defined(CONFIG_SMP) */



/*
 * Setup common bits before finally enabling the MMU.  Essentially
 * this is just loading the page table pointer and domain access
 * registers.
 *
 *  r0  = cp#15 control register
 *  r1  = machine ID
 *  r2  = atags pointer
 *  r4  = page table pointer
 *  r9  = processor ID
 *  r13 = *virtual* address to jump to upon completion
 */
__enable_mmu:
#ifdef CONFIG_ALIGNMENT_TRAP
	orr	r0, r0, #CR_A
#else
	bic	r0, r0, #CR_A
#endif
#ifdef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CR_C
#endif
#ifdef CONFIG_CPU_BPREDICT_DISABLE
	bic	r0, r0, #CR_Z
#endif
#ifdef CONFIG_CPU_ICACHE_DISABLE
	bic	r0, r0, #CR_I
#endif
	mov	r5, #(domain_val(DOMAIN_USER, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_TABLE, DOMAIN_MANAGER) | \
		      domain_val(DOMAIN_IO, DOMAIN_CLIENT))
	mcr	p15, 0, r5, c3, c0, 0		@ load domain access register
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer
	b	__turn_mmu_on
ENDPROC(__enable_mmu)

/*
 * Enable the MMU.  This completely changes the structure of the visible
 * memory space.  You will not be able to trace execution through this.
 * If you have an enquiry about this, *please* check the linux-arm-kernel
 * mailing list archives BEFORE sending another post to the list.
 *
 *  r0  = cp#15 control register
 *  r1  = machine ID
 *  r2  = atags pointer
 *  r9  = processor ID
 *  r13 = *virtual* address to jump to upon completion
 *
 * other registers depend on the function called upon completion
 */
	.align	5
__turn_mmu_on:
	mov	r0, r0
	mcr	p15, 0, r0, c1, c0, 0		@ write control reg
	mrc	p15, 0, r3, c0, c0, 0		@ read id reg
	mov	r3, r3
	mov	r3, r13
	mov	pc, r3
__enable_mmu_end:
ENDPROC(__turn_mmu_on)


#ifdef CONFIG_SMP_ON_UP
__fixup_smp:
	mov	r7, #0x00070000
	orr	r6, r7, #0xff000000	@ mask 0xff070000
	orr	r7, r7, #0x41000000	@ val 0x41070000
	and	r0, r9, r6
	teq	r0, r7			@ ARM CPU and ARMv6/v7?
	bne	__fixup_smp_on_up	@ no, assume UP

	orr	r6, r6, #0x0000ff00
	orr	r6, r6, #0x000000f0	@ mask 0xff07fff0
	orr	r7, r7, #0x0000b000
	orr	r7, r7, #0x00000020	@ val 0x4107b020
	and	r0, r9, r6
	teq	r0, r7			@ ARM 11MPCore?
	moveq	pc, lr			@ yes, assume SMP

	mrc	p15, 0, r0, c0, c0, 5	@ read MPIDR
	tst	r0, #1 << 31
	movne	pc, lr			@ bit 31 => SMP

__fixup_smp_on_up:
	adr	r0, 1f
	ldmia	r0, {r3, r6, r7}
	sub	r3, r0, r3
	add	r6, r6, r3
	add	r7, r7, r3
2:	cmp	r6, r7
	ldmia	r6!, {r0, r4}
	strlo	r4, [r0, r3]
	blo	2b
	mov	pc, lr
ENDPROC(__fixup_smp)

	.align
1:	.word	.
	.word	__smpalt_begin
	.word	__smpalt_end

	.pushsection .data
	.globl	smp_on_up
smp_on_up:
	ALT_SMP(.long	1)
	ALT_UP(.long	0)
	.popsection

#endif

#include "head-common.S"
