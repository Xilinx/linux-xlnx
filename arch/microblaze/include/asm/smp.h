/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * smp.h: MicroBlaze-specific SMP code
 *
 * Original was a copy of PowerPC smp.h, which was a copy of
 * sparc smp.h.  Now heavily modified for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 * Copyright (C) 2013-2020 Xilinx, Inc.
 */

#ifndef _ASM_MICROBLAZE_SMP_H
#define _ASM_MICROBLAZE_SMP_H

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>

#include <asm/percpu.h>

void handle_IPI(int ipinr, struct pt_regs *regs);

void set_smp_cross_call(void (*)(unsigned int, unsigned int));

void smp_send_debugger_break(void);

#define raw_smp_processor_id()		(current_thread_info()->cpu)

enum microblaze_msg {
	MICROBLAZE_MSG_RESCHEDULE = 0,
	MICROBLAZE_MSG_CALL_FUNCTION,
	MICROBLAZE_MSG_CALL_FUNCTION_SINGLE,
	MICROBLAZE_MSG_DEBUGGER_BREAK,
	MICROBLAZE_NUM_IPIS
};

void start_secondary(void);
extern struct thread_info *secondary_ti;
void secondary_machine_init(void);

void arch_send_call_function_single_ipi(int cpu);
void arch_send_call_function_ipi_mask(const struct cpumask *mask);

#endif /* _ASM_MICROBLAZE_SMP_H */
