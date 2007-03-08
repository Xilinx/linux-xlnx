/*
 *  linux/include/asm-arm/nommu_context.h
 *
 *  Copyright (C) 2001 RidgRun Inc (www.ridgerun.com)
 *  Copyright (C) 2004 Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   20-02-2001 GJM     Gutted for uClinux
 *   05-03-2004 HSC     modified for 2.6
 */
#ifndef __ASM_ARM_NOMMU_CONTEXT_H
#define __ASM_ARM_NOMMU_CONTEXT_H

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgalloc.h>

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

extern inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	return(0);
}

#define destroy_context(mm)		do { } while(0)

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
}

#define deactivate_mm(tsk,mm)	do { } while (0)

extern inline void activate_mm(struct mm_struct *prev_mm,
			       struct mm_struct *next_mm)
{
}

#endif
