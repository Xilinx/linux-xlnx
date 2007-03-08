/*
 * include/asm-arm/arch-lpc2210/serial.h
 *
 * Copyright (C) 2004 Philips Semiconductors
 */

#ifndef __ASM_ARCH_SERIAL_H__
#define __ASM_ARCH_SERIAL_H__

#include <asm/hardware.h>

#define BASE_BAUD  (LPC22xx_Fpclk / 16)
#define UART0_BASE  0xE000C000
#define UART1_BASE  0xE0010000

/* Standard COM flags */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#define RS_TABLE_SIZE 2

/*
 * Rather empty table...
 * Hardwired serial ports should be defined here.
 * PCMCIA will fill it dynamically.
 */
#define STD_SERIAL_PORT_DEFNS	\
       /* UART	CLK     	PORT		IRQ	FLAGS		*/ \
	{ 0,	BASE_BAUD,	UART0_BASE, 	6,	STD_COM_FLAGS,	\
	.iomem_reg_shift = 2,		\
	.iomem_base = UART0_BASE,	\
	.io_type = UPIO_MEM},	\
	{ 1,	BASE_BAUD,	UART1_BASE, 	7,	STD_COM_FLAGS,	\
	.iomem_reg_shift = 2,		\
	.iomem_base = UART1_BASE,	\
	.io_type = UPIO_MEM}

#define EXTRA_SERIAL_PORT_DEFNS

#define SERIAL_PORT_DFNS	\
	STD_SERIAL_PORT_DEFNS	\
	EXTRA_SERIAL_PORT_DEFNS

#endif /* __ASM_ARCH_SERIAL_H__ */
