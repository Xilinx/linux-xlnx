/* arch/arm/mach-xilinx/include/mach/common.h 
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

#ifndef __ASM_ARCH_COMMON_H__
#define __ASM_ARCH_COMMON_H__

#include <mach/hardware.h>
#include <mach/uart.h>

/* The purpose of this header file is for code that is common to the kernel
   and the bootstrap loader such that it's not duplicated.  This code cannot
   reside in uart.h as it gets included from assembly and C.
*/

/* Initialize the UART0 */

static inline void xilinx_uart_init(void)
{
	/* If a boot loader has already initialized the UART to a specific baud
	   rate then don't touch it, otherwise set it up for 9600 baud for when
	   the kernel is loaded using a Xilinx probe.
	 */

	if (ReadReg(MXC_LL_UART_VADDR, UART_MR_OFFSET) != 0)
		return;
	
	/* Enable the transmitter and receiver, the mode for no parity, 1 stop,
	   8 data bits, and baud rate of 9600 
	 */

	WriteReg(MXC_LL_UART_VADDR, UART_CR_OFFSET, (UART_CR_TX_EN | UART_CR_RX_EN));
	WriteReg(MXC_LL_UART_VADDR, UART_MR_OFFSET, UART_MR_PARITY_NONE);
	WriteReg(MXC_LL_UART_VADDR, UART_BAUDGEN_OFFSET, UART_BAUD_115K); 
	WriteReg(MXC_LL_UART_VADDR, UART_BAUDDIV_OFFSET, UART_BAUDDIV_115K); 
}

#endif	/* __ASM_ARCH_COMMON_H__ */
