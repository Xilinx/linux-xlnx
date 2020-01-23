// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2018-2020 Xilinx, Inc.
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Anurag Kumar Vulisha <anuragku@xilinx.com>
 */
#ifndef __USB_CORE_XHCI_PDRIVER_H
#define __USB_CORE_XHCI_PDRIVER_H

/* Call dwc3_host_wakeup_capable() only for dwc3 DRD mode or HOST only mode */
#if (IS_REACHABLE(CONFIG_USB_DWC3_HOST) || \
		(IS_REACHABLE(CONFIG_USB_DWC3_OF_SIMPLE) && \
			!IS_REACHABLE(CONFIG_USB_DWC3_GADGET)))

/* Let the dwc3 driver know about device wakeup capability */
void dwc3_host_wakeup_capable(struct device *dev, bool wakeup);

#else
void dwc3_host_wakeup_capable(struct device *dev, bool wakeup)
{ ; }
#endif

#endif /* __USB_CORE_XHCI_PDRIVER_H */
