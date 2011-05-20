/*
 * arch/arm/mach-zynq/localtimer.c
 *
 * Both cortex-a9 cores have their own timer in it's CPU domain.
 *
 * Copyright (C) 2011 Xilinx, Inc.
 *
 * This file is based on arch/arm/plat-versatile/localtimer.c
 *
 * Copyright (C) 2002 ARM Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/clockchips.h>
#include <asm/smp_twd.h>
#include <asm/localtimer.h>
#include <mach/zynq_soc.h>

/*
 * Setup the local clock events for a CPU.
 */
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
	twd_base = SCU_CPU_TIMER_BASE;

	evt->irq = 29;
	twd_timer_setup(evt);
	return 0;
}
