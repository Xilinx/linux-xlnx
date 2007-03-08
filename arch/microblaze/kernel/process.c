/*
 * arch/microblaze/kernel/process.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/module.h>
#include <linux/sched.h>

/* FIXME */
void show_regs(struct pt_regs *regs)
{
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void cpu_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	while (1) {
		while (!need_resched()) {
			cpu_relax();
		}
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void flush_thread(void)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
                struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct thread_info *ti = task_thread_info(p);
	extern void ret_from_fork(void);

	*childregs = *regs;

	if (user_mode(regs))
		childregs->sp = usp;
	else
		childregs->sp = ((unsigned long) ti) + THREAD_SIZE;

	memset(&ti->cpu_context, 0, sizeof(struct cpu_context));
	ti->cpu_context.sp  = (unsigned long)childregs;
	ti->cpu_context.r15 = (unsigned long)ret_from_fork - 8;

	if (clone_flags & CLONE_SETTLS)
		;/* FIXME: not sure what to do */

	return 0;
}

/*
 * Return saved PC of a blocked thread.
 * FIXME this needs to be checked
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct cpu_context *ctx=&(tsk->thread_info->cpu_context);

	/* Check whether the thread is blocked in resume() */
	if (in_sched_functions(ctx->r15))
		return ((unsigned long *)ctx->r15);
	else
		return ctx->r14;
}

static void kernel_thread_helper(int (*fn)(void *), void *arg)
{
	fn(arg);
	do_exit(-1);
}

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;
	int ret;

	memset(&regs, 0, sizeof(regs));
	/* store them in non-volatile registers */
	regs.r5 = (unsigned long)fn;
	regs.r6 = (unsigned long)arg;
	regs.pc = (unsigned long)kernel_thread_helper;
	regs.kernel_mode = 1;

	ret = do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);

	return ret;
}

unsigned long get_wchan(struct task_struct *p)
{
/* TBD (used by procfs) */
	return 0;
}
