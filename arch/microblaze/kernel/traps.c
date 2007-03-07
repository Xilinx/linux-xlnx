/*
 * arch/microblaze/kernel/traps.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>

/* FIXME */
void trap_init(void)
{
}

void __bad_xchg(volatile void *ptr, int size)
{
	printk("xchg: bad data size: pc 0x%p, ptr 0x%p, size %d\n",
		__builtin_return_address(0), ptr, size);
	BUG();
}
EXPORT_SYMBOL(__bad_xchg);

static int kstack_depth_to_print = 24;

void show_trace(struct task_struct *task, unsigned long *stack)
{
	unsigned long addr;

	if (!stack)
		stack = (unsigned long*)&stack;

	printk("Call Trace: ");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	while (!kstack_end(stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
			printk("[<%08lx>] ", addr);
			print_symbol("%s\n", addr);
		}
	}
	printk("\n");
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long *stack;
	int i;

	if (sp == NULL) {
		if (task)
			sp = (unsigned long *)task->thread_info->cpu_context.sp;
		else
			sp = (unsigned long *)&sp;
	}

	stack = sp;

	printk("\nStack:\n  ");

	for(i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(sp))
			break;
		if (i && ((i % 8) == 0))
			printk("\n  ");
		printk("%08lx ", *sp++);
	}
	printk("\n");
	show_trace(task, stack);
}

void dump_stack(void)
{
	show_stack(NULL, NULL);
}

EXPORT_SYMBOL(dump_stack);
