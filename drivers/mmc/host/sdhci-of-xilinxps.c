/*
 * Xilinx Zynq Secure Digital Host Controller Interface.
 * Copyright (C) 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 Wind River Systems, Inc.
 * Copyright (C) 2013 Xilinx Inc.
 *
 * Based on sdhci-of-esdhc.c
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/module.h>
#include "sdhci-pltfm.h"

/**
 * struct xsdhcips
 * @devclk:	Pointer to the peripheral clock
 * @aperclk:	Pointer to the APER clock
 */
struct xsdhcips {
	struct clk		*devclk;
	struct clk		*aperclk;
};

static unsigned int zynq_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock;
}

static struct sdhci_ops sdhci_zynq_ops = {
	.get_max_clock = zynq_of_get_max_clock,
};

static struct sdhci_pltfm_data sdhci_zynq_pdata = {
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
	.ops = &sdhci_zynq_ops,
};

#ifdef CONFIG_PM_SLEEP
/**
 * xsdhcips_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Put the device in a low power state.
 */
static int xsdhcips_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable(xsdhcips->devclk);
	clk_disable(xsdhcips->aperclk);

	return 0;
}

/**
 * xsdhcips_resume - Resume method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Resume operation after suspend
 */
static int xsdhcips_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;
	int ret;

	ret = clk_enable(xsdhcips->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(xsdhcips->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xsdhcips->aperclk);
		return ret;
	}

	return sdhci_resume_host(host);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(xsdhcips_dev_pm_ops, xsdhcips_suspend,
			 xsdhcips_resume);

static int sdhci_zynq_probe(struct platform_device *pdev)
{
	int ret;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct xsdhcips *xsdhcips;

	xsdhcips = devm_kzalloc(&pdev->dev, sizeof(*xsdhcips), GFP_KERNEL);
	if (!xsdhcips)
		return -ENOMEM;

	xsdhcips->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(xsdhcips->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		return PTR_ERR(xsdhcips->aperclk);
	}

	xsdhcips->devclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xsdhcips->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		return PTR_ERR(xsdhcips->devclk);
	}

	ret = clk_prepare_enable(xsdhcips->aperclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(xsdhcips->devclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_aper;
	}

	ret = sdhci_pltfm_register(pdev, &sdhci_zynq_pdata);
	if (ret) {
		dev_err(&pdev->dev, "Platform registration failed\n");
		goto clk_disable_all;
	}

	host = platform_get_drvdata(pdev);
	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = xsdhcips;

	return 0;

clk_disable_all:
	clk_disable_unprepare(xsdhcips->devclk);
clk_dis_aper:
	clk_disable_unprepare(xsdhcips->aperclk);

	return ret;
}

static int sdhci_zynq_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;

	clk_disable_unprepare(xsdhcips->devclk);
	clk_disable_unprepare(xsdhcips->aperclk);

	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_zynq_of_match[] = {
	{ .compatible = "xlnx,ps7-sdhci-1.00.a" },
	{ .compatible = "generic-sdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_zynq_of_match);

static struct platform_driver sdhci_zynq_driver = {
	.driver = {
		.name = "sdhci-zynq",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_zynq_of_match,
		.pm = &xsdhcips_dev_pm_ops,
	},
	.probe = sdhci_zynq_probe,
	.remove = sdhci_zynq_remove,
};

module_platform_driver(sdhci_zynq_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface OF driver");
MODULE_AUTHOR("Michal Simek <monstr@monstr.eu>, Vlad Lungu <vlad.lungu@windriver.com>");
MODULE_LICENSE("GPL v2");
