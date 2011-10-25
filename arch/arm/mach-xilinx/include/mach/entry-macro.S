/*
 * arch/arm/mach-xilinx/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros
 *
 *  Copyright (C) 2009 Xilinx
 *  Copyright (C) 2007 Lennert Buytenhek <buytenh@wantstofly.org>
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <mach/hardware.h>
#include <asm/hardware/gic.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		ldr	\base, =gic_cpu_base_addr
		ldr	\base, [\base]
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		/*
		 * The interrupt numbering scheme is defined in the
		 * interrupt controller spec. 
		 *
		 * Interrupts 0-15 are SFI and are filtered
		 * Interrupts 31-1019 are normal and should be processed
		 * 1020-1021 are reserved and are filtered
		 * 1022 is not needed and filtered
		 * 1023 is "spurious" (no interrupt) and is filtered 
		 *
		 * A simple read from the controller will tell us the number of the highest
                 * priority enabled interrupt.  We then just need to check whether it is in the
		 * valid range for an IRQ (16-1019 inclusive). If a valid interrupt, Z = 0 on return.
		 */

		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp

		ldr     \irqstat, [\base, #GIC_CPU_INTACK] /* bits 12-10 = src CPU, 9-0 = int # */

		ldr	\tmp, =1020

		bic     \irqnr, \irqstat, #0x1c00

		cmp     \irqnr, #31
		cmpcc	\irqnr, \irqnr
		cmpne	\irqnr, \tmp
		cmpcs	\irqnr, \irqnr

		.endm

		.macro test_for_ipi, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		cmp	\irqnr, #16
		it	cc
		strcc	\irqstat, [\base, #GIC_CPU_EOI]
		it	cs
		cmpcs	\irqnr, \irqnr
		.endm

		/* As above, this assumes that irqstat and base are preserved */

		.macro test_for_ltirq, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		mov 	\tmp, #0
		cmp	\irqnr, #29
		itt	eq
		moveq	\tmp, #1
		streq	\irqstat, [\base, #GIC_CPU_EOI]
		cmp	\tmp, #0
		.endm

