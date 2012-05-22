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

/* Virtual address must be inside vmalloc area - this is weird - better
 * to create virtual mapping on the fly */
#define UART0_PHYS			0xE0000000
#define UART0_VIRT			UART0_PHYS + 0x1E000000

#define UART1_PHYS			0xE0001000
#define UART1_VIRT			UART1_PHYS + 0x1E000000

#define TTC0_PHYS			0xF8001000
#define TTC0_VIRT			TTC0_PHYS

#define PL310_L2CC_PHYS			0xF8F02000
#define PL310_L2CC_VIRT			PL310_L2CC_PHYS

#define SCU_PERIPH_PHYS			0xF8F00000
#define SCU_PERIPH_VIRT			SCU_PERIPH_PHYS

/* The following are intended for the devices that are mapped early */

#define TTC0_BASE			IOMEM(TTC0_VIRT)
#define SCU_PERIPH_BASE			IOMEM(SCU_PERIPH_VIRT)
#define SCU_GIC_CPU_BASE		(SCU_PERIPH_BASE + 0x100)
#define SCU_CPU_TIMER_BASE		(SCU_PERIPH_BASE + 0x600)
#define SCU_GIC_DIST_BASE		(SCU_PERIPH_BASE + 0x1000)
#define PL310_L2CC_BASE			IOMEM(PL310_L2CC_VIRT)

/*
 * Mandatory for CONFIG_LL_DEBUG, UART is mapped virtual = physical
 */
#define LL_UART_PADDR	UART1_PHYS
#define LL_UART_VADDR	UART1_VIRT


/* There are two OCM addresses needed for communication between CPUs in SMP.
 * The memory addresses are in the high on-chip RAM and these addresses are
 * mapped flat (virtual = physical). The memory must be mapped early and
 * non-cached.
 */
#define BOOT_ADDR_OFFSET	0xFF0
#define BOOT_STATUS_OFFSET	0xFF4
#define BOOT_STATUS_CPU1_UP	1

/* not possible to you the last page */
#define OCM_HIGH_PHYS			0xFFFFF000
#define OCM_HIGH_VIRT			(OCM_HIGH_PHYS - 0x1000)

#define OCM_HIGH_BASE			IOMEM(OCM_HIGH_VIRT)

#endif
