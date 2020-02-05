/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions used by low-level trap handlers
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 */

#ifndef _ASM_MICROBLAZE_ENTRY_H
#define _ASM_MICROBLAZE_ENTRY_H

#include <asm/ptrace.h>
#include <linux/linkage.h>

/*
 * These are per-cpu variables required in entry.S, among other
 * places
 */

#define PER_CPU(var) var

#ifdef CONFIG_SMP
/* Addresses in BRAM */
#define CURRENT_SAVE_ADDR	0x50
#define ENTRY_SP_ADDR		0x54
#define PT_POOL_SPACE_ADDR	0x100
#endif /* CONFIG_SMP */

# ifndef __ASSEMBLY__
#include <asm/percpu.h>

#ifndef CONFIG_SMP
DECLARE_PER_CPU(unsigned int, KSP); /* Saved kernel stack pointer */
DECLARE_PER_CPU(unsigned int, KM); /* Kernel/user mode */
DECLARE_PER_CPU(unsigned int, ENTRY_SP); /* Saved SP on kernel entry */
DECLARE_PER_CPU(unsigned int, R11_SAVE); /* Temp variable for entry */
DECLARE_PER_CPU(unsigned int, CURRENT_SAVE); /* Saved current pointer */
#endif /* CONFIG_SMP */

extern asmlinkage void do_notify_resume(struct pt_regs *regs, int in_syscall);
# endif /* __ASSEMBLY__ */

#endif /* _ASM_MICROBLAZE_ENTRY_H */
