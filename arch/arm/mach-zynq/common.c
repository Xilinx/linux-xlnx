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
#include <linux/mmzone.h>

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

#define DMA_ZONE_PAGES 		(SZ_32M >> PAGE_SHIFT)
#define DMA_ZONE_HOLE_PAGES 	(SZ_512K >> PAGE_SHIFT)

/* Setup a special DMA memory zone to deal with the fact the from 0 - 512K cannot
 * be DMA-ed into. The size of the DMA zone is a bit arbitrary but doesn't hurt to
 * be larger as the memory allocator will use the DMA zone for normal if needed.
 */
void xilinx_adjust_zones(unsigned long *zone_size, unsigned long *zhole_size)
{
	/* the normal zone has already been setup when this function is called and is
	 * assumed to be the only zone, this code is a bit confusing
	 */

	pr_info("Xilinx: Adjusting memory zones to add DMA zone\n");

	/* setup the zone sizes reducing the normal zone by the size
	 * of the DMA zone
	 */
	zone_size[ZONE_NORMAL] = zone_size[0] - DMA_ZONE_PAGES;
	zone_size[ZONE_DMA] = DMA_ZONE_PAGES;

	/* setup the holes in each zone, the normal zone has the same hole it had
	 * on entry to this function which should be no hole
	 * the dma zone has a hole where DMA can't be done
	 */
	zhole_size[ZONE_NORMAL] = zhole_size[0];
	zhole_size[ZONE_DMA] = DMA_ZONE_HOLE_PAGES;	
}

/**
 * xilinx_init_machine() - System specific initialization, intended to be
 *			   called from board specific initialization.
 */
void __init xilinx_init_machine(void)
{
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
		.virtual	= TTC0_VIRT,
		.pfn		= __phys_to_pfn(TTC0_PHYS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
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
		.length		= SZ_4K,
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
