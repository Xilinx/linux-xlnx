#ifndef _NIOSKNOMMU_UCONTEXT_H
#define _NIOSKNOMMU_UCONTEXT_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/ucontext.h
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
 *
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


typedef int greg_t;
#define NGREG 32
typedef greg_t gregset_t[NGREG];

#ifdef CONFIG_FPU
typedef struct fpregset {
	int f_pcr;
	int f_psr;
	int f_fpiaddr;
	int f_fpregs[8][3];
} fpregset_t;
#endif

struct mcontext {
	int version;
	int status_extension;
	gregset_t gregs;
#ifdef CONFIG_FPU
	fpregset_t fpregs;
#endif
};

#define MCONTEXT_VERSION 2

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct mcontext	  uc_mcontext;
#ifdef CONFIG_FPU
	unsigned long	  uc_filler[80];
#endif
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif
