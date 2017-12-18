/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * RajeshwarR: Dec 11, 2007
 *   -- Added support for Inter Processor Interrupts
 *
 * Vineetg: Nov 1st, 2007
 *    -- Initial Write (Borrowed heavily from ARM)
 */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/profile.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/reboot.h>
#include <linux/irqdomain.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/mach_desc.h>

#ifndef CONFIG_ARC_HAS_LLSC
arch_spinlock_t smp_atomic_ops_lock = __ARCH_SPIN_LOCK_UNLOCKED;
arch_spinlock_t smp_bitops_lock = __ARCH_SPIN_LOCK_UNLOCKED;
#endif

struct plat_smp_ops  __weak plat_smp_ops;

/* XXX: per cpu ? Only needed once in early seconday boot */
struct task_struct *secondary_idle_tsk;

/* Called from start_kernel */
void __init smp_prepare_boot_cpu(void)
{
}

/*
 * Called from setup_arch() before calling setup_processor()
 *
 * - Initialise the CPU possible map early - this describes the CPUs
 *   which may be present or become present in the system.
 * - Call early smp init hook. This can initialize a specific multi-core
 *   IP which is say common to several platforms (hence not part of
 *   platform specific int_early() hook)
 */
void __init smp_init_cpus(void)
{
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++)
		set_cpu_possible(i, true);

	if (plat_smp_ops.init_early_smp)
		plat_smp_ops.init_early_smp();
}

/* called from init ( ) =>  process 1 */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	 * if platform didn't set the present map already, do it now
	 * boot cpu is set to present already by init/main.c
	 */
	if (num_present_cpus() <= 1) {
		for (i = 0; i < max_cpus; i++)
			set_cpu_present(i, true);
	}
}

void __init smp_cpus_done(unsigned int max_cpus)
{

}

/*
 * Default smp boot helper for Run-on-reset case where all cores start off
 * together. Non-masters need to wait for Master to start running.
 * This is implemented using a flag in memory, which Non-masters spin-wait on.
 * Master sets it to cpu-id of core to "ungate" it.
 */
static volatile int wake_flag;

static void arc_default_smp_cpu_kick(int cpu, unsigned long pc)
{
	BUG_ON(cpu == 0);
	wake_flag = cpu;
}

void arc_platform_smp_wait_to_boot(int cpu)
{
	while (wake_flag != cpu)
		;

	wake_flag = 0;
	__asm__ __volatile__("j @first_lines_of_secondary	\n");
}


const char *arc_platform_smp_cpuinfo(void)
{
	return plat_smp_ops.info ? : "";
}

/*
 * The very first "C" code executed by secondary
 * Called from asm stub in head.S
 * "current"/R25 already setup by low level boot code
 */
void start_kernel_secondary(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	/* MMU, Caches, Vector Table, Interrupts etc */
	setup_processor();

	atomic_inc(&mm->mm_users);
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	/* Some SMP H/w setup - for each cpu */
	if (plat_smp_ops.init_per_cpu)
		plat_smp_ops.init_per_cpu(cpu);

	if (machine_desc->init_per_cpu)
		machine_desc->init_per_cpu(cpu);

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);

	pr_info("## CPU%u LIVE ##: Executing Code...\n", cpu);

	local_irq_enable();
	preempt_disable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

/*
 * Called from kernel_init( ) -> smp_init( ) - for each CPU
 *
 * At this point, Secondary Processor  is "HALT"ed:
 *  -It booted, but was halted in head.S
 *  -It was configured to halt-on-reset
 *  So need to wake it up.
 *
 * Essential requirements being where to run from (PC) and stack (SP)
*/
int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	unsigned long wait_till;

	secondary_idle_tsk = idle;

	pr_info("Idle Task [%d] %p", cpu, idle);
	pr_info("Trying to bring up CPU%u ...\n", cpu);

	if (plat_smp_ops.cpu_kick)
		plat_smp_ops.cpu_kick(cpu,
				(unsigned long)first_lines_of_secondary);
	else
		arc_default_smp_cpu_kick(cpu, (unsigned long)NULL);

	/* wait for 1 sec after kicking the secondary */
	wait_till = jiffies + HZ;
	while (time_before(jiffies, wait_till)) {
		if (cpu_online(cpu))
			break;
	}

	if (!cpu_online(cpu)) {
		pr_info("Timeout: CPU%u FAILED to comeup !!!\n", cpu);
		return -1;
	}

	secondary_idle_tsk = NULL;

	return 0;
}

/*
 * not supported here
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*****************************************************************************/
/*              Inter Processor Interrupt Handling                           */
/*****************************************************************************/

enum ipi_msg_type {
	IPI_EMPTY = 0,
	IPI_RESCHEDULE = 1,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
};

/*
 * In arches with IRQ for each msg type (above), receiver can use IRQ-id  to
 * figure out what msg was sent. For those which don't (ARC has dedicated IPI
 * IRQ), the msg-type needs to be conveyed via per-cpu data
 */

static DEFINE_PER_CPU(unsigned long, ipi_data);

static void ipi_send_msg_one(int cpu, enum ipi_msg_type msg)
{
	unsigned long __percpu *ipi_data_ptr = per_cpu_ptr(&ipi_data, cpu);
	unsigned long old, new;
	unsigned long flags;

	pr_debug("%d Sending msg [%d] to %d\n", smp_processor_id(), msg, cpu);

	local_irq_save(flags);

	/*
	 * Atomically write new msg bit (in case others are writing too),
	 * and read back old value
	 */
	do {
		new = old = ACCESS_ONCE(*ipi_data_ptr);
		new |= 1U << msg;
	} while (cmpxchg(ipi_data_ptr, old, new) != old);

	/*
	 * Call the platform specific IPI kick function, but avoid if possible:
	 * Only do so if there's no pending msg from other concurrent sender(s).
	 * Otherwise, recevier will see this msg as well when it takes the
	 * IPI corresponding to that msg. This is true, even if it is already in
	 * IPI handler, because !@old means it has not yet dequeued the msg(s)
	 * so @new msg can be a free-loader
	 */
	if (plat_smp_ops.ipi_send && !old)
		plat_smp_ops.ipi_send(cpu);

	local_irq_restore(flags);
}

static void ipi_send_msg(const struct cpumask *callmap, enum ipi_msg_type msg)
{
	unsigned int cpu;

	for_each_cpu(cpu, callmap)
		ipi_send_msg_one(cpu, msg);
}

void smp_send_reschedule(int cpu)
{
	ipi_send_msg_one(cpu, IPI_RESCHEDULE);
}

void smp_send_stop(void)
{
	struct cpumask targets;
	cpumask_copy(&targets, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &targets);
	ipi_send_msg(&targets, IPI_CPU_STOP);
}

void arch_send_call_function_single_ipi(int cpu)
{
	ipi_send_msg_one(cpu, IPI_CALL_FUNC);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	ipi_send_msg(mask, IPI_CALL_FUNC);
}

/*
 * ipi_cpu_stop - handle IPI from smp_send_stop()
 */
static void ipi_cpu_stop(void)
{
	machine_halt();
}

static inline int __do_IPI(unsigned long msg)
{
	int rc = 0;

	switch (msg) {
	case IPI_RESCHEDULE:
		scheduler_ipi();
		break;

	case IPI_CALL_FUNC:
		generic_smp_call_function_interrupt();
		break;

	case IPI_CPU_STOP:
		ipi_cpu_stop();
		break;

	default:
		rc = 1;
	}

	return rc;
}

/*
 * arch-common ISR to handle for inter-processor interrupts
 * Has hooks for platform specific IPI
 */
irqreturn_t do_IPI(int irq, void *dev_id)
{
	unsigned long pending;
	unsigned long __maybe_unused copy;

	pr_debug("IPI [%ld] received on cpu %d\n",
		 *this_cpu_ptr(&ipi_data), smp_processor_id());

	if (plat_smp_ops.ipi_clear)
		plat_smp_ops.ipi_clear(irq);

	/*
	 * "dequeue" the msg corresponding to this IPI (and possibly other
	 * piggybacked msg from elided IPIs: see ipi_send_msg_one() above)
	 */
	copy = pending = xchg(this_cpu_ptr(&ipi_data), 0);

	do {
		unsigned long msg = __ffs(pending);
		int rc;

		rc = __do_IPI(msg);
		if (rc)
			pr_info("IPI with bogus msg %ld in %ld\n", msg, copy);
		pending &= ~(1U << msg);
	} while (pending);

	return IRQ_HANDLED;
}

/*
 * API called by platform code to hookup arch-common ISR to their IPI IRQ
 *
 * Note: If IPI is provided by platform (vs. say ARC MCIP), their intc setup/map
 * function needs to call call irq_set_percpu_devid() for IPI IRQ, otherwise
 * request_percpu_irq() below will fail
 */
static DEFINE_PER_CPU(int, ipi_dev);

int smp_ipi_irq_setup(int cpu, irq_hw_number_t hwirq)
{
	int *dev = per_cpu_ptr(&ipi_dev, cpu);
	unsigned int virq = irq_find_mapping(NULL, hwirq);

	if (!virq)
		panic("Cannot find virq for root domain and hwirq=%lu", hwirq);

	/* Boot cpu calls request, all call enable */
	if (!cpu) {
		int rc;

		rc = request_percpu_irq(virq, do_IPI, "IPI Interrupt", dev);
		if (rc)
			panic("Percpu IRQ request failed for %u\n", virq);
	}

	enable_percpu_irq(virq, 0);

	return 0;
}
