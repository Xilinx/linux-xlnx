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
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <mach/nand.h>
#include <linux/amba/xilinx_dma.h>

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

/*************************PSS DMA***********************/
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
		.start = IRQ_DMAC0_ABORT,
		.end = IRQ_DMAC0_ABORT,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DMAC0,
		.end = IRQ_DMAC0 + 3,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DMAC3,
		.end = IRQ_DMAC3 + 3,
		.flags = IORESOURCE_IRQ,
	},
};

struct pl330_platform_config dmac_config0 = {
	.channels = 8,
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


#ifdef CONFIG_XILINX_TEST

static struct platform_device xilinx_dma_test = {
	.name = "pl330_test",
	.id = 0,
	.dev = {
		.platform_data = NULL,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = NULL,
	.num_resources = 0,
};

#endif

/*************************AXI CDMA***********************/
/* There is a single driver for all AXI DMA cores. Note
 * the name of the driver for all is xilinx-axidma. The
 * following platform data sort of mimics the device
 * tree that is used with MicroBlaze Linux. The user needs
 * to setup the resources and configurations for each
 * core. Once we have device tree on ARM this will all
 * go away and be much easier.
 */

//#define AXI_CDMA
#ifdef AXI_CDMA

#define AXI_CDMA_BASE	0x44600000
#define AXI_CDMA_IRQ0	91

static struct resource cdma_resources[] = {
	{
		.start = AXI_CDMA_BASE,
		.end = AXI_CDMA_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = AXI_CDMA_IRQ0,
		.end = AXI_CDMA_IRQ0,
		.flags = IORESOURCE_IRQ,
	},
};

struct dma_channel_config cdma_channel_config[] = {
	{
		.type = "axi-cdma",
		.lite_mode = 0,		/* must use 128 test length, no dre */
		.include_dre = 1,
		.datawidth = 64,
		.max_burst_len = 16,
	},
};

struct dma_device_config cdma_device_config = {
	.type = "axi-cdma",
	.include_sg = 1,
	.channel_count = 1,
	.channel_config = &cdma_channel_config[0],
};

struct platform_device axicdma_device = {
	.name = "xilinx-axidma",
	.id = 0,
	.dev = {
		.platform_data = &cdma_device_config,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = cdma_resources,
	.num_resources = ARRAY_SIZE(cdma_resources),
};

#endif

/*************************AXI VDMA***********************/

//#define AXI_VDMA
#ifdef AXI_VDMA

#define AXI_VDMA_BASE	0x40000000
#define AXI_VDMA_IRQ0	91
#define AXI_VDMA_IRQ1	90

static struct resource vdma_resources[] = {
	{
		.start = AXI_VDMA_BASE,
		.end = AXI_VDMA_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = AXI_VDMA_IRQ0,
		.end = AXI_VDMA_IRQ0,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = AXI_VDMA_IRQ1,
		.end = AXI_VDMA_IRQ1,
		.flags = IORESOURCE_IRQ,
	},
};

struct dma_channel_config vdma_channel_config[] = {
	{
		.type = "axi-vdma-mm2s-channel",
		.include_dre = 0,
		.genlock_mode = 0,
		.datawidth = 64,
		.max_burst_len = 256,
	},
	{
		.type = "axi-vdma-s2mm-channel",
		.include_dre = 0,
		.genlock_mode = 0,
		.datawidth = 64,
		.max_burst_len = 256,
	},
};

struct dma_device_config vdma_device_config = {
	.type = "axi-vdma",
	.include_sg = 1,
	.num_fstores = 3,
	.channel_count = 2,
	.channel_config = &vdma_channel_config[0],
};

struct platform_device axivdma_device = {
	.name = "xilinx-axidma",
	.id = 0,
	.dev = {
		.platform_data = &vdma_device_config,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = vdma_resources,
	.num_resources = ARRAY_SIZE(vdma_resources),
};

#endif

/*************************AXI DMA***********************/

// #define AXI_DMA
#ifdef AXI_DMA

#define AXI_DMA_BASE	0x40000000
#define AXI_DMA_IRQ0	91
#define AXI_DMA_IRQ1	90

static struct resource dma_resources[] = {
	{
		.start = AXI_DMA_BASE,
		.end = AXI_DMA_BASE + 0xFFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = AXI_DMA_IRQ0,
		.end = AXI_DMA_IRQ0,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = AXI_DMA_IRQ1,
		.end = AXI_DMA_IRQ1,
		.flags = IORESOURCE_IRQ,
	},
};

struct dma_channel_config dma_channel_config[] = {
	{
		.type = "axi-dma-mm2s-channel",
		.include_dre = 0,		/* DRE not working yet */
		.datawidth = 64,
	},
	{
		.type = "axi-dma-s2mm-channel",
		.include_dre = 0,		/* DRE not working yet */
		.datawidth = 64,
	},
};

struct dma_device_config dma_device_config = {
	.type = "axi-dma",
	.include_sg = 1,
	.sg_include_stscntrl_strm = 1,
	.channel_count = 2,
	.channel_config = &dma_channel_config[0],
};

struct platform_device axidma_device = {
	.name = "xilinx-axidma",
	.id = 0,
	.dev = {
		.platform_data = &dma_device_config,
		.dma_mask = &dma_mask,
		.coherent_dma_mask = 0xFFFFFFFF,
	},
	.resource = dma_resources,
	.num_resources = ARRAY_SIZE(dma_resources),
};

#endif

/*************************PSS I2C***********************/
static struct xi2cpss_platform_data xi2cpss_0_pdata = {
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
		.platform_data = &xi2cpss_0_pdata,
	},
	.resource = xi2cpss_0_resource,
	.num_resources = ARRAY_SIZE(xi2cpss_0_resource),
};

static struct xi2cpss_platform_data xi2cpss_1_pdata = {
	.input_clk = 50000000,
	.i2c_clk = 100000,
};

static struct resource xi2cpss_1_resource[] = {
	{
		.start = I2C1_BASE,
		.end = I2C1_BASE + 0x00FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_I2C1,
		.end = IRQ_I2C1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device xilinx_i2cpss_1_device = {
	.name = "XILINX_PSS_I2C",
	.id = 1,
	.dev = {
		.platform_data = &xi2cpss_1_pdata,
	},
	.resource = xi2cpss_1_resource,
	.num_resources = ARRAY_SIZE(xi2cpss_1_resource),
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

/*************************AXI GPIO*********************/
/*
 * Platform data for AXI GPIO soft IP.
 * Users need to update this data based on the system configuration.
 * This info will not be required when we have device-tree support for ARM.
 */
// #define AXI_GPIO

#ifdef AXI_GPIO

#define AXI_GPIO_0_BASE		0x40000000
#define AXI_GPIO_0_DOUT_DEFAULT	0x00000000
#define AXI_GPIO_0_TRI_DEFAULT	0xFFFFF7FF
#define AXI_GPIO_0_WIDTH	32

static struct xgpio_platform_data xilinx_gpio_0_data = {
	.state	= AXI_GPIO_0_DOUT_DEFAULT,
	.dir	= AXI_GPIO_0_TRI_DEFAULT,
	.width	= AXI_GPIO_0_WIDTH,
};

static struct resource xgpio_0_resource[] = {
	{
		.start	= AXI_GPIO_0_BASE,
		.end	= AXI_GPIO_0_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
};
struct platform_device xilinx_gpio_0_device = {
	.name = "xilinx_gpio",
	.id = 0,
	.dev.platform_data = &xilinx_gpio_0_data,
	.resource =  xgpio_0_resource,
	.num_resources = ARRAY_SIZE(xgpio_0_resource),
};

#endif

/*************************PSS NOR***********************/
static struct physmap_flash_data xilinx_norpss_data = {
	.width		= 1, /* operating width of the flash */
};

static struct resource xnorpss_0_resource[] = {
	{
		.start	= NOR_BASE,
		.end	= NOR_BASE + SZ_32M - 1,
		.flags	= IORESOURCE_MEM
	},
};
struct platform_device xilinx_norpss_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &xilinx_norpss_data,
	},
	.resource =  xnorpss_0_resource,
	.num_resources = ARRAY_SIZE(xnorpss_0_resource),
};

/*************************PSS NAND ***********************/
#include <linux/mtd/nand.h>

static struct mtd_partition nand_flash_partitions[] = {
	{
		.name		= "nand-fsbl",
		.size		= 0x100000,	/* 1MB */
		.offset		= 0,
	},
	{
		.name		= "nand-u-boot",
		.size		= 0x100000,	/* 1MB */
		.offset		= 0x100000,
	},
	{
		.name		= "nand-linux",
		.size		= 0x500000,	/* 5MB */
		.offset		= 0x200000,
	},
	{
		.name		= "nand-user",
		.size		= 0x100000,	/* 1MB */
		.offset		= 0x700000,
	},
	{
		.name		= "nand-scratch",
		.size		= 0x100000,	/* 1MB */
		.offset		= 0x800000,
	},
	{
		.name		= "nand-rootfs",
		.size		= 0x8000000,	/* 128MB */
		.offset		= 0x900000,
	},
	{
		.name		= "nand-bitstreams",
		.size		= 0x7700000,	/* 119MB */
		.offset		= 0x8900000,
	},
};

static struct xnand_platform_data xilinx_nand_pdata = {
	.options = NAND_NO_AUTOINCR | NAND_USE_FLASH_BBT,
	.parts = &nand_flash_partitions,
	.nr_parts = ARRAY_SIZE(nand_flash_partitions),
};

static struct resource xnand_res[] = {
	{
		.start  = NAND_BASE,
		.end    = NAND_BASE + 0xFFFFFF,
		.flags  = IORESOURCE_MEM
	},
	{
		.start  = SMC_BASE,
		.end    = SMC_BASE + 0xFFF,
		.flags  = IORESOURCE_MEM
	},
};

struct platform_device xilinx_nandpss_device = {
	.name = "Xilinx_PSS_NAND",
	.id = 0,
	.dev = {
		.platform_data = &xilinx_nand_pdata,
	},
	.num_resources = ARRAY_SIZE(xnand_res),
	.resource = xnand_res,
};

/*************************PSS SDIO ***********************/
static struct resource xsdio0_res[] = {
	{
		.start  = SDIO0_BASE,
		.end    = SDIO0_BASE + 0xFFF,
		.flags  = IORESOURCE_MEM
	},
	{
		.start  = SDIO0_IRQ,
		.end    = SDIO0_IRQ,
		.flags  = IORESOURCE_IRQ
	},
};

struct platform_device xilinx_sdio0pss_device = {
	.name = "sdhci",
	.id = 0,
	.dev.platform_data = NULL,
	.num_resources = ARRAY_SIZE(xsdio0_res),
	.resource = xsdio0_res,
};

static struct resource xsdio1_res[] = {
	{
		.start  = SDIO1_BASE,
		.end    = SDIO1_BASE + 0xFFF,
		.flags  = IORESOURCE_MEM
	},
	{
		.start  = SDIO1_IRQ,
		.end    = SDIO1_IRQ,
		.flags  = IORESOURCE_IRQ
	},
};

struct platform_device xilinx_sdio1pss_device = {
	.name = "sdhci",
	.id = 1,
	.dev.platform_data = NULL,
	.num_resources = ARRAY_SIZE(xsdio1_res),
	.resource = xsdio1_res,
};

#define ETH0_PHY_MASK 0x17
#define ETH1_PHY_MASK 0x10

struct xemacpss_eth_data {
	u32 phy_mask;
};

static struct xemacpss_eth_data __initdata eth0_data = {
	.phy_mask = ~(1U << ETH0_PHY_MASK),
};

static struct xemacpss_eth_data __initdata eth1_data = {
	.phy_mask = ~(1U << ETH1_PHY_MASK),
};

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

/*************************PSS SPI*********************/

static struct xspi_platform_data xspi_0_pdata = {
	.speed_hz = 50000000,
	.bus_num = 0,
	.num_chipselect = 4
};

static struct xspi_platform_data xspi_1_pdata = {
	.speed_hz = 50000000,
	.bus_num = 1,
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

static struct spi_board_info __initdata xilinx_spipss_1_boardinfo = {
	.modalias		= "spidev",
	.platform_data		= &xspi_1_pdata,
	.irq			= IRQ_SPI1,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 1,
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

static struct resource xspipss_1_resource[] = {
	{
		.start	= SPI1_BASE,
		.end	= SPI1_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_SPI1,
		.end	= IRQ_SPI1,
		.flags	= IORESOURCE_IRQ
	},
};

static struct platform_device xilinx_spipss_1_device = {
	.name = "Xilinx_PSS_SPI",
	.id = 1,
	.dev = {
		.platform_data = &xspi_1_pdata,
	},
	.resource = xspipss_1_resource,
	.num_resources = ARRAY_SIZE(xspipss_1_resource),
};

/*************************PSS QSPI*********************/
static struct xspi_platform_data xqspi_0_pdata = {
	.speed_hz = 100000000,
	.bus_num = 2,
	.num_chipselect = 1
};

#ifdef CONFIG_SPI_SPIDEV

static struct spi_board_info __initdata xilinx_qspipss_0_boardinfo = {
	.modalias		= "spidev",
	.platform_data		= &xqspi_0_pdata,
	.irq			= IRQ_QSPI0,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 2,
	.chip_select		= 0,
};

#elif defined CONFIG_MTD_M25P80

static struct mtd_partition qspi_flash_partitions[] = {
	{
		.name		= "qpsi-fsbl",
		.size		= 0x80000,
		.offset		= 0,
	},
	{
		.name		= "qpsi-u-boot",
		.size		= 0x80000,
		.offset		= 0x80000,
	},
	{
		.name		= "qpsi-linux",
		.size		= 0x500000,
		.offset		= 0x100000,
	},
	{
		.name		= "qpsi-user",
		.size		= 0x100000,
		.offset		= 0x600000,
	},
	{
		.name		= "qpsi-scratch",
		.size		= 0x100000,
		.offset		= 0x700000,
	},
	{
		.name		= "qpsi-rootfs",
#ifdef CONFIG_XILINX_PSS_QSPI_USE_DUAL_FLASH
		.size		= 0x1800000,
#else
		.size		= 0x800000,
#endif
		.offset		= 0x800000,
	},
};

static struct flash_platform_data qspi_flash_pdata = {
	.name			= "serial_flash",
	.parts			= qspi_flash_partitions,
	.nr_parts		= ARRAY_SIZE(qspi_flash_partitions),
#ifdef CONFIG_XILINX_PSS_QSPI_USE_DUAL_FLASH
	.type			= "n25q128x2"	/* dual flash devices */
#else
	.type			= "n25q128"	/* single flash device */
#endif
};

static struct spi_board_info __initdata xilinx_qspipss_0_boardinfo = {
	.modalias		= "m25p80",
	.platform_data		= &qspi_flash_pdata,
	.irq			= IRQ_QSPI0,
	.max_speed_hz		= 50000000, /* max sample rate at 3V */
	.bus_num		= 2,
	.chip_select		= 0,
};

#endif

static struct resource xqspipss_0_resource[] = {
	{
		.start	= QSPI0_BASE,
		.end	= QSPI0_BASE + 0xFFF,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= IRQ_QSPI0,
		.end	= IRQ_QSPI0,
		.flags	= IORESOURCE_IRQ
	},
};

static struct platform_device xilinx_qspipss_0_device = {
	.name = "Xilinx_PSS_QSPI",
	.id = 0,
	.dev = {
		.platform_data = &xqspi_0_pdata,
	},
	.resource = xqspipss_0_resource,
	.num_resources = ARRAY_SIZE(xqspipss_0_resource),
};

/*************************PSS WDT*********************/
static struct resource xwdtpss_0_resource[] = {
	{
		.start	= WDT_BASE,
		.end	= WDT_BASE + 0x00FF,
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
		.start	= SCU_WDT_BASE,
		.end	= SCU_WDT_BASE + 0x20,
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

/************************* SLCR ***********************/
static struct resource xslcr_res[] = {
	{
		.start  = SLC_REG,
		.end    = SLC_REG + 0xFFF,
		.flags  = IORESOURCE_MEM
	},
};

struct platform_device xilinx_slcr_device = {
	.name = "xilinx_slcr",
	.id = 0,
	.dev.platform_data = NULL,
	.num_resources = ARRAY_SIZE(xslcr_res),
	.resource = xslcr_res,
};

/************************* Device Config ***********************/
static struct resource xdevcfg_resource[] = {
        {
                .start  = DVC_BASE,
                .end    = DVC_BASE + 0x7FFF,
                .flags  = IORESOURCE_MEM
        },
        {
                .start  = IRQ_DVC,
                .end    = IRQ_DVC,
                .flags  = IORESOURCE_IRQ
        },

};
struct platform_device xilinx_devcfg_device = {
        .name = "xdevcfg",
        .id = 0,
        .dev = {
                .platform_data = NULL,
        },
        .resource =  xdevcfg_resource,
        .num_resources = ARRAY_SIZE(xdevcfg_resource),
};

/* add all platform devices to the following table so they
 * will be registered, create seperate lists for AMP on each
 * CPU so that they don't try to use the same devices
 */
struct platform_device *xilinx_pdevices[] __initdata = {
#ifndef CONFIG_OF
	&uart_device0,
	&uart_device1,
#endif
#ifdef AXI_DMA
	&axidma_device,
#endif
#ifdef AXI_CDMA
	&axicdma_device,
#endif
#ifdef AXI_VDMA
	&axivdma_device,
#endif
	&dmac_device0,
	/* &dmac_device1, */
#ifdef CONFIG_XILINX_TEST
	&xilinx_dma_test,
#endif
	&xilinx_i2cpss_0_device,
	&xilinx_i2cpss_1_device,
	&xilinx_gpiopss_0_device,
#ifdef AXI_GPIO
	&xilinx_gpio_0_device,
#endif
	&xilinx_norpss_device,
	&eth_device0,
	&eth_device1,
	&xilinx_spipss_0_device,
	&xilinx_spipss_1_device,
	&xilinx_qspipss_0_device,
	&xilinx_wdtpss_0_device,
	&xilinx_a9wdt_device,
	&xilinx_nandpss_device,
	&xilinx_sdio0pss_device,
	&xilinx_sdio1pss_device,
	&xilinx_slcr_device,
        &xilinx_devcfg_device,
};

struct platform_device *xilinx_pdevices_amp0[] __initdata = {
	&uart_device0,
	&dmac_device0,
	&xilinx_i2cpss_0_device,
	&xilinx_gpiopss_0_device,
	&xilinx_norpss_device,
	&eth_device0,
	&xilinx_spipss_0_device,
	&xilinx_qspipss_0_device,
	&xilinx_wdtpss_0_device,
	&xilinx_a9wdt_device,
	&xilinx_nandpss_device,
	&xilinx_sdio0pss_device,
};

struct platform_device *xilinx_pdevices_amp1[] __initdata = {
	&uart_device1,
	&xilinx_i2cpss_1_device,
	&eth_device1,
	&xilinx_spipss_1_device,
	&xilinx_sdio1pss_device,
};

/**
 * platform_device_init - Initialize all the platform devices.
 *
 **/
void __init platform_device_init(void)
{
	int ret, i;
	struct platform_device **devptr;
	int size;

#ifdef CONFIG_XILINX_AMP_CPU0_MASTER
	devptr = &xilinx_pdevices_amp0[0];
	size = ARRAY_SIZE(xilinx_pdevices_amp0);
#elif defined(CONFIG_XILINX_AMP_CPU1_SLAVE) || defined(CONFIG_XILINX_CPU1_TEST)
	devptr = &xilinx_pdevices_amp1[0];
	size = ARRAY_SIZE(xilinx_pdevices_amp1);
#else
	devptr = &xilinx_pdevices[0];
	size = ARRAY_SIZE(xilinx_pdevices);
#endif

	ret = 0;

	/* Initialize all the platform devices */

	for (i = 0; i < size; i++, devptr++) {
		pr_info("registering platform device '%s' id %d\n",
			(*devptr)->name,
			(*devptr)->id);
			ret = platform_device_register(*devptr);
		if (ret)
			pr_info("Unable to register platform device '%s': %d\n",
				(*devptr)->name, ret);
#ifdef CONFIG_SPI_SPIDEV
		else if (&xilinx_spipss_0_device == *devptr)
			spi_register_board_info(&xilinx_spipss_0_boardinfo, 1);
#endif
#if (defined CONFIG_SPI_SPIDEV || defined CONFIG_MTD_M25P80)
		else if (&xilinx_qspipss_0_device == *devptr)
			spi_register_board_info(&xilinx_qspipss_0_boardinfo, 1);
#endif
	}
}

