/*
 *  linux/arch/arm/mach-ks8695/irq.c
 *
 *  Copyright (C) 2002 Micrel Inc.
 *  Copyright (C) 2006 Greg Ungerer <gerg@snapgear.com>
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/irq.h>

static void ks8695_irq_mask(unsigned int irq)
{
	unsigned long msk;
	msk = __raw_readl(KS8695_REG(KS8695_INT_ENABLE));
	msk &= ~(1 << irq);
	__raw_writel(msk, KS8695_REG(KS8695_INT_ENABLE));
}

static void ks8695_irq_unmask(unsigned int irq)
{
	unsigned long msk;
	msk = __raw_readl(KS8695_REG(KS8695_INT_ENABLE));
	msk |= (1 << irq);
	__raw_writel(msk, KS8695_REG(KS8695_INT_ENABLE));
}

static int ks8695_irq_set_type(unsigned int irq, unsigned int type)
{
	return 0;
}

struct irqchip ks8695_irq_chip = {
	.ack		= ks8695_irq_mask,
	.mask		= ks8695_irq_mask,
	.unmask		= ks8695_irq_unmask,
	.set_type	= ks8695_irq_set_type,
};

void __init ks8695_init_irq(void)
{
	unsigned int i;

	/* Disable all interrupts initially. */
	__raw_writel(0, KS8695_REG(KS8695_INT_CONTL));
	__raw_writel(0, KS8695_REG(KS8695_INT_ENABLE));

	for (i = 0; (i < NR_IRQS); i++) {
		set_irq_chip(i, &ks8695_irq_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID);
	}
}

