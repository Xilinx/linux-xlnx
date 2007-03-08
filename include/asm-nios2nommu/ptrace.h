/*
 * Taken from the m68k port.
 *
 * Copyright (C) 2004, Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef _NIOS2NOMMU_PTRACE_H
#define _NIOS2NOMMU_PTRACE_H

#ifndef __ASSEMBLY__

#define PTR_R0		0
#define PTR_R1		1
#define PTR_R2		2
#define PTR_R3		3
#define PTR_R4		4
#define PTR_R5		5
#define PTR_R6		6
#define PTR_R7		7
#define PTR_R8		8
#define PTR_R9		9
#define PTR_R10		10
#define PTR_R11		11
#define PTR_R12		12
#define PTR_R13		13
#define PTR_R14		14
#define PTR_R15		15
#define PTR_R16		16
#define PTR_R17		17
#define PTR_R18		18
#define PTR_R19		19
#define PTR_R20		20
#define PTR_R21		21
#define PTR_R22		22
#define PTR_R23		23
#define PTR_R24		24
#define PTR_R25		25
#define PTR_GP		26
#define PTR_SP		27
#define PTR_FP		28
#define PTR_EA		29
#define PTR_BA		30
#define PTR_RA		31
#define PTR_STATUS	32
#define PTR_ESTATUS	33
#define PTR_BSTATUS	34
#define PTR_IENABLE	35
#define PTR_IPENDING	36

/* this struct defines the way the registers are stored on the
   stack during a system call. 

   There is a fake_regs in setup.c that has to match pt_regs.*/

struct pt_regs {
	unsigned long  r8;
	unsigned long  r9;
	unsigned long  r10;
	unsigned long  r11;
	unsigned long  r12;
	unsigned long  r13;
	unsigned long  r14;
	unsigned long  r15;
	unsigned long  r1;
	unsigned long  r2;
	unsigned long  r3;
	unsigned long  r4;
	unsigned long  r5;
	unsigned long  r6;
	unsigned long  r7;
	unsigned long  orig_r2;
	unsigned long  ra;
	unsigned long  fp;
	unsigned long  sp;
	unsigned long  gp;
	unsigned long  estatus;
	unsigned long  status_extension;
	unsigned long  ea;
};


/*
 * This is the extended stack used by signal handlers and the context
 * switcher: it's pushed after the normal "struct pt_regs".
 */
struct switch_stack {
	unsigned long  r16;
	unsigned long  r17;
	unsigned long  r18;
	unsigned long  r19;
	unsigned long  r20;
	unsigned long  r21;
	unsigned long  r22;
	unsigned long  r23;
	unsigned long  fp;
	unsigned long  gp;
	unsigned long  ra;
};

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#ifdef CONFIG_FPU
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#endif

#ifdef __KERNEL__

#ifndef PS_S
#define PS_S  (0x00000001)
#endif
#ifndef PS_T
#define PS_T  (0x00000002)
#endif

#define user_mode(regs) (!((regs)->status_extension & PS_S))
#define instruction_pointer(regs) ((regs)->ra)
#define profile_pc(regs) instruction_pointer(regs)
extern void show_regs(struct pt_regs *);

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _NIOS2NOMMU_PTRACE_H */
