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
#include <asm/bug.h>

extern void system_timer_init(void);

/* FIXME */
void time_init(void)
{
	system_timer_init();
}

/* FIXME */
int do_settimeofday(struct timespec *tv)
{
	BUG();
	return 0;
}

unsigned long do_gettimeoffset(void)
{
	return 0UL;
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick = tick_usec - tickadj;

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
