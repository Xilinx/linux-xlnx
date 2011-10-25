/*
 * Xilinx SMP source file. It contains platform specific fucntions
 * needed for the linux smp kernel.
 *
 * Copyright (C) 2010 Xilinx, Inc.
 *
 * This file is based on arm omap smp platform file and arm
 * realview smp platform.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 * Copyright (c) 2002 ARM Limited.
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
#include <asm/hardware/gic.h>
#include <mach/hardware.h>

#include <asm/cacheflush.h>
#include <asm/smp.h>

extern void xilinx_secondary_startup(void);

/* SCU base address */
static void __iomem *scu_base = (void *)SCU_PERIPH_BASE;

/*
 * Use SCU config register to count number of cores
 */
static inline unsigned int get_core_count(void)
{
	if (scu_base)
		return scu_get_core_count(scu_base);
	return 1;
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();

	/*
	 * If any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */

	gic_secondary_init(0);

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
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	printk("Xilinx SMP: booting CPU1 now\n");

	/*
	 * Update boot lock register with the boot key to allow the 
	 * secondary processor to start the kernel. The function, 
	 * xilinx_secondary_startup(), will hold the secondary core 
	 * till the boot register lock is updated with a key.
	 */
	__raw_writel(BOOT_LOCK_KEY, BOOT_REG_BASE + BOOT_LOCKREG_OFFSET);

	/* Flush the kernel cache to ensure that the page tables are 
	 * available for the secondary CPU to use. A barrier is added 
	 * to ensure that write buffer is drained.
	 */
	flush_cache_all();
	smp_wmb();

	/*
	 * Send a 'sev' to wake the secondary core from WFE. Make sure 
	 * to do this after writing to the key and flushing the cache to 
	 * ensure that CPU1 sees the boot key when it wakes up.
	 */
	sev();

	/* Give the secondary CPU some time to start the kernel. 
	 */
	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout))
		;
	/*
	 * Now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

static void __init wakeup_secondary(void)
{
	/* Initialize the boot lock register to prevent CPU1 from 
	   starting the kernel before CPU0 is ready for that.
	*/
	__raw_writel(0, BOOT_REG_BASE + BOOT_LOCKREG_OFFSET);

	/*
	 * Write the address of secondary startup routine into the
	 * boot address register. The secondary CPU will use this value 
	 * to get into the kernel after it's awake (in WFE state now).
	 *
	 * Note the physical address is needed as the secondary CPU
	 * will not have the MMU on yet. A barrier is added to ensure
	 * that write buffer is drained.
	 */
	__raw_writel(virt_to_phys(xilinx_secondary_startup), 
					BOOT_REG_BASE + BOOT_ADDRREG_OFFSET);
	smp_wmb();

	/*
	 * Send a 'sev' to wake the secondary core from WFE. 
	 *
	 * Secondary CPU kernel startup is a 2 phase process.
	 * 1st phase is transition from a boot loader to the kernel, but 
	 * then wait not starting the kernel yet. 2nd phase to start the 
	 * the kernel. In both phases, the secondary CPU uses WFE.
	 */
	sev();
	mb();
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores = get_core_count();

	/* sanity check */
	if (ncores == 0) {
		printk(KERN_ERR
		       "Xilinx: strange core count of 0? Default to 1\n");
		ncores = 1;
	}

	if (ncores > NR_CPUS) {
		printk(KERN_WARNING
		       "Xilinx: no. of cores (%d) greater than configured "
		       "maximum of %d - clipping\n",
		       ncores, NR_CPUS);
		ncores = NR_CPUS;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
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

	/*
	 * Initialize the SCU and wake up the secondary core 
	 */
	scu_enable(scu_base);
	wakeup_secondary();
}
