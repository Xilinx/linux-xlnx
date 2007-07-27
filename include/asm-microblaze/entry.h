/*
 * include/asm-microblaze/entry.h -- Definitions used by low-level 
 *                                   trap handlers
 *
 *  Copyright (C) 2007       PetaLogix
 *  Copyright (C) 2007       John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef __MICROBLAZE_ENTRY_H__
#define __MICROBLAZE_ENTRY_H__


#include <asm/percpu.h>
#include <asm/ptrace.h>

/* These are per-cpu variables required in entry.S, among other
   places */

DECLARE_PER_CPU(unsigned int, KSP);	/* Saved kernel stack pointer */
DECLARE_PER_CPU(unsigned int, KM);	/* Kernel/user mode */
DECLARE_PER_CPU(unsigned int, ENTRY_SP);	/* Saved SP on kernel entry */
DECLARE_PER_CPU(unsigned int, R11_SAVE);	/* Temp variable for entry */
DECLARE_PER_CPU(unsigned int, CURRENT_SAVE);	/* Saved current pointer */
DECLARE_PER_CPU(unsigned int, SYSCALL_SAVE);	/* Saved syscall number */

#endif /* __MICROBLAZE_ENTRY_H__ */

