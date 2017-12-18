/*
 * r8a7779 processor support
 *
 * Copyright (C) 2011, 2013  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2013  Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk/renesas.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "r8a7779.h"

static struct map_desc r8a7779_io_desc[] __initdata = {
	/* 2M identity mapping for 0xf0000000 (MPCORE) */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0xf0000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE_NONSHARED
	},
	/* 16M identity mapping for 0xfexxxxxx (DMAC-S/HPBREG/INTC2/LRAM/DBSC) */
	{
		.virtual	= 0xfe000000,
		.pfn		= __phys_to_pfn(0xfe000000),
		.length		= SZ_16M,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init r8a7779_map_io(void)
{
	debug_ll_io_init();
	iotable_init(r8a7779_io_desc, ARRAY_SIZE(r8a7779_io_desc));
}

/* IRQ */
#define INT2SMSKCR0 IOMEM(0xfe7822a0)
#define INT2SMSKCR1 IOMEM(0xfe7822a4)
#define INT2SMSKCR2 IOMEM(0xfe7822a8)
#define INT2SMSKCR3 IOMEM(0xfe7822ac)
#define INT2SMSKCR4 IOMEM(0xfe7822b0)

#define INT2NTSR0 IOMEM(0xfe700060)
#define INT2NTSR1 IOMEM(0xfe700064)

static void __init r8a7779_init_irq_dt(void)
{
	irqchip_init();

	/* route all interrupts to ARM */
	__raw_writel(0xffffffff, INT2NTSR0);
	__raw_writel(0x3fffffff, INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	__raw_writel(0xfffffff0, INT2SMSKCR0);
	__raw_writel(0xfff7ffff, INT2SMSKCR1);
	__raw_writel(0xfffbffdf, INT2SMSKCR2);
	__raw_writel(0xbffffffc, INT2SMSKCR3);
	__raw_writel(0x003fee3f, INT2SMSKCR4);
}

#define MODEMR		0xffcc0020

static u32 __init r8a7779_read_mode_pins(void)
{
	static u32 mode;
	static bool mode_valid;

	if (!mode_valid) {
		void __iomem *modemr = ioremap_nocache(MODEMR, PAGE_SIZE);
		BUG_ON(!modemr);
		mode = ioread32(modemr);
		iounmap(modemr);
		mode_valid = true;
	}

	return mode;
}

static void __init r8a7779_init_time(void)
{
	r8a7779_clocks_init(r8a7779_read_mode_pins());
	clocksource_probe();
}

static const char *const r8a7779_compat_dt[] __initconst = {
	"renesas,r8a7779",
	NULL,
};

DT_MACHINE_START(R8A7779_DT, "Generic R8A7779 (Flattened Device Tree)")
	.smp		= smp_ops(r8a7779_smp_ops),
	.map_io		= r8a7779_map_io,
	.init_early	= shmobile_init_delay,
	.init_time	= r8a7779_init_time,
	.init_irq	= r8a7779_init_irq_dt,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a7779_compat_dt,
MACHINE_END
