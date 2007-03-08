/*
 * arch/nios2nommu/kernel/usb.c -- platform level USB initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef	DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/nios.h>

#if defined(CONFIG_USB_SL811_HCD) || defined (CONFIG_USB_SL811_HCD_MODULE)
#if defined(CONFIG_MICROTRONIX_STRATIX) || defined (CONFIG_MICROTRONIX_CYCLONE)

#include <linux/usb_sl811.h>
#define SL811_ADDR ((unsigned int)na_usb)
#define SL811_IRQ na_usb_irq

static void sl811_port_power(struct device *dev, int is_on)
{
}

static 	void sl811_port_reset(struct device *dev)
{
	writeb(0xA, (SL811_ADDR+8));
	mdelay(10);
	writeb(4, (SL811_ADDR+8));
}

static struct resource sl811hs_resources[] = {
	{
		.start	= (SL811_ADDR),
		.end	= (SL811_ADDR + 3),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= (SL811_ADDR + 4),
		.end	= (SL811_ADDR + 7),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= SL811_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sl811_platform_data sl811_data = {
	.can_wakeup =	0,
	.potpg		=	0,
	.power		=	250,
	.port_power	=	sl811_port_power,
	.reset		=	sl811_port_reset,
};

static struct platform_device sl811hs_device = {
	.name			= "sl811-hcd",
	.id			= -1,
	.dev = {
		//.release		= usb_release,
		//.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0x0fffffff,
		.platform_data = &sl811_data,
	},
	.num_resources	= ARRAY_SIZE(sl811hs_resources),
	.resource		= sl811hs_resources,
};


static int __init mtx_kit_usb_init(void)
{
	int status;

	status = platform_device_register(&sl811hs_device);
	if (status) {
		pr_debug("can't register sl811hs device, %d\n", status);
		return -1;
	}
	
	writeb(4, (SL811_ADDR+8));
	return 0;
}

subsys_initcall(mtx_kit_usb_init);
#endif /* (CONFIG_MICROTRONIX_STRATIX) || (CONFIG_MICROTRONIX_CYCLONE)*/
#endif  /*(CONFIG_USB_SL811_HCD) ||(CONFIG_USB_SL811_HCD_MODULE) */

#if defined(CONFIG_USB_ISP116X_HCD) || defined (CONFIG_USB_ISP116X_HCD_MODULE)

#include <linux/usb_isp116x.h>

#define ISP116X_HCD_ADDR ((unsigned int)na_usb)
#define ISP116X_HCD_IRQ na_usb_irq

static void isp116x_delay(struct device *dev, int delay)
{
	ndelay(delay);
}

static struct resource isp116x_hcd_resources[] = {
	{
		.start	= (ISP116X_HCD_ADDR),
		.end	= (ISP116X_HCD_ADDR + 3),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= (ISP116X_HCD_ADDR + 4),
		.end	= (ISP116X_HCD_ADDR + 7),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= ISP116X_HCD_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct isp116x_platform_data isp116x_data = {
		// Enable internal resistors on downstream ports
        .sel15Kres              = 0,
        // Clock cannot be stopped
        .clknotstop             = 1,
        // On-chip overcurrent protection
        .oc_enable              = 0,
        // INT output polarity
        .int_act_high           = 0,
        // INT edge or level triggered
        .int_edge_triggered     = 0,
        // End-of-transfer input polarity
        .eot_act_high           = 0,
        // DREQ output polarity
        .dreq_act_high          = 1,
        // WAKEUP pin connected
        .remote_wakeup_connected= 0,
        // Wakeup by devices on usb bus enabled
        .remote_wakeup_enable   = 0,
        // Switch or not to switch (keep always powered)
        .no_power_switching     = 1,
        // Ganged port power switching (0) or individual port power switching (1)
        .power_switching_mode   = 0,
        .reset                  = NULL /* isp116x_reset */,
        .delay                  = isp116x_delay,
};

static struct platform_device isp116x_hcd = {
	.name			= "isp116x-hcd",
	.id			= -1,
	.dev = {
		//.release		= usb_release,
		//.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0x0fffffff,
		.platform_data = &isp116x_data,
	},
	.num_resources	= ARRAY_SIZE(isp116x_hcd_resources),
	.resource		= isp116x_hcd_resources,
};

static int __init usb_hcd_init(void)
{
	int status;

	status = platform_device_register(&isp116x_hcd);
	if (status) {
		pr_debug("can't register isp116x host controller, %d\n", status);
		return -1;
	}
	
	return 0;
}
subsys_initcall(usb_hcd_init);
#endif  /*(CONFIG_USB_ISP116X_HCD) ||(CONFIG_USB_ISP116X_HCD_MODULE) */

#if defined(CONFIG_USB_ISP1161X) || defined(CONFIG_USB_ISP1161X_MODULE)
#include <linux/usb_isp116x_dc.h>

#define ISP116X_UDC_ADDR ((unsigned int)na_usb+8)
#define ISP116X_UDC_IRQ	 na_int2_usb_irq

static struct resource isp116x_udc_resources[] = {
	{
		.start	= (ISP116X_UDC_ADDR),
		.end	= (ISP116X_UDC_ADDR + 3),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= (ISP116X_UDC_ADDR + 4),
		.end	= (ISP116X_UDC_ADDR + 7),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= ISP116X_UDC_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};
static void isp116x_udc_delay()
{
	__asm__ __volatile__(
        "1:  \n\t"
        "    beq    %0,zero,2f\n\t"
        "    addi   %0, %0, -1\n\t" 
        "    br     1b\n\t" 
        "2:  \n\t" 
        :
        :  "r" (nasys_clock_freq_1000 * 180 / 2000000)
        );
	
}
struct isp116x_dc_platform_data isp116x_udc_data = {
	.ext_pullup_enable	=0,
	.no_lazy			=1,
	.eot_act_high		=0,
	.remote_wakeup_enable=1,
	.power_off_enable	=1,
	.int_edge_triggered	=0,
	.int_act_high		=0,
	.clkout_freq		=12,
	.delay				= isp116x_udc_delay
};

static struct platform_device isp116x_udc = {
	.name			= "isp1161a_udc",
	.id			= -1,
	.dev = {
		//.release		= usb_release,
		//.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0x0fffffff,
		.platform_data = &isp116x_udc_data,
	},
	.num_resources	= ARRAY_SIZE(isp116x_udc_resources),
	.resource		= isp116x_udc_resources,
};

static int __init usb_udc_init(void)
{
	int status;
	np_pio* pio;

	status = platform_device_register(&isp116x_udc);
	if (status) {
		pr_debug("can't register isp116x device controller, %d\n", status);
		return -1;
	}
	
	/* enable interrupts */
	pio = (np_pio*)na_int2_usb;
	//outw(0, &pio->np_piodata);
	//outw(0, &pio->np_pioedgecapture);
	outw(1, &pio->np_piointerruptmask);
	
	return 0;
}
subsys_initcall(usb_udc_init);
#endif

#if defined(na_ISP1362_avalonS)  // for DE2 dev board, FIX ME otehrwise
#define na_usb na_ISP1362_avalonS
#define na_usb_irq na_ISP1362_avalonS_irq
#endif

#if defined(na_ISP1362_avalon_slave_0)  // for DE2 dev board, FIX ME otehrwise
#define na_usb na_ISP1362_avalon_slave_0
#define na_usb_irq na_ISP1362_avalon_slave_0_irq
#endif

#if defined(CONFIG_USB_ISP1362_HCD) && defined(na_usb)

#include <linux/usb_isp1362.h>
#define ISP1362_HCD_ADDR ((unsigned int)na_usb)
#define ISP1362_HCD_IRQ na_usb_irq

static struct resource isp1362_hcd_resources[] = {
	{
		.start	= (ISP1362_HCD_ADDR),
		.end	= (ISP1362_HCD_ADDR + 3),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= (ISP1362_HCD_ADDR + 4),
		.end	= (ISP1362_HCD_ADDR + 7),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= ISP1362_HCD_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct isp1362_platform_data isp1362_data = {
		// Enable internal resistors on downstream ports
	.sel15Kres	= 1,
        // Clock cannot be stopped
	.clknotstop	= 0,
        // On-chip overcurrent protection
	.oc_enable	= 0,
        // INT output polarity
	.int_act_high	= 0,
        // INT edge or level triggered
	.int_edge_triggered	= 0,
        // WAKEUP pin connected
 	.remote_wakeup_connected	= 0,
        // Switch or not to switch (keep always powered)
	.no_power_switching	= 1,
        // Ganged port power switching (0) or individual port power switching (1)
	.power_switching_mode	= 0,
};

static struct platform_device isp1362_hcd = {
	.name			= "isp1362-hcd",
	.id			= -1,
	.dev = {
		//.release		= usb_release,
		//.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0x0fffffff,
		.platform_data = &isp1362_data,
	},
	.num_resources	= ARRAY_SIZE(isp1362_hcd_resources),
	.resource		= isp1362_hcd_resources,
};

static int __init usb_hcd_init(void)
{
	int status;

	status = platform_device_register(&isp1362_hcd);
	if (status) {
		pr_debug("can't register isp1362 host controller, %d\n", status);
		return -1;
	}
	
	return 0;
}
subsys_initcall(usb_hcd_init);
#endif

