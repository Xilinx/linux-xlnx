/***************************************************************************
 * usb-mcf532x.c - Platform level (mcf532x) USB initialization.
 *
 * Andrey Butok Andrey.Butok@freescale.com.
 * Copyright Freescale Semiconductor, Inc 2006
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ***************************************************************************
 * Changes:
 *   v0.01	31 March 2006	Andrey Butok
 *   		Initial Release - developed on uClinux with 2.6.15.6 kernel
 *
 * WARNING: The MCF532x USB functionality was tested
 *          only with low-speed USB devices (cause of HW bugs).
 */

#undef	DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>

/* Start address of HC registers.*/
#define MCF532x_USB_HOST_REG_START	(0xfc0b4000)
/* End address of HC registers */
#define MCF532x_USB_HOST_REG_END	(MCF532x_USB_HOST_REG_START+0x200)
/* USB Host Interrupt number */
#define MCF532x_USB_HOST_INT_NUMBER	(128+48)

#ifdef CONFIG_USB_OTG
/* Start address of OTG module registers.*/
#define MCF532x_USB_OTG_REG_START	(0xfc0b0000)
/* End address of OTG module registers */
#define MCF532x_USB_OTG_REG_END		(MCF532x_USB_OTG_REG_START+0x200)
/* USB OTG Interrupt number */
#define MCF532x_USB_OTG_INT_NUMBER	(128+47)
#endif

/*-------------------------------------------------------------------------*/

static void
usb_release(struct device *dev)
{
	/* normally not freed */
}

/*
 * USB Host module structures
 */
static struct resource ehci_host_resources[] = {
	{
	 .start = MCF532x_USB_HOST_REG_START,
	 .end = MCF532x_USB_HOST_REG_END,
	 .flags = IORESOURCE_MEM,
	 },
	{
	 .start = MCF532x_USB_HOST_INT_NUMBER,
	 .flags = IORESOURCE_IRQ,
	 },
};

static struct platform_device ehci_host_device = {
	.name = "ehci",
	.id = 1,
	.dev = {
		.release = usb_release,
		.dma_mask = 0x0},
	.num_resources = ARRAY_SIZE(ehci_host_resources),
	.resource = ehci_host_resources,
};

/*
 * USB OTG module structures.
 */
#ifdef CONFIG_USB_OTG
static struct resource ehci_otg_resources[] = {
	{
	 .start = MCF532x_USB_OTG_REG_START,
	 .end = MCF532x_USB_OTG_REG_END,
	 .flags = IORESOURCE_MEM,
	 },
	{
	 .start = MCF532x_USB_OTG_INT_NUMBER,
	 .flags = IORESOURCE_IRQ,
	 },
};

static struct platform_device ehci_otg_device = {
	.name = "ehci",
	.id = 0,
	.dev = {
		.release = usb_release,
		.dma_mask = 0x0},
	.num_resources = ARRAY_SIZE(ehci_otg_resources),
	.resource = ehci_otg_resources,
};
#endif

typedef volatile u8		vuint8;  /*  8 bits */

static int __init
mcf532x_usb_init(void)
{
	int status;

	/*
	 * Initialize the clock divider for the USB:
	 */
#ifdef CONFIG_CLOCK_240MHz
	/*
	 * CPU oerating on 240Mhz (MISCCR[USBDIV]=1)
	 */
	(*(volatile u16 *) (0xFC0A0010)) |= (0x0002);
#elif defined(CONFIG_CLOCK_180MHz)
	/*
	 * CPU oerating on 180Mhz (MISCCR[USBDIV]=0)
	 */
	(*(volatile u16 *) (0xFC0A0010)) |= ~(0x0002);
#else
#error "CONFIG_CLOCK_240MHz or CONFIG_CLOCK_180MHz must be defined for MCF532x."
#endif
	/*
	 * Register USB Host device:
	 */
	status = platform_device_register(&ehci_host_device);
	if (status) {
		pr_info
		    ("USB-MCF532x: Can't register MCF532x USB Host device, %d\n",
		     status);
		return -ENODEV;
	}
	pr_info("USB-MCF532x: MCF532x USB Host device is registered\n");

#ifdef CONFIG_USB_OTG
	/*
	 *  Register USB OTG device:
	 *  Done only USB Host.
	 *  TODO: Device and OTG functinality.
	 */
	status = platform_device_register(&ehci_otg_device);
	if (status) {
		pr_info
		    ("USB-MCF532x: Can't register MCF532x USB OTG device, %d\n",
		     status);
		return -ENODEV;
	}
	pr_info("USB-MCF532x: MCF532x USB OTG device is registered\n");
#endif

	return 0;
}

subsys_initcall(mcf532x_usb_init);
