/*
 * include/asm-microblaze/thread_info.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 * FIXME -- need review
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

/* we have 8k stack */
#define THREAD_SHIFT		13
#define THREAD_SIZE		(1 << THREAD_SHIFT)

#ifndef __ASSEMBLY__
# include <asm/processor.h>
# include <asm/segment.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */

struct cpu_context {
	__u32	sp;
	__u32	r2;
	/* dedicated registers */
	__u32	r13;
	__u32	r14;
	__u32	r15;
	__u32	r16;
	__u32	r17;
	__u32	r18;
	/* non-volatile registers */
	__u32	r19;
	__u32	r20;
	__u32	r21;
	__u32	r22;
	__u32	r23;
	__u32	r24;
	__u32	r25;
	__u32	r26;
	__u32	r27;
	__u32	r28;
	__u32	r29;
	__u32	r30;
	/* special purpose registers */
	__u32	msr;
	__u32	ear;
	__u32	esr;
	__u32	fsr;
};

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	__s32			preempt_count;	/* 0 => preemptable,< 0 => BUG*/
	mm_segment_t		addr_limit;	/* thread address space */
	struct restart_block    restart_block;

	struct cpu_context	cpu_context;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.addr_limit	= KERNEL_DS,		\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	register unsigned long sp asm("r1");

	return (struct thread_info *)(sp & ~(THREAD_SIZE-1));
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) __get_free_pages(GFP_KERNEL,1))
#define free_thread_info(ti) free_pages((unsigned long) (ti), 1)

#endif /* __ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x10000000

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* restore singlestep on return to user mode */
#define TIF_IRET		5	/* return with iret */
#define TIF_MEMDIE		6
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_IRET		(1<<TIF_IRET)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */
#define _TIF_ALLWORK_MASK	0x0000FFFF	/* work to do on any return to u-space */

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */

#endif	/* __KERNEL__ */
#endif	/* _ASM_THREAD_INFO_H */
