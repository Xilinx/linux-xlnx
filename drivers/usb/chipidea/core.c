/*
 * core.c - ChipIdea USB IP core family device controller
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Description: ChipIdea USB IP core family device controller
 *
 * This driver is composed of several blocks:
 * - HW:     hardware interface
 * - DBG:    debug facilities (optional)
 * - UTIL:   utilities
 * - ISR:    interrupts handling
 * - ENDPT:  endpoint operations (Gadget API)
 * - GADGET: gadget operations (Gadget API)
 * - BUS:    bus glue code, bus abstraction layer
 *
 * Compile Options
 * - CONFIG_USB_GADGET_DEBUG_FILES: enable debug facilities
 * - STALL_IN:  non-empty bulk-in pipes cannot be halted
 *              if defined mass storage compliance succeeds but with warnings
 *              => case 4: Hi >  Dn
 *              => case 5: Hi >  Di
 *              => case 8: Hi <> Do
 *              if undefined usbtest 13 fails
 * - TRACE:     enable function tracing (depends on DEBUG)
 *
 * Main Features
 * - Chapter 9 & Mass Storage Compliance with Gadget File Storage
 * - Chapter 9 Compliance with Gadget Zero (STALL_IN undefined)
 * - Normal & LPM support
 *
 * USBTEST Report
 * - OK: 0-12, 13 (STALL_IN defined) & 14
 * - Not Supported: 15 & 16 (ISO)
 *
 * TODO List
 * - OTG
 * - Interrupt Traffic
 * - GET_STATUS(device) - always reports 0
 * - Gadget API (majority of optional features)
 * - Suspend & Remote Wakeup
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/chipidea.h>
#include <linux/usb/of.h>
#include <linux/phy.h>
#include <linux/regulator/consumer.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "host.h"
#include "debug.h"
#include "otg.h"

/* Controller register map */
static uintptr_t ci_regs_nolpm[] = {
	[CAP_CAPLENGTH]		= 0x000UL,
	[CAP_HCCPARAMS]		= 0x008UL,
	[CAP_DCCPARAMS]		= 0x024UL,
	[CAP_TESTMODE]		= 0x038UL,
	[OP_USBCMD]		= 0x000UL,
	[OP_USBSTS]		= 0x004UL,
	[OP_USBINTR]		= 0x008UL,
	[OP_DEVICEADDR]		= 0x014UL,
	[OP_ENDPTLISTADDR]	= 0x018UL,
	[OP_PORTSC]		= 0x044UL,
	[OP_DEVLC]		= 0x084UL,
	[OP_OTGSC]		= 0x064UL,
	[OP_USBMODE]		= 0x068UL,
	[OP_ENDPTSETUPSTAT]	= 0x06CUL,
	[OP_ENDPTPRIME]		= 0x070UL,
	[OP_ENDPTFLUSH]		= 0x074UL,
	[OP_ENDPTSTAT]		= 0x078UL,
	[OP_ENDPTCOMPLETE]	= 0x07CUL,
	[OP_ENDPTCTRL]		= 0x080UL,
};

static uintptr_t ci_regs_lpm[] = {
	[CAP_CAPLENGTH]		= 0x000UL,
	[CAP_HCCPARAMS]		= 0x008UL,
	[CAP_DCCPARAMS]		= 0x024UL,
	[CAP_TESTMODE]		= 0x0FCUL,
	[OP_USBCMD]		= 0x000UL,
	[OP_USBSTS]		= 0x004UL,
	[OP_USBINTR]		= 0x008UL,
	[OP_DEVICEADDR]		= 0x014UL,
	[OP_ENDPTLISTADDR]	= 0x018UL,
	[OP_PORTSC]		= 0x044UL,
	[OP_DEVLC]		= 0x084UL,
	[OP_OTGSC]		= 0x0C4UL,
	[OP_USBMODE]		= 0x0C8UL,
	[OP_ENDPTSETUPSTAT]	= 0x0D8UL,
	[OP_ENDPTPRIME]		= 0x0DCUL,
	[OP_ENDPTFLUSH]		= 0x0E0UL,
	[OP_ENDPTSTAT]		= 0x0E4UL,
	[OP_ENDPTCOMPLETE]	= 0x0E8UL,
	[OP_ENDPTCTRL]		= 0x0ECUL,
};

static int hw_alloc_regmap(struct ci_hdrc *ci, bool is_lpm)
{
	int i;

	kfree(ci->hw_bank.regmap);

	ci->hw_bank.regmap = kzalloc((OP_LAST + 1) * sizeof(void *),
				     GFP_KERNEL);
	if (!ci->hw_bank.regmap)
		return -ENOMEM;

	for (i = 0; i < OP_ENDPTCTRL; i++)
		ci->hw_bank.regmap[i] =
			(i <= CAP_LAST ? ci->hw_bank.cap : ci->hw_bank.op) +
			(is_lpm ? ci_regs_lpm[i] : ci_regs_nolpm[i]);

	for (; i <= OP_LAST; i++)
		ci->hw_bank.regmap[i] = ci->hw_bank.op +
			4 * (i - OP_ENDPTCTRL) +
			(is_lpm
			 ? ci_regs_lpm[OP_ENDPTCTRL]
			 : ci_regs_nolpm[OP_ENDPTCTRL]);

	return 0;
}

/**
 * hw_port_test_set: writes port test mode (execute without interruption)
 * @mode: new value
 *
 * This function returns an error code
 */
int hw_port_test_set(struct ci_hdrc *ci, u8 mode)
{
	const u8 TEST_MODE_MAX = 7;

	if (mode > TEST_MODE_MAX)
		return -EINVAL;

	hw_write(ci, OP_PORTSC, PORTSC_PTC, mode << __ffs(PORTSC_PTC));
	return 0;
}

/**
 * hw_port_test_get: reads port test mode value
 *
 * This function returns port test mode value
 */
u8 hw_port_test_get(struct ci_hdrc *ci)
{
	return hw_read(ci, OP_PORTSC, PORTSC_PTC) >> __ffs(PORTSC_PTC);
}

/* The PHY enters/leaves low power mode */
static void ci_hdrc_enter_lpm(struct ci_hdrc *ci, bool enable)
{
	enum ci_hw_regs reg = ci->hw_bank.lpm ? OP_DEVLC : OP_PORTSC;
	bool lpm = !!(hw_read(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm)));

	if (enable && !lpm) {
		hw_write(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm),
				PORTSC_PHCD(ci->hw_bank.lpm));
	} else  if (!enable && lpm) {
		hw_write(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm),
				0);
		/* 
		 * The controller needs at least 1ms to reflect
		 * PHY's status, the PHY also needs some time (less
		 * than 1ms) to leave low power mode.
		 */
		usleep_range(1500, 2000);
	}
}

static int hw_device_init(struct ci_hdrc *ci, void __iomem *base)
{
	u32 reg;

	/* bank is a module variable */
	ci->hw_bank.abs = base;

	ci->hw_bank.cap = ci->hw_bank.abs;
	ci->hw_bank.cap += ci->platdata->capoffset;
	ci->hw_bank.op = ci->hw_bank.cap + (ioread32(ci->hw_bank.cap) & 0xff);

	hw_alloc_regmap(ci, false);
	reg = hw_read(ci, CAP_HCCPARAMS, HCCPARAMS_LEN) >>
		__ffs(HCCPARAMS_LEN);
	ci->hw_bank.lpm  = reg;
	hw_alloc_regmap(ci, !!reg);
	ci->hw_bank.size = ci->hw_bank.op - ci->hw_bank.abs;
	ci->hw_bank.size += OP_LAST;
	ci->hw_bank.size /= sizeof(u32);

	reg = hw_read(ci, CAP_DCCPARAMS, DCCPARAMS_DEN) >>
		__ffs(DCCPARAMS_DEN);
	ci->hw_ep_max = reg * 2;   /* cache hw ENDPT_MAX */

	if (ci->hw_ep_max > ENDPT_MAX)
		return -ENODEV;

	ci_hdrc_enter_lpm(ci, false);

	/* Disable all interrupts bits */
	hw_write(ci, OP_USBINTR, 0xffffffff, 0);

	/* Clear all interrupts status bits*/
	hw_write(ci, OP_USBSTS, 0xffffffff, 0xffffffff);

	dev_dbg(ci->dev, "ChipIdea HDRC found, lpm: %d; cap: %p op: %p\n",
		ci->hw_bank.lpm, ci->hw_bank.cap, ci->hw_bank.op);

	/* setup lock mode ? */

	/* ENDPTSETUPSTAT is '0' by default */

	/* HCSPARAMS.bf.ppc SHOULD BE zero for device */

	return 0;
}

static void hw_phymode_configure(struct ci_hdrc *ci)
{
	u32 portsc, lpm, sts;

	switch (ci->platdata->phy_mode) {
	case USBPHY_INTERFACE_MODE_UTMI:
		portsc = PORTSC_PTS(PTS_UTMI);
		lpm = DEVLC_PTS(PTS_UTMI);
		break;
	case USBPHY_INTERFACE_MODE_UTMIW:
		portsc = PORTSC_PTS(PTS_UTMI) | PORTSC_PTW;
		lpm = DEVLC_PTS(PTS_UTMI) | DEVLC_PTW;
		break;
	case USBPHY_INTERFACE_MODE_ULPI:
		portsc = PORTSC_PTS(PTS_ULPI);
		lpm = DEVLC_PTS(PTS_ULPI);
		break;
	case USBPHY_INTERFACE_MODE_SERIAL:
		portsc = PORTSC_PTS(PTS_SERIAL);
		lpm = DEVLC_PTS(PTS_SERIAL);
		sts = 1;
		break;
	case USBPHY_INTERFACE_MODE_HSIC:
		portsc = PORTSC_PTS(PTS_HSIC);
		lpm = DEVLC_PTS(PTS_HSIC);
		break;
	default:
		return;
	}

	if (ci->hw_bank.lpm) {
		hw_write(ci, OP_DEVLC, DEVLC_PTS(7) | DEVLC_PTW, lpm);
		hw_write(ci, OP_DEVLC, DEVLC_STS, sts);
	} else {
		hw_write(ci, OP_PORTSC, PORTSC_PTS(7) | PORTSC_PTW, portsc);
		hw_write(ci, OP_PORTSC, PORTSC_STS, sts);
	}
}

/**
 * hw_device_reset: resets chip (execute without interruption)
 * @ci: the controller
  *
 * This function returns an error code
 */
int hw_device_reset(struct ci_hdrc *ci, u32 mode)
{
	/* should flush & stop before reset */
	hw_write(ci, OP_ENDPTFLUSH, ~0, ~0);
	hw_write(ci, OP_USBCMD, USBCMD_RS, 0);

	hw_write(ci, OP_USBCMD, USBCMD_RST, USBCMD_RST);
	while (hw_read(ci, OP_USBCMD, USBCMD_RST))
		udelay(10);		/* not RTOS friendly */

	if (ci->platdata->notify_event)
		ci->platdata->notify_event(ci,
			CI_HDRC_CONTROLLER_RESET_EVENT);

	if (ci->platdata->flags & CI_HDRC_DISABLE_STREAMING)
		hw_write(ci, OP_USBMODE, USBMODE_CI_SDIS, USBMODE_CI_SDIS);

	/* USBMODE should be configured step by step */
	hw_write(ci, OP_USBMODE, USBMODE_CM, USBMODE_CM_IDLE);
	hw_write(ci, OP_USBMODE, USBMODE_CM, mode);
	/* HW >= 2.3 */
	hw_write(ci, OP_USBMODE, USBMODE_SLOM, USBMODE_SLOM);

	if (hw_read(ci, OP_USBMODE, USBMODE_CM) != mode) {
		pr_err("cannot enter in %s mode", ci_role(ci)->name);
		pr_err("lpm = %i", ci->hw_bank.lpm);
		return -ENODEV;
	}

	return 0;
}

/**
 * hw_wait_reg: wait the register value
 *
 * Sometimes, it needs to wait register value before going on.
 * Eg, when switch to device mode, the vbus value should be lower
 * than OTGSC_BSV before connects to host.
 *
 * @ci: the controller
 * @reg: register index
 * @mask: mast bit
 * @value: the bit value to wait
 * @timeout_ms: timeout in millisecond
 *
 * This function returns an error code if timeout
 */
int hw_wait_reg(struct ci_hdrc *ci, enum ci_hw_regs reg, u32 mask,
				u32 value, unsigned int timeout_ms)
{
	unsigned long elapse = jiffies + msecs_to_jiffies(timeout_ms);

	while (hw_read(ci, reg, mask) != value) {
		if (time_after(jiffies, elapse)) {
			dev_err(ci->dev, "timeout waiting for %08x in %d\n",
					mask, reg);
			return -ETIMEDOUT;
		}
		msleep(20);
	}

	return 0;
}

static irqreturn_t ci_irq(int irq, void *data)
{
	struct ci_hdrc *ci = data;
	irqreturn_t ret = IRQ_NONE;
	u32 otgsc = 0;

	if (ci->is_otg)
		otgsc = hw_read(ci, OP_OTGSC, ~0);

	/*
	 * Handle id change interrupt, it indicates device/host function
	 * switch.
	 */
	if (ci->is_otg && (otgsc & OTGSC_IDIE) && (otgsc & OTGSC_IDIS)) {
		ci->id_event = true;
		ci_clear_otg_interrupt(ci, OTGSC_IDIS);
		disable_irq_nosync(ci->irq);
		queue_work(ci->wq, &ci->work);
		return IRQ_HANDLED;
	}

	/*
	 * Handle vbus change interrupt, it indicates device connection
	 * and disconnection events.
	 */
	if (ci->is_otg && (otgsc & OTGSC_BSVIE) && (otgsc & OTGSC_BSVIS)) {
		ci->b_sess_valid_event = true;
		ci_clear_otg_interrupt(ci, OTGSC_BSVIS);
		disable_irq_nosync(ci->irq);
		queue_work(ci->wq, &ci->work);
		return IRQ_HANDLED;
	}

	/* Handle device/host interrupt */
	if (ci->role != CI_ROLE_END)
		ret = ci_role(ci)->irq(ci);

	return ret;
}

static int ci_get_platdata(struct device *dev,
		struct ci_hdrc_platform_data *platdata)
{
	if (!platdata->phy_mode)
		platdata->phy_mode = of_usb_get_phy_mode(dev->of_node);

	if (!platdata->dr_mode)
		platdata->dr_mode = of_usb_get_dr_mode(dev->of_node);

	if (platdata->dr_mode == USB_DR_MODE_UNKNOWN)
		platdata->dr_mode = USB_DR_MODE_OTG;

	if (platdata->dr_mode != USB_DR_MODE_PERIPHERAL) {
		/* Get the vbus regulator */
		platdata->reg_vbus = devm_regulator_get(dev, "vbus");
		if (PTR_ERR(platdata->reg_vbus) == -EPROBE_DEFER) {
			return -EPROBE_DEFER;
		} else if (PTR_ERR(platdata->reg_vbus) == -ENODEV) {
			/* no vbus regualator is needed */
			platdata->reg_vbus = NULL;
		} else if (IS_ERR(platdata->reg_vbus)) {
			dev_err(dev, "Getting regulator error: %ld\n",
				PTR_ERR(platdata->reg_vbus));
			return PTR_ERR(platdata->reg_vbus);
		}
	}

	return 0;
}

static DEFINE_IDA(ci_ida);

struct platform_device *ci_hdrc_add_device(struct device *dev,
			struct resource *res, int nres,
			struct ci_hdrc_platform_data *platdata)
{
	struct platform_device *pdev;
	int id, ret;

	ret = ci_get_platdata(dev, platdata);
	if (ret)
		return ERR_PTR(ret);

	id = ida_simple_get(&ci_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	pdev = platform_device_alloc("ci_hdrc", id);
	if (!pdev) {
		ret = -ENOMEM;
		goto put_id;
	}

	pdev->dev.parent = dev;
	pdev->dev.dma_mask = dev->dma_mask;
	pdev->dev.dma_parms = dev->dma_parms;
	dma_set_coherent_mask(&pdev->dev, dev->coherent_dma_mask);

	ret = platform_device_add_resources(pdev, res, nres);
	if (ret)
		goto err;

	ret = platform_device_add_data(pdev, platdata, sizeof(*platdata));
	if (ret)
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
		goto err;

	return pdev;

err:
	platform_device_put(pdev);
put_id:
	ida_simple_remove(&ci_ida, id);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(ci_hdrc_add_device);

void ci_hdrc_remove_device(struct platform_device *pdev)
{
	int id = pdev->id;
	platform_device_unregister(pdev);
	ida_simple_remove(&ci_ida, id);
}
EXPORT_SYMBOL_GPL(ci_hdrc_remove_device);

static inline void ci_role_destroy(struct ci_hdrc *ci)
{
	ci_hdrc_gadget_destroy(ci);
	ci_hdrc_host_destroy(ci);
	if (ci->is_otg)
		ci_hdrc_otg_destroy(ci);
}

static void ci_get_otg_capable(struct ci_hdrc *ci)
{
	if (ci->platdata->flags & CI_HDRC_DUAL_ROLE_NOT_OTG)
		ci->is_otg = false;
	else
		ci->is_otg = (hw_read(ci, CAP_DCCPARAMS,
				DCCPARAMS_DC | DCCPARAMS_HC)
					== (DCCPARAMS_DC | DCCPARAMS_HC));
	if (ci->is_otg) {
		dev_dbg(ci->dev, "It is OTG capable controller\n");
		ci_disable_otg_interrupt(ci, OTGSC_INT_EN_BITS);
		ci_clear_otg_interrupt(ci, OTGSC_INT_STATUS_BITS);
	}
}

static int ci_usb_phy_init(struct ci_hdrc *ci)
{
	if (ci->platdata->phy) {
		ci->transceiver = ci->platdata->phy;
		return usb_phy_init(ci->transceiver);
	} else {
		ci->global_phy = true;
		ci->transceiver = usb_get_phy(USB_PHY_TYPE_USB2);
		if (IS_ERR(ci->transceiver))
			ci->transceiver = NULL;

		return 0;
	}
}

static void ci_usb_phy_destroy(struct ci_hdrc *ci)
{
	if (!ci->transceiver)
		return;

	otg_set_peripheral(ci->transceiver->otg, NULL);
	if (ci->global_phy)
		usb_put_phy(ci->transceiver);
	else
		usb_phy_shutdown(ci->transceiver);
}

static int ci_hdrc_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct ci_hdrc	*ci;
	struct resource	*res;
	void __iomem	*base;
	int		ret;
	enum usb_dr_mode dr_mode;

	if (!dev->platform_data) {
		dev_err(dev, "platform data missing\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ci = devm_kzalloc(dev, sizeof(*ci), GFP_KERNEL);
	if (!ci) {
		dev_err(dev, "can't allocate device\n");
		return -ENOMEM;
	}

	ci->dev = dev;
	ci->platdata = dev->platform_data;

	ret = hw_device_init(ci, base);
	if (ret < 0) {
		dev_err(dev, "can't initialize hardware\n");
		return -ENODEV;
	}

	ret = ci_usb_phy_init(ci);
	if (ret) {
		dev_err(dev, "unable to init phy: %d\n", ret);
		return ret;
	}

	ci->hw_bank.phys = res->start;

	ci->irq = platform_get_irq(pdev, 0);
	if (ci->irq < 0) {
		dev_err(dev, "missing IRQ\n");
		ret = -ENODEV;
		goto destroy_phy;
	}

	ci_get_otg_capable(ci);

	hw_phymode_configure(ci);

	dr_mode = ci->platdata->dr_mode;
	/* initialize role(s) before the interrupt is requested */
	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_HOST) {
		ret = ci_hdrc_host_init(ci);
		if (ret)
			dev_info(dev, "doesn't support host\n");
	}

	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_PERIPHERAL) {
		ret = ci_hdrc_gadget_init(ci);
		if (ret)
			dev_info(dev, "doesn't support gadget\n");
		if (!ret && ci->transceiver) {
			ret = otg_set_peripheral(ci->transceiver->otg,
							&ci->gadget);
			/*
			 * If we implement all USB functions using chipidea drivers,
			 * it doesn't need to call above API, meanwhile, if we only
			 * use gadget function, calling above API is useless.
			 */
			if (ret && ret != -ENOTSUPP)
				goto destroy_phy;
		}
	}

	if (!ci->roles[CI_ROLE_HOST] && !ci->roles[CI_ROLE_GADGET]) {
		dev_err(dev, "no supported roles\n");
		ret = -ENODEV;
		goto destroy_phy;
	}

	if (ci->is_otg) {
		ret = ci_hdrc_otg_init(ci);
		if (ret) {
			dev_err(dev, "init otg fails, ret = %d\n", ret);
			goto stop;
		}
	}

	if (ci->roles[CI_ROLE_HOST] && ci->roles[CI_ROLE_GADGET]) {
		if (ci->is_otg) {
			/*
			 * ID pin needs 1ms debouce time,
			 * we delay 2ms for safe.
			 */
			mdelay(2);
			ci->role = ci_otg_role(ci);
			ci_enable_otg_interrupt(ci, OTGSC_IDIE);
		} else {
			/*
			 * If the controller is not OTG capable, but support
			 * role switch, the defalt role is gadget, and the
			 * user can switch it through debugfs.
			 */
			ci->role = CI_ROLE_GADGET;
		}
	} else {
		ci->role = ci->roles[CI_ROLE_HOST]
			? CI_ROLE_HOST
			: CI_ROLE_GADGET;
	}

	/* only update vbus status for peripheral */
	if (ci->role == CI_ROLE_GADGET)
		ci_handle_vbus_change(ci);

	ret = ci_role_start(ci, ci->role);
	if (ret) {
		dev_err(dev, "can't start %s role\n", ci_role(ci)->name);
		goto stop;
	}

	platform_set_drvdata(pdev, ci);
	ret = request_irq(ci->irq, ci_irq, IRQF_SHARED, ci->platdata->name,
			  ci);
	if (ret)
		goto stop;

	ret = dbg_create_files(ci);
	if (!ret)
		return 0;

	free_irq(ci->irq, ci);
stop:
	ci_role_destroy(ci);
destroy_phy:
	ci_usb_phy_destroy(ci);

	return ret;
}

static int ci_hdrc_remove(struct platform_device *pdev)
{
	struct ci_hdrc *ci = platform_get_drvdata(pdev);

	dbg_remove_files(ci);
	free_irq(ci->irq, ci);
	ci_role_destroy(ci);
	ci_hdrc_enter_lpm(ci, true);
	ci_usb_phy_destroy(ci);
	kfree(ci->hw_bank.regmap);

	return 0;
}

static struct platform_driver ci_hdrc_driver = {
	.probe	= ci_hdrc_probe,
	.remove	= ci_hdrc_remove,
	.driver	= {
		.name	= "ci_hdrc",
	},
};

module_platform_driver(ci_hdrc_driver);

MODULE_ALIAS("platform:ci_hdrc");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Lopo <dlopo@chipidea.mips.com>");
MODULE_DESCRIPTION("ChipIdea HDRC Driver");
