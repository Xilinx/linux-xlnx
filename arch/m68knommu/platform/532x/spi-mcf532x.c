/***************************************************************************/
/*
 *	linux/arch/m68knommu/platform/532x/spi-mcf532x.c
 *
 *	Sub-architcture dependant initialization code for the Freescale
 *	532x SPI module
 *
 *	Yaroslav Vinogradov yaroslav.vinogradov@freescale.com
 *	Copyright Freescale Semiconductor, Inc 2006
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or 
 *	(at your option) any later version.
 */
/***************************************************************************/


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/mcfqspi.h>
#include <linux/spi/ads7843.h>

#include <asm/dma.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

#define SPI_NUM_CHIPSELECTS 	0x04
#define SPI_PAR_VAL		0xFFF0  /* Enable DIN, DOUT, CLK */

#define MCF532x_QSPI_IRQ_SOURCE	(31)
#define MCF532x_QSPI_IRQ_VECTOR	(64 + MCF532x_QSPI_IRQ_SOURCE)

#define MCF532x_QSPI_PAR	(0xFC0A405A)
#define MCF532x_QSPI_QMR	(0xFC05C000)
#define MCF532x_INTC0_ICR	(0xFC048040)
#define MCF532x_INTC0_IMRL	(0xFC04800C)

/* on 5329 EVB ADS7843 is connected to IRQ4 */
#define ADS784x_IRQ_SOURCE	4
#define ADS784x_IRQ_VECTOR	(64+ADS784x_IRQ_SOURCE)
#define ADS7843_IRQ_LEVEL	2


void coldfire_qspi_cs_control(u8 cs, u8 command)
{
}

#if defined(CONFIG_TOUCHSCREEN_ADS7843)
static struct coldfire_spi_chip ads784x_chip_info = {
	.mode = SPI_MODE_0,
	.bits_per_word = 8,
	.del_cs_to_clk = 17,
	.del_after_trans = 1,
	.void_write_data = 0
};

static struct ads7843_platform_data ads784x_platform_data = {
	.model = 7843,
	.vref_delay_usecs = 0,
	.x_plate_ohms = 580,
	.y_plate_ohms = 410
};
#endif


static struct spi_board_info spi_board_info[] = {
#if defined(CONFIG_TOUCHSCREEN_ADS7843)
	{
		.modalias = "ads7843",
		.max_speed_hz = 125000 * 16,
		.bus_num = 1,
		.chip_select = 1,
		.irq = ADS784x_IRQ_VECTOR,
		.platform_data = &ads784x_platform_data,
		.controller_data = &ads784x_chip_info
	}
#endif
};

static struct coldfire_spi_master coldfire_master_info = {
	.bus_num = 1,
	.num_chipselect = SPI_NUM_CHIPSELECTS,
	.irq_source = MCF532x_QSPI_IRQ_SOURCE,
	.irq_vector = MCF532x_QSPI_IRQ_VECTOR,
	.irq_mask = (0x01 << MCF532x_QSPI_IRQ_SOURCE),
	.irq_lp = 0x5,  /* Level */
	.par_val = 0,   /* not used on 532x */
	.par_val16 = SPI_PAR_VAL,
	.cs_control = coldfire_qspi_cs_control,
};

static struct resource coldfire_spi_resources[] = {
	[0] = {
		.name = "qspi-par",
		.start = MCF532x_QSPI_PAR,
		.end = MCF532x_QSPI_PAR,
		.flags = IORESOURCE_MEM
	},

	[1] = {
		.name = "qspi-module",
		.start = MCF532x_QSPI_QMR,
		.end = MCF532x_QSPI_QMR + 0x18,
		.flags = IORESOURCE_MEM
	},

	[2] = {
		.name = "qspi-int-level",
		.start = MCF532x_INTC0_ICR + MCF532x_QSPI_IRQ_SOURCE,
		.end = MCF532x_INTC0_ICR + MCF532x_QSPI_IRQ_SOURCE,
		.flags = IORESOURCE_MEM
	},

	[3] = {
		.name = "qspi-int-mask",
		.start = MCF532x_INTC0_IMRL,
		.end = MCF532x_INTC0_IMRL,
		.flags = IORESOURCE_MEM
	}
};

static struct platform_device coldfire_spi = {
	.name = "coldfire-qspi",
	.id = -1,
	.resource = coldfire_spi_resources,
	.num_resources = ARRAY_SIZE(coldfire_spi_resources),
	.dev = {
		.platform_data = &coldfire_master_info,
	}
};

#if defined(CONFIG_TOUCHSCREEN_ADS7843)
static int __init init_ads7843(void)
{
	/* GPIO initiaalization */
	MCF_GPIO_PAR_IRQ = MCF_GPIO_PAR_IRQ_PAR_IRQ4(0);
	/* EPORT initialization */
	MCF_EPORT_EPPAR = MCF_EPORT_EPPAR_EPPA4(MCF_EPORT_EPPAR_FALLING);
	MCF_EPORT_EPDDR = 0;
	MCF_EPORT_EPIER = MCF_EPORT_EPIER_EPIE4;
	/* enable interrupt source */
	MCF_INTC0_ICR4 = ADS7843_IRQ_LEVEL;
	MCF_INTC0_CIMR = ADS784x_IRQ_SOURCE;
}
#endif

static int __init spi_dev_init(void)
{
	int retval = 0;
#if defined(CONFIG_TOUCHSCREEN_ADS7843)
	init_ads7843();
#endif

	retval = platform_device_register(&coldfire_spi);
	if (retval < 0)
		goto out;

	if (ARRAY_SIZE(spi_board_info))
		retval = spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));


out:
	return retval;
}

arch_initcall(spi_dev_init);
