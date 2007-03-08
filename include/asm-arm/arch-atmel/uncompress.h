/*
 * linux/include/asm-arm/arch-atmel/uncompress.h
 *
 * Copyright (C) 2006, Greg Ungerer <gerg@snapgear.com>
 * Copyright (C) 2001 RidgeRun, Inc. (http://www.ridgerun.com)
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

static void putc(char c)
{
	static int inited = 0;
	if (!inited) {
		HW_AT91_USART_INIT
		at91_usart_init((volatile struct atmel_usart_regs *) AT91_USART0_BASE, 9600);
		inited = 1;
	}
	at91_usart_putc((volatile struct atmel_usart_regs *) AT91_USART0_BASE, c);
}

static void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
