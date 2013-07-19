/*
 * Xilinx SLCR driver
 *
 * Copyright (c) 2011-2013 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk/zynq.h>
#include "common.h"

/* register offsets */
#define SLCR_UNLOCK_OFFSET		0x8   /* SCLR unlock register */
#define SLCR_UNLOCK_MAGIC		0xDF0D

#define SLCR_PS_RST_CTRL_OFFSET		0x200 /* PS Software Reset Control */
#define SLCR_A9_CPU_CLKSTOP		0x10
#define SLCR_A9_CPU_RST			0x1

#define SLCR_FPGA_RST_CTRL_OFFSET	0x240 /* FPGA Software Reset Control */
#define SLCR_A9_CPU_RST_CTRL_OFFSET	0x244 /* CPU Software Reset Control */
#define SLCR_REBOOT_STATUS_OFFSET	0x258 /* PS Reboot Status */
#define SLCR_LVL_SHFTR_EN_OFFSET	0x900 /* Level Shifters Enable */

void __iomem *zynq_slcr_base;

/**
 * zynq_slcr_system_reset - Reset the entire system.
 */
void zynq_slcr_system_reset(void)
{
	u32 reboot;

	/*
	 * Unlock the SLCR then reset the system.
	 * Note that this seems to require raw i/o
	 * functions or there's a lockup?
	 */
	writel(SLCR_UNLOCK_MAGIC, zynq_slcr_base + SLCR_UNLOCK_OFFSET);

	/*
	 * Clear 0x0F000000 bits of reboot status register to workaround
	 * the FSBL not loading the bitstream after soft-reboot
	 * This is a temporary solution until we know more.
	 */
	reboot = readl(zynq_slcr_base + SLCR_REBOOT_STATUS_OFFSET);
	writel(reboot & 0xF0FFFFFF, zynq_slcr_base + SLCR_REBOOT_STATUS_OFFSET);
	writel(1, zynq_slcr_base + SLCR_PS_RST_CTRL_OFFSET);
}

/**
 * xslcr_write - Write to a register in SLCR block
 *
 * @offset:	Register offset in SLCR block
 * @val:	Value to write to the register
 **/
void xslcr_write(u32 val, u32 offset)
{
	writel(val, zynq_slcr_base + offset);
}
EXPORT_SYMBOL(xslcr_write);

/**
 * xslcr_read - Read a register in SLCR block
 *
 * @offset:	Register offset in SLCR block
 *
 * return:	Value read from the SLCR register
 **/
u32 xslcr_read(u32 offset)
{
	return readl(zynq_slcr_base + offset);
}
EXPORT_SYMBOL(xslcr_read);

/**
 * xslcr_init_preload_fpga - Disable communication from the PL to PS.
 */
void xslcr_init_preload_fpga(void)
{

	/* Assert FPGA top level output resets */
	xslcr_write(0xF, SLCR_FPGA_RST_CTRL_OFFSET);

	/* Disable level shifters */
	xslcr_write(0, SLCR_LVL_SHFTR_EN_OFFSET);

	/* Enable output level shifters */
	xslcr_write(0xA, SLCR_LVL_SHFTR_EN_OFFSET);
}
EXPORT_SYMBOL(xslcr_init_preload_fpga);

/**
 * xslcr_init_postload_fpga - Re-enable communication from the PL to PS.
 */
void xslcr_init_postload_fpga(void)
{

	/* Enable level shifters */
	xslcr_write(0xf, SLCR_LVL_SHFTR_EN_OFFSET);

	/* Deassert AXI interface resets */
	xslcr_write(0, SLCR_FPGA_RST_CTRL_OFFSET);
}
EXPORT_SYMBOL(xslcr_init_postload_fpga);

/**
 * zynq_slcr_cpu_start - Start cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_start(int cpu)
{
	u32 reg = readl(zynq_slcr_base + SLCR_A9_CPU_RST_CTRL_OFFSET);

	reg &= ~(SLCR_A9_CPU_RST << cpu);
	writel(reg, zynq_slcr_base + SLCR_A9_CPU_RST_CTRL_OFFSET);
	reg &= ~(SLCR_A9_CPU_CLKSTOP << cpu);
	writel(reg, zynq_slcr_base + SLCR_A9_CPU_RST_CTRL_OFFSET);
}

/**
 * zynq_slcr_cpu_stop - Stop cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_stop(int cpu)
{
	u32 reg = readl(zynq_slcr_base + SLCR_A9_CPU_RST_CTRL_OFFSET);
	reg |= (SLCR_A9_CPU_CLKSTOP | SLCR_A9_CPU_RST) << cpu;
	writel(reg, zynq_slcr_base + SLCR_A9_CPU_RST_CTRL_OFFSET);
}

/**
 * zynq_slcr_init
 * Returns 0 on success, negative errno otherwise.
 *
 * Called early during boot from platform code to remap SLCR area.
 */
int __init zynq_slcr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-slcr");
	if (!np) {
		pr_err("%s: no slcr node found\n", __func__);
		BUG();
	}

	zynq_slcr_base = of_iomap(np, 0);
	if (!zynq_slcr_base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		BUG();
	}

	/* unlock the SLCR so that registers can be changed */
	writel(SLCR_UNLOCK_MAGIC, zynq_slcr_base + SLCR_UNLOCK_OFFSET);

	pr_info("%s mapped to %p\n", np->name, zynq_slcr_base);

	zynq_clock_init(zynq_slcr_base);

	of_node_put(np);

	return 0;
}
