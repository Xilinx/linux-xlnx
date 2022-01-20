// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMP support for MicroBlaze, borrowing a great
 * deal of code from the PowerPC implementation
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 * Copyright (C) 2013-2020 Xilinx, Inc.
 */

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/profile.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/smp.h>

#include <asm/barrier.h>
#include <asm/cacheflush.h>
#include <asm/cpuinfo.h>
#include <asm/tlbflush.h>

struct thread_info *secondary_ti;

static struct thread_info *current_set[NR_CPUS];

unsigned long irq_err_count;

static unsigned int boot_cpuid;

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_SYMBOL(irq_stat);

static DEFINE_PER_CPU(cpumask_var_t, cpu_core_map);

static volatile unsigned int cpu_callin_map[NR_CPUS];

static void (*crash_ipi_function_ptr)(struct pt_regs *);

static const char * const smp_ipi_name[] = {
	[MICROBLAZE_MSG_RESCHEDULE] = "ipi reschedule",
	[MICROBLAZE_MSG_CALL_FUNCTION] = "ipi call function",
	[MICROBLAZE_MSG_CALL_FUNCTION_SINGLE] = "ipi call function single",
	[MICROBLAZE_MSG_DEBUGGER_BREAK] = "ipi debugger",
};

/* Functions for recording IPI handler */
static void (*__smp_cross_call)(unsigned int, unsigned int);

void __init set_smp_cross_call(void (*fn)(unsigned int, unsigned int))
{
	if (!__smp_cross_call)
		__smp_cross_call = fn;
}

static inline struct cpumask *cpu_core_mask(int cpu)
{
	return per_cpu(cpu_core_map, cpu);
}

u64 smp_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = 0;
	int i;

	for (i = 0; i < MICROBLAZE_NUM_IPIS; i++)
		sum += __get_irq_stat(cpu, ipi_irqs[i]);

	return sum;
}

static void show_ipi_list(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < MICROBLAZE_NUM_IPIS; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i,
			prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10u ",
				__get_irq_stat(cpu, ipi_irqs[i]));
		seq_printf(p, " %s\n", smp_ipi_name[i]);
	}
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_list(p, prec);
	seq_printf(p, "%*s: %10lu\n", prec, "Err", irq_err_count);
	return 0;
}

void handle_IPI(int ipinr, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int cpu = smp_processor_id();

	pr_debug("%s: cpu: %d got IPI: %d\n", __func__, cpu, ipinr);

	__inc_irq_stat(cpu, ipi_irqs[ipinr]);

	switch (ipinr) {
	case MICROBLAZE_MSG_RESCHEDULE:
		scheduler_ipi();
		break;
	case MICROBLAZE_MSG_CALL_FUNCTION:
		generic_smp_call_function_interrupt();
		break;
	case MICROBLAZE_MSG_CALL_FUNCTION_SINGLE:
		generic_smp_call_function_single_interrupt();
		break;
	case MICROBLAZE_MSG_DEBUGGER_BREAK:
		if (crash_ipi_function_ptr)
			crash_ipi_function_ptr(get_irq_regs());
		break;
	default:
		BUG();
	}

	set_irq_regs(old_regs);
}

void smp_send_reschedule(int cpu)
{
	if (cpu_online(cpu))
		__smp_cross_call(cpu, MICROBLAZE_MSG_RESCHEDULE);
}

void arch_send_call_function_single_ipi(int cpu)
{
	if (cpu_online(cpu))
		__smp_cross_call(cpu, MICROBLAZE_MSG_CALL_FUNCTION_SINGLE);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		__smp_cross_call(cpu, MICROBLAZE_MSG_CALL_FUNCTION);
}

#ifdef CONFIG_KGDB
void smp_send_debugger_break(void)
{
	int cpu;
	int me = raw_smp_processor_id();

	for_each_online_cpu(cpu)
		if (cpu != me)
			__smp_cross_call(cpu, MICROBLAZE_MSG_DEBUGGER_BREAK);
}

void crash___smp_cross_call(void (*crash_ipi_callback)(struct pt_regs *))
{
	crash_ipi_function_ptr = crash_ipi_callback;
	if (crash_ipi_callback) {
		mb();
		smp_send_debugger_break();
	}
}
#endif

static void stop_this_cpu(void *dummy)
{
	/* Remove this CPU */
	set_cpu_online(smp_processor_id(), false);

	local_irq_disable();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 0);
}

static void __init smp_create_idle(unsigned int cpu)
{
	struct task_struct *p;

	/* create a process for the processor */
	p = fork_idle(cpu);
	if (IS_ERR(p)) {
		panic("failed fork for CPU %u: %li", cpu, PTR_ERR(p));
		pr_alert("failed to create cpu %d idle\n", cpu);
	}

	task_thread_info(p)->cpu = cpu;
	current_set[cpu] = task_thread_info(p);

}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu;

	/*
	 * setup_cpu may need to be called on the boot cpu. We havent
	 * spun any cpus up but lets be paranoid.
	 */
	BUG_ON(boot_cpuid != smp_processor_id());

	/* Fixup boot cpu */
	cpu_callin_map[boot_cpuid] = 1;

	for_each_possible_cpu(cpu) {
		zalloc_cpumask_var_node(&per_cpu(cpu_core_map, cpu),
					GFP_KERNEL, cpu_to_node(cpu));
	}

	cpumask_set_cpu(boot_cpuid, cpu_core_mask(boot_cpuid));

	max_cpus = NR_CPUS;

	for_each_possible_cpu(cpu) {
		if (cpu != boot_cpuid)
			smp_create_idle(cpu);
	}
}

void __init smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);
	current_set[boot_cpuid] = task_thread_info(current);
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int c;

	secondary_ti = current_set[cpu];

	/*
	 * Make sure callin-map entry is 0 (can be leftover a CPU
	 * hotplug
	 */
	cpu_callin_map[cpu] = 0;

	/*
	 * The information for processor bringup must
	 * be written out to main store before we release
	 * the processor.
	 */
	smp_mb();

	/* wake up cpu */
	pr_alert("From cpu %d: Waking CPU %d\n", smp_processor_id(), cpu);

	__smp_cross_call(cpu, 0);

	if (system_state < SYSTEM_RUNNING)
		for (c = 10000; c && !cpu_callin_map[cpu]; c--)
			udelay(100);

	if (!cpu_callin_map[cpu]) {
		pr_err("Processor %u is stuck.\n", cpu);
		return -ENOENT;
	}

	while (!cpu_online(cpu))
		cpu_relax();

	pr_alert("Processor %u found.\n", cpu);

	return 0;
}

asmlinkage void __init secondary_machine_init(void)
{
	unsigned long *src, *dst;
	unsigned int offset = 0;

	/*
	 * Do not copy reset vectors. offset = 0x2 means skip the first
	 * two instructions. dst is pointer to MB vectors which are placed
	 * in block ram. If you want to copy reset vector setup offset to 0x0
	 */
#if !CONFIG_MANUAL_RESET_VECTOR
	offset = 0x2;
#endif
	dst = (unsigned long *) (offset * sizeof(u32));
	for (src = __ivt_start + offset; src < __ivt_end; src++, dst++)
		*dst = *src;
}

/* Activate a secondary processor. */
void __init start_secondary(void) // FIXME this is not __init
{
	unsigned int cpu = smp_processor_id();
	int i;

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	cpumask_set_cpu(cpu, mm_cpumask(&init_mm));
	local_flush_tlb_mm(&init_mm);

	pr_alert("cpu: %d alive\n", cpu);

	setup_cpuinfo();
	microblaze_cache_init();

	preempt_disable();

	/* calibrate_delay(); */

	cpu_callin_map[cpu] = 1;

	notify_cpu_starting(cpu);

	set_cpu_online(cpu, true);

	for_each_online_cpu(i) {
		cpumask_set_cpu(cpu, cpu_core_mask(i));
		cpumask_set_cpu(i, cpu_core_mask(cpu));
	}
	local_irq_enable();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);

	BUG();
}

#ifdef CONFIG_PROFILING
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}
#endif

void __init smp_cpus_done(unsigned int max_cpus)
{ }
