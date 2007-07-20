/*
 * include/asm-microblaze/hardirq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_HARDIRQ_H
#define _ASM_HARDIRQ_H

#include <linux/cache.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/current.h>
#include <asm/ptrace.h>

/* should be defined in each interrupt controller driver */
extern unsigned int get_irq(struct pt_regs *regs);

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

void ack_bad_irq(unsigned int irq);

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#endif /* _ASM_HARDIRQ_H */
