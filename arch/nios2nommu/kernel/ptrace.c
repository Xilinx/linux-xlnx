/*
 *  linux/arch/m68knommu/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in the SR the user has access to. */
/* 1 = access 0 = no access */
#define SR_MASK 0x00000000

/* Find the stack offset for a register, relative to thread.ksp. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static int regoff[] = {
	-1, PT_REG(r1), PT_REG(r2), PT_REG(r3), PT_REG(r4),
	PT_REG(r5), PT_REG(r6), PT_REG(r7), PT_REG(r8),
	PT_REG(r9), PT_REG(r10), PT_REG(r11), PT_REG(r12),
	PT_REG(r13), PT_REG(r14), PT_REG(r15), SW_REG(r16),
	SW_REG(r17), SW_REG(r18), SW_REG(r19), SW_REG(r20),
	SW_REG(r21), SW_REG(r22), SW_REG(r23), -1, -1,
	PT_REG(gp), PT_REG(sp), -1, -1, PT_REG(ra), -1,
	PT_REG(estatus), -1, -1, -1
};

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	unsigned long *addr;

	if (regno == PTR_R0)
		return 0;
	else if (regno == PTR_BA)
		return 0;
	else if (regno == PTR_STATUS)
		return 0;
	else if (regno == PTR_IENABLE)
		return 0;
	else if (regno == PTR_IPENDING)
		return 0;
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
		addr = (unsigned long *)(task->thread.kregs + regoff[regno]);
	else
		return 0;
	return *addr;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	unsigned long *addr;

	if (regno == PTR_R0)
		return -1;
	else if (regno == PTR_BA)
		return -1;
	else if (regno == PTR_STATUS)
		return -1;
	else if (regno == PTR_IENABLE)
		return -1;
	else if (regno == PTR_IPENDING)
		return -1;
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
		addr = (unsigned long *) (task->thread.kregs + regoff[regno]);
	else
		return -1;
	*addr = data;
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Nothing special to do here, no processor debug support.
 */
void ptrace_disable(struct task_struct *child)
{
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	switch (request) {
		/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int copied;

			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				break;
			ret = put_user(tmp,(unsigned long *) data);
			break;
		}

		/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			ret = -EIO;
			if ((addr & 3) || addr < 0 ||
			    addr > sizeof(struct user) - 3)
				break;
			
			tmp = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */
			ret = -EIO;
			if (addr < 19) {
				tmp = get_reg(child, addr);
#if 0 // No FPU stuff
			} else if (addr >= 21 && addr < 49) {
				tmp = child->thread.fp[addr - 21];
#ifdef CONFIG_M68KFPU_EMU
				/* Convert internal fpu reg representation
				 * into long double format
				 */
				if (FPU_IS_EMU && (addr < 45) && !(addr % 3))
					tmp = ((tmp & 0xffff0000) << 15) |
					      ((tmp & 0x0000ffff) << 16);
#endif
#endif
			} else if (addr == 49) {
				tmp = child->mm->start_code;
			} else if (addr == 50) {
				tmp = child->mm->start_data;
			} else if (addr == 51) {
				tmp = child->mm->end_code;
			} else
				break;
			ret = put_user(tmp,(unsigned long *) data);
			break;
		}

		/* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = 0;
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				break;
			ret = -EIO;
			break;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 ||
			    addr > sizeof(struct user) - 3)
				break;

			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PTR_ESTATUS) {
				data &= SR_MASK;
				data |= get_reg(child, PTR_ESTATUS) & ~(SR_MASK);
			}
			if (addr < 19) {
				if (put_reg(child, addr, data))
					break;
				ret = 0;
				break;
			}
#if 0 // No FPU stuff
			if (addr >= 21 && addr < 48)
			{
#ifdef CONFIG_M68KFPU_EMU
				/* Convert long double format
				 * into internal fpu reg representation
				 */
				if (FPU_IS_EMU && (addr < 45) && !(addr % 3)) {
					data = (unsigned long)data << 15;
					data = (data & 0xffff0000) |
					       ((data & 0x0000ffff) >> 1);
				}
#endif
				child->thread.fp[addr - 21] = data;
				ret = 0;
			}
#endif
			break;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			if (request == PTRACE_SYSCALL)
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			else
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			wake_up_process(child);
			ret = 0;
			break;
		}

		/*
		 * make the child exit.  Best I can do is send it a sigkill. 
		 * perhaps it should be put in the status that it wants to 
		 * exit.
		 */
		case PTRACE_KILL: {

			ret = 0;
			if (child->state == EXIT_ZOMBIE) /* already dead */
				break;
			child->exit_code = SIGKILL;
			wake_up_process(child);
			break;
		}

		/*
		 * Single stepping requires placing break instructions in
		 * the code to break back. If you are stepping through a 
		 * conditional branch you need to decode the test and put
		 * the break in the correct location.
		 */
		case PTRACE_SINGLESTEP: {  /* set the trap flag. */

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

			child->exit_code = data;
			/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			break;
		}

		case PTRACE_DETACH:	/* detach a process that was attached. */
			ret = ptrace_detach(child, data);
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    tmp = get_reg(child, i);
			    if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    if (i == PTR_ESTATUS) {
				tmp &= SR_MASK;
				tmp |= get_reg(child, PTR_ESTATUS) & ~(SR_MASK);
			    }
			    put_reg(child, i, tmp);
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

#ifdef PTRACE_GETFPREGS
		case PTRACE_GETFPREGS: { /* Get the child FPU state. */
			ret = 0;
			if (copy_to_user((void *)data, &child->thread.fp,
					 sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			break;
		}
#endif

#ifdef PTRACE_SETFPREGS
		case PTRACE_SETFPREGS: { /* Set the child FPU state. */
			ret = 0;
			if (copy_from_user(&child->thread.fp, (void *)data,
					   sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			break;
		}
#endif

		default:
			ret = -EIO;
			break;
	}
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
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
