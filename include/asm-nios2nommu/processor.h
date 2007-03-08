/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2001  Ken Hill (khill@microtronix.com)    
 *                     Vic Phillips (vic@microtronix.com)
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * hacked from:
 *      include/asm-sparc/processor.h
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
 * Nov/02/2003      dgt     Fix task_size
 *
 ---------------------------------------------------------------------*/

#ifndef __ASM_NIOS_PROCESSOR_H
#define __ASM_NIOS_PROCESSOR_H

#define NIOS2_FLAG_KTHREAD	0x00000001	/* task is a kernel thread */
#define NIOS2_FLAG_COPROC	0x00000002	/* Thread used coprocess */
#define NIOS2_FLAG_DEBUG	0x00000004	/* task is being debugged */

#define NIOS2_OP_NOP 0x1883a
#define NIOS2_OP_BREAK	0x3da03a

#ifndef __ASSEMBLY__

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <linux/a.out.h>
#include <linux/string.h>

#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/segment.h>
#include <asm/current.h>
#include <asm/system.h> /* for get_hi_limit */

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * The nios has no problems with write protection
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/* Whee, this is STACK_TOP and the lowest kernel address too... */
#if 0
#define KERNBASE        0x00000000  /* First address the kernel will eventually be */
#define TASK_SIZE	(KERNBASE)
#define MAX_USER_ADDR	TASK_SIZE
#define MMAP_SEARCH_START (TASK_SIZE/3)
#endif

#define TASK_SIZE	((unsigned int) nasys_program_mem_end)   //...this is better...

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's. We won't be using it
 */
#define TASK_UNMAPPED_BASE	0

/* The Nios processor specific thread struct. */
struct thread_struct {
	struct pt_regs *kregs;

	/* For signal handling */
	unsigned long sig_address;
	unsigned long sig_desc;

	/* Context switch saved kernel state. */
	unsigned long ksp;
	unsigned long kpsr;
	unsigned long kesr;

	/* Flags are defined below */

	unsigned long flags;
	int current_ds;
	struct exec core_exec;     /* just what it says. */
};

#define INIT_MMAP { &init_mm, (0), (0), \
		    __pgprot(0x0) , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_THREAD  { \
	.kregs		= 0,			\
	.sig_address	= 0,			\
	.sig_desc	= 0,			\
	.ksp		= 0,			\
	.kpsr		= 0,			\
	.kesr		= PS_S,			\
	.flags		= NIOS2_FLAG_KTHREAD,	\
	.current_ds	= __KERNEL_DS,		\
	.core_exec	= INIT_EXEC		\
}

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

extern unsigned long thread_saved_pc(struct task_struct *t);

extern void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.kregs->ea)
#define KSTK_ESP(tsk)  ((tsk)->thread.kregs->sp)

#ifdef __KERNEL__
/* Allocation and freeing of basic task resources. */

//;dgt2;#define alloc_task_struct() ((struct task_struct *) xx..see..linux..fork..xx __get_free_pages(GFP_KERNEL,1))
//;dgt2;#define get_task_struct(tsk) xx..see..linux..sched.h...atomic_inc(&mem_map[MAP_NR(tsk)].count)

#endif

#define cpu_relax()    do { } while (0)
#endif /* __ASSEMBLY__ */
#endif /* __ASM_NIOS_PROCESSOR_H */
