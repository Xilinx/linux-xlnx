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

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define __inc_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)++
#define __get_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)

u64 smp_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	smp_irq_stat_cpu

extern unsigned long irq_err_count;

static inline void ack_bad_irq(unsigned int irq)
{
	irq_err_count++;
}
# endif /* CONFIG_MMU */

#endif /* _ASM_MICROBLAZE_HARDIRQ_H */
