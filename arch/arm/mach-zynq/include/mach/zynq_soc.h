/* arch/arm/mach-zynq/include/mach/zynq_soc.h
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

#ifndef __MACH_XILINX_SOC_H__
#define __MACH_XILINX_SOC_H__

#define PERIPHERAL_CLOCK_RATE		2500000

/* For now, all mappings are flat (physical = virtual)
 */
#define UART0_PHYS		0xE0000000
#define UART1_PHYS		0xE0001000
#define UART_SIZE		SZ_4K
#define UART_VIRT		0xF0001000

#define SCU_PERIPH_PHYS			0xF8F00000
#define SCU_PERIPH_VIRT			SCU_PERIPH_PHYS

#if IS_ENABLED(CONFIG_DEBUG_ZYNQ_UART1)
# define LL_UART_PADDR		UART1_PHYS
#else
# define LL_UART_PADDR		UART0_PHYS
#endif

#define LL_UART_VADDR		UART_VIRT

/* The following are intended for the devices that are mapped early */

#define SCU_PERIPH_BASE			IOMEM(SCU_PERIPH_VIRT)

#define SLCR_BASE_VIRT			0xf8000000
#define SLCR_BASE_PHYS			0xf8000000

#define SLCR_ARMPLL_CTRL		(SLCR_BASE_VIRT | 0x100)
#define SLCR_DDRPLL_CTRL		(SLCR_BASE_VIRT | 0x104)
#define SLCR_IOPLL_CTRL			(SLCR_BASE_VIRT | 0x108)
#define SLCR_PLL_STATUS			(SLCR_BASE_VIRT | 0x10c)
#define SLCR_ARMPLL_CFG			(SLCR_BASE_VIRT | 0x110)
#define SLCR_DDRPLL_CFG			(SLCR_BASE_VIRT | 0x114)
#define SLCR_IOPLL_CFG			(SLCR_BASE_VIRT | 0x118)
#define SLCR_ARM_CLK_CTRL		(SLCR_BASE_VIRT | 0x120)
#define SLCR_DDR_CLK_CTRL		(SLCR_BASE_VIRT | 0x124)
#define SLCR_DCI_CLK_CTRL		(SLCR_BASE_VIRT | 0x128)
#define SLCR_APER_CLK_CTRL		(SLCR_BASE_VIRT | 0x12c)
#define SLCR_GEM0_CLK_CTRL		(SLCR_BASE_VIRT | 0x140)
#define SLCR_GEM1_CLK_CTRL		(SLCR_BASE_VIRT | 0x144)
#define SLCR_SMC_CLK_CTRL		(SLCR_BASE_VIRT | 0x148)
#define SLCR_LQSPI_CLK_CTRL		(SLCR_BASE_VIRT | 0x14c)
#define SLCR_SDIO_CLK_CTRL		(SLCR_BASE_VIRT | 0x150)
#define SLCR_UART_CLK_CTRL		(SLCR_BASE_VIRT | 0x154)
#define SLCR_SPI_CLK_CTRL		(SLCR_BASE_VIRT | 0x158)
#define SLCR_CAN_CLK_CTRL		(SLCR_BASE_VIRT | 0x15c)
#define SLCR_DBG_CLK_CTRL		(SLCR_BASE_VIRT | 0x164)
#define SLCR_PCAP_CLK_CTRL		(SLCR_BASE_VIRT | 0x168)
#define SLCR_FPGA0_CLK_CTRL		(SLCR_BASE_VIRT | 0x170)
#define SLCR_FPGA1_CLK_CTRL		(SLCR_BASE_VIRT | 0x180)
#define SLCR_FPGA2_CLK_CTRL		(SLCR_BASE_VIRT | 0x190)
#define SLCR_FPGA3_CLK_CTRL		(SLCR_BASE_VIRT | 0x1a0)
#define SLCR_621_TRUE			(SLCR_BASE_VIRT | 0x1c4)

#endif
