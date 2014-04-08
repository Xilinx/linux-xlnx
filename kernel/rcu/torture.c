/*
 * Read-Copy Update module-based torture test facility
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2005, 2006
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
 *	  Josh Triplett <josh@freedesktop.org>
 *
 * See also:  Documentation/RCU/torture.txt
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/trace_clock.h>
#include <asm/byteorder.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@us.ibm.com> and Josh Triplett <josh@freedesktop.org>");

MODULE_ALIAS("rcutorture");
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcutorture."

static int fqs_duration;
module_param(fqs_duration, int, 0444);
MODULE_PARM_DESC(fqs_duration, "Duration of fqs bursts (us), 0 to disable");
static int fqs_holdoff;
module_param(fqs_holdoff, int, 0444);
MODULE_PARM_DESC(fqs_holdoff, "Holdoff time within fqs bursts (us)");
static int fqs_stutter = 3;
module_param(fqs_stutter, int, 0444);
MODULE_PARM_DESC(fqs_stutter, "Wait time between fqs bursts (s)");
static bool gp_exp;
module_param(gp_exp, bool, 0444);
MODULE_PARM_DESC(gp_exp, "Use expedited GP wait primitives");
static bool gp_normal;
module_param(gp_normal, bool, 0444);
MODULE_PARM_DESC(gp_normal, "Use normal (non-expedited) GP wait primitives");
static int irqreader = 1;
module_param(irqreader, int, 0444);
MODULE_PARM_DESC(irqreader, "Allow RCU readers from irq handlers");
static int n_barrier_cbs;
module_param(n_barrier_cbs, int, 0444);
MODULE_PARM_DESC(n_barrier_cbs, "# of callbacks/kthreads for barrier testing");
static int nfakewriters = 4;
module_param(nfakewriters, int, 0444);
MODULE_PARM_DESC(nfakewriters, "Number of RCU fake writer threads");
static int nreaders = -1;
module_param(nreaders, int, 0444);
MODULE_PARM_DESC(nreaders, "Number of RCU reader threads");
static int object_debug;
module_param(object_debug, int, 0444);
MODULE_PARM_DESC(object_debug, "Enable debug-object double call_rcu() testing");
static int onoff_holdoff;
module_param(onoff_holdoff, int, 0444);
MODULE_PARM_DESC(onoff_holdoff, "Time after boot before CPU hotplugs (s)");
static int onoff_interval;
module_param(onoff_interval, int, 0444);
MODULE_PARM_DESC(onoff_interval, "Time between CPU hotplugs (s), 0=disable");
static int shuffle_interval = 3;
module_param(shuffle_interval, int, 0444);
MODULE_PARM_DESC(shuffle_interval, "Number of seconds between shuffles");
static int shutdown_secs;
module_param(shutdown_secs, int, 0444);
MODULE_PARM_DESC(shutdown_secs, "Shutdown time (s), <= zero to disable.");
static int stall_cpu;
module_param(stall_cpu, int, 0444);
MODULE_PARM_DESC(stall_cpu, "Stall duration (s), zero to disable.");
static int stall_cpu_holdoff = 10;
module_param(stall_cpu_holdoff, int, 0444);
MODULE_PARM_DESC(stall_cpu_holdoff, "Time to wait before starting stall (s).");
static int stat_interval = 60;
module_param(stat_interval, int, 0644);
MODULE_PARM_DESC(stat_interval, "Number of seconds between stats printk()s");
static int stutter = 5;
module_param(stutter, int, 0444);
MODULE_PARM_DESC(stutter, "Number of seconds to run/halt test");
static int test_boost = 1;
module_param(test_boost, int, 0444);
MODULE_PARM_DESC(test_boost, "Test RCU prio boost: 0=no, 1=maybe, 2=yes.");
static int test_boost_duration = 4;
module_param(test_boost_duration, int, 0444);
MODULE_PARM_DESC(test_boost_duration, "Duration of each boost test, seconds.");
static int test_boost_interval = 7;
module_param(test_boost_interval, int, 0444);
MODULE_PARM_DESC(test_boost_interval, "Interval between boost tests, seconds.");
static bool test_no_idle_hz = true;
module_param(test_no_idle_hz, bool, 0444);
MODULE_PARM_DESC(test_no_idle_hz, "Test support for tickless idle CPUs");
static char *torture_type = "rcu";
module_param(torture_type, charp, 0444);
MODULE_PARM_DESC(torture_type, "Type of RCU to torture (rcu, rcu_bh, ...)");
static bool verbose;
module_param(verbose, bool, 0444);
MODULE_PARM_DESC(verbose, "Enable verbose debugging printk()s");

#define TORTURE_FLAG "-torture:"
#define PRINTK_STRING(s) \
	do { pr_alert("%s" TORTURE_FLAG s "\n", torture_type); } while (0)
#define VERBOSE_PRINTK_STRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG s "\n", torture_type); } while (0)
#define VERBOSE_PRINTK_ERRSTRING(s) \
	do { if (verbose) pr_alert("%s" TORTURE_FLAG "!!! " s "\n", torture_type); } while (0)

static char printk_buf[4096];

static int nrealreaders;
static struct task_struct *writer_task;
static struct task_struct **fakewriter_tasks;
static struct task_struct **reader_tasks;
static struct task_struct *stats_task;
static struct task_struct *shuffler_task;
static struct task_struct *stutter_task;
static struct task_struct *fqs_task;
static struct task_struct *boost_tasks[NR_CPUS];
static struct task_struct *shutdown_task;
#ifdef CONFIG_HOTPLUG_CPU
static struct task_struct *onoff_task;
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
static struct task_struct *stall_task;
static struct task_struct **barrier_cbs_tasks;
static struct task_struct *barrier_task;

#define RCU_TORTURE_PIPE_LEN 10

struct rcu_torture {
	struct rcu_head rtort_rcu;
	int rtort_pipe_count;
	struct list_head rtort_free;
	int rtort_mbtest;
};

static LIST_HEAD(rcu_torture_freelist);
static struct rcu_torture __rcu *rcu_torture_current;
static unsigned long rcu_torture_current_version;
static struct rcu_torture rcu_tortures[10 * RCU_TORTURE_PIPE_LEN];
static DEFINE_SPINLOCK(rcu_torture_lock);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_count) =
	{ 0 };
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_batch) =
	{ 0 };
static atomic_t rcu_torture_wcount[RCU_TORTURE_PIPE_LEN + 1];
static atomic_t n_rcu_torture_alloc;
static atomic_t n_rcu_torture_alloc_fail;
static atomic_t n_rcu_torture_free;
static atomic_t n_rcu_torture_mberror;
static atomic_t n_rcu_torture_error;
static long n_rcu_torture_barrier_error;
static long n_rcu_torture_boost_ktrerror;
static long n_rcu_torture_boost_rterror;
static long n_rcu_torture_boost_failure;
static long n_rcu_torture_boosts;
static long n_rcu_torture_timers;
static long n_offline_attempts;
static long n_offline_successes;
static unsigned long sum_offline;
static int min_offline = -1;
static int max_offline;
static long n_online_attempts;
static long n_online_successes;
static unsigned long sum_online;
static int min_online = -1;
static int max_online;
static long n_barrier_attempts;
static long n_barrier_successes;
static struct list_head rcu_torture_removed;
static cpumask_var_t shuffle_tmp_mask;

static int stutter_pause_test;

#if defined(MODULE) || defined(CONFIG_RCU_TORTURE_TEST_RUNNABLE)
#define RCUTORTURE_RUNNABLE_INIT 1
#else
#define RCUTORTURE_RUNNABLE_INIT 0
#endif
int rcutorture_runnable = RCUTORTURE_RUNNABLE_INIT;
module_param(rcutorture_runnable, int, 0444);
MODULE_PARM_DESC(rcutorture_runnable, "Start rcutorture at boot");

#if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU)
#define rcu_can_boost() 1
#else /* #if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU) */
#define rcu_can_boost() 0
#endif /* #else #if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU) */

#ifdef CONFIG_RCU_TRACE
static u64 notrace rcu_trace_clock_local(void)
{
	u64 ts = trace_clock_local();
	unsigned long __maybe_unused ts_rem = do_div(ts, NSEC_PER_USEC);
	return ts;
}
#else /* #ifdef CONFIG_RCU_TRACE */
static u64 notrace rcu_trace_clock_local(void)
{
	return 0ULL;
}
#endif /* #else #ifdef CONFIG_RCU_TRACE */

static unsigned long shutdown_time;	/* jiffies to system shutdown. */
static unsigned long boost_starttime;	/* jiffies of next boost test start. */
DEFINE_MUTEX(boost_mutex);		/* protect setting boost_starttime */
					/*  and boost task create/destroy. */
static atomic_t barrier_cbs_count;	/* Barrier callbacks registered. */
static bool barrier_phase;		/* Test phase. */
static atomic_t barrier_cbs_invoked;	/* Barrier callbacks invoked. */
static wait_queue_head_t *barrier_cbs_wq; /* Coordinate barrier testing. */
static DECLARE_WAIT_QUEUE_HEAD(barrier_wq);

/* Mediate rmmod and system shutdown.  Concurrent rmmod & shutdown illegal! */

#define FULLSTOP_DONTSTOP 0	/* Normal operation. */
#define FULLSTOP_SHUTDOWN 1	/* System shutdown with rcutorture running. */
#define FULLSTOP_RMMOD    2	/* Normal rmmod of rcutorture. */
static int fullstop = FULLSTOP_RMMOD;
/*
 * Protect fullstop transitions and spawning of kthreads.
 */
static DEFINE_MUTEX(fullstop_mutex);

/* Forward reference. */
static void rcu_torture_cleanup(void);

/*
 * Detect and respond to a system shutdown.
 */
static int
rcutorture_shutdown_notify(struct notifier_block *unused1,
			   unsigned long unused2, void *unused3)
{
	mutex_lock(&fullstop_mutex);
	if (fullstop == FULLSTOP_DONTSTOP)
		fullstop = FULLSTOP_SHUTDOWN;
	else
		pr_warn(/* but going down anyway, so... */
		       "Concurrent 'rmmod rcutorture' and shutdown illegal!\n");
	mutex_unlock(&fullstop_mutex);
	return NOTIFY_DONE;
}

/*
 * Absorb kthreads into a kernel function that won't return, so that
 * they won't ever access module text or data again.
 */
static void rcutorture_shutdown_absorb(const char *title)
{
	if (ACCESS_ONCE(fullstop) == FULLSTOP_SHUTDOWN) {
		pr_notice(
		       "rcutorture thread %s parking due to system shutdown\n",
		       title);
		schedule_timeout_uninterruptible(MAX_SCHEDULE_TIMEOUT);
	}
}

/*
 * Allocate an element from the rcu_tortures pool.
 */
static struct rcu_torture *
rcu_torture_alloc(void)
{
	struct list_head *p;

	spin_lock_bh(&rcu_torture_lock);
	if (list_empty(&rcu_torture_freelist)) {
		atomic_inc(&n_rcu_torture_alloc_fail);
		spin_unlock_bh(&rcu_torture_lock);
		return NULL;
	}
	atomic_inc(&n_rcu_torture_alloc);
	p = rcu_torture_freelist.next;
	list_del_init(p);
	spin_unlock_bh(&rcu_torture_lock);
	return container_of(p, struct rcu_torture, rtort_free);
}

/*
 * Free an element to the rcu_tortures pool.
 */
static void
rcu_torture_free(struct rcu_torture *p)
{
	atomic_inc(&n_rcu_torture_free);
	spin_lock_bh(&rcu_torture_lock);
	list_add_tail(&p->rtort_free, &rcu_torture_freelist);
	spin_unlock_bh(&rcu_torture_lock);
}

struct rcu_random_state {
	unsigned long rrs_state;
	long rrs_count;
};

#define RCU_RANDOM_MULT 39916801  /* prime */
#define RCU_RANDOM_ADD	479001701 /* prime */
#define RCU_RANDOM_REFRESH 10000

#define DEFINE_RCU_RANDOM(name) struct rcu_random_state name = { 0, 0 }

/*
 * Crude but fast random-number generator.  Uses a linear congruential
 * generator, with occasional help from cpu_clock().
 */
static unsigned long
rcu_random(struct rcu_random_state *rrsp)
{
	if (--rrsp->rrs_count < 0) {
		rrsp->rrs_state += (unsigned long)local_clock();
		rrsp->rrs_count = RCU_RANDOM_REFRESH;
	}
	rrsp->rrs_state = rrsp->rrs_state * RCU_RANDOM_MULT + RCU_RANDOM_ADD;
	return swahw32(rrsp->rrs_state);
}

static void
rcu_stutter_wait(const char *title)
{
	while (stutter_pause_test || !rcutorture_runnable) {
		if (rcutorture_runnable)
			schedule_timeout_interruptible(1);
		else
			schedule_timeout_interruptible(round_jiffies_relative(HZ));
		rcutorture_shutdown_absorb(title);
	}
}

/*
 * Operations vector for selecting different types of tests.
 */

struct rcu_torture_ops {
	void (*init)(void);
	int (*readlock)(void);
	void (*read_delay)(struct rcu_random_state *rrsp);
	void (*readunlock)(int idx);
	int (*completed)(void);
	void (*deferred_free)(struct rcu_torture *p);
	void (*sync)(void);
	void (*exp_sync)(void);
	void (*call)(struct rcu_head *head, void (*func)(struct rcu_head *rcu));
	void (*cb_barrier)(void);
	void (*fqs)(void);
	int (*stats)(char *page);
	int irq_capable;
	int can_boost;
	const char *name;
};

static struct rcu_torture_ops *cur_ops;

/*
 * Definitions for rcu torture testing.
 */

static int rcu_torture_read_lock(void) __acquires(RCU)
{
	rcu_read_lock();
	return 0;
}

static void rcu_read_delay(struct rcu_random_state *rrsp)
{
	const unsigned long shortdelay_us = 200;
	const unsigned long longdelay_ms = 50;

	/* We want a short delay sometimes to make a reader delay the grace
	 * period, and we want a long delay occasionally to trigger
	 * force_quiescent_state. */

	if (!(rcu_random(rrsp) % (nrealreaders * 2000 * longdelay_ms)))
		mdelay(longdelay_ms);
	if (!(rcu_random(rrsp) % (nrealreaders * 2 * shortdelay_us)))
		udelay(shortdelay_us);
#ifdef CONFIG_PREEMPT
	if (!preempt_count() && !(rcu_random(rrsp) % (nrealreaders * 20000)))
		preempt_schedule();  /* No QS if preempt_disable() in effect */
#endif
}

static void rcu_torture_read_unlock(int idx) __releases(RCU)
{
	rcu_read_unlock();
}

static int rcu_torture_completed(void)
{
	return rcu_batches_completed();
}

static void
rcu_torture_cb(struct rcu_head *p)
{
	int i;
	struct rcu_torture *rp = container_of(p, struct rcu_torture, rtort_rcu);

	if (fullstop != FULLSTOP_DONTSTOP) {
		/* Test is ending, just drop callbacks on the floor. */
		/* The next initialization will pick up the pieces. */
		return;
	}
	i = rp->rtort_pipe_count;
	if (i > RCU_TORTURE_PIPE_LEN)
		i = RCU_TORTURE_PIPE_LEN;
	atomic_inc(&rcu_torture_wcount[i]);
	if (++rp->rtort_pipe_count >= RCU_TORTURE_PIPE_LEN) {
		rp->rtort_mbtest = 0;
		rcu_torture_free(rp);
	} else {
		cur_ops->deferred_free(rp);
	}
}

static int rcu_no_completed(void)
{
	return 0;
}

static void rcu_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu(&p->rtort_rcu, rcu_torture_cb);
}

static void rcu_sync_torture_init(void)
{
	INIT_LIST_HEAD(&rcu_torture_removed);
}

static struct rcu_torture_ops rcu_ops = {
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock,
	.read_delay	= rcu_read_delay,
	.readunlock	= rcu_torture_read_unlock,
	.completed	= rcu_torture_completed,
	.deferred_free	= rcu_torture_deferred_free,
	.sync		= synchronize_rcu,
	.exp_sync	= synchronize_rcu_expedited,
	.call		= call_rcu,
	.cb_barrier	= rcu_barrier,
	.fqs		= rcu_force_quiescent_state,
	.stats		= NULL,
	.irq_capable	= 1,
	.can_boost	= rcu_can_boost(),
	.name		= "rcu"
};

/*
 * Definitions for rcu_bh torture testing.
 */

static int rcu_bh_torture_read_lock(void) __acquires(RCU_BH)
{
	rcu_read_lock_bh();
	return 0;
}

static void rcu_bh_torture_read_unlock(int idx) __releases(RCU_BH)
{
	rcu_read_unlock_bh();
}

static int rcu_bh_torture_completed(void)
{
	return rcu_batches_completed_bh();
}

static void rcu_bh_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu_bh(&p->rtort_rcu, rcu_torture_cb);
}

static struct rcu_torture_ops rcu_bh_ops = {
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_bh_torture_read_lock,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= rcu_bh_torture_read_unlock,
	.completed	= rcu_bh_torture_completed,
	.deferred_free	= rcu_bh_torture_deferred_free,
	.sync		= synchronize_rcu_bh,
	.exp_sync	= synchronize_rcu_bh_expedited,
	.call		= call_rcu_bh,
	.cb_barrier	= rcu_barrier_bh,
	.fqs		= rcu_bh_force_quiescent_state,
	.stats		= NULL,
	.irq_capable	= 1,
	.name		= "rcu_bh"
};

/*
 * Definitions for srcu torture testing.
 */

DEFINE_STATIC_SRCU(srcu_ctl);

static int srcu_torture_read_lock(void) __acquires(&srcu_ctl)
{
	return srcu_read_lock(&srcu_ctl);
}

static void srcu_read_delay(struct rcu_random_state *rrsp)
{
	long delay;
	const long uspertick = 1000000 / HZ;
	const long longdelay = 10;

	/* We want there to be long-running readers, but not all the time. */

	delay = rcu_random(rrsp) % (nrealreaders * 2 * longdelay * uspertick);
	if (!delay)
		schedule_timeout_interruptible(longdelay);
	else
		rcu_read_delay(rrsp);
}

static void srcu_torture_read_unlock(int idx) __releases(&srcu_ctl)
{
	srcu_read_unlock(&srcu_ctl, idx);
}

static int srcu_torture_completed(void)
{
	return srcu_batches_completed(&srcu_ctl);
}

static void srcu_torture_deferred_free(struct rcu_torture *rp)
{
	call_srcu(&srcu_ctl, &rp->rtort_rcu, rcu_torture_cb);
}

static void srcu_torture_synchronize(void)
{
	synchronize_srcu(&srcu_ctl);
}

static void srcu_torture_call(struct rcu_head *head,
			      void (*func)(struct rcu_head *head))
{
	call_srcu(&srcu_ctl, head, func);
}

static void srcu_torture_barrier(void)
{
	srcu_barrier(&srcu_ctl);
}

static int srcu_torture_stats(char *page)
{
	int cnt = 0;
	int cpu;
	int idx = srcu_ctl.completed & 0x1;

	cnt += sprintf(&page[cnt], "%s%s per-CPU(idx=%d):",
		       torture_type, TORTURE_FLAG, idx);
	for_each_possible_cpu(cpu) {
		cnt += sprintf(&page[cnt], " %d(%lu,%lu)", cpu,
			       per_cpu_ptr(srcu_ctl.per_cpu_ref, cpu)->c[!idx],
			       per_cpu_ptr(srcu_ctl.per_cpu_ref, cpu)->c[idx]);
	}
	cnt += sprintf(&page[cnt], "\n");
	return cnt;
}

static void srcu_torture_synchronize_expedited(void)
{
	synchronize_srcu_expedited(&srcu_ctl);
}

static struct rcu_torture_ops srcu_ops = {
	.init		= rcu_sync_torture_init,
	.readlock	= srcu_torture_read_lock,
	.read_delay	= srcu_read_delay,
	.readunlock	= srcu_torture_read_unlock,
	.completed	= srcu_torture_completed,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.name		= "srcu"
};

/*
 * Definitions for sched torture testing.
 */

static int sched_torture_read_lock(void)
{
	preempt_disable();
	return 0;
}

static void sched_torture_read_unlock(int idx)
{
	preempt_enable();
}

static void rcu_sched_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu_sched(&p->rtort_rcu, rcu_torture_cb);
}

static struct rcu_torture_ops sched_ops = {
	.init		= rcu_sync_torture_init,
	.readlock	= sched_torture_read_lock,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= sched_torture_read_unlock,
	.completed	= rcu_no_completed,
	.deferred_free	= rcu_sched_torture_deferred_free,
	.sync		= synchronize_sched,
	.exp_sync	= synchronize_sched_expedited,
	.call		= call_rcu_sched,
	.cb_barrier	= rcu_barrier_sched,
	.fqs		= rcu_sched_force_quiescent_state,
	.stats		= NULL,
	.irq_capable	= 1,
	.name		= "sched"
};

/*
 * RCU torture priority-boost testing.  Runs one real-time thread per
 * CPU for moderate bursts, repeatedly registering RCU callbacks and
 * spinning waiting for them to be invoked.  If a given callback takes
 * too long to be invoked, we assume that priority inversion has occurred.
 */

struct rcu_boost_inflight {
	struct rcu_head rcu;
	int inflight;
};

static void rcu_torture_boost_cb(struct rcu_head *head)
{
	struct rcu_boost_inflight *rbip =
		container_of(head, struct rcu_boost_inflight, rcu);

	smp_mb(); /* Ensure RCU-core accesses precede clearing ->inflight */
	rbip->inflight = 0;
}

static int rcu_torture_boost(void *arg)
{
	unsigned long call_rcu_time;
	unsigned long endtime;
	unsigned long oldstarttime;
	struct rcu_boost_inflight rbi = { .inflight = 0 };
	struct sched_param sp;

	VERBOSE_PRINTK_STRING("rcu_torture_boost started");

	/* Set real-time priority. */
	sp.sched_priority = 1;
	if (sched_setscheduler(current, SCHED_FIFO, &sp) < 0) {
		VERBOSE_PRINTK_STRING("rcu_torture_boost RT prio failed!");
		n_rcu_torture_boost_rterror++;
	}

	init_rcu_head_on_stack(&rbi.rcu);
	/* Each pass through the following loop does one boost-test cycle. */
	do {
		/* Wait for the next test interval. */
		oldstarttime = boost_starttime;
		while (ULONG_CMP_LT(jiffies, oldstarttime)) {
			schedule_timeout_interruptible(oldstarttime - jiffies);
			rcu_stutter_wait("rcu_torture_boost");
			if (kthread_should_stop() ||
			    fullstop != FULLSTOP_DONTSTOP)
				goto checkwait;
		}

		/* Do one boost-test interval. */
		endtime = oldstarttime + test_boost_duration * HZ;
		call_rcu_time = jiffies;
		while (ULONG_CMP_LT(jiffies, endtime)) {
			/* If we don't have a callback in flight, post one. */
			if (!rbi.inflight) {
				smp_mb(); /* RCU core before ->inflight = 1. */
				rbi.inflight = 1;
				call_rcu(&rbi.rcu, rcu_torture_boost_cb);
				if (jiffies - call_rcu_time >
					 test_boost_duration * HZ - HZ / 2) {
					VERBOSE_PRINTK_STRING("rcu_torture_boost boosting failed");
					n_rcu_torture_boost_failure++;
				}
				call_rcu_time = jiffies;
			}
			cond_resched();
			rcu_stutter_wait("rcu_torture_boost");
			if (kthread_should_stop() ||
			    fullstop != FULLSTOP_DONTSTOP)
				goto checkwait;
		}

		/*
		 * Set the start time of the next test interval.
		 * Yes, this is vulnerable to long delays, but such
		 * delays simply cause a false negative for the next
		 * interval.  Besides, we are running at RT priority,
		 * so delays should be relatively rare.
		 */
		while (oldstarttime == boost_starttime &&
		       !kthread_should_stop()) {
			if (mutex_trylock(&boost_mutex)) {
				boost_starttime = jiffies +
						  test_boost_interval * HZ;
				n_rcu_torture_boosts++;
				mutex_unlock(&boost_mutex);
				break;
			}
			schedule_timeout_uninterruptible(1);
		}

		/* Go do the stutter. */
checkwait:	rcu_stutter_wait("rcu_torture_boost");
	} while (!kthread_should_stop() && fullstop  == FULLSTOP_DONTSTOP);

	/* Clean up and exit. */
	VERBOSE_PRINTK_STRING("rcu_torture_boost task stopping");
	rcutorture_shutdown_absorb("rcu_torture_boost");
	while (!kthread_should_stop() || rbi.inflight)
		schedule_timeout_uninterruptible(1);
	smp_mb(); /* order accesses to ->inflight before stack-frame death. */
	destroy_rcu_head_on_stack(&rbi.rcu);
	return 0;
}

/*
 * RCU torture force-quiescent-state kthread.  Repeatedly induces
 * bursts of calls to force_quiescent_state(), increasing the probability
 * of occurrence of some important types of race conditions.
 */
static int
rcu_torture_fqs(void *arg)
{
	unsigned long fqs_resume_time;
	int fqs_burst_remaining;

	VERBOSE_PRINTK_STRING("rcu_torture_fqs task started");
	do {
		fqs_resume_time = jiffies + fqs_stutter * HZ;
		while (ULONG_CMP_LT(jiffies, fqs_resume_time) &&
		       !kthread_should_stop()) {
			schedule_timeout_interruptible(1);
		}
		fqs_burst_remaining = fqs_duration;
		while (fqs_burst_remaining > 0 &&
		       !kthread_should_stop()) {
			cur_ops->fqs();
			udelay(fqs_holdoff);
			fqs_burst_remaining -= fqs_holdoff;
		}
		rcu_stutter_wait("rcu_torture_fqs");
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);
	VERBOSE_PRINTK_STRING("rcu_torture_fqs task stopping");
	rcutorture_shutdown_absorb("rcu_torture_fqs");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

/*
 * RCU torture writer kthread.  Repeatedly substitutes a new structure
 * for that pointed to by rcu_torture_current, freeing the old structure
 * after a series of grace periods (the "pipeline").
 */
static int
rcu_torture_writer(void *arg)
{
	bool exp;
	int i;
	struct rcu_torture *rp;
	struct rcu_torture *rp1;
	struct rcu_torture *old_rp;
	static DEFINE_RCU_RANDOM(rand);

	VERBOSE_PRINTK_STRING("rcu_torture_writer task started");
	set_user_nice(current, 19);

	do {
		schedule_timeout_uninterruptible(1);
		rp = rcu_torture_alloc();
		if (rp == NULL)
			continue;
		rp->rtort_pipe_count = 0;
		udelay(rcu_random(&rand) & 0x3ff);
		old_rp = rcu_dereference_check(rcu_torture_current,
					       current == writer_task);
		rp->rtort_mbtest = 1;
		rcu_assign_pointer(rcu_torture_current, rp);
		smp_wmb(); /* Mods to old_rp must follow rcu_assign_pointer() */
		if (old_rp) {
			i = old_rp->rtort_pipe_count;
			if (i > RCU_TORTURE_PIPE_LEN)
				i = RCU_TORTURE_PIPE_LEN;
			atomic_inc(&rcu_torture_wcount[i]);
			old_rp->rtort_pipe_count++;
			if (gp_normal == gp_exp)
				exp = !!(rcu_random(&rand) & 0x80);
			else
				exp = gp_exp;
			if (!exp) {
				cur_ops->deferred_free(old_rp);
			} else {
				cur_ops->exp_sync();
				list_add(&old_rp->rtort_free,
					 &rcu_torture_removed);
				list_for_each_entry_safe(rp, rp1,
							 &rcu_torture_removed,
							 rtort_free) {
					i = rp->rtort_pipe_count;
					if (i > RCU_TORTURE_PIPE_LEN)
						i = RCU_TORTURE_PIPE_LEN;
					atomic_inc(&rcu_torture_wcount[i]);
					if (++rp->rtort_pipe_count >=
					    RCU_TORTURE_PIPE_LEN) {
						rp->rtort_mbtest = 0;
						list_del(&rp->rtort_free);
						rcu_torture_free(rp);
					}
				 }
			}
		}
		rcutorture_record_progress(++rcu_torture_current_version);
		rcu_stutter_wait("rcu_torture_writer");
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);
	VERBOSE_PRINTK_STRING("rcu_torture_writer task stopping");
	rcutorture_shutdown_absorb("rcu_torture_writer");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

/*
 * RCU torture fake writer kthread.  Repeatedly calls sync, with a random
 * delay between calls.
 */
static int
rcu_torture_fakewriter(void *arg)
{
	DEFINE_RCU_RANDOM(rand);

	VERBOSE_PRINTK_STRING("rcu_torture_fakewriter task started");
	set_user_nice(current, 19);

	do {
		schedule_timeout_uninterruptible(1 + rcu_random(&rand)%10);
		udelay(rcu_random(&rand) & 0x3ff);
		if (cur_ops->cb_barrier != NULL &&
		    rcu_random(&rand) % (nfakewriters * 8) == 0) {
			cur_ops->cb_barrier();
		} else if (gp_normal == gp_exp) {
			if (rcu_random(&rand) & 0x80)
				cur_ops->sync();
			else
				cur_ops->exp_sync();
		} else if (gp_normal) {
			cur_ops->sync();
		} else {
			cur_ops->exp_sync();
		}
		rcu_stutter_wait("rcu_torture_fakewriter");
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);

	VERBOSE_PRINTK_STRING("rcu_torture_fakewriter task stopping");
	rcutorture_shutdown_absorb("rcu_torture_fakewriter");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

void rcutorture_trace_dump(void)
{
	static atomic_t beenhere = ATOMIC_INIT(0);

	if (atomic_read(&beenhere))
		return;
	if (atomic_xchg(&beenhere, 1) != 0)
		return;
	ftrace_dump(DUMP_ALL);
}

/*
 * RCU torture reader from timer handler.  Dereferences rcu_torture_current,
 * incrementing the corresponding element of the pipeline array.  The
 * counter in the element should never be greater than 1, otherwise, the
 * RCU implementation is broken.
 */
static void rcu_torture_timer(unsigned long unused)
{
	int idx;
	int completed;
	int completed_end;
	static DEFINE_RCU_RANDOM(rand);
	static DEFINE_SPINLOCK(rand_lock);
	struct rcu_torture *p;
	int pipe_count;
	unsigned long long ts;

	idx = cur_ops->readlock();
	completed = cur_ops->completed();
	ts = rcu_trace_clock_local();
	p = rcu_dereference_check(rcu_torture_current,
				  rcu_read_lock_bh_held() ||
				  rcu_read_lock_sched_held() ||
				  srcu_read_lock_held(&srcu_ctl));
	if (p == NULL) {
		/* Leave because rcu_torture_writer is not yet underway */
		cur_ops->readunlock(idx);
		return;
	}
	if (p->rtort_mbtest == 0)
		atomic_inc(&n_rcu_torture_mberror);
	spin_lock(&rand_lock);
	cur_ops->read_delay(&rand);
	n_rcu_torture_timers++;
	spin_unlock(&rand_lock);
	preempt_disable();
	pipe_count = p->rtort_pipe_count;
	if (pipe_count > RCU_TORTURE_PIPE_LEN) {
		/* Should not happen, but... */
		pipe_count = RCU_TORTURE_PIPE_LEN;
	}
	completed_end = cur_ops->completed();
	if (pipe_count > 1) {
		do_trace_rcu_torture_read(cur_ops->name, &p->rtort_rcu, ts,
					  completed, completed_end);
		rcutorture_trace_dump();
	}
	__this_cpu_inc(rcu_torture_count[pipe_count]);
	completed = completed_end - completed;
	if (completed > RCU_TORTURE_PIPE_LEN) {
		/* Should not happen, but... */
		completed = RCU_TORTURE_PIPE_LEN;
	}
	__this_cpu_inc(rcu_torture_batch[completed]);
	preempt_enable();
	cur_ops->readunlock(idx);
}

/*
 * RCU torture reader kthread.  Repeatedly dereferences rcu_torture_current,
 * incrementing the corresponding element of the pipeline array.  The
 * counter in the element should never be greater than 1, otherwise, the
 * RCU implementation is broken.
 */
static int
rcu_torture_reader(void *arg)
{
	int completed;
	int completed_end;
	int idx;
	DEFINE_RCU_RANDOM(rand);
	struct rcu_torture *p;
	int pipe_count;
	struct timer_list t;
	unsigned long long ts;

	VERBOSE_PRINTK_STRING("rcu_torture_reader task started");
	set_user_nice(current, 19);
	if (irqreader && cur_ops->irq_capable)
		setup_timer_on_stack(&t, rcu_torture_timer, 0);

	do {
		if (irqreader && cur_ops->irq_capable) {
			if (!timer_pending(&t))
				mod_timer(&t, jiffies + 1);
		}
		idx = cur_ops->readlock();
		completed = cur_ops->completed();
		ts = rcu_trace_clock_local();
		p = rcu_dereference_check(rcu_torture_current,
					  rcu_read_lock_bh_held() ||
					  rcu_read_lock_sched_held() ||
					  srcu_read_lock_held(&srcu_ctl));
		if (p == NULL) {
			/* Wait for rcu_torture_writer to get underway */
			cur_ops->readunlock(idx);
			schedule_timeout_interruptible(HZ);
			continue;
		}
		if (p->rtort_mbtest == 0)
			atomic_inc(&n_rcu_torture_mberror);
		cur_ops->read_delay(&rand);
		preempt_disable();
		pipe_count = p->rtort_pipe_count;
		if (pipe_count > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			pipe_count = RCU_TORTURE_PIPE_LEN;
		}
		completed_end = cur_ops->completed();
		if (pipe_count > 1) {
			do_trace_rcu_torture_read(cur_ops->name, &p->rtort_rcu,
						  ts, completed, completed_end);
			rcutorture_trace_dump();
		}
		__this_cpu_inc(rcu_torture_count[pipe_count]);
		completed = completed_end - completed;
		if (completed > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			completed = RCU_TORTURE_PIPE_LEN;
		}
		__this_cpu_inc(rcu_torture_batch[completed]);
		preempt_enable();
		cur_ops->readunlock(idx);
		schedule();
		rcu_stutter_wait("rcu_torture_reader");
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);
	VERBOSE_PRINTK_STRING("rcu_torture_reader task stopping");
	rcutorture_shutdown_absorb("rcu_torture_reader");
	if (irqreader && cur_ops->irq_capable)
		del_timer_sync(&t);
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

/*
 * Create an RCU-torture statistics message in the specified buffer.
 */
static int
rcu_torture_printk(char *page)
{
	int cnt = 0;
	int cpu;
	int i;
	long pipesummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };
	long batchsummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };

	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			pipesummary[i] += per_cpu(rcu_torture_count, cpu)[i];
			batchsummary[i] += per_cpu(rcu_torture_batch, cpu)[i];
		}
	}
	for (i = RCU_TORTURE_PIPE_LEN - 1; i >= 0; i--) {
		if (pipesummary[i] != 0)
			break;
	}
	cnt += sprintf(&page[cnt], "%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt],
		       "rtc: %p ver: %lu tfle: %d rta: %d rtaf: %d rtf: %d ",
		       rcu_torture_current,
		       rcu_torture_current_version,
		       list_empty(&rcu_torture_freelist),
		       atomic_read(&n_rcu_torture_alloc),
		       atomic_read(&n_rcu_torture_alloc_fail),
		       atomic_read(&n_rcu_torture_free));
	cnt += sprintf(&page[cnt], "rtmbe: %d rtbke: %ld rtbre: %ld ",
		       atomic_read(&n_rcu_torture_mberror),
		       n_rcu_torture_boost_ktrerror,
		       n_rcu_torture_boost_rterror);
	cnt += sprintf(&page[cnt], "rtbf: %ld rtb: %ld nt: %ld ",
		       n_rcu_torture_boost_failure,
		       n_rcu_torture_boosts,
		       n_rcu_torture_timers);
	cnt += sprintf(&page[cnt],
		       "onoff: %ld/%ld:%ld/%ld %d,%d:%d,%d %lu:%lu (HZ=%d) ",
		       n_online_successes, n_online_attempts,
		       n_offline_successes, n_offline_attempts,
		       min_online, max_online,
		       min_offline, max_offline,
		       sum_online, sum_offline, HZ);
	cnt += sprintf(&page[cnt], "barrier: %ld/%ld:%ld",
		       n_barrier_successes,
		       n_barrier_attempts,
		       n_rcu_torture_barrier_error);
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	if (atomic_read(&n_rcu_torture_mberror) != 0 ||
	    n_rcu_torture_barrier_error != 0 ||
	    n_rcu_torture_boost_ktrerror != 0 ||
	    n_rcu_torture_boost_rterror != 0 ||
	    n_rcu_torture_boost_failure != 0 ||
	    i > 1) {
		cnt += sprintf(&page[cnt], "!!! ");
		atomic_inc(&n_rcu_torture_error);
		WARN_ON_ONCE(1);
	}
	cnt += sprintf(&page[cnt], "Reader Pipe: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		cnt += sprintf(&page[cnt], " %ld", pipesummary[i]);
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt], "Reader Batch: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		cnt += sprintf(&page[cnt], " %ld", batchsummary[i]);
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt], "Free-Block Circulation: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
		cnt += sprintf(&page[cnt], " %d",
			       atomic_read(&rcu_torture_wcount[i]));
	}
	cnt += sprintf(&page[cnt], "\n");
	if (cur_ops->stats)
		cnt += cur_ops->stats(&page[cnt]);
	return cnt;
}

/*
 * Print torture statistics.  Caller must ensure that there is only
 * one call to this function at a given time!!!  This is normally
 * accomplished by relying on the module system to only have one copy
 * of the module loaded, and then by giving the rcu_torture_stats
 * kthread full control (or the init/cleanup functions when rcu_torture_stats
 * thread is not running).
 */
static void
rcu_torture_stats_print(void)
{
	int cnt;

	cnt = rcu_torture_printk(printk_buf);
	pr_alert("%s", printk_buf);
}

/*
 * Periodically prints torture statistics, if periodic statistics printing
 * was specified via the stat_interval module parameter.
 *
 * No need to worry about fullstop here, since this one doesn't reference
 * volatile state or register callbacks.
 */
static int
rcu_torture_stats(void *arg)
{
	VERBOSE_PRINTK_STRING("rcu_torture_stats task started");
	do {
		schedule_timeout_interruptible(stat_interval * HZ);
		rcu_torture_stats_print();
		rcutorture_shutdown_absorb("rcu_torture_stats");
	} while (!kthread_should_stop());
	VERBOSE_PRINTK_STRING("rcu_torture_stats task stopping");
	return 0;
}

static int rcu_idle_cpu;	/* Force all torture tasks off this CPU */

/* Shuffle tasks such that we allow @rcu_idle_cpu to become idle. A special case
 * is when @rcu_idle_cpu = -1, when we allow the tasks to run on all CPUs.
 */
static void rcu_torture_shuffle_tasks(void)
{
	int i;

	cpumask_setall(shuffle_tmp_mask);
	get_online_cpus();

	/* No point in shuffling if there is only one online CPU (ex: UP) */
	if (num_online_cpus() == 1) {
		put_online_cpus();
		return;
	}

	if (rcu_idle_cpu != -1)
		cpumask_clear_cpu(rcu_idle_cpu, shuffle_tmp_mask);

	set_cpus_allowed_ptr(current, shuffle_tmp_mask);

	if (reader_tasks) {
		for (i = 0; i < nrealreaders; i++)
			if (reader_tasks[i])
				set_cpus_allowed_ptr(reader_tasks[i],
						     shuffle_tmp_mask);
	}
	if (fakewriter_tasks) {
		for (i = 0; i < nfakewriters; i++)
			if (fakewriter_tasks[i])
				set_cpus_allowed_ptr(fakewriter_tasks[i],
						     shuffle_tmp_mask);
	}
	if (writer_task)
		set_cpus_allowed_ptr(writer_task, shuffle_tmp_mask);
	if (stats_task)
		set_cpus_allowed_ptr(stats_task, shuffle_tmp_mask);
	if (stutter_task)
		set_cpus_allowed_ptr(stutter_task, shuffle_tmp_mask);
	if (fqs_task)
		set_cpus_allowed_ptr(fqs_task, shuffle_tmp_mask);
	if (shutdown_task)
		set_cpus_allowed_ptr(shutdown_task, shuffle_tmp_mask);
#ifdef CONFIG_HOTPLUG_CPU
	if (onoff_task)
		set_cpus_allowed_ptr(onoff_task, shuffle_tmp_mask);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
	if (stall_task)
		set_cpus_allowed_ptr(stall_task, shuffle_tmp_mask);
	if (barrier_cbs_tasks)
		for (i = 0; i < n_barrier_cbs; i++)
			if (barrier_cbs_tasks[i])
				set_cpus_allowed_ptr(barrier_cbs_tasks[i],
						     shuffle_tmp_mask);
	if (barrier_task)
		set_cpus_allowed_ptr(barrier_task, shuffle_tmp_mask);

	if (rcu_idle_cpu == -1)
		rcu_idle_cpu = num_online_cpus() - 1;
	else
		rcu_idle_cpu--;

	put_online_cpus();
}

/* Shuffle tasks across CPUs, with the intent of allowing each CPU in the
 * system to become idle at a time and cut off its timer ticks. This is meant
 * to test the support for such tickless idle CPU in RCU.
 */
static int
rcu_torture_shuffle(void *arg)
{
	VERBOSE_PRINTK_STRING("rcu_torture_shuffle task started");
	do {
		schedule_timeout_interruptible(shuffle_interval * HZ);
		rcu_torture_shuffle_tasks();
		rcutorture_shutdown_absorb("rcu_torture_shuffle");
	} while (!kthread_should_stop());
	VERBOSE_PRINTK_STRING("rcu_torture_shuffle task stopping");
	return 0;
}

/* Cause the rcutorture test to "stutter", starting and stopping all
 * threads periodically.
 */
static int
rcu_torture_stutter(void *arg)
{
	VERBOSE_PRINTK_STRING("rcu_torture_stutter task started");
	do {
		schedule_timeout_interruptible(stutter * HZ);
		stutter_pause_test = 1;
		if (!kthread_should_stop())
			schedule_timeout_interruptible(stutter * HZ);
		stutter_pause_test = 0;
		rcutorture_shutdown_absorb("rcu_torture_stutter");
	} while (!kthread_should_stop());
	VERBOSE_PRINTK_STRING("rcu_torture_stutter task stopping");
	return 0;
}

static inline void
rcu_torture_print_module_parms(struct rcu_torture_ops *cur_ops, const char *tag)
{
	pr_alert("%s" TORTURE_FLAG
		 "--- %s: nreaders=%d nfakewriters=%d "
		 "stat_interval=%d verbose=%d test_no_idle_hz=%d "
		 "shuffle_interval=%d stutter=%d irqreader=%d "
		 "fqs_duration=%d fqs_holdoff=%d fqs_stutter=%d "
		 "test_boost=%d/%d test_boost_interval=%d "
		 "test_boost_duration=%d shutdown_secs=%d "
		 "stall_cpu=%d stall_cpu_holdoff=%d "
		 "n_barrier_cbs=%d "
		 "onoff_interval=%d onoff_holdoff=%d\n",
		 torture_type, tag, nrealreaders, nfakewriters,
		 stat_interval, verbose, test_no_idle_hz, shuffle_interval,
		 stutter, irqreader, fqs_duration, fqs_holdoff, fqs_stutter,
		 test_boost, cur_ops->can_boost,
		 test_boost_interval, test_boost_duration, shutdown_secs,
		 stall_cpu, stall_cpu_holdoff,
		 n_barrier_cbs,
		 onoff_interval, onoff_holdoff);
}

static struct notifier_block rcutorture_shutdown_nb = {
	.notifier_call = rcutorture_shutdown_notify,
};

static void rcutorture_booster_cleanup(int cpu)
{
	struct task_struct *t;

	if (boost_tasks[cpu] == NULL)
		return;
	mutex_lock(&boost_mutex);
	VERBOSE_PRINTK_STRING("Stopping rcu_torture_boost task");
	t = boost_tasks[cpu];
	boost_tasks[cpu] = NULL;
	mutex_unlock(&boost_mutex);

	/* This must be outside of the mutex, otherwise deadlock! */
	kthread_stop(t);
	boost_tasks[cpu] = NULL;
}

static int rcutorture_booster_init(int cpu)
{
	int retval;

	if (boost_tasks[cpu] != NULL)
		return 0;  /* Already created, nothing more to do. */

	/* Don't allow time recalculation while creating a new task. */
	mutex_lock(&boost_mutex);
	VERBOSE_PRINTK_STRING("Creating rcu_torture_boost task");
	boost_tasks[cpu] = kthread_create_on_node(rcu_torture_boost, NULL,
						  cpu_to_node(cpu),
						  "rcu_torture_boost");
	if (IS_ERR(boost_tasks[cpu])) {
		retval = PTR_ERR(boost_tasks[cpu]);
		VERBOSE_PRINTK_STRING("rcu_torture_boost task create failed");
		n_rcu_torture_boost_ktrerror++;
		boost_tasks[cpu] = NULL;
		mutex_unlock(&boost_mutex);
		return retval;
	}
	kthread_bind(boost_tasks[cpu], cpu);
	wake_up_process(boost_tasks[cpu]);
	mutex_unlock(&boost_mutex);
	return 0;
}

/*
 * Cause the rcutorture test to shutdown the system after the test has
 * run for the time specified by the shutdown_secs module parameter.
 */
static int
rcu_torture_shutdown(void *arg)
{
	long delta;
	unsigned long jiffies_snap;

	VERBOSE_PRINTK_STRING("rcu_torture_shutdown task started");
	jiffies_snap = ACCESS_ONCE(jiffies);
	while (ULONG_CMP_LT(jiffies_snap, shutdown_time) &&
	       !kthread_should_stop()) {
		delta = shutdown_time - jiffies_snap;
		if (verbose)
			pr_alert("%s" TORTURE_FLAG
				 "rcu_torture_shutdown task: %lu jiffies remaining\n",
				 torture_type, delta);
		schedule_timeout_interruptible(delta);
		jiffies_snap = ACCESS_ONCE(jiffies);
	}
	if (kthread_should_stop()) {
		VERBOSE_PRINTK_STRING("rcu_torture_shutdown task stopping");
		return 0;
	}

	/* OK, shut down the system. */

	VERBOSE_PRINTK_STRING("rcu_torture_shutdown task shutting down system");
	shutdown_task = NULL;	/* Avoid self-kill deadlock. */
	rcu_torture_cleanup();	/* Get the success/failure message. */
	kernel_power_off();	/* Shut down the system. */
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Execute random CPU-hotplug operations at the interval specified
 * by the onoff_interval.
 */
static int
rcu_torture_onoff(void *arg)
{
	int cpu;
	unsigned long delta;
	int maxcpu = -1;
	DEFINE_RCU_RANDOM(rand);
	int ret;
	unsigned long starttime;

	VERBOSE_PRINTK_STRING("rcu_torture_onoff task started");
	for_each_online_cpu(cpu)
		maxcpu = cpu;
	WARN_ON(maxcpu < 0);
	if (onoff_holdoff > 0) {
		VERBOSE_PRINTK_STRING("rcu_torture_onoff begin holdoff");
		schedule_timeout_interruptible(onoff_holdoff * HZ);
		VERBOSE_PRINTK_STRING("rcu_torture_onoff end holdoff");
	}
	while (!kthread_should_stop()) {
		cpu = (rcu_random(&rand) >> 4) % (maxcpu + 1);
		if (cpu_online(cpu) && cpu_is_hotpluggable(cpu)) {
			if (verbose)
				pr_alert("%s" TORTURE_FLAG
					 "rcu_torture_onoff task: offlining %d\n",
					 torture_type, cpu);
			starttime = jiffies;
			n_offline_attempts++;
			ret = cpu_down(cpu);
			if (ret) {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "rcu_torture_onoff task: offline %d failed: errno %d\n",
						 torture_type, cpu, ret);
			} else {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "rcu_torture_onoff task: offlined %d\n",
						 torture_type, cpu);
				n_offline_successes++;
				delta = jiffies - starttime;
				sum_offline += delta;
				if (min_offline < 0) {
					min_offline = delta;
					max_offline = delta;
				}
				if (min_offline > delta)
					min_offline = delta;
				if (max_offline < delta)
					max_offline = delta;
			}
		} else if (cpu_is_hotpluggable(cpu)) {
			if (verbose)
				pr_alert("%s" TORTURE_FLAG
					 "rcu_torture_onoff task: onlining %d\n",
					 torture_type, cpu);
			starttime = jiffies;
			n_online_attempts++;
			ret = cpu_up(cpu);
			if (ret) {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "rcu_torture_onoff task: online %d failed: errno %d\n",
						 torture_type, cpu, ret);
			} else {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "rcu_torture_onoff task: onlined %d\n",
						 torture_type, cpu);
				n_online_successes++;
				delta = jiffies - starttime;
				sum_online += delta;
				if (min_online < 0) {
					min_online = delta;
					max_online = delta;
				}
				if (min_online > delta)
					min_online = delta;
				if (max_online < delta)
					max_online = delta;
			}
		}
		schedule_timeout_interruptible(onoff_interval * HZ);
	}
	VERBOSE_PRINTK_STRING("rcu_torture_onoff task stopping");
	return 0;
}

static int
rcu_torture_onoff_init(void)
{
	int ret;

	if (onoff_interval <= 0)
		return 0;
	onoff_task = kthread_run(rcu_torture_onoff, NULL, "rcu_torture_onoff");
	if (IS_ERR(onoff_task)) {
		ret = PTR_ERR(onoff_task);
		onoff_task = NULL;
		return ret;
	}
	return 0;
}

static void rcu_torture_onoff_cleanup(void)
{
	if (onoff_task == NULL)
		return;
	VERBOSE_PRINTK_STRING("Stopping rcu_torture_onoff task");
	kthread_stop(onoff_task);
	onoff_task = NULL;
}

#else /* #ifdef CONFIG_HOTPLUG_CPU */

static int
rcu_torture_onoff_init(void)
{
	return 0;
}

static void rcu_torture_onoff_cleanup(void)
{
}

#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */

/*
 * CPU-stall kthread.  It waits as specified by stall_cpu_holdoff, then
 * induces a CPU stall for the time specified by stall_cpu.
 */
static int rcu_torture_stall(void *args)
{
	unsigned long stop_at;

	VERBOSE_PRINTK_STRING("rcu_torture_stall task started");
	if (stall_cpu_holdoff > 0) {
		VERBOSE_PRINTK_STRING("rcu_torture_stall begin holdoff");
		schedule_timeout_interruptible(stall_cpu_holdoff * HZ);
		VERBOSE_PRINTK_STRING("rcu_torture_stall end holdoff");
	}
	if (!kthread_should_stop()) {
		stop_at = get_seconds() + stall_cpu;
		/* RCU CPU stall is expected behavior in following code. */
		pr_alert("rcu_torture_stall start.\n");
		rcu_read_lock();
		preempt_disable();
		while (ULONG_CMP_LT(get_seconds(), stop_at))
			continue;  /* Induce RCU CPU stall warning. */
		preempt_enable();
		rcu_read_unlock();
		pr_alert("rcu_torture_stall end.\n");
	}
	rcutorture_shutdown_absorb("rcu_torture_stall");
	while (!kthread_should_stop())
		schedule_timeout_interruptible(10 * HZ);
	return 0;
}

/* Spawn CPU-stall kthread, if stall_cpu specified. */
static int __init rcu_torture_stall_init(void)
{
	int ret;

	if (stall_cpu <= 0)
		return 0;
	stall_task = kthread_run(rcu_torture_stall, NULL, "rcu_torture_stall");
	if (IS_ERR(stall_task)) {
		ret = PTR_ERR(stall_task);
		stall_task = NULL;
		return ret;
	}
	return 0;
}

/* Clean up after the CPU-stall kthread, if one was spawned. */
static void rcu_torture_stall_cleanup(void)
{
	if (stall_task == NULL)
		return;
	VERBOSE_PRINTK_STRING("Stopping rcu_torture_stall_task.");
	kthread_stop(stall_task);
	stall_task = NULL;
}

/* Callback function for RCU barrier testing. */
void rcu_torture_barrier_cbf(struct rcu_head *rcu)
{
	atomic_inc(&barrier_cbs_invoked);
}

/* kthread function to register callbacks used to test RCU barriers. */
static int rcu_torture_barrier_cbs(void *arg)
{
	long myid = (long)arg;
	bool lastphase = 0;
	struct rcu_head rcu;

	init_rcu_head_on_stack(&rcu);
	VERBOSE_PRINTK_STRING("rcu_torture_barrier_cbs task started");
	set_user_nice(current, 19);
	do {
		wait_event(barrier_cbs_wq[myid],
			   barrier_phase != lastphase ||
			   kthread_should_stop() ||
			   fullstop != FULLSTOP_DONTSTOP);
		lastphase = barrier_phase;
		smp_mb(); /* ensure barrier_phase load before ->call(). */
		if (kthread_should_stop() || fullstop != FULLSTOP_DONTSTOP)
			break;
		cur_ops->call(&rcu, rcu_torture_barrier_cbf);
		if (atomic_dec_and_test(&barrier_cbs_count))
			wake_up(&barrier_wq);
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);
	VERBOSE_PRINTK_STRING("rcu_torture_barrier_cbs task stopping");
	rcutorture_shutdown_absorb("rcu_torture_barrier_cbs");
	while (!kthread_should_stop())
		schedule_timeout_interruptible(1);
	cur_ops->cb_barrier();
	destroy_rcu_head_on_stack(&rcu);
	return 0;
}

/* kthread function to drive and coordinate RCU barrier testing. */
static int rcu_torture_barrier(void *arg)
{
	int i;

	VERBOSE_PRINTK_STRING("rcu_torture_barrier task starting");
	do {
		atomic_set(&barrier_cbs_invoked, 0);
		atomic_set(&barrier_cbs_count, n_barrier_cbs);
		smp_mb(); /* Ensure barrier_phase after prior assignments. */
		barrier_phase = !barrier_phase;
		for (i = 0; i < n_barrier_cbs; i++)
			wake_up(&barrier_cbs_wq[i]);
		wait_event(barrier_wq,
			   atomic_read(&barrier_cbs_count) == 0 ||
			   kthread_should_stop() ||
			   fullstop != FULLSTOP_DONTSTOP);
		if (kthread_should_stop() || fullstop != FULLSTOP_DONTSTOP)
			break;
		n_barrier_attempts++;
		cur_ops->cb_barrier();
		if (atomic_read(&barrier_cbs_invoked) != n_barrier_cbs) {
			n_rcu_torture_barrier_error++;
			WARN_ON_ONCE(1);
		}
		n_barrier_successes++;
		schedule_timeout_interruptible(HZ / 10);
	} while (!kthread_should_stop() && fullstop == FULLSTOP_DONTSTOP);
	VERBOSE_PRINTK_STRING("rcu_torture_barrier task stopping");
	rcutorture_shutdown_absorb("rcu_torture_barrier");
	while (!kthread_should_stop())
		schedule_timeout_interruptible(1);
	return 0;
}

/* Initialize RCU barrier testing. */
static int rcu_torture_barrier_init(void)
{
	int i;
	int ret;

	if (n_barrier_cbs == 0)
		return 0;
	if (cur_ops->call == NULL || cur_ops->cb_barrier == NULL) {
		pr_alert("%s" TORTURE_FLAG
			 " Call or barrier ops missing for %s,\n",
			 torture_type, cur_ops->name);
		pr_alert("%s" TORTURE_FLAG
			 " RCU barrier testing omitted from run.\n",
			 torture_type);
		return 0;
	}
	atomic_set(&barrier_cbs_count, 0);
	atomic_set(&barrier_cbs_invoked, 0);
	barrier_cbs_tasks =
		kzalloc(n_barrier_cbs * sizeof(barrier_cbs_tasks[0]),
			GFP_KERNEL);
	barrier_cbs_wq =
		kzalloc(n_barrier_cbs * sizeof(barrier_cbs_wq[0]),
			GFP_KERNEL);
	if (barrier_cbs_tasks == NULL || !barrier_cbs_wq)
		return -ENOMEM;
	for (i = 0; i < n_barrier_cbs; i++) {
		init_waitqueue_head(&barrier_cbs_wq[i]);
		barrier_cbs_tasks[i] = kthread_run(rcu_torture_barrier_cbs,
						   (void *)(long)i,
						   "rcu_torture_barrier_cbs");
		if (IS_ERR(barrier_cbs_tasks[i])) {
			ret = PTR_ERR(barrier_cbs_tasks[i]);
			VERBOSE_PRINTK_ERRSTRING("Failed to create rcu_torture_barrier_cbs");
			barrier_cbs_tasks[i] = NULL;
			return ret;
		}
	}
	barrier_task = kthread_run(rcu_torture_barrier, NULL,
				   "rcu_torture_barrier");
	if (IS_ERR(barrier_task)) {
		ret = PTR_ERR(barrier_task);
		VERBOSE_PRINTK_ERRSTRING("Failed to create rcu_torture_barrier");
		barrier_task = NULL;
	}
	return 0;
}

/* Clean up after RCU barrier testing. */
static void rcu_torture_barrier_cleanup(void)
{
	int i;

	if (barrier_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_barrier task");
		kthread_stop(barrier_task);
		barrier_task = NULL;
	}
	if (barrier_cbs_tasks != NULL) {
		for (i = 0; i < n_barrier_cbs; i++) {
			if (barrier_cbs_tasks[i] != NULL) {
				VERBOSE_PRINTK_STRING("Stopping rcu_torture_barrier_cbs task");
				kthread_stop(barrier_cbs_tasks[i]);
				barrier_cbs_tasks[i] = NULL;
			}
		}
		kfree(barrier_cbs_tasks);
		barrier_cbs_tasks = NULL;
	}
	if (barrier_cbs_wq != NULL) {
		kfree(barrier_cbs_wq);
		barrier_cbs_wq = NULL;
	}
}

static int rcutorture_cpu_notify(struct notifier_block *self,
				 unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		(void)rcutorture_booster_init(cpu);
		break;
	case CPU_DOWN_PREPARE:
		rcutorture_booster_cleanup(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block rcutorture_cpu_nb = {
	.notifier_call = rcutorture_cpu_notify,
};

static void
rcu_torture_cleanup(void)
{
	int i;

	mutex_lock(&fullstop_mutex);
	rcutorture_record_test_transition();
	if (fullstop == FULLSTOP_SHUTDOWN) {
		pr_warn(/* but going down anyway, so... */
		       "Concurrent 'rmmod rcutorture' and shutdown illegal!\n");
		mutex_unlock(&fullstop_mutex);
		schedule_timeout_uninterruptible(10);
		if (cur_ops->cb_barrier != NULL)
			cur_ops->cb_barrier();
		return;
	}
	fullstop = FULLSTOP_RMMOD;
	mutex_unlock(&fullstop_mutex);
	unregister_reboot_notifier(&rcutorture_shutdown_nb);
	rcu_torture_barrier_cleanup();
	rcu_torture_stall_cleanup();
	if (stutter_task) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_stutter task");
		kthread_stop(stutter_task);
	}
	stutter_task = NULL;
	if (shuffler_task) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_shuffle task");
		kthread_stop(shuffler_task);
		free_cpumask_var(shuffle_tmp_mask);
	}
	shuffler_task = NULL;

	if (writer_task) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_writer task");
		kthread_stop(writer_task);
	}
	writer_task = NULL;

	if (reader_tasks) {
		for (i = 0; i < nrealreaders; i++) {
			if (reader_tasks[i]) {
				VERBOSE_PRINTK_STRING(
					"Stopping rcu_torture_reader task");
				kthread_stop(reader_tasks[i]);
			}
			reader_tasks[i] = NULL;
		}
		kfree(reader_tasks);
		reader_tasks = NULL;
	}
	rcu_torture_current = NULL;

	if (fakewriter_tasks) {
		for (i = 0; i < nfakewriters; i++) {
			if (fakewriter_tasks[i]) {
				VERBOSE_PRINTK_STRING(
					"Stopping rcu_torture_fakewriter task");
				kthread_stop(fakewriter_tasks[i]);
			}
			fakewriter_tasks[i] = NULL;
		}
		kfree(fakewriter_tasks);
		fakewriter_tasks = NULL;
	}

	if (stats_task) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_stats task");
		kthread_stop(stats_task);
	}
	stats_task = NULL;

	if (fqs_task) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_fqs task");
		kthread_stop(fqs_task);
	}
	fqs_task = NULL;
	if ((test_boost == 1 && cur_ops->can_boost) ||
	    test_boost == 2) {
		unregister_cpu_notifier(&rcutorture_cpu_nb);
		for_each_possible_cpu(i)
			rcutorture_booster_cleanup(i);
	}
	if (shutdown_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_shutdown task");
		kthread_stop(shutdown_task);
	}
	shutdown_task = NULL;
	rcu_torture_onoff_cleanup();

	/* Wait for all RCU callbacks to fire.  */

	if (cur_ops->cb_barrier != NULL)
		cur_ops->cb_barrier();

	rcu_torture_stats_print();  /* -After- the stats thread is stopped! */

	if (atomic_read(&n_rcu_torture_error) || n_rcu_torture_barrier_error)
		rcu_torture_print_module_parms(cur_ops, "End of test: FAILURE");
	else if (n_online_successes != n_online_attempts ||
		 n_offline_successes != n_offline_attempts)
		rcu_torture_print_module_parms(cur_ops,
					       "End of test: RCU_HOTPLUG");
	else
		rcu_torture_print_module_parms(cur_ops, "End of test: SUCCESS");
}

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
static void rcu_torture_leak_cb(struct rcu_head *rhp)
{
}

static void rcu_torture_err_cb(struct rcu_head *rhp)
{
	/*
	 * This -might- happen due to race conditions, but is unlikely.
	 * The scenario that leads to this happening is that the
	 * first of the pair of duplicate callbacks is queued,
	 * someone else starts a grace period that includes that
	 * callback, then the second of the pair must wait for the
	 * next grace period.  Unlikely, but can happen.  If it
	 * does happen, the debug-objects subsystem won't have splatted.
	 */
	pr_alert("rcutorture: duplicated callback was invoked.\n");
}
#endif /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */

/*
 * Verify that double-free causes debug-objects to complain, but only
 * if CONFIG_DEBUG_OBJECTS_RCU_HEAD=y.  Otherwise, say that the test
 * cannot be carried out.
 */
static void rcu_test_debug_objects(void)
{
#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
	struct rcu_head rh1;
	struct rcu_head rh2;

	init_rcu_head_on_stack(&rh1);
	init_rcu_head_on_stack(&rh2);
	pr_alert("rcutorture: WARN: Duplicate call_rcu() test starting.\n");

	/* Try to queue the rh2 pair of callbacks for the same grace period. */
	preempt_disable(); /* Prevent preemption from interrupting test. */
	rcu_read_lock(); /* Make it impossible to finish a grace period. */
	call_rcu(&rh1, rcu_torture_leak_cb); /* Start grace period. */
	local_irq_disable(); /* Make it harder to start a new grace period. */
	call_rcu(&rh2, rcu_torture_leak_cb);
	call_rcu(&rh2, rcu_torture_err_cb); /* Duplicate callback. */
	local_irq_enable();
	rcu_read_unlock();
	preempt_enable();

	/* Wait for them all to get done so we can safely return. */
	rcu_barrier();
	pr_alert("rcutorture: WARN: Duplicate call_rcu() test complete.\n");
	destroy_rcu_head_on_stack(&rh1);
	destroy_rcu_head_on_stack(&rh2);
#else /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */
	pr_alert("rcutorture: !CONFIG_DEBUG_OBJECTS_RCU_HEAD, not testing duplicate call_rcu()\n");
#endif /* #else #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */
}

static int __init
rcu_torture_init(void)
{
	int i;
	int cpu;
	int firsterr = 0;
	int retval;
	static struct rcu_torture_ops *torture_ops[] = {
		&rcu_ops, &rcu_bh_ops, &srcu_ops, &sched_ops,
	};

	mutex_lock(&fullstop_mutex);

	/* Process args and tell the world that the torturer is on the job. */
	for (i = 0; i < ARRAY_SIZE(torture_ops); i++) {
		cur_ops = torture_ops[i];
		if (strcmp(torture_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(torture_ops)) {
		pr_alert("rcu-torture: invalid torture type: \"%s\"\n",
			 torture_type);
		pr_alert("rcu-torture types:");
		for (i = 0; i < ARRAY_SIZE(torture_ops); i++)
			pr_alert(" %s", torture_ops[i]->name);
		pr_alert("\n");
		mutex_unlock(&fullstop_mutex);
		return -EINVAL;
	}
	if (cur_ops->fqs == NULL && fqs_duration != 0) {
		pr_alert("rcu-torture: ->fqs NULL and non-zero fqs_duration, fqs disabled.\n");
		fqs_duration = 0;
	}
	if (cur_ops->init)
		cur_ops->init(); /* no "goto unwind" prior to this point!!! */

	if (nreaders >= 0)
		nrealreaders = nreaders;
	else
		nrealreaders = 2 * num_online_cpus();
	rcu_torture_print_module_parms(cur_ops, "Start of test");
	fullstop = FULLSTOP_DONTSTOP;

	/* Set up the freelist. */

	INIT_LIST_HEAD(&rcu_torture_freelist);
	for (i = 0; i < ARRAY_SIZE(rcu_tortures); i++) {
		rcu_tortures[i].rtort_mbtest = 0;
		list_add_tail(&rcu_tortures[i].rtort_free,
			      &rcu_torture_freelist);
	}

	/* Initialize the statistics so that each run gets its own numbers. */

	rcu_torture_current = NULL;
	rcu_torture_current_version = 0;
	atomic_set(&n_rcu_torture_alloc, 0);
	atomic_set(&n_rcu_torture_alloc_fail, 0);
	atomic_set(&n_rcu_torture_free, 0);
	atomic_set(&n_rcu_torture_mberror, 0);
	atomic_set(&n_rcu_torture_error, 0);
	n_rcu_torture_barrier_error = 0;
	n_rcu_torture_boost_ktrerror = 0;
	n_rcu_torture_boost_rterror = 0;
	n_rcu_torture_boost_failure = 0;
	n_rcu_torture_boosts = 0;
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		atomic_set(&rcu_torture_wcount[i], 0);
	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			per_cpu(rcu_torture_count, cpu)[i] = 0;
			per_cpu(rcu_torture_batch, cpu)[i] = 0;
		}
	}

	/* Start up the kthreads. */

	VERBOSE_PRINTK_STRING("Creating rcu_torture_writer task");
	writer_task = kthread_create(rcu_torture_writer, NULL,
				     "rcu_torture_writer");
	if (IS_ERR(writer_task)) {
		firsterr = PTR_ERR(writer_task);
		VERBOSE_PRINTK_ERRSTRING("Failed to create writer");
		writer_task = NULL;
		goto unwind;
	}
	wake_up_process(writer_task);
	fakewriter_tasks = kzalloc(nfakewriters * sizeof(fakewriter_tasks[0]),
				   GFP_KERNEL);
	if (fakewriter_tasks == NULL) {
		VERBOSE_PRINTK_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nfakewriters; i++) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_fakewriter task");
		fakewriter_tasks[i] = kthread_run(rcu_torture_fakewriter, NULL,
						  "rcu_torture_fakewriter");
		if (IS_ERR(fakewriter_tasks[i])) {
			firsterr = PTR_ERR(fakewriter_tasks[i]);
			VERBOSE_PRINTK_ERRSTRING("Failed to create fakewriter");
			fakewriter_tasks[i] = NULL;
			goto unwind;
		}
	}
	reader_tasks = kzalloc(nrealreaders * sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (reader_tasks == NULL) {
		VERBOSE_PRINTK_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealreaders; i++) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_reader task");
		reader_tasks[i] = kthread_run(rcu_torture_reader, NULL,
					      "rcu_torture_reader");
		if (IS_ERR(reader_tasks[i])) {
			firsterr = PTR_ERR(reader_tasks[i]);
			VERBOSE_PRINTK_ERRSTRING("Failed to create reader");
			reader_tasks[i] = NULL;
			goto unwind;
		}
	}
	if (stat_interval > 0) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_stats task");
		stats_task = kthread_run(rcu_torture_stats, NULL,
					"rcu_torture_stats");
		if (IS_ERR(stats_task)) {
			firsterr = PTR_ERR(stats_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create stats");
			stats_task = NULL;
			goto unwind;
		}
	}
	if (test_no_idle_hz) {
		rcu_idle_cpu = num_online_cpus() - 1;

		if (!alloc_cpumask_var(&shuffle_tmp_mask, GFP_KERNEL)) {
			firsterr = -ENOMEM;
			VERBOSE_PRINTK_ERRSTRING("Failed to alloc mask");
			goto unwind;
		}

		/* Create the shuffler thread */
		shuffler_task = kthread_run(rcu_torture_shuffle, NULL,
					  "rcu_torture_shuffle");
		if (IS_ERR(shuffler_task)) {
			free_cpumask_var(shuffle_tmp_mask);
			firsterr = PTR_ERR(shuffler_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create shuffler");
			shuffler_task = NULL;
			goto unwind;
		}
	}
	if (stutter < 0)
		stutter = 0;
	if (stutter) {
		/* Create the stutter thread */
		stutter_task = kthread_run(rcu_torture_stutter, NULL,
					  "rcu_torture_stutter");
		if (IS_ERR(stutter_task)) {
			firsterr = PTR_ERR(stutter_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create stutter");
			stutter_task = NULL;
			goto unwind;
		}
	}
	if (fqs_duration < 0)
		fqs_duration = 0;
	if (fqs_duration) {
		/* Create the stutter thread */
		fqs_task = kthread_run(rcu_torture_fqs, NULL,
				       "rcu_torture_fqs");
		if (IS_ERR(fqs_task)) {
			firsterr = PTR_ERR(fqs_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create fqs");
			fqs_task = NULL;
			goto unwind;
		}
	}
	if (test_boost_interval < 1)
		test_boost_interval = 1;
	if (test_boost_duration < 2)
		test_boost_duration = 2;
	if ((test_boost == 1 && cur_ops->can_boost) ||
	    test_boost == 2) {

		boost_starttime = jiffies + test_boost_interval * HZ;
		register_cpu_notifier(&rcutorture_cpu_nb);
		for_each_possible_cpu(i) {
			if (cpu_is_offline(i))
				continue;  /* Heuristic: CPU can go offline. */
			retval = rcutorture_booster_init(i);
			if (retval < 0) {
				firsterr = retval;
				goto unwind;
			}
		}
	}
	if (shutdown_secs > 0) {
		shutdown_time = jiffies + shutdown_secs * HZ;
		shutdown_task = kthread_create(rcu_torture_shutdown, NULL,
					       "rcu_torture_shutdown");
		if (IS_ERR(shutdown_task)) {
			firsterr = PTR_ERR(shutdown_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create shutdown");
			shutdown_task = NULL;
			goto unwind;
		}
		wake_up_process(shutdown_task);
	}
	i = rcu_torture_onoff_init();
	if (i != 0) {
		firsterr = i;
		goto unwind;
	}
	register_reboot_notifier(&rcutorture_shutdown_nb);
	i = rcu_torture_stall_init();
	if (i != 0) {
		firsterr = i;
		goto unwind;
	}
	retval = rcu_torture_barrier_init();
	if (retval != 0) {
		firsterr = retval;
		goto unwind;
	}
	if (object_debug)
		rcu_test_debug_objects();
	rcutorture_record_test_transition();
	mutex_unlock(&fullstop_mutex);
	return 0;

unwind:
	mutex_unlock(&fullstop_mutex);
	rcu_torture_cleanup();
	return firsterr;
}

module_init(rcu_torture_init);
module_exit(rcu_torture_cleanup);
