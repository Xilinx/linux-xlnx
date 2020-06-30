// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx SPI controller driver (master mode only)
 *
 * Author: MontaVista Software, Inc.
 *	source@mvista.com
 *
 * Copyright (c) 2010 Secret Lab Technologies, Ltd.
 * Copyright (c) 2009 Intel Corporation
 * 2002-2007 (c) MontaVista Software, Inc.

 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#define XILINX_SPI_MAX_CS	32

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
#define XSPI_CR_MODE_MASK	(XSPI_CR_CPHA | XSPI_CR_CPOL | \
				 XSPI_CR_LSB_FIRST | XSPI_CR_LOOP)
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

/* Number of bits per word */
#define XSPI_ONE_BITS_PER_WORD 1
#define XSPI_TWO_BITS_PER_WORD 2
#define XSPI_FOUR_BITS_PER_WORD 4

/* Number of data lines used to receive */
#define XSPI_RX_ONE_WIRE	1
#define XSPI_RX_FOUR_WIRE	4

/* Auto suspend timeout in milliseconds */
#define SPI_AUTOSUSPEND_TIMEOUT		3000

/* Command used for Dummy read Id */
#define SPI_READ_ID		0x9F

/**
 * struct xilinx_spi - This definition define spi driver instance
 * @regs:		virt. address of the control registers
 * @irq:		IRQ number
 * @axi_clk:		Pointer to the AXI clock
 * @axi4_clk:		Pointer to the AXI4 clock
 * @spi_clk:		Pointer to the SPI clock
 * @dev:		Pointer to the device
 * @rx_ptr:		Pointer to the RX buffer
 * @tx_ptr:		Pointer to the TX buffer
 * @bytes_per_word:	Number of bytes in a word
 * @buffer_size:	Buffer size in words
 * @cs_inactive:	Level of the CS pins when inactive
 * @read_fn:		For reading data from SPI registers
 * @write_fn:		For writing data to SPI registers
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @rx_bus_width:	Number of wires used to receive data
 * @tx_fifo:		For writing data to fifo
 * @rx_fifo:		For reading data from fifo
 */
struct xilinx_spi {
	void __iomem	*regs;	/* virt. address of the control registers */

	int		irq;

	struct clk *axi_clk;
	struct clk *axi4_clk;
	struct clk *spi_clk;
	struct device *dev;
	u8 *rx_ptr;		/* pointer in the Tx buffer */
	const u8 *tx_ptr;	/* pointer in the Rx buffer */
	u8 bytes_per_word;
	int buffer_size;	/* buffer size in words */
	u32 cs_inactive;	/* Level of the CS pins when inactive*/
	unsigned int (*read_fn)(void __iomem *);
	void (*write_fn)(u32, void __iomem *);
	u32 bytes_to_transfer;
	u32 bytes_to_receive;
	u32 rx_bus_width;
	void (*tx_fifo)(struct xilinx_spi *xqspi);
	void (*rx_fifo)(struct xilinx_spi *xqspi);
};

/**
 * XSPI_FIFO_READ - Generate xspi_read_rx_fifo_* functions
 * @size: bits_per_word that are read from RX FIFO
 * @type: C type of value argument
 *
 * Generates xspi_read_rx_fifo_* functions used to write
 * data into RX FIFO for different transaction widths.
 */
#define XSPI_FIFO_READ(size, type)					\
static void xspi_read_rx_fifo_##size(struct xilinx_spi *xqspi)		\
{									\
	int i;								\
	int count = (xqspi->bytes_to_receive > xqspi->buffer_size) ?	\
			xqspi->buffer_size : xqspi->bytes_to_receive;	\
	u32 data;							\
	for (i = 0; i < count; i += (size / 8)) {			\
		data = readl_relaxed(xqspi->regs + XSPI_RXD_OFFSET);	\
		if (xqspi->rx_ptr)					\
			((type *)xqspi->rx_ptr)[i] = (type)data;	\
	}								\
	xqspi->bytes_to_receive -= count;				\
	if (xqspi->rx_ptr)						\
		xqspi->rx_ptr += count;					\
}

/**
 * XSPI_FIFO_WRITE - Generate xspi_fill_tx_fifo_* functions
 * @size: bits_per_word that are written into TX FIFO
 * @type: C type of value argument
 *
 * Generates xspi_fill_tx_fifo_* functions used to write
 * data into TX FIFO for different transaction widths.
 */
#define XSPI_FIFO_WRITE(size, type)					\
static void xspi_fill_tx_fifo_##size(struct xilinx_spi *xqspi)		\
{									\
	int i;								\
	int count = (xqspi->bytes_to_transfer > xqspi->buffer_size) ?	\
			xqspi->buffer_size : xqspi->bytes_to_transfer;	\
	u32 data = 0;							\
	for (i = 0; i < count; i += (size / 8)) {			\
		if (xqspi->tx_ptr)					\
			data = *(type *)&xqspi->tx_ptr[i];		\
		writel_relaxed(data, (xqspi->regs + XSPI_TXD_OFFSET));	\
	}								\
	xqspi->bytes_to_transfer -= count;				\
	if (xqspi->tx_ptr)						\
		xqspi->tx_ptr += count;					\
}

XSPI_FIFO_READ(8, u8)
XSPI_FIFO_READ(16, u16)
XSPI_FIFO_READ(32, u32)
XSPI_FIFO_WRITE(8, u8)
XSPI_FIFO_WRITE(16, u16)
XSPI_FIFO_WRITE(32, u32)
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

/**
 * xspi_init_hw - Initialize the hardware
 * @xspi:	Pointer to the zynqmp_qspi structure
 *
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual slave select
 *	- Enable the SPI controller
 */
static void xspi_init_hw(struct xilinx_spi *xspi)
{
	void __iomem *regs_base = xspi->regs;

	/* Reset the SPI device */
	xspi->write_fn(XIPIF_V123B_RESET_MASK,
		regs_base + XIPIF_V123B_RESETR_OFFSET);
	/* Enable the transmit empty interrupt, which we use to determine
	 * progress on the transmission.
	 */
	xspi->write_fn(XSPI_INTR_TX_EMPTY,
			regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Disable the global IPIF interrupt */
	xspi->write_fn(0, regs_base + XIPIF_V123B_DGIER_OFFSET);
	/* Deselect the slave on the SPI bus */
	xspi->write_fn(0xffff, regs_base + XSPI_SSR_OFFSET);
	/* Disable the transmitter, enable Manual Slave Select Assertion,
	 * put SPI controller into master mode, and enable it */
	xspi->write_fn(XSPI_CR_MANUAL_SSELECT |	XSPI_CR_MASTER_MODE |
		XSPI_CR_ENABLE | XSPI_CR_TXFIFO_RESET |	XSPI_CR_RXFIFO_RESET,
		regs_base + XSPI_CR_OFFSET);
}

/**
 * xspi_chipselect -	Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 *
 */
static void xspi_chipselect(struct spi_device *qspi, bool is_high)
{
	struct xilinx_spi *xqspi = spi_master_get_devdata(qspi->master);
	u32 cs;

	if (is_high) {
		/* Deselect the slave */
		xqspi->write_fn(xqspi->cs_inactive,
			xqspi->regs + XSPI_SSR_OFFSET);
	} else {
		cs = xqspi->cs_inactive;
		cs ^= BIT(qspi->chip_select);
		/* Activate the chip select */
		xqspi->write_fn(cs, xqspi->regs + XSPI_SSR_OFFSET);
	}
}

/**
 * xilinx_spi_startup_block - Perform a dummy read as a
 * work around for the startup block issue in the spi controller.
 * @xspi:	Pointer to the xilinx_spi structure
 * @cs_num:	chip select number.
 *
 * Perform a dummy read if startup block is enabled in the
 * spi controller.
 *
 * Return:	None
 */
static void xilinx_spi_startup_block(struct xilinx_spi *xspi, u32 cs_num)
{
	void __iomem *regs_base = xspi->regs;
	u32 chip_sel, config_reg, status_reg;

	/* Activate the chip select */
	chip_sel = xspi->cs_inactive;
	chip_sel ^= BIT(cs_num);
	xspi->write_fn(chip_sel, regs_base + XSPI_SSR_OFFSET);

	/* Write ReadId to the TXD register */
	xspi->write_fn(SPI_READ_ID, regs_base + XSPI_TXD_OFFSET);
	xspi->write_fn(0x0, regs_base + XSPI_TXD_OFFSET);
	xspi->write_fn(0x0, regs_base + XSPI_TXD_OFFSET);

	config_reg = xspi->read_fn(regs_base + XSPI_CR_OFFSET);
	/* Enable master transaction  */
	config_reg &= ~XSPI_CR_TRANS_INHIBIT;
	xspi->write_fn(config_reg, regs_base + XSPI_CR_OFFSET);

	status_reg = xspi->read_fn(regs_base + XSPI_SR_OFFSET);
	while ((status_reg & XSPI_SR_TX_EMPTY_MASK) == 0)
		status_reg = xspi->read_fn(regs_base + XSPI_SR_OFFSET);

	/* Disable master transaction */
	config_reg |= XSPI_CR_TRANS_INHIBIT;
	xspi->write_fn(config_reg, regs_base + XSPI_CR_OFFSET);

	/* Read the RXD Register */
	status_reg = xspi->read_fn(regs_base + XSPI_SR_OFFSET);
	while ((status_reg & XSPI_SR_RX_EMPTY_MASK) == 0) {
		xspi->read_fn(regs_base + XSPI_RXD_OFFSET);
		status_reg = xspi->read_fn(regs_base + XSPI_SR_OFFSET);
	}

	xspi_init_hw(xspi);
}

/**
 * xilinx_spi_setup_transfer - Configure SPI controller for specified
 *			 transfer
 * @spi:	Pointer to the spi_device structure
 * @t:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the operational mode of QSPI controller for the next QSPI
 * transfer.
 *
 * Return:	0 always
 */
static int xilinx_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);
	u32 config_reg;

	config_reg = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET);
	/* Set the QSPI clock phase and clock polarity */
	config_reg &= ~(XSPI_CR_CPHA | XSPI_CR_CPOL);
	if (spi->mode & SPI_CPHA)
		config_reg |= XSPI_CR_CPHA;
	if (spi->mode & SPI_CPOL)
		config_reg |= XSPI_CR_CPOL;
	if (spi->mode & SPI_LSB_FIRST)
		config_reg |= XSPI_CR_LSB_FIRST;
	xspi->write_fn(config_reg, xspi->regs + XSPI_CR_OFFSET);

	if (spi->mode & SPI_CS_HIGH)
		xspi->cs_inactive &= ~BIT(spi->chip_select);
	else
		xspi->cs_inactive |= BIT(spi->chip_select);

	return 0;
}

/**
 * xspi_setup -	Configure the SPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI
 * transfer.
 *
 * Return:	0 on success; error value otherwise.
 */
static int xspi_setup(struct spi_device *qspi)
{
	int ret;
	struct xilinx_spi *xqspi = spi_master_get_devdata(qspi->master);

	if (qspi->master->busy)
		return -EBUSY;

	ret = pm_runtime_get_sync(xqspi->dev);
	if (ret < 0)
		return ret;

	ret = xilinx_spi_setup_transfer(qspi, NULL);
	pm_runtime_put_sync(xqspi->dev);

	return ret;
}

/**
 * xspi_start_transfer - Initiates the SPI transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the SPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */

static int xspi_start_transfer(struct spi_master *master,
			       struct spi_device *qspi,
			       struct spi_transfer *transfer)
{
	struct xilinx_spi *xqspi = spi_master_get_devdata(master);
	u32 cr;

	xqspi->tx_ptr = transfer->tx_buf;
	xqspi->rx_ptr = transfer->rx_buf;

	if (transfer->dummy) {
		xqspi->bytes_to_transfer = (transfer->len - (transfer->dummy / 8))
							+ ((transfer->dummy / 8) *
							xqspi->rx_bus_width);
		xqspi->bytes_to_receive = (transfer->len - (transfer->dummy / 8))
							+ ((transfer->dummy / 8) *
							xqspi->rx_bus_width);
	} else {
		xqspi->bytes_to_transfer = transfer->len;
		xqspi->bytes_to_receive = transfer->len;
	}

	xilinx_spi_setup_transfer(qspi, transfer);
	cr = xqspi->read_fn(xqspi->regs + XSPI_CR_OFFSET);
	/* Enable master transaction inhibit */
	cr |= XSPI_CR_TRANS_INHIBIT;
	xqspi->write_fn(cr, xqspi->regs + XSPI_CR_OFFSET);
	xqspi->tx_fifo(xqspi);
	/* Disable master transaction inhibit */
	cr &= ~XSPI_CR_TRANS_INHIBIT;
	xqspi->write_fn(cr, xqspi->regs + XSPI_CR_OFFSET);
	xqspi->write_fn(XIPIF_V123B_GINTR_ENABLE,
			xqspi->regs + XIPIF_V123B_DGIER_OFFSET);

	return transfer->len;
}

/**
 * xspi_prepare_transfer_hardware -	Prepares hardware for transfer.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function enables SPI master controller.
 *
 * Return:	0 on success; error value otherwise
 */
static int xspi_prepare_transfer_hardware(struct spi_master *master)
{
	struct xilinx_spi *xqspi = spi_master_get_devdata(master);

	u32 cr;
	int ret;

	ret = pm_runtime_get_sync(xqspi->dev);
	if (ret < 0)
		return ret;

	cr = xqspi->read_fn(xqspi->regs + XSPI_CR_OFFSET);
	cr |= XSPI_CR_ENABLE;
	xqspi->write_fn(cr, xqspi->regs + XSPI_CR_OFFSET);

	return 0;
}

/**
 * xspi_unprepare_transfer_hardware -	Relaxes hardware after transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function disables the SPI master controller.
 *
 * Return:	Always 0
 */
static int xspi_unprepare_transfer_hardware(struct spi_master *master)
{
	struct xilinx_spi *xqspi = spi_master_get_devdata(master);
	u32 cr;

	cr = xqspi->read_fn(xqspi->regs + XSPI_CR_OFFSET);
	cr &= ~XSPI_CR_ENABLE;
	xqspi->write_fn(cr, xqspi->regs + XSPI_CR_OFFSET);

	pm_runtime_put_sync(xqspi->dev);

	return 0;
}

/**
 * xilinx_spi_runtime_resume - Runtime resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function enables the clocks
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused xilinx_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_enable(xspi->axi_clk);
	if (ret) {
		dev_err(dev, "Can not enable AXI clock\n");
		return ret;
	}

	ret = clk_enable(xspi->axi4_clk);
	if (ret) {
		dev_err(dev, "Can not enable AXI4 clock\n");
		goto clk_disable_axi_clk;
	}

	ret = clk_enable(xspi->spi_clk);
	if (ret) {
		dev_err(dev, "Can not enable SPI clock\n");
		goto clk_disable_axi4_clk;
	}

	return 0;

clk_disable_axi4_clk:
	clk_disable(xspi->axi4_clk);
clk_disable_axi_clk:
	clk_disable(xspi->axi_clk);

	return ret;
}

/**
 * xilinx_spi_runtime_suspend - Runtime suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the clocks
 *
 * Return:	Always 0
 */
static int __maybe_unused xilinx_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);

	clk_disable(xspi->axi_clk);
	clk_disable(xspi->axi4_clk);
	clk_disable(xspi->spi_clk);

	return 0;
}

/**
 * xilinx_spi_resume - Resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the SPI driver queue and initializes the SPI
 * controller
 *
 * Return:	0 on success; error value otherwise
 */
static int __maybe_unused xilinx_spi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);
	int ret = 0;

	if (!pm_runtime_suspended(dev)) {
		ret = xilinx_spi_runtime_resume(dev);
		if (ret < 0)
			return ret;
	}

	ret = spi_master_resume(master);
	if (ret < 0) {
		clk_disable(xspi->axi_clk);
		clk_disable(xspi->axi4_clk);
		clk_disable(xspi->spi_clk);
	}

	return ret;
}

/**
 * xilinx_spi_suspend - Suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function stops the SPI driver queue and disables the SPI controller
 *
 * Return:	Always 0
 */
static int __maybe_unused xilinx_spi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret = 0;

	ret = spi_master_suspend(master);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(dev))
		xilinx_spi_runtime_suspend(dev);

	xspi_unprepare_transfer_hardware(master);

	return ret;
}

static const struct dev_pm_ops xilinx_spi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(xilinx_spi_runtime_suspend,
			   xilinx_spi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(xilinx_spi_suspend, xilinx_spi_resume)
};

/* This driver supports single master mode only. Hence Tx FIFO Empty
 * is the only interrupt we care about.
 * Receive FIFO Overrun, Transmit FIFO Underrun, Mode Fault, and Slave Mode
 * Fault are not to happen.
 */
static irqreturn_t xilinx_spi_irq(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct xilinx_spi *xspi = spi_master_get_devdata(dev_id);
	u32 ipif_isr;
	irqreturn_t status = IRQ_NONE;

	/* Get the IPIF interrupts, and clear them immediately */
	ipif_isr = xspi->read_fn(xspi->regs + XIPIF_V123B_IISR_OFFSET);
	xspi->write_fn(ipif_isr, xspi->regs + XIPIF_V123B_IISR_OFFSET);
	if (ipif_isr & XSPI_INTR_TX_EMPTY)  {
		/* Transmission completed */
		xspi->rx_fifo(xspi);
		if (xspi->bytes_to_transfer) {
			/* There is more data to send */
			xspi->tx_fifo(xspi);
		}
		status = IRQ_HANDLED;
	}

	if (!xspi->bytes_to_receive && !xspi->bytes_to_transfer) {
		spi_finalize_current_transfer(master);
		/* Disable the interrupts here. */
		xspi->write_fn(0x0, xspi->regs + XIPIF_V123B_DGIER_OFFSET);
	}

	return status;
}
static int xilinx_spi_probe(struct platform_device *pdev)
{
	struct xilinx_spi *xspi;
	struct resource *res;
	int ret;
	u32 num_cs = 0, bits_per_word = 8;
	u32 cs_num;
	struct spi_master *master;
	struct device_node *nc;
	u32 tmp, rx_bus_width, fifo_size;
	bool startup_block;

	if (of_property_read_u32(pdev->dev.of_node, "num-cs", &num_cs))
		dev_info(&pdev->dev,
			 "Missing num-cs optional property, assuming default as <1>\n");
	if (!num_cs)
		num_cs = 1;

	if (num_cs > XILINX_SPI_MAX_CS) {
		dev_err(&pdev->dev, "Invalid number of spi slaves\n");
		return -EINVAL;
	}

	startup_block = of_property_read_bool(pdev->dev.of_node,
					      "xlnx,startup-block");
	master = spi_alloc_master(&pdev->dev, sizeof(struct xilinx_spi));
	if (!master)
		return -ENODEV;

	xspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xspi->regs)) {
		ret = PTR_ERR(xspi->regs);
		goto put_master;
	}
	ret = of_property_read_u32(pdev->dev.of_node, "fifo-size",
				   &fifo_size);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Missing fifo size\n");
		return -EINVAL;
	}
	if (of_property_read_u32(pdev->dev.of_node, "bits-per-word",
				 &bits_per_word))
		dev_info(&pdev->dev,
			 "Missing bits-per-word optional property, assuming default as <8>\n");

	xspi->rx_bus_width = XSPI_ONE_BITS_PER_WORD;
	for_each_available_child_of_node(pdev->dev.of_node, nc) {
		if (startup_block) {
			ret = of_property_read_u32(nc, "reg",
						   &cs_num);
			if (ret < 0)
				return -EINVAL;
		}
		ret = of_property_read_u32(nc, "spi-rx-bus-width",
					   &rx_bus_width);
		if (!ret) {
			xspi->rx_bus_width = rx_bus_width;
			break;
		}
	}

	xspi->axi_clk = devm_clk_get(&pdev->dev, "axi_clk");
	if (IS_ERR(xspi->axi_clk)) {
		if (PTR_ERR(xspi->axi_clk) != -ENOENT) {
			ret = PTR_ERR(xspi->axi_clk);
			goto put_master;
		}

		/*
		 * Clock framework support is optional, continue on,
		 * anyways if we don't find a matching clock
		 */
		xspi->axi_clk = NULL;
	}

	ret = clk_prepare(xspi->axi_clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare AXI clock\n");
		goto put_master;
	}

	xspi->axi4_clk = devm_clk_get(&pdev->dev, "axi4_clk");
	if (IS_ERR(xspi->axi4_clk)) {
		if (PTR_ERR(xspi->axi4_clk) != -ENOENT) {
			ret = PTR_ERR(xspi->axi4_clk);
			goto clk_unprepare_axi_clk;
		}

		/*
		 * Clock framework support is optional, continue on,
		 * anyways if we don't find a matching clock
		 */
		xspi->axi4_clk = NULL;
	}

	ret = clk_prepare(xspi->axi4_clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare AXI4 clock\n");
		goto clk_unprepare_axi_clk;
	}

	xspi->spi_clk = devm_clk_get(&pdev->dev, "spi_clk");
	if (IS_ERR(xspi->spi_clk)) {
		if (PTR_ERR(xspi->spi_clk) != -ENOENT) {
			ret = PTR_ERR(xspi->spi_clk);
			goto clk_unprepare_axi4_clk;
		}

		/*
		 * Clock framework support is optional, continue on,
		 * anyways if we don't find a matching clock
		 */
		xspi->spi_clk = NULL;
	}

	ret = clk_prepare(xspi->spi_clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare SPI clock\n");
		goto clk_unprepare_axi4_clk;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto clk_unprepare_all;

	xspi->dev = &pdev->dev;

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

	xspi->buffer_size = fifo_size;
	xspi->irq = platform_get_irq(pdev, 0);
	if (xspi->irq < 0 && xspi->irq != -ENXIO) {
		ret = xspi->irq;
		goto clk_unprepare_all;
	} else if (xspi->irq >= 0) {
		/* Register for SPI Interrupt */
		ret = devm_request_irq(&pdev->dev, xspi->irq, xilinx_spi_irq,
				       0, dev_name(&pdev->dev), master);
		if (ret)
			goto clk_unprepare_all;
	}

	/* SPI controller initializations */
	xspi_init_hw(xspi);

	pm_runtime_put(&pdev->dev);

	master->bus_num = pdev->id;
	master->num_chipselect = num_cs;
	master->setup = xspi_setup;
	master->set_cs = xspi_chipselect;
	master->transfer_one = xspi_start_transfer;
	master->prepare_transfer_hardware = xspi_prepare_transfer_hardware;
	master->unprepare_transfer_hardware = xspi_unprepare_transfer_hardware;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	xspi->bytes_per_word = bits_per_word / 8;
	xspi->tx_fifo = xspi_fill_tx_fifo_8;
	xspi->rx_fifo = xspi_read_rx_fifo_8;
	if (xspi->rx_bus_width == XSPI_RX_ONE_WIRE) {
		if (xspi->bytes_per_word == XSPI_TWO_BITS_PER_WORD) {
			xspi->tx_fifo = xspi_fill_tx_fifo_16;
			xspi->rx_fifo = xspi_read_rx_fifo_16;
		} else if (xspi->bytes_per_word == XSPI_FOUR_BITS_PER_WORD) {
			xspi->tx_fifo = xspi_fill_tx_fifo_32;
			xspi->rx_fifo = xspi_read_rx_fifo_32;
		}
	} else if (xspi->rx_bus_width == XSPI_RX_FOUR_WIRE) {
		master->mode_bits |= SPI_TX_QUAD | SPI_RX_QUAD;
	} else {
		dev_err(&pdev->dev, "Dual Mode not supported\n");
		goto clk_unprepare_all;
	}
	xspi->cs_inactive = 0xffffffff;

	/*
	 * This is the work around for the startup block issue in
	 * the spi controller. SPI clock is passing through STARTUP
	 * block to FLASH. STARTUP block don't provide clock as soon
	 * as QSPI provides command. So first command fails.
	 */
	if (startup_block)
		xilinx_spi_startup_block(xspi, cs_num);

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto clk_unprepare_all;
	}

	return ret;

clk_unprepare_all:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_unprepare(xspi->spi_clk);
clk_unprepare_axi4_clk:
	clk_unprepare(xspi->axi4_clk);
clk_unprepare_axi_clk:
	clk_unprepare(xspi->axi_clk);
put_master:
	spi_master_put(master);

	return ret;
}

/**
 * xilinx_spi_remove -	Remove method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 Always
 */
static int xilinx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);
	void __iomem *regs_base = xspi->regs;

	/* Disable all the interrupts just in case */
	xspi->write_fn(0, regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Disable the global IPIF interrupt */
	xspi->write_fn(0, regs_base + XIPIF_V123B_DGIER_OFFSET);

	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(xspi->axi_clk);
	clk_disable_unprepare(xspi->axi4_clk);
	clk_disable_unprepare(xspi->spi_clk);

	spi_unregister_master(master);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" XILINX_SPI_NAME);

static const struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,axi-quad-spi-1.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static struct platform_driver xilinx_spi_driver = {
	.probe = xilinx_spi_probe,
	.remove = xilinx_spi_remove,
	.driver = {
		.name = XILINX_SPI_NAME,
		.of_match_table = xilinx_spi_of_match,
		.pm = &xilinx_spi_dev_pm_ops,
	},
};
module_platform_driver(xilinx_spi_driver);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx SPI driver");
MODULE_LICENSE("GPL");
