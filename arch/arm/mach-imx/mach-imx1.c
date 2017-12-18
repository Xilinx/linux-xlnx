/*
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "hardware.h"

#define MX1_AVIC_ADDR	0x00223000

static void __init imx1_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX1);
}

static void __init imx1_init_irq(void)
{
	void __iomem *avic_addr;

	avic_addr = ioremap(MX1_AVIC_ADDR, SZ_4K);
	WARN_ON(!avic_addr);

	mxc_init_irq(avic_addr);
}

static const char * const imx1_dt_board_compat[] __initconst = {
	"fsl,imx1",
	NULL
};

DT_MACHINE_START(IMX1_DT, "Freescale i.MX1 (Device Tree Support)")
	.map_io		= debug_ll_io_init,
	.init_early	= imx1_init_early,
	.init_irq	= imx1_init_irq,
	.dt_compat	= imx1_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
