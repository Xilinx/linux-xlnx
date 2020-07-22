// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DP Subsystem Driver
 *
 *  Copyright (C) 2017 - 2018 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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

#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "xlnx_drv.h"

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"

static int
zynqmp_dpsub_bind(struct device *dev, struct device *master, void *data)
{
	int ret;

	ret = zynqmp_disp_bind(dev, master, data);
	if (ret)
		return ret;

	/* zynqmp_disp should bind first, so zynqmp_dp encoder can find crtc */
	ret = zynqmp_dp_bind(dev, master, data);
	if (ret)
		return ret;

	return 0;
}

static void
zynqmp_dpsub_unbind(struct device *dev, struct device *master, void *data)
{
	zynqmp_dp_unbind(dev, master, data);
	zynqmp_disp_unbind(dev, master, data);
}

static const struct component_ops zynqmp_dpsub_component_ops = {
	.bind	= zynqmp_dpsub_bind,
	.unbind	= zynqmp_dpsub_unbind,
};

static int zynqmp_dpsub_probe(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub;
	int ret;

	dpsub = devm_kzalloc(&pdev->dev, sizeof(*dpsub), GFP_KERNEL);
	if (!dpsub)
		return -ENOMEM;

	/* Sub-driver will access dpsub from drvdata */
	platform_set_drvdata(pdev, dpsub);
	pm_runtime_enable(&pdev->dev);

	/*
	 * DP should be probed first so that the zynqmp_disp can set the output
	 * format accordingly.
	 */
	ret = zynqmp_dp_probe(pdev);
	if (ret)
		goto err_pm;

	ret = zynqmp_disp_probe(pdev);
	if (ret)
		goto err_dp;

	ret = component_add(&pdev->dev, &zynqmp_dpsub_component_ops);
	if (ret)
		goto err_disp;

	/* Try the reserved memory. Proceed if there's none */
	of_reserved_mem_device_init(&pdev->dev);

	/* Populate the sound child nodes */
	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to populate child nodes\n");
		goto err_rmem;
	}

	dpsub->master = xlnx_drm_pipeline_init(pdev);
	if (IS_ERR(dpsub->master)) {
		dev_err(&pdev->dev, "failed to initialize the drm pipeline\n");
		goto err_populate;
	}

	dev_info(&pdev->dev, "ZynqMP DisplayPort Subsystem driver probed");

	return 0;

err_populate:
	of_platform_depopulate(&pdev->dev);
err_rmem:
	of_reserved_mem_device_release(&pdev->dev);
	component_del(&pdev->dev, &zynqmp_dpsub_component_ops);
err_disp:
	zynqmp_disp_remove(pdev);
err_dp:
	zynqmp_dp_remove(pdev);
err_pm:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int zynqmp_dpsub_remove(struct platform_device *pdev)
{
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);
	int err, ret = 0;

	xlnx_drm_pipeline_exit(dpsub->master);
	of_platform_depopulate(&pdev->dev);
	of_reserved_mem_device_release(&pdev->dev);
	component_del(&pdev->dev, &zynqmp_dpsub_component_ops);

	err = zynqmp_disp_remove(pdev);
	if (err)
		ret = -EIO;

	err = zynqmp_dp_remove(pdev);
	if (err)
		ret = -EIO;

	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int __maybe_unused zynqmp_dpsub_pm_suspend(struct device *dev)
{
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	zynqmp_dp_pm_suspend(dpsub->dp);

	return 0;
}

static int __maybe_unused zynqmp_dpsub_pm_resume(struct device *dev)
{
	struct platform_device *pdev =
		container_of(dev, struct platform_device, dev);
	struct zynqmp_dpsub *dpsub = platform_get_drvdata(pdev);

	zynqmp_dp_pm_resume(dpsub->dp);

	return 0;
}

static const struct dev_pm_ops zynqmp_dpsub_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynqmp_dpsub_pm_suspend,
			zynqmp_dpsub_pm_resume)
};

static const struct of_device_id zynqmp_dpsub_of_match[] = {
	{ .compatible = "xlnx,zynqmp-dpsub-1.7", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_dpsub_of_match);

static struct platform_driver zynqmp_dpsub_driver = {
	.probe			= zynqmp_dpsub_probe,
	.remove			= zynqmp_dpsub_remove,
	.driver			= {
		.name		= "zynqmp-display",
		.of_match_table	= zynqmp_dpsub_of_match,
		.pm             = &zynqmp_dpsub_pm_ops,
	},
};

module_platform_driver(zynqmp_dpsub_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("ZynqMP DP Subsystem Driver");
MODULE_LICENSE("GPL v2");
