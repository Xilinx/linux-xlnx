/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013-2020 Xilinx, Inc
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_MMU_CONTEXT_H
#define _ASM_MICROBLAZE_MMU_CONTEXT_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/sched.h>

#include <asm/bitops.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/mmu.h>
#include <asm-generic/mm_hooks.h>

/*
 * This function defines the mapping from contexts to VSIDs (virtual
 * segment IDs).  We use a skew on both the context and the high 4 bits
 * of the 32-bit virtual address (the "effective segment ID") in order
 * to spread out the entries in the MMU hash table.
 */
# define CTX_TO_VSID(ctx, va)	(((ctx) * (897 * 16) + ((va) >> 28) * 0x111) \
				 & 0xffffff)

/*
   MicroBlaze has 256 contexts, so we can just rotate through these
   as a way of "switching" contexts.  If the TID of the TLB is zero,
   the PID/TID comparison is disabled, so we can use a TID of zero
   to represent all kernel pages as shared among all contexts.
 */

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

# define NO_CONTEXT	256
# define LAST_CONTEXT	255
# define FIRST_CONTEXT	1

/*
 * Set the current MMU context.
 * This is done by loading up the segment registers for the user part of the
 * address space.
 *
 * Since the PGD is immediately available, it is much faster to simply
 * pass this along as a second parameter, which is required for 8xx and
 * can be used for debugging on all processors (if you happen to have
 * an Abatron).
 */
extern void set_context(unsigned long id, pgd_t *pgd);

/*
 * Since we don't have sufficient contexts to give one to every task
 * that could be in the system, we need to be able to steal contexts.
 */
extern void steal_context(void);

/*
 * Set up the context for a new address space.
 */
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);

/*
 * We're finished using the context for an address space.
 */
extern void destroy_context(struct mm_struct *mm);

/*
 * Switch context
 */
extern void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next);

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	/* Mark this context has been used on the new CPU */
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(next));
	tsk->thread.pgdir = next->pgd;

	/* Nothing else to do if we aren't actually switching */
	if (prev == next)
		return;

	/* Out of line for now */
	switch_mmu_context(prev, next);
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *active_mm,
			struct mm_struct *mm)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm(active_mm, mm, current);
	local_irq_restore(flags);
}

extern void mmu_context_init(void);

#endif /* _ASM_MICROBLAZE_MMU_CONTEXT_H */
