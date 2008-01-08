/*
 * Virtex hard ppc405 core common device listing
 *
 * Copyright 2005-2007 Secret Lab Technologies Ltd.
 * Copyright 2005 Freescale Semiconductor Inc.
 * Copyright 2002-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/serial_8250.h>
#include <syslib/virtex_devices.h>
#include <platforms/4xx/xparameters/xparameters.h>
#include <linux/xilinx_devices.h>
#include <asm/io.h>

/*
 * UARTLITE: shortcut macro for single instance
 */
#define XPAR_UARTLITE(num) { \
	.name = "uartlite", \
	.id = num, \
	.num_resources = 2, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_UARTLITE_##num##_BASEADDR + 3, \
			.end = XPAR_UARTLITE_##num##_HIGHADDR, \
			.flags = IORESOURCE_MEM, \
		}, \
		{ \
			.start = XPAR_INTC_0_UARTLITE_##num##_VEC_ID, \
			.flags = IORESOURCE_IRQ, \
		}, \
	}, \
}

/*
 * Full UART: shortcut macro for single instance + platform data structure
 */
#define XPAR_UART(num) { \
	.mapbase = XPAR_UARTNS550_##num##_BASEADDR + 3, \
	.irq = XPAR_INTC_0_UARTNS550_##num##_VEC_ID, \
	.iotype = UPIO_MEM, \
	.uartclk = XPAR_UARTNS550_##num##_CLOCK_FREQ_HZ, \
	.flags = UPF_BOOT_AUTOCONF, \
	.regshift = 2, \
}

/*
 * SystemACE: shortcut macro for single instance
 */
#define XPAR_SYSACE(num) { \
	.name		= "xsysace", \
	.id		= XPAR_SYSACE_##num##_DEVICE_ID, \
	.num_resources	= 2, \
	.resource = (struct resource[]) { \
		{ \
			.start	= XPAR_SYSACE_##num##_BASEADDR, \
			.end	= XPAR_SYSACE_##num##_HIGHADDR, \
			.flags	= IORESOURCE_MEM, \
		}, \
		{ \
			.start	= XPAR_INTC_0_SYSACE_##num##_VEC_ID, \
			.flags	= IORESOURCE_IRQ, \
		}, \
	}, \
}

/*
 * ML300/ML403 Video Device: shortcut macro for single instance
 */
#define XPAR_TFT(num) { \
	.name = "xilinxfb", \
	.id = num, \
	.num_resources = 1, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_TFT_##num##_BASEADDR, \
			.end = XPAR_TFT_##num##_BASEADDR+7, \
			.flags = IORESOURCE_IO, \
		}, \
	}, \
}

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
			.start	= XPAR_INTC_0_EMAC_##num##_VEC_ID, \
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
		.mac_addr = {2, 0, 0, 0, 0, num}, \
	}, \
}

/*
 * EMAC: shortcut macro for single instance
 */
#define XPAR_EMACLITE(num) { \
	.name		= "xilinx_emaclite", \
	.id		= num, \
	.num_resources	= 2, \
	.resource = (struct resource[]) { \
		{ \
			.start	= XPAR_EMACLITE_##num##_BASEADDR, \
			.end	= XPAR_EMACLITE_##num##_HIGHADDR, \
			.flags	= IORESOURCE_MEM, \
		}, \
		{ \
			.start	= XPAR_INTC_0_EMACLITE_##num##_VEC_ID, \
			.flags	= IORESOURCE_IRQ, \
		}, \
	}, \
	.dev.platform_data = &(struct xemaclite_platform_data) { \
		.tx_ping_pong = XPAR_EMACLITE_##num##_TX_PING_PONG, \
		.rx_ping_pong = XPAR_EMACLITE_##num##_RX_PING_PONG, \
		/* locally administered default address */ \
		.mac_addr = {2, 0, 0, 0, 0, num}, \
	}, \
}


/*
 * Tri-mode EMAC (TEMAC): shortcut macro for single instance
 */
#define XPAR_TEMAC_RESOURCES(num) \
	.name = "xilinx_temac", \
	.id = XPAR_TEMAC_##num##_DEVICE_ID, \
	.num_resources = 2, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_TEMAC_##num##_BASEADDR, \
			.end = XPAR_TEMAC_##num##_HIGHADDR, \
			.flags = IORESOURCE_MEM \
		}, \
		{ \
			.start = XPAR_INTC_0_TEMAC_##num##_VEC_ID, \
			.end = XPAR_INTC_0_TEMAC_##num##_VEC_ID, \
			.flags = IORESOURCE_IRQ \
		} \
	}

#define XPAR_TEMAC_RX_CSUM(num) { \
	XPAR_TEMAC_RESOURCES(num), \
	.dev.platform_data = &(struct xtemac_platform_data) { \
		.tx_dre = XPAR_TEMAC_##num##_TX_DRE_TYPE, \
		.rx_dre = XPAR_TEMAC_##num##_RX_DRE_TYPE, \
		.tx_csum = XPAR_TEMAC_##num##_INCLUDE_TX_CSUM, \
		.rx_csum = XPAR_TEMAC_##num##_INCLUDE_RX_CSUM, \
		.phy_type = XPAR_HARD_TEMAC_##num##_PHY_TYPE, \
		.rx_pkt_fifo_depth = XPAR_TEMAC_##num##_RXFIFO_DEPTH, \
		.tx_pkt_fifo_depth = XPAR_TEMAC_##num##_TXFIFO_DEPTH, \
		.dma_mode = XPAR_TEMAC_##num##_DMA_TYPE, \
		.mac_fifo_depth = XPAR_TEMAC_##num##_MAC_FIFO_DEPTH, \
	}, \
}

#define XPAR_TEMAC_NO_RX_CSUM(num) { \
	XPAR_TEMAC_RESOURCES(num), \
	.dev.platform_data = &(struct xtemac_platform_data) { \
		.dcr_host = XPAR_TEMAC_##num##_TEMAC_DCR_HOST, \
		.dre = XPAR_TEMAC_##num##_INCLUDE_DRE, \
		.rx_pkt_fifo_depth = XPAR_TEMAC_##num##_IPIF_RDFIFO_DEPTH, \
		.tx_pkt_fifo_depth = XPAR_TEMAC_##num##_IPIF_WRFIFO_DEPTH, \
		.dma_mode = XPAR_TEMAC_##num##_DMA_TYPE, \
		.mac_fifo_depth = XPAR_TEMAC_##num##_MAC_FIFO_DEPTH \
	}, \
}

// wgr TODO: Check with John about the address space size!!!
#define XPAR_LLTEMAC_RESOURCES(num) \
	.name = "xilinx_lltemac", \
	.id = XPAR_LLTEMAC_##num##_DEVICE_ID, \
	.num_resources = 2, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_LLTEMAC_##num##_BASEADDR, \
			.end = XPAR_LLTEMAC_##num##_BASEADDR + 0x1000, \
			.flags = IORESOURCE_MEM \
		}, \
		{ \
			.start = XPAR_INTC_0_LLTEMAC_##num##_VEC_ID, \
			.end = XPAR_INTC_0_LLTEMAC_##num##_VEC_ID, \
			.flags = IORESOURCE_IRQ \
		} \
	}

#define XPAR_LLTEMAC(num) { \
	XPAR_LLTEMAC_RESOURCES(num), \
	.dev.platform_data = &(struct xlltemac_platform_data) { \
		.tx_csum = XPAR_LLTEMAC_##num##_TXCSUM, \
		.rx_csum = XPAR_LLTEMAC_##num##_RXCSUM, \
		.phy_type = XPAR_LLTEMAC_##num##_PHY_TYPE, \
		.dcr_host = 0xff, \
		.ll_dev_type = XPAR_LLTEMAC_##num##_LLINK_CONNECTED_TYPE, \
		.ll_dev_baseaddress = XPAR_LLTEMAC_##num##_LLINK_CONNECTED_BASEADDR, \
		.ll_dev_dma_rx_irq = XPAR_LLTEMAC_##num##_LLINK_CONNECTED_DMARX_INTR, \
		.ll_dev_dma_tx_irq = XPAR_LLTEMAC_##num##_LLINK_CONNECTED_DMATX_INTR, \
		.ll_dev_fifo_irq = XPAR_LLTEMAC_##num##_LLINK_CONNECTED_FIFO_INTR, \
		/* locally administered default address */ \
		.mac_addr = {2, 0, 0, 0, 0, num}, \
	}, \
}


#define XPAR_PS2(num) { \
	.name = "xilinx_ps2", \
	.id = num, \
	.num_resources = 2, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_PS2_##num##_BASEADDR, \
			.end = XPAR_PS2_##num##_HIGHADDR, \
			.flags = IORESOURCE_MEM, \
		}, \
		{ \
			.start = XPAR_INTC_0_PS2_##num##_VEC_ID, \
			.flags = IORESOURCE_IRQ, \
		}, \
	}, \
}

#define XPAR_HWICAP(num) { \
	.name = "xilinx_icap", \
	.id = num, \
	.num_resources = 1, \
	.resource = (struct resource[]) { \
		{ \
                        .start = XPAR_HWICAP_##num##_BASEADDR,  \
			.end   = XPAR_HWICAP_##num##_HIGHADDR,    \
			.flags = IORESOURCE_MEM, \
		}, \
	}, \
}

#define XPAR_AC97_CONTROLLER_REFERENCE(num) { \
	.name = "ml403_ac97cr", \
	.id = num, \
	.num_resources = 3, \
	.resource = (struct resource[]) { \
		{ \
			.start = XPAR_OPB_AC97_CONTROLLER_REF_##num##_BASEADDR, \
			.end = XPAR_OPB_AC97_CONTROLLER_REF_##num##_HIGHADDR, \
			.flags = IORESOURCE_MEM, \
		}, \
		{ \
			.start = XPAR_OPB_INTC_0_OPB_AC97_CONTROLLER_REF_##num##_PLAYBACK_INTERRUPT_INTR, \
			.end = XPAR_OPB_INTC_0_OPB_AC97_CONTROLLER_REF_##num##_PLAYBACK_INTERRUPT_INTR, \
			.flags = IORESOURCE_IRQ, \
		}, \
		{ \
			.start = XPAR_OPB_INTC_0_OPB_AC97_CONTROLLER_REF_##num##_RECORD_INTERRUPT_INTR, \
			.end = XPAR_OPB_INTC_0_OPB_AC97_CONTROLLER_REF_##num##_RECORD_INTERRUPT_INTR, \
			.flags = IORESOURCE_IRQ, \
		}, \
	}, \
}

/* UART 8250 driver platform data table */
struct plat_serial8250_port virtex_serial_platform_data[] = {
#if defined(XPAR_UARTNS550_0_BASEADDR)
	XPAR_UART(0),
#endif
#if defined(XPAR_UARTNS550_1_BASEADDR)
	XPAR_UART(1),
#endif
#if defined(XPAR_UARTNS550_2_BASEADDR)
	XPAR_UART(2),
#endif
#if defined(XPAR_UARTNS550_3_BASEADDR)
	XPAR_UART(3),
#endif
#if defined(XPAR_UARTNS550_4_BASEADDR)
	XPAR_UART(4),
#endif
#if defined(XPAR_UARTNS550_5_BASEADDR)
	XPAR_UART(5),
#endif
#if defined(XPAR_UARTNS550_6_BASEADDR)
	XPAR_UART(6),
#endif
#if defined(XPAR_UARTNS550_7_BASEADDR)
	XPAR_UART(7),
#endif
	{ }, /* terminated by empty record */
};


struct platform_device virtex_platform_devices[] = {
	/* UARTLITE instances */
#if defined(XPAR_UARTLITE_0_BASEADDR)
	XPAR_UARTLITE(0),
#endif
#if defined(XPAR_UARTLITE_1_BASEADDR)
	XPAR_UARTLITE(1),
#endif
#if defined(XPAR_UARTLITE_2_BASEADDR)
	XPAR_UARTLITE(2),
#endif
#if defined(XPAR_UARTLITE_3_BASEADDR)
	XPAR_UARTLITE(3),
#endif
#if defined(XPAR_UARTLITE_4_BASEADDR)
	XPAR_UARTLITE(4),
#endif
#if defined(XPAR_UARTLITE_5_BASEADDR)
	XPAR_UARTLITE(5),
#endif
#if defined(XPAR_UARTLITE_6_BASEADDR)
	XPAR_UARTLITE(6),
#endif
#if defined(XPAR_UARTLITE_7_BASEADDR)
	XPAR_UARTLITE(7),
#endif

	/* Full UART instances */
#if defined(XPAR_UARTNS550_0_BASEADDR)
	{
		.name = "serial8250",
		.id = 0,
		.dev.platform_data = virtex_serial_platform_data,
	},
#endif

	/* SystemACE instances */
#if defined(XPAR_SYSACE_0_BASEADDR)
	XPAR_SYSACE(0),
#endif
#if defined(XPAR_SYSACE_1_BASEADDR)
	XPAR_SYSACE(1),
#endif

	/* EMAC instances */
#if defined(XPAR_EMAC_0_BASEADDR)
	XPAR_EMAC(0),
#endif
#if defined(XPAR_EMAC_1_BASEADDR)
	XPAR_EMAC(1),
#endif
#if defined(XPAR_EMAC_2_BASEADDR)
	XPAR_EMAC(2),
#endif
#if defined(XPAR_EMAC_3_BASEADDR)
	XPAR_EMAC(3),
#endif

	/* EMACLITE instances */
#if defined(XPAR_EMACLITE_0_BASEADDR)
	XPAR_EMACLITE(0),
#endif
#if defined(XPAR_EMACLITE_1_BASEADDR)
	XPAR_EMACLITE(1),
#endif
#if defined(XPAR_EMACLITE_2_BASEADDR)
	XPAR_EMACLITE(2),
#endif
#if defined(XPAR_EMACLITE_3_BASEADDR)
	XPAR_EMACLITE(3),
#endif

	/* TEMAC instances */
#if defined(XPAR_TEMAC_0_BASEADDR)
#if defined(XPAR_TEMAC_0_INCLUDE_RX_CSUM)
	XPAR_TEMAC_RX_CSUM(0),
#else
	XPAR_TEMAC_NO_RX_CSUM(0),
#endif
#endif

#if defined(XPAR_TEMAC_1_BASEADDR)
#if defined(XPAR_TEMAC_1_INCLUDE_RX_CSUM)
	XPAR_TEMAC_RX_CSUM(1),
#else
	XPAR_TEMAC_NO_RX_CSUM(1),
#endif
#endif

#if defined(XPAR_TEMAC_2_BASEADDR)
#if defined(XPAR_TEMAC_2_INCLUDE_RX_CSUM)
	XPAR_TEMAC_RX_CSUM(2),
#else
	XPAR_TEMAC_NO_RX_CSUM(2),
#endif
#endif

#if defined(XPAR_TEMAC_3_BASEADDR)
#if defined(XPAR_TEMAC_3_INCLUDE_RX_CSUM)
	XPAR_TEMAC_RX_CSUM(3),
#else
	XPAR_TEMAC_NO_RX_CSUM(3),
#endif
#endif

	/* LLTEMAC instances */
#if defined(XPAR_LLTEMAC_0_BASEADDR)
	XPAR_LLTEMAC(0),
#endif
#if defined(XPAR_LLTEMAC_1_BASEADDR)
	XPAR_LLTEMAC(1),
#endif
#if defined(XPAR_LLTEMAC_2_BASEADDR)
	XPAR_LLTEMAC(2),
#endif
#if defined(XPAR_LLTEMAC_3_BASEADDR)
	XPAR_LLTEMAC(3),
#endif

#if defined(XPAR_PS2_0_BASEADDR)
	XPAR_PS2(0),
#endif
#if defined(XPAR_PS2_1_BASEADDR)
	XPAR_PS2(1),
#endif
#if defined(XPAR_PS2_2_BASEADDR)
	XPAR_PS2(2),
#endif
#if defined(XPAR_PS2_3_BASEADDR)
	XPAR_PS2(3),
#endif

#if defined(XPAR_HWICAP_0_BASEADDR)
	XPAR_HWICAP(0),
#endif

	/* ML300/403 reference design framebuffer */
#if defined(XPAR_TFT_0_BASEADDR)
	XPAR_TFT(0),
#endif
#if defined(XPAR_TFT_1_BASEADDR)
	XPAR_TFT(1),
#endif
#if defined(XPAR_TFT_2_BASEADDR)
	XPAR_TFT(2),
#endif
#if defined(XPAR_TFT_3_BASEADDR)
	XPAR_TFT(3),
#endif

	/* AC97 Controller Reference instances */
#if defined(XPAR_OPB_AC97_CONTROLLER_REF_0_BASEADDR)
	XPAR_AC97_CONTROLLER_REFERENCE(0),
#endif
#if defined(XPAR_OPB_AC97_CONTROLLER_REF_1_BASEADDR)
	XPAR_AC97_CONTROLLER_REFERENCE(1),
#endif
};

/* Early serial support functions */
static void __init
virtex_early_serial_init(int num, struct plat_serial8250_port *pdata)
{
#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	extern void gen550_init(int i, struct uart_port *serial_req);
	struct uart_port serial_req;

	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.mapbase = pdata->mapbase;
	serial_req.membase = pdata->membase;
	serial_req.irq = pdata->irq;
	serial_req.uartclk = pdata->uartclk;
	serial_req.regshift = pdata->regshift;
	serial_req.iotype = pdata->iotype;
	serial_req.flags = pdata->flags;
	gen550_init(num, &serial_req);
#endif
}

void __init
virtex_early_serial_map(void)
{
#ifdef CONFIG_SERIAL_8250
	struct plat_serial8250_port *pdata;
	int i = 0;

	pdata = virtex_serial_platform_data;
	while(pdata && pdata->flags) {
		pdata->membase = ioremap(pdata->mapbase, 0x100);
		virtex_early_serial_init(i, pdata);
		pdata++;
		i++;
	}
#endif /* CONFIG_SERIAL_8250 */
}

/*
 * default fixup routine; do nothing and return success.
 *
 * Reimplement this routine in your custom board support file to
 * override the default behaviour
 */
int __attribute__ ((weak))
virtex_device_fixup(struct platform_device *dev)
{
	return 0;
}

static int __init virtex_init(void)
{
	struct platform_device *index = virtex_platform_devices;
	unsigned int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(virtex_platform_devices); i++, index++) {
		if (virtex_device_fixup(index) != 0)
			continue;

		printk(KERN_INFO "Registering device %s:%d\n",
		       index->name, index->id);
		if (platform_device_register(index)) {
			ret = 1;
			printk(KERN_ERR "cannot register dev %s:%d\n",
			       index->name, index->id);
		}
	}
	return ret;
}

subsys_initcall(virtex_init);
