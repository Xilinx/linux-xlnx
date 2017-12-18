/*
 *    Time of day based timer functions.
 *
 *  S390 version
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "arch/i386/kernel/time.c"
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#define KMSG_COMPONENT "time"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/stop_machine.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/notifier.h>
#include <linux/timekeeper_internal.h>
#include <linux/clockchips.h>
#include <linux/gfp.h>
#include <linux/kprobes.h>
#include <asm/uaccess.h>
#include <asm/facility.h>
#include <asm/delay.h>
#include <asm/div64.h>
#include <asm/vdso.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/vtimer.h>
#include <asm/stp.h>
#include <asm/cio.h>
#include "entry.h"

u64 sched_clock_base_cc = -1;	/* Force to data section. */
EXPORT_SYMBOL_GPL(sched_clock_base_cc);

static DEFINE_PER_CPU(struct clock_event_device, comparators);

ATOMIC_NOTIFIER_HEAD(s390_epoch_delta_notifier);
EXPORT_SYMBOL(s390_epoch_delta_notifier);

unsigned char ptff_function_mask[16];
unsigned long lpar_offset;
unsigned long initial_leap_seconds;

/*
 * Get time offsets with PTFF
 */
void __init ptff_init(void)
{
	struct ptff_qto qto;
	struct ptff_qui qui;

	if (!test_facility(28))
		return;
	ptff(&ptff_function_mask, sizeof(ptff_function_mask), PTFF_QAF);

	/* get LPAR offset */
	if (ptff_query(PTFF_QTO) && ptff(&qto, sizeof(qto), PTFF_QTO) == 0)
		lpar_offset = qto.tod_epoch_difference;

	/* get initial leap seconds */
	if (ptff_query(PTFF_QUI) && ptff(&qui, sizeof(qui), PTFF_QUI) == 0)
		initial_leap_seconds = (unsigned long)
			((long) qui.old_leap * 4096000000L);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long notrace sched_clock(void)
{
	return tod_to_ns(get_tod_clock_monotonic());
}
NOKPROBE_SYMBOL(sched_clock);

/*
 * Monotonic_clock - returns # of nanoseconds passed since time_init()
 */
unsigned long long monotonic_clock(void)
{
	return sched_clock();
}
EXPORT_SYMBOL(monotonic_clock);

void tod_to_timeval(__u64 todval, struct timespec64 *xt)
{
	unsigned long long sec;

	sec = todval >> 12;
	do_div(sec, 1000000);
	xt->tv_sec = sec;
	todval -= (sec * 1000000) << 12;
	xt->tv_nsec = ((todval * 1000) >> 12);
}
EXPORT_SYMBOL(tod_to_timeval);

void clock_comparator_work(void)
{
	struct clock_event_device *cd;

	S390_lowcore.clock_comparator = -1ULL;
	cd = this_cpu_ptr(&comparators);
	cd->event_handler(cd);
}

/*
 * Fixup the clock comparator.
 */
static void fixup_clock_comparator(unsigned long long delta)
{
	/* If nobody is waiting there's nothing to fix. */
	if (S390_lowcore.clock_comparator == -1ULL)
		return;
	S390_lowcore.clock_comparator += delta;
	set_clock_comparator(S390_lowcore.clock_comparator);
}

static int s390_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	S390_lowcore.clock_comparator = get_tod_clock() + delta;
	set_clock_comparator(S390_lowcore.clock_comparator);
	return 0;
}

/*
 * Set up lowcore and control register of the current cpu to
 * enable TOD clock and clock comparator interrupts.
 */
void init_cpu_timer(void)
{
	struct clock_event_device *cd;
	int cpu;

	S390_lowcore.clock_comparator = -1ULL;
	set_clock_comparator(S390_lowcore.clock_comparator);

	cpu = smp_processor_id();
	cd = &per_cpu(comparators, cpu);
	cd->name		= "comparator";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;
	cd->mult		= 16777;
	cd->shift		= 12;
	cd->min_delta_ns	= 1;
	cd->max_delta_ns	= LONG_MAX;
	cd->rating		= 400;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= s390_next_event;

	clockevents_register_device(cd);

	/* Enable clock comparator timer interrupt. */
	__ctl_set_bit(0,11);

	/* Always allow the timing alert external interrupt. */
	__ctl_set_bit(0, 4);
}

static void clock_comparator_interrupt(struct ext_code ext_code,
				       unsigned int param32,
				       unsigned long param64)
{
	inc_irq_stat(IRQEXT_CLK);
	if (S390_lowcore.clock_comparator == -1ULL)
		set_clock_comparator(S390_lowcore.clock_comparator);
}

static void stp_timing_alert(struct stp_irq_parm *);

static void timing_alert_interrupt(struct ext_code ext_code,
				   unsigned int param32, unsigned long param64)
{
	inc_irq_stat(IRQEXT_TLA);
	if (param32 & 0x00038000)
		stp_timing_alert((struct stp_irq_parm *) &param32);
}

static void stp_reset(void);

void read_persistent_clock64(struct timespec64 *ts)
{
	__u64 clock;

	clock = get_tod_clock() - initial_leap_seconds;
	tod_to_timeval(clock - TOD_UNIX_EPOCH, ts);
}

void read_boot_clock64(struct timespec64 *ts)
{
	__u64 clock;

	clock = sched_clock_base_cc - initial_leap_seconds;
	tod_to_timeval(clock - TOD_UNIX_EPOCH, ts);
}

static cycle_t read_tod_clock(struct clocksource *cs)
{
	return get_tod_clock();
}

static struct clocksource clocksource_tod = {
	.name		= "tod",
	.rating		= 400,
	.read		= read_tod_clock,
	.mask		= -1ULL,
	.mult		= 1000,
	.shift		= 12,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

struct clocksource * __init clocksource_default_clock(void)
{
	return &clocksource_tod;
}

void update_vsyscall(struct timekeeper *tk)
{
	u64 nsecps;

	if (tk->tkr_mono.clock != &clocksource_tod)
		return;

	/* Make userspace gettimeofday spin until we're done. */
	++vdso_data->tb_update_count;
	smp_wmb();
	vdso_data->xtime_tod_stamp = tk->tkr_mono.cycle_last;
	vdso_data->xtime_clock_sec = tk->xtime_sec;
	vdso_data->xtime_clock_nsec = tk->tkr_mono.xtime_nsec;
	vdso_data->wtom_clock_sec =
		tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
	vdso_data->wtom_clock_nsec = tk->tkr_mono.xtime_nsec +
		+ ((u64) tk->wall_to_monotonic.tv_nsec << tk->tkr_mono.shift);
	nsecps = (u64) NSEC_PER_SEC << tk->tkr_mono.shift;
	while (vdso_data->wtom_clock_nsec >= nsecps) {
		vdso_data->wtom_clock_nsec -= nsecps;
		vdso_data->wtom_clock_sec++;
	}

	vdso_data->xtime_coarse_sec = tk->xtime_sec;
	vdso_data->xtime_coarse_nsec =
		(long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);
	vdso_data->wtom_coarse_sec =
		vdso_data->xtime_coarse_sec + tk->wall_to_monotonic.tv_sec;
	vdso_data->wtom_coarse_nsec =
		vdso_data->xtime_coarse_nsec + tk->wall_to_monotonic.tv_nsec;
	while (vdso_data->wtom_coarse_nsec >= NSEC_PER_SEC) {
		vdso_data->wtom_coarse_nsec -= NSEC_PER_SEC;
		vdso_data->wtom_coarse_sec++;
	}

	vdso_data->tk_mult = tk->tkr_mono.mult;
	vdso_data->tk_shift = tk->tkr_mono.shift;
	smp_wmb();
	++vdso_data->tb_update_count;
}

extern struct timezone sys_tz;

void update_vsyscall_tz(void)
{
	vdso_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime = sys_tz.tz_dsttime;
}

/*
 * Initialize the TOD clock and the CPU timer of
 * the boot cpu.
 */
void __init time_init(void)
{
	/* Reset time synchronization interfaces. */
	stp_reset();

	/* request the clock comparator external interrupt */
	if (register_external_irq(EXT_IRQ_CLK_COMP, clock_comparator_interrupt))
		panic("Couldn't request external interrupt 0x1004");

	/* request the timing alert external interrupt */
	if (register_external_irq(EXT_IRQ_TIMING_ALERT, timing_alert_interrupt))
		panic("Couldn't request external interrupt 0x1406");

	if (__clocksource_register(&clocksource_tod) != 0)
		panic("Could not register TOD clock source");

	/* Enable TOD clock interrupts on the boot cpu. */
	init_cpu_timer();

	/* Enable cpu timer interrupts on the boot cpu. */
	vtime_init();
}

static DEFINE_PER_CPU(atomic_t, clock_sync_word);
static DEFINE_MUTEX(clock_sync_mutex);
static unsigned long clock_sync_flags;

#define CLOCK_SYNC_HAS_STP	0
#define CLOCK_SYNC_STP		1

/*
 * The get_clock function for the physical clock. It will get the current
 * TOD clock, subtract the LPAR offset and write the result to *clock.
 * The function returns 0 if the clock is in sync with the external time
 * source. If the clock mode is local it will return -EOPNOTSUPP and
 * -EAGAIN if the clock is not in sync with the external reference.
 */
int get_phys_clock(unsigned long long *clock)
{
	atomic_t *sw_ptr;
	unsigned int sw0, sw1;

	sw_ptr = &get_cpu_var(clock_sync_word);
	sw0 = atomic_read(sw_ptr);
	*clock = get_tod_clock() - lpar_offset;
	sw1 = atomic_read(sw_ptr);
	put_cpu_var(clock_sync_word);
	if (sw0 == sw1 && (sw0 & 0x80000000U))
		/* Success: time is in sync. */
		return 0;
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -EOPNOTSUPP;
	if (!test_bit(CLOCK_SYNC_STP, &clock_sync_flags))
		return -EACCES;
	return -EAGAIN;
}
EXPORT_SYMBOL(get_phys_clock);

/*
 * Make get_phys_clock() return -EAGAIN.
 */
static void disable_sync_clock(void *dummy)
{
	atomic_t *sw_ptr = this_cpu_ptr(&clock_sync_word);
	/*
	 * Clear the in-sync bit 2^31. All get_phys_clock calls will
	 * fail until the sync bit is turned back on. In addition
	 * increase the "sequence" counter to avoid the race of an
	 * stp event and the complete recovery against get_phys_clock.
	 */
	atomic_andnot(0x80000000, sw_ptr);
	atomic_inc(sw_ptr);
}

/*
 * Make get_phys_clock() return 0 again.
 * Needs to be called from a context disabled for preemption.
 */
static void enable_sync_clock(void)
{
	atomic_t *sw_ptr = this_cpu_ptr(&clock_sync_word);
	atomic_or(0x80000000, sw_ptr);
}

/*
 * Function to check if the clock is in sync.
 */
static inline int check_sync_clock(void)
{
	atomic_t *sw_ptr;
	int rc;

	sw_ptr = &get_cpu_var(clock_sync_word);
	rc = (atomic_read(sw_ptr) & 0x80000000U) != 0;
	put_cpu_var(clock_sync_word);
	return rc;
}

/* Single threaded workqueue used for stp sync events */
static struct workqueue_struct *time_sync_wq;

static void __init time_init_wq(void)
{
	if (time_sync_wq)
		return;
	time_sync_wq = create_singlethread_workqueue("timesync");
}

struct clock_sync_data {
	atomic_t cpus;
	int in_sync;
	unsigned long long fixup_cc;
};

static void clock_sync_cpu(struct clock_sync_data *sync)
{
	atomic_dec(&sync->cpus);
	enable_sync_clock();
	while (sync->in_sync == 0) {
		__udelay(1);
		/*
		 * A different cpu changes *in_sync. Therefore use
		 * barrier() to force memory access.
		 */
		barrier();
	}
	if (sync->in_sync != 1)
		/* Didn't work. Clear per-cpu in sync bit again. */
		disable_sync_clock(NULL);
	/*
	 * This round of TOD syncing is done. Set the clock comparator
	 * to the next tick and let the processor continue.
	 */
	fixup_clock_comparator(sync->fixup_cc);
}

/*
 * Server Time Protocol (STP) code.
 */
static bool stp_online;
static struct stp_sstpi stp_info;
static void *stp_page;

static void stp_work_fn(struct work_struct *work);
static DEFINE_MUTEX(stp_work_mutex);
static DECLARE_WORK(stp_work, stp_work_fn);
static struct timer_list stp_timer;

static int __init early_parse_stp(char *p)
{
	return kstrtobool(p, &stp_online);
}
early_param("stp", early_parse_stp);

/*
 * Reset STP attachment.
 */
static void __init stp_reset(void)
{
	int rc;

	stp_page = (void *) get_zeroed_page(GFP_ATOMIC);
	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000, NULL);
	if (rc == 0)
		set_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags);
	else if (stp_online) {
		pr_warn("The real or virtual hardware system does not provide an STP interface\n");
		free_page((unsigned long) stp_page);
		stp_page = NULL;
		stp_online = 0;
	}
}

static void stp_timeout(unsigned long dummy)
{
	queue_work(time_sync_wq, &stp_work);
}

static int __init stp_init(void)
{
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return 0;
	setup_timer(&stp_timer, stp_timeout, 0UL);
	time_init_wq();
	if (!stp_online)
		return 0;
	queue_work(time_sync_wq, &stp_work);
	return 0;
}

arch_initcall(stp_init);

/*
 * STP timing alert. There are three causes:
 * 1) timing status change
 * 2) link availability change
 * 3) time control parameter change
 * In all three cases we are only interested in the clock source state.
 * If a STP clock source is now available use it.
 */
static void stp_timing_alert(struct stp_irq_parm *intparm)
{
	if (intparm->tsc || intparm->lac || intparm->tcpc)
		queue_work(time_sync_wq, &stp_work);
}

/*
 * STP sync check machine check. This is called when the timing state
 * changes from the synchronized state to the unsynchronized state.
 * After a STP sync check the clock is not in sync. The machine check
 * is broadcasted to all cpus at the same time.
 */
int stp_sync_check(void)
{
	disable_sync_clock(NULL);
	return 1;
}

/*
 * STP island condition machine check. This is called when an attached
 * server  attempts to communicate over an STP link and the servers
 * have matching CTN ids and have a valid stratum-1 configuration
 * but the configurations do not match.
 */
int stp_island_check(void)
{
	disable_sync_clock(NULL);
	return 1;
}

void stp_queue_work(void)
{
	queue_work(time_sync_wq, &stp_work);
}

static int stp_sync_clock(void *data)
{
	static int first;
	unsigned long long clock_delta;
	struct clock_sync_data *stp_sync;
	struct ptff_qto qto;
	int rc;

	stp_sync = data;

	if (xchg(&first, 1) == 1) {
		/* Slave */
		clock_sync_cpu(stp_sync);
		return 0;
	}

	/* Wait until all other cpus entered the sync function. */
	while (atomic_read(&stp_sync->cpus) != 0)
		cpu_relax();

	enable_sync_clock();

	rc = 0;
	if (stp_info.todoff[0] || stp_info.todoff[1] ||
	    stp_info.todoff[2] || stp_info.todoff[3] ||
	    stp_info.tmd != 2) {
		rc = chsc_sstpc(stp_page, STP_OP_SYNC, 0, &clock_delta);
		if (rc == 0) {
			/* fixup the monotonic sched clock */
			sched_clock_base_cc += clock_delta;
			if (ptff_query(PTFF_QTO) &&
			    ptff(&qto, sizeof(qto), PTFF_QTO) == 0)
				/* Update LPAR offset */
				lpar_offset = qto.tod_epoch_difference;
			atomic_notifier_call_chain(&s390_epoch_delta_notifier,
						   0, &clock_delta);
			stp_sync->fixup_cc = clock_delta;
			fixup_clock_comparator(clock_delta);
			rc = chsc_sstpi(stp_page, &stp_info,
					sizeof(struct stp_sstpi));
			if (rc == 0 && stp_info.tmd != 2)
				rc = -EAGAIN;
		}
	}
	if (rc) {
		disable_sync_clock(NULL);
		stp_sync->in_sync = -EAGAIN;
	} else
		stp_sync->in_sync = 1;
	xchg(&first, 0);
	return 0;
}

/*
 * STP work. Check for the STP state and take over the clock
 * synchronization if the STP clock source is usable.
 */
static void stp_work_fn(struct work_struct *work)
{
	struct clock_sync_data stp_sync;
	int rc;

	/* prevent multiple execution. */
	mutex_lock(&stp_work_mutex);

	if (!stp_online) {
		chsc_sstpc(stp_page, STP_OP_CTRL, 0x0000, NULL);
		del_timer_sync(&stp_timer);
		goto out_unlock;
	}

	rc = chsc_sstpc(stp_page, STP_OP_CTRL, 0xb0e0, NULL);
	if (rc)
		goto out_unlock;

	rc = chsc_sstpi(stp_page, &stp_info, sizeof(struct stp_sstpi));
	if (rc || stp_info.c == 0)
		goto out_unlock;

	/* Skip synchronization if the clock is already in sync. */
	if (check_sync_clock())
		goto out_unlock;

	memset(&stp_sync, 0, sizeof(stp_sync));
	get_online_cpus();
	atomic_set(&stp_sync.cpus, num_online_cpus() - 1);
	stop_machine(stp_sync_clock, &stp_sync, cpu_online_mask);
	put_online_cpus();

	if (!check_sync_clock())
		/*
		 * There is a usable clock but the synchonization failed.
		 * Retry after a second.
		 */
		mod_timer(&stp_timer, jiffies + HZ);

out_unlock:
	mutex_unlock(&stp_work_mutex);
}

/*
 * STP subsys sysfs interface functions
 */
static struct bus_type stp_subsys = {
	.name		= "stp",
	.dev_name	= "stp",
};

static ssize_t stp_ctn_id_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%016llx\n",
		       *(unsigned long long *) stp_info.ctnid);
}

static DEVICE_ATTR(ctn_id, 0400, stp_ctn_id_show, NULL);

static ssize_t stp_ctn_type_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.ctn);
}

static DEVICE_ATTR(ctn_type, 0400, stp_ctn_type_show, NULL);

static ssize_t stp_dst_offset_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x2000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.dsto);
}

static DEVICE_ATTR(dst_offset, 0400, stp_dst_offset_show, NULL);

static ssize_t stp_leap_seconds_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x8000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.leaps);
}

static DEVICE_ATTR(leap_seconds, 0400, stp_leap_seconds_show, NULL);

static ssize_t stp_stratum_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.stratum);
}

static DEVICE_ATTR(stratum, 0400, stp_stratum_show, NULL);

static ssize_t stp_time_offset_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x0800))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int) stp_info.tto);
}

static DEVICE_ATTR(time_offset, 0400, stp_time_offset_show, NULL);

static ssize_t stp_time_zone_offset_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online || !(stp_info.vbits & 0x4000))
		return -ENODATA;
	return sprintf(buf, "%i\n", (int)(s16) stp_info.tzo);
}

static DEVICE_ATTR(time_zone_offset, 0400,
			 stp_time_zone_offset_show, NULL);

static ssize_t stp_timing_mode_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.tmd);
}

static DEVICE_ATTR(timing_mode, 0400, stp_timing_mode_show, NULL);

static ssize_t stp_timing_state_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	if (!stp_online)
		return -ENODATA;
	return sprintf(buf, "%i\n", stp_info.tst);
}

static DEVICE_ATTR(timing_state, 0400, stp_timing_state_show, NULL);

static ssize_t stp_online_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%i\n", stp_online);
}

static ssize_t stp_online_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int value;

	value = simple_strtoul(buf, NULL, 0);
	if (value != 0 && value != 1)
		return -EINVAL;
	if (!test_bit(CLOCK_SYNC_HAS_STP, &clock_sync_flags))
		return -EOPNOTSUPP;
	mutex_lock(&clock_sync_mutex);
	stp_online = value;
	if (stp_online)
		set_bit(CLOCK_SYNC_STP, &clock_sync_flags);
	else
		clear_bit(CLOCK_SYNC_STP, &clock_sync_flags);
	queue_work(time_sync_wq, &stp_work);
	mutex_unlock(&clock_sync_mutex);
	return count;
}

/*
 * Can't use DEVICE_ATTR because the attribute should be named
 * stp/online but dev_attr_online already exists in this file ..
 */
static struct device_attribute dev_attr_stp_online = {
	.attr = { .name = "online", .mode = 0600 },
	.show	= stp_online_show,
	.store	= stp_online_store,
};

static struct device_attribute *stp_attributes[] = {
	&dev_attr_ctn_id,
	&dev_attr_ctn_type,
	&dev_attr_dst_offset,
	&dev_attr_leap_seconds,
	&dev_attr_stp_online,
	&dev_attr_stratum,
	&dev_attr_time_offset,
	&dev_attr_time_zone_offset,
	&dev_attr_timing_mode,
	&dev_attr_timing_state,
	NULL
};

static int __init stp_init_sysfs(void)
{
	struct device_attribute **attr;
	int rc;

	rc = subsys_system_register(&stp_subsys, NULL);
	if (rc)
		goto out;
	for (attr = stp_attributes; *attr; attr++) {
		rc = device_create_file(stp_subsys.dev_root, *attr);
		if (rc)
			goto out_unreg;
	}
	return 0;
out_unreg:
	for (; attr >= stp_attributes; attr--)
		device_remove_file(stp_subsys.dev_root, *attr);
	bus_unregister(&stp_subsys);
out:
	return rc;
}

device_initcall(stp_init_sysfs);
