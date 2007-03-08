/* thread_info.h: niosnommu low-level thread information
 * adapted from the m68knommu
 *
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 * Copyright (C) 2002 Microtronix Datacom 
 *
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
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

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <asm/page.h>

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/*
 * low level task data.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long	   flags;		/* low level flags */
	int		   cpu;			/* cpu we're on */
	int		   preempt_count;	/* 0 => preemptable, <0 => BUG*/
	struct restart_block restart_block;
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)


/* how to get the thread information struct from C
   usable only in supervisor mode */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__ __volatile__(
		"mov	%0, sp\n"
		"and	%0, %0, %1\n"
		: "=&r"(ti)
		: "r" (~(THREAD_SIZE-1))
		);
	return ti;
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) \
				__get_free_pages(GFP_KERNEL, 1))
#define free_thread_info(ti)	free_pages((unsigned long) (ti), 1)
#define put_thread_info(ti)	put_task_struct((ti)->task)

#define	PREEMPT_ACTIVE	0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE 	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME 	1	/* resumption notification requested */
#define TIF_SIGPENDING	  	2	/* signal pending */
#define TIF_NEED_RESCHED  	3	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	4	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_MEMDIE		5

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */

#else /* __ASSEMBLY__ */

/* how to get the thread information struct from ASM 
   usable only in supervisor mode */
.macro GET_THREAD_INFO reg 
.if THREAD_SIZE & 0xffff0000
	andhi	\reg, sp, %hi(~(THREAD_SIZE-1))
.else
	addi	\reg, r0, %lo(~(THREAD_SIZE-1))
	and	\reg, \reg, sp
.endif
.endm

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
