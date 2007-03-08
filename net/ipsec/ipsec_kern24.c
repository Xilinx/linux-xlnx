/*
 * Copyright 2005 (C) Michael Richardson <mcr@xelerance.com>
 *
 * This is a file of functions which are present in 2.6 kernels,
 * but are not available by default in the 2.4 series.
 *
 * As such this code is usually from the Linux kernel, and is covered by
 * GPL.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * $Id: ipsec_kern24.c,v 1.2 2005/05/20 03:19:18 mcr Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/spinlock.h>

/*
 * printk rate limiting, lifted from the networking subsystem.
 *
 * This enforces a rate limit: not more than one kernel message
 * every printk_ratelimit_jiffies to make a denial-of-service
 * attack impossible.
 */
static spinlock_t ratelimit_lock = SPIN_LOCK_UNLOCKED;

int __printk_ratelimit(int ratelimit_jiffies, int ratelimit_burst)
{
	static unsigned long toks = 10*5*HZ;
	static unsigned long last_msg;
	static int missed;
	unsigned long flags;
	unsigned long now = jiffies;

	spin_lock_irqsave(&ratelimit_lock, flags);
	toks += now - last_msg;
	last_msg = now;
	if (toks > (ratelimit_burst * ratelimit_jiffies))
		toks = ratelimit_burst * ratelimit_jiffies;
	if (toks >= ratelimit_jiffies) {
		int lost = missed;
		missed = 0;
		toks -= ratelimit_jiffies;
		spin_unlock_irqrestore(&ratelimit_lock, flags);
		if (lost)
			printk(KERN_WARNING "printk: %d messages suppressed.\n", lost);
		return 1;
	}
	missed++;
	spin_unlock_irqrestore(&ratelimit_lock, flags);
	return 0;
}

/* minimum time in jiffies between messages */
int printk_ratelimit_jiffies = 5*HZ;

/* number of messages we send before ratelimiting */
int printk_ratelimit_burst = 10;

int printk_ratelimit(void)
{
	return __printk_ratelimit(printk_ratelimit_jiffies,
				printk_ratelimit_burst);
}
