/*
 * arch/microblaze/kernel/time.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <asm/bug.h>

extern void system_timer_init(void);
extern unsigned long do_gettimeoffset(void);

void time_init(void)
{
	system_timer_init();
}

int do_settimeofday(struct timespec *tv)
{
	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq (&xtime_lock);

	/* This is revolting. We need to set the xtime.tv_nsec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
#if 0
	tv->tv_nsec -= mach_gettimeoffset() * 1000;
#endif

	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;

        time_adjust = 0;
        time_status |= STA_UNSYNC;
        time_maxerror = NTP_PHASE_LIMIT;
        time_esterror = NTP_PHASE_LIMIT;

	write_sequnlock_irq (&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin(&xtime_lock);
		usec = do_gettimeoffset();
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry(&xtime_lock, seq));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}
