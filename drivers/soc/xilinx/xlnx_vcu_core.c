// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx VCU core driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Rajan Vaja <rajan.vaja@xilinx.com>
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <soc/xilinx/xlnx_vcu.h>

static const struct mfd_cell xvcu_devs[] = {
	{
		.name = "xilinx-vcu-clk",
	},
	{
		.name = "xilinx-vcu",
	},
};

static int xvcu_core_probe(struct platform_device *pdev)
{
	struct xvcu_device *xvcu;
	struct resource *res;
	int ret;

	xvcu = devm_kzalloc(&pdev->dev, sizeof(*xvcu), GFP_KERNEL);
	if (!xvcu)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vcu_slcr");
	if (!res) {
		dev_err(&pdev->dev, "get vcu_slcr memory resource failed.\n");
		return -ENODEV;
	}

	xvcu->vcu_slcr_ba = devm_ioremap_nocache(&pdev->dev, res->start,
						 resource_size(res));
	if (!xvcu->vcu_slcr_ba) {
		dev_err(&pdev->dev, "vcu_slcr register mapping failed.\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "logicore");
	if (!res) {
		dev_err(&pdev->dev, "get logicore memory resource failed.\n");
		return -ENODEV;
	}

	xvcu->logicore_reg_ba = devm_ioremap_nocache(&pdev->dev, res->start,
						     resource_size(res));
	if (!xvcu->logicore_reg_ba) {
		dev_err(&pdev->dev, "logicore register mapping failed.\n");
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, xvcu);

	xvcu->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(xvcu->aclk)) {
		dev_err(&pdev->dev, "Could not get aclk clock\n");
		return PTR_ERR(xvcu->aclk);
	}

	ret = clk_prepare_enable(xvcu->aclk);
	if (ret) {
		dev_err(&pdev->dev, "aclk clock enable failed\n");
		return ret;
	}

	/*
	 * Do the Gasket isolation and put the VCU out of reset
	 * Bit 0 : Gasket isolation
	 * Bit 1 : put VCU out of reset
	 */
	xvcu->reset_gpio = devm_gpiod_get_optional(&pdev->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(xvcu->reset_gpio)) {
		ret = PTR_ERR(xvcu->reset_gpio);
		dev_err(&pdev->dev, "failed to get reset gpio for vcu.\n");
		return ret;
	}

	if (xvcu->reset_gpio) {
		gpiod_set_value(xvcu->reset_gpio, 0);
		/* min 2 clock cycle of vcu pll_ref, slowest freq is 33.33KHz */
		usleep_range(60, 120);
		gpiod_set_value(xvcu->reset_gpio, 1);
		usleep_range(60, 120);
	} else {
		dev_warn(&pdev->dev, "No reset gpio info from dts for vcu. This may lead to incorrect functionality if VCU isolation is removed post initialization.\n");
	}

	iowrite32(VCU_GASKET_VALUE, xvcu->logicore_reg_ba + VCU_GASKET_INIT);

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, xvcu_devs,
			      ARRAY_SIZE(xvcu_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to add MFD devices %d\n", ret);
		goto err_mfd_add_devices;
	}

	dev_dbg(&pdev->dev, "Successfully added MFD devices\n");

	return 0;

err_mfd_add_devices:
	/* Add the the Gasket isolation and put the VCU in reset. */
	iowrite32(0, xvcu->logicore_reg_ba + VCU_GASKET_INIT);

	clk_disable_unprepare(xvcu->aclk);

	return ret;
}

static int xvcu_core_remove(struct platform_device *pdev)
{
	struct xvcu_device *xvcu;

	xvcu = platform_get_drvdata(pdev);
	if (!xvcu)
		return -ENODEV;

	mfd_remove_devices(&pdev->dev);

	/* Add the the Gasket isolation and put the VCU in reset. */
	if (xvcu->reset_gpio) {
		gpiod_set_value(xvcu->reset_gpio, 0);
		/* min 2 clock cycle of vcu pll_ref, slowest freq is 33.33KHz */
		usleep_range(60, 120);
		gpiod_set_value(xvcu->reset_gpio, 1);
		usleep_range(60, 120);
	}
	iowrite32(0, xvcu->logicore_reg_ba + VCU_GASKET_INIT);

	clk_disable_unprepare(xvcu->aclk);

	return 0;
}

static const struct of_device_id xvcu_core_of_id_table[] = {
	{ .compatible = "xlnx,vcu" },
	{ .compatible = "xlnx,vcu-logicoreip-1.0" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvcu_core_of_id_table);

static struct platform_driver xvcu_core_driver = {
	.driver = {
		.name = "xilinx-vcu-core",
		.of_match_table = xvcu_core_of_id_table,
	},
	.probe = xvcu_core_probe,
	.remove = xvcu_core_remove,
};

module_platform_driver(xvcu_core_driver);

MODULE_AUTHOR("Rajan Vaja <rajan.vaja@xilinx.com>");
MODULE_DESCRIPTION("Xilinx VCU core Driver");
MODULE_LICENSE("GPL v2");
