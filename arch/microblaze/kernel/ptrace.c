/*
 * arch/microblaze/kernel/ptrace.c -- `ptrace' system call
 *
 *  Copyright (C) 2007     PetaLogix
 *  Copyright (C) 2004-07  John Williams <john.williams@petalogix.com>
 *
 * derived from arch/v850/kernel/ptrace.c
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * Derived from arch/mips/kernel/ptrace.c:
 *
 *  Copyright (C) 1992 Ross Biro
 *  Copyright (C) Linus Torvalds
 *  Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 *  Copyright (C) 1996 David S. Miller
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 1999 MIPS Technologies, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>
#include <linux/signal.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/asm-offsets.h>

#if 0
#define DBPRINTK(...) printk(__VA_ARGS__)
#else
#define DBPRINTK(...)
#endif

/* Returns the address where the register at REG_OFFS in P is stashed away.  */
static microblaze_reg_t *reg_save_addr (unsigned reg_offs, struct task_struct *t)
{
	struct pt_regs *regs;

	/* Three basic cases:

	   (1) A register normally saved before calling the scheduler, is
	       available in the kernel entry pt_regs structure at the top
	       of the kernel stack.  The kernel trap/irq exit path takes
	       care to save/restore almost all registers for ptrace'd
	       processes.

	   (2) A call-clobbered register, where the process P entered the
	       kernel via [syscall] trap, is not stored anywhere; that's
	       OK, because such registers are not expected to be preserved
	       when the trap returns anyway (so we don't actually bother to
	       test for this case).

	   (3) A few registers not used at all by the kernel, and so
	       normally never saved except by context-switches, are in the
	       context switch state.  */

	/* Register saved during kernel entry (or not available).  */
	regs = task_pt_regs (t);

	return (microblaze_reg_t *)((char *)regs + reg_offs);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int rval;

	switch (request) {
		unsigned long val, copied;

	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		DBPRINTK("PEEKTEXT/PEEKDATA at %08lX\n",addr); 
		copied = access_process_vm(child, addr, &val, sizeof(val), 0);
		rval = -EIO;
		if (copied != sizeof(val))
			break;
		rval = put_user(val, (unsigned long *)data);
		goto out;

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		DBPRINTK("POKETEXT/POKEDATA to %08lX\n",addr);
		rval = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    == sizeof(data))
			break;
		rval = -EIO;
		goto out;

	/* Read/write the word at location ADDR in the registers.  */
	case PTRACE_PEEKUSR:
	case PTRACE_POKEUSR:
		DBPRINTK("PEEKUSR/POKEUSR : 0x%08lx\n",addr);
		rval = 0;
		if (addr >= PT_SIZE && request == PTRACE_PEEKUSR) {
			/* Special requests that don't actually correspond
			   to offsets in struct pt_regs.  */
			if (addr == PT_TEXT_ADDR)
			{
				val = child->mm->start_code;
			}
			else if (addr == PT_DATA_ADDR)
			{
				val = child->mm->start_data;
			}
			else if (addr == PT_TEXT_LEN)
			{
				val = child->mm->end_code
					- child->mm->start_code;
			}
			else
			{
				rval = -EIO;
			}
		} else if (addr >= 0 && addr < PT_SIZE && (addr & 0x3) == 0) {
			microblaze_reg_t *reg_addr = reg_save_addr(addr, child);
			if (request == PTRACE_PEEKUSR)
			{
				val = *reg_addr;
			}
			else
			{
				*reg_addr = data;
			}
		} else
			rval = -EIO;

		if (rval == 0 && request == PTRACE_PEEKUSR)
		{
			rval = put_user (val, (unsigned long *)data);
		}
		goto out;

	/* Continue and stop at next (return from) syscall */
	case PTRACE_SYSCALL:
		DBPRINTK("PTRACE_SYSCALL\n");
	case PTRACE_SINGLESTEP:
		DBPRINTK("PTRACE_SINGLESTEP\n");
	/* Restart after a signal.  */
	case PTRACE_CONT:
		DBPRINTK("PTRACE_CONT\n"); 
		rval = -EIO;
		if (!valid_signal(data))
			break;

		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

		child->exit_code = data;
		DBPRINTK("wakeup_process\n");
		wake_up_process(child);
		rval = 0;
		break;

	/*
	 * make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		DBPRINTK("PTRACE_KILL\n"); 
		rval = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;

	case PTRACE_DETACH: /* detach a process that was attached. */
		DBPRINTK("PTRACE_DETACH\n"); 
		rval = ptrace_detach(child, data);
		break;

	default:
		rval = -EIO;
		goto out;
	}
 out:
	return rval;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

void ptrace_disable (struct task_struct *child)
{
	/* nothing to do */
}
