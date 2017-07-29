/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
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
