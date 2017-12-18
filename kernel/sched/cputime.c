#include <linux/export.h>
#include <linux/sched.h>
#include <linux/tsacct_kern.h>
#include <linux/kernel_stat.h>
#include <linux/static_key.h>
#include <linux/context_tracking.h>
#include "sched.h"
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#endif


#ifdef CONFIG_IRQ_TIME_ACCOUNTING

/*
 * There are no locks covering percpu hardirq/softirq time.
 * They are only modified in vtime_account, on corresponding CPU
 * with interrupts disabled. So, writes are safe.
 * They are read and saved off onto struct rq in update_rq_clock().
 * This may result in other CPU reading this CPU's irq time and can
 * race with irq/vtime_account on this CPU. We would either get old
 * or new value with a side effect of accounting a slice of irq time to wrong
 * task when irq is in progress while we read rq->clock. That is a worthy
 * compromise in place of having locks on each irq in account_system_time.
 */
DEFINE_PER_CPU(struct irqtime, cpu_irqtime);

static int sched_clock_irqtime;

void enable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 1;
}

void disable_sched_clock_irqtime(void)
{
	sched_clock_irqtime = 0;
}

/*
 * Called before incrementing preempt_count on {soft,}irq_enter
 * and before decrementing preempt_count on {soft,}irq_exit.
 */
void irqtime_account_irq(struct task_struct *curr)
{
	struct irqtime *irqtime = this_cpu_ptr(&cpu_irqtime);
	s64 delta;
	int cpu;

	if (!sched_clock_irqtime)
		return;

	cpu = smp_processor_id();
	delta = sched_clock_cpu(cpu) - irqtime->irq_start_time;
	irqtime->irq_start_time += delta;

	u64_stats_update_begin(&irqtime->sync);
	/*
	 * We do not account for softirq time from ksoftirqd here.
	 * We want to continue accounting softirq time to ksoftirqd thread
	 * in that case, so as not to confuse scheduler with a special task
	 * that do not consume any time, but still wants to run.
	 */
	if (hardirq_count())
		irqtime->hardirq_time += delta;
	else if (in_serving_softirq() && curr != this_cpu_ksoftirqd())
		irqtime->softirq_time += delta;

	u64_stats_update_end(&irqtime->sync);
}
EXPORT_SYMBOL_GPL(irqtime_account_irq);

static cputime_t irqtime_account_update(u64 irqtime, int idx, cputime_t maxtime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	cputime_t irq_cputime;

	irq_cputime = nsecs_to_cputime64(irqtime) - cpustat[idx];
	irq_cputime = min(irq_cputime, maxtime);
	cpustat[idx] += irq_cputime;

	return irq_cputime;
}

static cputime_t irqtime_account_hi_update(cputime_t maxtime)
{
	return irqtime_account_update(__this_cpu_read(cpu_irqtime.hardirq_time),
				      CPUTIME_IRQ, maxtime);
}

static cputime_t irqtime_account_si_update(cputime_t maxtime)
{
	return irqtime_account_update(__this_cpu_read(cpu_irqtime.softirq_time),
				      CPUTIME_SOFTIRQ, maxtime);
}

#else /* CONFIG_IRQ_TIME_ACCOUNTING */

#define sched_clock_irqtime	(0)

static cputime_t irqtime_account_hi_update(cputime_t dummy)
{
	return 0;
}

static cputime_t irqtime_account_si_update(cputime_t dummy)
{
	return 0;
}

#endif /* !CONFIG_IRQ_TIME_ACCOUNTING */

static inline void task_group_account_field(struct task_struct *p, int index,
					    u64 tmp)
{
	/*
	 * Since all updates are sure to touch the root cgroup, we
	 * get ourselves ahead and touch it first. If the root cgroup
	 * is the only cgroup, then nothing else should be necessary.
	 *
	 */
	__this_cpu_add(kernel_cpustat.cpustat[index], tmp);

	cpuacct_account_field(p, index, tmp);
}

/*
 * Account user cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in user space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_user_time(struct task_struct *p, cputime_t cputime,
		       cputime_t cputime_scaled)
{
	int index;

	/* Add user time to process. */
	p->utime += cputime;
	p->utimescaled += cputime_scaled;
	account_group_user_time(p, cputime);

	index = (task_nice(p) > 0) ? CPUTIME_NICE : CPUTIME_USER;

	/* Add user time to cpustat. */
	task_group_account_field(p, index, (__force u64) cputime);

	/* Account for user time used */
	acct_account_cputime(p);
}

/*
 * Account guest cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in virtual machine since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
static void account_guest_time(struct task_struct *p, cputime_t cputime,
			       cputime_t cputime_scaled)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	/* Add guest time to process. */
	p->utime += cputime;
	p->utimescaled += cputime_scaled;
	account_group_user_time(p, cputime);
	p->gtime += cputime;

	/* Add guest time to cpustat. */
	if (task_nice(p) > 0) {
		cpustat[CPUTIME_NICE] += (__force u64) cputime;
		cpustat[CPUTIME_GUEST_NICE] += (__force u64) cputime;
	} else {
		cpustat[CPUTIME_USER] += (__force u64) cputime;
		cpustat[CPUTIME_GUEST] += (__force u64) cputime;
	}
}

/*
 * Account system cpu time to a process and desired cpustat field
 * @p: the process that the cpu time gets accounted to
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 * @target_cputime64: pointer to cpustat field that has to be updated
 */
static inline
void __account_system_time(struct task_struct *p, cputime_t cputime,
			cputime_t cputime_scaled, int index)
{
	/* Add system time to process. */
	p->stime += cputime;
	p->stimescaled += cputime_scaled;
	account_group_system_time(p, cputime);

	/* Add system time to cpustat. */
	task_group_account_field(p, index, (__force u64) cputime);

	/* Account for system time used */
	acct_account_cputime(p);
}

/*
 * Account system cpu time to a process.
 * @p: the process that the cpu time gets accounted to
 * @hardirq_offset: the offset to subtract from hardirq_count()
 * @cputime: the cpu time spent in kernel space since the last update
 * @cputime_scaled: cputime scaled by cpu frequency
 */
void account_system_time(struct task_struct *p, int hardirq_offset,
			 cputime_t cputime, cputime_t cputime_scaled)
{
	int index;

	if ((p->flags & PF_VCPU) && (irq_count() - hardirq_offset == 0)) {
		account_guest_time(p, cputime, cputime_scaled);
		return;
	}

	if (hardirq_count() - hardirq_offset)
		index = CPUTIME_IRQ;
	else if (in_serving_softirq())
		index = CPUTIME_SOFTIRQ;
	else
		index = CPUTIME_SYSTEM;

	__account_system_time(p, cputime, cputime_scaled, index);
}

/*
 * Account for involuntary wait time.
 * @cputime: the cpu time spent in involuntary wait
 */
void account_steal_time(cputime_t cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;

	cpustat[CPUTIME_STEAL] += (__force u64) cputime;
}

/*
 * Account for idle time.
 * @cputime: the cpu time spent in idle wait
 */
void account_idle_time(cputime_t cputime)
{
	u64 *cpustat = kcpustat_this_cpu->cpustat;
	struct rq *rq = this_rq();

	if (atomic_read(&rq->nr_iowait) > 0)
		cpustat[CPUTIME_IOWAIT] += (__force u64) cputime;
	else
		cpustat[CPUTIME_IDLE] += (__force u64) cputime;
}

/*
 * When a guest is interrupted for a longer amount of time, missed clock
 * ticks are not redelivered later. Due to that, this function may on
 * occasion account more time than the calling functions think elapsed.
 */
static __always_inline cputime_t steal_account_process_time(cputime_t maxtime)
{
#ifdef CONFIG_PARAVIRT
	if (static_key_false(&paravirt_steal_enabled)) {
		cputime_t steal_cputime;
		u64 steal;

		steal = paravirt_steal_clock(smp_processor_id());
		steal -= this_rq()->prev_steal_time;

		steal_cputime = min(nsecs_to_cputime(steal), maxtime);
		account_steal_time(steal_cputime);
		this_rq()->prev_steal_time += cputime_to_nsecs(steal_cputime);

		return steal_cputime;
	}
#endif
	return 0;
}

/*
 * Account how much elapsed time was spent in steal, irq, or softirq time.
 */
static inline cputime_t account_other_time(cputime_t max)
{
	cputime_t accounted;

	/* Shall be converted to a lockdep-enabled lightweight check */
	WARN_ON_ONCE(!irqs_disabled());

	accounted = steal_account_process_time(max);

	if (accounted < max)
		accounted += irqtime_account_hi_update(max - accounted);

	if (accounted < max)
		accounted += irqtime_account_si_update(max - accounted);

	return accounted;
}

#ifdef CONFIG_64BIT
static inline u64 read_sum_exec_runtime(struct task_struct *t)
{
	return t->se.sum_exec_runtime;
}
#else
static u64 read_sum_exec_runtime(struct task_struct *t)
{
	u64 ns;
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(t, &rf);
	ns = t->se.sum_exec_runtime;
	task_rq_unlock(rq, t, &rf);

	return ns;
}
#endif

/*
 * Accumulate raw cputime values of dead tasks (sig->[us]time) and live
 * tasks (sum on group iteration) belonging to @tsk's group.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	struct signal_struct *sig = tsk->signal;
	cputime_t utime, stime;
	struct task_struct *t;
	unsigned int seq, nextseq;
	unsigned long flags;

	/*
	 * Update current task runtime to account pending time since last
	 * scheduler action or thread_group_cputime() call. This thread group
	 * might have other running tasks on different CPUs, but updating
	 * their runtime can affect syscall performance, so we skip account
	 * those pending times and rely only on values updated on tick or
	 * other scheduler action.
	 */
	if (same_thread_group(current, tsk))
		(void) task_sched_runtime(current);

	rcu_read_lock();
	/* Attempt a lockless read on the first round. */
	nextseq = 0;
	do {
		seq = nextseq;
		flags = read_seqbegin_or_lock_irqsave(&sig->stats_lock, &seq);
		times->utime = sig->utime;
		times->stime = sig->stime;
		times->sum_exec_runtime = sig->sum_sched_runtime;

		for_each_thread(tsk, t) {
			task_cputime(t, &utime, &stime);
			times->utime += utime;
			times->stime += stime;
			times->sum_exec_runtime += read_sum_exec_runtime(t);
		}
		/* If lockless access failed, take the lock. */
		nextseq = 1;
	} while (need_seqretry(&sig->stats_lock, seq));
	done_seqretry_irqrestore(&sig->stats_lock, seq, flags);
	rcu_read_unlock();
}

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
/*
 * Account a tick to a process and cpustat
 * @p: the process that the cpu time gets accounted to
 * @user_tick: is the tick from userspace
 * @rq: the pointer to rq
 *
 * Tick demultiplexing follows the order
 * - pending hardirq update
 * - pending softirq update
 * - user_time
 * - idle_time
 * - system time
 *   - check for guest_time
 *   - else account as system_time
 *
 * Check for hardirq is done both for system and user time as there is
 * no timer going off while we are on hardirq and hence we may never get an
 * opportunity to update it solely in system time.
 * p->stime and friends are only updated on system time and not on irq
 * softirq as those do not count in task exec_runtime any more.
 */
static void irqtime_account_process_tick(struct task_struct *p, int user_tick,
					 struct rq *rq, int ticks)
{
	u64 cputime = (__force u64) cputime_one_jiffy * ticks;
	cputime_t scaled, other;

	/*
	 * When returning from idle, many ticks can get accounted at
	 * once, including some ticks of steal, irq, and softirq time.
	 * Subtract those ticks from the amount of time accounted to
	 * idle, or potentially user or system time. Due to rounding,
	 * other time can exceed ticks occasionally.
	 */
	other = account_other_time(ULONG_MAX);
	if (other >= cputime)
		return;
	cputime -= other;
	scaled = cputime_to_scaled(cputime);

	if (this_cpu_ksoftirqd() == p) {
		/*
		 * ksoftirqd time do not get accounted in cpu_softirq_time.
		 * So, we have to handle it separately here.
		 * Also, p->stime needs to be updated for ksoftirqd.
		 */
		__account_system_time(p, cputime, scaled, CPUTIME_SOFTIRQ);
	} else if (user_tick) {
		account_user_time(p, cputime, scaled);
	} else if (p == rq->idle) {
		account_idle_time(cputime);
	} else if (p->flags & PF_VCPU) { /* System time or guest time */
		account_guest_time(p, cputime, scaled);
	} else {
		__account_system_time(p, cputime, scaled,	CPUTIME_SYSTEM);
	}
}

static void irqtime_account_idle_ticks(int ticks)
{
	struct rq *rq = this_rq();

	irqtime_account_process_tick(current, 0, rq, ticks);
}
#else /* CONFIG_IRQ_TIME_ACCOUNTING */
static inline void irqtime_account_idle_ticks(int ticks) {}
static inline void irqtime_account_process_tick(struct task_struct *p, int user_tick,
						struct rq *rq, int nr_ticks) {}
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING

#ifndef __ARCH_HAS_VTIME_TASK_SWITCH
void vtime_common_task_switch(struct task_struct *prev)
{
	if (is_idle_task(prev))
		vtime_account_idle(prev);
	else
		vtime_account_system(prev);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	vtime_account_user(prev);
#endif
	arch_vtime_task_switch(prev);
}
#endif

#endif /* CONFIG_VIRT_CPU_ACCOUNTING */


#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
/*
 * Archs that account the whole time spent in the idle task
 * (outside irq) as idle time can rely on this and just implement
 * vtime_account_system() and vtime_account_idle(). Archs that
 * have other meaning of the idle time (s390 only includes the
 * time spent by the CPU when it's in low power mode) must override
 * vtime_account().
 */
#ifndef __ARCH_HAS_VTIME_ACCOUNT
void vtime_account_irq_enter(struct task_struct *tsk)
{
	if (!in_interrupt() && is_idle_task(tsk))
		vtime_account_idle(tsk);
	else
		vtime_account_system(tsk);
}
EXPORT_SYMBOL_GPL(vtime_account_irq_enter);
#endif /* __ARCH_HAS_VTIME_ACCOUNT */

void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	*ut = p->utime;
	*st = p->stime;
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);

	*ut = cputime.utime;
	*st = cputime.stime;
}
#else /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
/*
 * Account a single tick of cpu time.
 * @p: the process that the cpu time gets accounted to
 * @user_tick: indicates if the tick is a user or a system tick
 */
void account_process_tick(struct task_struct *p, int user_tick)
{
	cputime_t cputime, scaled, steal;
	struct rq *rq = this_rq();

	if (vtime_accounting_cpu_enabled())
		return;

	if (sched_clock_irqtime) {
		irqtime_account_process_tick(p, user_tick, rq, 1);
		return;
	}

	cputime = cputime_one_jiffy;
	steal = steal_account_process_time(ULONG_MAX);

	if (steal >= cputime)
		return;

	cputime -= steal;
	scaled = cputime_to_scaled(cputime);

	if (user_tick)
		account_user_time(p, cputime, scaled);
	else if ((p != rq->idle) || (irq_count() != HARDIRQ_OFFSET))
		account_system_time(p, HARDIRQ_OFFSET, cputime, scaled);
	else
		account_idle_time(cputime);
}

/*
 * Account multiple ticks of idle time.
 * @ticks: number of stolen ticks
 */
void account_idle_ticks(unsigned long ticks)
{
	cputime_t cputime, steal;

	if (sched_clock_irqtime) {
		irqtime_account_idle_ticks(ticks);
		return;
	}

	cputime = jiffies_to_cputime(ticks);
	steal = steal_account_process_time(ULONG_MAX);

	if (steal >= cputime)
		return;

	cputime -= steal;
	account_idle_time(cputime);
}

/*
 * Perform (stime * rtime) / total, but avoid multiplication overflow by
 * loosing precision when the numbers are big.
 */
static cputime_t scale_stime(u64 stime, u64 rtime, u64 total)
{
	u64 scaled;

	for (;;) {
		/* Make sure "rtime" is the bigger of stime/rtime */
		if (stime > rtime)
			swap(rtime, stime);

		/* Make sure 'total' fits in 32 bits */
		if (total >> 32)
			goto drop_precision;

		/* Does rtime (and thus stime) fit in 32 bits? */
		if (!(rtime >> 32))
			break;

		/* Can we just balance rtime/stime rather than dropping bits? */
		if (stime >> 31)
			goto drop_precision;

		/* We can grow stime and shrink rtime and try to make them both fit */
		stime <<= 1;
		rtime >>= 1;
		continue;

drop_precision:
		/* We drop from rtime, it has more bits than stime */
		rtime >>= 1;
		total >>= 1;
	}

	/*
	 * Make sure gcc understands that this is a 32x32->64 multiply,
	 * followed by a 64/32->64 divide.
	 */
	scaled = div_u64((u64) (u32) stime * (u64) (u32) rtime, (u32)total);
	return (__force cputime_t) scaled;
}

/*
 * Adjust tick based cputime random precision against scheduler runtime
 * accounting.
 *
 * Tick based cputime accounting depend on random scheduling timeslices of a
 * task to be interrupted or not by the timer.  Depending on these
 * circumstances, the number of these interrupts may be over or
 * under-optimistic, matching the real user and system cputime with a variable
 * precision.
 *
 * Fix this by scaling these tick based values against the total runtime
 * accounted by the CFS scheduler.
 *
 * This code provides the following guarantees:
 *
 *   stime + utime == rtime
 *   stime_i+1 >= stime_i, utime_i+1 >= utime_i
 *
 * Assuming that rtime_i+1 >= rtime_i.
 */
static void cputime_adjust(struct task_cputime *curr,
			   struct prev_cputime *prev,
			   cputime_t *ut, cputime_t *st)
{
	cputime_t rtime, stime, utime;
	unsigned long flags;

	/* Serialize concurrent callers such that we can honour our guarantees */
	raw_spin_lock_irqsave(&prev->lock, flags);
	rtime = nsecs_to_cputime(curr->sum_exec_runtime);

	/*
	 * This is possible under two circumstances:
	 *  - rtime isn't monotonic after all (a bug);
	 *  - we got reordered by the lock.
	 *
	 * In both cases this acts as a filter such that the rest of the code
	 * can assume it is monotonic regardless of anything else.
	 */
	if (prev->stime + prev->utime >= rtime)
		goto out;

	stime = curr->stime;
	utime = curr->utime;

	/*
	 * If either stime or both stime and utime are 0, assume all runtime is
	 * userspace. Once a task gets some ticks, the monotonicy code at
	 * 'update' will ensure things converge to the observed ratio.
	 */
	if (stime == 0) {
		utime = rtime;
		goto update;
	}

	if (utime == 0) {
		stime = rtime;
		goto update;
	}

	stime = scale_stime((__force u64)stime, (__force u64)rtime,
			    (__force u64)(stime + utime));

update:
	/*
	 * Make sure stime doesn't go backwards; this preserves monotonicity
	 * for utime because rtime is monotonic.
	 *
	 *  utime_i+1 = rtime_i+1 - stime_i
	 *            = rtime_i+1 - (rtime_i - utime_i)
	 *            = (rtime_i+1 - rtime_i) + utime_i
	 *            >= utime_i
	 */
	if (stime < prev->stime)
		stime = prev->stime;
	utime = rtime - stime;

	/*
	 * Make sure utime doesn't go backwards; this still preserves
	 * monotonicity for stime, analogous argument to above.
	 */
	if (utime < prev->utime) {
		utime = prev->utime;
		stime = rtime - utime;
	}

	prev->stime = stime;
	prev->utime = utime;
out:
	*ut = prev->utime;
	*st = prev->stime;
	raw_spin_unlock_irqrestore(&prev->lock, flags);
}

void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime = {
		.sum_exec_runtime = p->se.sum_exec_runtime,
	};

	task_cputime(p, &cputime.utime, &cputime.stime);
	cputime_adjust(&cputime, &p->prev_cputime, ut, st);
}
EXPORT_SYMBOL_GPL(task_cputime_adjusted);

void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);
	cputime_adjust(&cputime, &p->signal->prev_cputime, ut, st);
}
#endif /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
static cputime_t vtime_delta(struct task_struct *tsk)
{
	unsigned long now = READ_ONCE(jiffies);

	if (time_before(now, (unsigned long)tsk->vtime_snap))
		return 0;

	return jiffies_to_cputime(now - tsk->vtime_snap);
}

static cputime_t get_vtime_delta(struct task_struct *tsk)
{
	unsigned long now = READ_ONCE(jiffies);
	cputime_t delta, other;

	/*
	 * Unlike tick based timing, vtime based timing never has lost
	 * ticks, and no need for steal time accounting to make up for
	 * lost ticks. Vtime accounts a rounded version of actual
	 * elapsed time. Limit account_other_time to prevent rounding
	 * errors from causing elapsed vtime to go negative.
	 */
	delta = jiffies_to_cputime(now - tsk->vtime_snap);
	other = account_other_time(delta);
	WARN_ON_ONCE(tsk->vtime_snap_whence == VTIME_INACTIVE);
	tsk->vtime_snap = now;

	return delta - other;
}

static void __vtime_account_system(struct task_struct *tsk)
{
	cputime_t delta_cpu = get_vtime_delta(tsk);

	account_system_time(tsk, irq_count(), delta_cpu, cputime_to_scaled(delta_cpu));
}

void vtime_account_system(struct task_struct *tsk)
{
	if (!vtime_delta(tsk))
		return;

	write_seqcount_begin(&tsk->vtime_seqcount);
	__vtime_account_system(tsk);
	write_seqcount_end(&tsk->vtime_seqcount);
}

void vtime_account_user(struct task_struct *tsk)
{
	cputime_t delta_cpu;

	write_seqcount_begin(&tsk->vtime_seqcount);
	tsk->vtime_snap_whence = VTIME_SYS;
	if (vtime_delta(tsk)) {
		delta_cpu = get_vtime_delta(tsk);
		account_user_time(tsk, delta_cpu, cputime_to_scaled(delta_cpu));
	}
	write_seqcount_end(&tsk->vtime_seqcount);
}

void vtime_user_enter(struct task_struct *tsk)
{
	write_seqcount_begin(&tsk->vtime_seqcount);
	if (vtime_delta(tsk))
		__vtime_account_system(tsk);
	tsk->vtime_snap_whence = VTIME_USER;
	write_seqcount_end(&tsk->vtime_seqcount);
}

void vtime_guest_enter(struct task_struct *tsk)
{
	/*
	 * The flags must be updated under the lock with
	 * the vtime_snap flush and update.
	 * That enforces a right ordering and update sequence
	 * synchronization against the reader (task_gtime())
	 * that can thus safely catch up with a tickless delta.
	 */
	write_seqcount_begin(&tsk->vtime_seqcount);
	if (vtime_delta(tsk))
		__vtime_account_system(tsk);
	current->flags |= PF_VCPU;
	write_seqcount_end(&tsk->vtime_seqcount);
}
EXPORT_SYMBOL_GPL(vtime_guest_enter);

void vtime_guest_exit(struct task_struct *tsk)
{
	write_seqcount_begin(&tsk->vtime_seqcount);
	__vtime_account_system(tsk);
	current->flags &= ~PF_VCPU;
	write_seqcount_end(&tsk->vtime_seqcount);
}
EXPORT_SYMBOL_GPL(vtime_guest_exit);

void vtime_account_idle(struct task_struct *tsk)
{
	cputime_t delta_cpu = get_vtime_delta(tsk);

	account_idle_time(delta_cpu);
}

void arch_vtime_task_switch(struct task_struct *prev)
{
	write_seqcount_begin(&prev->vtime_seqcount);
	prev->vtime_snap_whence = VTIME_INACTIVE;
	write_seqcount_end(&prev->vtime_seqcount);

	write_seqcount_begin(&current->vtime_seqcount);
	current->vtime_snap_whence = VTIME_SYS;
	current->vtime_snap = jiffies;
	write_seqcount_end(&current->vtime_seqcount);
}

void vtime_init_idle(struct task_struct *t, int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	write_seqcount_begin(&t->vtime_seqcount);
	t->vtime_snap_whence = VTIME_SYS;
	t->vtime_snap = jiffies;
	write_seqcount_end(&t->vtime_seqcount);
	local_irq_restore(flags);
}

cputime_t task_gtime(struct task_struct *t)
{
	unsigned int seq;
	cputime_t gtime;

	if (!vtime_accounting_enabled())
		return t->gtime;

	do {
		seq = read_seqcount_begin(&t->vtime_seqcount);

		gtime = t->gtime;
		if (t->vtime_snap_whence == VTIME_SYS && t->flags & PF_VCPU)
			gtime += vtime_delta(t);

	} while (read_seqcount_retry(&t->vtime_seqcount, seq));

	return gtime;
}

/*
 * Fetch cputime raw values from fields of task_struct and
 * add up the pending nohz execution time since the last
 * cputime snapshot.
 */
static void
fetch_task_cputime(struct task_struct *t,
		   cputime_t *u_dst, cputime_t *s_dst,
		   cputime_t *u_src, cputime_t *s_src,
		   cputime_t *udelta, cputime_t *sdelta)
{
	unsigned int seq;
	unsigned long long delta;

	do {
		*udelta = 0;
		*sdelta = 0;

		seq = read_seqcount_begin(&t->vtime_seqcount);

		if (u_dst)
			*u_dst = *u_src;
		if (s_dst)
			*s_dst = *s_src;

		/* Task is sleeping, nothing to add */
		if (t->vtime_snap_whence == VTIME_INACTIVE ||
		    is_idle_task(t))
			continue;

		delta = vtime_delta(t);

		/*
		 * Task runs either in user or kernel space, add pending nohz time to
		 * the right place.
		 */
		if (t->vtime_snap_whence == VTIME_USER || t->flags & PF_VCPU) {
			*udelta = delta;
		} else {
			if (t->vtime_snap_whence == VTIME_SYS)
				*sdelta = delta;
		}
	} while (read_seqcount_retry(&t->vtime_seqcount, seq));
}


void task_cputime(struct task_struct *t, cputime_t *utime, cputime_t *stime)
{
	cputime_t udelta, sdelta;

	if (!vtime_accounting_enabled()) {
		if (utime)
			*utime = t->utime;
		if (stime)
			*stime = t->stime;
		return;
	}

	fetch_task_cputime(t, utime, stime, &t->utime,
			   &t->stime, &udelta, &sdelta);
	if (utime)
		*utime += udelta;
	if (stime)
		*stime += sdelta;
}

void task_cputime_scaled(struct task_struct *t,
			 cputime_t *utimescaled, cputime_t *stimescaled)
{
	cputime_t udelta, sdelta;

	if (!vtime_accounting_enabled()) {
		if (utimescaled)
			*utimescaled = t->utimescaled;
		if (stimescaled)
			*stimescaled = t->stimescaled;
		return;
	}

	fetch_task_cputime(t, utimescaled, stimescaled,
			   &t->utimescaled, &t->stimescaled, &udelta, &sdelta);
	if (utimescaled)
		*utimescaled += cputime_to_scaled(udelta);
	if (stimescaled)
		*stimescaled += cputime_to_scaled(sdelta);
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_GEN */
