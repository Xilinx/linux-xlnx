/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Xilinx, Inc.
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef _ASM_MICROBLAZE_HARDIRQ_H
#define _ASM_MICROBLAZE_HARDIRQ_H

# ifndef CONFIG_SMP
#include <asm-generic/hardirq.h>
# else
#include <linux/cache.h>
#include <linux/percpu.h>
#include <linux/threads.h>
#include <asm/irq.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned int ipi_irqs[MICROBLAZE_NUM_IPIS];
} ____cacheline_aligned irq_cpustat_t;

#define __ARCH_IRQ_STAT
DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define local_softirq_pending_ref	irq_stat.__softirq_pending

#define __inc_irq_stat(cpu, member)	this_cpu_inc(irq_stat.member)
#define __get_irq_stat(cpu, member)	this_cpu_read(irq_stat.member)

u64 smp_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	smp_irq_stat_cpu

extern unsigned long irq_err_count;

static inline void ack_bad_irq(unsigned int irq)
{
	irq_err_count++;
}
# endif /* CONFIG_MMU */

#endif /* _ASM_MICROBLAZE_HARDIRQ_H */
