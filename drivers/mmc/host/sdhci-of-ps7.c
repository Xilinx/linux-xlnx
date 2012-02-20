/*
 * drivers/mmc/host/sdhci-of-ps7.c
 *
 * Xilinx Zynq ps7 Secure Digital Host Controller Interface.
 * Copyright (C) 2011 Michal Simek <monstr@monstr.eu>
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

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"

static struct sdhci_pltfm_data sdhci_xilinx_pdata = {
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
};

static int __devinit sdhci_xilinx_probe(struct platform_device *pdev)
{
	return sdhci_pltfm_register(pdev, &sdhci_xilinx_pdata);
}

static int __devexit sdhci_xilinx_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_of_match[] = {
	{ .compatible = "generic-sdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_of_match);

static struct platform_driver sdhci_of_driver = {
	.driver = {
		.name = "generic-sdhci",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_of_match,
	},
	.probe = sdhci_xilinx_probe,
	.remove = __devexit_p(sdhci_xilinx_remove),
};

module_platform_driver(sdhci_of_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface OF driver");
MODULE_AUTHOR("Michal Simek <monstr@monstr.eu>");
MODULE_LICENSE("GPL");
