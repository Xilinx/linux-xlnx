/*
 *  linux/arch/armnommu/mm/arm940.S: MPU functions for ARM940
 *  2001 Minspeed
 * 
 *  2004/03/22  modified for 2.6 kernel by heechul.yun@samsung.com 
 *  2004/04/20  clean-up by Hyok S. Choi
 */
	
#include <linux/linkage.h>
#include <linux/init.h>	
#include <asm/assembler.h>
#include <asm/elf.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include "proc-macros.S"

	/* FIXME - this is also defined in cache-v4wb.S */ 
#define CACHE_DSIZE	4096	/* 4K */
#define CACHE_DLINESIZE	16	/* 4word */
#define CACHE_DLIMIT	(CACHE_DSIZE * 4)

/*
 * cpu_arm940_data_abort()
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
ENTRY(cpu_arm940_data_abort)
	mrc	p15, 0, r3, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR
	ldr	r1, [r0]			@ read aborted instruction
	tst	r1, r1, lsr #21			@ C = bit 20
	sbc	r1, r1, r1			@ r1 = C - 1
	and	r3, r3, #255
	mov	pc, lr

/*
 * cpu_arm940_check_bugs()
 */
ENTRY(cpu_arm940_check_bugs)
	mrs	ip, cpsr
	bic	ip, ip, #PSR_F_BIT
	msr	cpsr, ip
	mov	pc, lr

/*
 * cpu_arm940_proc_init()
 *	
 *  setup_processor()  - setup.c
 *  +-> cpu_proc_init() - cpu-multi32.h --> proc-arm940.S
 *	
 *  TODO: cp15 initialization code can be placed here - hcyun 
 */
ENTRY(cpu_arm940_proc_init)
	mov	pc, lr

/*
 * cpu_arm940_proc_fin()
 */
ENTRY(cpu_arm940_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
#if defined(CONFIG_CPU_CACHE_V4WT)
	bl	v4wt_flush_kern_cache_all	@ write-through
#elif defined(CONFIG_CPU_CACHE_V4WB)
	bl	v4wb_flush_kern_cache_all	@ write-back
#endif /* CONFIG_CPU_CACHE_V4WT */
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

/*
 * cpu_arm940_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
ENTRY(cpu_arm940_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f			@ ............wcam
	bic	ip, ip, #0x1100			@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * idle mode processing 
 */
ENTRY(cpu_arm940_do_idle)  
#if defined(CONFIG_CPU_ARM940_CPU_IDLE)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
#endif
	mov	pc, lr

/* 
 * *FIXME* 
 */ 	
ENTRY(cpu_arm940_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
	mov	pc, lr
/*
 * Function: arm940_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, 
 */	
ENTRY(cpu_arm940_switch_mm)
	mov	pc, lr	

	
	/* .section ".text.init", #alloc, #execinstr */ 
	__INIT
	
__arm940_setup:
	MRC     p15, 0, R0, c1, c0, 0		@ get control register
		
/*
 * Clear out 'unwanted' bits (then put them in if we need them)
 */
	bic	r0, r0, #0x0e00			@ ....??r.........
	bic	r0, r0, #0x0002			@ ..............a.
	bic	r0, r0, #0x000c			@ W,D
	bic	r0, r0, #0x1000			@ I
/*
 * Turn on what we want
 */
#ifdef CONFIG_CPU_MXU_ENABLE
	orr	r0, r0, #0x0001			@ Enable PU
#else
	bic	r0, r0, #0x0001			@ Disable PU
#endif

#ifdef CONFIG_CPU_ARM940_D_CACHE_ON
	orr	r0, r0, #0x0004			@ Enable D cache
#endif
#ifdef CONFIG_CPU_ARM940_I_CACHE_ON
	orr	r0, r0, #0x1000                 @ I Cache on
#endif
	mov	pc, lr
	.size	__arm940_setup, . - __arm940_setup	

	__INITDATA

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	arm940_processor_functions, #object
ENTRY(arm940_processor_functions)
	.word	cpu_arm940_data_abort
	.word	cpu_arm940_proc_init
	.word	cpu_arm940_proc_fin
	.word	cpu_arm940_reset
	.word   cpu_arm940_do_idle

	.word	cpu_arm940_dcache_clean_area
	.word	cpu_arm940_switch_mm
	.size	arm940_processor_functions, . - arm940_processor_functions

	.section ".rodata"	
	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4t"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name

	.type	cpu_arm940_name, #object
cpu_arm940_name:
	.ascii	"ARM940T"
	.size	cpu_arm940_name, . - cpu_arm940_name

	.align


	.section ".proc.info.init", #alloc, #execinstr
	.type	__arm940_proc_info,#object
__arm940_proc_info:
	.long	0x41009400
	.long	0xff00fff0
	.long	0
	.long	0
	b	__arm940_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_26BIT
	.long	cpu_arm940_name  
	.long	arm940_processor_functions 
	.long	0
	.long	0
#if defined(CONFIG_CPU_CACHE_V4WT)
	.long	v4wt_cache_fns			@ cache model
#elif defined(CONFIG_CPU_CACHE_V4WB)
	.long	v4wb_cache_fns			@ cache model
#endif /* CONFIG_CPU_CACHE_V4WT */
	.size	__arm940_proc_info, . - __arm940_proc_info

