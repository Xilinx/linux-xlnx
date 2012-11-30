/*
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
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/hardware/gic.h>
#include <mach/system.h>
#include <mach/zynq_soc.h>
#include "common.h"

extern void secondary_startup(void);

static DEFINE_SPINLOCK(boot_lock);

/* Store pointer to ioremap area which points to address 0x0 */
static u8 __iomem *zero;

static unsigned int mem_backup[3];
static unsigned int mem_backup_done;

/*
 * Store number of cores in the system
 * Because of scu_get_core_count() must be in __init section and can't
 * be called from zynq_cpun_start() because it is in __cpuinit section.
 */
static int ncores;

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

	/* Restore memory content */
	if (mem_backup_done) {
		__raw_writel(mem_backup[0], zero + 0x0);
		__raw_writel(mem_backup[1], zero + 0x4);
		__raw_writel(mem_backup[2], zero + 0x8);
	}


	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

/*
 * Store pointer to SLCR registers. SLCR driver can't be used because
 * it is not initialized yet and this code is used for bootup the second CPU
 */
#define SLCR_UNLOCK	0xDF0D
#define SLCR_LOCK	0x767B

static u8 __iomem *slcr;

int __cpuinit zynq_cpun_start(u32 address, int cpu)
{
	if (cpu > ncores) {
		pr_warn("CPU No. is not available in the system\n");
		return -1;
	}

	if (!slcr) {
		printk(KERN_INFO "Map SLCR registers\n");
		/*
		 * Remap the SLCR registers to be able to work
		 * with secondary CPUs
		 */
		slcr = ioremap(0xF8000000, PAGE_SIZE);
		if (!slcr) {
			printk(KERN_WARNING
				"!!!! SLCR jump vectors can't be used !!!!\n");
			return -1;
		}
	}
	mem_backup_done = 0;

	/* MS: Expectation that SLCR are directly map and accessible */
	/* Not possible to jump to non aligned address */
	if (!(address & 3) && (!address || (address >= 0xC))) {
		__raw_writel(SLCR_UNLOCK, slcr + 0x8); /* UNLOCK SLCR */
		/* stop CLK and reset CPUn */
		__raw_writel(0x11 << cpu, slcr + 0x244);

		/*
		 * This is elegant way how to jump to any address
		 * 0x0: Load address at 0x8 to r0
		 * 0x4: Jump by mov instruction
		 * 0x8: Jumping address
		 */
		if (address) {
			if (!zero) {
				printk(KERN_WARNING
					"BOOTUP jump vectors is not mapped!\n");
				return -1;
			}
			mem_backup[0] = __raw_readl(zero + 0x0);
			mem_backup[1] = __raw_readl(zero + 0x4);
			mem_backup[2] = __raw_readl(zero + 0x8);
			mem_backup_done = 1;
			__raw_writel(0xe59f0000, zero + 0x0);/* 0:ldr r0, [8] */
			__raw_writel(0xe1a0f000, zero + 0x4);/* 4:mov pc, r0 */
			__raw_writel(address, zero + 0x8);/* 8:.word address */
		}

		flush_cache_all();
		outer_flush_all();
		wmb();

		__raw_writel(0x10 << cpu, slcr + 0x244); /* enable CPUn */
		__raw_writel(0x0, slcr + 0x244); /* enable CLK for CPUn */

		/* the SLCR locking/unlocking needs to be re-done, for now
		 * there is not centralized locking/unlocking so leave it
		 * unlocked
		 */
#if 0
		__raw_writel(SLCR_LOCK, slcr + 0x4); /* LOCK SLCR */
#endif
		return 0;
	}

	pr_warn("Can't start CPU%d: Wrong starting address %x\n", cpu, address);

	return -1;
}
EXPORT_SYMBOL(zynq_cpun_start);

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	ret = zynq_cpun_start(virt_to_phys(secondary_startup), cpu);
	if (ret) {
		spin_unlock(&boot_lock);
		return -1;
	}

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
	int i;

	ncores = scu_get_core_count(SCU_PERIPH_BASE);

	for (i = 0; i < ncores && i < CONFIG_NR_CPUS; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	 * Remap the first three addresses at zero which are used
	 * for 32bit long jump for SMP. Look at zynq_cpun_start()
	 */
#if defined(CONFIG_PHYS_OFFSET) && (CONFIG_PHYS_OFFSET != 0)
	zero = ioremap(0, 12);
	if (!zero) {
		printk(KERN_WARNING
			"!!!! BOOTUP jump vectors can't be used !!!!\n");
		while (1)
			;
	}
#else
	/* The first three addresses at zero are already mapped */
	zero = (__force u8 __iomem *)CONFIG_PAGE_OFFSET;
#endif

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	scu_enable(SCU_PERIPH_BASE);
}

