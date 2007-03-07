/*
 * arch/microblaze/kernel/process.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/sched.h>
#include <asm/bug.h>

/* FIXME */
void ptrace_disable(struct task_struct *child)
{
	BUG();
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	BUG();
	return 0;
}
