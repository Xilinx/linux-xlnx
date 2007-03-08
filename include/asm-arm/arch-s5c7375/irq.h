/*
 *  linux/include/asm-arm/arch-s5c7375/irq.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *                         Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __S5C7375_irq_h
#define __S5C7375_irq_h

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>

/* To understand S5C7375 irq 
 * Look hard at "fixup_irq definition in irq.c file "
 */
extern unsigned  int fixup_irq(int i);

extern void do_IRQ(int irq, struct pt_regs *regs);

extern void s5c7375_init_irq(void);

#define 	irq_init_irq 	s5c7375_init_irq

#endif 
