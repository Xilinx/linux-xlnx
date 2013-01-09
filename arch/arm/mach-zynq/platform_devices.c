/*
 * Copyright (C) 2011 Xilinx
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/module.h>
#include <linux/amba/xilinx_dma.h>
#include <linux/xilinx_devices.h>
#include <mach/dma.h>
#include <asm/pmu.h>
#include "common.h"

#define DMAC0_BASE		(0xF8003000)
#define IRQ_DMAC0_ABORT		45
#define IRQ_DMAC0		46
#define IRQ_DMAC3		72

static struct resource dmac0[] = {
	{
		.start = DMAC0_BASE,
		.end = DMAC0_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_DMAC0_ABORT,
		.end = IRQ_DMAC0_ABORT,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DMAC0,
		.end = IRQ_DMAC0 + 3,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DMAC3,
		.end = IRQ_DMAC3 + 3,
		.flags = IORESOURCE_IRQ,
	},
};

static struct pl330_platform_config dmac_config0 = {
	.channels = 8,
	.starting_channel = 0,
};

static u64 dma_mask = 0xFFFFFFFFUL;

static struct platform_device dmac_device0 = {
	.name = "pl330",
	.id = 0,
	.dev = {
		.platform_data = &dmac_config0,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = dmac0,
	.num_resources = ARRAY_SIZE(dmac0),
};


#ifdef CONFIG_XILINX_TEST

static struct platform_device xilinx_dma_test = {
	.name = "pl330_test",
	.id = 0,
	.dev = {
		.platform_data = NULL,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = NULL,
	.num_resources = 0,
};

#endif

static struct resource xilinx_pmu_resource = {
        .start  = 37,
        .end    = 38,
        .flags  = IORESOURCE_IRQ,
};

static struct platform_device xilinx_pmu_device = {
        .name           = "arm-pmu",
        .id             = ARM_PMU_DEVICE_CPU,
        .num_resources  = 1,
	.resource	= &xilinx_pmu_resource,
};

/* add all platform devices to the following table so they
 * will be registered
 */
static struct platform_device *xilinx_pdevices[] __initdata = {
	&dmac_device0,
	/* &dmac_device1, */
#ifdef CONFIG_XILINX_TEST
	&xilinx_dma_test,
#endif
	&xilinx_pmu_device,
};

/**
 * platform_device_init - Initialize all the platform devices.
 *
 **/
void __init platform_device_init(void)
{
	int ret, i;
	struct platform_device **devptr;
	int size;

	devptr = &xilinx_pdevices[0];
	size = ARRAY_SIZE(xilinx_pdevices);

	ret = 0;

	/* Initialize all the platform devices */

	for (i = 0; i < size; i++, devptr++) {
		pr_info("registering platform device '%s' id %d\n",
			(*devptr)->name,
			(*devptr)->id);
			ret = platform_device_register(*devptr);
		if (ret)
			pr_info("Unable to register platform device '%s': %d\n",
				(*devptr)->name, ret);
	}

//#if defined CONFIG_SPI_SPIDEV || defined CONFIG_MTD_M25P80
//	spi_register_board_info(&xilinx_qspipss_0_boardinfo, 1);
//#endif

}

