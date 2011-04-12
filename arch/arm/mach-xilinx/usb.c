 /*
 * Xilinx PS USB Controller platform level initialization.
 *
 * Copyright (C) 2011 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/xilinx_devices.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <mach/hardware.h>

static u64 dmamask = ~(u32)0;

static struct resource xusbps_resource0[] = {
	{
		.start	= USB0_BASE,
		.end	= USB0_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_USB0,
		.end	= IRQ_USB0,
		.flags	= IORESOURCE_IRQ
	},
};

static struct resource xusbps_resource1[] = {
	{
		.start	= USB1_BASE,
		.end	= USB1_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_USB1,
		.end	= IRQ_USB1,
		.flags	= IORESOURCE_IRQ
	},
};

static struct xusbps_usb2_platform_data usb_pdata0 = {
	.operating_mode = XUSBPS_USB2_DR_OTG,
	.phy_mode       = XUSBPS_USB2_PHY_ULPI,
};

static struct xusbps_usb2_platform_data usb_pdata1 = {
	.operating_mode = XUSBPS_USB2_DR_OTG,
	.phy_mode       = XUSBPS_USB2_PHY_ULPI,
};

/* USB Host Mode */
static struct platform_device xusbps_0_host = {
	.name = "xusbps-ehci",
	.id = 0,
	.dev = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata0,
	},
};

static struct platform_device xusbps_1_host = {
	.name = "xusbps-ehci",
	.id = 1,
	.dev = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata1,
	},
};

/* USB Device Mode */
struct platform_device xusbps_0_device = {
	.name = "xusbps-udc",
	.id   = 0,
	.dev  = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata0,
	},
};

struct platform_device xusbps_1_device = {
	.name = "xusbps-udc",
	.id   = 1,
	.dev  = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata1,
	},
};

/* USB OTG Mode */
static struct platform_device xusbps_otg_0_device = {
	.name		= "xusbps-otg",
	.id		= 0,
	.dev  = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata0,
	},
	.num_resources	= ARRAY_SIZE(xusbps_resource0),
	.resource	= xusbps_resource0,
};

static struct platform_device xusbps_otg_1_device = {
	.name		= "xusbps-otg",
	.id		= 1,
	.dev  = {
		.dma_mask		= &dmamask,
		.coherent_dma_mask	= 0xFFFFFFFF,
		.platform_data		= &usb_pdata1,
	},
	.num_resources	= ARRAY_SIZE(xusbps_resource1),
	.resource	= xusbps_resource1,
};

/*-------------------------------------------------------------------------*/

int __init
xusbps_init(void)
{
	int	status;
	struct resource *res;
	void __iomem *usb0_regs;
	void __iomem *usb1_regs;
	int usb0_irq;
	int usb1_irq;

	/* Allocating resources to platform data */
	/* USB0 */
	res = platform_get_resource(&xusbps_otg_0_device, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!request_mem_region(res->start, res->end - res->start + 1,
				xusbps_otg_0_device.name))
		return -EBUSY;

	usb0_regs = ioremap(res->start, resource_size(res));
	if (!usb0_regs)
		return -ENOMEM;

	usb0_irq = platform_get_irq(&xusbps_otg_0_device, 0);

	usb_pdata0.regs = usb0_regs;
	usb_pdata0.irq = usb0_irq;

	/* USB1 */
	res = platform_get_resource(&xusbps_otg_1_device, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!request_mem_region(res->start, res->end - res->start + 1,
				xusbps_otg_1_device.name))
		return -EBUSY;

	usb1_regs = ioremap(res->start, resource_size(res));
	if (!usb0_regs)
		return -ENOMEM;

	usb1_irq = platform_get_irq(&xusbps_otg_1_device, 0);

	usb_pdata1.regs = usb1_regs;
	usb_pdata1.irq = usb1_irq;

	/* Registering platform device */

	/* usb device mode */
	/* USB0 */
	pr_info("registering platform device '%s' id %d\n",
			xusbps_0_device.name,
			xusbps_0_device.id);
	status = platform_device_register(&xusbps_0_device);
	if (status)
		pr_info("Unable to register platform device '%s': %d\n",
			xusbps_0_device.name, status);

	/* usb host mode */
#ifdef CONFIG_USB_XUSBPS_OTG
	/* USB0 */
	pr_info("registering platform device '%s' id %d\n",
			xusbps_0_host.name,
			xusbps_0_host.id);
	status = platform_device_register(&xusbps_0_host);
	if (status)
		pr_info("Unable to register platform device '%s': %d\n",
			xusbps_0_host.name, status);
#endif

	/* USB1 */
	pr_info("registering platform device '%s' id %d\n",
			xusbps_1_host.name,
			xusbps_1_host.id);
	status = platform_device_register(&xusbps_1_host);
	if (status)
		pr_info("Unable to register platform device '%s': %d\n",
			xusbps_1_host.name, status);

	/* usb otg mode */
#ifdef CONFIG_USB_XUSBPS_OTG
	/* USB0 */
	pr_info("registering platform device '%s' id %d\n",
			xusbps_otg_0_device.name,
			xusbps_otg_0_device.id);
	status = platform_device_register(&xusbps_otg_0_device);
	if (status)
		pr_info("Unable to register platform device '%s': %d\n",
			xusbps_otg_0_device.name, status);
#endif

	return 0;
}
