/*
 *  linux/include/asm-arm/arch-ks8695/system.h
 *
 *  Copyright (C) 2002 Micrel Inc.
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
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/io.h>
#include <asm/arch/ks8695-regs.h>

static void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks
	 */
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	unsigned int val;

	/* To reset, use the watchdog timer */
	val = __raw_readl(KS8695_REG(KS8695_TIMER_CTRL)) & 0x02;
	__raw_writel(val, KS8695_REG(KS8695_TIMER_CTRL));
	val = (10 << 8) | 0xFF;
	__raw_writel(val, KS8695_REG(KS8695_TIMER0));
	val = __raw_readl(KS8695_REG(KS8695_TIMER_CTRL)) | 0x01;
	__raw_writel(val, KS8695_REG(KS8695_TIMER_CTRL));
}

#endif /* __ASM_ARCH_SYSTEM_H */
