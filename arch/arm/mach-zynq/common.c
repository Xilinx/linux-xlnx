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

static struct of_device_id zynq_of_bus_ids[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};

static const struct of_device_id xilinx_dt_irq_match[] __initconst = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init },
	{ }
};

/**
 * xilinx_irq_init() - Interrupt controller initialization for the GIC.
 */
void __init xilinx_irq_init(void)
{
	of_irq_init(xilinx_dt_irq_match);
}

/* The minimum devices needed to be mapped before the VM system is up and
 * running include the GIC, UART and Timer Counter.
 */
struct map_desc io_desc[] __initdata = {
	{
		.virtual	= SCU_PERIPH_VIRT,
		.pfn		= __phys_to_pfn(SCU_PERIPH_PHYS),
		.length		= SZ_8K,
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
#if (CONFIG_PHYS_OFFSET == 0)
	/* Reserve the 0-0x4000 addresses (before page tables and kernel)
	 * which can't be used for DMA
	 */ 
	memblock_reserve(0, 0x4000);
#endif
}

#ifdef CONFIG_CACHE_L2X0
static int __init xilinx_l2c_init(void)
{
	/* 64KB way size, 8-way associativity, parity disabled,
	 * prefetching option */
#ifndef	CONFIG_XILINX_L2_PREFETCH
	return l2x0_of_init(0x02060000, 0xF0F0FFFF);
#else
	return l2x0_of_init(0x72060000, 0xF0F0FFFF);
#endif
}
early_initcall(xilinx_l2c_init);
#endif

/**
 * xilinx_init_machine() - System specific initialization, intended to be
 *			   called from board specific initialization.
 */
void __init xilinx_init_machine(void)
{
	of_platform_bus_probe(NULL, zynq_of_bus_ids, NULL);
	platform_device_init();
}
