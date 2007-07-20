/*
 * include/asm-microblaze/irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/* FIXME */
#define NR_IRQS 32

static inline int irq_canonicalize(int irq)
{
	return (irq);
}

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

#endif /* _ASM_IRQ_H */
