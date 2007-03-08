/*
 * arch/niosnommu/kernel/traps.c
 *
 * Copyright 2004 Microtronix Datacom Ltd.
 * Copyright 2001 Vic Phillips
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * hacked from:
 *
 * arch/sparcnommu/kernel/traps.c
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

#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/module.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>

#include <asm/nios.h>

/* #define TRAP_DEBUG */

#if 0
void dumpit(unsigned long l1, unsigned long l2)
{
	printk("0x%08x l1 0x%08x l2\n");
	while(1);
}

struct trap_trace_entry {
	unsigned long pc;
	unsigned long type;
};

int trap_curbuf = 0;
struct trap_trace_entry trapbuf[1024];

void syscall_trace_entry(struct pt_regs *regs)
{
	printk("%s[%d]: ", current->comm, current->pid);
	printk("scall<%d> (could be %d)\n", (int) regs->r3,
	       (int) regs->r4);
}

void syscall_trace_exit(struct pt_regs *regs)
{
}
#endif

/*
 * The architecture-independent backtrace generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_stack(current, &stack);
}

EXPORT_SYMBOL(dump_stack);

/*
 * The show_stack is an external API which we do not use ourselves.
 * The oops is printed in die_if_kernel.
 */

int kstack_depth_to_print = 48;

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *endstack, addr;
	extern char _start, _etext;
	int i;

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.ksp;
		else
			stack = (unsigned long *)&stack;
	}

	addr = (unsigned long) stack;
	endstack = (unsigned long *) PAGE_ALIGN(addr);

	printk(KERN_EMERG "Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i % 8 == 0)
			printk(KERN_EMERG "\n       ");
		printk(KERN_EMERG " %08lx", *stack++);
	}

	printk(KERN_EMERG "\nCall Trace:");
	i = 0;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (((addr >= (unsigned long) &_start) &&
		     (addr <= (unsigned long) &_etext))) {
			if (i % 4 == 0)
				printk(KERN_EMERG "\n       ");
			printk(KERN_EMERG " [<%08lx>]", addr);
			i++;
		}
	}
	printk(KERN_EMERG "\n");
}

void die_if_kernel(char *str, struct pt_regs *pregs)
{
	unsigned long pc;

	pc = pregs->ra;
	printk("0x%08lx\n trapped to die_if_kernel\n",pregs->ra);
	show_regs(pregs);
	if(pregs->status_extension & PS_S)
		do_exit(SIGKILL);
	do_exit(SIGSEGV);
}

void do_hw_interrupt(unsigned long type, unsigned long psr, unsigned long pc)
{
	if(type < 0x10) {
		printk("Unimplemented Nios2 TRAP, type = %02lx\n", type);
		die_if_kernel("Whee... Hello Mr. Penguin", current->thread.kregs);
	}	
}

#if 0
void handle_watchpoint(struct pt_regs *regs, unsigned long pc, unsigned long psr)
{
#ifdef TRAP_DEBUG
	printk("Watchpoint detected at PC %08lx PSR %08lx\n", pc, psr);
#endif
	if(psr & PSR_SUPERVISOR)
		panic("Tell me what a watchpoint trap is, and I'll then deal "
		      "with such a beast...");
}
#endif

void trap_init(void)
{
#ifdef DEBUG
	printk("trap_init reached\n");
#endif
}
