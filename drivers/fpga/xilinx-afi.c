// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA AFI bridge.
 * Copyright (c) 2018 Xilinx Inc.
 */

#include <linux/err.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/**
 * struct afi_fpga - AFI register description
 * @value: value to be written to the register
 * @regid: Register id for the register to be written
 */
struct afi_fpga {
	u32 value;
	u32 regid;
};

static int afi_fpga_probe(struct platform_device *pdev)
{
	struct afi_fpga *afi_fpga;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int i, entries, pairs;
	u32 reg, val;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	if (!eemi_ops->ioctl)
		return -ENOTSUPP;

	afi_fpga = devm_kzalloc(&pdev->dev, sizeof(*afi_fpga), GFP_KERNEL);
	if (!afi_fpga)
		return -ENOMEM;
	platform_set_drvdata(pdev, afi_fpga);

	entries = of_property_count_u32_elems(np, "config-afi");
	if (!entries || (entries % 2)) {
		dev_err(&pdev->dev, "Invalid number of registers\n");
		return -EINVAL;
	}
	pairs = entries / 2;

	for (i = 0; i < pairs; i++) {
		ret = of_property_read_u32_index(np, "config-afi", i * 2,
						 &reg);
		if (ret) {
			dev_err(&pdev->dev, "failed to read register\n");
			return -EINVAL;
		}
		ret = of_property_read_u32_index(np, "config-afi", i * 2 + 1,
						 &val);
		if (ret) {
			dev_err(&pdev->dev, "failed to read value\n");
			return -EINVAL;
		}
		ret = eemi_ops->ioctl(0, IOCTL_AFI, reg, val, NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "AFI register write error %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

static const struct of_device_id afi_fpga_ids[] = {
	{ .compatible = "xlnx,afi-fpga" },
	{ },
};
MODULE_DEVICE_TABLE(of, afi_fpga_ids);

static struct platform_driver afi_fpga_driver = {
	.driver = {
		.name = "afi-fpga",
		.of_match_table = afi_fpga_ids,
	},
	.probe = afi_fpga_probe,
};
module_platform_driver(afi_fpga_driver);

MODULE_DESCRIPTION("FPGA afi module");
MODULE_AUTHOR("Shubhrajyoti Datta <shubhrajyoti.datta@xilinx.com>");
MODULE_LICENSE("GPL v2");
