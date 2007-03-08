/*
 * linux/arch/nios2nommu/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 *
 * 1997-12-01  Modified for POSIX.1b signals by Andreas Schwab
 *
 * mathemu support by Roman Zippel
 *  (Note: fpstate in the signal context is completely ignored for the emulator
 *         and the internal floating point format is put on stack)
 *
 * ++roman (07/09/96): implemented signal stacks (specially for tosemu on
 * Atari :-) Current limitation: Only one sigstack can be active at one time.
 * If a second signal with SA_ONSTACK set arrives while working on a sigstack,
 * SA_ONSTACK is ignored. This behaviour avoids lots of trouble with nested
 * signal handlers!
 *
 * Jan/20/2004		dgt	    NiosII
 *
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/highuid.h>
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/binfmts.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/ucontext.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage long sys_wait4(pid_t pid, unsigned int * stat_addr, int options,
			struct rusage * ru);
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int do_sigsuspend(struct pt_regs *regs)
{
	old_sigset_t mask = regs->r4;  // Verify correct syscall reg
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->r2 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

asmlinkage int
do_rt_sigsuspend(struct pt_regs *regs)
{
	sigset_t *unewset = (sigset_t *)regs->r4;
	size_t sigsetsize = (size_t)regs->r5;
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->r2 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * Do a signal return; undo the signal stack.
 *
 * Keep the return code on the stack quadword aligned!
 * That makes the cache flush below easier.
 */


struct sigframe
{
	char retcode[12];
	unsigned long extramask[_NSIG_WORDS-1];
	struct sigcontext sc;
};

struct rt_sigframe
{
	char retcode[12];
	struct siginfo info;
	struct ucontext uc;
};

#ifdef CONFIG_FPU

static unsigned char fpu_version = 0;	/* version number of fpu, set by setup_frame */

static inline int restore_fpu_state(struct sigcontext *sc)
{
	int err = 1;

	if (FPU_IS_EMU) {
	    /* restore registers */
	    memcpy(current->thread.fpcntl, sc->sc_fpcntl, 12);
	    memcpy(current->thread.fp, sc->sc_fpregs, 24);
	    return 0;
	}

	if (sc->sc_fpstate[0]) {
	    /* Verify the frame format.  */
	    if (sc->sc_fpstate[0] != fpu_version)
		goto out;

	    __asm__ volatile ("Nios II FPU"
			      : /* no outputs */
			      : );
	}
	__asm__ volatile ("Nios II FPU"
			  : : );
	err = 0;

out:
	return err;
}

#define FPCONTEXT_SIZE	216
#define uc_fpstate	uc_filler[0]
#define uc_formatvec	uc_filler[FPCONTEXT_SIZE/4]
#define uc_extra	uc_filler[FPCONTEXT_SIZE/4+1]

static inline int rt_restore_fpu_state(struct ucontext *uc)
{
	unsigned char fpstate[FPCONTEXT_SIZE];
	int context_size = 0;
	fpregset_t fpregs;
	int err = 1;

	if (FPU_IS_EMU) {
		/* restore fpu control register */
		if (__copy_from_user(current->thread.fpcntl,
				&uc->uc_mcontext.fpregs.f_pcr, 12))
			goto out;
		/* restore all other fpu register */
		if (__copy_from_user(current->thread.fp,
				uc->uc_mcontext.fpregs.f_fpregs, 96))
			goto out;
		return 0;
	}

	if (__get_user(*(long *)fpstate, (long *)&uc->uc_fpstate))
		goto out;
	if (fpstate[0]) {
		context_size = fpstate[1];

		/* Verify the frame format.  */
		if (fpstate[0] != fpu_version)
			goto out;
		if (__copy_from_user(&fpregs, &uc->uc_mcontext.fpregs,
		     sizeof(fpregs)))
			goto out;
		__asm__ volatile ("Nios II FPU"
				  : /* no outputs */
				  : );
	}
	if (context_size &&
	    __copy_from_user(fpstate + 4, (long *)&uc->uc_fpstate + 1,
			     context_size))
		goto out;
	__asm__ volatile ("Nios II FPU"
			   : : );
	err = 0;

out:
	return err;
}

#endif

static inline int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *usc, void *fp,
		   int *pr2)
{
	int err = 0;
	int estatus;

	estatus = regs->estatus;

	/* get previous pt_regs */
	if (copy_from_user(regs, &usc->regs, sizeof(*regs)))
		goto badframe;

	/* Prevent user from being able to change
	 * certain processor status bits. Currently nothing.
	 */
	regs->estatus = (estatus & 0xffffffff) | (regs->estatus & 0);

	*pr2 = regs->r2;
	regs->orig_r2 = -1;		/* disable syscall checks */

#ifdef CONFIG_FPU
	err |= restore_fpu_state(&context);
#endif

	return err;

badframe:
	return 1;
}

static inline int
rt_restore_ucontext(struct pt_regs *regs, struct switch_stack *sw,
		    struct ucontext *uc, int *pr2)
{
	int temp;
	greg_t *gregs = uc->uc_mcontext.gregs;
	unsigned long usp;
	int err;

	err = __get_user(temp, &uc->uc_mcontext.version);
	if (temp != MCONTEXT_VERSION)
		goto badframe;
	/* restore passed registers */
	err |= __get_user(regs->r1, &gregs[0]);
	err |= __get_user(regs->r2, &gregs[1]);
	err |= __get_user(regs->r3, &gregs[2]);
	err |= __get_user(regs->r4, &gregs[3]);
	err |= __get_user(regs->r5, &gregs[4]);
	err |= __get_user(regs->r6, &gregs[5]);
	err |= __get_user(regs->r7, &gregs[6]);
	err |= __get_user(regs->r8, &gregs[7]);
	err |= __get_user(regs->r9, &gregs[8]);
	err |= __get_user(regs->r10, &gregs[9]);
	err |= __get_user(regs->r11, &gregs[10]);
	err |= __get_user(regs->r12, &gregs[11]);
	err |= __get_user(regs->r13, &gregs[12]);
	err |= __get_user(regs->r14, &gregs[13]);
	err |= __get_user(regs->r15, &gregs[14]);
	err |= __get_user(sw->r16, &gregs[15]);
	err |= __get_user(sw->r17, &gregs[16]);
	err |= __get_user(sw->r18, &gregs[17]);
	err |= __get_user(sw->r19, &gregs[18]);
	err |= __get_user(sw->r20, &gregs[19]);
	err |= __get_user(sw->r21, &gregs[20]);
	err |= __get_user(sw->r22, &gregs[21]);
	err |= __get_user(sw->r23, &gregs[22]);
	err |= __get_user(usp, &gregs[23]);
	err |= __get_user(sw->fp, &gregs[24]);  // Verify, should this be settable
	err |= __get_user(sw->gp, &gregs[25]);  // Verify, should this be settable

	err |= __get_user(temp, &gregs[26]);  // Not really necessary no user settable bits
	regs->estatus = (regs->estatus & 0xffffffff) | (temp & 0x0);
	err |= __get_user(regs->status_extension, 
			  &uc->uc_mcontext.status_extension);
	regs->orig_r2 = -1;		/* disable syscall checks */

	if (do_sigaltstack(&uc->uc_stack, NULL, usp) == -EFAULT)
		goto badframe;

	*pr2 = regs->r2;
	return err;

badframe:
	return 1;
}

asmlinkage int do_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame = (struct sigframe *) regs->sp;
	sigset_t set;
	int rval;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.sc_mask) ||
	    (_NSIG_WORDS > 1 &&
	     __copy_from_user(&set.sig[1], &frame->extramask,
			      sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->sc, frame + 1, &rval))
		goto badframe;
	return rval;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int do_rt_sigreturn(struct switch_stack *sw)
{
	struct pt_regs *regs = (struct pt_regs *) sw + 1;
	struct rt_sigframe *frame = (struct rt_sigframe *) regs->sp;  // Verify, can we follow the stack back
	sigset_t set;
	int rval;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (rt_restore_ucontext(regs, sw, &frame->uc, &rval))
		goto badframe;
	return rval;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

#ifdef CONFIG_FPU
/*
 * Set up a signal frame.
 *
 * Not converted, no FPU support at moment.
 */

static inline int save_fpu_state(struct sigcontext *sc, struct pt_regs *regs)
{
	int err = 0;

	if (FPU_IS_EMU) {
		/* save registers */
		err |= copy_to_user(&sc->sc_fpcntl, current->thread.fpcntl, 12);
		err |= copy_to_user(&sc->sc_fpregs, current->thread.fp, 24);
		return err;
	}

	__asm__ volatile ("Nios II FPUt"
			  : : );

	if (sc->sc_fpstate[0]) {
		fpu_version = sc->sc_fpstate[0];
		__asm__ volatile ("Nios II FPU"
				  : /* no outputs */
				  : 
				  : );
	}
	return err;
}

static inline int rt_save_fpu_state(struct ucontext *uc, struct pt_regs *regs)
{
	unsigned char fpstate[FPCONTEXT_SIZE];
	int context_size = 0;
	int err = 0;

	if (FPU_IS_EMU) {
		/* save fpu control register */
		err |= copy_to_user(&uc->uc_mcontext.fpregs.f_pcr,
				current->thread.fpcntl, 12);
		/* save all other fpu register */
		err |= copy_to_user(uc->uc_mcontext.fpregs.f_fpregs,
				current->thread.fp, 96);
		return err;
	}

	__asm__ volatile ("Nios II FPU"
			  : : : );

	err |= __put_user(*(long *)fpstate, (long *)&uc->uc_fpstate);
	if (fpstate[0]) {
		fpregset_t fpregs;
		context_size = fpstate[1];
		fpu_version = fpstate[0];
		__asm__ volatile ("Nios II FPU"
				  : /* no outputs */
				  : 
				  : );
		err |= copy_to_user(&uc->uc_mcontext.fpregs, &fpregs,
				    sizeof(fpregs));
	}
	if (context_size)
		err |= copy_to_user((long *)&uc->uc_fpstate + 1, fpstate + 4,
				    context_size);
	return err;
}

#endif

static int setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
			     unsigned long mask)
{
	int err = 0;

	err |= __put_user(mask, &sc->sc_mask);
	err |= copy_to_user(&sc->regs, regs, sizeof(*regs));
#ifdef CONFIG_FPU
	err |= save_fpu_state(sc, regs);
#endif
	return err;
}

static inline int rt_setup_ucontext(struct ucontext *uc, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	greg_t *gregs = uc->uc_mcontext.gregs;
	int err = 0;

	err |= __put_user(MCONTEXT_VERSION, &uc->uc_mcontext.version);
	err |= __put_user(regs->status_extension, 
			  &uc->uc_mcontext.status_extension);
	err |= __put_user(regs->r1, &gregs[0]);
	err |= __put_user(regs->r2, &gregs[1]);
	err |= __put_user(regs->r3, &gregs[2]);
	err |= __put_user(regs->r4, &gregs[3]);
	err |= __put_user(regs->r5, &gregs[4]);
	err |= __put_user(regs->r6, &gregs[5]);
	err |= __put_user(regs->r7, &gregs[6]);
	err |= __put_user(regs->r8, &gregs[7]);
	err |= __put_user(regs->r9, &gregs[8]);
	err |= __put_user(regs->r10, &gregs[9]);
	err |= __put_user(regs->r11, &gregs[10]);
	err |= __put_user(regs->r12, &gregs[11]);
	err |= __put_user(regs->r13, &gregs[12]);
	err |= __put_user(regs->r14, &gregs[13]);
	err |= __put_user(regs->r15, &gregs[14]);
	err |= __put_user(sw->r16, &gregs[15]);
	err |= __put_user(sw->r17, &gregs[16]);
	err |= __put_user(sw->r18, &gregs[17]);
	err |= __put_user(sw->r19, &gregs[18]);
	err |= __put_user(sw->r20, &gregs[19]);
	err |= __put_user(sw->r21, &gregs[20]);
	err |= __put_user(sw->r22, &gregs[21]);
	err |= __put_user(sw->r23, &gregs[22]);
	err |= __put_user(regs->sp, &gregs[23]);
	err |= __put_user(sw->fp, &gregs[24]);
	err |= __put_user(sw->gp, &gregs[25]);
#ifdef CONFIG_FPU
	err |= rt_save_fpu_state(uc, regs);
#endif
	return err;
}

extern void cache_push_v (unsigned long vaddr, int len);

static inline void push_cache (unsigned long vaddr)
{
	cache_push_v(vaddr,12);
}

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long usp;

	/* Default to using normal stack.  */
	usp = regs->sp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (!on_sig_stack(usp))
			usp = current->sas_ss_sp + current->sas_ss_size;
	}
	return (void *)((usp - frame_size) & -8UL);  // Verify, is it 32 or 64 bit aligned
}

static void setup_frame (int sig, struct k_sigaction *ka,
			 sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (_NSIG_WORDS > 1)
		err |= copy_to_user(frame->extramask, &set->sig[1],
				    sizeof(frame->extramask));

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	/* Set up to return from userspace.  */
	regs->ra = (unsigned long) &frame->retcode[0];
	/* movi r3,__NR_sigreturn */
	err |= __put_user(0x00c00004 + (__NR_sigreturn << 6), (long *)(frame->retcode));
	/* mov r2,r0 */
	err |= __put_user(0x0005883a, (long *)(frame->retcode + 4));
	/* trap */
	err |= __put_user(0x003b683a, (long *)(frame->retcode + 8));

	if (err)
		goto give_sigsegv;

	push_cache ((unsigned long) &frame->retcode);

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->r4 = (unsigned long) (current_thread_info()->exec_domain
				    && current_thread_info()->exec_domain->signal_invmap
				    && sig < 32
				    ? current_thread_info()->exec_domain->signal_invmap[sig]
				    : sig);
	regs->ea = (unsigned long) ka->sa.sa_handler;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame (int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= rt_setup_ucontext(&frame->uc, regs);
	err |= copy_to_user (&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  */
	regs->ra = (unsigned long) &frame->retcode[0];
	/* movi r3,__NR_rt_sigreturn */
	err |= __put_user(0x00c00004 + (__NR_rt_sigreturn << 6), (long *)(frame->retcode));
	/* mov r2,r0 */
	err |= __put_user(0x0005883a, (long *)(frame->retcode + 4));
	/* trap */
	err |= __put_user(0x003b683a, (long *)(frame->retcode + 8));

	if (err)
		goto give_sigsegv;

	push_cache ((unsigned long) &frame->retcode);

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->r4 = (unsigned long) (current_thread_info()->exec_domain
				    && current_thread_info()->exec_domain->signal_invmap
				    && sig < 32
				    ? current_thread_info()->exec_domain->signal_invmap[sig]
				    : sig);
	regs->r5 = (unsigned long) &frame->info;
	regs->r6 = (unsigned long) &frame->uc;
	regs->ea = (unsigned long) ka->sa.sa_handler;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static inline void
handle_restart(struct pt_regs *regs, struct k_sigaction *ka, int has_handler)
{
	switch (regs->r2) {
	case -ERESTARTNOHAND:
		if (!has_handler)
			goto do_restart;
		regs->r2 = -EINTR;
		break;

	case -ERESTARTSYS:
		if (has_handler && !(ka->sa.sa_flags & SA_RESTART)) {
			regs->r2 = -EINTR;
			break;
		}
	/* fallthrough */
	case -ERESTARTNOINTR:
	do_restart:
		regs->r2 = regs->orig_r2;
		regs->ea -= 4;
		break;
	}
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(int sig, struct k_sigaction *ka, siginfo_t *info,
	      sigset_t *oldset, struct pt_regs *regs)
{
	/* are we from a system call? */
	if (regs->orig_r2 >= 0)
		/* If so, check system call restarting.. */
		handle_restart(regs, ka, 1);

	/* set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	/* FIXME - Do we still need to do this ? */
	current->thread.kregs = regs;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->orig_r2 >= 0){
		/* Restart the system call - no handlers present */
		if (regs->r2 == -ERESTARTNOHAND
		    || regs->r2 == -ERESTARTSYS
		    || regs->r2 == -ERESTARTNOINTR) {
			regs->r2 = regs->orig_r2;
			regs->ea -= 4;
		} else if (regs->r2 == -ERESTART_RESTARTBLOCK) {
			regs->r2 = __NR_restart_syscall;
			regs->ea -= 4;
		}
	}
	return 0;
}
