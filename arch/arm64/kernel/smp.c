/*
 * SMP initialisation and IPI support
 * Based on arch/arm/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/clockchips.h>
#include <linux/completion.h>
#include <linux/of.h>

#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cpu_ops.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/smp_plat.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/ptrace.h>

/*
 * as from 2.5, kernels no longer have an init_tasks structure
 * so we need some other way of telling a new secondary core
 * where to place its SVC stack
 */
struct secondary_data secondary_data;

enum ipi_msg_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CALL_FUNC_SINGLE,
	IPI_CPU_STOP,
};

/*
 * Boot a secondary CPU, and assign it the specified idle task.
 * This also gives us the initial stack to use for this CPU.
 */
static int boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (cpu_ops[cpu]->cpu_boot)
		return cpu_ops[cpu]->cpu_boot(cpu);

	return -EOPNOTSUPP;
}

static DECLARE_COMPLETION(cpu_running);

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	int ret;

	/*
	 * We need to tell the secondary core where to find its stack and the
	 * page tables.
	 */
	secondary_data.stack = task_stack_page(idle) + THREAD_START_SP;
	__flush_dcache_area(&secondary_data, sizeof(secondary_data));

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		/*
		 * CPU was successfully started, wait for it to come online or
		 * time out.
		 */
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_err("CPU%u: failed to boot: %d\n", cpu, ret);
	}

	secondary_data.stack = NULL;

	return ret;
}

/*
 * This is the secondary CPU boot entry.  We're using this CPUs
 * idle thread stack, but a set of temporary page tables.
 */
asmlinkage void secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	printk("CPU%u: Booted secondary processor\n", cpu);

	/*
	 * All kernel threads share the same mm context; grab a
	 * reference and switch to it.
	 */
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_set_reserved_ttbr0();
	flush_tlb_all();

	preempt_disable();
	trace_hardirqs_off();

	if (cpu_ops[cpu]->cpu_postboot)
		cpu_ops[cpu]->cpu_postboot();

	/*
	 * Enable GIC and timers.
	 */
	notify_cpu_starting(cpu);

	/*
	 * OK, now it's safe to let the boot CPU continue.  Wait for
	 * the CPU migration code to notice that the CPU is online
	 * before we continue.
	 */
	set_cpu_online(cpu, true);
	complete(&cpu_running);

	local_irq_enable();
	local_fiq_enable();
	local_async_enable();

	/*
	 * OK, it's off to the idle thread for us
	 */
	cpu_startup_entry(CPUHP_ONLINE);
}

#ifdef CONFIG_HOTPLUG_CPU
static int op_cpu_disable(unsigned int cpu)
{
	/*
	 * If we don't have a cpu_die method, abort before we reach the point
	 * of no return. CPU0 may not have an cpu_ops, so test for it.
	 */
	if (!cpu_ops[cpu] || !cpu_ops[cpu]->cpu_die)
		return -EOPNOTSUPP;

	/*
	 * We may need to abort a hot unplug for some other mechanism-specific
	 * reason.
	 */
	if (cpu_ops[cpu]->cpu_disable)
		return cpu_ops[cpu]->cpu_disable(cpu);

	return 0;
}

/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();
	int ret;

	ret = op_cpu_disable(cpu);
	if (ret)
		return ret;

	/*
	 * Take this CPU offline.  Once we clear this, we can't return,
	 * and we must not schedule until we're ready to give up the cpu.
	 */
	set_cpu_online(cpu, false);

	/*
	 * OK - migrate IRQs away from this CPU
	 */
	migrate_irqs();

	/*
	 * Remove this CPU from the vm mask set of all processes.
	 */
	clear_tasks_mm_cpumask(cpu);

	return 0;
}

static DECLARE_COMPLETION(cpu_died);

/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpu_die(unsigned int cpu)
{
	if (!wait_for_completion_timeout(&cpu_died, msecs_to_jiffies(5000))) {
		pr_crit("CPU%u: cpu didn't die\n", cpu);
		return;
	}
	pr_notice("CPU%u: shutdown\n", cpu);
}

/*
 * Called from the idle thread for the CPU which has been shutdown.
 *
 * Note that we disable IRQs here, but do not re-enable them
 * before returning to the caller. This is also the behaviour
 * of the other hotplug-cpu capable cores, so presumably coming
 * out of idle fixes this.
 */
void cpu_die(void)
{
	unsigned int cpu = smp_processor_id();

	idle_task_exit();

	local_irq_disable();

	/* Tell __cpu_die() that this CPU is now safe to dispose of */
	complete(&cpu_died);

	/*
	 * Actually shutdown the CPU. This must never fail. The specific hotplug
	 * mechanism must perform all required cache maintenance to ensure that
	 * no dirty lines are lost in the process of shutting down the CPU.
	 */
	cpu_ops[cpu]->cpu_die(cpu);

	BUG();
}
#endif

void __init smp_cpus_done(unsigned int max_cpus)
{
	pr_info("SMP: Total of %d processors activated.\n", num_online_cpus());
}

void __init smp_prepare_boot_cpu(void)
{
}

static void (*smp_cross_call)(const struct cpumask *, unsigned int);

/*
 * Enumerate the possible CPU set from the device tree and build the
 * cpu logical map array containing MPIDR values related to logical
 * cpus. Assumes that cpu_logical_map(0) has already been initialized.
 */
void __init smp_init_cpus(void)
{
	struct device_node *dn = NULL;
	unsigned int i, cpu = 1;
	bool bootcpu_valid = false;

	while ((dn = of_find_node_by_type(dn, "cpu"))) {
		const u32 *cell;
		u64 hwid;

		/*
		 * A cpu node with missing "reg" property is
		 * considered invalid to build a cpu_logical_map
		 * entry.
		 */
		cell = of_get_property(dn, "reg", NULL);
		if (!cell) {
			pr_err("%s: missing reg property\n", dn->full_name);
			goto next;
		}
		hwid = of_read_number(cell, of_n_addr_cells(dn));

		/*
		 * Non affinity bits must be set to 0 in the DT
		 */
		if (hwid & ~MPIDR_HWID_BITMASK) {
			pr_err("%s: invalid reg property\n", dn->full_name);
			goto next;
		}

		/*
		 * Duplicate MPIDRs are a recipe for disaster. Scan
		 * all initialized entries and check for
		 * duplicates. If any is found just ignore the cpu.
		 * cpu_logical_map was initialized to INVALID_HWID to
		 * avoid matching valid MPIDR values.
		 */
		for (i = 1; (i < cpu) && (i < NR_CPUS); i++) {
			if (cpu_logical_map(i) == hwid) {
				pr_err("%s: duplicate cpu reg properties in the DT\n",
					dn->full_name);
				goto next;
			}
		}

		/*
		 * The numbering scheme requires that the boot CPU
		 * must be assigned logical id 0. Record it so that
		 * the logical map built from DT is validated and can
		 * be used.
		 */
		if (hwid == cpu_logical_map(0)) {
			if (bootcpu_valid) {
				pr_err("%s: duplicate boot cpu reg property in DT\n",
					dn->full_name);
				goto next;
			}

			bootcpu_valid = true;

			/*
			 * cpu_logical_map has already been
			 * initialized and the boot cpu doesn't need
			 * the enable-method so continue without
			 * incrementing cpu.
			 */
			continue;
		}

		if (cpu >= NR_CPUS)
			goto next;

		if (cpu_read_ops(dn, cpu) != 0)
			goto next;

		if (cpu_ops[cpu]->cpu_init(dn, cpu))
			goto next;

		pr_debug("cpu logical map 0x%llx\n", hwid);
		cpu_logical_map(cpu) = hwid;
next:
		cpu++;
	}

	/* sanity check */
	if (cpu > NR_CPUS)
		pr_warning("no. of cores (%d) greater than configured maximum of %d - clipping\n",
			   cpu, NR_CPUS);

	if (!bootcpu_valid) {
		pr_err("DT missing boot CPU MPIDR, not enabling secondaries\n");
		return;
	}

	/*
	 * All the cpus that made it to the cpu_logical_map have been
	 * validated so set them as possible cpus.
	 */
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_logical_map(i) != INVALID_HWID)
			set_cpu_possible(i, true);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int err;
	unsigned int cpu, ncores = num_possible_cpus();

	/*
	 * are we trying to boot more cores than exist?
	 */
	if (max_cpus > ncores)
		max_cpus = ncores;

	/* Don't bother if we're effectively UP */
	if (max_cpus <= 1)
		return;

	/*
	 * Initialise the present map (which describes the set of CPUs
	 * actually populated at the present time) and release the
	 * secondaries from the bootloader.
	 *
	 * Make sure we online at most (max_cpus - 1) additional CPUs.
	 */
	max_cpus--;
	for_each_possible_cpu(cpu) {
		if (max_cpus == 0)
			break;

		if (cpu == smp_processor_id())
			continue;

		if (!cpu_ops[cpu])
			continue;

		err = cpu_ops[cpu]->cpu_prepare(cpu);
		if (err)
			continue;

		set_cpu_present(cpu, true);
		max_cpus--;
	}
}


void __init set_smp_cross_call(void (*fn)(const struct cpumask *, unsigned int))
{
	smp_cross_call = fn;
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	smp_cross_call(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	smp_cross_call(cpumask_of(cpu), IPI_CALL_FUNC_SINGLE);
}

static const char *ipi_types[NR_IPI] = {
#define S(x,s)	[x - IPI_RESCHEDULE] = s
	S(IPI_RESCHEDULE, "Rescheduling interrupts"),
	S(IPI_CALL_FUNC, "Function call interrupts"),
	S(IPI_CALL_FUNC_SINGLE, "Single function call interrupts"),
	S(IPI_CPU_STOP, "CPU stop interrupts"),
};

void show_ipi_list(struct seq_file *p, int prec)
{
	unsigned int cpu, i;

	for (i = 0; i < NR_IPI; i++) {
		seq_printf(p, "%*s%u:%s", prec - 1, "IPI", i + IPI_RESCHEDULE,
			   prec >= 4 ? " " : "");
		for_each_online_cpu(cpu)
			seq_printf(p, "%10u ",
				   __get_irq_stat(cpu, ipi_irqs[i]));
		seq_printf(p, "      %s\n", ipi_types[i]);
	}
}

u64 smp_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = 0;
	int i;

	for (i = 0; i < NR_IPI; i++)
		sum += __get_irq_stat(cpu, ipi_irqs[i]);

	return sum;
}

static DEFINE_RAW_SPINLOCK(stop_lock);

/*
 * ipi_cpu_stop - handle IPI from smp_send_stop()
 */
static void ipi_cpu_stop(unsigned int cpu)
{
	if (system_state == SYSTEM_BOOTING ||
	    system_state == SYSTEM_RUNNING) {
		raw_spin_lock(&stop_lock);
		pr_crit("CPU%u: stopping\n", cpu);
		dump_stack();
		raw_spin_unlock(&stop_lock);
	}

	set_cpu_online(cpu, false);

	local_fiq_disable();
	local_irq_disable();

	while (1)
		cpu_relax();
}

/*
 * Main handler for inter-processor interrupts
 */
void handle_IPI(int ipinr, struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();
	struct pt_regs *old_regs = set_irq_regs(regs);

	if (ipinr >= IPI_RESCHEDULE && ipinr < IPI_RESCHEDULE + NR_IPI)
		__inc_irq_stat(cpu, ipi_irqs[ipinr - IPI_RESCHEDULE]);

	switch (ipinr) {
	case IPI_RESCHEDULE:
		scheduler_ipi();
		break;

	case IPI_CALL_FUNC:
		irq_enter();
		generic_smp_call_function_interrupt();
		irq_exit();
		break;

	case IPI_CALL_FUNC_SINGLE:
		irq_enter();
		generic_smp_call_function_single_interrupt();
		irq_exit();
		break;

	case IPI_CPU_STOP:
		irq_enter();
		ipi_cpu_stop(cpu);
		irq_exit();
		break;

	default:
		pr_crit("CPU%u: Unknown IPI message 0x%x\n", cpu, ipinr);
		break;
	}
	set_irq_regs(old_regs);
}

void smp_send_reschedule(int cpu)
{
	smp_cross_call(cpumask_of(cpu), IPI_RESCHEDULE);
}

void smp_send_stop(void)
{
	unsigned long timeout;

	if (num_online_cpus() > 1) {
		cpumask_t mask;

		cpumask_copy(&mask, cpu_online_mask);
		cpu_clear(smp_processor_id(), mask);

		smp_cross_call(&mask, IPI_CPU_STOP);
	}

	/* Wait up to one second for other CPUs to stop */
	timeout = USEC_PER_SEC;
	while (num_online_cpus() > 1 && timeout--)
		udelay(1);

	if (num_online_cpus() > 1)
		pr_warning("SMP: failed to stop secondary CPUs\n");
}

/*
 * not supported here
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}
