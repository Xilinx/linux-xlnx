/*
 * Xilinx Zynq UltraScale+ MPSoC Quad-SPI (QSPI) controller driver
 * (master mode only)
 *
 * Copyright (C) 2009 - 2015 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/* Name of this driver */
#define DRIVER_NAME		"zynqmp-qspi"

/* Generic qspi register offsets */
#define GQSPI_CONFIG_OFST		0x00000100
#define GQSPI_ISR_OFST			0X00000104
#define GQSPI_IDR_OFST			0X0000010C
#define GQSPI_IER_OFST			0X00000108
#define GQSPI_EN_OFST			0X00000114
#define GQSPI_TXD_OFST			0X0000011C
#define GQSPI_RXD_OFST			0x00000120
#define GQSPI_TX_THRESHOLD_OFST		0X00000128
#define GQSPI_RX_THRESHOLD_OFST		0X0000012C
#define GQSPI_LPBK_DLY_ADJ_OFST		0X00000138
#define GQSPI_GEN_FIFO_OFST		0X00000140
#define GQSPI_SEL_OFST			0x00000144
#define GQSPI_GF_THRESHOLD_OFST		0X00000150
#define GQSPI_FIFO_CTRL_OFST		0X0000014C
#define GQSPI_QSPIDMA_DST_STS_OFST	0X00000808
#define GQSPI_QSPIDMA_DST_I_STS_OFST	0X00000814
#define GQSPI_QSPIDMA_DST_I_DIS_OFST	0X0000081C

/* GQSPI Register Bit masks */
#define GQSPI_SEL_MASK				0X00000001
#define GQSPI_EN_MASK				0X00000001
#define GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK	0X00000020
#define GQSPI_ISR_WR_TO_CLR_MASK		0X00000002
#define GQSPI_IDR_ALL_MASK			0X00000FBE
#define GQSPI_CFG_MODE_EN_MASK			0XC0000000
#define GQSPI_CFG_GEN_FIFO_START_MODE_MASK	0X20000000
#define GQSPI_CFG_ENDIAN_MASK			0X04000000
#define GQSPI_CFG_EN_POLL_TO_MASK		0X00100000
#define GQSPI_CFG_WP_HOLD_MASK			0X00080000
#define GQSPI_CFG_BAUD_RATE_DIV_MASK		0X00000038
#define GQSPI_CFG_CLK_PHA_MASK			0X00000004
#define GQSPI_CFG_CLK_POL_MASK			0X00000002
#define GQSPI_CFG_START_GEN_FIFO_MASK		0X10000000
#define GQSPI_GENFIFO_IMM_DATA_MASK		0x000000FF
#define GQSPI_GENFIFO_DATA_XFER			0x00000100
#define GQSPI_GENFIFO_EXP			0x00000200
#define GQSPI_GENFIFO_MODE_SPI			0x00000400
#define GQSPI_GENFIFO_MODE_DUALSPI		0x00000800
#define GQSPI_GENFIFO_MODE_QUADSPI		0x00000C00
#define GQSPI_GENFIFO_MODE_MASK			0x00000C00
#define GQSPI_GENFIFO_CS_LOWER			0x00001000
#define GQSPI_GENFIFO_CS_UPPER			0x00002000
#define GQSPI_GENFIFO_BUS_LOWER			0x00004000
#define GQSPI_GENFIFO_BUS_UPPER			0x00008000
#define GQSPI_GENFIFO_BUS_BOTH			0x0000C000
#define GQSPI_GENFIFO_BUS_MASK			0x0000C000
#define GQSPI_GENFIFO_TX			0x00010000
#define GQSPI_GENFIFO_RX			0x00020000
#define GQSPI_GENFIFO_STRIPE			0x00040000
#define GQSPI_GENFIFO_POLL			0x00080000
#define GQSPI_GENFIFO_EXP_START			0x00000100
#define GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK	0X00000004
#define GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK	0X00000002
#define GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK	0X00000001
#define GQSPI_ISR_RXEMPTY_MASK			0X00000800
#define GQSPI_ISR_GENFIFOFULL_MASK		0X00000400
#define GQSPI_ISR_GENFIFONOT_FULL_MASK		0X00000200
#define GQSPI_ISR_TXEMPTY_MASK			0X00000100
#define GQSPI_ISR_GENFIFOEMPTY_MASK		0X00000080
#define GQSPI_ISR_RXFULL_MASK			0X00000020
#define GQSPI_ISR_RXNEMPTY_MASK			0X00000010
#define GQSPI_ISR_TXFULL_MASK			0X00000008
#define GQSPI_ISR_TXNOT_FULL_MASK		0X00000004
#define GQSPI_ISR_POLL_TIME_EXPIRE_MASK		0X00000002
#define GQSPI_IER_TXNOT_FULL_MASK		0X00000004
#define GQSPI_IER_RXEMPTY_MASK			0X00000800
#define GQSPI_IER_POLL_TIME_EXPIRE_MASK		0X00000002
#define GQSPI_IER_RXNEMPTY_MASK			0X00000010
#define GQSPI_IER_GENFIFOEMPTY_MASK		0X00000080
#define GQSPI_IER_TXEMPTY_MASK			0X00000100
#define GQSPI_QSPIDMA_DST_INTR_ALL_MASK		0X000000FE
#define GQSPI_QSPIDMA_DST_STS_WTC		0x0000E000
#define GQSPI_ISR_IDR_MASK			0x00000990

#define GQSPI_CFG_BAUD_RATE_DIV_SHIFT		3
#define GQSPI_GENFIFO_CS_SETUP			0x04
#define GQSPI_GENFIFO_CS_HOLD			0x03
#define GQSPI_TXD_DEPTH				64
#define GQSPI_RX_FIFO_THRESHOLD			32
#define GQSPI_RX_FIFO_FILL	(GQSPI_RX_FIFO_THRESHOLD * 4)
#define GQSPI_TX_FIFO_THRESHOLD_RESET_VAL	0x01
#define GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL	0X10
#define GQSPI_SELECT_FLASH_CS_LOWER		0x1
#define GQSPI_SELECT_FLASH_CS_UPPER		0x2
#define GQSPI_SELECT_FLASH_CS_BOTH		0x3
#define GQSPI_SELECT_FLASH_BUS_LOWER		0x1
#define GQSPI_SELECT_FLASH_BUS_UPPER		0x2
#define GQSPI_SELECT_FLASH_BUS_BOTH		0x3
#define GQSPI_BAUD_DIV_MAX	7	/* Baud rate divisor maximum */
#define GQSPI_SELECT_MODE_SPI		0x1
#define GQSPI_SELECT_MODE_DUALSPI	0x2
#define GQSPI_SELECT_MODE_QUADSPI	0x4

/* Default number of chip selects */
#define GQSPI_DEFAULT_NUM_CS	1

/**
 * struct zynqmp_qspi - Defines qspi driver instance
 * @regs:		Virtual address of the QSPI controller registers
 * @refclk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @irq:		IRQ number
 * @dev:		pointer to struct device
 * @txbuf:		Pointer to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @genfifocs:		Used for chip select
 * @genfifobus:		USed to select the upper or lower bus
 */
struct zynqmp_qspi {
	void __iomem *regs;
	struct clk *refclk;
	struct clk *pclk;
	int irq;
	struct device *dev;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u32 genfifocs;
	u32 genfifobus;
};

/* functions for the GQSPI controller read/write */
static u32 zynqmp_gqspi_read(struct zynqmp_qspi *xqspi, u32 offset)
{
	return readl_relaxed(xqspi->regs + offset);
}

static inline void zynqmp_gqspi_write(struct zynqmp_qspi *xqspi, u32 offset,
				      u32 val)
{
	writel_relaxed(val, (xqspi->regs + offset));
}

static void zynqmp_gqspi_selectflash(struct zynqmp_qspi *instanceptr,
				     u8 flashcs, u8 flashbus)
{
	/*
	 * Bus and CS lines selected here will be updated in the instance and
	 * used for subsequent GENFIFO entries during transfer.
	 */
	/* Choose slave select line */
	switch (flashcs) {
	case GQSPI_SELECT_FLASH_CS_BOTH:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER |
		    GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_UPPER:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_CS_LOWER:
	default:
		instanceptr->genfifocs = GQSPI_GENFIFO_CS_LOWER;
	}

	/* Choose bus */
	switch (flashbus) {
	case GQSPI_SELECT_FLASH_BUS_BOTH:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER |
		    GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_UPPER:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_UPPER;
		break;
	case GQSPI_SELECT_FLASH_BUS_LOWER:
	default:
		instanceptr->genfifobus = GQSPI_GENFIFO_BUS_LOWER;
	}
}

/**
 * zynqmp_qspi_init_hw - Initialize the hardware
 * @xqspi:	Pointer to the zynqmp_qspi structure
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Master mode
 *	- Tx thresold set to 1 Rx threshold set to 1
 *	- Flash memory interface mode enabled
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual slave select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the little endian mode of TX FIFO and
 *	- Enable the QSPI controller
 */
static void zynqmp_qspi_init_hw(struct zynqmp_qspi *xqspi)
{
	u32 config_reg;

	/* Select the Generic qspi mode */
	zynqmp_gqspi_write(xqspi, GQSPI_SEL_OFST, GQSPI_SEL_MASK);
	/* Clear and disable interrupts */
	zynqmp_gqspi_write(xqspi, GQSPI_ISR_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST) |
			   GQSPI_ISR_WR_TO_CLR_MASK);
	/* Clear the DMA STS */
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_I_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_I_STS_OFST));
	zynqmp_gqspi_write(xqspi, GQSPI_QSPIDMA_DST_STS_OFST,
			   zynqmp_gqspi_read(xqspi,
					     GQSPI_QSPIDMA_DST_STS_OFST) |
					     GQSPI_QSPIDMA_DST_STS_WTC);
	zynqmp_gqspi_write(xqspi, GQSPI_IDR_OFST, GQSPI_IDR_ALL_MASK);
	zynqmp_gqspi_write(xqspi,
			   GQSPI_QSPIDMA_DST_I_DIS_OFST,
			   GQSPI_QSPIDMA_DST_INTR_ALL_MASK);
	/* Disable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x00);
	config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
	config_reg &= ~GQSPI_CFG_MODE_EN_MASK;
	/* Manual start */
	config_reg |= GQSPI_CFG_GEN_FIFO_START_MODE_MASK;
	/* Little endain by default */
	config_reg &= ~GQSPI_CFG_ENDIAN_MASK;
	/* Disable poll timeout */
	config_reg &= ~GQSPI_CFG_EN_POLL_TO_MASK;
	/* Set hold bit */
	config_reg |= GQSPI_CFG_WP_HOLD_MASK;
	/* Clear prescalar by default */
	config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
	/* CPOL CPHA 00 */
	config_reg &= ~GQSPI_CFG_CLK_PHA_MASK;
	config_reg &= ~GQSPI_CFG_CLK_POL_MASK;
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);

	/* Clear the TX RX FIFO */
	zynqmp_gqspi_write(xqspi, GQSPI_FIFO_CTRL_OFST,
			   GQSPI_FIFO_CTRL_RST_RX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_TX_FIFO_MASK |
			   GQSPI_FIFO_CTRL_RST_GEN_FIFO_MASK);
	/* Set by default to allow for high frequencies */
	zynqmp_gqspi_write(xqspi, GQSPI_LPBK_DLY_ADJ_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_LPBK_DLY_ADJ_OFST) |
			   GQSPI_LPBK_DLY_ADJ_USE_LPBK_MASK);
	/* Reset thresholds */
	zynqmp_gqspi_write(xqspi, GQSPI_TX_THRESHOLD_OFST,
			   GQSPI_TX_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_write(xqspi, GQSPI_RX_THRESHOLD_OFST,
			   GQSPI_RX_FIFO_THRESHOLD);
	zynqmp_gqspi_write(xqspi, GQSPI_GF_THRESHOLD_OFST,
			   GQSPI_GEN_FIFO_THRESHOLD_RESET_VAL);
	zynqmp_gqspi_selectflash(xqspi,
				 GQSPI_SELECT_FLASH_CS_LOWER,
				 GQSPI_SELECT_FLASH_BUS_LOWER);
	/* Enable the GQSPI */
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);
}

/**
 * zynqmp_qspi_copy_read_data - Copy data to RX buffer
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @data:	The 32 bit variable where data is stored
 * @size:	Number of bytes to be copied from data to RX buffer
 */
static void zynqmp_qspi_copy_read_data(struct zynqmp_qspi *xqspi,
				       u32 data, u8 size)
{
	memcpy(xqspi->rxbuf, ((u8 *) &data), size);
	xqspi->rxbuf += size;
	xqspi->bytes_to_receive -= size;
}

/**
 * zynqmp_prepare_transfer_hardware - Prepares hardware for transfer.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function enables SPI master controller.
 *
 * Return:	Always 0
 */
static int zynqmp_prepare_transfer_hardware(struct spi_master *master)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	clk_enable(xqspi->refclk);
	clk_enable(xqspi->pclk);
	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, GQSPI_EN_MASK);
	return 0;
}

/**
 * zynqmp_unprepare_transfer_hardware - Relaxes hardware after transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function disables the SPI master controller.
 *
 * Return:	Always 0
 */
static int zynqmp_unprepare_transfer_hardware(struct spi_master *master)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x00);
	clk_disable(xqspi->refclk);
	clk_disable(xqspi->pclk);

	return 0;
}

/**
 * zynqmp_qspi_chipselect - Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 */
static void zynqmp_qspi_chipselect(struct spi_device *qspi, bool is_high)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 genfifoentry = 0x00, statusreg, timeout;

	genfifoentry |= GQSPI_GENFIFO_MODE_SPI;
	genfifoentry |= xqspi->genfifobus;
	if (!is_high) {
		genfifoentry |= xqspi->genfifocs;
		genfifoentry |= GQSPI_GENFIFO_CS_SETUP;
	} else {
		genfifoentry |= GQSPI_GENFIFO_CS_HOLD;
	}
	zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);
	if (is_high) {
		/* manual start the generic fifo command */
		zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
				   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
				   GQSPI_CFG_START_GEN_FIFO_MASK);
		timeout = 10000;
		/* wait until the generic fifo command is empty */
		do {
			statusreg = zynqmp_gqspi_read(xqspi, GQSPI_ISR_OFST);
			timeout--;
		} while (!(statusreg &
			   GQSPI_ISR_GENFIFOEMPTY_MASK) &&
			 (statusreg & GQSPI_ISR_TXEMPTY_MASK) && timeout);
		if (!timeout)
			dev_err(xqspi->dev, "Chip select timed out\n");

	}
}

/**
 * zynqmp_qspi_setup_transfer - Configure QSPI controller for specified transfer
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides information
 *		about next transfer setup parameters
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	0 on success and -EINVAL on invalid input parameter
 *
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value, the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the QSPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static int zynqmp_qspi_setup_transfer(struct spi_device *qspi,
				      struct spi_transfer *transfer)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg, req_hz, baud_rate_val = 0;

	if (transfer)
		req_hz = transfer->speed_hz;
	else
		req_hz = qspi->max_speed_hz;

	/* Set the clock frequency */
	/* If req_hz == 0, default to lowest speed */
	while ((baud_rate_val < GQSPI_BAUD_DIV_MAX) &&
	       (clk_get_rate(xqspi->refclk) / (2 << baud_rate_val)) > req_hz)
		baud_rate_val++;

	config_reg = zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST);
	/* Set the QSPI clock phase and clock polarity */
	config_reg &= (~GQSPI_CFG_CLK_PHA_MASK) & (~GQSPI_CFG_CLK_POL_MASK);
	if (qspi->mode & SPI_CPHA)
		config_reg |= GQSPI_CFG_CLK_PHA_MASK;
	if (qspi->mode & SPI_CPOL)
		config_reg |= GQSPI_CFG_CLK_POL_MASK;

	config_reg &= ~GQSPI_CFG_BAUD_RATE_DIV_MASK;
	config_reg |= (baud_rate_val << GQSPI_CFG_BAUD_RATE_DIV_SHIFT);
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST, config_reg);
	return 0;
}

/**
 * zynqmp_qspi_setup - Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer, baud
 * rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success and error value on failure
 */
static int zynqmp_qspi_setup(struct spi_device *qspi)
{
	if (qspi->master->busy)
		return -EBUSY;
	return zynqmp_qspi_setup_transfer(qspi, NULL);
}

/**
 * zynqmp_qspi_filltxfifo - Fills the TX FIFO as long as there is room in
 * the FIFO or the bytes required to be transmitted.
 *
 * @xqspi:	Pointer to the zynqmp_qspi structure
 * @size:	Number of bytes to be copied from TX buffer to TX fifo
 */
static void zynqmp_qspi_filltxfifo(struct zynqmp_qspi *xqspi, int size)
{
	u32 count = 0;

	while ((xqspi->bytes_to_transfer > 0) && (count < size)) {
		writel(*((u32 *) xqspi->txbuf), xqspi->regs + GQSPI_TXD_OFST);
		if (xqspi->bytes_to_transfer >= 4) {
			xqspi->txbuf += 4;
			xqspi->bytes_to_transfer -= 4;
		} else {
			xqspi->txbuf += xqspi->bytes_to_transfer;
			xqspi->bytes_to_transfer = 0;
		}
		count++;
	}
}

static void zynqmp_qspi_readrxfifo(struct zynqmp_qspi *xqspi, u32 size)
{
	int count = 0;
	u32 data;

	while ((count < size) && (xqspi->bytes_to_receive > 0)) {
		if (xqspi->bytes_to_receive >= 4) {
			(*(u32 *) xqspi->rxbuf) =
			    readl(xqspi->regs + GQSPI_RXD_OFST);
			xqspi->rxbuf += 4;
			xqspi->bytes_to_receive -= 4;
			count += 4;
		} else {
			data = readl(xqspi->regs + GQSPI_RXD_OFST);
			count += xqspi->bytes_to_receive;
			zynqmp_qspi_copy_read_data(xqspi, data,
						   xqspi->bytes_to_receive);
			xqspi->bytes_to_receive = 0;
		}
	}
}

/**
 * zynqmp_qspi_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED when interrupt is handled; IRQ_NONE otherwise.
 */
static irqreturn_t zynqmp_qspi_irq(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	int ret = IRQ_NONE;
	u32 status;

	status = readl(xqspi->regs + GQSPI_ISR_OFST);

	if (status & GQSPI_ISR_TXEMPTY_MASK) {
		zynqmp_qspi_filltxfifo(xqspi, GQSPI_TXD_DEPTH);
		ret = IRQ_HANDLED;
	}
	if (status & GQSPI_IER_RXNEMPTY_MASK) {
		zynqmp_qspi_readrxfifo(xqspi, GQSPI_RX_FIFO_FILL);
		ret = IRQ_HANDLED;
	} else if (!(status & GQSPI_IER_RXEMPTY_MASK)) {
		zynqmp_qspi_readrxfifo(xqspi, GQSPI_RX_FIFO_FILL);
		ret = IRQ_HANDLED;
	}

	if ((xqspi->bytes_to_receive == 0) &&
	    (xqspi->bytes_to_transfer == 0) &&
	    (status & GQSPI_IER_GENFIFOEMPTY_MASK) &&
	    (status & GQSPI_IER_TXEMPTY_MASK) &&
	    (status & GQSPI_IER_RXEMPTY_MASK)) {
		writel((GQSPI_IER_TXEMPTY_MASK |
			GQSPI_IER_GENFIFOEMPTY_MASK |
			GQSPI_IER_RXNEMPTY_MASK | GQSPI_IER_RXEMPTY_MASK),
		       xqspi->regs + GQSPI_IDR_OFST);
		spi_finalize_current_transfer(master);
		ret = IRQ_HANDLED;
	}

	writel(status, xqspi->regs + GQSPI_ISR_OFST);
	return ret;
}

/**
 * zynqmp_qspi_selectspimode - Selects SPI mode - x1 or x2 or x4.
 *
 * @spimode:	spimode - spi or dual or quad.
 * Return:	mask to set desired SPI mode in GENFIFO entry.
 */
static inline u32 zynqmp_qspi_selectspimode(u8 spimode)
{
	u32 mask;

	switch (spimode) {
	case GQSPI_SELECT_MODE_DUALSPI:
		mask = GQSPI_GENFIFO_MODE_DUALSPI;
		break;
	case GQSPI_SELECT_MODE_QUADSPI:
		mask = GQSPI_GENFIFO_MODE_QUADSPI;
		break;
	case GQSPI_SELECT_MODE_SPI:
	default:
		mask = GQSPI_GENFIFO_MODE_SPI;
	}
	return mask;
}

/**
 * zynqmp_qspi_txrxsetup - This function checks the TX/RX buffers in
 * the transfer and setups up the GENFIFO entries, TX FIFO as required.
 *
 * @xqspi:	xqspi is a pointer to the GQSPI instance.
 * @transfer:	it is a pointer to the structure containing transfer data.
 * @genfifoentry:	genfifoentry is pointer to the variable in which GENFIFO
 *			mask is returned to calling function
 */
static void zynqmp_qspi_txrxsetup(struct zynqmp_qspi *xqspi,
				  struct spi_transfer *transfer,
				  u32 *genfifoentry)
{
	/* Transmit */
	if ((xqspi->txbuf != NULL) && (xqspi->rxbuf == NULL)) {
		/* Setup data to be TXed */
		*genfifoentry &= ~GQSPI_GENFIFO_RX;
		*genfifoentry |= GQSPI_GENFIFO_DATA_XFER;
		*genfifoentry |= GQSPI_GENFIFO_TX;
		*genfifoentry |= zynqmp_qspi_selectspimode(transfer->tx_nbits);
		xqspi->bytes_to_transfer = transfer->len;
		zynqmp_qspi_filltxfifo(xqspi, GQSPI_TXD_DEPTH);
		/* Discard RX data */
		xqspi->bytes_to_receive = 0;
	} else if ((xqspi->txbuf == NULL) && (xqspi->rxbuf != NULL)) {
		/* Receive */

		/* TX auto fill */
		*genfifoentry &= ~GQSPI_GENFIFO_TX;
		/* Setup RX */
		*genfifoentry |= GQSPI_GENFIFO_DATA_XFER;
		*genfifoentry |= GQSPI_GENFIFO_RX;
		*genfifoentry |= zynqmp_qspi_selectspimode(transfer->rx_nbits);
		xqspi->bytes_to_transfer = 0;
		xqspi->bytes_to_receive = transfer->len;
	}
}

/**
 * zynqmp_qspi_start_transfer - Initiates the QSPI transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 * @qspi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the QSPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int zynqmp_qspi_start_transfer(struct spi_master *master,
				      struct spi_device *qspi,
				      struct spi_transfer *transfer)
{
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	u32 genfifoentry = 0x00;

	xqspi->txbuf = transfer->tx_buf;
	xqspi->rxbuf = transfer->rx_buf;

	genfifoentry |= xqspi->genfifocs;
	genfifoentry |= xqspi->genfifobus;
	zynqmp_qspi_txrxsetup(xqspi, transfer, &genfifoentry);

	if ((transfer->len) < GQSPI_GENFIFO_IMM_DATA_MASK) {
		genfifoentry &= ~GQSPI_GENFIFO_IMM_DATA_MASK;
		genfifoentry |= transfer->len;
		zynqmp_gqspi_write(xqspi, GQSPI_GEN_FIFO_OFST, genfifoentry);
	} else {
		int tempcount = transfer->len;
		u32 exponent = 8;	/* 2^8 = 256 */
		u8 imm_data = tempcount & 0xFF;

		tempcount &= ~(tempcount & 0xFF);
		/* Immediate entry */
		if (tempcount != 0) {
			/* Exponent entries */
			genfifoentry |= GQSPI_GENFIFO_EXP;
			while (tempcount != 0) {
				if (tempcount & GQSPI_GENFIFO_EXP_START) {
					genfifoentry &=
					    ~GQSPI_GENFIFO_IMM_DATA_MASK;
					genfifoentry |= exponent;
					zynqmp_gqspi_write(xqspi,
							   GQSPI_GEN_FIFO_OFST,
							   genfifoentry);
				}
				tempcount = tempcount >> 1;
				exponent++;
			}
		}
		if (imm_data != 0) {
			genfifoentry &= ~GQSPI_GENFIFO_EXP;
			genfifoentry &= ~GQSPI_GENFIFO_IMM_DATA_MASK;
			genfifoentry |= (u8) (imm_data & 0xFF);
			zynqmp_gqspi_write(xqspi,
					   GQSPI_GEN_FIFO_OFST, genfifoentry);
		}
	}

	/* Since we are using maual mode */
	zynqmp_gqspi_write(xqspi, GQSPI_CONFIG_OFST,
			   zynqmp_gqspi_read(xqspi, GQSPI_CONFIG_OFST) |
			   GQSPI_CFG_START_GEN_FIFO_MASK);

	if (xqspi->txbuf != NULL)
		/* Enable interrupts for Tx */
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				   GQSPI_IER_TXEMPTY_MASK |
				   GQSPI_IER_GENFIFOEMPTY_MASK);

	if (xqspi->rxbuf != NULL)
		/* Enable interrupts for Rx */
		zynqmp_gqspi_write(xqspi, GQSPI_IER_OFST,
				   GQSPI_IER_GENFIFOEMPTY_MASK |
				   GQSPI_IER_RXNEMPTY_MASK |
				   GQSPI_IER_RXEMPTY_MASK);

	return transfer->len;
}

/**
 * zynqmp_qspi_suspend - Suspend method for the QSPI driver
 * @_dev:	Address of the platform_device structure
 *
 * This function stops the QSPI driver queue and disables the QSPI controller
 *
 * Return:	Always 0
 */
static int __maybe_unused zynqmp_qspi_suspend(struct device *_dev)
{
	struct platform_device *pdev = container_of(_dev,
						    struct platform_device,
						    dev);
	struct spi_master *master = platform_get_drvdata(pdev);

	spi_master_suspend(master);

	zynqmp_unprepare_transfer_hardware(master);

	return 0;
}

/**
 * zynqmp_qspi_resume - Resume method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the QSPI driver queue and initializes the QSPI controller
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused zynqmp_qspi_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
						    struct platform_device,
						    dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = clk_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xqspi->pclk);
		return ret;
	}

	spi_master_resume(master);

	return 0;
}

static SIMPLE_DEV_PM_OPS(zynqmp_qspi_dev_pm_ops, zynqmp_qspi_suspend,
			 zynqmp_qspi_resume);

/**
 * zynqmp_qspi_probe - Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on failure
 */
static int zynqmp_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_master *master;
	struct zynqmp_qspi *xqspi;
	struct resource *res;
	struct device *dev = &pdev->dev;
	u32 num_cs;

	master = spi_alloc_master(&pdev->dev, sizeof(*xqspi));
	if (!master)
		return -ENOMEM;
	xqspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xqspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xqspi->regs)) {
		ret = PTR_ERR(xqspi->regs);
		goto remove_master;
	}
	xqspi->dev = dev;
	xqspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xqspi->pclk)) {
		dev_err(dev, "pclk clock not found.\n");
		ret = PTR_ERR(xqspi->pclk);
		goto remove_master;
	}

	xqspi->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->refclk)) {
		dev_err(dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xqspi->refclk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret) {
		dev_err(dev, "Unable to enable APB clock.\n");
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(dev, "Unable to enable device clock.\n");
		goto clk_dis_pclk;
	}

	/* QSPI controller initializations */
	zynqmp_qspi_init_hw(xqspi);

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq <= 0) {
		ret = -ENXIO;
		dev_err(dev, "irq resource not found\n");
		goto remove_master;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynqmp_qspi_irq,
			       0, pdev->name, master);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(dev, "request_irq failed\n");
		goto remove_master;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "num-cs", &num_cs);
	if (ret < 0)
		master->num_chipselect = GQSPI_DEFAULT_NUM_CS;
	else
		master->num_chipselect = num_cs;

	master->setup = zynqmp_qspi_setup;
	master->set_cs = zynqmp_qspi_chipselect;
	master->transfer_one = zynqmp_qspi_start_transfer;
	master->prepare_transfer_hardware = zynqmp_prepare_transfer_hardware;
	master->unprepare_transfer_hardware =
					zynqmp_unprepare_transfer_hardware;
	master->max_speed_hz = clk_get_rate(xqspi->refclk) / 2;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD |
			    SPI_TX_DUAL | SPI_TX_QUAD;
	if (master->dev.parent == NULL)
		master->dev.parent = &master->dev;
	ret = spi_register_master(master);
	if (ret)
		goto clk_dis_all;

	return ret;

clk_dis_all:
	clk_disable_unprepare(xqspi->refclk);
clk_dis_pclk:
	clk_disable_unprepare(xqspi->pclk);
remove_master:
	spi_master_put(master);
	return ret;
}

/**
 * zynqmp_qspi_remove - Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on failure
 */
static int zynqmp_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynqmp_qspi *xqspi = spi_master_get_devdata(master);

	zynqmp_gqspi_write(xqspi, GQSPI_EN_OFST, 0x00);
	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);

	spi_unregister_master(master);

	return 0;
}

static const struct of_device_id zynqmp_qspi_of_match[] = {
	{ .compatible = "xlnx,zynqmp-qspi-1.0", },
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, zynqmp_qspi_of_match);

static struct platform_driver zynqmp_qspi_driver = {
	.probe = zynqmp_qspi_probe,
	.remove = zynqmp_qspi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = zynqmp_qspi_of_match,
		.pm = &zynqmp_qspi_dev_pm_ops,
	},
};

module_platform_driver(zynqmp_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynqmp QSPI driver");
MODULE_LICENSE("GPL");
