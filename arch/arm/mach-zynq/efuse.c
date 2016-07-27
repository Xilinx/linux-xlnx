/*
 * Xilinx EFUSE driver
 *
 * Copyright (c) 2016 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/of_address.h>
#include "common.h"

#define EFUSE_STATUS_OFFSET	0x10

/* 0 means cpu1 is working, 1 means cpu1 is broken */
#define EFUSE_STATUS_CPU_BIT	BIT(7)

void __iomem *zynq_efuse_base;

/**
 * zynq_efuse_cpu_state - Read/write cpu state
 * @cpu:	cpu number
 *
 * Return: true if cpu is running, false if cpu is broken
 */
bool zynq_efuse_cpu_state(int cpu)
{
	u32 state;

	if (!cpu)
		return true;

	if (!zynq_efuse_base)
		return true;

	state = readl(zynq_efuse_base + EFUSE_STATUS_OFFSET);
	state &= EFUSE_STATUS_CPU_BIT;

	if (!state)
		return true;

	return false;
}

/**
 * zynq_early_efuse_init - Early efuse init function
 *
 * Return:	0 on success, negative errno otherwise.
 *
 * Called very early during boot from platform code.
 */
int __init zynq_early_efuse_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-efuse");
	if (!np) {
		pr_err("%s: no efuse node found\n", __func__);
		return -1;
	}

	zynq_efuse_base = of_iomap(np, 0);
	if (!zynq_efuse_base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		return -1;
	}

	np->data = (__force void *)zynq_efuse_base;

	pr_info("%s mapped to %p\n", np->name, zynq_efuse_base);

	of_node_put(np);

	return 0;
}
