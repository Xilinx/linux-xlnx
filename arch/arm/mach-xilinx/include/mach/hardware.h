/* arch/arm/mach-xilinx/include/mach/hardware.h
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

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#ifdef DEBUG
#define xilinx_debug(x, ...)	pr_info(x, ...)
#else
#define xilinx_debug(x, ...)
#endif

/*
 * defines the clock rates
 */
#define PERIPHERAL_CLOCK_RATE	781250
#define CLOCK_TICK_RATE		PERIPHERAL_CLOCK_RATE / 32 /* prescaled in timer */

/*
 * Device base addresses, all are mapped flat such that virtual = physical, is
 * still true now with EPA9 addresses?
 */
#define	EPA9_BASE		0x90000000
#define IO_BASE			0xE0000000

#define TTC0_BASE		(EPA9_BASE + 0x00001000)/* 0x90001000 */
#define GIC_DIST_BASE		(EPA9_BASE + 0x00100000)/* 0x90100000 */

#define UART0_BASE		(IO_BASE)		/* 0xE0000000 */
#define UART1_BASE		(IO_BASE + 0x00001000)	/* 0xE0001000 */
#define GIC_CPU_BASE		(IO_BASE + 0x08000000)	/* 0xE8000000 */
#define DMAC0_BASE		(IO_BASE + 0x0C000000)	/* 0xEC000000 */
#define DMAC1_BASE		(IO_BASE + 0x0C001000)	/* 0xEC001000 */
#define I2C0_BASE		(IO_BASE + 0x00004000)	/* 0xE0004000 */
#define GPIO0_BASE		(IO_BASE + 0x0000A000)	/* 0xE000A000 */
#define SMC_BASE		(IO_BASE + 0x0000E000)	/* 0xE000E000 */
#define NOR_BASE		(IO_BASE + 0x02000000)	/* 0xE2000000 */
#define ETH0_BASE               (IO_BASE + 0x0000B000)  /* 0xE000B000 */
#define ETH1_BASE               (IO_BASE + 0x0000C000)  /* 0xE000C000 */
#define SPI0_BASE		(IO_BASE + 0x00006000)	/* 0xE0006000 */
#define SPI1_BASE		(IO_BASE + 0x00007000)	/* 0xE0007000 */

/*
 * GIC Interrupts
 */
#define IRQ_GIC_START		32

#define IRQ_TIMERCOUNTER0	(IRQ_GIC_START + 1)
#define IRQ_TIMERCOUNTER1	(IRQ_GIC_START + 2)
#define IRQ_UART0		(IRQ_GIC_START + 20)
#define IRQ_UART1		(IRQ_GIC_START + 45)
#define IRQ_DMAC0		(IRQ_GIC_START + 4)
#define IRQ_DMAC1		(IRQ_GIC_START + 34)
#define IRQ_I2C0		(IRQ_GIC_START + 18)
#define IRQ_GPIO0		(IRQ_GIC_START + 13)
#define IRQ_ETH0                (IRQ_GIC_START + 15)
#define IRQ_ETH1                (IRQ_GIC_START + 41)
#define IRQ_SPI0		(IRQ_GIC_START + 19)
#define IRQ_SPI1		(IRQ_GIC_START + 44)

/*
 * Start and size of physical RAM
 */
#define PHYS_OFFSET             0
#define MEM_SIZE		(32 * 1024 * 1024)

/*
 * Mandatory for CONFIG_LL_DEBUG
 */
#define MXC_LL_UART_PADDR	UART0_BASE
#define MXC_LL_UART_VADDR	UART0_BASE

#endif /* __ASM_ARCH_HARDWARE_H__ */
