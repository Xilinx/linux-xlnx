/**
 * host.c - DesignWare USB3 DRD Controller Host Glue
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/usb/xhci_pdriver.h>

#include "core.h"

void dwc3_host_wakeup_capable(struct device *dev, bool wakeup)
{
	dwc3_simple_wakeup_capable(dev, wakeup);
}
EXPORT_SYMBOL(dwc3_host_wakeup_capable);

int dwc3_host_init(struct dwc3 *dwc)
{
	struct property_entry	props[3];
	struct platform_device	*xhci;
	int			ret, irq;
	unsigned int		index = 0;
	struct resource		*res;
	struct platform_device	*dwc3_pdev = to_platform_device(dwc->dev);

	irq = platform_get_irq_byname(dwc3_pdev, "host");
	if (irq == -EPROBE_DEFER)
		return irq;

	if (irq <= 0) {
		irq = platform_get_irq_byname(dwc3_pdev, "dwc_usb3");
		if (irq == -EPROBE_DEFER)
			return irq;

		if (irq <= 0) {
			irq = platform_get_irq(dwc3_pdev, 0);
			if (irq <= 0) {
				if (irq != -EPROBE_DEFER) {
					dev_err(dwc->dev,
						"missing host IRQ\n");
				}
				if (!irq)
					irq = -EINVAL;
				return irq;
			} else {
				res = platform_get_resource(dwc3_pdev,
							    IORESOURCE_IRQ, 0);
			}
		} else {
			res = platform_get_resource_byname(dwc3_pdev,
							   IORESOURCE_IRQ,
							   "dwc_usb3");
		}

	} else {
		res = platform_get_resource_byname(dwc3_pdev, IORESOURCE_IRQ,
						   "host");
	}

	dwc->xhci_resources[1].start = irq;
	dwc->xhci_resources[1].end = irq;
	dwc->xhci_resources[1].flags = res->flags;
	dwc->xhci_resources[1].name = res->name;

	xhci = platform_device_alloc("xhci-hcd", PLATFORM_DEVID_AUTO);
	if (!xhci) {
		dev_err(dwc->dev, "couldn't allocate xHCI device\n");
		return -ENOMEM;
	}

	xhci->dev.parent	= dwc->dev;

	if (IS_ENABLED(CONFIG_OF)) {
		/* Let OF configure xhci dev's DMA configuration */
		of_dma_configure(&xhci->dev, dwc->dev->of_node);
	} else {
		dma_set_coherent_mask(&xhci->dev, dwc->dev->coherent_dma_mask);

		xhci->dev.dma_mask	= dwc->dev->dma_mask;
		xhci->dev.dma_parms	= dwc->dev->dma_parms;
		xhci->dev.archdata	= dwc->dev->archdata;
	}

	dwc->xhci = xhci;

	ret = platform_device_add_resources(xhci, dwc->xhci_resources,
						DWC3_XHCI_RESOURCES_NUM);
	if (ret) {
		dev_err(dwc->dev, "couldn't add resources to xHCI device\n");
		goto err1;
	}

	memset(props, 0, sizeof(struct property_entry) * ARRAY_SIZE(props));

	if (dwc->usb3_lpm_capable)
		props[index++].name = "usb3-lpm-capable";

	if (device_property_read_bool(&dwc3_pdev->dev,
					"snps,xhci-stream-quirk"))
		props[index++].name = "xhci-stream-quirk";

	if (index > 0) {
		ret = platform_device_add_properties(xhci, props);
		if (ret) {
			dev_err(dwc->dev, "failed to add properties to xHCI\n");
			goto err1;
		}
	}

	phy_create_lookup(dwc->usb2_generic_phy, "usb2-phy",
			  dev_name(&xhci->dev));
	phy_create_lookup(dwc->usb3_generic_phy, "usb3-phy",
			  dev_name(&xhci->dev));

	if (dwc->dr_mode == USB_DR_MODE_OTG) {
		struct usb_phy *phy;
		phy = usb_get_phy(USB_PHY_TYPE_USB3);
		if (!IS_ERR(phy)) {
			if (phy && phy->otg)
				otg_set_host(phy->otg,
						(struct usb_bus *)(long)1);
			usb_put_phy(phy);
		}
	}

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(dwc->dev, "failed to register xHCI device\n");
		goto err2;
	}

	return 0;
err2:
	phy_remove_lookup(dwc->usb2_generic_phy, "usb2-phy",
			  dev_name(&xhci->dev));
	phy_remove_lookup(dwc->usb3_generic_phy, "usb3-phy",
			  dev_name(&xhci->dev));
err1:
	platform_device_put(xhci);
	return ret;
}

void dwc3_host_exit(struct dwc3 *dwc)
{
	phy_remove_lookup(dwc->usb2_generic_phy, "usb2-phy",
			  dev_name(&dwc->xhci->dev));
	phy_remove_lookup(dwc->usb3_generic_phy, "usb3-phy",
			  dev_name(&dwc->xhci->dev));
	platform_device_unregister(dwc->xhci);
}
