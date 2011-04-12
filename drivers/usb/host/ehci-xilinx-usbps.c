/*
 * Xilinx PS USB Host Controller Driver.
 *
 * Copyright (C) 2011 Xilinx, Inc.
 *
 * This file is based on ehci-fsl.c file with few minor modifications
 * to support Xilinx PS USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/xilinx_devices.h>
#include <linux/usb/otg.h>
#include <linux/usb/xilinx_usbps_otg.h>

#include "ehci-xilinx-usbps.h"

#ifdef CONFIG_USB_XUSBPS_OTG
/********************************************************************
 * OTG related functions
 ********************************************************************/
static int ehci_xusbps_reinit(struct ehci_hcd *ehci);

/* This connection event is useful when a OTG test device is connected.
   In that case, the device connect notify event will not be generated
   since the device will be suspended before complete enumeration.
*/
static int ehci_xusbps_update_device(struct usb_hcd *hcd, struct usb_device
		*udev)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct xusbps_otg *xotg = xceiv_to_xotg(ehci->transceiver);

	if (udev->portnum == hcd->self.otg_port) {
		/* HNP test device */
		if ((le16_to_cpu(udev->descriptor.idVendor) == 0x1a0a &&
			le16_to_cpu(udev->descriptor.idProduct) == 0xbadd)) {
			if (xotg->otg.default_a == 1)
				xotg->hsm.b_conn = 1;
			else
				xotg->hsm.a_conn = 1;
			xusbps_update_transceiver();
		}
	}
	return 0;
}

static void ehci_xusbps_start_hnp(struct ehci_hcd *ehci)
{
	const unsigned	port = ehci_to_hcd(ehci)->self.otg_port - 1;
	unsigned long	flags;
	u32 portsc;

	local_irq_save(flags);
	portsc = ehci_readl(ehci, &ehci->regs->port_status[port]);
	portsc |= PORT_SUSPEND;
	ehci_writel(ehci, portsc, &ehci->regs->port_status[port]);
	local_irq_restore(flags);

	otg_start_hnp(ehci->transceiver);
}

static int ehci_xusbps_otg_start_host(struct otg_transceiver  *otg)
{
	struct usb_hcd		*hcd = bus_to_hcd(otg->host);
	struct xusbps_otg *xotg =
			xceiv_to_xotg(hcd_to_ehci(hcd)->transceiver);

	usb_add_hcd(hcd, xotg->irq, IRQF_SHARED | IRQF_DISABLED);
	return 0;
}

static int ehci_xusbps_otg_stop_host(struct otg_transceiver  *otg)
{
	struct usb_hcd		*hcd = bus_to_hcd(otg->host);

	usb_remove_hcd(hcd);
	return 0;
}
#endif

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_xusbps_probe - initialize XUSBPS-based HCDs
 * @driver: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
static int usb_hcd_xusbps_probe(const struct hc_driver *driver,
			     struct platform_device *pdev)
{
	struct xusbps_usb2_platform_data *pdata;
	struct usb_hcd *hcd;
	int irq;
	int retval;
#ifdef CONFIG_USB_XUSBPS_OTG
	struct xusbps_otg *xotg;
	struct ehci_hcd *ehci;
#endif

	pr_debug("initializing XUSBPS-SOC USB Controller\n");

	/* Need platform data for setup */
	pdata = (struct xusbps_usb2_platform_data *)pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev,
			"No platform data for %s.\n", dev_name(&pdev->dev));
		return -ENODEV;
	}

	/*
	 * This is a host mode driver, verify that we're supposed to be
	 * in host mode.
	 */
	if (!((pdata->operating_mode == XUSBPS_USB2_DR_HOST) ||
	      (pdata->operating_mode == XUSBPS_USB2_MPH_HOST) ||
	      (pdata->operating_mode == XUSBPS_USB2_DR_OTG))) {
		dev_err(&pdev->dev, "Non Host Mode configured for %s. Wrong \
				driver linked.\n", dev_name(&pdev->dev));
		return -ENODEV;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	irq = pdata->irq;
	hcd->regs = pdata->regs;

	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto err2;
	}

	if (pdata->otg)
		hcd->self.otg_port = 1;
	/*
	 * do platform specific init: check the clock, grab/config pins, etc.
	 */
	if (pdata->init && pdata->init(pdev)) {
		retval = -ENODEV;
		goto err2;
	}

#ifdef CONFIG_USB_XUSBPS_OTG
	ehci = hcd_to_ehci(hcd);
	if (pdata->otg) {
		ehci->transceiver = pdata->otg;
		retval = otg_set_host(ehci->transceiver,
				&ehci_to_hcd(ehci)->self);
		if (retval)
			return retval;
		xotg = xceiv_to_xotg(ehci->transceiver);
		ehci->start_hnp = ehci_xusbps_start_hnp;
		xotg->start_host = ehci_xusbps_otg_start_host;
		xotg->stop_host = ehci_xusbps_otg_stop_host;
		/* inform otg driver about host driver */
		xusbps_update_transceiver();
	} else {
		retval = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
		if (retval != 0)
			goto err2;
	}
#else
	/* Don't need to set host mode here. It will be done by tdi_reset() */
	retval = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (retval != 0)
		goto err2;
#endif
	return retval;

err2:
	usb_put_hcd(hcd);
err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", dev_name(&pdev->dev), retval);
	if (pdata->exit)
		pdata->exit(pdev);

	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_xusbps_remove - shutdown processing for XUSBPS-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_xusbps_probe().
 *
 */
static void usb_hcd_xusbps_remove(struct usb_hcd *hcd,
			       struct platform_device *pdev)
{
	struct xusbps_usb2_platform_data *pdata = pdev->dev.platform_data;

	usb_remove_hcd(hcd);

	/*
	 * do platform specific un-initialization:
	 * release iomux pins, disable clock, etc.
	 */
	if (pdata->exit)
		pdata->exit(pdev);
	usb_put_hcd(hcd);
}

static void ehci_xusbps_setup_phy(struct ehci_hcd *ehci,
			       enum xusbps_usb2_phy_modes phy_mode,
			       unsigned int port_offset)
{
	u32 portsc;

	portsc = ehci_readl(ehci, &ehci->regs->port_status[port_offset]);
	portsc &= ~(PORT_PTS_MSK | PORT_PTS_PTW);

	switch (phy_mode) {
	case XUSBPS_USB2_PHY_ULPI:
		portsc |= PORT_PTS_ULPI;
		break;
	case XUSBPS_USB2_PHY_SERIAL:
		portsc |= PORT_PTS_SERIAL;
		break;
	case XUSBPS_USB2_PHY_UTMI_WIDE:
		portsc |= PORT_PTS_PTW;
		/* fall through */
	case XUSBPS_USB2_PHY_UTMI:
		portsc |= PORT_PTS_UTMI;
		break;
	case XUSBPS_USB2_PHY_NONE:
		break;
	}
	ehci_writel(ehci, portsc, &ehci->regs->port_status[port_offset]);
}

static void ehci_xusbps_usb_setup(struct ehci_hcd *ehci)
{
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
	struct xusbps_usb2_platform_data *pdata;

	pdata = hcd->self.controller->platform_data;

	if ((pdata->operating_mode == XUSBPS_USB2_DR_HOST) ||
			(pdata->operating_mode == XUSBPS_USB2_DR_OTG))
		ehci_xusbps_setup_phy(ehci, pdata->phy_mode, 0);

	if (pdata->operating_mode == XUSBPS_USB2_MPH_HOST) {
		if (pdata->port_enables & XUSBPS_USB2_PORT0_ENABLED)
			ehci_xusbps_setup_phy(ehci, pdata->phy_mode, 0);
		if (pdata->port_enables & XUSBPS_USB2_PORT1_ENABLED)
			ehci_xusbps_setup_phy(ehci, pdata->phy_mode, 1);
	}
}

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_xusbps_reinit(struct ehci_hcd *ehci)
{
	ehci_xusbps_usb_setup(ehci);
#ifdef CONFIG_USB_XUSBPS_OTG
	/* Don't turn off port power in OTG mode */
	if (!ehci->transceiver)
#endif
		ehci_port_power(ehci, 0);

	return 0;
}

struct ehci_xusbps {
	struct ehci_hcd	ehci;

#ifdef CONFIG_PM
	/* Saved USB PHY settings, need to restore after deep sleep. */
	u32 usb_ctrl;
#endif
};

/* called during probe() after chip reset completes */
static int ehci_xusbps_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
	    HC_LENGTH(ehci_readl(ehci, &ehci->caps->hc_capbase));
	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	hcd->has_tt = 1;

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	retval = ehci_xusbps_reinit(ehci);
	return retval;
}

#ifdef CONFIG_PM

static int ehci_xusbps_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	ehci_prepare_ports_for_controller_suspend(hcd_to_ehci(hcd),
			device_may_wakeup(dev));

	return 0;
}

static int ehci_xusbps_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	ehci_prepare_ports_for_controller_resume(ehci);

	usb_root_hub_lost_power(hcd->self.root_hub);

	ehci_reset(ehci);
	ehci_xusbps_reinit(ehci);

	return 0;
}

static int ehci_xusbps_drv_restore(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_root_hub_lost_power(hcd->self.root_hub);
	return 0;
}

static struct dev_pm_ops ehci_xusbps_pm_ops = {
	.suspend = ehci_xusbps_drv_suspend,
	.resume = ehci_xusbps_drv_resume,
	.restore = ehci_xusbps_drv_restore,
};

#define EHCI_XUSBPS_PM_OPS		(&ehci_xusbps_pm_ops)
#else
#define EHCI_XUSBPS_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct hc_driver ehci_xusbps_hc_driver = {
	.description = hcd_name,
	.product_desc = "Xilinx PS USB EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_xusbps),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_xusbps_setup,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
#ifdef CONFIG_USB_XUSBPS_OTG
	.update_device = ehci_xusbps_update_device,
#endif
};

static int ehci_xusbps_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	/* FIXME we only want one one probe() not two */
	return usb_hcd_xusbps_probe(&ehci_xusbps_hc_driver, pdev);
}

static int ehci_xusbps_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	/* FIXME we only want one one remove() not two */
	usb_hcd_xusbps_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("platform:xusbps-ehci");

static struct platform_driver ehci_xusbps_driver = {
	.probe = ehci_xusbps_drv_probe,
	.remove = ehci_xusbps_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		.name = "xusbps-ehci",
		.pm = EHCI_XUSBPS_PM_OPS,
	},
};
