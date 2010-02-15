/*
 * OMAP4 SMP source file. It contains platform specific fucntions
 * needed for the linux smp kernel.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author:
 *      Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Platform file needed for the OMAP4 SMP. This file is based on arm
 * realview smp platform.
 * * Copyright (c) 2002 ARM Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/localtimer.h>
#include <asm/smp_scu.h>
#include <mach/hardware.h>


/* SCU base address */
// static void __iomem *scu_base = SCU_PERIPH_BASE;

/*
 * Use SCU config register to count number of cores
 */
static inline unsigned int get_core_count(void)
{
	return 1;	// Hacked by JHL
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	return; 	// Hacked by JHL

}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return 0;	// Hacked by JHL
}

static void __init wakeup_secondary(void)
{
	return; 	// Hacked by JHL

}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	return;		// Hacked by JHL
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	return; 	// Hacked by JHL
}
