/*
 * The local timer source file. Both cortex-a9 cores have
 * own timer in it's CPU domain. These timers will be driving the
 * linux kernel SMP tick framework when active. 
 *
 * Copyright (C) 2010 Xilinx, Inc.
 *
 * This file is based on arm realview smp platform file.
 * Copyright (C) 2002 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/clockchips.h>
#include <asm/irq.h>
#include <asm/smp_twd.h>
#include <asm/localtimer.h>

int local_timer_setup(struct clock_event_device *);

/*
 * Setup the local clock events for a CPU.
 */
int __cpuinit local_timer_setup(struct clock_event_device *evt)
{
	twd_base = (void __iomem *)SCU_CPU_TIMER_BASE;

	evt->irq = IRQ_SCU_CPU_TIMER;
	twd_timer_setup(evt);
	return 0;
}

