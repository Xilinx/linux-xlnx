/*
 * arch/microblaze/kernel/heartbeat.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/sched.h>
#include <asm/page.h>
#include <asm/io.h>

void heartbeat(void)
{
#if 0
	static unsigned int cnt = 0, period = 0, dist = 0;

	if (cnt == 0 || cnt == dist) {
		iowrite32(1, XPAR_LEDS_4BIT_BASEADDR);
	} else if (cnt == 7 || cnt == dist + 7) {
		iowrite32(0, XPAR_LEDS_4BIT_BASEADDR);
	}

	if (++cnt > period) {
		cnt = 0;

		/*
		 * The hyperbolic function below modifies the heartbeat period
		 * length in dependency of the current (5min) load. It goes
		 * through the points f(0)=126, f(1)=86, f(5)=51, f(inf)->30.
		 */
		period = ((672 << FSHIFT) / (5 * avenrun[0] +
					    (7 << FSHIFT))) + 30;
		dist = period / 4;
	}
#endif
}
