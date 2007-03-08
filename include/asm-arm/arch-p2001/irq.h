/*
 *  linux/include/asm-armnommu/arch-p2001/irq.h
 *
 *  Copyright (C) 2004 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IRQ_H__
#define __ASM_ARCH_IRQ_H__


#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>

#define fixup_irq(x) (x)

extern void do_IRQ(int irq, struct pt_regs *regs);

extern void p2001_mask_irq(unsigned int irq);
extern void p2001_unmask_irq(unsigned int irq);
extern void p2001_mask_ack_irq(unsigned int irq);

extern void p2001_init_irq(void);

#define 	irq_init_irq 	p2001_init_irq

#endif /* __ASM_ARCH_IRQ_H__ */
