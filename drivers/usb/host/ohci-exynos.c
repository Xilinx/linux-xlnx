/*
 * SAMSUNG EXYNOS USB HOST OHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb/phy.h>
#include <linux/usb/samsung_usb_phy.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/otg.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI EXYNOS driver"

static const char hcd_name[] = "ohci-exynos";
static struct hc_driver __read_mostly exynos_ohci_hc_driver;

#define to_exynos_ohci(hcd) (struct exynos_ohci_hcd *)(hcd_to_ohci(hcd)->priv)

struct exynos_ohci_hcd {
	struct clk *clk;
	struct usb_phy *phy;
	struct usb_otg *otg;
};

static void exynos_ohci_phy_enable(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (exynos_ohci->phy)
		usb_phy_init(exynos_ohci->phy);
}

static void exynos_ohci_phy_disable(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (exynos_ohci->phy)
		usb_phy_shutdown(exynos_ohci->phy);
}

static int exynos_ohci_probe(struct platform_device *pdev)
{
	struct exynos_ohci_hcd *exynos_ohci;
	struct usb_hcd *hcd;
	struct resource *res;
	struct usb_phy *phy;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	hcd = usb_create_hcd(&exynos_ohci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	exynos_ohci = to_exynos_ohci(hcd);

	if (of_device_is_compatible(pdev->dev.of_node,
					"samsung,exynos5440-ohci"))
		goto skip_phy;

	phy = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(phy)) {
		usb_put_hcd(hcd);
		dev_warn(&pdev->dev, "no platform data or transceiver defined\n");
		return -EPROBE_DEFER;
	} else {
		exynos_ohci->phy = phy;
		exynos_ohci->otg = phy->otg;
	}

skip_phy:
	exynos_ohci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ohci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exynos_ohci->clk);
	if (err)
		goto fail_clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, res->start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg, &hcd->self);

	platform_set_drvdata(pdev, hcd);

	exynos_ohci_phy_enable(pdev);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	return 0;

fail_add_hcd:
	exynos_ohci_phy_disable(pdev);
fail_io:
	clk_disable_unprepare(exynos_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exynos_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	usb_remove_hcd(hcd);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg, &hcd->self);

	exynos_ohci_phy_disable(pdev);

	clk_disable_unprepare(exynos_ohci->clk);

	usb_put_hcd(hcd);

	return 0;
}

static void exynos_ohci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int exynos_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct platform_device *pdev = to_platform_device(dev);
	unsigned long flags;
	int rc = 0;

	/*
	 * Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (ohci->rh_state != OHCI_RH_SUSPENDED &&
			ohci->rh_state != OHCI_RH_HALTED) {
		rc = -EINVAL;
		goto fail;
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg, &hcd->self);

	exynos_ohci_phy_disable(pdev);

	clk_disable_unprepare(exynos_ohci->clk);

fail:
	spin_unlock_irqrestore(&ohci->lock, flags);

	return rc;
}

static int exynos_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd			= dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci	= to_exynos_ohci(hcd);
	struct platform_device *pdev		= to_platform_device(dev);

	clk_prepare_enable(exynos_ohci->clk);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg, &hcd->self);

	exynos_ohci_phy_enable(pdev);

	ohci_resume(hcd, false);

	return 0;
}
#else
#define exynos_ohci_suspend	NULL
#define exynos_ohci_resume	NULL
#endif

static const struct ohci_driver_overrides exynos_overrides __initconst = {
	.extra_priv_size =	sizeof(struct exynos_ohci_hcd),
};

static const struct dev_pm_ops exynos_ohci_pm_ops = {
	.suspend	= exynos_ohci_suspend,
	.resume		= exynos_ohci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ohci_match[] = {
	{ .compatible = "samsung,exynos4210-ohci" },
	{ .compatible = "samsung,exynos5440-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ohci_match);
#endif

static struct platform_driver exynos_ohci_driver = {
	.probe		= exynos_ohci_probe,
	.remove		= exynos_ohci_remove,
	.shutdown	= exynos_ohci_shutdown,
	.driver = {
		.name	= "exynos-ohci",
		.owner	= THIS_MODULE,
		.pm	= &exynos_ohci_pm_ops,
		.of_match_table	= of_match_ptr(exynos_ohci_match),
	}
};
static int __init ohci_exynos_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);
	ohci_init_driver(&exynos_ohci_hc_driver, &exynos_overrides);
	return platform_driver_register(&exynos_ohci_driver);
}
module_init(ohci_exynos_init);

static void __exit ohci_exynos_cleanup(void)
{
	platform_driver_unregister(&exynos_ohci_driver);
}
module_exit(ohci_exynos_cleanup);

MODULE_ALIAS("platform:exynos-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_LICENSE("GPL v2");
