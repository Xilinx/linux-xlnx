/*
 * include/asm-microblaze/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#ifndef __ASSEMBLY__
#include <asm/types.h>

struct pt_regs {
	unsigned long msr;
	unsigned long ear;
	unsigned long esr;
	unsigned long fsr;
	unsigned long pc;
	unsigned long sp;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long r16;
	unsigned long r17;
	unsigned long r18;
	unsigned long r19;
	unsigned long r20;
	unsigned long r21;
	unsigned long r22;
	unsigned long r23;
	unsigned long r24;
	unsigned long r25;
	unsigned long r26;
	unsigned long r27;
	unsigned long r28;
	unsigned long r29;
	unsigned long r30;
	unsigned long r31;
	int kernel_mode;
};

#define kernel_mode(regs)		((regs)->kernel_mode)
#define user_mode(regs)			(!kernel_mode(regs))

#define instruction_pointer(regs)	((regs)->pc)
#define profile_pc(regs)		instruction_pointer(regs)

#endif /*  __ASSEMBLY__ */

#endif /* _ASM_PTRACE_H */
