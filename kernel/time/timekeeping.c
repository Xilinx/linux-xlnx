/*
 *  linux/kernel/time/timekeeping.c
 *
 *  Kernel timekeeping code and accessor functions
 *
 *  This code was moved from linux/kernel/timer.c.
 *  Please see that file for copyright and history logs.
 *
 */

#include <linux/timekeeper_internal.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/tick.h>
#include <linux/stop_machine.h>
#include <linux/pvclock_gtod.h>

#include "tick-internal.h"
#include "ntp_internal.h"
#include "timekeeping_internal.h"

#define TK_CLEAR_NTP		(1 << 0)
#define TK_MIRROR		(1 << 1)
#define TK_CLOCK_WAS_SET	(1 << 2)

static struct timekeeper timekeeper;
static DEFINE_RAW_SPINLOCK(timekeeper_lock);
static seqcount_t timekeeper_seq;
static struct timekeeper shadow_timekeeper;

/* flag for if timekeeping is suspended */
int __read_mostly timekeeping_suspended;

/* Flag for if there is a persistent clock on this platform */
bool __read_mostly persistent_clock_exist = false;

static inline void tk_normalize_xtime(struct timekeeper *tk)
{
	while (tk->xtime_nsec >= ((u64)NSEC_PER_SEC << tk->shift)) {
		tk->xtime_nsec -= (u64)NSEC_PER_SEC << tk->shift;
		tk->xtime_sec++;
	}
}

static void tk_set_xtime(struct timekeeper *tk, const struct timespec *ts)
{
	tk->xtime_sec = ts->tv_sec;
	tk->xtime_nsec = (u64)ts->tv_nsec << tk->shift;
}

static void tk_xtime_add(struct timekeeper *tk, const struct timespec *ts)
{
	tk->xtime_sec += ts->tv_sec;
	tk->xtime_nsec += (u64)ts->tv_nsec << tk->shift;
	tk_normalize_xtime(tk);
}

static void tk_set_wall_to_mono(struct timekeeper *tk, struct timespec wtm)
{
	struct timespec tmp;

	/*
	 * Verify consistency of: offset_real = -wall_to_monotonic
	 * before modifying anything
	 */
	set_normalized_timespec(&tmp, -tk->wall_to_monotonic.tv_sec,
					-tk->wall_to_monotonic.tv_nsec);
	WARN_ON_ONCE(tk->offs_real.tv64 != timespec_to_ktime(tmp).tv64);
	tk->wall_to_monotonic = wtm;
	set_normalized_timespec(&tmp, -wtm.tv_sec, -wtm.tv_nsec);
	tk->offs_real = timespec_to_ktime(tmp);
	tk->offs_tai = ktime_sub(tk->offs_real, ktime_set(tk->tai_offset, 0));
}

static void tk_set_sleep_time(struct timekeeper *tk, struct timespec t)
{
	/* Verify consistency before modifying */
	WARN_ON_ONCE(tk->offs_boot.tv64 != timespec_to_ktime(tk->total_sleep_time).tv64);

	tk->total_sleep_time	= t;
	tk->offs_boot		= timespec_to_ktime(t);
}

/**
 * timekeeper_setup_internals - Set up internals to use clocksource clock.
 *
 * @clock:		Pointer to clocksource.
 *
 * Calculates a fixed cycle/nsec interval for a given clocksource/adjustment
 * pair and interval request.
 *
 * Unless you're the timekeeping code, you should not be using this!
 */
static void tk_setup_internals(struct timekeeper *tk, struct clocksource *clock)
{
	cycle_t interval;
	u64 tmp, ntpinterval;
	struct clocksource *old_clock;

	old_clock = tk->clock;
	tk->clock = clock;
	tk->cycle_last = clock->cycle_last = clock->read(clock);

	/* Do the ns -> cycle conversion first, using original mult */
	tmp = NTP_INTERVAL_LENGTH;
	tmp <<= clock->shift;
	ntpinterval = tmp;
	tmp += clock->mult/2;
	do_div(tmp, clock->mult);
	if (tmp == 0)
		tmp = 1;

	interval = (cycle_t) tmp;
	tk->cycle_interval = interval;

	/* Go back from cycles -> shifted ns */
	tk->xtime_interval = (u64) interval * clock->mult;
	tk->xtime_remainder = ntpinterval - tk->xtime_interval;
	tk->raw_interval =
		((u64) interval * clock->mult) >> clock->shift;

	 /* if changing clocks, convert xtime_nsec shift units */
	if (old_clock) {
		int shift_change = clock->shift - old_clock->shift;
		if (shift_change < 0)
			tk->xtime_nsec >>= -shift_change;
		else
			tk->xtime_nsec <<= shift_change;
	}
	tk->shift = clock->shift;

	tk->ntp_error = 0;
	tk->ntp_error_shift = NTP_SCALE_SHIFT - clock->shift;

	/*
	 * The timekeeper keeps its own mult values for the currently
	 * active clocksource. These value will be adjusted via NTP
	 * to counteract clock drifting.
	 */
	tk->mult = clock->mult;
}

/* Timekeeper helper functions. */

#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
u32 (*arch_gettimeoffset)(void);

u32 get_arch_timeoffset(void)
{
	if (likely(arch_gettimeoffset))
		return arch_gettimeoffset();
	return 0;
}
#else
static inline u32 get_arch_timeoffset(void) { return 0; }
#endif

static inline s64 timekeeping_get_ns(struct timekeeper *tk)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;
	s64 nsec;

	/* read clocksource: */
	clock = tk->clock;
	cycle_now = clock->read(clock);

	/* calculate the delta since the last update_wall_time: */
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

	nsec = cycle_delta * tk->mult + tk->xtime_nsec;
	nsec >>= tk->shift;

	/* If arch requires, add in get_arch_timeoffset() */
	return nsec + get_arch_timeoffset();
}

static inline s64 timekeeping_get_ns_raw(struct timekeeper *tk)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;
	s64 nsec;

	/* read clocksource: */
	clock = tk->clock;
	cycle_now = clock->read(clock);

	/* calculate the delta since the last update_wall_time: */
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

	/* convert delta to nanoseconds. */
	nsec = clocksource_cyc2ns(cycle_delta, clock->mult, clock->shift);

	/* If arch requires, add in get_arch_timeoffset() */
	return nsec + get_arch_timeoffset();
}

static RAW_NOTIFIER_HEAD(pvclock_gtod_chain);

static void update_pvclock_gtod(struct timekeeper *tk, bool was_set)
{
	raw_notifier_call_chain(&pvclock_gtod_chain, was_set, tk);
}

/**
 * pvclock_gtod_register_notifier - register a pvclock timedata update listener
 */
int pvclock_gtod_register_notifier(struct notifier_block *nb)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	ret = raw_notifier_chain_register(&pvclock_gtod_chain, nb);
	update_pvclock_gtod(tk, true);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(pvclock_gtod_register_notifier);

/**
 * pvclock_gtod_unregister_notifier - unregister a pvclock
 * timedata update listener
 */
int pvclock_gtod_unregister_notifier(struct notifier_block *nb)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	ret = raw_notifier_chain_unregister(&pvclock_gtod_chain, nb);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(pvclock_gtod_unregister_notifier);

/* must hold timekeeper_lock */
static void timekeeping_update(struct timekeeper *tk, unsigned int action)
{
	if (action & TK_CLEAR_NTP) {
		tk->ntp_error = 0;
		ntp_clear();
	}
	update_vsyscall(tk);
	update_pvclock_gtod(tk, action & TK_CLOCK_WAS_SET);

	if (action & TK_MIRROR)
		memcpy(&shadow_timekeeper, &timekeeper, sizeof(timekeeper));
}

/**
 * timekeeping_forward_now - update clock to the current time
 *
 * Forward the current clock to update its state since the last call to
 * update_wall_time(). This is useful before significant clock changes,
 * as it avoids having to deal with this time offset explicitly.
 */
static void timekeeping_forward_now(struct timekeeper *tk)
{
	cycle_t cycle_now, cycle_delta;
	struct clocksource *clock;
	s64 nsec;

	clock = tk->clock;
	cycle_now = clock->read(clock);
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;
	tk->cycle_last = clock->cycle_last = cycle_now;

	tk->xtime_nsec += cycle_delta * tk->mult;

	/* If arch requires, add in get_arch_timeoffset() */
	tk->xtime_nsec += (u64)get_arch_timeoffset() << tk->shift;

	tk_normalize_xtime(tk);

	nsec = clocksource_cyc2ns(cycle_delta, clock->mult, clock->shift);
	timespec_add_ns(&tk->raw_time, nsec);
}

/**
 * __getnstimeofday - Returns the time of day in a timespec.
 * @ts:		pointer to the timespec to be set
 *
 * Updates the time of day in the timespec.
 * Returns 0 on success, or -ve when suspended (timespec will be undefined).
 */
int __getnstimeofday(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	s64 nsecs = 0;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		ts->tv_sec = tk->xtime_sec;
		nsecs = timekeeping_get_ns(tk);

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsecs);

	/*
	 * Do not bail out early, in case there were callers still using
	 * the value, even in the face of the WARN_ON.
	 */
	if (unlikely(timekeeping_suspended))
		return -EAGAIN;
	return 0;
}
EXPORT_SYMBOL(__getnstimeofday);

/**
 * getnstimeofday - Returns the time of day in a timespec.
 * @ts:		pointer to the timespec to be set
 *
 * Returns the time of day in a timespec (WARN if suspended).
 */
void getnstimeofday(struct timespec *ts)
{
	WARN_ON(__getnstimeofday(ts));
}
EXPORT_SYMBOL(getnstimeofday);

ktime_t ktime_get(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned int seq;
	s64 secs, nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		secs = tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
		nsecs = timekeeping_get_ns(tk) + tk->wall_to_monotonic.tv_nsec;

	} while (read_seqcount_retry(&timekeeper_seq, seq));
	/*
	 * Use ktime_set/ktime_add_ns to create a proper ktime on
	 * 32-bit architectures without CONFIG_KTIME_SCALAR.
	 */
	return ktime_add_ns(ktime_set(secs, 0), nsecs);
}
EXPORT_SYMBOL_GPL(ktime_get);

/**
 * ktime_get_ts - get the monotonic clock in timespec format
 * @ts:		pointer to timespec variable
 *
 * The function calculates the monotonic clock from the realtime
 * clock and the wall_to_monotonic offset and stores the result
 * in normalized timespec format in the variable pointed to by @ts.
 */
void ktime_get_ts(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec tomono;
	s64 nsec;
	unsigned int seq;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		ts->tv_sec = tk->xtime_sec;
		nsec = timekeeping_get_ns(tk);
		tomono = tk->wall_to_monotonic;

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	ts->tv_sec += tomono.tv_sec;
	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsec + tomono.tv_nsec);
}
EXPORT_SYMBOL_GPL(ktime_get_ts);


/**
 * timekeeping_clocktai - Returns the TAI time of day in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the time of day in a timespec.
 */
void timekeeping_clocktai(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	u64 nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		ts->tv_sec = tk->xtime_sec + tk->tai_offset;
		nsecs = timekeeping_get_ns(tk);

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsecs);

}
EXPORT_SYMBOL(timekeeping_clocktai);


/**
 * ktime_get_clocktai - Returns the TAI time of day in a ktime
 *
 * Returns the time of day in a ktime.
 */
ktime_t ktime_get_clocktai(void)
{
	struct timespec ts;

	timekeeping_clocktai(&ts);
	return timespec_to_ktime(ts);
}
EXPORT_SYMBOL(ktime_get_clocktai);

#ifdef CONFIG_NTP_PPS

/**
 * getnstime_raw_and_real - get day and raw monotonic time in timespec format
 * @ts_raw:	pointer to the timespec to be set to raw monotonic time
 * @ts_real:	pointer to the timespec to be set to the time of day
 *
 * This function reads both the time of day and raw monotonic time at the
 * same time atomically and stores the resulting timestamps in timespec
 * format.
 */
void getnstime_raw_and_real(struct timespec *ts_raw, struct timespec *ts_real)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	s64 nsecs_raw, nsecs_real;

	WARN_ON_ONCE(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		*ts_raw = tk->raw_time;
		ts_real->tv_sec = tk->xtime_sec;
		ts_real->tv_nsec = 0;

		nsecs_raw = timekeeping_get_ns_raw(tk);
		nsecs_real = timekeeping_get_ns(tk);

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	timespec_add_ns(ts_raw, nsecs_raw);
	timespec_add_ns(ts_real, nsecs_real);
}
EXPORT_SYMBOL(getnstime_raw_and_real);

#endif /* CONFIG_NTP_PPS */

/**
 * do_gettimeofday - Returns the time of day in a timeval
 * @tv:		pointer to the timeval to be set
 *
 * NOTE: Users should be converted to using getnstimeofday()
 */
void do_gettimeofday(struct timeval *tv)
{
	struct timespec now;

	getnstimeofday(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}
EXPORT_SYMBOL(do_gettimeofday);

/**
 * do_settimeofday - Sets the time of day
 * @tv:		pointer to the timespec variable containing the new time
 *
 * Sets the time of day to the new time and update NTP and notify hrtimers
 */
int do_settimeofday(const struct timespec *tv)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec ts_delta, xt;
	unsigned long flags;

	if (!timespec_valid_strict(tv))
		return -EINVAL;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	timekeeping_forward_now(tk);

	xt = tk_xtime(tk);
	ts_delta.tv_sec = tv->tv_sec - xt.tv_sec;
	ts_delta.tv_nsec = tv->tv_nsec - xt.tv_nsec;

	tk_set_wall_to_mono(tk, timespec_sub(tk->wall_to_monotonic, ts_delta));

	tk_set_xtime(tk, tv);

	timekeeping_update(tk, TK_CLEAR_NTP | TK_MIRROR | TK_CLOCK_WAS_SET);

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();

	return 0;
}
EXPORT_SYMBOL(do_settimeofday);

/**
 * timekeeping_inject_offset - Adds or subtracts from the current time.
 * @tv:		pointer to the timespec variable containing the offset
 *
 * Adds or subtracts an offset value from the current time.
 */
int timekeeping_inject_offset(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;
	struct timespec tmp;
	int ret = 0;

	if ((unsigned long)ts->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	timekeeping_forward_now(tk);

	/* Make sure the proposed value is valid */
	tmp = timespec_add(tk_xtime(tk),  *ts);
	if (!timespec_valid_strict(&tmp)) {
		ret = -EINVAL;
		goto error;
	}

	tk_xtime_add(tk, ts);
	tk_set_wall_to_mono(tk, timespec_sub(tk->wall_to_monotonic, *ts));

error: /* even if we error out, we forwarded the time, so call update */
	timekeeping_update(tk, TK_CLEAR_NTP | TK_MIRROR | TK_CLOCK_WAS_SET);

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();

	return ret;
}
EXPORT_SYMBOL(timekeeping_inject_offset);


/**
 * timekeeping_get_tai_offset - Returns current TAI offset from UTC
 *
 */
s32 timekeeping_get_tai_offset(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned int seq;
	s32 ret;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		ret = tk->tai_offset;
	} while (read_seqcount_retry(&timekeeper_seq, seq));

	return ret;
}

/**
 * __timekeeping_set_tai_offset - Lock free worker function
 *
 */
static void __timekeeping_set_tai_offset(struct timekeeper *tk, s32 tai_offset)
{
	tk->tai_offset = tai_offset;
	tk->offs_tai = ktime_sub(tk->offs_real, ktime_set(tai_offset, 0));
}

/**
 * timekeeping_set_tai_offset - Sets the current TAI offset from UTC
 *
 */
void timekeeping_set_tai_offset(s32 tai_offset)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);
	__timekeeping_set_tai_offset(tk, tai_offset);
	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
	clock_was_set();
}

/**
 * change_clocksource - Swaps clocksources if a new one is available
 *
 * Accumulates current time interval and initializes new clocksource
 */
static int change_clocksource(void *data)
{
	struct timekeeper *tk = &timekeeper;
	struct clocksource *new, *old;
	unsigned long flags;

	new = (struct clocksource *) data;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	timekeeping_forward_now(tk);
	/*
	 * If the cs is in module, get a module reference. Succeeds
	 * for built-in code (owner == NULL) as well.
	 */
	if (try_module_get(new->owner)) {
		if (!new->enable || new->enable(new) == 0) {
			old = tk->clock;
			tk_setup_internals(tk, new);
			if (old->disable)
				old->disable(old);
			module_put(old->owner);
		} else {
			module_put(new->owner);
		}
	}
	timekeeping_update(tk, TK_CLEAR_NTP | TK_MIRROR | TK_CLOCK_WAS_SET);

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	return 0;
}

/**
 * timekeeping_notify - Install a new clock source
 * @clock:		pointer to the clock source
 *
 * This function is called from clocksource.c after a new, better clock
 * source has been registered. The caller holds the clocksource_mutex.
 */
int timekeeping_notify(struct clocksource *clock)
{
	struct timekeeper *tk = &timekeeper;

	if (tk->clock == clock)
		return 0;
	stop_machine(change_clocksource, clock, NULL);
	tick_clock_notify();
	return tk->clock == clock ? 0 : -1;
}

/**
 * ktime_get_real - get the real (wall-) time in ktime_t format
 *
 * returns the time in ktime_t format
 */
ktime_t ktime_get_real(void)
{
	struct timespec now;

	getnstimeofday(&now);

	return timespec_to_ktime(now);
}
EXPORT_SYMBOL_GPL(ktime_get_real);

/**
 * getrawmonotonic - Returns the raw monotonic time in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the raw monotonic time (completely un-modified by ntp)
 */
void getrawmonotonic(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	s64 nsecs;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		nsecs = timekeeping_get_ns_raw(tk);
		*ts = tk->raw_time;

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	timespec_add_ns(ts, nsecs);
}
EXPORT_SYMBOL(getrawmonotonic);

/**
 * timekeeping_valid_for_hres - Check if timekeeping is suitable for hres
 */
int timekeeping_valid_for_hres(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	int ret;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		ret = tk->clock->flags & CLOCK_SOURCE_VALID_FOR_HRES;

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	return ret;
}

/**
 * timekeeping_max_deferment - Returns max time the clocksource can be deferred
 */
u64 timekeeping_max_deferment(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	u64 ret;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		ret = tk->clock->max_idle_ns;

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	return ret;
}

/**
 * read_persistent_clock -  Return time from the persistent clock.
 *
 * Weak dummy function for arches that do not yet support it.
 * Reads the time from the battery backed persistent clock.
 * Returns a timespec with tv_sec=0 and tv_nsec=0 if unsupported.
 *
 *  XXX - Do be sure to remove it once all arches implement it.
 */
void __attribute__((weak)) read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

/**
 * read_boot_clock -  Return time of the system start.
 *
 * Weak dummy function for arches that do not yet support it.
 * Function to read the exact time the system has been started.
 * Returns a timespec with tv_sec=0 and tv_nsec=0 if unsupported.
 *
 *  XXX - Do be sure to remove it once all arches implement it.
 */
void __attribute__((weak)) read_boot_clock(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

/*
 * timekeeping_init - Initializes the clocksource and common timekeeping values
 */
void __init timekeeping_init(void)
{
	struct timekeeper *tk = &timekeeper;
	struct clocksource *clock;
	unsigned long flags;
	struct timespec now, boot, tmp;

	read_persistent_clock(&now);

	if (!timespec_valid_strict(&now)) {
		pr_warn("WARNING: Persistent clock returned invalid value!\n"
			"         Check your CMOS/BIOS settings.\n");
		now.tv_sec = 0;
		now.tv_nsec = 0;
	} else if (now.tv_sec || now.tv_nsec)
		persistent_clock_exist = true;

	read_boot_clock(&boot);
	if (!timespec_valid_strict(&boot)) {
		pr_warn("WARNING: Boot clock returned invalid value!\n"
			"         Check your CMOS/BIOS settings.\n");
		boot.tv_sec = 0;
		boot.tv_nsec = 0;
	}

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);
	ntp_init();

	clock = clocksource_default_clock();
	if (clock->enable)
		clock->enable(clock);
	tk_setup_internals(tk, clock);

	tk_set_xtime(tk, &now);
	tk->raw_time.tv_sec = 0;
	tk->raw_time.tv_nsec = 0;
	if (boot.tv_sec == 0 && boot.tv_nsec == 0)
		boot = tk_xtime(tk);

	set_normalized_timespec(&tmp, -boot.tv_sec, -boot.tv_nsec);
	tk_set_wall_to_mono(tk, tmp);

	tmp.tv_sec = 0;
	tmp.tv_nsec = 0;
	tk_set_sleep_time(tk, tmp);

	memcpy(&shadow_timekeeper, &timekeeper, sizeof(timekeeper));

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
}

/* time in seconds when suspend began */
static struct timespec timekeeping_suspend_time;

/**
 * __timekeeping_inject_sleeptime - Internal function to add sleep interval
 * @delta: pointer to a timespec delta value
 *
 * Takes a timespec offset measuring a suspend interval and properly
 * adds the sleep offset to the timekeeping variables.
 */
static void __timekeeping_inject_sleeptime(struct timekeeper *tk,
							struct timespec *delta)
{
	if (!timespec_valid_strict(delta)) {
		printk(KERN_WARNING "__timekeeping_inject_sleeptime: Invalid "
					"sleep delta value!\n");
		return;
	}
	tk_xtime_add(tk, delta);
	tk_set_wall_to_mono(tk, timespec_sub(tk->wall_to_monotonic, *delta));
	tk_set_sleep_time(tk, timespec_add(tk->total_sleep_time, *delta));
	tk_debug_account_sleep_time(delta);
}

/**
 * timekeeping_inject_sleeptime - Adds suspend interval to timeekeeping values
 * @delta: pointer to a timespec delta value
 *
 * This hook is for architectures that cannot support read_persistent_clock
 * because their RTC/persistent clock is only accessible when irqs are enabled.
 *
 * This function should only be called by rtc_resume(), and allows
 * a suspend offset to be injected into the timekeeping values.
 */
void timekeeping_inject_sleeptime(struct timespec *delta)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;

	/*
	 * Make sure we don't set the clock twice, as timekeeping_resume()
	 * already did it
	 */
	if (has_persistent_clock())
		return;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	timekeeping_forward_now(tk);

	__timekeeping_inject_sleeptime(tk, delta);

	timekeeping_update(tk, TK_CLEAR_NTP | TK_MIRROR | TK_CLOCK_WAS_SET);

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();
}

/**
 * timekeeping_resume - Resumes the generic timekeeping subsystem.
 *
 * This is for the generic clocksource timekeeping.
 * xtime/wall_to_monotonic/jiffies/etc are
 * still managed by arch specific suspend/resume code.
 */
static void timekeeping_resume(void)
{
	struct timekeeper *tk = &timekeeper;
	struct clocksource *clock = tk->clock;
	unsigned long flags;
	struct timespec ts_new, ts_delta;
	cycle_t cycle_now, cycle_delta;
	bool suspendtime_found = false;

	read_persistent_clock(&ts_new);

	clockevents_resume();
	clocksource_resume();

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	/*
	 * After system resumes, we need to calculate the suspended time and
	 * compensate it for the OS time. There are 3 sources that could be
	 * used: Nonstop clocksource during suspend, persistent clock and rtc
	 * device.
	 *
	 * One specific platform may have 1 or 2 or all of them, and the
	 * preference will be:
	 *	suspend-nonstop clocksource -> persistent clock -> rtc
	 * The less preferred source will only be tried if there is no better
	 * usable source. The rtc part is handled separately in rtc core code.
	 */
	cycle_now = clock->read(clock);
	if ((clock->flags & CLOCK_SOURCE_SUSPEND_NONSTOP) &&
		cycle_now > clock->cycle_last) {
		u64 num, max = ULLONG_MAX;
		u32 mult = clock->mult;
		u32 shift = clock->shift;
		s64 nsec = 0;

		cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

		/*
		 * "cycle_delta * mutl" may cause 64 bits overflow, if the
		 * suspended time is too long. In that case we need do the
		 * 64 bits math carefully
		 */
		do_div(max, mult);
		if (cycle_delta > max) {
			num = div64_u64(cycle_delta, max);
			nsec = (((u64) max * mult) >> shift) * num;
			cycle_delta -= num * max;
		}
		nsec += ((u64) cycle_delta * mult) >> shift;

		ts_delta = ns_to_timespec(nsec);
		suspendtime_found = true;
	} else if (timespec_compare(&ts_new, &timekeeping_suspend_time) > 0) {
		ts_delta = timespec_sub(ts_new, timekeeping_suspend_time);
		suspendtime_found = true;
	}

	if (suspendtime_found)
		__timekeeping_inject_sleeptime(tk, &ts_delta);

	/* Re-base the last cycle value */
	tk->cycle_last = clock->cycle_last = cycle_now;
	tk->ntp_error = 0;
	timekeeping_suspended = 0;
	timekeeping_update(tk, TK_MIRROR | TK_CLOCK_WAS_SET);
	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	touch_softlockup_watchdog();

	clockevents_notify(CLOCK_EVT_NOTIFY_RESUME, NULL);

	/* Resume hrtimers */
	hrtimers_resume();
}

static int timekeeping_suspend(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;
	struct timespec		delta, delta_delta;
	static struct timespec	old_delta;

	read_persistent_clock(&timekeeping_suspend_time);

	/*
	 * On some systems the persistent_clock can not be detected at
	 * timekeeping_init by its return value, so if we see a valid
	 * value returned, update the persistent_clock_exists flag.
	 */
	if (timekeeping_suspend_time.tv_sec || timekeeping_suspend_time.tv_nsec)
		persistent_clock_exist = true;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);
	timekeeping_forward_now(tk);
	timekeeping_suspended = 1;

	/*
	 * To avoid drift caused by repeated suspend/resumes,
	 * which each can add ~1 second drift error,
	 * try to compensate so the difference in system time
	 * and persistent_clock time stays close to constant.
	 */
	delta = timespec_sub(tk_xtime(tk), timekeeping_suspend_time);
	delta_delta = timespec_sub(delta, old_delta);
	if (abs(delta_delta.tv_sec)  >= 2) {
		/*
		 * if delta_delta is too large, assume time correction
		 * has occured and set old_delta to the current delta.
		 */
		old_delta = delta;
	} else {
		/* Otherwise try to adjust old_system to compensate */
		timekeeping_suspend_time =
			timespec_add(timekeeping_suspend_time, delta_delta);
	}
	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	clockevents_notify(CLOCK_EVT_NOTIFY_SUSPEND, NULL);
	clocksource_suspend();
	clockevents_suspend();

	return 0;
}

/* sysfs resume/suspend bits for timekeeping */
static struct syscore_ops timekeeping_syscore_ops = {
	.resume		= timekeeping_resume,
	.suspend	= timekeeping_suspend,
};

static int __init timekeeping_init_ops(void)
{
	register_syscore_ops(&timekeeping_syscore_ops);
	return 0;
}

device_initcall(timekeeping_init_ops);

/*
 * If the error is already larger, we look ahead even further
 * to compensate for late or lost adjustments.
 */
static __always_inline int timekeeping_bigadjust(struct timekeeper *tk,
						 s64 error, s64 *interval,
						 s64 *offset)
{
	s64 tick_error, i;
	u32 look_ahead, adj;
	s32 error2, mult;

	/*
	 * Use the current error value to determine how much to look ahead.
	 * The larger the error the slower we adjust for it to avoid problems
	 * with losing too many ticks, otherwise we would overadjust and
	 * produce an even larger error.  The smaller the adjustment the
	 * faster we try to adjust for it, as lost ticks can do less harm
	 * here.  This is tuned so that an error of about 1 msec is adjusted
	 * within about 1 sec (or 2^20 nsec in 2^SHIFT_HZ ticks).
	 */
	error2 = tk->ntp_error >> (NTP_SCALE_SHIFT + 22 - 2 * SHIFT_HZ);
	error2 = abs(error2);
	for (look_ahead = 0; error2 > 0; look_ahead++)
		error2 >>= 2;

	/*
	 * Now calculate the error in (1 << look_ahead) ticks, but first
	 * remove the single look ahead already included in the error.
	 */
	tick_error = ntp_tick_length() >> (tk->ntp_error_shift + 1);
	tick_error -= tk->xtime_interval >> 1;
	error = ((error - tick_error) >> look_ahead) + tick_error;

	/* Finally calculate the adjustment shift value.  */
	i = *interval;
	mult = 1;
	if (error < 0) {
		error = -error;
		*interval = -*interval;
		*offset = -*offset;
		mult = -1;
	}
	for (adj = 0; error > i; adj++)
		error >>= 1;

	*interval <<= adj;
	*offset <<= adj;
	return mult << adj;
}

/*
 * Adjust the multiplier to reduce the error value,
 * this is optimized for the most common adjustments of -1,0,1,
 * for other values we can do a bit more work.
 */
static void timekeeping_adjust(struct timekeeper *tk, s64 offset)
{
	s64 error, interval = tk->cycle_interval;
	int adj;

	/*
	 * The point of this is to check if the error is greater than half
	 * an interval.
	 *
	 * First we shift it down from NTP_SHIFT to clocksource->shifted nsecs.
	 *
	 * Note we subtract one in the shift, so that error is really error*2.
	 * This "saves" dividing(shifting) interval twice, but keeps the
	 * (error > interval) comparison as still measuring if error is
	 * larger than half an interval.
	 *
	 * Note: It does not "save" on aggravation when reading the code.
	 */
	error = tk->ntp_error >> (tk->ntp_error_shift - 1);
	if (error > interval) {
		/*
		 * We now divide error by 4(via shift), which checks if
		 * the error is greater than twice the interval.
		 * If it is greater, we need a bigadjust, if its smaller,
		 * we can adjust by 1.
		 */
		error >>= 2;
		/*
		 * XXX - In update_wall_time, we round up to the next
		 * nanosecond, and store the amount rounded up into
		 * the error. This causes the likely below to be unlikely.
		 *
		 * The proper fix is to avoid rounding up by using
		 * the high precision tk->xtime_nsec instead of
		 * xtime.tv_nsec everywhere. Fixing this will take some
		 * time.
		 */
		if (likely(error <= interval))
			adj = 1;
		else
			adj = timekeeping_bigadjust(tk, error, &interval, &offset);
	} else {
		if (error < -interval) {
			/* See comment above, this is just switched for the negative */
			error >>= 2;
			if (likely(error >= -interval)) {
				adj = -1;
				interval = -interval;
				offset = -offset;
			} else {
				adj = timekeeping_bigadjust(tk, error, &interval, &offset);
			}
		} else {
			goto out_adjust;
		}
	}

	if (unlikely(tk->clock->maxadj &&
		(tk->mult + adj > tk->clock->mult + tk->clock->maxadj))) {
		printk_once(KERN_WARNING
			"Adjusting %s more than 11%% (%ld vs %ld)\n",
			tk->clock->name, (long)tk->mult + adj,
			(long)tk->clock->mult + tk->clock->maxadj);
	}
	/*
	 * So the following can be confusing.
	 *
	 * To keep things simple, lets assume adj == 1 for now.
	 *
	 * When adj != 1, remember that the interval and offset values
	 * have been appropriately scaled so the math is the same.
	 *
	 * The basic idea here is that we're increasing the multiplier
	 * by one, this causes the xtime_interval to be incremented by
	 * one cycle_interval. This is because:
	 *	xtime_interval = cycle_interval * mult
	 * So if mult is being incremented by one:
	 *	xtime_interval = cycle_interval * (mult + 1)
	 * Its the same as:
	 *	xtime_interval = (cycle_interval * mult) + cycle_interval
	 * Which can be shortened to:
	 *	xtime_interval += cycle_interval
	 *
	 * So offset stores the non-accumulated cycles. Thus the current
	 * time (in shifted nanoseconds) is:
	 *	now = (offset * adj) + xtime_nsec
	 * Now, even though we're adjusting the clock frequency, we have
	 * to keep time consistent. In other words, we can't jump back
	 * in time, and we also want to avoid jumping forward in time.
	 *
	 * So given the same offset value, we need the time to be the same
	 * both before and after the freq adjustment.
	 *	now = (offset * adj_1) + xtime_nsec_1
	 *	now = (offset * adj_2) + xtime_nsec_2
	 * So:
	 *	(offset * adj_1) + xtime_nsec_1 =
	 *		(offset * adj_2) + xtime_nsec_2
	 * And we know:
	 *	adj_2 = adj_1 + 1
	 * So:
	 *	(offset * adj_1) + xtime_nsec_1 =
	 *		(offset * (adj_1+1)) + xtime_nsec_2
	 *	(offset * adj_1) + xtime_nsec_1 =
	 *		(offset * adj_1) + offset + xtime_nsec_2
	 * Canceling the sides:
	 *	xtime_nsec_1 = offset + xtime_nsec_2
	 * Which gives us:
	 *	xtime_nsec_2 = xtime_nsec_1 - offset
	 * Which simplfies to:
	 *	xtime_nsec -= offset
	 *
	 * XXX - TODO: Doc ntp_error calculation.
	 */
	tk->mult += adj;
	tk->xtime_interval += interval;
	tk->xtime_nsec -= offset;
	tk->ntp_error -= (interval - offset) << tk->ntp_error_shift;

out_adjust:
	/*
	 * It may be possible that when we entered this function, xtime_nsec
	 * was very small.  Further, if we're slightly speeding the clocksource
	 * in the code above, its possible the required corrective factor to
	 * xtime_nsec could cause it to underflow.
	 *
	 * Now, since we already accumulated the second, cannot simply roll
	 * the accumulated second back, since the NTP subsystem has been
	 * notified via second_overflow. So instead we push xtime_nsec forward
	 * by the amount we underflowed, and add that amount into the error.
	 *
	 * We'll correct this error next time through this function, when
	 * xtime_nsec is not as small.
	 */
	if (unlikely((s64)tk->xtime_nsec < 0)) {
		s64 neg = -(s64)tk->xtime_nsec;
		tk->xtime_nsec = 0;
		tk->ntp_error += neg << tk->ntp_error_shift;
	}

}

/**
 * accumulate_nsecs_to_secs - Accumulates nsecs into secs
 *
 * Helper function that accumulates a the nsecs greater then a second
 * from the xtime_nsec field to the xtime_secs field.
 * It also calls into the NTP code to handle leapsecond processing.
 *
 */
static inline unsigned int accumulate_nsecs_to_secs(struct timekeeper *tk)
{
	u64 nsecps = (u64)NSEC_PER_SEC << tk->shift;
	unsigned int action = 0;

	while (tk->xtime_nsec >= nsecps) {
		int leap;

		tk->xtime_nsec -= nsecps;
		tk->xtime_sec++;

		/* Figure out if its a leap sec and apply if needed */
		leap = second_overflow(tk->xtime_sec);
		if (unlikely(leap)) {
			struct timespec ts;

			tk->xtime_sec += leap;

			ts.tv_sec = leap;
			ts.tv_nsec = 0;
			tk_set_wall_to_mono(tk,
				timespec_sub(tk->wall_to_monotonic, ts));

			__timekeeping_set_tai_offset(tk, tk->tai_offset - leap);

			clock_was_set_delayed();
			action = TK_CLOCK_WAS_SET;
		}
	}
	return action;
}

/**
 * logarithmic_accumulation - shifted accumulation of cycles
 *
 * This functions accumulates a shifted interval of cycles into
 * into a shifted interval nanoseconds. Allows for O(log) accumulation
 * loop.
 *
 * Returns the unconsumed cycles.
 */
static cycle_t logarithmic_accumulation(struct timekeeper *tk, cycle_t offset,
						u32 shift)
{
	cycle_t interval = tk->cycle_interval << shift;
	u64 raw_nsecs;

	/* If the offset is smaller then a shifted interval, do nothing */
	if (offset < interval)
		return offset;

	/* Accumulate one shifted interval */
	offset -= interval;
	tk->cycle_last += interval;

	tk->xtime_nsec += tk->xtime_interval << shift;
	accumulate_nsecs_to_secs(tk);

	/* Accumulate raw time */
	raw_nsecs = (u64)tk->raw_interval << shift;
	raw_nsecs += tk->raw_time.tv_nsec;
	if (raw_nsecs >= NSEC_PER_SEC) {
		u64 raw_secs = raw_nsecs;
		raw_nsecs = do_div(raw_secs, NSEC_PER_SEC);
		tk->raw_time.tv_sec += raw_secs;
	}
	tk->raw_time.tv_nsec = raw_nsecs;

	/* Accumulate error between NTP and clock interval */
	tk->ntp_error += ntp_tick_length() << shift;
	tk->ntp_error -= (tk->xtime_interval + tk->xtime_remainder) <<
						(tk->ntp_error_shift + shift);

	return offset;
}

#ifdef CONFIG_GENERIC_TIME_VSYSCALL_OLD
static inline void old_vsyscall_fixup(struct timekeeper *tk)
{
	s64 remainder;

	/*
	* Store only full nanoseconds into xtime_nsec after rounding
	* it up and add the remainder to the error difference.
	* XXX - This is necessary to avoid small 1ns inconsistnecies caused
	* by truncating the remainder in vsyscalls. However, it causes
	* additional work to be done in timekeeping_adjust(). Once
	* the vsyscall implementations are converted to use xtime_nsec
	* (shifted nanoseconds), and CONFIG_GENERIC_TIME_VSYSCALL_OLD
	* users are removed, this can be killed.
	*/
	remainder = tk->xtime_nsec & ((1ULL << tk->shift) - 1);
	tk->xtime_nsec -= remainder;
	tk->xtime_nsec += 1ULL << tk->shift;
	tk->ntp_error += remainder << tk->ntp_error_shift;
	tk->ntp_error -= (1ULL << tk->shift) << tk->ntp_error_shift;
}
#else
#define old_vsyscall_fixup(tk)
#endif



/**
 * update_wall_time - Uses the current clocksource to increment the wall time
 *
 */
static void update_wall_time(void)
{
	struct clocksource *clock;
	struct timekeeper *real_tk = &timekeeper;
	struct timekeeper *tk = &shadow_timekeeper;
	cycle_t offset;
	int shift = 0, maxshift;
	unsigned int action;
	unsigned long flags;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);

	/* Make sure we're fully resumed: */
	if (unlikely(timekeeping_suspended))
		goto out;

	clock = real_tk->clock;

#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
	offset = real_tk->cycle_interval;
#else
	offset = (clock->read(clock) - clock->cycle_last) & clock->mask;
#endif

	/* Check if there's really nothing to do */
	if (offset < real_tk->cycle_interval)
		goto out;

	/*
	 * With NO_HZ we may have to accumulate many cycle_intervals
	 * (think "ticks") worth of time at once. To do this efficiently,
	 * we calculate the largest doubling multiple of cycle_intervals
	 * that is smaller than the offset.  We then accumulate that
	 * chunk in one go, and then try to consume the next smaller
	 * doubled multiple.
	 */
	shift = ilog2(offset) - ilog2(tk->cycle_interval);
	shift = max(0, shift);
	/* Bound shift to one less than what overflows tick_length */
	maxshift = (64 - (ilog2(ntp_tick_length())+1)) - 1;
	shift = min(shift, maxshift);
	while (offset >= tk->cycle_interval) {
		offset = logarithmic_accumulation(tk, offset, shift);
		if (offset < tk->cycle_interval<<shift)
			shift--;
	}

	/* correct the clock when NTP error is too big */
	timekeeping_adjust(tk, offset);

	/*
	 * XXX This can be killed once everyone converts
	 * to the new update_vsyscall.
	 */
	old_vsyscall_fixup(tk);

	/*
	 * Finally, make sure that after the rounding
	 * xtime_nsec isn't larger than NSEC_PER_SEC
	 */
	action = accumulate_nsecs_to_secs(tk);

	write_seqcount_begin(&timekeeper_seq);
	/* Update clock->cycle_last with the new value */
	clock->cycle_last = tk->cycle_last;
	/*
	 * Update the real timekeeper.
	 *
	 * We could avoid this memcpy by switching pointers, but that
	 * requires changes to all other timekeeper usage sites as
	 * well, i.e. move the timekeeper pointer getter into the
	 * spinlocked/seqcount protected sections. And we trade this
	 * memcpy under the timekeeper_seq against one before we start
	 * updating.
	 */
	memcpy(real_tk, tk, sizeof(*tk));
	timekeeping_update(real_tk, action);
	write_seqcount_end(&timekeeper_seq);
out:
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
}

/**
 * getboottime - Return the real time of system boot.
 * @ts:		pointer to the timespec to be set
 *
 * Returns the wall-time of boot in a timespec.
 *
 * This is based on the wall_to_monotonic offset and the total suspend
 * time. Calls to settimeofday will affect the value returned (which
 * basically means that however wrong your real time clock is at boot time,
 * you get the right time here).
 */
void getboottime(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec boottime = {
		.tv_sec = tk->wall_to_monotonic.tv_sec +
				tk->total_sleep_time.tv_sec,
		.tv_nsec = tk->wall_to_monotonic.tv_nsec +
				tk->total_sleep_time.tv_nsec
	};

	set_normalized_timespec(ts, -boottime.tv_sec, -boottime.tv_nsec);
}
EXPORT_SYMBOL_GPL(getboottime);

/**
 * get_monotonic_boottime - Returns monotonic time since boot
 * @ts:		pointer to the timespec to be set
 *
 * Returns the monotonic time since boot in a timespec.
 *
 * This is similar to CLOCK_MONTONIC/ktime_get_ts, but also
 * includes the time spent in suspend.
 */
void get_monotonic_boottime(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec tomono, sleep;
	s64 nsec;
	unsigned int seq;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		ts->tv_sec = tk->xtime_sec;
		nsec = timekeeping_get_ns(tk);
		tomono = tk->wall_to_monotonic;
		sleep = tk->total_sleep_time;

	} while (read_seqcount_retry(&timekeeper_seq, seq));

	ts->tv_sec += tomono.tv_sec + sleep.tv_sec;
	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsec + tomono.tv_nsec + sleep.tv_nsec);
}
EXPORT_SYMBOL_GPL(get_monotonic_boottime);

/**
 * ktime_get_boottime - Returns monotonic time since boot in a ktime
 *
 * Returns the monotonic time since boot in a ktime
 *
 * This is similar to CLOCK_MONTONIC/ktime_get, but also
 * includes the time spent in suspend.
 */
ktime_t ktime_get_boottime(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);
	return timespec_to_ktime(ts);
}
EXPORT_SYMBOL_GPL(ktime_get_boottime);

/**
 * monotonic_to_bootbased - Convert the monotonic time to boot based.
 * @ts:		pointer to the timespec to be converted
 */
void monotonic_to_bootbased(struct timespec *ts)
{
	struct timekeeper *tk = &timekeeper;

	*ts = timespec_add(*ts, tk->total_sleep_time);
}
EXPORT_SYMBOL_GPL(monotonic_to_bootbased);

unsigned long get_seconds(void)
{
	struct timekeeper *tk = &timekeeper;

	return tk->xtime_sec;
}
EXPORT_SYMBOL(get_seconds);

struct timespec __current_kernel_time(void)
{
	struct timekeeper *tk = &timekeeper;

	return tk_xtime(tk);
}

struct timespec current_kernel_time(void)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec now;
	unsigned long seq;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		now = tk_xtime(tk);
	} while (read_seqcount_retry(&timekeeper_seq, seq));

	return now;
}
EXPORT_SYMBOL(current_kernel_time);

struct timespec get_monotonic_coarse(void)
{
	struct timekeeper *tk = &timekeeper;
	struct timespec now, mono;
	unsigned long seq;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		now = tk_xtime(tk);
		mono = tk->wall_to_monotonic;
	} while (read_seqcount_retry(&timekeeper_seq, seq));

	set_normalized_timespec(&now, now.tv_sec + mono.tv_sec,
				now.tv_nsec + mono.tv_nsec);
	return now;
}

/*
 * Must hold jiffies_lock
 */
void do_timer(unsigned long ticks)
{
	jiffies_64 += ticks;
	update_wall_time();
	calc_global_load(ticks);
}

/**
 * get_xtime_and_monotonic_and_sleep_offset() - get xtime, wall_to_monotonic,
 *    and sleep offsets.
 * @xtim:	pointer to timespec to be set with xtime
 * @wtom:	pointer to timespec to be set with wall_to_monotonic
 * @sleep:	pointer to timespec to be set with time in suspend
 */
void get_xtime_and_monotonic_and_sleep_offset(struct timespec *xtim,
				struct timespec *wtom, struct timespec *sleep)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		*xtim = tk_xtime(tk);
		*wtom = tk->wall_to_monotonic;
		*sleep = tk->total_sleep_time;
	} while (read_seqcount_retry(&timekeeper_seq, seq));
}

#ifdef CONFIG_HIGH_RES_TIMERS
/**
 * ktime_get_update_offsets - hrtimer helper
 * @offs_real:	pointer to storage for monotonic -> realtime offset
 * @offs_boot:	pointer to storage for monotonic -> boottime offset
 * @offs_tai:	pointer to storage for monotonic -> clock tai offset
 *
 * Returns current monotonic time and updates the offsets
 * Called from hrtimer_interrupt() or retrigger_next_event()
 */
ktime_t ktime_get_update_offsets(ktime_t *offs_real, ktime_t *offs_boot,
							ktime_t *offs_tai)
{
	struct timekeeper *tk = &timekeeper;
	ktime_t now;
	unsigned int seq;
	u64 secs, nsecs;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);

		secs = tk->xtime_sec;
		nsecs = timekeeping_get_ns(tk);

		*offs_real = tk->offs_real;
		*offs_boot = tk->offs_boot;
		*offs_tai = tk->offs_tai;
	} while (read_seqcount_retry(&timekeeper_seq, seq));

	now = ktime_add_ns(ktime_set(secs, 0), nsecs);
	now = ktime_sub(now, *offs_real);
	return now;
}
#endif

/**
 * ktime_get_monotonic_offset() - get wall_to_monotonic in ktime_t format
 */
ktime_t ktime_get_monotonic_offset(void)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long seq;
	struct timespec wtom;

	do {
		seq = read_seqcount_begin(&timekeeper_seq);
		wtom = tk->wall_to_monotonic;
	} while (read_seqcount_retry(&timekeeper_seq, seq));

	return timespec_to_ktime(wtom);
}
EXPORT_SYMBOL_GPL(ktime_get_monotonic_offset);

/**
 * do_adjtimex() - Accessor function to NTP __do_adjtimex function
 */
int do_adjtimex(struct timex *txc)
{
	struct timekeeper *tk = &timekeeper;
	unsigned long flags;
	struct timespec ts;
	s32 orig_tai, tai;
	int ret;

	/* Validate the data before disabling interrupts */
	ret = ntp_validate_timex(txc);
	if (ret)
		return ret;

	if (txc->modes & ADJ_SETOFFSET) {
		struct timespec delta;
		delta.tv_sec  = txc->time.tv_sec;
		delta.tv_nsec = txc->time.tv_usec;
		if (!(txc->modes & ADJ_NANO))
			delta.tv_nsec *= 1000;
		ret = timekeeping_inject_offset(&delta);
		if (ret)
			return ret;
	}

	getnstimeofday(&ts);

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	orig_tai = tai = tk->tai_offset;
	ret = __do_adjtimex(txc, &ts, &tai);

	if (tai != orig_tai) {
		__timekeeping_set_tai_offset(tk, tai);
		update_pvclock_gtod(tk, true);
		clock_was_set_delayed();
	}
	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);

	ntp_notify_cmos_timer();

	return ret;
}

#ifdef CONFIG_NTP_PPS
/**
 * hardpps() - Accessor function to NTP __hardpps function
 */
void hardpps(const struct timespec *phase_ts, const struct timespec *raw_ts)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&timekeeper_lock, flags);
	write_seqcount_begin(&timekeeper_seq);

	__hardpps(phase_ts, raw_ts);

	write_seqcount_end(&timekeeper_seq);
	raw_spin_unlock_irqrestore(&timekeeper_lock, flags);
}
EXPORT_SYMBOL(hardpps);
#endif

/**
 * xtime_update() - advances the timekeeping infrastructure
 * @ticks:	number of ticks, that have elapsed since the last call.
 *
 * Must be called with interrupts disabled.
 */
void xtime_update(unsigned long ticks)
{
	write_seqlock(&jiffies_lock);
	do_timer(ticks);
	write_sequnlock(&jiffies_lock);
}
