/*
 * This file contains common code that is intended to be used across
 * boards so that it's not replicated.
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>

#include <mach/zynq_soc.h>
#include <mach/clkdev.h>
#include "common.h"

#define IRQ_TIMERCOUNTER1	69
#define IRQ_ETH1                77
#define SDIO1_IRQ		79
#define IRQ_I2C1		80
#define IRQ_SPI1		81
#define IRQ_UART1		82

static struct of_device_id zynq_of_bus_ids[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};

/**
 * xilinx_init_machine() - System specific initialization, intended to be
 *			   called from board specific initialization.
 */
void __init xilinx_init_machine(void)
{
	u32 dscr;

#ifdef CONFIG_CACHE_L2X0
	void *l2cache_base;

	/* Static mapping, never released */
	l2cache_base = ioremap((int)PL310_L2CC_BASE, SZ_4K);
	BUG_ON(!l2cache_base);

	__raw_writel(0x121, l2cache_base + L2X0_TAG_LATENCY_CTRL);
	__raw_writel(0x121, l2cache_base + L2X0_DATA_LATENCY_CTRL);

	/*
	 * 64KB way size, 8-way associativity, parity disabled, prefetching option
	 */
#ifndef	CONFIG_XILINX_L2_PREFETCH
	l2x0_init(l2cache_base, 0x02060000, 0xF0F0FFFF);
#else
	l2x0_init(l2cache_base, 0x72060000, 0xF0F0FFFF);
#endif
#endif
	/* clear out the halt debug mode so that the Linux hardware
	 * breakpoint system is happy 
	 */
	ARM_DBG_READ(c1, 0, dscr);
	ARM_DBG_WRITE(c2, 2, (dscr & ~ARM_DSCR_HDBGEN));

	of_platform_bus_probe(NULL, zynq_of_bus_ids, NULL);

	platform_device_init();
}

/**
 * xilinx_irq_init() - Interrupt controller initialization for the GIC.
 */
void __init xilinx_irq_init(void)
{
	gic_init(0, 29, SCU_GIC_DIST_BASE, SCU_GIC_CPU_BASE);

	/* when running in AMP mode on CPU0, allocate unused interrupts to the 
	 * other CPU so another OS can run on it, or if just running Linux on 
	 * the 2nd CPU as a test, do the same
	 */
#if 	defined(CONFIG_XILINX_AMP_CPU0_MASTER)	|| \
	defined(CONFIG_ZYNQ_AMP_CPU0_MASTER)	|| \
	defined(CONFIG_XILINX_CPU1_TEST)

	pr_info("Xilinx AMP: Setting IRQs to CPU1\n");
	gic_set_cpu(1, IRQ_TIMERCOUNTER1);
	gic_set_cpu(1, IRQ_TIMERCOUNTER1 + 1);
	gic_set_cpu(1, IRQ_UART1);
	gic_set_cpu(1, IRQ_I2C1);
	gic_set_cpu(1, IRQ_ETH1);
	gic_set_cpu(1, IRQ_SPI1);
	gic_set_cpu(1, SDIO1_IRQ);
#endif

}

/* The minimum devices needed to be mapped before the VM system is up and
 * running include the GIC, UART and Timer Counter.
 */

static struct map_desc io_desc[] __initdata = {
	{
		.virtual	= SCU_PERIPH_VIRT,
		.pfn		= __phys_to_pfn(SCU_PERIPH_PHYS),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= PL310_L2CC_VIRT,
		.pfn		= __phys_to_pfn(PL310_L2CC_PHYS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},

#ifdef CONFIG_DEBUG_LL
	{
		.virtual	= UART0_VIRT,
		.pfn		= __phys_to_pfn(UART0_PHYS),
		.length		= SZ_8K,
		.type		= MT_DEVICE,
	},
#endif
	/* create a mapping for the OCM  (256K) leaving a hole for the
	 * interrupt vectors which are handled in the kernel
	 */
	{
		.virtual	= OCM_LOW_VIRT,
		.pfn		= __phys_to_pfn(OCM_LOW_PHYS),
		.length		= (192 * SZ_1K),
		.type		= MT_DEVICE_CACHED,
	},
	{
		.virtual	= OCM_HIGH_VIRT,
		.pfn		= __phys_to_pfn(OCM_HIGH_PHYS),
		.length		= (60 * SZ_1K),
		.type		= MT_DEVICE,
	},
};

/**
 * xilinx_map_io() - Create memory mappings needed for early I/O.
 */
void __init xilinx_map_io(void)
{
	iotable_init(io_desc, ARRAY_SIZE(io_desc));
}

/**
 * xilinx_memory_init() - Initialize special memory 
 * 
 * We need to stop things allocating the low memory as DMA can't work in
 * the 1st 512K of memory.  Using reserve vs remove is not totally clear yet.
 */
void __init xilinx_memory_init()
{
	/* Reserve the 0-0x4000 addresses (before page tables and kernel)
	 * which can't be used for DMA
	 */ 
	memblock_reserve(0, 0x4000);

	/* the video frame buffer is in DDR and shouldn't be used by the kernel
	 * as it will be ioremapped by the frame buffer driver
	 */
	memblock_remove(0xF000000, 0x1000000);
}
