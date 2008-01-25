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

struct device_node;

/* FIXME */
#define NR_IRQS 32

#define NO_IRQ NR_IRQS

/* irq_of_parse_and_map - Parse and Map an interrupt into linux virq space
 * @device: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_map_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
extern unsigned int irq_of_parse_and_map(struct device_node *dev, int index);

static inline int irq_canonicalize(int irq)
{
	return (irq);
}

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

#endif /* _ASM_IRQ_H */
