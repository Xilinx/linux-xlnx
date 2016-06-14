/*
 * Copyright (C) 2016 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/soc/xilinx/zynqmp/firmware.h>

#define ZYNQMP_NR_RESETS (ZYNQMP_PM_RESET_END - ZYNQMP_PM_RESET_START - 2)
#define ZYNQMP_RESET_ID (ZYNQMP_PM_RESET_START + 1)

struct zynqmp_reset {
	struct reset_controller_dev rcdev;
};

static int zynqmp_reset_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return zynqmp_pm_reset_assert(ZYNQMP_RESET_ID + id,
						PM_RESET_ACTION_ASSERT);
}

static int zynqmp_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return zynqmp_pm_reset_assert(ZYNQMP_RESET_ID + id,
						PM_RESET_ACTION_RELEASE);
}

static int zynqmp_reset_status(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	int val;

	zynqmp_pm_reset_get_status(ZYNQMP_RESET_ID + id, &val);
	return val;
}

static int zynqmp_reset_reset(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return zynqmp_pm_reset_assert(ZYNQMP_RESET_ID + id,
						PM_RESET_ACTION_PULSE);
}

static struct reset_control_ops zynqmp_reset_ops = {
	.reset = zynqmp_reset_reset,
	.assert = zynqmp_reset_assert,
	.deassert = zynqmp_reset_deassert,
	.status = zynqmp_reset_status,
};

static int zynqmp_reset_probe(struct platform_device *pdev)
{
	struct zynqmp_reset *zynqmp_reset;
	int ret;

	zynqmp_reset = devm_kzalloc(&pdev->dev,
				sizeof(*zynqmp_reset), GFP_KERNEL);
	if (!zynqmp_reset)
		return -ENOMEM;

	platform_set_drvdata(pdev, zynqmp_reset);

	zynqmp_reset->rcdev.ops = &zynqmp_reset_ops;
	zynqmp_reset->rcdev.owner = THIS_MODULE;
	zynqmp_reset->rcdev.of_node = pdev->dev.of_node;
	zynqmp_reset->rcdev.of_reset_n_cells = 1;
	zynqmp_reset->rcdev.nr_resets = ZYNQMP_NR_RESETS;

	ret = reset_controller_register(&zynqmp_reset->rcdev);
	if (!ret)
		dev_info(&pdev->dev, "Xilinx zynqmp reset driver probed\n");

	return ret;
}

static const struct of_device_id zynqmp_reset_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-reset", },
	{ },
};

static struct platform_driver zynqmp_reset_driver = {
	.probe	= zynqmp_reset_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= zynqmp_reset_dt_ids,
	},
};

static int __init zynqmp_reset_init(void)
{
	return platform_driver_register(&zynqmp_reset_driver);
}

arch_initcall(zynqmp_reset_init);
