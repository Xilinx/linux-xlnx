/* arch/arm/mach-xilinx/include/mach/uart.h
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
#ifndef __ASM_ARCH_UART_H__
#define __ASM_ARCH_UART_H__

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

#define UART_BAUD_9600		0x145	/* 9600 based on 50 MHz clock */
#define UART_BAUDDIV_9600	0xF	
#define UART_BAUD_115K		0x56	/* 115200 based on 50 MHz clock */
#define UART_BAUDDIV_115K	0x4

#define ReadReg(BaseAddress, RegOffset) \
    *(volatile int *)((BaseAddress) + (RegOffset))

#define WriteReg(BaseAddress, RegOffset, RegisterValue) \
    *(volatile int *)((BaseAddress) + (RegOffset)) = (RegisterValue)

#define IsTransmitFull(BaseAddress)                   \
    ((*(volatile int *)((BaseAddress) + UART_SR_OFFSET) & UART_SR_TXFULL) == UART_SR_TXFULL)

#endif	/* __ASM_ARCH_UART_H__ */
