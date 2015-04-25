/*
 * Xilinx SPI controller driver (master mode only)
 *
 * Author: MontaVista Software, Inc.
 *	source@mvista.com
 *
 * Copyright (c) 2010 Secret Lab Technologies, Ltd.
 * Copyright (c) 2009 Intel Corporation
 * 2002-2007 (c) MontaVista Software, Inc.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/io.h>

#define XILINX_SPI_NAME "xilinx_spi"

/* Register definitions as per "OPB Serial Peripheral Interface (SPI) (v1.00e)
 * Product Specification", DS464
 */
#define XSPI_CR_OFFSET		0x60	/* Control Register */

#define XSPI_CR_LOOP		0x01
#define XSPI_CR_ENABLE		0x02
#define XSPI_CR_MASTER_MODE	0x04
#define XSPI_CR_CPOL		0x08
#define XSPI_CR_CPHA		0x10
#define XSPI_CR_MODE_MASK	(XSPI_CR_CPHA | XSPI_CR_CPOL)
#define XSPI_CR_TXFIFO_RESET	0x20
#define XSPI_CR_RXFIFO_RESET	0x40
#define XSPI_CR_MANUAL_SSELECT	0x80
#define XSPI_CR_TRANS_INHIBIT	0x100
#define XSPI_CR_LSB_FIRST	0x200

#define XSPI_SR_OFFSET		0x64	/* Status Register */

#define XSPI_SR_RX_EMPTY_MASK	0x01	/* Receive FIFO is empty */
#define XSPI_SR_RX_FULL_MASK	0x02	/* Receive FIFO is full */
#define XSPI_SR_TX_EMPTY_MASK	0x04	/* Transmit FIFO is empty */
#define XSPI_SR_TX_FULL_MASK	0x08	/* Transmit FIFO is full */
#define XSPI_SR_MODE_FAULT_MASK	0x10	/* Mode fault error */

#define XSPI_TXD_OFFSET		0x68	/* Data Transmit Register */
#define XSPI_RXD_OFFSET		0x6c	/* Data Receive Register */

#define XSPI_SSR_OFFSET		0x70	/* 32-bit Slave Select Register */

/* Register definitions as per "OPB IPIF (v3.01c) Product Specification", DS414
 * IPIF registers are 32 bit
 */
#define XIPIF_V123B_DGIER_OFFSET	0x1c	/* IPIF global int enable reg */
#define XIPIF_V123B_GINTR_ENABLE	0x80000000

#define XIPIF_V123B_IISR_OFFSET		0x20	/* IPIF interrupt status reg */
#define XIPIF_V123B_IIER_OFFSET		0x28	/* IPIF interrupt enable reg */

#define XSPI_INTR_MODE_FAULT		0x01	/* Mode fault error */
#define XSPI_INTR_SLAVE_MODE_FAULT	0x02	/* Selected as slave while
						 * disabled */
#define XSPI_INTR_TX_EMPTY		0x04	/* TxFIFO is empty */
#define XSPI_INTR_TX_UNDERRUN		0x08	/* TxFIFO was underrun */
#define XSPI_INTR_RX_FULL		0x10	/* RxFIFO is full */
#define XSPI_INTR_RX_OVERRUN		0x20	/* RxFIFO was overrun */
#define XSPI_INTR_TX_HALF_EMPTY		0x40	/* TxFIFO is half empty */

#define XIPIF_V123B_RESETR_OFFSET	0x40	/* IPIF reset register */
#define XIPIF_V123B_RESET_MASK		0x0a	/* the value to write */

struct xilinx_spi {
	/* bitbang has to be first */
	struct spi_bitbang bitbang;
	struct completion done;
	void __iomem	*regs;	/* virt. address of the control registers */

	int		irq;

	u8 *rx_ptr;		/* pointer in the Tx buffer */
	const u8 *tx_ptr;	/* pointer in the Rx buffer */
	int remaining_bytes;	/* the number of bytes left to transfer */
	u8 bits_per_word;
	unsigned int (*read_fn)(void __iomem *);
	void (*write_fn)(u32, void __iomem *);
	void (*tx_fn)(struct xilinx_spi *);
	void (*rx_fn)(struct xilinx_spi *);
};

static void xspi_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int xspi_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void xspi_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int xspi_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

static void xspi_tx8(struct xilinx_spi *xspi)
{
	xspi->write_fn(*xspi->tx_ptr, xspi->regs + XSPI_TXD_OFFSET);
	xspi->tx_ptr++;
}

static void xspi_tx16(struct xilinx_spi *xspi)
{
	xspi->write_fn(*(u16 *)(xspi->tx_ptr), xspi->regs + XSPI_TXD_OFFSET);
	xspi->tx_ptr += 2;
}

static void xspi_tx32(struct xilinx_spi *xspi)
{
	xspi->write_fn(*(u32 *)(xspi->tx_ptr), xspi->regs + XSPI_TXD_OFFSET);
	xspi->tx_ptr += 4;
}

static void xspi_rx8(struct xilinx_spi *xspi)
{
	u32 data = xspi->read_fn(xspi->regs + XSPI_RXD_OFFSET);
	if (xspi->rx_ptr) {
		*xspi->rx_ptr = data & 0xff;
		xspi->rx_ptr++;
	}
}

static void xspi_rx16(struct xilinx_spi *xspi)
{
	u32 data = xspi->read_fn(xspi->regs + XSPI_RXD_OFFSET);
	if (xspi->rx_ptr) {
		*(u16 *)(xspi->rx_ptr) = data & 0xffff;
		xspi->rx_ptr += 2;
	}
}

static void xspi_rx32(struct xilinx_spi *xspi)
{
	u32 data = xspi->read_fn(xspi->regs + XSPI_RXD_OFFSET);
	if (xspi->rx_ptr) {
		*(u32 *)(xspi->rx_ptr) = data;
		xspi->rx_ptr += 4;
	}
}

static void xspi_init_hw(struct xilinx_spi *xspi)
{
	void __iomem *regs_base = xspi->regs;

	/* Reset the SPI device */
	xspi->write_fn(XIPIF_V123B_RESET_MASK,
		regs_base + XIPIF_V123B_RESETR_OFFSET);
	/* Disable all the interrupts just in case */
	xspi->write_fn(0, regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Enable the global IPIF interrupt */
	xspi->write_fn(XIPIF_V123B_GINTR_ENABLE,
		regs_base + XIPIF_V123B_DGIER_OFFSET);
	/* Deselect the slave on the SPI bus */
	xspi->write_fn(0xffff, regs_base + XSPI_SSR_OFFSET);
	/* Disable the transmitter, enable Manual Slave Select Assertion,
	 * put SPI controller into master mode, and enable it */
	xspi->write_fn(XSPI_CR_TRANS_INHIBIT | XSPI_CR_MANUAL_SSELECT |
		XSPI_CR_MASTER_MODE | XSPI_CR_ENABLE | XSPI_CR_TXFIFO_RESET |
		XSPI_CR_RXFIFO_RESET, regs_base + XSPI_CR_OFFSET);
}

static void xilinx_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);

	if (is_on == BITBANG_CS_INACTIVE) {
		/* Deselect the slave on the SPI bus */
		xspi->write_fn(0xffff, xspi->regs + XSPI_SSR_OFFSET);
	} else if (is_on == BITBANG_CS_ACTIVE) {
		/* Set the SPI clock phase and polarity */
		u16 cr = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET)
			 & ~XSPI_CR_MODE_MASK;
		if (spi->mode & SPI_CPHA)
			cr |= XSPI_CR_CPHA;
		if (spi->mode & SPI_CPOL)
			cr |= XSPI_CR_CPOL;
		xspi->write_fn(cr, xspi->regs + XSPI_CR_OFFSET);

		/* We do not check spi->max_speed_hz here as the SPI clock
		 * frequency is not software programmable (the IP block design
		 * parameter)
		 */

		/* Activate the chip select */
		xspi->write_fn(~(0x0001 << spi->chip_select),
			xspi->regs + XSPI_SSR_OFFSET);
	}
}

/* spi_bitbang requires custom setup_transfer() to be defined if there is a
 * custom txrx_bufs().
 */
static int xilinx_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	return 0;
}

static void xilinx_spi_fill_tx_fifo(struct xilinx_spi *xspi)
{
	u8 sr;

	/* Fill the Tx FIFO with as many bytes as possible */
	sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
	while ((sr & XSPI_SR_TX_FULL_MASK) == 0 && xspi->remaining_bytes > 0) {
		if (xspi->tx_ptr)
			xspi->tx_fn(xspi);
		else
			xspi->write_fn(0, xspi->regs + XSPI_TXD_OFFSET);
		xspi->remaining_bytes -= xspi->bits_per_word / 8;
		sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
	}
}

static int xilinx_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ipif_ier;

	/* We get here with transmitter inhibited */

	xspi->tx_ptr = t->tx_buf;
	xspi->rx_ptr = t->rx_buf;
	xspi->remaining_bytes = t->len;
	reinit_completion(&xspi->done);


	/* Enable the transmit empty interrupt, which we use to determine
	 * progress on the transmission.
	 */
	ipif_ier = xspi->read_fn(xspi->regs + XIPIF_V123B_IIER_OFFSET);
	xspi->write_fn(ipif_ier | XSPI_INTR_TX_EMPTY,
		xspi->regs + XIPIF_V123B_IIER_OFFSET);

	for (;;) {
		u16 cr;
		u8 sr;

		xilinx_spi_fill_tx_fifo(xspi);

		/* Start the transfer by not inhibiting the transmitter any
		 * longer
		 */
		cr = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET) &
							~XSPI_CR_TRANS_INHIBIT;
		xspi->write_fn(cr, xspi->regs + XSPI_CR_OFFSET);

		wait_for_completion(&xspi->done);

		/* A transmit has just completed. Process received data and
		 * check for more data to transmit. Always inhibit the
		 * transmitter while the Isr refills the transmit register/FIFO,
		 * or make sure it is stopped if we're done.
		 */
		cr = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET);
		xspi->write_fn(cr | XSPI_CR_TRANS_INHIBIT,
			       xspi->regs + XSPI_CR_OFFSET);

		/* Read out all the data from the Rx FIFO */
		sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
		while ((sr & XSPI_SR_RX_EMPTY_MASK) == 0) {
			xspi->rx_fn(xspi);
			sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
		}

		/* See if there is more data to send */
		if (xspi->remaining_bytes <= 0)
			break;
	}

	/* Disable the transmit empty interrupt */
	xspi->write_fn(ipif_ier, xspi->regs + XIPIF_V123B_IIER_OFFSET);

	return t->len - xspi->remaining_bytes;
}


/* This driver supports single master mode only. Hence Tx FIFO Empty
 * is the only interrupt we care about.
 * Receive FIFO Overrun, Transmit FIFO Underrun, Mode Fault, and Slave Mode
 * Fault are not to happen.
 */
static irqreturn_t xilinx_spi_irq(int irq, void *dev_id)
{
	struct xilinx_spi *xspi = dev_id;
	u32 ipif_isr;

	/* Get the IPIF interrupts, and clear them immediately */
	ipif_isr = xspi->read_fn(xspi->regs + XIPIF_V123B_IISR_OFFSET);
	xspi->write_fn(ipif_isr, xspi->regs + XIPIF_V123B_IISR_OFFSET);

	if (ipif_isr & XSPI_INTR_TX_EMPTY) {	/* Transmission completed */
		complete(&xspi->done);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static int xilinx_spi_probe(struct platform_device *pdev)
{
	struct xilinx_spi *xspi;
	struct xspi_platform_data *pdata;
	struct resource *res;
	int ret, num_cs = 0, bits_per_word = 8;
	struct spi_master *master;
	u32 tmp;
	u8 i;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		num_cs = pdata->num_chipselect;
		bits_per_word = pdata->bits_per_word;
	} else {
		of_property_read_u32(pdev->dev.of_node, "xlnx,num-ss-bits",
					  &num_cs);
	}

	if (!num_cs) {
		dev_err(&pdev->dev,
			"Missing slave select configuration data\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct xilinx_spi));
	if (!master)
		return -ENODEV;

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA;

	xspi = spi_master_get_devdata(master);
	xspi->bitbang.master = master;
	xspi->bitbang.chipselect = xilinx_spi_chipselect;
	xspi->bitbang.setup_transfer = xilinx_spi_setup_transfer;
	xspi->bitbang.txrx_bufs = xilinx_spi_txrx_bufs;
	init_completion(&xspi->done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xspi->regs)) {
		ret = PTR_ERR(xspi->regs);
		goto put_master;
	}

	master->bus_num = pdev->id;
	master->num_chipselect = num_cs;
	master->dev.of_node = pdev->dev.of_node;

	/*
	 * Detect endianess on the IP via loop bit in CR. Detection
	 * must be done before reset is sent because incorrect reset
	 * value generates error interrupt.
	 * Setup little endian helper functions first and try to use them
	 * and check if bit was correctly setup or not.
	 */
	xspi->read_fn = xspi_read32;
	xspi->write_fn = xspi_write32;

	xspi->write_fn(XSPI_CR_LOOP, xspi->regs + XSPI_CR_OFFSET);
	tmp = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET);
	tmp &= XSPI_CR_LOOP;
	if (tmp != XSPI_CR_LOOP) {
		xspi->read_fn = xspi_read32_be;
		xspi->write_fn = xspi_write32_be;
	}

	master->bits_per_word_mask = SPI_BPW_MASK(bits_per_word);
	xspi->bits_per_word = bits_per_word;
	if (xspi->bits_per_word == 8) {
		xspi->tx_fn = xspi_tx8;
		xspi->rx_fn = xspi_rx8;
	} else if (xspi->bits_per_word == 16) {
		xspi->tx_fn = xspi_tx16;
		xspi->rx_fn = xspi_rx16;
	} else if (xspi->bits_per_word == 32) {
		xspi->tx_fn = xspi_tx32;
		xspi->rx_fn = xspi_rx32;
	} else {
		ret = -EINVAL;
		goto put_master;
	}

	/* SPI controller initializations */
	xspi_init_hw(xspi);

	xspi->irq = platform_get_irq(pdev, 0);
	if (xspi->irq < 0) {
		ret = xspi->irq;
		goto put_master;
	}

	/* Register for SPI Interrupt */
	ret = devm_request_irq(&pdev->dev, xspi->irq, xilinx_spi_irq, 0,
			       dev_name(&pdev->dev), xspi);
	if (ret)
		goto put_master;

	ret = spi_bitbang_start(&xspi->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "spi_bitbang_start FAILED\n");
		goto put_master;
	}

	dev_info(&pdev->dev, "at 0x%08llX mapped to 0x%p, irq=%d\n",
		(unsigned long long)res->start, xspi->regs, xspi->irq);

	if (pdata) {
		for (i = 0; i < pdata->num_devices; i++)
			spi_new_device(master, pdata->devices + i);
	}

	platform_set_drvdata(pdev, master);
	return 0;

put_master:
	spi_master_put(master);

	return ret;
}

static int xilinx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);
	void __iomem *regs_base = xspi->regs;

	spi_bitbang_stop(&xspi->bitbang);

	/* Disable all the interrupts just in case */
	xspi->write_fn(0, regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Disable the global IPIF interrupt */
	xspi->write_fn(0, regs_base + XIPIF_V123B_DGIER_OFFSET);

	spi_master_put(xspi->bitbang.master);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" XILINX_SPI_NAME);

static struct platform_driver xilinx_spi_driver = {
	.probe = xilinx_spi_probe,
	.remove = xilinx_spi_remove,
	.driver = {
		.name = XILINX_SPI_NAME,
		.of_match_table = xilinx_spi_of_match,
	},
};
module_platform_driver(xilinx_spi_driver);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx SPI driver");
MODULE_LICENSE("GPL");
