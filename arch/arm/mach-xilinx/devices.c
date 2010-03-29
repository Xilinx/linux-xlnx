/* arch/arm/mach-xilinx/devices.c
 *
 * Copyright (C) 2009 Xilinx
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/xilinx_devices.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/phy.h>
#include <mach/hardware.h>
#include <mach/dma.h>
#include <linux/spi/spi.h>

/* Create all the platform devices for the BSP */

static struct resource uart0[] = {
	{
		.start = UART0_BASE,
		.end = UART0_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_UART0,
		.end = IRQ_UART0,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned long uart_clk = 50000000;

struct platform_device uart_device0 = {
	.name = "xuartpss",
	.id = 0,
	.dev = {
		.platform_data = &uart_clk,
	},
	.resource = uart0,
	.num_resources = ARRAY_SIZE(uart0),
};

static struct resource uart1[] = {
	{
		.start = UART1_BASE,
		.end = UART1_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_UART1,
		.end = IRQ_UART1,
		.flags = IORESOURCE_IRQ,
	},
};

static u64 dma_mask = 0xFFFFFFFFUL;

struct platform_device uart_device1 = {
	.name = "xuartpss",
	.id = 1,
	.dev = {
		.platform_data = &uart_clk,
	},
	.resource = uart1,
	.num_resources = ARRAY_SIZE(uart1),
};

static struct resource dmac0[] = {
	{
		.start = DMAC0_BASE,
		.end = DMAC0_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_DMAC0,
		.end = IRQ_DMAC0 + 4,
		.flags = IORESOURCE_IRQ,
	},
};

struct pl330_platform_config dmac_config0 = {
	.channels = 4,
	.starting_channel = 0,
};

struct platform_device dmac_device0 = {
	.name = "pl330",
	.id = 0,
	.dev = {
		.platform_data = &dmac_config0,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = dmac0,
	.num_resources = ARRAY_SIZE(dmac0),
};

static struct resource dmac1[] = {
	{
		.start = DMAC1_BASE,
		.end = DMAC1_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_DMAC1,
		.end = IRQ_DMAC1 + 4,
		.flags = IORESOURCE_IRQ,
	},
};

struct pl330_platform_config dmac_config1 = {
	.channels = 4,
	.starting_channel = 4,
};

struct platform_device dmac_device1 = {
	.name = "pl330",
	.id = 1,
	.dev = {
		.platform_data = &dmac_config1,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = dmac1,
	.num_resources = ARRAY_SIZE(dmac1),
};

/*************************PSS I2C***********************/
static struct xi2cpss_platform_data xi2cpss_pdata = {
	.input_clk = 50000000,
	.i2c_clk = 100000,
};

static struct resource xi2cpss_0_resource[] = {
	{
		.start = I2C0_BASE,
		.end = I2C0_BASE + 0x00FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_I2C0,
		.end = IRQ_I2C0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device xilinx_i2cpss_0_device = {
	.name = "XILINX_PSS_I2C",
	.id = 0,
	.dev = {
		.platform_data = &xi2cpss_pdata,
	},
	.resource = xi2cpss_0_resource,
	.num_resources = ARRAY_SIZE(xi2cpss_0_resource),
};

/*************************PSS GPIO*********************/
static struct resource xgpiopss_0_resource[] = {
	{
		.start	= GPIO0_BASE,
		.end	= GPIO0_BASE + 0x0FFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_GPIO0,
		.end	= IRQ_GPIO0,
		.flags	= IORESOURCE_IRQ
	},
};
struct platform_device xilinx_gpiopss_0_device = {
	.name = "xilinx_gpiopss",
	.id = 0,
	.dev.platform_data = NULL,
	.resource =  xgpiopss_0_resource,
	.num_resources = ARRAY_SIZE(xgpiopss_0_resource),
};

/*************************PSS NOR***********************/
int flash_width = 2;
static struct resource xnorpss_0_resource[] = {
	{
		.start	= NOR_BASE,
		.end	= NOR_BASE + 0xFFFFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= SMC_BASE,
		.end	= SMC_BASE + 0xFF,
		.flags	= IORESOURCE_MEM
	},
};
struct platform_device xilinx_norpss_device = {
	.name = "xnorpss",
	.id = 0,
	.dev = {
		.platform_data = &flash_width,
	},
	.resource =  xnorpss_0_resource,
	.num_resources = ARRAY_SIZE(xnorpss_0_resource),
};

#define ETH0_PHY_MASK 7
#define ETH1_PHY_MASK 24

struct xemacpss_eth_data {
	u32 phy_mask;
};

static struct xemacpss_eth_data __initdata eth0_data = {
	.phy_mask = ~(1U << ETH0_PHY_MASK),
};

/* eth1 will be available in EP5 and up, comment out for now. */
#ifdef EP5_AND_UP
static struct xemacpss_eth_data __initdata eth1_data = {
	.phy_mask = ~(1U << ETH1_PHY_MASK),
};
#endif

static struct resource eth0[] = {
	{
		.start = ETH0_BASE,
		.end   = ETH0_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_ETH0,
		.end   = IRQ_ETH0,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device eth_device0 = {
	.name = "xemacpss",
	.id = 0,
	.dev = {
		.dma_mask          = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
		.platform_data     = &eth0_data,
	},
	.resource = eth0,
	.num_resources = ARRAY_SIZE(eth0),
};

#ifdef EP5_AND_UP
static struct resource eth1[] = {
	{
		.start = ETH1_BASE,
		.end   = ETH1_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_ETH1,
		.end   = IRQ_ETH1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device eth_device1 = {
	.name = "xemacpss",
	.id = 1,
	.dev = {
		.dma_mask          = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
		.platform_data     = &eth1_data,
	},
	.resource = eth1,
	.num_resources = ARRAY_SIZE(eth1),
};
#endif

/*************************PSS SPI*********************/

static struct xspi_platform_data xspi_0_pdata = {
	.speed_hz = 50000000,
	.bus_num = 0,
	.num_chipselect = 4
};

#ifdef CONFIG_SPI_SPIDEV

static struct spi_board_info __initdata xilinx_spipss_0_boardinfo = {
	.modalias		= "spidev",
	.platform_data		= &xspi_0_pdata,
	.irq			= IRQ_SPI0,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 0,
	.chip_select		= 0,
};

#endif

static struct resource xspipss_0_resource[] = {
	{
		.start	= SPI0_BASE,
		.end	= SPI0_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_SPI0,
		.end	= IRQ_SPI0,
		.flags	= IORESOURCE_IRQ
	},
};

static struct platform_device xilinx_spipss_0_device = {
	.name = "Xilinx_PSS_SPI",
	.id = 0,
	.dev = {
		.platform_data = &xspi_0_pdata,
	},
	.resource = xspipss_0_resource,
	.num_resources = ARRAY_SIZE(xspipss_0_resource),
};

/*************************PSS WDT*********************/
static struct resource xwdtpss_0_resource[] = {
	{
		.start	= WDT0_BASE,
		.end	= WDT0_BASE + 0x00FF,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device xilinx_wdtpss_0_device = {
	.name = "xilinx_pss_wdt",
	.id = 0,
	.dev = {
		.platform_data = NULL,
	},
	.resource = xwdtpss_0_resource,
	.num_resources = ARRAY_SIZE(xwdtpss_0_resource),
};

/*************************Private WDT***********************/
static struct resource xa9wdt_resource[] = {
	{
		.start	= SCU_PWDT_BASE,
		.end	= SCU_PWDT_BASE + 0x20,
		.flags	= IORESOURCE_MEM
	},
};
struct platform_device xilinx_a9wdt_device = {
	.name = "xilinx_a9wdt",
	.id = 0,
	.dev = {
		.platform_data = NULL,
	},
	.resource =  xa9wdt_resource,
	.num_resources = ARRAY_SIZE(xa9wdt_resource),
};

/* add all platform devices to the following table so they
 * will be registered
 */
struct platform_device *xilinx_pdevices[] __initdata = {
	&uart_device0,
	&uart_device1,
	&dmac_device0,
	&dmac_device1,
	&xilinx_i2cpss_0_device,
	&xilinx_gpiopss_0_device,
	&xilinx_norpss_device,
	&eth_device0,
#ifdef EP5_AND_UP
	&eth_device1,
#endif
	&xilinx_spipss_0_device,
	&xilinx_wdtpss_0_device,
	&xilinx_a9wdt_device,
};

/**
 * platform_device_init - Initialize all the platform devices.
 *
 **/
void __init platform_device_init(void)
{
	int ret, i;

	ret = 0;

	/* Initialize all the platform devices */

	for (i = 0; i < ARRAY_SIZE(xilinx_pdevices); i++) {
		pr_info("registering platform device '%s' id %d\n",
			xilinx_pdevices[i]->name,
			xilinx_pdevices[i]->id);
		ret = platform_device_register(xilinx_pdevices[i]);
		if (ret)
			pr_info("Unable to register platform device '%s': %d\n",
				xilinx_pdevices[i]->name, ret);
#ifdef CONFIG_SPI_SPIDEV
		else if (&xilinx_spipss_0_device == xilinx_pdevices[i])
			spi_register_board_info(&xilinx_spipss_0_boardinfo, 1);
#endif

	}
}

