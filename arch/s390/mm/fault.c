/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Ulrich Weigand (uweigand@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/kernel_stat.h>
#include <linux/perf_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/facility.h>
#include "../kernel/entry.h"

#ifndef CONFIG_64BIT
#define __FAIL_ADDR_MASK 0x7ffff000
#define __SUBCODE_MASK 0x0200
#define __PF_RES_FIELD 0ULL
#else /* CONFIG_64BIT */
#define __FAIL_ADDR_MASK -4096L
#define __SUBCODE_MASK 0x0600
#define __PF_RES_FIELD 0x8000000000000000ULL
#endif /* CONFIG_64BIT */

#define VM_FAULT_BADCONTEXT	0x010000
#define VM_FAULT_BADMAP		0x020000
#define VM_FAULT_BADACCESS	0x040000
#define VM_FAULT_SIGNAL		0x080000

static unsigned long store_indication __read_mostly;

#ifdef CONFIG_64BIT
static int __init fault_init(void)
{
	if (test_facility(75))
		store_indication = 0xc00;
	return 0;
}
early_initcall(fault_init);
#endif

static inline int notify_page_fault(struct pt_regs *regs)
{
	int ret = 0;

	/* kprobe_running() needs smp_processor_id() */
	if (kprobes_built_in() && !user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 14))
			ret = 1;
		preempt_enable();
	}
	return ret;
}


/*
 * Unlock any spinlocks which will prevent us from getting the
 * message out.
 */
void bust_spinlocks(int yes)
{
	if (yes) {
		oops_in_progress = 1;
	} else {
		int loglevel_save = console_loglevel;
		console_unblank();
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

/*
 * Returns the address space associated with the fault.
 * Returns 0 for kernel space and 1 for user space.
 */
static inline int user_space_fault(unsigned long trans_exc_code)
{
	/*
	 * The lowest two bits of the translation exception
	 * identification indicate which paging table was used.
	 */
	trans_exc_code &= 3;
	if (trans_exc_code == 2)
		/* Access via secondary space, set_fs setting decides */
		return current->thread.mm_segment.ar4;
	/*
	 * Access via primary space or access register is from user space
	 * and access via home space is from the kernel.
	 */
	return trans_exc_code != 3;
}

static inline void report_user_fault(struct pt_regs *regs, long signr)
{
	if ((task_pid_nr(current) > 1) && !show_unhandled_signals)
		return;
	if (!unhandled_signal(current, signr))
		return;
	if (!printk_ratelimit())
		return;
	printk(KERN_ALERT "User process fault: interruption code 0x%X ",
	       regs->int_code);
	print_vma_addr(KERN_CONT "in ", regs->psw.addr & PSW_ADDR_INSN);
	printk(KERN_CONT "\n");
	printk(KERN_ALERT "failing address: %lX\n",
	       regs->int_parm_long & __FAIL_ADDR_MASK);
	show_regs(regs);
}

/*
 * Send SIGSEGV to task.  This is an external routine
 * to keep the stack usage of do_page_fault small.
 */
static noinline void do_sigsegv(struct pt_regs *regs, int si_code)
{
	struct siginfo si;

	report_user_fault(regs, SIGSEGV);
	si.si_signo = SIGSEGV;
	si.si_code = si_code;
	si.si_addr = (void __user *)(regs->int_parm_long & __FAIL_ADDR_MASK);
	force_sig_info(SIGSEGV, &si, current);
}

static noinline void do_no_context(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;
	unsigned long address;

	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
	if (fixup) {
		regs->psw.addr = extable_fixup(fixup) | PSW_ADDR_AMODE;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	address = regs->int_parm_long & __FAIL_ADDR_MASK;
	if (!user_space_fault(regs->int_parm_long))
		printk(KERN_ALERT "Unable to handle kernel pointer dereference"
		       " at virtual kernel address %p\n", (void *)address);
	else
		printk(KERN_ALERT "Unable to handle kernel paging request"
		       " at virtual user address %p\n", (void *)address);

	die(regs, "Oops");
	do_exit(SIGKILL);
}

static noinline void do_low_address(struct pt_regs *regs)
{
	/* Low-address protection hit in kernel mode means
	   NULL pointer write access in kernel mode.  */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		/* Low-address protection hit in user mode 'cannot happen'. */
		die (regs, "Low-address protection");
		do_exit(SIGKILL);
	}

	do_no_context(regs);
}

static noinline void do_sigbus(struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct siginfo si;

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_code = BUS_ADRERR;
	si.si_addr = (void __user *)(regs->int_parm_long & __FAIL_ADDR_MASK);
	force_sig_info(SIGBUS, &si, tsk);
}

static noinline void do_fault_error(struct pt_regs *regs, int fault)
{
	int si_code;

	switch (fault) {
	case VM_FAULT_BADACCESS:
	case VM_FAULT_BADMAP:
		/* Bad memory access. Check if it is kernel or user space. */
		if (user_mode(regs)) {
			/* User mode accesses just cause a SIGSEGV */
			si_code = (fault == VM_FAULT_BADMAP) ?
				SEGV_MAPERR : SEGV_ACCERR;
			do_sigsegv(regs, si_code);
			return;
		}
	case VM_FAULT_BADCONTEXT:
		do_no_context(regs);
		break;
	case VM_FAULT_SIGNAL:
		if (!user_mode(regs))
			do_no_context(regs);
		break;
	default: /* fault & VM_FAULT_ERROR */
		if (fault & VM_FAULT_OOM) {
			if (!user_mode(regs))
				do_no_context(regs);
			else
				pagefault_out_of_memory();
		} else if (fault & VM_FAULT_SIGBUS) {
			/* Kernel mode? Handle exceptions or die */
			if (!user_mode(regs))
				do_no_context(regs);
			else
				do_sigbus(regs);
		} else
			BUG();
		break;
	}
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * interruption code (int_code):
 *   04       Protection           ->  Write-Protection  (suprression)
 *   10       Segment translation  ->  Not present       (nullification)
 *   11       Page translation     ->  Not present       (nullification)
 *   3b       Region third trans.  ->  Not present       (nullification)
 */
static inline int do_exception(struct pt_regs *regs, int access)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long trans_exc_code;
	unsigned long address;
	unsigned int flags;
	int fault;

	tsk = current;
	/*
	 * The instruction that caused the program check has
	 * been nullified. Don't signal single step via SIGTRAP.
	 */
	clear_tsk_thread_flag(tsk, TIF_PER_TRAP);

	if (notify_page_fault(regs))
		return 0;

	mm = tsk->mm;
	trans_exc_code = regs->int_parm_long;

	/*
	 * Verify that the fault happened in user space, that
	 * we are not in an interrupt and that there is a 
	 * user context.
	 */
	fault = VM_FAULT_BADCONTEXT;
	if (unlikely(!user_space_fault(trans_exc_code) || in_atomic() || !mm))
		goto out;

	address = trans_exc_code & __FAIL_ADDR_MASK;
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
	flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	if (access == VM_WRITE || (trans_exc_code & store_indication) == 0x400)
		flags |= FAULT_FLAG_WRITE;
	down_read(&mm->mmap_sem);

#ifdef CONFIG_PGSTE
	if ((current->flags & PF_VCPU) && S390_lowcore.gmap) {
		address = __gmap_fault(address,
				     (struct gmap *) S390_lowcore.gmap);
		if (address == -EFAULT) {
			fault = VM_FAULT_BADMAP;
			goto out_up;
		}
		if (address == -ENOMEM) {
			fault = VM_FAULT_OOM;
			goto out_up;
		}
	}
#endif

retry:
	fault = VM_FAULT_BADMAP;
	vma = find_vma(mm, address);
	if (!vma)
		goto out_up;

	if (unlikely(vma->vm_start > address)) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out_up;
		if (expand_stack(vma, address))
			goto out_up;
	}

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
	fault = VM_FAULT_BADACCESS;
	if (unlikely(!(vma->vm_flags & access)))
		goto out_up;

	if (is_vm_hugetlb_page(vma))
		address &= HPAGE_MASK;
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, flags);
	/* No reason to continue if interrupted by SIGKILL. */
	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current)) {
		fault = VM_FAULT_SIGNAL;
		goto out;
	}
	if (unlikely(fault & VM_FAULT_ERROR))
		goto out_up;

	/*
	 * Major/minor page fault accounting is only done on the
	 * initial attempt. If we go through a retry, it is extremely
	 * likely that the page will be found in page cache at that point.
	 */
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR) {
			tsk->maj_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
				      regs, address);
		} else {
			tsk->min_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
				      regs, address);
		}
		if (fault & VM_FAULT_RETRY) {
			/* Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation. */
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;
			down_read(&mm->mmap_sem);
			goto retry;
		}
	}
	fault = 0;
out_up:
	up_read(&mm->mmap_sem);
out:
	return fault;
}

void __kprobes do_protection_exception(struct pt_regs *regs)
{
	unsigned long trans_exc_code;
	int fault;

	trans_exc_code = regs->int_parm_long;
	/*
	 * Protection exceptions are suppressing, decrement psw address.
	 * The exception to this rule are aborted transactions, for these
	 * the PSW already points to the correct location.
	 */
	if (!(regs->int_code & 0x200))
		regs->psw.addr = __rewind_psw(regs->psw, regs->int_code >> 16);
	/*
	 * Check for low-address protection.  This needs to be treated
	 * as a special case because the translation exception code
	 * field is not guaranteed to contain valid data in this case.
	 */
	if (unlikely(!(trans_exc_code & 4))) {
		do_low_address(regs);
		return;
	}
	fault = do_exception(regs, VM_WRITE);
	if (unlikely(fault))
		do_fault_error(regs, fault);
}

void __kprobes do_dat_exception(struct pt_regs *regs)
{
	int access, fault;

	access = VM_READ | VM_EXEC | VM_WRITE;
	fault = do_exception(regs, access);
	if (unlikely(fault))
		do_fault_error(regs, fault);
}

int __handle_fault(unsigned long uaddr, unsigned long pgm_int_code, int write)
{
	struct pt_regs regs;
	int access, fault;

	/* Emulate a uaccess fault from kernel mode. */
	regs.psw.mask = PSW_KERNEL_BITS | PSW_MASK_DAT | PSW_MASK_MCHECK;
	if (!irqs_disabled())
		regs.psw.mask |= PSW_MASK_IO | PSW_MASK_EXT;
	regs.psw.addr = (unsigned long) __builtin_return_address(0);
	regs.psw.addr |= PSW_ADDR_AMODE;
	regs.int_code = pgm_int_code;
	regs.int_parm_long = (uaddr & PAGE_MASK) | 2;
	access = write ? VM_WRITE : VM_READ;
	fault = do_exception(&regs, access);
	/*
	 * Since the fault happened in kernel mode while performing a uaccess
	 * all we need to do now is emulating a fixup in case "fault" is not
	 * zero.
	 * For the calling uaccess functions this results always in -EFAULT.
	 */
	return fault ? -EFAULT : 0;
}

#ifdef CONFIG_PFAULT 
/*
 * 'pfault' pseudo page faults routines.
 */
static int pfault_disable;

static int __init nopfault(char *str)
{
	pfault_disable = 1;
	return 1;
}

__setup("nopfault", nopfault);

struct pfault_refbk {
	u16 refdiagc;
	u16 reffcode;
	u16 refdwlen;
	u16 refversn;
	u64 refgaddr;
	u64 refselmk;
	u64 refcmpmk;
	u64 reserved;
} __attribute__ ((packed, aligned(8)));

int pfault_init(void)
{
	struct pfault_refbk refbk = {
		.refdiagc = 0x258,
		.reffcode = 0,
		.refdwlen = 5,
		.refversn = 2,
		.refgaddr = __LC_CURRENT_PID,
		.refselmk = 1ULL << 48,
		.refcmpmk = 1ULL << 48,
		.reserved = __PF_RES_FIELD };
        int rc;

	if (pfault_disable)
		return -1;
	asm volatile(
		"	diag	%1,%0,0x258\n"
		"0:	j	2f\n"
		"1:	la	%0,8\n"
		"2:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "a" (&refbk), "m" (refbk) : "cc");
        return rc;
}

void pfault_fini(void)
{
	struct pfault_refbk refbk = {
		.refdiagc = 0x258,
		.reffcode = 1,
		.refdwlen = 5,
		.refversn = 2,
	};

	if (pfault_disable)
		return;
	asm volatile(
		"	diag	%0,0,0x258\n"
		"0:\n"
		EX_TABLE(0b,0b)
		: : "a" (&refbk), "m" (refbk) : "cc");
}

static DEFINE_SPINLOCK(pfault_lock);
static LIST_HEAD(pfault_list);

static void pfault_interrupt(struct ext_code ext_code,
			     unsigned int param32, unsigned long param64)
{
	struct task_struct *tsk;
	__u16 subcode;
	pid_t pid;

	/*
	 * Get the external interruption subcode & pfault
	 * initial/completion signal bit. VM stores this 
	 * in the 'cpu address' field associated with the
         * external interrupt. 
	 */
	subcode = ext_code.subcode;
	if ((subcode & 0xff00) != __SUBCODE_MASK)
		return;
	inc_irq_stat(IRQEXT_PFL);
	/* Get the token (= pid of the affected task). */
	pid = sizeof(void *) == 4 ? param32 : param64;
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk)
		get_task_struct(tsk);
	rcu_read_unlock();
	if (!tsk)
		return;
	spin_lock(&pfault_lock);
	if (subcode & 0x0080) {
		/* signal bit is set -> a page has been swapped in by VM */
		if (tsk->thread.pfault_wait == 1) {
			/* Initial interrupt was faster than the completion
			 * interrupt. pfault_wait is valid. Set pfault_wait
			 * back to zero and wake up the process. This can
			 * safely be done because the task is still sleeping
			 * and can't produce new pfaults. */
			tsk->thread.pfault_wait = 0;
			list_del(&tsk->thread.list);
			wake_up_process(tsk);
			put_task_struct(tsk);
		} else {
			/* Completion interrupt was faster than initial
			 * interrupt. Set pfault_wait to -1 so the initial
			 * interrupt doesn't put the task to sleep.
			 * If the task is not running, ignore the completion
			 * interrupt since it must be a leftover of a PFAULT
			 * CANCEL operation which didn't remove all pending
			 * completion interrupts. */
			if (tsk->state == TASK_RUNNING)
				tsk->thread.pfault_wait = -1;
		}
	} else {
		/* signal bit not set -> a real page is missing. */
		if (WARN_ON_ONCE(tsk != current))
			goto out;
		if (tsk->thread.pfault_wait == 1) {
			/* Already on the list with a reference: put to sleep */
			__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			set_tsk_need_resched(tsk);
		} else if (tsk->thread.pfault_wait == -1) {
			/* Completion interrupt was faster than the initial
			 * interrupt (pfault_wait == -1). Set pfault_wait
			 * back to zero and exit. */
			tsk->thread.pfault_wait = 0;
		} else {
			/* Initial interrupt arrived before completion
			 * interrupt. Let the task sleep.
			 * An extra task reference is needed since a different
			 * cpu may set the task state to TASK_RUNNING again
			 * before the scheduler is reached. */
			get_task_struct(tsk);
			tsk->thread.pfault_wait = 1;
			list_add(&tsk->thread.list, &pfault_list);
			__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			set_tsk_need_resched(tsk);
		}
	}
out:
	spin_unlock(&pfault_lock);
	put_task_struct(tsk);
}

static int pfault_cpu_notify(struct notifier_block *self, unsigned long action,
			     void *hcpu)
{
	struct thread_struct *thread, *next;
	struct task_struct *tsk;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DEAD:
		spin_lock_irq(&pfault_lock);
		list_for_each_entry_safe(thread, next, &pfault_list, list) {
			thread->pfault_wait = 0;
			list_del(&thread->list);
			tsk = container_of(thread, struct task_struct, thread);
			wake_up_process(tsk);
			put_task_struct(tsk);
		}
		spin_unlock_irq(&pfault_lock);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int __init pfault_irq_init(void)
{
	int rc;

	rc = register_external_interrupt(0x2603, pfault_interrupt);
	if (rc)
		goto out_extint;
	rc = pfault_init() == 0 ? 0 : -EOPNOTSUPP;
	if (rc)
		goto out_pfault;
	irq_subclass_register(IRQ_SUBCLASS_SERVICE_SIGNAL);
	hotcpu_notifier(pfault_cpu_notify, 0);
	return 0;

out_pfault:
	unregister_external_interrupt(0x2603, pfault_interrupt);
out_extint:
	pfault_disable = 1;
	return rc;
}
early_initcall(pfault_irq_init);

#endif /* CONFIG_PFAULT */
