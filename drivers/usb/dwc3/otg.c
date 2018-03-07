/**
 * otg.c - DesignWare USB3 DRD Controller OTG file
 *
 * Copyright (C) 2016 Xilinx, Inc. All rights reserved.
 *
 * Author:  Manish Narani <mnarani@xilinx.com>
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/sysfs.h>

#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>

#include <../drivers/usb/host/xhci.h>
#include "platform_data.h"
#include "core.h"
#include "gadget.h"
#include "io.h"
#include "otg.h"

#include <linux/ulpi/regs.h>
#include <linux/ulpi/driver.h>
#include "debug.h"

/* Print the hardware registers' value for debugging purpose */
static void print_debug_regs(struct dwc3_otg *otg)
{
	u32 gctl = otg_read(otg, DWC3_GCTL);
	u32 gsts = otg_read(otg, DWC3_GSTS);
	u32 gdbgltssm = otg_read(otg, DWC3_GDBGLTSSM);
	u32 gusb2phycfg0 = otg_read(otg, DWC3_GUSB2PHYCFG(0));
	u32 gusb3pipectl0 = otg_read(otg, DWC3_GUSB3PIPECTL(0));
	u32 dcfg = otg_read(otg, DWC3_DCFG);
	u32 dctl = otg_read(otg, DWC3_DCTL);
	u32 dsts = otg_read(otg, DWC3_DSTS);
	u32 ocfg = otg_read(otg, OCFG);
	u32 octl = otg_read(otg, OCTL);
	u32 oevt = otg_read(otg, OEVT);
	u32 oevten = otg_read(otg, OEVTEN);
	u32 osts = otg_read(otg, OSTS);

	otg_info(otg, "gctl = %08x\n", gctl);
	otg_info(otg, "gsts = %08x\n", gsts);
	otg_info(otg, "gdbgltssm = %08x\n", gdbgltssm);
	otg_info(otg, "gusb2phycfg0 = %08x\n", gusb2phycfg0);
	otg_info(otg, "gusb3pipectl0 = %08x\n", gusb3pipectl0);
	otg_info(otg, "dcfg = %08x\n", dcfg);
	otg_info(otg, "dctl = %08x\n", dctl);
	otg_info(otg, "dsts = %08x\n", dsts);
	otg_info(otg, "ocfg = %08x\n", ocfg);
	otg_info(otg, "octl = %08x\n", octl);
	otg_info(otg, "oevt = %08x\n", oevt);
	otg_info(otg, "oevten = %08x\n", oevten);
	otg_info(otg, "osts = %08x\n", osts);
}

/* Check whether the hardware supports HNP or not */
static int hnp_capable(struct dwc3_otg *otg)
{
	if (otg->hwparams6 & GHWPARAMS6_HNP_SUPPORT_ENABLED)
		return 1;
	return 0;
}

/* Check whether the hardware supports SRP or not */
static int srp_capable(struct dwc3_otg *otg)
{
	if (otg->hwparams6 & GHWPARAMS6_SRP_SUPPORT_ENABLED)
		return 1;
	return 0;
}

/* Wakeup main thread to execute the OTG flow after an event */
static void wakeup_main_thread(struct dwc3_otg *otg)
{
	if (!otg->main_thread)
		return;

	otg_vdbg(otg, "\n");
	/* Tell the main thread that something has happened */
	otg->main_wakeup_needed = 1;
	wake_up_interruptible(&otg->main_wq);
}

/* Sleep main thread for 'msecs' to wait for an event to occur */
static int sleep_main_thread_timeout(struct dwc3_otg *otg, int msecs)
{
	signed long jiffies;
	int rc = msecs;

	if (signal_pending(current)) {
		otg_dbg(otg, "Main thread signal pending\n");
		rc = -EINTR;
		goto done;
	}
	if (otg->main_wakeup_needed) {
		otg_dbg(otg, "Main thread wakeup needed\n");
		rc = msecs;
		goto done;
	}

	jiffies = msecs_to_jiffies(msecs);
	rc = wait_event_freezable_timeout(otg->main_wq,
					  otg->main_wakeup_needed,
					  jiffies);

	if (rc > 0)
		rc = jiffies_to_msecs(rc);

done:
	otg->main_wakeup_needed = 0;
	return rc;
}

/* Sleep main thread to wait for an event to occur */
static int sleep_main_thread(struct dwc3_otg *otg)
{
	int rc;

	do {
		rc = sleep_main_thread_timeout(otg, 5000);
	} while (rc == 0);

	return rc;
}

static void get_events(struct dwc3_otg *otg, u32 *otg_events, u32 *user_events)
{
	unsigned long flags;

	spin_lock_irqsave(&otg->lock, flags);

	if (otg_events)
		*otg_events = otg->otg_events;

	if (user_events)
		*user_events = otg->user_events;

	spin_unlock_irqrestore(&otg->lock, flags);
}

static void get_and_clear_events(struct dwc3_otg *otg, u32 *otg_events,
		u32 *user_events)
{
	unsigned long flags;

	spin_lock_irqsave(&otg->lock, flags);

	if (otg_events)
		*otg_events = otg->otg_events;

	if (user_events)
		*user_events = otg->user_events;

	otg->otg_events = 0;
	otg->user_events = 0;

	spin_unlock_irqrestore(&otg->lock, flags);
}

static int check_event(struct dwc3_otg *otg, u32 otg_mask, u32 user_mask)
{
	u32 otg_events;
	u32 user_events;

	get_events(otg, &otg_events, &user_events);
	if ((otg_events & otg_mask) || (user_events & user_mask)) {
		otg_dbg(otg, "Event occurred: otg_events=%x, otg_mask=%x, \
				user_events=%x, user_mask=%x\n", otg_events,
				otg_mask, user_events, user_mask);
		return 1;
	}

	return 0;
}

static int sleep_until_event(struct dwc3_otg *otg, u32 otg_mask, u32 user_mask,
		u32 *otg_events, u32 *user_events, int timeout)
{
	int rc;

	/* Enable the events */
	if (otg_mask)
		otg_write(otg, OEVTEN, otg_mask);

	/* Wait until it occurs, or timeout, or interrupt. */
	if (timeout) {
		otg_vdbg(otg, "Waiting for event (timeout=%d)...\n", timeout);
		rc = sleep_main_thread_until_condition_timeout(otg,
			check_event(otg, otg_mask, user_mask), timeout);
	} else {
		otg_vdbg(otg, "Waiting for event (no timeout)...\n");
		rc = sleep_main_thread_until_condition(otg,
			check_event(otg, otg_mask, user_mask));
	}

	/* Disable the events */
	otg_write(otg, OEVTEN, 0);

	otg_vdbg(otg, "Woke up rc=%d\n", rc);
	if (rc >= 0)
		get_and_clear_events(otg, otg_events, user_events);

	return rc;
}

static void set_capabilities(struct dwc3_otg *otg)
{
	u32 ocfg = 0;

	otg_dbg(otg, "\n");
	if (srp_capable(otg))
		ocfg |= OCFG_SRP_CAP;

	if (hnp_capable(otg))
		ocfg |= OCFG_HNP_CAP;

	otg_write(otg, OCFG, ocfg);

	otg_dbg(otg, "Enabled SRP and HNP capabilities in OCFG\n");
}

static int otg3_handshake(struct dwc3_otg *otg, u32 reg, u32 mask, u32 done,
		u32 msec)
{
	u32 result;
	u32 usec = msec * 1000;

	otg_vdbg(otg, "reg=%08x, mask=%08x, value=%08x\n", reg, mask, done);
	do {
		result = otg_read(otg, reg);
		if ((result & mask) == done)
			return 1;
		udelay(1);
		usec -= 1;
	} while (usec > 0);

	return 0;
}

static int reset_port(struct dwc3_otg *otg)
{
	otg_dbg(otg, "\n");
	if (!otg->otg.host)
		return -ENODEV;
	return usb_bus_start_enum(otg->otg.host, 1);
}

static int set_peri_mode(struct dwc3_otg *otg, int mode)
{
	u32 octl;

	/* Set peri_mode */
	octl = otg_read(otg, OCTL);
	if (mode)
		octl |= OCTL_PERI_MODE;
	else
		octl &= ~OCTL_PERI_MODE;

	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "set OCTL PERI_MODE = %d in OCTL\n", mode);

	if (mode)
		return otg3_handshake(otg, OSTS, OSTS_PERIP_MODE,
				OSTS_PERIP_MODE, 100);
	else
		return otg3_handshake(otg, OSTS, OSTS_PERIP_MODE, 0, 100);

	msleep(20);
}

static int start_host(struct dwc3_otg *otg)
{
	int ret = -ENODEV;
	int flg;
	u32 octl;
	u32 osts;
	u32 dctl;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	otg_dbg(otg, "\n");

	if (!otg->otg.host)
		return -ENODEV;

	dctl = otg_read(otg, DCTL);
	if (dctl & DWC3_DCTL_RUN_STOP) {
		otg_dbg(otg, "Disabling the RUN/STOP bit\n");
		dctl &= ~DWC3_DCTL_RUN_STOP;
		otg_write(otg, DCTL, dctl);
	}

	if (!set_peri_mode(otg, PERI_MODE_HOST)) {
		otg_err(otg, "Failed to start host\n");
		return -EINVAL;
	}

	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);
	otg_dbg(otg, "hcd=%p xhci=%p\n", hcd, xhci);

	if (otg->host_started) {
		otg_info(otg, "Host already started\n");
		goto skip;
	}

	/* Start host driver */

	*(struct xhci_hcd **)hcd->hcd_priv = xhci;
	ret = usb_add_hcd(hcd, otg->hcd_irq, IRQF_SHARED);
	if (ret) {
		otg_err(otg, "%s: failed to start primary hcd, ret=%d\n",
			__func__, ret);
		return ret;
	}

	*(struct xhci_hcd **)xhci->shared_hcd->hcd_priv = xhci;
	if (xhci->shared_hcd) {
		ret = usb_add_hcd(xhci->shared_hcd, otg->hcd_irq, IRQF_SHARED);
		if (ret) {
			otg_err(otg,
				"%s: failed to start secondary hcd, ret=%d\n",
				__func__, ret);
			usb_remove_hcd(hcd);
			return ret;
		}
	}

	otg->host_started = 1;
skip:
	hcd->self.otg_port = 1;
	if (xhci->shared_hcd)
		xhci->shared_hcd->self.otg_port = 1;

	set_capabilities(otg);

	/* Power the port only for A-host */
	if (otg->otg.state == OTG_STATE_A_WAIT_VRISE) {
		/* Spin on xhciPrtPwr bit until it becomes 1 */
		osts = otg_read(otg, OSTS);
		flg = otg3_handshake(otg, OSTS,
				OSTS_XHCI_PRT_PWR,
				OSTS_XHCI_PRT_PWR,
				1000);
		if (flg) {
			otg_dbg(otg, "Port is powered by xhci-hcd\n");
			/* Set port power control bit */
			octl = otg_read(otg, OCTL);
			octl |= OCTL_PRT_PWR_CTL;
			otg_write(otg, OCTL, octl);
		} else {
			otg_dbg(otg, "Port is not powered by xhci-hcd\n");
		}
	}

	return ret;
}

static int stop_host(struct dwc3_otg *otg)
{
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	otg_dbg(otg, "\n");

	if (!otg->host_started) {
		otg_info(otg, "Host already stopped\n");
		return 1;
	}

	if (!otg->otg.host)
		return -ENODEV;

	otg_dbg(otg, "%s: turn off host %s\n",
		__func__, otg->otg.host->bus_name);

	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);

	if (xhci->shared_hcd)
		usb_remove_hcd(xhci->shared_hcd);
	usb_remove_hcd(hcd);

	otg->host_started = 0;
	otg->dev_enum = 0;
	return 0;
}

int dwc3_otg_host_release(struct usb_hcd *hcd)
{
	struct usb_bus *bus;
	struct usb_device *rh;
	struct usb_device *udev;

	if (!hcd)
		return -EINVAL;

	bus = &hcd->self;
	if (!bus->otg_port)
		return 0;

	rh = bus->root_hub;
	udev = usb_hub_find_child(rh, bus->otg_port);
	if (!udev)
		return 0;

	if (udev->config && udev->parent == udev->bus->root_hub) {
		struct usb_otg20_descriptor *desc;

		if (__usb_get_extra_descriptor(udev->rawdescriptors[0],
				le16_to_cpu(udev->config[0].desc.wTotalLength),
				USB_DT_OTG, (void **) &desc) == 0) {
			int err;

			dev_info(&udev->dev, "found OTG descriptor\n");
			if ((desc->bcdOTG >= 0x0200) &&
			    (udev->speed == USB_SPEED_HIGH)) {
				err = usb_control_msg(udev,
						usb_sndctrlpipe(udev, 0),
						USB_REQ_SET_FEATURE, 0,
						USB_DEVICE_TEST_MODE,
						7 << 8,
						NULL, 0, USB_CTRL_SET_TIMEOUT);
				if (err < 0) {
					dev_info(&udev->dev,
						"can't initiate HNP from host: %d\n",
						err);
					return -1;
				}
			}
		} else {
			dev_info(&udev->dev, "didn't find OTG descriptor\n");
		}
	} else {
		dev_info(&udev->dev,
			 "udev->config NULL or udev->parent != udev->bus->root_hub\n");
	}

	return 0;
}

/* Sends the host release set feature request */
static void host_release(struct dwc3_otg *otg)
{
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	otg_dbg(otg, "\n");
	if (!otg->otg.host)
		return;
	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);
	dwc3_otg_host_release(hcd);
	if (xhci->shared_hcd)
		dwc3_otg_host_release(xhci->shared_hcd);
}

static void dwc3_otg_setup_event_buffers(struct dwc3_otg *otg)
{
	if (dwc3_readl(otg->dwc->regs, DWC3_GEVNTADRLO(0)) == 0x0) {

		otg_dbg(otg, "setting up event buffers\n");
		dwc3_event_buffers_setup(otg->dwc);
	}

}

static void start_peripheral(struct dwc3_otg *otg)
{
	struct usb_gadget *gadget = otg->otg.gadget;
	struct dwc3 *dwc = otg->dwc;

	otg_dbg(otg, "\n");
	if (!gadget)
		return;

	if (!set_peri_mode(otg, PERI_MODE_PERIPHERAL))
		otg_err(otg, "Failed to set peripheral mode\n");

	if (otg->peripheral_started) {
		otg_info(otg, "Peripheral already started\n");
		return;
	}

	dwc3_otg_setup_event_buffers(otg);

	if (dwc->gadget_driver) {
		struct dwc3_ep		*dep;
		int			ret;

		spin_lock(&otg->lock);
		dep = dwc->eps[0];

		ret = __dwc3_gadget_ep_enable(dep, false, false);
		if (ret)
			goto err0;

		dep = dwc->eps[1];

		ret = __dwc3_gadget_ep_enable(dep, false, false);
		if (ret)
			goto err1;

		otg_dbg(otg, "enabled ep in gadget driver\n");
		/* begin to receive SETUP packets */
		dwc->ep0state = EP0_SETUP_PHASE;
		dwc3_ep0_out_start(dwc);

		otg_dbg(otg, "enabled irq\n");
		dwc3_gadget_enable_irq(dwc);

		otg_write(otg, DCTL, otg_read(otg, DCTL) | DCTL_RUN_STOP);
		otg_dbg(otg, "Setting DCTL_RUN_STOP to 1 in DCTL\n");
		spin_unlock(&otg->lock);
	}

	gadget->b_hnp_enable = 0;
	gadget->host_request_flag = 0;

	otg->peripheral_started = 1;

	msleep(20);

	return;
err1:
		__dwc3_gadget_ep_disable(dwc->eps[0]);

err0:
		return;
}

static void stop_peripheral(struct dwc3_otg *otg)
{
	struct usb_gadget *gadget = otg->otg.gadget;
	struct dwc3 *dwc = otg->dwc;

	otg_dbg(otg, "\n");

	if (!otg->peripheral_started) {
		otg_info(otg, "Peripheral already stopped\n");
		return;
	}

	if (!gadget)
		return;

	otg_dbg(otg, "disabled ep in gadget driver\n");
	spin_lock(&otg->lock);

	dwc3_gadget_disable_irq(dwc);
	__dwc3_gadget_ep_disable(dwc->eps[0]);
	__dwc3_gadget_ep_disable(dwc->eps[1]);

	spin_unlock(&otg->lock);

	otg->peripheral_started = 0;
	msleep(20);
}

static void set_b_host(struct dwc3_otg *otg, int val)
{
	otg->otg.host->is_b_host = val;
}

static enum usb_otg_state do_b_idle(struct dwc3_otg *otg);

static int init_b_device(struct dwc3_otg *otg)
{
	otg_dbg(otg, "\n");
	set_capabilities(otg);

	if (!set_peri_mode(otg, PERI_MODE_PERIPHERAL))
		otg_err(otg, "Failed to start peripheral\n");

	return do_b_idle(otg);
}

static int init_a_device(struct dwc3_otg *otg)
{
	otg_write(otg, OCFG, 0);
	otg_write(otg, OCTL, 0);

	otg_dbg(otg, "Write 0 to OCFG and OCTL\n");
	return OTG_STATE_A_IDLE;
}

static enum usb_otg_state do_connector_id_status(struct dwc3_otg *otg)
{
	enum usb_otg_state state;
	u32 osts;

	otg_dbg(otg, "\n");

	otg_write(otg, OCFG, 0);
	otg_write(otg, OEVTEN, 0);
	otg_write(otg, OEVT, 0xffffffff);
	otg_write(otg, OEVTEN, OEVT_CONN_ID_STS_CHNG_EVNT);

	msleep(60);

	osts = otg_read(otg, OSTS);
	if (!(osts & OSTS_CONN_ID_STS)) {
		otg_dbg(otg, "Connector ID is A\n");
		state = init_a_device(otg);
	} else {
		otg_dbg(otg, "Connector ID is B\n");
		stop_host(otg);
		state = init_b_device(otg);
	}

	/* TODO: This is a workaround for latest hibernation-enabled bitfiles
	 * which have problems before initializing SRP.
	 */
	msleep(50);

	return state;
}

static void reset_hw(struct dwc3_otg *otg)
{
	u32 temp;

	otg_dbg(otg, "\n");

	otg_write(otg, OEVTEN, 0);
	temp = otg_read(otg, OCTL);
	temp &= OCTL_PERI_MODE;
	otg_write(otg, OCTL, temp);
	temp = otg_read(otg, GCTL);
	temp |= GCTL_PRT_CAP_DIR_OTG << GCTL_PRT_CAP_DIR_SHIFT;
	otg_write(otg, GCTL, temp);
}

#define SRP_TIMEOUT			6000

static void start_srp(struct dwc3_otg *otg)
{
	u32 octl;

	octl = otg_read(otg, OCTL);
	octl |= OCTL_SES_REQ;
	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "set OCTL_SES_REQ in OCTL\n");
}

static void start_b_hnp(struct dwc3_otg *otg)
{
	u32 octl;

	octl = otg_read(otg, OCTL);
	octl |= OCTL_HNP_REQ | OCTL_DEV_SET_HNP_EN;
	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "set (OCTL_HNP_REQ | OCTL_DEV_SET_HNP_EN) in OCTL\n");
}

static void stop_b_hnp(struct dwc3_otg *otg)
{
	u32 octl;

	octl = otg_read(otg, OCTL);
	octl &= ~(OCTL_HNP_REQ | OCTL_DEV_SET_HNP_EN);
	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "Clear ~(OCTL_HNP_REQ | OCTL_DEV_SET_HNP_EN) in OCTL\n");
}

static void start_a_hnp(struct dwc3_otg *otg)
{
	u32 octl;

	octl = otg_read(otg, OCTL);
	octl |= OCTL_HST_SET_HNP_EN;
	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "set OCTL_HST_SET_HNP_EN in OCTL\n");
}

static void stop_a_hnp(struct dwc3_otg *otg)
{
	u32 octl;

	octl = otg_read(otg, OCTL);
	octl &= ~OCTL_HST_SET_HNP_EN;
	otg_write(otg, OCTL, octl);
	otg_dbg(otg, "clear OCTL_HST_SET_HNP_EN in OCTL\n");
}

static enum usb_otg_state do_a_hnp_init(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 otg_events = 0;

	otg_dbg(otg, "");
	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_A_DEV_HNP_CHNG_EVNT;

	start_a_hnp(otg);
	rc = 3000;

again:
	rc = sleep_until_event(otg,
			otg_mask, 0,
			&otg_events, NULL, rc);
	stop_a_hnp(otg);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;

	} else if (otg_events & OEVT_A_DEV_HNP_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_HNP_CHNG_EVNT\n");
		if (otg_events & OEVT_HST_NEG_SCS) {
			otg_dbg(otg, "A-HNP Success\n");
			return OTG_STATE_A_PERIPHERAL;

		} else {
			otg_dbg(otg, "A-HNP Failed\n");
			return OTG_STATE_A_WAIT_VFALL;
		}

	} else if (rc == 0) {
		otg_dbg(otg, "A-HNP Failed (Timed out)\n");
		return OTG_STATE_A_WAIT_VFALL;

	} else {
		goto again;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_a_host(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask;
	u32 otg_events = 0;
	u32 user_events = 0;

	otg_dbg(otg, "");

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_A_DEV_SESS_END_DET_EVNT;
	user_mask = USER_SRP_EVENT |
		USER_HNP_EVENT;

	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events, 0);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;

	} else if (otg_events & OEVT_A_DEV_SESS_END_DET_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_SESS_END_DET_EVNT\n");
		return OTG_STATE_A_WAIT_VFALL;

	} else if (user_events & USER_HNP_EVENT) {
		otg_dbg(otg, "USER_HNP_EVENT\n");
		return OTG_STATE_A_SUSPEND;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

#define A_WAIT_VFALL_TIMEOUT 1000

static enum usb_otg_state do_a_wait_vfall(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 otg_events = 0;

	otg_dbg(otg, "");

	otg_mask = OEVT_A_DEV_IDLE_EVNT;

	rc = A_WAIT_VFALL_TIMEOUT;
	rc = sleep_until_event(otg,
			otg_mask, 0,
			&otg_events, NULL, rc);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (otg_events & OEVT_A_DEV_IDLE_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_IDLE_EVNT\n");
		return OTG_STATE_A_IDLE;

	} else if (rc == 0) {
		otg_dbg(otg, "A_WAIT_VFALL_TIMEOUT\n");
		return OTG_STATE_A_IDLE;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;

}

#define A_WAIT_BCON_TIMEOUT 1000

static enum usb_otg_state do_a_wait_bconn(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 otg_events = 0;

	otg_dbg(otg, "");

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_A_DEV_SESS_END_DET_EVNT |
		OEVT_A_DEV_HOST_EVNT;

	rc = A_WAIT_BCON_TIMEOUT;
	rc = sleep_until_event(otg,
			otg_mask, 0,
			&otg_events, NULL, rc);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;

	} else if (otg_events & OEVT_A_DEV_SESS_END_DET_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_SESS_END_DET_EVNT\n");
		return OTG_STATE_A_WAIT_VFALL;

	} else if (otg_events & OEVT_A_DEV_HOST_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_HOST_EVNT\n");
		return OTG_STATE_A_HOST;

	} else if (rc == 0) {
		if (otg_read(otg, OCTL) & OCTL_PRT_PWR_CTL)
			return OTG_STATE_A_HOST;
		else
			return OTG_STATE_A_WAIT_VFALL;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

#define A_WAIT_VRISE_TIMEOUT 100

static enum usb_otg_state do_a_wait_vrise(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 otg_events = 0;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	otg_dbg(otg, "");
	set_b_host(otg, 0);
	start_host(otg);
	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);
	usb_kick_hub_wq(hcd->self.root_hub);
	if (xhci->shared_hcd)
		usb_kick_hub_wq(xhci->shared_hcd->self.root_hub);

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_A_DEV_SESS_END_DET_EVNT;

	rc = A_WAIT_VRISE_TIMEOUT;

	rc = sleep_until_event(otg,
			otg_mask, 0,
			&otg_events, NULL, rc);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;

	} else if (otg_events & OEVT_A_DEV_SESS_END_DET_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_SESS_END_DET_EVNT\n");
		return OTG_STATE_A_WAIT_VFALL;

	} else if (rc == 0) {
		if (otg_read(otg, OCTL) & OCTL_PRT_PWR_CTL)
			return OTG_STATE_A_WAIT_BCON;
		else
			return OTG_STATE_A_WAIT_VFALL;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_a_idle(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask;
	u32 otg_events = 0;
	u32 user_events = 0;

	otg_dbg(otg, "");

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT | OEVT_A_DEV_SRP_DET_EVNT;
	user_mask = USER_SRP_EVENT;

	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events,
			0);

	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (otg_events & OEVT_A_DEV_SRP_DET_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_SRP_DET_EVNT\n");
		return OTG_STATE_A_WAIT_VRISE;
	} else if (user_events & USER_SRP_EVENT) {
		otg_dbg(otg, "User initiated VBUS\n");
		return OTG_STATE_A_WAIT_VRISE;
	}

	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_a_peripheral(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 otg_events = 0;

	otg_dbg(otg, "");
	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_A_DEV_SESS_END_DET_EVNT |
		OEVT_A_DEV_B_DEV_HOST_END_EVNT;

	rc = sleep_until_event(otg,
			otg_mask, 0,
			&otg_events, NULL, 0);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;

	} else if (otg_events & OEVT_A_DEV_SESS_END_DET_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_SESS_END_DET_EVNT\n");
		return OTG_STATE_A_WAIT_VFALL;

	} else if (otg_events & OEVT_A_DEV_B_DEV_HOST_END_EVNT) {
		otg_dbg(otg, "OEVT_A_DEV_B_DEV_HOST_END_EVNT\n");
		return OTG_STATE_A_WAIT_VRISE;
	}

	return OTG_STATE_UNDEFINED;
}

#define HNP_TIMEOUT	4000

static enum usb_otg_state do_b_hnp_init(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 events = 0;

	otg_dbg(otg, "");
	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_B_DEV_HNP_CHNG_EVNT |
		OEVT_B_DEV_VBUS_CHNG_EVNT;

	start_b_hnp(otg);
	rc = HNP_TIMEOUT;

again:
	rc = sleep_until_event(otg,
			otg_mask, 0,
			&events, NULL, rc);
	stop_b_hnp(otg);

	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (events & OEVT_B_DEV_VBUS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_VBUS_CHNG_EVNT\n");
		return OTG_STATE_B_IDLE;
	} else if (events & OEVT_B_DEV_HNP_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_HNP_CHNG_EVNT\n");
		if (events & OEVT_HST_NEG_SCS) {
			otg_dbg(otg, "B-HNP Success\n");
			return OTG_STATE_B_WAIT_ACON;

		} else {
			otg_err(otg, "B-HNP Failed\n");
			return OTG_STATE_B_PERIPHERAL;
		}
	} else if (rc == 0) {
		/* Timeout */
		otg_err(otg, "HNP timed out!\n");
		return OTG_STATE_B_PERIPHERAL;

	} else {
		goto again;
	}

	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_b_peripheral(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask;
	u32 otg_events = 0;
	u32 user_events = 0;

	otg_dbg(otg, "");
	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT | OEVT_B_DEV_VBUS_CHNG_EVNT;
	user_mask = USER_HNP_EVENT | USER_END_SESSION |
		USER_SRP_EVENT | INITIAL_SRP;

again:
	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events, 0);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (otg_events & OEVT_B_DEV_VBUS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_VBUS_CHNG_EVNT\n");

		if (otg_events & OEVT_B_SES_VLD_EVT) {
			otg_dbg(otg, "Session valid\n");
			goto again;
		} else {
			otg_dbg(otg, "Session not valid\n");
			return OTG_STATE_B_IDLE;
		}

	} else if (user_events & USER_HNP_EVENT) {
		otg_dbg(otg, "USER_HNP_EVENT\n");
		return do_b_hnp_init(otg);
	} else if (user_events & USER_END_SESSION) {
		otg_dbg(otg, "USER_END_SESSION\n");
		return OTG_STATE_B_IDLE;
	}

	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_b_wait_acon(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask = 0;
	u32 otg_events = 0;
	u32 user_events = 0;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	otg_dbg(otg, "");
	set_b_host(otg, 1);
	start_host(otg);
	otg_mask = OEVT_B_DEV_B_HOST_END_EVNT;
	otg_write(otg, OEVTEN, otg_mask);
	reset_port(otg);

	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);
	usb_kick_hub_wq(hcd->self.root_hub);
	if (xhci->shared_hcd)
		usb_kick_hub_wq(xhci->shared_hcd->self.root_hub);

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_B_DEV_B_HOST_END_EVNT |
		OEVT_B_DEV_VBUS_CHNG_EVNT |
		OEVT_HOST_ROLE_REQ_INIT_EVNT;
	user_mask = USER_A_CONN_EVENT;

again:
	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events, 0);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (otg_events & OEVT_B_DEV_B_HOST_END_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_B_HOST_END_EVNT\n");
		return OTG_STATE_B_PERIPHERAL;
	} else if (otg_events & OEVT_B_DEV_VBUS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_VBUS_CHNG_EVNT\n");
		if (otg_events & OEVT_B_SES_VLD_EVT) {
			otg_dbg(otg, "Session valid\n");
			goto again;
		} else {
			otg_dbg(otg, "Session not valid\n");
			return OTG_STATE_B_IDLE;
		}
	} else if (user_events & USER_A_CONN_EVENT) {
		otg_dbg(otg, "A-device connected\n");
		return OTG_STATE_B_HOST;
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_b_host(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask = 0;
	u32 otg_events = 0;
	u32 user_events = 0;

	otg_dbg(otg, "");

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_B_DEV_B_HOST_END_EVNT |
		OEVT_B_DEV_VBUS_CHNG_EVNT |
		OEVT_HOST_ROLE_REQ_INIT_EVNT;

again:
	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events, 0);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	/* Higher priority first */
	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (otg_events & OEVT_B_DEV_B_HOST_END_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_B_HOST_END_EVNT\n");
		return OTG_STATE_B_PERIPHERAL;
	} else if (otg_events & OEVT_B_DEV_VBUS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_VBUS_CHNG_EVNT\n");
		if (otg_events & OEVT_B_SES_VLD_EVT) {
			otg_dbg(otg, "Session valid\n");
			goto again;
		} else {
			otg_dbg(otg, "Session not valid\n");
			return OTG_STATE_B_IDLE;
		}
	}

	/* Invalid state */
	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_b_idle(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 user_mask;
	u32 otg_events = 0;
	u32 user_events = 0;

	otg_dbg(otg, "");

	if (!set_peri_mode(otg, PERI_MODE_PERIPHERAL))
		otg_err(otg, "Failed to set peripheral mode\n");

	dwc3_otg_setup_event_buffers(otg);

	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_B_DEV_SES_VLD_DET_EVNT |
		OEVT_B_DEV_VBUS_CHNG_EVNT;
	user_mask = USER_SRP_EVENT;

again:
	rc = sleep_until_event(otg,
			otg_mask, user_mask,
			&otg_events, &user_events, 0);

	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (otg_events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if ((otg_events & OEVT_B_DEV_VBUS_CHNG_EVNT) ||
		(otg_events & OEVT_B_DEV_SES_VLD_DET_EVNT)) {
		otg_dbg(otg, "OEVT_B_DEV_VBUS_CHNG_EVNT\n");
		if (otg_events & OEVT_B_SES_VLD_EVT) {
			otg_dbg(otg, "Session valid\n");
			return OTG_STATE_B_PERIPHERAL;

		} else {
			otg_dbg(otg, "Session not valid\n");
			goto again;
		}
	} else if (user_events & USER_SRP_EVENT) {
		otg_dbg(otg, "USER_SRP_EVENT\n");
		return OTG_STATE_B_SRP_INIT;
	}

	return OTG_STATE_UNDEFINED;
}

static enum usb_otg_state do_b_srp_init(struct dwc3_otg *otg)
{
	int rc;
	u32 otg_mask;
	u32 events = 0;

	otg_dbg(otg, "");
	otg_mask = OEVT_CONN_ID_STS_CHNG_EVNT |
		OEVT_B_DEV_SES_VLD_DET_EVNT |
		OEVT_B_DEV_VBUS_CHNG_EVNT;

	otg_write(otg, OEVTEN, otg_mask);
	start_srp(otg);

	rc = SRP_TIMEOUT;

again:
	rc = sleep_until_event(otg,
			otg_mask, 0,
			&events, NULL, rc);
	if (rc < 0)
		return OTG_STATE_UNDEFINED;

	if (events & OEVT_CONN_ID_STS_CHNG_EVNT) {
		otg_dbg(otg, "OEVT_CONN_ID_STS_CHNG_EVNT\n");
		return OTG_STATE_UNDEFINED;
	} else if (events & OEVT_B_DEV_SES_VLD_DET_EVNT) {
		otg_dbg(otg, "OEVT_B_DEV_SES_VLD_DET_EVNT\n");
		return OTG_STATE_B_PERIPHERAL;
	} else if (rc == 0) {
		otg_dbg(otg, "SRP Timeout (rc=%d)\n", rc);
		otg_info(otg, "DEVICE NO RESPONSE FOR SRP\n");
		return OTG_STATE_B_IDLE;

	} else {
		goto again;
	}

	return OTG_STATE_UNDEFINED;
}

int otg_main_thread(void *data)
{
	struct dwc3_otg *otg = (struct dwc3_otg *)data;
	enum usb_otg_state prev = OTG_STATE_UNDEFINED;

#ifdef VERBOSE_DEBUG
	u32 snpsid = otg_read(otg, 0xc120);

	otg_vdbg(otg, "io_priv=%p\n", otg->regs);
	otg_vdbg(otg, "c120: %x\n", snpsid);
#endif

	/* Allow the thread to be killed by a signal, but set the signal mask
	 * to block everything but INT, TERM, KILL, and USR1.
	 */
	allow_signal(SIGINT);
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	allow_signal(SIGUSR1);

	/* Allow the thread to be frozen */
	set_freezable();

	/* Allow host/peripheral driver load to finish */
	msleep(100);

	reset_hw(otg);

	stop_host(otg);
	stop_peripheral(otg);

	otg_dbg(otg, "Thread running\n");
	while (1) {
		enum usb_otg_state next = OTG_STATE_UNDEFINED;

		otg_vdbg(otg, "Main thread entering state\n");

		switch (otg->otg.state) {
		case OTG_STATE_UNDEFINED:
			otg_dbg(otg, "OTG_STATE_UNDEFINED\n");
			next = do_connector_id_status(otg);
			break;

		case OTG_STATE_A_IDLE:
			otg_dbg(otg, "OTG_STATE_A_IDLE\n");
			stop_peripheral(otg);

			if (prev == OTG_STATE_UNDEFINED)
				next = OTG_STATE_A_WAIT_VRISE;
			else
				next = do_a_idle(otg);
			break;

		case OTG_STATE_A_WAIT_VRISE:
			otg_dbg(otg, "OTG_STATE_A_WAIT_VRISE\n");
			next = do_a_wait_vrise(otg);
			break;

		case OTG_STATE_A_WAIT_BCON:
			otg_dbg(otg, "OTG_STATE_A_WAIT_BCON\n");
			next = do_a_wait_bconn(otg);
			break;

		case OTG_STATE_A_HOST:
			otg_dbg(otg, "OTG_STATE_A_HOST\n");
			stop_peripheral(otg);
			next = do_a_host(otg);
			/* Don't stop the host here if we are going into
			 * A_SUSPEND. We need to delay that until later. It
			 * will be stopped when coming out of A_SUSPEND
			 * state.
			 */
			if (next != OTG_STATE_A_SUSPEND)
				stop_host(otg);
			break;

		case OTG_STATE_A_SUSPEND:
			otg_dbg(otg, "OTG_STATE_A_SUSPEND\n");
			next = do_a_hnp_init(otg);

			/* Stop the host. */
			stop_host(otg);
			break;

		case OTG_STATE_A_WAIT_VFALL:
			otg_dbg(otg, "OTG_STATE_A_WAIT_VFALL\n");
			next = do_a_wait_vfall(otg);
			stop_host(otg);
			break;

		case OTG_STATE_A_PERIPHERAL:
			otg_dbg(otg, "OTG_STATE_A_PERIPHERAL\n");
			stop_host(otg);
			start_peripheral(otg);
			next = do_a_peripheral(otg);
			stop_peripheral(otg);
			break;

		case OTG_STATE_B_IDLE:
			otg_dbg(otg, "OTG_STATE_B_IDLE\n");
			next = do_b_idle(otg);
			break;

		case OTG_STATE_B_PERIPHERAL:
			otg_dbg(otg, "OTG_STATE_B_PERIPHERAL\n");
			stop_host(otg);
			start_peripheral(otg);
			next = do_b_peripheral(otg);
			stop_peripheral(otg);
			break;

		case OTG_STATE_B_SRP_INIT:
			otg_dbg(otg, "OTG_STATE_B_SRP_INIT\n");
			otg_read(otg, OSTS);
			next = do_b_srp_init(otg);
			break;

		case OTG_STATE_B_WAIT_ACON:
			otg_dbg(otg, "OTG_STATE_B_WAIT_ACON\n");
			next = do_b_wait_acon(otg);
			break;

		case OTG_STATE_B_HOST:
			otg_dbg(otg, "OTG_STATE_B_HOST\n");
			next = do_b_host(otg);
			stop_host(otg);
			break;

		default:
			otg_err(otg, "Unknown state %d, sleeping...\n",
					otg->state);
			sleep_main_thread(otg);
			break;
		}

		prev = otg->otg.state;
		otg->otg.state = next;
		if (kthread_should_stop())
			break;
	}

	otg->main_thread = NULL;
	otg_dbg(otg, "OTG main thread exiting....\n");

	return 0;
}

static void start_main_thread(struct dwc3_otg *otg)
{
	if (!otg->main_thread && otg->otg.gadget && otg->otg.host) {
		otg_dbg(otg, "Starting OTG main thread\n");
		otg->main_thread = kthread_create(otg_main_thread, otg, "otg");
		wake_up_process(otg->main_thread);
	}
}

static inline struct dwc3_otg *otg_to_dwc3_otg(struct usb_otg *x)
{
	return container_of(x, struct dwc3_otg, otg);
}

static irqreturn_t dwc3_otg_irq(int irq, void *_otg)
{
	struct dwc3_otg *otg;
	u32 oevt;
	u32 osts;
	u32 octl;
	u32 ocfg;
	u32 oevten;
	u32 otg_mask = OEVT_ALL;

	if (!_otg)
		return 0;

	otg = (struct dwc3_otg *)_otg;

	oevt = otg_read(otg, OEVT);
	osts = otg_read(otg, OSTS);
	octl = otg_read(otg, OCTL);
	ocfg = otg_read(otg, OCFG);
	oevten = otg_read(otg, OEVTEN);

	/* Clear handled events */
	otg_write(otg, OEVT, oevt);

	otg_vdbg(otg, "\n");
	otg_vdbg(otg, "    oevt = %08x\n", oevt);
	otg_vdbg(otg, "    osts = %08x\n", osts);
	otg_vdbg(otg, "    octl = %08x\n", octl);
	otg_vdbg(otg, "    ocfg = %08x\n", ocfg);
	otg_vdbg(otg, "  oevten = %08x\n", oevten);

	otg_vdbg(otg, "oevt[DeviceMode] = %s\n",
			oevt & OEVT_DEV_MOD_EVNT ? "Device" : "Host");

	if (oevt & OEVT_CONN_ID_STS_CHNG_EVNT)
		otg_dbg(otg, "Connector ID Status Change Event\n");
	if (oevt & OEVT_HOST_ROLE_REQ_INIT_EVNT)
		otg_dbg(otg, "Host Role Request Init Notification Event\n");
	if (oevt & OEVT_HOST_ROLE_REQ_CONFIRM_EVNT)
		otg_dbg(otg, "Host Role Request Confirm Notification Event\n");
	if (oevt & OEVT_A_DEV_B_DEV_HOST_END_EVNT)
		otg_dbg(otg, "A-Device B-Host End Event\n");
	if (oevt & OEVT_A_DEV_HOST_EVNT)
		otg_dbg(otg, "A-Device Host Event\n");
	if (oevt & OEVT_A_DEV_HNP_CHNG_EVNT)
		otg_dbg(otg, "A-Device HNP Change Event\n");
	if (oevt & OEVT_A_DEV_SRP_DET_EVNT)
		otg_dbg(otg, "A-Device SRP Detect Event\n");
	if (oevt & OEVT_A_DEV_SESS_END_DET_EVNT)
		otg_dbg(otg, "A-Device Session End Detected Event\n");
	if (oevt & OEVT_B_DEV_B_HOST_END_EVNT)
		otg_dbg(otg, "B-Device B-Host End Event\n");
	if (oevt & OEVT_B_DEV_HNP_CHNG_EVNT)
		otg_dbg(otg, "B-Device HNP Change Event\n");
	if (oevt & OEVT_B_DEV_SES_VLD_DET_EVNT)
		otg_dbg(otg, "B-Device Session Valid Detect Event\n");
	if (oevt & OEVT_B_DEV_VBUS_CHNG_EVNT)
		otg_dbg(otg, "B-Device VBUS Change Event\n");

	if (oevt & otg_mask) {
		/* Pass event to main thread */
		spin_lock(&otg->lock);
		otg->otg_events |= oevt;
		wakeup_main_thread(otg);
		spin_unlock(&otg->lock);
		return 1;
	}

	return IRQ_HANDLED;
}

static void hnp_polling_work(struct work_struct *w)
{
	struct dwc3_otg *otg = container_of(w, struct dwc3_otg,
			hp_work.work);
	struct usb_bus *bus;
	struct usb_device *udev;
	struct usb_hcd *hcd;
	u8 *otgstatus;
	int ret;
	int err;

	hcd = container_of(otg->otg.host, struct usb_hcd, self);
	if (!hcd)
		return;

	bus = &hcd->self;
	if (!bus->otg_port)
		return;

	udev = usb_hub_find_child(bus->root_hub, bus->otg_port);
	if (!udev)
		return;

	otgstatus = kmalloc(sizeof(*otgstatus), GFP_NOIO);
	if (!otgstatus)
		return;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RECIP_DEVICE,
			0, 0xf000, otgstatus, sizeof(*otgstatus),
			USB_CTRL_GET_TIMEOUT);

	if (ret == sizeof(*otgstatus) && (*otgstatus & 0x1)) {
		/* enable HNP before suspend, it's simpler */

		udev->bus->b_hnp_enable = 1;
		err = usb_control_msg(udev,
				usb_sndctrlpipe(udev, 0),
				USB_REQ_SET_FEATURE, 0,
				udev->bus->b_hnp_enable
				? USB_DEVICE_B_HNP_ENABLE
				: USB_DEVICE_A_ALT_HNP_SUPPORT,
				0, NULL, 0, USB_CTRL_SET_TIMEOUT);

		if (err < 0) {
			/* OTG MESSAGE: report errors here,
			 * customize to match your product.
			 */
			otg_info(otg, "ERROR : Device no response\n");
			dev_info(&udev->dev, "can't set HNP mode: %d\n",
					err);
			udev->bus->b_hnp_enable = 0;
			if (le16_to_cpu(udev->descriptor.idVendor) == 0x1a0a) {
				if (usb_port_suspend(udev, PMSG_AUTO_SUSPEND)
						< 0)
					dev_dbg(&udev->dev, "HNP fail, %d\n",
							err);
			}
		} else {
			/* Device wants role-switch, suspend the bus. */
			static struct usb_phy *phy;

			phy = usb_get_phy(USB_PHY_TYPE_USB3);
			otg_start_hnp(phy->otg);
			usb_put_phy(phy);

			if (usb_port_suspend(udev, PMSG_AUTO_SUSPEND) < 0)
				dev_dbg(&udev->dev, "HNP fail, %d\n", err);
		}
	} else if (ret < 0) {
		udev->bus->b_hnp_enable = 1;
		err = usb_control_msg(udev,
				usb_sndctrlpipe(udev, 0),
				USB_REQ_SET_FEATURE, 0,
				USB_DEVICE_B_HNP_ENABLE,
				0, NULL, 0, USB_CTRL_SET_TIMEOUT);
		if (usb_port_suspend(udev, PMSG_AUTO_SUSPEND) < 0)
			dev_dbg(&udev->dev, "HNP fail, %d\n", err);
	} else {
		schedule_delayed_work(&otg->hp_work, 1 * HZ);
	}

	kfree(otgstatus);
}

static int dwc3_otg_notify_connect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	struct usb_bus *bus;
	struct usb_device *udev;
	struct usb_hcd *hcd;
	struct dwc3_otg *otg;
	int err = 0;

	otg = otg_to_dwc3_otg(phy->otg);

	hcd = container_of(phy->otg->host, struct usb_hcd, self);
	if (!hcd)
		return -EINVAL;

	bus = &hcd->self;
	if (!bus->otg_port)
		return 0;

	udev = usb_hub_find_child(bus->root_hub, bus->otg_port);
	if (!udev)
		return 0;

	/*
	 * OTG-aware devices on OTG-capable root hubs may be able to use SRP,
	 * to wake us after we've powered off VBUS; and HNP, switching roles
	 * "host" to "peripheral".  The OTG descriptor helps figure this out.
	 */
	if (udev->config && udev->parent == udev->bus->root_hub) {
		struct usb_otg20_descriptor	*desc = NULL;

		/* descriptor may appear anywhere in config */
		err = __usb_get_extra_descriptor(udev->rawdescriptors[0],
				le16_to_cpu(udev->config[0].desc.wTotalLength),
				USB_DT_OTG, (void **) &desc);
		if (err || !(desc->bmAttributes & USB_OTG_HNP))
			return 0;

		if (udev->portnum == udev->bus->otg_port) {
			INIT_DELAYED_WORK(&otg->hp_work,
					hnp_polling_work);
			schedule_delayed_work(&otg->hp_work, HZ);
		}

	}

	return err;
}

static int dwc3_otg_notify_disconnect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	struct dwc3_otg *otg;

	otg = otg_to_dwc3_otg(phy->otg);

	if (work_pending(&otg->hp_work.work)) {
		while (!cancel_delayed_work(&otg->hp_work))
			msleep(20);
	}
	return 0;
}

static void dwc3_otg_set_peripheral(struct usb_otg *_otg, int yes)
{
	struct dwc3_otg *otg;

	if (!_otg)
		return;

	otg = otg_to_dwc3_otg(_otg);
	otg_dbg(otg, "\n");

	if (yes) {
		if (otg->hwparams6 == 0xdeadbeef)
			otg->hwparams6 = otg_read(otg, GHWPARAMS6);
		stop_host(otg);
	} else {
		stop_peripheral(otg);
	}

	set_peri_mode(otg, yes);
}
EXPORT_SYMBOL(dwc3_otg_set_peripheral);

static int dwc3_otg_set_periph(struct usb_otg *_otg, struct usb_gadget *gadget)
{
	struct dwc3_otg *otg;

	if (!_otg)
		return -ENODEV;

	otg = otg_to_dwc3_otg(_otg);
	otg_dbg(otg, "\n");

	if ((long)gadget == 1) {
		dwc3_otg_set_peripheral(_otg, 1);
		return 0;
	}

	if (!gadget) {
		otg->otg.gadget = NULL;
		return -ENODEV;
	}

	otg->otg.gadget = gadget;
	otg->otg.gadget->hnp_polling_support = 1;
	otg->otg.state = OTG_STATE_B_IDLE;

	start_main_thread(otg);
	return 0;
}

static int dwc3_otg_set_host(struct usb_otg *_otg, struct usb_bus *host)
{
	struct dwc3_otg *otg;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	if (!_otg)
		return -ENODEV;

	otg = otg_to_dwc3_otg(_otg);
	otg_dbg(otg, "\n");

	if ((long)host == 1) {
		dwc3_otg_set_peripheral(_otg, 0);
		return 0;
	}

	if (!host) {
		otg->otg.host = NULL;
		otg->hcd_irq = 0;
		return -ENODEV;
	}

	hcd = container_of(host, struct usb_hcd, self);
	xhci = hcd_to_xhci(hcd);
	otg_dbg(otg, "hcd=%p xhci=%p\n", hcd, xhci);

	hcd->self.otg_port = 1;
	if (xhci->shared_hcd) {
		xhci->shared_hcd->self.otg_port = 1;
		otg_dbg(otg, "shared_hcd=%p\n", xhci->shared_hcd);
	}

	otg->otg.host = host;
	otg->hcd_irq = hcd->irq;
	otg_dbg(otg, "host=%p irq=%d\n", otg->otg.host, otg->hcd_irq);


	otg->host_started = 1;
	otg->dev_enum = 0;
	start_main_thread(otg);
	return 0;
}

static int dwc3_otg_start_srp(struct usb_otg *x)
{
	unsigned long flags;
	struct dwc3_otg *otg;

	if (!x)
		return -ENODEV;

	otg = otg_to_dwc3_otg(x);
	otg_dbg(otg, "\n");

	if (!otg->otg.host || !otg->otg.gadget)
		return -ENODEV;

	spin_lock_irqsave(&otg->lock, flags);
	otg->user_events |= USER_SRP_EVENT;
	wakeup_main_thread(otg);
	spin_unlock_irqrestore(&otg->lock, flags);
	return 0;
}

static int dwc3_otg_start_hnp(struct usb_otg *x)
{
	unsigned long flags;
	struct dwc3_otg *otg;

	if (!x)
		return -ENODEV;

	otg = otg_to_dwc3_otg(x);
	otg_dbg(otg, "\n");

	if (!otg->otg.host || !otg->otg.gadget)
		return -ENODEV;

	spin_lock_irqsave(&otg->lock, flags);
	otg->user_events |= USER_HNP_EVENT;
	wakeup_main_thread(otg);
	spin_unlock_irqrestore(&otg->lock, flags);
	return 0;
}

static int dwc3_otg_end_session(struct usb_otg *x)
{
	unsigned long flags;
	struct dwc3_otg *otg;

	if (!x)
		return -ENODEV;

	otg = otg_to_dwc3_otg(x);
	otg_dbg(otg, "\n");

	if (!otg->otg.host || !otg->otg.gadget)
		return -ENODEV;

	spin_lock_irqsave(&otg->lock, flags);
	otg->user_events |= USER_END_SESSION;
	wakeup_main_thread(otg);
	spin_unlock_irqrestore(&otg->lock, flags);
	return 0;
}

static int otg_end_session(struct usb_otg *otg)
{
	return dwc3_otg_end_session(otg);
}
EXPORT_SYMBOL(otg_end_session);

static int dwc3_otg_received_host_release(struct usb_otg *x)
{
	struct dwc3_otg *otg;
	unsigned long flags;

	if (!x)
		return -ENODEV;

	otg = otg_to_dwc3_otg(x);
	otg_dbg(otg, "\n");

	if (!otg->otg.host || !otg->otg.gadget)
		return -ENODEV;

	spin_lock_irqsave(&otg->lock, flags);
	otg->user_events |= PCD_RECEIVED_HOST_RELEASE_EVENT;
	wakeup_main_thread(otg);
	spin_unlock_irqrestore(&otg->lock, flags);
	return 0;
}

int otg_host_release(struct usb_otg *otg)
{
	return dwc3_otg_received_host_release(otg);
}
EXPORT_SYMBOL(otg_host_release);

static void dwc3_otg_enable_irq(struct dwc3_otg *otg)
{
	u32			reg;

	/* Enable OTG IRQs */
	reg = OEVT_ALL;

	otg_write(otg, OEVTEN, reg);
}

static ssize_t store_srp(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct usb_phy *phy;
	struct usb_otg *otg;

	phy = usb_get_phy(USB_PHY_TYPE_USB3);
	if (IS_ERR(phy) || !phy) {
		if (!IS_ERR(phy))
			usb_put_phy(phy);
		return count;
	}

	otg = phy->otg;
	if (!otg) {
		usb_put_phy(phy);
		return count;
	}

	otg_start_srp(otg);
	usb_put_phy(phy);
	return count;
}
static DEVICE_ATTR(srp, 0220, NULL, store_srp);

static ssize_t store_end(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct usb_phy *phy;
	struct usb_otg *otg;

	phy = usb_get_phy(USB_PHY_TYPE_USB3);
	if (IS_ERR(phy) || !phy) {
		if (!IS_ERR(phy))
			usb_put_phy(phy);
		return count;
	}

	otg = phy->otg;
	if (!otg) {
		usb_put_phy(phy);
		return count;
	}

	otg_end_session(otg);
	usb_put_phy(phy);
	return count;
}
static DEVICE_ATTR(end, 0220, NULL, store_end);

static ssize_t store_hnp(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct usb_phy *phy = usb_get_phy(USB_PHY_TYPE_USB3);
	struct usb_otg *otg;

	dev_dbg(dwc->dev, "%s()\n", __func__);

	if (IS_ERR(phy) || !phy) {
		dev_info(dwc->dev, "NO PHY!!\n");
		if (!IS_ERR(phy))
			usb_put_phy(phy);
		return count;
	}

	otg = phy->otg;
	if (!otg) {
		dev_info(dwc->dev, "NO OTG!!\n");
		usb_put_phy(phy);
		return count;
	}

	dev_info(dev, "b_hnp_enable is FALSE\n");
	dwc->gadget.host_request_flag = 1;

	usb_put_phy(phy);
	return count;
}
static DEVICE_ATTR(hnp, 0220, NULL, store_hnp);

static ssize_t store_a_hnp_reqd(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_otg *otg;

	otg = dwc->otg;
	host_release(otg);
	return count;
}
static DEVICE_ATTR(a_hnp_reqd, 0220, NULL, store_a_hnp_reqd);

static ssize_t store_print_dbg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_otg *otg;

	otg = dwc->otg;
	print_debug_regs(otg);

	return count;
}
static DEVICE_ATTR(print_dbg, 0220, NULL, store_print_dbg);

void dwc_usb3_remove_dev_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_print_dbg);
	device_remove_file(dev, &dev_attr_a_hnp_reqd);
	device_remove_file(dev, &dev_attr_end);
	device_remove_file(dev, &dev_attr_srp);
	device_remove_file(dev, &dev_attr_hnp);
}

int dwc3_otg_create_dev_files(struct device *dev)
{
	int retval;

	retval = device_create_file(dev, &dev_attr_hnp);
	if (retval)
		goto fail;

	retval = device_create_file(dev, &dev_attr_srp);
	if (retval)
		goto fail;

	retval = device_create_file(dev, &dev_attr_end);
	if (retval)
		goto fail;

	retval = device_create_file(dev, &dev_attr_a_hnp_reqd);
	if (retval)
		goto fail;

	retval = device_create_file(dev, &dev_attr_print_dbg);
	if (retval)
		goto fail;

	return 0;

fail:
	dev_err(dev, "Failed to create one or more sysfs files!!\n");
	return retval;
}

int dwc3_otg_init(struct dwc3 *dwc)
{
	struct dwc3_otg *otg;
	int err;
	u32 reg;

	dev_dbg(dwc->dev, "dwc3_otg_init\n");

	/*
	 * GHWPARAMS6[10] bit is SRPSupport.
	 * This bit also reflects DWC_USB3_EN_OTG
	 */
	reg = dwc3_readl(dwc->regs, DWC3_GHWPARAMS6);
	if (!(reg & GHWPARAMS6_SRP_SUPPORT_ENABLED)) {
		/*
		 * No OTG support in the HW core.
		 * We return 0 to indicate no error, since this is acceptable
		 * situation, just continue probe the dwc3 driver without otg.
		 */
		dev_dbg(dwc->dev, "dwc3_otg address space is not supported\n");
		return 0;
	}

	otg = kzalloc(sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	dwc->otg = otg;
	otg->dev = dwc->dev;
	otg->dwc = dwc;

	otg->regs = dwc->regs - DWC3_GLOBALS_REGS_START;
	otg->otg.usb_phy = kzalloc(sizeof(struct usb_phy), GFP_KERNEL);
	otg->otg.usb_phy->dev = otg->dev;
	otg->otg.usb_phy->label = "dwc3_otg";
	otg->otg.state = OTG_STATE_UNDEFINED;
	otg->otg.usb_phy->otg = &otg->otg;
	otg->otg.usb_phy->notify_connect = dwc3_otg_notify_connect;
	otg->otg.usb_phy->notify_disconnect = dwc3_otg_notify_disconnect;

	otg->otg.start_srp = dwc3_otg_start_srp;
	otg->otg.start_hnp = dwc3_otg_start_hnp;
	otg->otg.set_host = dwc3_otg_set_host;
	otg->otg.set_peripheral = dwc3_otg_set_periph;

	otg->hwparams6 = reg;
	otg->state = OTG_STATE_UNDEFINED;

	spin_lock_init(&otg->lock);
	init_waitqueue_head(&otg->main_wq);

	err = usb_add_phy(otg->otg.usb_phy, USB_PHY_TYPE_USB3);
	if (err) {
		dev_err(otg->dev, "can't register transceiver, err: %d\n",
			err);
		goto exit;
	}

	otg->irq = platform_get_irq(to_platform_device(otg->dev), 1);

	dwc3_otg_create_dev_files(otg->dev);

	/* Set irq handler */
	err = request_irq(otg->irq, dwc3_otg_irq, IRQF_SHARED, "dwc3_otg", otg);
	if (err) {
		dev_err(otg->otg.usb_phy->dev, "failed to request irq #%d --> %d\n",
				otg->irq, err);
		goto exit;
	}

	dwc3_otg_enable_irq(otg);

	return 0;
exit:
	kfree(otg->otg.usb_phy);
	kfree(otg);
	return err;
}

void dwc3_otg_exit(struct dwc3 *dwc)
{
	struct dwc3_otg *otg = dwc->otg;

	otg_dbg(otg, "\n");
	usb_remove_phy(otg->otg.usb_phy);
	kfree(otg->otg.usb_phy);
	kfree(otg);
}
