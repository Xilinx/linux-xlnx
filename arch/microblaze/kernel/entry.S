/*
 * arch/microblaze/kernel/entry.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/registers.h>
#include <asm/unistd.h>

/* FIXME: should be ifdef'ed */
	.macro	disable_irq
	msrclr r0, MSR_IE
	.endm

	.macro	clear_bip
	msrclr r0, MSR_BIP
	.endm

/* Xilinx has been reserved interrupt vector area from 0x0 to
 * 0x4f. we'll use 0x60 to store temporary variable, 0x64 to store the
 * current, 0x68 to store kernel/user mode, 0x6c to store original
 * stack pointer */

ENTRY(_interrupt)
	swi	r1, r0, 0x6c		/* save the current sp */
	swi	r11, r0, 0x60		/* temporarily save r11 */
	lwi	r11, r0, 0x68		/* load mode indicator */
	beqid	r11, switch_to_kernel_stack
	nop
kernel_mode:
	brid	save_context		/* jump over */
	addik	r1, r1, (-PT_SIZE)
switch_to_kernel_stack:
	lwi	r1, r0, 0x64		/* get the saved current */
	lwi	r1, r1, TS_THREAD_INFO	/* get the thread info */
	addik	r1, r1, THREAD_SIZE - PT_SIZE	/* calcurate kernel stack pointer */
save_context:
	swi	r11, r1, PT_MODE	/* store the mode */
	lwi	r11, r0, 0x60		/* reload r11 */
	swi	r2, r1, PT_R2
	swi	r3, r1, PT_R3
	swi	r4, r1, PT_R4
	swi	r5, r1, PT_R5
	swi	r6, r1, PT_R6
	swi	r7, r1, PT_R7
	swi	r8, r1, PT_R8
	swi	r9, r1, PT_R9
	swi	r10, r1, PT_R10
	swi	r11, r1, PT_R11
	swi	r12, r1, PT_R12
	swi	r13, r1, PT_R13
	swi	r14, r1, PT_R14
	swi	r14, r1, PT_PC
	swi	r15, r1, PT_R15
	swi	r16, r1, PT_R16
	swi	r17, r1, PT_R17
	swi	r18, r1, PT_R18
	swi	r19, r1, PT_R19
	swi	r20, r1, PT_R20
	swi	r21, r1, PT_R21
	swi	r22, r1, PT_R22
	swi	r23, r1, PT_R23
	swi	r24, r1, PT_R24
	swi	r25, r1, PT_R25
	swi	r26, r1, PT_R26
	swi	r27, r1, PT_R27
	swi	r28, r1, PT_R28
	swi	r29, r1, PT_R29
	swi	r30, r1, PT_R30
	swi	r31, r1, PT_R31
	/* special purpose registers */
	mfs	r11, rmsr
	swi	r11, r1, PT_MSR
	mfs	r11, rear
	swi	r11, r1, PT_EAR
	mfs	r11, resr
	swi	r11, r1, PT_ESR
	mfs	r11, rfsr
	swi	r11, r1, PT_FSR
	/* reload original stack pointer and save it */
	lwi	r11, r0, 0x6c
	swi	r11, r1, PT_SP
	/* update mode indicator we are in kernel mode */
	lwi	r11, r0, 1
	swi	r11, r0, 0x68	
	/* restore r31 */
	lwi	r31, r0, 0x64
	/* prepare the link register, the argument and jump */
	la	r15, r0, ret_from_intr - 8
	addk	r6, r0, r15
	braid	do_IRQ
	add	r5, r0, r1

ret_from_intr:
	/* save r31 */
	swi	r31, r0, 0x64
	/* save mode indicator */
	lwi	r11, r1, PT_MODE
	swi	r11, r0, 0x68
restore_context:
	/* sepcial purpose registers */
	lwi	r11, r1, PT_FSR
	mts	rfsr, r11
	lwi	r11, r1, PT_ESR
	mts	resr, r11
	lwi	r11, r1, PT_EAR
	mts	rear, r11
	lwi	r11, r1, PT_MSR
	mts	rmsr, r11

	lwi	r31, r1, PT_R31
	lwi	r30, r1, PT_R30
	lwi	r29, r1, PT_R29
	lwi	r28, r1, PT_R28
	lwi	r27, r1, PT_R27
	lwi	r26, r1, PT_R26
	lwi	r25, r1, PT_R25
	lwi	r24, r1, PT_R24
	lwi	r23, r1, PT_R23
	lwi	r22, r1, PT_R22
	lwi	r21, r1, PT_R21
	lwi	r20, r1, PT_R20
	lwi	r19, r1, PT_R19
	lwi	r18, r1, PT_R18
	lwi	r17, r1, PT_R17
	lwi	r16, r1, PT_R16
	lwi	r15, r1, PT_R15
	lwi	r14, r1, PT_R14
	lwi	r13, r1, PT_R13
	lwi	r12, r1, PT_R12
	lwi	r11, r1, PT_R11
	lwi	r10, r1, PT_R10
	lwi	r9, r1, PT_R9
	lwi	r8, r1, PT_R8
	lwi	r7, r1, PT_R7
	lwi	r6, r1, PT_R6
	lwi	r5, r1, PT_R5
	lwi	r4, r1, PT_R4
	lwi	r3, r1, PT_R3
	lwi	r2, r1, PT_R2
	lwi	r1, r1, PT_SP
	rtid	r14, 0
	nop

ENTRY(_reset)
	bri	0

ENTRY(_user_exception)
	swi	r12, r0, 0x70		/* sys num */
	swi	r1, r0, 0x6c		/* save the current sp */
	lwi	r1, r0, 0x64		/* get the saved current */
	lwi	r1, r1, TS_THREAD_INFO	/* get the thread info */
	addik	r1, r1, THREAD_SIZE - PT_SIZE	/* get the kernel stack */
	swi	r11, r0, 0x60		/* temporarily save r11 */
	lwi	r11, r0, 0x68		/* load mode indicator */
//save_context:
	swi	r11, r1, PT_MODE	/* store the mode */
	lwi	r11, r0, 0x60		/* reload r11 */
	/* save them on stack */
	swi	r2, r1, PT_R2
	swi	r3, r1, PT_R3		/* r3: _always_ in clogger list; see unistd.h */
	swi	r4, r1, PT_R4		/* r4: _always_ in clogger list; see unistd.h */
	swi	r5, r1, PT_R5
	swi	r6, r1, PT_R6
	swi	r7, r1, PT_R7
	swi	r8, r1, PT_R8
	swi	r9, r1, PT_R9
	swi	r10, r1, PT_R10
	swi	r11, r1, PT_R11
	swi	r12, r1, PT_R12		/* r12: _always_ in clogger list; see unistd.h */
	swi	r13, r1, PT_R13
	swi	r14, r1, PT_R14		/* r14: _always_ in clogger list; see unistd.h */
	addik	r14, r14, 0x4		/* but we want to return to the next inst. */
	swi	r14, r1, PT_PC		/* increment by 4 and store in pc */
	swi	r15, r1, PT_R15
	swi	r16, r1, PT_R16
	swi	r17, r1, PT_R17
	swi	r18, r1, PT_R18
	swi	r19, r1, PT_R19
	swi	r20, r1, PT_R20
	swi	r21, r1, PT_R21
	swi	r22, r1, PT_R22
	swi	r23, r1, PT_R23
	swi	r24, r1, PT_R24
	swi	r25, r1, PT_R25
	swi	r26, r1, PT_R26
	swi	r27, r1, PT_R27
	swi	r28, r1, PT_R28
	swi	r29, r1, PT_R29
	swi	r30, r1, PT_R30
	swi	r31, r1, PT_R31

	disable_irq
	nop					/* make sure IE bit is in effect */
	clear_bip				/* once IE is in effect it is safe to clear BIP */
	nop

	/* special purpose registers */
	mfs	r11, rmsr
	swi	r11, r1, PT_MSR
	mfs	r11, rear
	swi	r11, r1, PT_EAR
	mfs	r11, resr
	swi	r11, r1, PT_ESR
	mfs	r11, rfsr
	swi	r11, r1, PT_FSR
	/* reload original stack pointer and save it */
	lwi	r11, r0, 0x6c
	swi	r11, r1, PT_SP
	/* update mode indicator we are in kernel mode */
	lwi	r11, r0, 1
	swi	r11, r0, 0x68	
	/* restore r31 */
	lwi	r31, r0, 0x64

	addi	r11, r12, -NR_syscalls		/* See if the system call number is valid. */
	bgei	r11, 1f				/* return to user if not valid
	/* Figure out which function to use for this system call. */
	/* Note Microblaze barrel shift is optional, so don't rely on it */
	add	r12, r12, r12			/* convert num -> ptr */
	add	r12, r12, r12
	lwi	r12, r12, sys_call_table	/* Get function pointer */
	la	r15, r0, ret_to_user-8		/* set retrun address */
	bra	r12				/* Make the system call. */
	bri	0				/* won't reach here */
1:
	brid	ret_to_user			/* jump to syscall epilogue */
	addi	r3, r0, -ENOSYS			/* set errno in delay slot  */


ENTRY(_break)
	bri	0
ENTRY(_hardware_exception)
	bri	0


/* struct task_struct *_switch_to(struct thread_info *prev, struct thread_info *next); */
ENTRY(_switch_to)
	/* prepare return value */
	addk	r3, r0, r31

	/* save registers in cpu_context */
	/* use r11 and r12, volatile registers, as temp register */
	addik	r11, r5, TI_CPU_CONTEXT
	swi	r1, r11, CC_SP
	swi	r2, r11, CC_R2
	/* skip volatile registers.
	 * they are saved on stack when we jumped to _switch_to() */
	/* dedicated registers */
	swi	r13, r11, CC_R13
	swi	r14, r11, CC_R14
	swi	r15, r11, CC_R15
	swi	r16, r11, CC_R16
	swi	r17, r11, CC_R17
	swi	r18, r11, CC_R18
	/* save non-volatile registers */
	swi	r19, r11, CC_R19
	swi	r20, r11, CC_R20
	swi	r21, r11, CC_R21
	swi	r22, r11, CC_R22
	swi	r23, r11, CC_R23
	swi	r24, r11, CC_R24
	swi	r25, r11, CC_R25
	swi	r26, r11, CC_R26
	swi	r27, r11, CC_R27
	swi	r28, r11, CC_R28
	swi	r29, r11, CC_R29
	swi	r30, r11, CC_R30
	/* special purpose registers */
	mfs	r12, rmsr
	swi	r12, r11, CC_MSR
	mfs	r12, rear
	swi	r12, r11, CC_EAR
	mfs	r12, resr
	swi	r12, r11, CC_ESR
	mfs	r12, rfsr
	swi	r12, r11, CC_FSR

	/* update r31, the current */
	lwi	r31, r6, TI_TASK
	swi	r31, r0, 0x64

	/* get new process' cpu context and restore */
	addik	r11, r6, TI_CPU_CONTEXT

	/* special purpose registers */
	lwi	r12, r11, CC_FSR
	mts	rfsr, r12
	lwi	r12, r11, CC_ESR
	mts	resr, r12
	lwi	r12, r11, CC_EAR
	mts	rear, r12
	lwi	r12, r11, CC_MSR
	mts	rmsr, r12
	/* non-volatile registers */
	lwi	r30, r11, CC_R30
	lwi	r29, r11, CC_R29
	lwi	r28, r11, CC_R28
	lwi	r27, r11, CC_R27
	lwi	r26, r11, CC_R26
	lwi	r25, r11, CC_R25
	lwi	r24, r11, CC_R24
	lwi	r23, r11, CC_R23
	lwi	r22, r11, CC_R22
	lwi	r21, r11, CC_R21
	lwi	r20, r11, CC_R20
	lwi	r19, r11, CC_R19
	/* dedicated registers */
	lwi	r12, r11, CC_R18
	lwi	r17, r11, CC_R17
	lwi	r16, r11, CC_R16
	lwi	r15, r11, CC_R15
	lwi	r14, r11, CC_R14
	lwi	r13, r11, CC_R13
	/* skip volatile registers */
	lwi	r2, r11, CC_R2
	lwi	r1, r11, CC_SP

	rtsd	r15, 8
	nop


ENTRY(ret_from_fork)
	addk	r5, r0, r3
	addk	r6, r0, r1
	brlid	r15, schedule_tail
	nop
	swi	r31, r1, PT_R31		/* save r31 in user context. */
	                                /* will soon be restored to r31 in ret_to_user */
	addk	r3, r0, r0
	brid	ret_to_user
	nop


work_pending:
	andi	r11, r19, _TIF_NEED_RESCHED
	beqi	r11, 1f
	bralid	r15, schedule
	nop
1:	andi	r11, r19, _TIF_SIGPENDING
	beqi	r11, no_work_pending
	addk	r5, r1, r0
	bralid	r15, do_signal
	addk	r6, r0, r0
	bri	no_work_pending

ENTRY(ret_to_user)
	disable_irq

	swi	r4, r1, PT_R4		/* retrun val */
	swi	r3, r1, PT_R3		/* retrun val */

	lwi	r6, r31, TS_THREAD_INFO	/* get thread info */
	lwi	r19, r6, TI_FLAGS	/* get flags in thread info */
	bnei	r19, work_pending	/* do an extra work if any bits are set */
no_work_pending:
	/* save r31 */
	swi	r31, r0, 0x64
	/* save mode indicator */
	lwi	r18, r1, PT_MODE
	swi	r18, r0, 0x68
//restore_context:
	/* special purpose registers */
	lwi	r18, r1, PT_FSR
	mts	rfsr, r18
	lwi	r18, r1, PT_ESR
	mts	resr, r18
	lwi	r18, r1, PT_EAR
	mts	rear, r18
	lwi	r18, r1, PT_MSR
	mts	rmsr, r18

	lwi	r31, r1, PT_R31
	lwi	r30, r1, PT_R30
	lwi	r29, r1, PT_R29
	lwi	r28, r1, PT_R28
	lwi	r27, r1, PT_R27
	lwi	r26, r1, PT_R26
	lwi	r25, r1, PT_R25
	lwi	r24, r1, PT_R24
	lwi	r23, r1, PT_R23
	lwi	r22, r1, PT_R22
	lwi	r21, r1, PT_R21
	lwi	r20, r1, PT_R20
	lwi	r19, r1, PT_R19
	lwi	r18, r1, PT_R18
	lwi	r17, r1, PT_R17
	lwi	r16, r1, PT_R16
	lwi	r15, r1, PT_R15
	lwi	r14, r1, PT_PC
	lwi	r13, r1, PT_R13
	lwi	r12, r1, PT_R12
	lwi	r11, r1, PT_R11
	lwi	r10, r1, PT_R10
	lwi	r9, r1, PT_R9
	lwi	r8, r1, PT_R8
	lwi	r7, r1, PT_R7
	lwi	r6, r1, PT_R6
	lwi	r5, r1, PT_R5
	lwi	r4, r1, PT_R4		/* retrun val */
	lwi	r3, r1, PT_R3		/* retrun val */
	lwi	r2, r1, PT_R2
	lwi	r1, r1, PT_SP

	rtid	r14, 0
	nop


sys_vfork_wrapper:
	brid	sys_vfork
	addk	r5, r1, r0

sys_clone_wrapper:
	brid	sys_clone
	addk	r7, r1, r0
	
sys_execve_wrapper:
	swi	r1, r0, 0x70
	brid	sys_execve
	addk	r8, r1, r0

sys_sigreturn_wrapper:
	brid	sys_sigreturn
	addk	r5, r1, r0

sys_rt_sigreturn_wrapper:
	brid	sys_rt_sigreturn
	addk	r5, r1, r0


	/* Interrupt vector table */
	.section	.init.ivt, "ax"
	.org 0x0
	brai	_reset
	brai	_user_exception
	brai	_interrupt
	brai	_break
	brai	_hardware_exception




.section .rodata,"a"
#include "syscall_table.S"

syscall_table_size=(.-sys_call_table)
