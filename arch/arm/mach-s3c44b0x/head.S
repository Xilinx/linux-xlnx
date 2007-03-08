/*
 *  linux/arch/armnommu/mach-s3c44b0x/head.S
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/mach/arch.h>
#include <asm/arch/hardware.h>

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
	/*
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE	@ make sure svc mode
	*/
	mov	r0, #PSR_I_BIT | MODE_SVC		@ make sure svc mode
	msr	cpsr_c, r0				@ and all irqs disabled

        adr	r5, LC0
        ldmia	r5, {r5, r6, r8, r9, sp}		@ Setup stack

        /*  Copy data sections to their new home.  */


        /*  Clear BSS */
        mov	r4, #0
1:      cmp	r5, r8					
        strcc	r4, [r5],#4
        bcc	1b

	/* handle CPU Cache */
	ldr	r4, =S3C44B0X_SYSCFG
	mov	r5, #0x0e
	str	r5, [r4]
	
        /*  Pretend we know what our processor code is (for arm_id)   */


	ldr	r2, S3C44B0_PROCESSOR_TYPE

	str     r2, [r6]
	ldr     r2, S3C44B0_MACH_TYPE
	str     r2, [r9]

        mov 	fp, #0
        b	start_kernel
        
LC0:	.long	__bss_start
        .long	processor_id
        .long	_end
	.long   __machine_arch_type
        .long	init_thread_union+8192
	
S3C44B0_PROCESSOR_TYPE:
	.long	0x44b07700
S3C44B0_MACH_TYPE:
	.long	MACH_TYPE_S3C44B0

#include "../kernel/head-common.S"
