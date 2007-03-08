/*
 *  arch/armnommu/mach-atmel/entry-macro.S
 *
 *  Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 *  Samsung Electronics Co.,Ltd.
 *
 *  defines machine dependent entry macros.
 *  included in the arch/armnommu/kernel/entry.S
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

#include <asm/hardware.h>

#if defined(CONFIG_ARCH_ATMEL)
		.macro  disable_fiq
		.endm
                        /* r0     r6      r5   lr */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	r4, =AIC_IVR
		ldr	\irqnr, [r4]		@ignore value
		ldr	r4, =AIC_ISR		@read interrupt nr.
		ldr	\irqnr, [r4]
		teq	\irqnr, #0
		ldreq	r4, =AIC_EOICR
		streq	r4, [r4]		@acknowledge the spurious read of the IVR
		.endm   
                
		.macro  irq_prio_table
		.endm

#endif
