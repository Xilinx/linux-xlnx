/*
 * include/asm-microblaze/hw_irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_HW_IRQ_H
#define _ASM_HW_IRQ_H

static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i)
{
	/* Nothing to do */
}

#endif /* _ASM_HW_IRQ_H */
