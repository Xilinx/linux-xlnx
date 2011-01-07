/* arch/arm/mach-xilinx/include/mach/debug-macro.S 
 *
 * Debugging macro include header
 *
 *  Copyright (C) 2009 Xilinx
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
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
#include <mach/uart.h>
#include <mach/debug-xilinx.S>

		.macro	addruart, rp, rv
		ldr	\rp, =MXC_LL_UART_PADDR	@ physical
		ldr	\rv, =MXC_LL_UART_VADDR	@ virtual
		.endm
