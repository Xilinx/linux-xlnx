/* arch/arm/mach-xilinx/include/mach/uncompress.h
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
#ifndef __ASM_ARCH_UNCOMPRESS_H__
#define __ASM_ARCH_UNCOMPRESS_H__

#include <mach/hardware.h>
#include <mach/uart.h>
#include <mach/common.h>

/* Initialize the UART for the bootstrap loader */

void arch_decomp_setup(void)
{
	xilinx_uart_init();
}

static inline void flush(void) 
{
}

#define arch_decomp_wdog()

static void putc(char ch)
{
        /*
         * Wait for room in the FIFO, then write the char into the FIFO
         */
        while (IsTransmitFull(MXC_LL_UART_VADDR));

        WriteReg(MXC_LL_UART_VADDR, UART_FIFO_OFFSET, ch);
}

#endif	/* __ASM_ARCH_UNCOMPRESS_H__ */
