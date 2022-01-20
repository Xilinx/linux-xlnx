// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for handling the MMU.
 *
 *    Copyright (C) 2007,2013-2020 Xilinx, Inc.  All rights reserved.
 *
 *  Derived from arch/powerpc/mm/mmu_context_nohash.c
 *    Copyright 2008 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                IBM Corp.
 *
 *  Derived from arch/ppc/mm/4xx_mmu.c:
 *  -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/slab.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

static unsigned int next_context, nr_free_contexts;
static unsigned long context_map[LAST_CONTEXT / BITS_PER_LONG + 1];
#ifdef CONFIG_SMP
static unsigned long stale_map[LAST_CONTEXT / BITS_PER_LONG + 1][NR_CPUS];
#endif
static struct mm_struct *context_mm[LAST_CONTEXT + 1];
static DEFINE_RAW_SPINLOCK(context_lock);

/*
 * Steal a context from a task that has one at the moment.
 *
 * This is used when we are running out of available PID numbers
 * on the processors.
 *
 * This isn't an LRU system, it just frees up each context in
 * turn (sort-of pseudo-random replacement :).  This would be the
 * place to implement an LRU scheme if anyone were motivated to do it.
 *
 * For context stealing, we use a slightly different approach for
 * SMP and UP. Basically, the UP one is simpler and doesn't use
 * the stale map as we can just flush the local CPU.
 */
#ifdef CONFIG_SMP
static unsigned int steal_context_smp(unsigned int id)
{
	struct mm_struct *mm;
	unsigned int cpu, max;

	max = LAST_CONTEXT - FIRST_CONTEXT;

	/* Attempt to free next_context first and then loop until we manage */
	while (max--) {
		/* Pick up the victim mm */
		mm = context_mm[id];

		/* We have a candidate victim, check if it's active, on SMP
		 * we cannot steal active contexts
		 */
		if (mm->context.active) {
			id++;
			if (id > LAST_CONTEXT)
				id = FIRST_CONTEXT;
			continue;
		}

		/* Mark this mm has having no context anymore */
		mm->context.id = MMU_NO_CONTEXT;

		/* Mark it stale on all CPUs that used this mm. */
		for_each_cpu(cpu, mm_cpumask(mm)) {
			__set_bit(id, stale_map[cpu]);
		}
		return id;
	}

	/* This will happen if you have more CPUs than available contexts,
	 * all we can do here is wait a bit and try again
	 */
	raw_spin_unlock(&context_lock);
	cpu_relax();
	raw_spin_lock(&context_lock);

	/* This will cause the caller to try again */
	return MMU_NO_CONTEXT;
}
#endif  /* CONFIG_SMP */

/*
 * Note that this will also be called on SMP if all other CPUs are
 * offlined, which means that it may be called for cpu != 0. For
 * this to work, we somewhat assume that CPUs that are onlined
 * come up with a fully clean TLB (or are cleaned when offlined)
 */
static unsigned int steal_context_up(unsigned int id)
{
	struct mm_struct *mm;
	unsigned int cpu = smp_processor_id();

	/* Pick up the victim mm */
	mm = context_mm[id];

	pr_debug("[%d] steal context %d from mm @%p\n", cpu, id, mm);

	/* Flush the TLB for that context */
	local_flush_tlb_mm(mm);

	/* Mark this mm has having no context anymore */
	mm->context.id = MMU_NO_CONTEXT;

	/* TODO: This clear should ultimately be part of local_flush_tlb_mm */
#ifdef CONFIG_SMP
	__clear_bit(id, stale_map[cpu]);
#endif

	return id;
}

void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned int id;
#ifdef CONFIG_SMP
	unsigned int cpu = smp_processor_id();
#endif
	unsigned long *map;

	/* No lockless fast path .. yet */
	raw_spin_lock(&context_lock);

#ifdef CONFIG_SMP
	/* Mark us active and the previous one not anymore */
	next->context.active++;
	if (prev) {
		WARN_ON(prev->context.active < 1);
		prev->context.active--;
	}

 again:
#endif /* CONFIG_SMP */

	/* If we already have a valid assigned context, skip all that */
	id = next->context.id;
	if (likely(id != MMU_NO_CONTEXT))
		goto ctxt_ok;

	/* We really don't have a context, let's try to acquire one */
	id = next_context;
	if (id > LAST_CONTEXT)
		id = FIRST_CONTEXT;
	map = context_map;

	/* No more free contexts, let's try to steal one */
	if (nr_free_contexts == 0) {
#ifdef CONFIG_SMP
		if (num_online_cpus() > 1) {
			id = steal_context_smp(id);
			if (id == MMU_NO_CONTEXT)
				goto again;
			goto stolen;
		}
#endif /* CONFIG_SMP */
		id = steal_context_up(id);
		goto stolen;
	}
	nr_free_contexts--;

	/* We know there's at least one free context, try to find it */
	while (__test_and_set_bit(id, map)) {
		id = find_next_zero_bit(map, LAST_CONTEXT + 1, id);
		if (id > LAST_CONTEXT)
			id = FIRST_CONTEXT;
	}
 stolen:
	next_context = id + 1;
	context_mm[id] = next;
	next->context.id = id;

 ctxt_ok:

	/* If that context got marked stale on this CPU, then flush the
	 * local TLB for it and unmark it before we use it
	 */
#ifdef CONFIG_SMP
	if (test_bit(id, stale_map[cpu])) {
		local_flush_tlb_mm(next);

		/*
		 * TODO: This clear should ultimately be part of
		 * local_flush_tlb_mm
		 */
		__clear_bit(id, stale_map[cpu]);
	}
#endif

	/* Flick the MMU and release lock */
	set_context(id, next->pgd);
	raw_spin_unlock(&context_lock);
}

/*
 * Set up the context for a new address space.
 */
int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = MMU_NO_CONTEXT;
	mm->context.active = 0;

	return 0;
}

/*
 * We're finished using the context for an address space.
 */
void destroy_context(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int id;

	if (mm->context.id == MMU_NO_CONTEXT)
		return;

	WARN_ON(mm->context.active != 0);

	raw_spin_lock_irqsave(&context_lock, flags);
	id = mm->context.id;
	if (id != MMU_NO_CONTEXT) {
		__clear_bit(id, context_map);
		mm->context.id = MMU_NO_CONTEXT;
		mm->context.active = 0;
		context_mm[id] = NULL;
		nr_free_contexts++;
	}
	raw_spin_unlock_irqrestore(&context_lock, flags);
}

/*
 * Initialize the context management stuff.
 */
void __init mmu_context_init(void)
{
	/*
	 * Mark init_mm as being active on all possible CPUs since
	 * we'll get called with prev == init_mm the first time
	 * we schedule on a given CPU
	 */
	init_mm.context.active = NR_CPUS;

	/*
	 * The use of context zero is reserved for the kernel.
	 * This code assumes FIRST_CONTEXT < 32.
	 */
	context_map[0] = (1 << FIRST_CONTEXT) - 1;
	next_context = FIRST_CONTEXT;
	nr_free_contexts = LAST_CONTEXT - FIRST_CONTEXT + 1;
}
