/*
 * arch/arm/mach-zynq/platsmp.c
 *
 * This file contains Xilinx specific SMP code, used to start up
 * the second processor.
 *
 * Copyright (C) 2011 Xilinx
 *
 * based on linux/arch/arm/mach-realview/platsmp.c
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
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <mach/zynq_soc.h>
#include <mach/smp.h>
#include "common.h"

static DEFINE_SPINLOCK(boot_lock);

/* Secondary CPU kernel startup is a 2 step process. The primary CPU
 * starts the secondary CPU by giving it the address of the kernel and
 * then sending it an event to wake it up. The secondary CPU then
 * starts the kernel and tells the primary CPU it's up and running.
 */
void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);

	/* Indicate to the primary core that the secondary is up and running.
	 * Let the write buffer drain.
	 */
	__raw_writel(BOOT_STATUS_CPU1_UP, OCM_HIGH_BASE + BOOT_STATUS_OFFSET);
	wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/* Initialize the boot status and give the secondary core
	 * the start address of the kernel, let the write buffer drain
	 */
	__raw_writel(0, OCM_HIGH_BASE + BOOT_STATUS_OFFSET);

	__raw_writel(virt_to_phys(secondary_startup),
					OCM_HIGH_BASE + BOOT_ADDR_OFFSET);
	wmb();

	/*
	 * Send an event to wake the secondary core from WFE state.
	 */
	sev();

	/*
	 * Wait for the other CPU to boot, but timeout if it doesn't
	 */
	timeout = jiffies + (1 * HZ);
	while ((__raw_readl(OCM_HIGH_BASE + BOOT_STATUS_OFFSET) !=
				BOOT_STATUS_CPU1_UP) &&
				(time_before(jiffies, timeout)))
		rmb();

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	int i, ncores;

	ncores = scu_get_core_count(SCU_PERIPH_BASE);

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	scu_enable(SCU_PERIPH_BASE);
}
