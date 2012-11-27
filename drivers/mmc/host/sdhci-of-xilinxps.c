/*
 * Xilinx Zynq Secure Digital Host Controller Interface.
 * Copyright (C) 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 Wind River Systems, Inc.
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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include "sdhci-pltfm.h"

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

static int __devinit sdhci_zynq_probe(struct platform_device *pdev)
{
	int ret;
	const void *prop = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host = NULL;

	ret = sdhci_pltfm_register(pdev, &sdhci_zynq_pdata);
	if (ret == 0) {
		prop = of_get_property(np, "xlnx,has-cd", NULL);
		if (prop == NULL) {
			host = platform_get_drvdata(pdev);
			host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
		} else if (!(u32) be32_to_cpup(prop))  {
			host = platform_get_drvdata(pdev);
			host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
		}
	} else {
		dev_err(&pdev->dev, "Platform registration failed\n");
	}

	return ret;
}

static int __devexit sdhci_zynq_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_zynq_of_match[] __devinitconst = {
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
		.pm = SDHCI_PLTFM_PMOPS,
	},
	.probe = sdhci_zynq_probe,
	.remove = __devexit_p(sdhci_zynq_remove),
};

module_platform_driver(sdhci_zynq_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface OF driver");
MODULE_AUTHOR("Michal Simek <monstr@monstr.eu>, Vlad Lungu <vlad.lungu@windriver.com>");
MODULE_LICENSE("GPL v2");
