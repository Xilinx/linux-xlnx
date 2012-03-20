/*
 * Based on sdhci-of-core.c
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

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mmc/host.h>
#include "sdhci.h"

struct sdhci_of_data {
	unsigned int quirks;
	struct sdhci_ops ops;
};

struct sdhci_of_host {
	unsigned int clock;
	u16 xfer_mode_shadow;
};

static unsigned int xilinx_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_of_host *of_host = sdhci_priv(host);
	return of_host->clock;
}

struct sdhci_of_data sdhci_data = { 
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
	.ops = {
		.get_max_clock = xilinx_of_get_max_clock,
	},
};


#ifdef CONFIG_PM

static int sdhci_of_suspend(struct platform_device *ofdev, pm_message_t state)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	return mmc_suspend_host(host->mmc);
}

static int sdhci_of_resume(struct platform_device *ofdev)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	return mmc_resume_host(host->mmc);
}

#else

#define sdhci_of_suspend NULL
#define sdhci_of_resume NULL

#endif

static bool __devinit sdhci_of_wp_inverted(struct device_node *np)
{
	if (of_get_property(np, "sdhci,wp-inverted", NULL))
		return true;

	return false;
}

static const struct of_device_id sdhci_of_match[];
static int __devinit sdhci_of_probe(struct platform_device *ofdev)
{
	const struct of_device_id *match;
	struct device_node *np = ofdev->dev.of_node;
	struct sdhci_of_data *sdhci_of_data;
	struct sdhci_host *host;
	struct sdhci_of_host *of_host;
	const __be32 *clk;
	int size;
	int ret;

	match = of_match_device(sdhci_of_match, &ofdev->dev);
	if (!match)
		return -EINVAL;
	sdhci_of_data = match->data;

	if (!of_device_is_available(np))
		return -ENODEV;

	host = sdhci_alloc_host(&ofdev->dev, sizeof(*of_host));
	if (IS_ERR(host))
		return -ENOMEM;

	of_host = sdhci_priv(host);
	dev_set_drvdata(&ofdev->dev, host);

	host->ioaddr = of_iomap(np, 0);
	if (!host->ioaddr) {
		ret = -ENOMEM;
		goto err_addr_map;
	}

	host->irq = irq_of_parse_and_map(np, 0);
	if (!host->irq) {
		ret = -EINVAL;
		goto err_no_irq;
	}

	host->hw_name = dev_name(&ofdev->dev);
	if (sdhci_of_data) {
		host->quirks = sdhci_of_data->quirks;
		host->ops = &sdhci_of_data->ops;
	}

	if (of_get_property(np, "sdhci,auto-cmd12", NULL))
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;


	if (of_get_property(np, "sdhci,1-bit-only", NULL))
		host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;

	if (sdhci_of_wp_inverted(np))
		host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;

	clk = of_get_property(np, "clock-frequency", &size);
	if (clk && size == sizeof(*clk) && *clk)
		of_host->clock = be32_to_cpup(clk);

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	irq_dispose_mapping(host->irq);
err_no_irq:
	iounmap(host->ioaddr);
err_addr_map:
	sdhci_free_host(host);
	return ret;
}

static int __devexit sdhci_of_remove(struct platform_device *ofdev)
{
	struct sdhci_host *host = dev_get_drvdata(&ofdev->dev);

	sdhci_remove_host(host, 0);
	sdhci_free_host(host);
	irq_dispose_mapping(host->irq);
	iounmap(host->ioaddr);
	return 0;
}

static const struct of_device_id sdhci_of_match[] = {
	{ .compatible = "xlnx,ps7-sdhci-1.00.a", .data = &sdhci_data, },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_of_match);

static struct platform_driver sdhci_of_driver = {
	.driver = {
		.name = "sdhci-xilinx-ps",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_of_match,
	},
	.probe = sdhci_of_probe,
	.remove = __devexit_p(sdhci_of_remove),
	.suspend = sdhci_of_suspend,
	.resume	= sdhci_of_resume,
};

static int __init sdhci_of_init(void)
{
	return platform_driver_register(&sdhci_of_driver);
}
module_init(sdhci_of_init);

static void __exit sdhci_of_exit(void)
{
	platform_driver_unregister(&sdhci_of_driver);
}
module_exit(sdhci_of_exit);

MODULE_DESCRIPTION("Xilinx SDHCI driver");
MODULE_LICENSE("GPL");
