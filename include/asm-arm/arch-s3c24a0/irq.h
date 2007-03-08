/*
 * include/asm-arm/arch-s3c24a0/irq.h
 * 
 * $Id: irq.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This prototype is required for cascading of multiplexed interrupts.
 * Since it doesn't exist elsewhere, we'll put it here for now.
 */
extern unsigned int fixup_irq(int i);
extern void do_IRQ(int irq, struct pt_regs *regs);
