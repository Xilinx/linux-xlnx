/* arch/arm/mach-xilinx/include/mach/xilinx-debug.S 
 *
 *  Copyright (C) 2009 Xilinx
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
		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_FIFO_OFFSET]	@ TXDATA
		.endm

		.macro	waituart,rd,rx		
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_SR_OFFSET]	@ get status register
		tst	\rd, #UART_SR_TXFULL		@ 
		bne	1002b			@ wait if FIFO is full
		.endm
