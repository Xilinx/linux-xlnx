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
#define UART0_PHYS			0xE0000000
#define UART0_VIRT			0xFE000000

//#define UART1_PHYS			0xE0001000
//#define UART1_VIRT			0xFE001000

#define SCU_PERIPH_PHYS			0xF8F00000
#define SCU_PERIPH_VIRT			0xFE00C000

/* Virtual addresses now have to stay lower in newer kernels, so move the OCM down
 * from 0xFFXXXXXX to 0xFEXXXXXX to make it work 
 */
#define OCM_LOW_PHYS			0xFFFC0000
#define OCM_LOW_VIRT			0xFE100000

#define OCM_HIGH_PHYS			0xFFFF1000
#define OCM_HIGH_VIRT			0xFE200000

/* The following are intended for the devices that are mapped early */

#define SCU_PERIPH_BASE			IOMEM(SCU_PERIPH_VIRT)
#define SCU_GIC_CPU_BASE		(SCU_PERIPH_BASE + 0x100)
#define SCU_GLOBAL_TIMER_BASE		(SCU_PERIPH_BASE + 0x200)
#define SCU_GIC_DIST_BASE		(SCU_PERIPH_BASE + 0x1000)
#define OCM_LOW_BASE			IOMEM(OCM_LOW_VIRT)
#define OCM_HIGH_BASE			IOMEM(OCM_HIGH_VIRT)

/* There are two OCM addresses needed for communication between CPUs in SMP.
 * The memory addresses are in the high on-chip RAM and these addresses are
 * mapped flat (virtual = physical). The memory must be mapped early and
 * non-cached.
 */
#define BOOT_ADDR_OFFSET	0xEFF0
#define BOOT_STATUS_OFFSET	0xEFF4
#define BOOT_STATUS_CPU1_UP	1

/*
 * Mandatory for CONFIG_LL_DEBUG, UART is mapped virtual = physical
 */
#if defined(CONFIG_ZYNQ_EARLY_UART1)
	#define LL_UART_PADDR	(UART0_PHYS+0x1000)
	#define LL_UART_VADDR	(UART0_VIRT+0x1000)
#else
	#define LL_UART_PADDR	UART0_PHYS
	#define LL_UART_VADDR	UART0_VIRT
#endif
#endif
