/*
 *  linux/include/asm-arm/arch-ks8695/uncompress.h
 *
 *  Copyright (C) 1999 ARM Limited
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

#include <asm/arch/ks8695-regs.h>

/*
 * These access routines operate on the physical address space.
 */
static inline unsigned int ks8695_getreg(unsigned int r)
{
	return *((unsigned int *) (KS8695_IO_BASE + r));
}

static inline void ks8695_setreg(unsigned int r, unsigned int v)
{
	*((unsigned int *) (KS8695_IO_BASE + r)) = v;
}

static void putc(char c)
{
	while ((ks8695_getreg(KS8695_UART_LINE_STATUS) & KS8695_UART_LINES_TXFE) == 0)
		;

	ks8695_setreg(KS8695_UART_TX_HOLDING, c);
}

static void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
