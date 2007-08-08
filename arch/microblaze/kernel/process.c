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
	unsigned long *p;
	int i;
	printk("pc:\t0x%08lx\tsp:\t0x%08lx\n", regs->pc, regs->r1);
	printk("flags:\t0x%08lx\tear:\t0x%08lx\tesr:\t0x%08lx\tfsr:\t0x%08lx\n",
		regs->msr, regs->ear, regs->esr, regs->fsr);
	printk("r0:\t0x%08lx\tr1:\t0x%08lx\tr2:\t0x%08lx\tr3\t0x%08lx\n",
		0L, regs->r1, regs->r2, regs->r3);
	for(i=4,p=&(regs->r4);i<32;i+=4,p+=4) {
		printk("r%i:\t0x%08lx\tr%i:\t0x%08lx\tr%i:\t0x%08lx\tr%i:\t0x%08lx\n",
			i,*p, i+1,*(p+1),i+2,*(p+2),i+3,*(p+3));
	}
	printk("\n");
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
		childregs->r1 = usp;
	else
		childregs->r1 = ((unsigned long) ti) + THREAD_SIZE;

	memset(&ti->cpu_context, 0, sizeof(struct cpu_context));
	ti->cpu_context.sp  = (unsigned long)childregs;
	ti->cpu_context.msr = (unsigned long)childregs->msr;
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
	struct cpu_context *ctx=&(((struct thread_info *)(tsk->stack))->cpu_context);

	/* Check whether the thread is blocked in resume() */
	if (in_sched_functions(ctx->r15))
		return ((unsigned long)ctx->r15);
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
	local_save_flags(regs.msr);
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
