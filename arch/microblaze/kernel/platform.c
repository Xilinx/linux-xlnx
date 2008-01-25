/*
 * arch/microblaze/kernel/platform.c
 *
 * Copyright 2007 Xilinx, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/resource.h>
#include <linux/xilinx_devices.h>
#include <linux/serial_8250.h>
#include <linux/serial.h>
#include <asm/xparameters.h>
#include <asm/io.h>
#include <asm/of_platform.h>

#ifdef XPAR_SPI_0_BASEADDR

static struct xspi_platform_data xspi_0_pdata = {
	.device_flags = (XPAR_SPI_0_FIFO_EXIST ? XSPI_HAS_FIFOS : 0) |
		(XPAR_SPI_0_SPI_SLAVE_ONLY ? XSPI_SLAVE_ONLY : 0),
	.num_slave_bits = XPAR_SPI_0_NUM_SS_BITS
};

static struct platform_device xilinx_spi_0_device = {
	.name = "xilinx_spi",
	.id = XPAR_SPI_0_DEVICE_ID,
	.dev.platform_data = &xspi_0_pdata,
	.num_resources = 2,
	.resource = (struct resource[]) {
		{
			.start	= XPAR_SPI_0_BASEADDR,
			.end	= XPAR_SPI_0_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
		{
			.start	= XPAR_INTC_0_SPI_0_VEC_ID,
			.end	= XPAR_INTC_0_SPI_0_VEC_ID,
			.flags	= IORESOURCE_IRQ
		}
	}
};

#endif /* XPAR_SPI_0_BASEADDR */

/*
 * EMAC: shortcut macro for single instance
 */
#define XPAR_EMAC(num) { \
	.name		= "xilinx_emac", \
	.id		= num, \
	.num_resources	= 2, \
	.resource = (struct resource[]) { \
		{ \
			.start	= XPAR_EMAC_##num##_BASEADDR, \
			.end	= XPAR_EMAC_##num##_HIGHADDR, \
			.flags	= IORESOURCE_MEM, \
		}, \
		{ \
			.start	= XPAR_EMAC_##num##_IRQ, \
			.flags	= IORESOURCE_IRQ, \
		}, \
	}, \
	.dev.platform_data = &(struct xemac_platform_data) { \
		.dma_mode = XPAR_EMAC_##num##_DMA_PRESENT, \
		.has_mii = XPAR_EMAC_##num##_MII_EXIST, \
		.has_cam = XPAR_EMAC_##num##_CAM_EXIST, \
		.has_err_cnt = XPAR_EMAC_##num##_ERR_COUNT_EXIST, \
		.has_jumbo = XPAR_EMAC_##num##_JUMBO_EXIST, \
		.tx_dre = XPAR_EMAC_##num##_TX_DRE_TYPE, \
		.rx_dre = XPAR_EMAC_##num##_RX_DRE_TYPE, \
		.tx_hw_csum = XPAR_EMAC_##num##_TX_INCLUDE_CSUM, \
		.rx_hw_csum = XPAR_EMAC_##num##_RX_INCLUDE_CSUM, \
		/* locally administered default address */ \
		.mac_addr = {0x00, 0x0A, 0x35, 5, 5, 5}, \
	}, \
}

#ifdef XPAR_EMAC_0_BASEADDR
static struct platform_device xilinx_emac_0_device = XPAR_EMAC(0);
#endif

#ifdef XPAR_GPIO_0_BASEADDR

static struct platform_device xilinx_gpio_0_device = {
	.name = "xilinx_gpio",
	.id = 0,
	.dev.platform_data = (XPAR_GPIO_0_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_0_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_0_BASEADDR,
			.end	= XPAR_GPIO_0_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_0_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_0_IRQ,
			.end	= XPAR_GPIO_0_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_0_BASEADDR */

#ifdef XPAR_GPIO_1_BASEADDR

static struct platform_device xilinx_gpio_1_device = {
	.name = "xilinx_gpio",
	.id = 1,
	.dev.platform_data = (XPAR_GPIO_1_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_1_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_1_BASEADDR,
			.end	= XPAR_GPIO_1_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_1_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_1_IRQ,
			.end	= XPAR_GPIO_1_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_1_BASEADDR */

#ifdef XPAR_GPIO_2_BASEADDR

static struct platform_device xilinx_gpio_2_device = {
	.name = "xilinx_gpio",
	.id = 2,
	.dev.platform_data = (XPAR_GPIO_2_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_2_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_2_BASEADDR,
			.end	= XPAR_GPIO_2_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_2_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_2_IRQ,
			.end	= XPAR_GPIO_2_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_2_BASEADDR */

#ifdef XPAR_GPIO_3_BASEADDR

static struct platform_device xilinx_gpio_3_device = {
	.name = "xilinx_gpio",
	.id = 3,
	.dev.platform_data = (XPAR_GPIO_3_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_3_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_3_BASEADDR,
			.end	= XPAR_GPIO_3_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_3_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_3_IRQ,
			.end	= XPAR_GPIO_3_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_3_BASEADDR */

#ifdef XPAR_GPIO_4_BASEADDR

static struct platform_device xilinx_gpio_4_device = {
	.name = "xilinx_gpio",
	.id = 4,
	.dev.platform_data = (XPAR_GPIO_4_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_4_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_4_BASEADDR,
			.end	= XPAR_GPIO_4_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_4_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_4_IRQ,
			.end	= XPAR_GPIO_4_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_4_BASEADDR */

#ifdef XPAR_GPIO_5_BASEADDR

static struct platform_device xilinx_gpio_5_device = {
	.name = "xilinx_gpio",
	.id = 5,
	.dev.platform_data = (XPAR_GPIO_5_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_5_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_5_BASEADDR,
			.end	= XPAR_GPIO_5_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_5_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_5_IRQ,
			.end	= XPAR_GPIO_5_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_5_BASEADDR */

#ifdef XPAR_GPIO_6_BASEADDR

static struct platform_device xilinx_gpio_6_device = {
	.name = "xilinx_gpio",
	.id = 6,
	.dev.platform_data = (XPAR_GPIO_6_IS_DUAL ? XGPIO_IS_DUAL : 0),
#if XPAR_GPIO_6_INTERRUPT_PRESENT
	.num_resources = 2,
#else
	.num_resources = 1,
#endif
	.resource = (struct resource[]) {
		{
			.start	= XPAR_GPIO_6_BASEADDR,
			.end	= XPAR_GPIO_6_HIGHADDR,
			.flags	= IORESOURCE_MEM
		},
#if XPAR_GPIO_6_INTERRUPT_PRESENT
		{
			.start	= XPAR_GPIO_6_IRQ,
			.end	= XPAR_GPIO_6_IRQ,
			.flags	= IORESOURCE_IRQ
		}
#endif
	}
};

#endif /* XPAR_GPIO_6_BASEADDR */

#if defined(XPAR_OPB_UART16550_0_BASEADDR) || defined(XPAR_OPB_UART16550_1_BASEADDR)
#define XPAR_HAVE_UART16550
#endif

#if defined(XPAR_HAVE_UART16550) && defined(CONFIG_SERIAL_8250)

#define XPAR_UART(num) { \
		.mapbase  = XPAR_OPB_UART16550_##num##_BASEADDR + 0x1003, \
		.irq      = XPAR_OPB_INTC_0_OPB_UART16550_##num##_IRQ, \
		.iotype   = UPIO_MEM, \
		.uartclk  = XPAR_CPU_CLOCK_FREQ, \
		.flags    = UPF_BOOT_AUTOCONF, \
		.regshift = 2, \
	}

static struct uart_port xilinx_16550_port[] = {
#ifdef XPAR_OPB_UART16550_0_BASEADDR
	XPAR_UART(0),
#endif
#ifdef XPAR_OPB_UART16550_1_BASEADDR
	XPAR_UART(1),
#endif
	{ }, /* terminated by empty record */
};

#endif /* defined(XPAR_HAVE_UART16550) && defined(CONFIG_SERIAL_8250) */

void __init uart_16550_early_init(void)
{
#if defined(XPAR_HAVE_UART16550) && defined(CONFIG_SERIAL_8250)
	int i;

	for (i = 0; xilinx_16550_port[i].flags; i++) {

		xilinx_16550_port[i].membase = ioremap(xilinx_16550_port[i].mapbase, 0x100);

		if (early_serial_setup(&xilinx_16550_port[i]) != 0) {
                	printk("Early serial init of port %d failed\n", i);
		}

	}
#endif /* defined(XPAR_HAVE_UART16550) && defined(CONFIG_SERIAL_8250) */
}

#ifdef CONFIG_DEVICE_TREE
static struct of_device_id xilinx_of_bus_ids[] = {
	{ .compatible = "xlnx,plb-v46-1.00.a", },
	{ .compatible = "xlnx,plb-v34-1.01.a", },
	{ .compatible = "xlnx,plb-v34-1.02.a", },
	{ .compatible = "xlnx,opb-v20-1.10.c", },
	{ .compatible = "xlnx,dcr-v29-1.00.a", },
	{ .compatible = "xlnx,compound", },
	{},
};
#endif

static int __init xilinx_platform_init(void)
{
#ifdef CONFIG_DEVICE_TREE
    of_platform_bus_probe(NULL, xilinx_of_bus_ids, NULL);
#else

#ifdef XPAR_SPI_0_BASEADDR
	platform_device_register(&xilinx_spi_0_device);
#endif /* XPAR_SPI_0_BASEADDR */

/* EMAC instances */
#if defined(XPAR_EMAC_0_BASEADDR)
	platform_device_register(&xilinx_emac_0_device);
#endif

#ifdef XPAR_GPIO_0_BASEADDR
	platform_device_register(&xilinx_gpio_0_device);
#endif /* XPAR_GPIO_0_BASEADDR */
#ifdef XPAR_GPIO_1_BASEADDR
	platform_device_register(&xilinx_gpio_1_device);
#endif /* XPAR_GPIO_1_BASEADDR */
#ifdef XPAR_GPIO_2_BASEADDR
	platform_device_register(&xilinx_gpio_2_device);
#endif /* XPAR_GPIO_2_BASEADDR */
#ifdef XPAR_GPIO_3_BASEADDR
	platform_device_register(&xilinx_gpio_3_device);
#endif /* XPAR_GPIO_3_BASEADDR */
#ifdef XPAR_GPIO_4_BASEADDR
	platform_device_register(&xilinx_gpio_4_device);
#endif /* XPAR_GPIO_4_BASEADDR */
#ifdef XPAR_GPIO_5_BASEADDR
	platform_device_register(&xilinx_gpio_5_device);
#endif /* XPAR_GPIO_5_BASEADDR */
#ifdef XPAR_GPIO_6_BASEADDR
	platform_device_register(&xilinx_gpio_6_device);
#endif /* XPAR_GPIO_6_BASEADDR */

#endif /* CONFIG_DEVICE_TREE */
	return 0;
}

subsys_initcall(xilinx_platform_init);
