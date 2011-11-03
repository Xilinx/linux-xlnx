/* arch/arm/mach-zynq/include/mach/uart.h
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_UART_H__
#define __MACH_UART_H__

#define UART_CR_OFFSET          0x00  /* Control Register [8:0] */
#define UART_MR_OFFSET          0x04  /* Mode Register [10:0] */
#define UART_BAUDGEN_OFFSET     0x18  /* Baud Rate Generator [15:0] */
#define UART_SR_OFFSET          0x2C  /* Channel Status [11:0] */
#define UART_FIFO_OFFSET        0x30  /* FIFO [15:0] or [7:0] */
#define UART_BAUDDIV_OFFSET     0x34  /* Baud Rate Divider [7:0] */

#define UART_CR_TX_EN     	0x00000010  /* TX enabled */
#define UART_CR_RX_EN       	0x00000004  /* RX enabled */
#define UART_MR_PARITY_NONE  	0x00000020  /* No parity mode */
#define UART_SR_TXFULL   	0x00000010  /* TX FIFO full */
#define UART_SR_TXEMPTY		0x00000008  /* TX FIFO empty */

/* The EP107 uses a different clock (50 MHz) right into the UART while the new boards
   will be using a 33.333 MHz clock into the chip which then is divided by 63.
*/
#ifdef CONFIG_XILINX_EARLY_UART_EP107
	#define UART_BAUD_115K		0x56	/* 115200 based on 50 MHz clock */
	#define UART_BAUDDIV_115K	0x4
#else
	#define UART_BAUD_115K		0x11	/* 115200 based on 33.33MHz / 63 clock */
	#define UART_BAUDDIV_115K	0x6
#endif

#ifndef __ASSEMBLY__

#include <linux/io.h>
#include <asm/processor.h>
#include <mach/zynq_soc.h>
#include <mach/uart.h>

static inline void uart_init(void)
{
	/* If a boot loader has already initialized the UART to a specific baud
	   rate then don't touch it, otherwise set it up for 115K baud for when
	   the kernel is loaded using a Xilinx probe.
	 */

	if (__raw_readl(IOMEM(LL_UART_PADDR + UART_MR_OFFSET)) != 0)
		return;
	
	/* Enable the transmitter and receiver, the mode for no parity, 1 stop,
	   8 data bits, and baud rate of 9600 
	 */

	__raw_writel((UART_CR_TX_EN | UART_CR_RX_EN), IOMEM(LL_UART_PADDR + UART_CR_OFFSET));
	__raw_writel(UART_MR_PARITY_NONE, IOMEM(LL_UART_PADDR + UART_MR_OFFSET));
	__raw_writel(UART_BAUD_115K, IOMEM(LL_UART_PADDR + UART_BAUDGEN_OFFSET)); 
	__raw_writel(UART_BAUDDIV_115K, IOMEM(LL_UART_PADDR + UART_BAUDDIV_OFFSET)); 

}

#endif

#endif
