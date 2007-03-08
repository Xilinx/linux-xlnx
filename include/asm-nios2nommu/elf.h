#ifndef __NIOS2_ELF_H
#define __NIOS2_ELF_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/elf.h
 *
 * Nio2 ELF relocation types
 *
 * Derived from M68knommu
 *
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Jan/20/2004		dgt	    NiosII
 * Mar/18/2004		xwt		NiosII relocation types added
 *
 ---------------------------------------------------------------------*/

#include <asm/ptrace.h>
#include <asm/user.h>

#define R_NIOS2_NONE			0
#define R_NIOS2_S16				1
#define R_NIOS2_U16				2
#define R_NIOS2_PCREL16			3
#define R_NIOS2_CALL26			4
#define R_NIOS2_IMM5			5
#define R_NIOS2_CACHE_OPX 		6
#define R_NIOS2_IMM6			7
#define R_NIOS2_IMM8			8
#define R_NIOS2_HI16			9
#define R_NIOS2_LO16			10
#define R_NIOS2_HIADJ16 		11
#define R_NIOS2_BFD_RELOC_32	12
#define R_NIOS2_BFD_RELOC_16	13
#define R_NIOS2_BFD_RELOC_8 	14
#define R_NIOS2_GPREL			15
#define R_NIOS2_GNU_VTINHERIT 	16
#define R_NIOS2_GNU_VTENTRY  	17
#define R_NIOS2_UJMP			18
#define R_NIOS2_CJMP			19
#define R_NIOS2_CALLR			20
#define R_NIOS2_ALIGN			21
/* Keep this the last entry.  */
#define R_NIOS2_NUM				22

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef unsigned long elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_ALTERA_NIOS2)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_ALTERA_NIOS2

#define ELF_PLAT_INIT(_r, load_addr)	_r->a1 = 0

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         0xD0000000UL

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different) */

#define ELF_CORE_COPY_REGS(pr_reg, regs)				\
	/* Bleech. */							\
	pr_reg[0] = regs->r1;						\
	pr_reg[1] = regs->r2;						\
	pr_reg[2] = regs->r3;						\
	pr_reg[3] = regs->r4;						\
	pr_reg[4] = regs->r5;						\
	pr_reg[5] = regs->r6;						\
	pr_reg[6] = regs->r7;						\
	pr_reg[7] = regs->r8;						\
	pr_reg[8] = regs->r9;						\
	pr_reg[9] = regs->r10;						\
	pr_reg[10] = regs->r11;						\
	pr_reg[11] = regs->r12;						\
	pr_reg[12] = regs->r13;						\
	pr_reg[13] = regs->r14;						\
	pr_reg[14] = regs->r15;						\
	pr_reg[23] = regs->sp;						\
	pr_reg[26] = regs->estatus;					\
	{								\
	  struct switch_stack *sw = ((struct switch_stack *)regs) - 1;	\
	  pr_reg[15] = sw->r16;						\
	  pr_reg[16] = sw->r17;						\
	  pr_reg[17] = sw->r18;						\
	  pr_reg[18] = sw->r19;						\
	  pr_reg[19] = sw->r20;						\
	  pr_reg[20] = sw->r21;						\
	  pr_reg[21] = sw->r22;						\
	  pr_reg[22] = sw->r23;						\
	  pr_reg[24] = sw->fp;						\
	  pr_reg[25] = sw->gp;						\
	}

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM  (NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)
#endif

#endif
