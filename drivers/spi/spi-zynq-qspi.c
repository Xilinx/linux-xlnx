/*
 * Xilinx Zynq Quad-SPI (QSPI) controller driver (master mode only)
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

/* Name of this driver */
#define DRIVER_NAME			"zynq-qspi"

/* Register offset definitions */
#define ZYNQ_QSPI_CONFIG_OFFSET		0x00 /* Configuration  Register, RW */
#define ZYNQ_QSPI_STATUS_OFFSET		0x04 /* Interrupt Status Register, RO */
#define ZYNQ_QSPI_IEN_OFFSET		0x08 /* Interrupt Enable Register, WO */
#define ZYNQ_QSPI_IDIS_OFFSET		0x0C /* Interrupt Disable Reg, WO */
#define ZYNQ_QSPI_IMASK_OFFSET		0x10 /* Interrupt Enabled Mask Reg,RO */
#define ZYNQ_QSPI_ENABLE_OFFSET		0x14 /* Enable/Disable Register, RW */
#define ZYNQ_QSPI_DELAY_OFFSET		0x18 /* Delay Register, RW */
#define ZYNQ_QSPI_TXD_00_00_OFFSET	0x1C /* Transmit 4-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_01_OFFSET	0x80 /* Transmit 1-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_10_OFFSET	0x84 /* Transmit 2-byte inst, WO */
#define ZYNQ_QSPI_TXD_00_11_OFFSET	0x88 /* Transmit 3-byte inst, WO */
#define ZYNQ_QSPI_RXD_OFFSET		0x20 /* Data Receive Register, RO */
#define ZYNQ_QSPI_SIC_OFFSET		0x24 /* Slave Idle Count Register, RW */
#define ZYNQ_QSPI_TX_THRESH_OFFSET	0x28 /* TX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_RX_THRESH_OFFSET	0x2C /* RX FIFO Watermark Reg, RW */
#define ZYNQ_QSPI_GPIO_OFFSET		0x30 /* GPIO Register, RW */
#define ZYNQ_QSPI_LINEAR_CFG_OFFSET	0xA0 /* Linear Adapter Config Ref, RW */
#define ZYNQ_QSPI_MOD_ID_OFFSET		0xFC /* Module ID Register, RO */

/*
 * QSPI Configuration Register bit Masks
 *
 * This register contains various control bits that effect the operation
 * of the QSPI controller
 */
#define ZYNQ_QSPI_CONFIG_IFMODE_MASK	0x80000000 /* Flash Memory Interface */
#define ZYNQ_QSPI_CONFIG_MANSRT_MASK	0x00010000 /* Manual TX Start */
#define ZYNQ_QSPI_CONFIG_MANSRTEN_MASK	0x00008000 /* Enable Manual TX Mode */
#define ZYNQ_QSPI_CONFIG_SSFORCE_MASK	0x00004000 /* Manual Chip Select */
#define ZYNQ_QSPI_CONFIG_BDRATE_MASK	0x00000038 /* Baud Rate Divisor Mask */
#define ZYNQ_QSPI_CONFIG_CPHA_MASK	0x00000004 /* Clock Phase Control */
#define ZYNQ_QSPI_CONFIG_CPOL_MASK	0x00000002 /* Clock Polarity Control */
#define ZYNQ_QSPI_CONFIG_SSCTRL_MASK	0x00000400 /* Slave Select Mask */
#define ZYNQ_QSPI_CONFIG_FWIDTH_MASK	0x000000C0 /* FIFO width */
#define ZYNQ_QSPI_CONFIG_MSTREN_MASK	0x00000001 /* Master Mode */

/*
 * QSPI Configuration Register - Baud rate and slave select
 *
 * These are the values used in the calculation of baud rate divisor and
 * setting the slave select.
 */
#define ZYNQ_QSPI_BAUD_DIV_MAX		7 /* Baud rate divisor maximum */
#define ZYNQ_QSPI_BAUD_DIV_SHIFT	3 /* Baud rate divisor shift in CR */
#define ZYNQ_QSPI_SS_SHIFT		10 /* Slave Select field shift in CR */

/*
 * QSPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define ZYNQ_QSPI_IXR_TXNFULL_MASK	0x00000004 /* QSPI TX FIFO Overflow */
#define ZYNQ_QSPI_IXR_TXFULL_MASK	0x00000008 /* QSPI TX FIFO is full */
#define ZYNQ_QSPI_IXR_RXNEMTY_MASK	0x00000010 /* QSPI RX FIFO Not Empty */
#define ZYNQ_QSPI_IXR_ALL_MASK		(ZYNQ_QSPI_IXR_TXNFULL_MASK | \
					ZYNQ_QSPI_IXR_RXNEMTY_MASK)

/*
 * QSPI Enable Register bit Masks
 *
 * This register is used to enable or disable the QSPI controller
 */
#define ZYNQ_QSPI_ENABLE_ENABLE_MASK	0x00000001 /* QSPI Enable Bit Mask */

/*
 * QSPI Linear Configuration Register
 *
 * It is named Linear Configuration but it controls other modes when not in
 * linear mode also.
 */
#define ZYNQ_QSPI_LCFG_TWO_MEM_MASK	0x40000000 /* LQSPI Two memories Mask */
#define ZYNQ_QSPI_LCFG_SEP_BUS_MASK	0x20000000 /* LQSPI Separate bus Mask */
#define ZYNQ_QSPI_LCFG_U_PAGE_MASK	0x10000000 /* LQSPI Upper Page Mask */

#define ZYNQ_QSPI_LCFG_DUMMY_SHIFT	8

#define ZYNQ_QSPI_FAST_READ_QOUT_CODE	0x6B /* read instruction code */
#define ZYNQ_QSPI_FIFO_DEPTH		63 /* FIFO depth in words */
#define ZYNQ_QSPI_RX_THRESHOLD		32 /* Rx FIFO threshold level */
#define ZYNQ_QSPI_TX_THRESHOLD		1 /* Tx FIFO threshold level */

/*
 * The modebits configurable by the driver to make the SPI support different
 * data formats
 */
#define MODEBITS			(SPI_CPOL | SPI_CPHA)

/* Default number of chip selects */
#define ZYNQ_QSPI_DEFAULT_NUM_CS	1

/**
 * struct zynq_qspi - Defines qspi driver instance
 * @regs:		Virtual address of the QSPI controller registers
 * @refclk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @irq:		IRQ number
 * @txbuf:		Pointer to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @bytes_to_transfer:	Number of bytes left to transfer
 * @bytes_to_receive:	Number of bytes left to receive
 * @is_dual:		Flag to indicate whether dual flash memories are used
 * @is_instr:		Flag to indicate if transfer contains an instruction
 *			(Used in dual parallel configuration)
 */
struct zynq_qspi {
	void __iomem *regs;
	struct clk *refclk;
	struct clk *pclk;
	int irq;
	const void *txbuf;
	void *rxbuf;
	int bytes_to_transfer;
	int bytes_to_receive;
	u32 is_dual;
	u8 is_instr;
};

/*
 * Inline functions for the QSPI controller read/write
 */
static inline u32 zynq_qspi_read(struct zynq_qspi *xqspi, u32 offset)
{
	return readl_relaxed(xqspi->regs + offset);
}

static inline void zynq_qspi_write(struct zynq_qspi *xqspi, u32 offset,
				   u32 val)
{
	writel_relaxed(val, xqspi->regs + offset);
}

/**
 * zynq_qspi_init_hw - Initialize the hardware
 * @xqspi:	Pointer to the zynq_qspi structure
 *
 * The default settings of the QSPI controller's configurable parameters on
 * reset are
 *	- Master mode
 *	- Baud rate divisor is set to 2
 *	- Tx thresold set to 1l Rx threshold set to 32
 *	- Flash memory interface mode enabled
 *	- Size of the word to be transferred as 8 bit
 * This function performs the following actions
 *	- Disable and clear all the interrupts
 *	- Enable manual slave select
 *	- Enable manual start
 *	- Deselect all the chip select lines
 *	- Set the size of the word to be transferred as 32 bit
 *	- Set the little endian mode of TX FIFO and
 *	- Enable the QSPI controller
 */
static void zynq_qspi_init_hw(struct zynq_qspi *xqspi)
{
	u32 config_reg;

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET, 0);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_IDIS_OFFSET, 0x7F);

	/* Disable linear mode as the boot loader may have used it */
	zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET, 0);

	/* Clear the RX FIFO */
	while (zynq_qspi_read(xqspi, ZYNQ_QSPI_STATUS_OFFSET) &
			      ZYNQ_QSPI_IXR_RXNEMTY_MASK)
		zynq_qspi_read(xqspi, ZYNQ_QSPI_RXD_OFFSET);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_STATUS_OFFSET, 0x7F);
	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);
	config_reg &= ~(ZYNQ_QSPI_CONFIG_MSTREN_MASK |
			ZYNQ_QSPI_CONFIG_CPOL_MASK |
			ZYNQ_QSPI_CONFIG_CPHA_MASK |
			ZYNQ_QSPI_CONFIG_BDRATE_MASK |
			ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
			ZYNQ_QSPI_CONFIG_MANSRTEN_MASK |
			ZYNQ_QSPI_CONFIG_MANSRT_MASK);
	config_reg |= (ZYNQ_QSPI_CONFIG_MSTREN_MASK |
		       ZYNQ_QSPI_CONFIG_SSFORCE_MASK |
		       ZYNQ_QSPI_CONFIG_FWIDTH_MASK |
		       ZYNQ_QSPI_CONFIG_IFMODE_MASK);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_RX_THRESH_OFFSET,
			ZYNQ_QSPI_RX_THRESHOLD);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_TX_THRESH_OFFSET,
			ZYNQ_QSPI_TX_THRESHOLD);

	if (xqspi->is_dual)
		/* Enable two memories on separate buses */
		zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET,
				(ZYNQ_QSPI_LCFG_TWO_MEM_MASK |
				ZYNQ_QSPI_LCFG_SEP_BUS_MASK |
				(1 << ZYNQ_QSPI_LCFG_DUMMY_SHIFT) |
				ZYNQ_QSPI_FAST_READ_QOUT_CODE));
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
	/* Enable two memories on shared bus */
	zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET,
			(ZYNQ_QSPI_LCFG_TWO_MEM_MASK |
			(1 << ZYNQ_QSPI_LCFG_DUMMY_SHIFT) |
			ZYNQ_QSPI_FAST_READ_QOUT_CODE));
#endif
	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET,
			ZYNQ_QSPI_ENABLE_ENABLE_MASK);
}

/**
 * zynq_qspi_read_rx_fifo - Read 1..4 bytes from RxFIFO to RX buffer
 * @xqspi:	Pointer to the zynq_qspi structure
 * @size:	Number of bytes to be read (1..4)
 *
 * Note: In case of dual parallel connection, even number of bytes are read
 * when odd bytes are requested to avoid transfer of a nibble to each flash.
 * The receive buffer though, is populated with the number of bytes requested.
 */
static void zynq_qspi_read_rx_fifo(struct zynq_qspi *xqspi, unsigned int size)
{
	unsigned int xsize;
	u32 data;

	data = zynq_qspi_read(xqspi, ZYNQ_QSPI_RXD_OFFSET);

	if (xqspi->rxbuf) {
		xsize = size;
		if (xqspi->is_dual && !xqspi->is_instr && (size % 2))
			xsize++;
		memcpy(xqspi->rxbuf, ((u8 *)&data) + 4 - xsize, size);
		xqspi->rxbuf += size;
	}

	xqspi->bytes_to_receive -= size;
	if (xqspi->bytes_to_receive < 0)
		xqspi->bytes_to_receive = 0;
}

/**
 * zynq_qspi_write_tx_fifo - Write 1..4 bytes from TX buffer to TxFIFO
 * @xqspi:	Pointer to the zynq_qspi structure
 * @size:	Number of bytes to be written (1..4)
 *
 * In dual parallel configuration, when read/write data operations
 * are performed, odd data bytes have to be converted to even to
 * avoid a nibble (of data when programming / dummy when reading)
 * going to individual flash devices, where a byte is expected.
 * This check is only for data and will not apply for commands.
 */
static void zynq_qspi_write_tx_fifo(struct zynq_qspi *xqspi, unsigned int size)
{
	static const unsigned int offset[4] = {
		ZYNQ_QSPI_TXD_00_01_OFFSET, ZYNQ_QSPI_TXD_00_10_OFFSET,
		ZYNQ_QSPI_TXD_00_11_OFFSET, ZYNQ_QSPI_TXD_00_00_OFFSET };
	unsigned int xsize;
	u32 data;

	if (xqspi->txbuf) {
		data = 0xffffffff;
		memcpy(&data, xqspi->txbuf, size);
		xqspi->txbuf += size;
	} else {
		data = 0;
	}

	xqspi->bytes_to_transfer -= size;

	xsize = size;
	if (xqspi->is_dual && !xqspi->is_instr && (size % 2))
		xsize++;
	zynq_qspi_write(xqspi, offset[xsize - 1], data);
}

/**
 * zynq_prepare_transfer_hardware - Prepares hardware for transfer.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function enables SPI master controller.
 *
 * Return:	Always 0
 */
static int zynq_prepare_transfer_hardware(struct spi_master *master)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);

	clk_enable(xqspi->refclk);
	clk_enable(xqspi->pclk);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET,
			ZYNQ_QSPI_ENABLE_ENABLE_MASK);

	return 0;
}

/**
 * zynq_unprepare_transfer_hardware - Relaxes hardware after transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function disables the SPI master controller.
 *
 * Return:	Always 0
 */
static int zynq_unprepare_transfer_hardware(struct spi_master *master)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET, 0);
	clk_disable(xqspi->refclk);
	clk_disable(xqspi->pclk);

	return 0;
}

/**
 * zynq_qspi_chipselect - Select or deselect the chip select line
 * @qspi:	Pointer to the spi_device structure
 * @is_high:	Select(0) or deselect (1) the chip select line
 */
static void zynq_qspi_chipselect(struct spi_device *qspi, bool is_high)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg;
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
	u32 lqspi_cfg_reg;
#endif

	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);

	/* Select upper/lower page before asserting CS */
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
		lqspi_cfg_reg = zynq_qspi_read(xqspi,
					       ZYNQ_QSPI_LINEAR_CFG_OFFSET);
		if (qspi->master->flags & SPI_MASTER_U_PAGE)
			lqspi_cfg_reg |= ZYNQ_QSPI_LCFG_U_PAGE_MASK;
		else
			lqspi_cfg_reg &= ~ZYNQ_QSPI_LCFG_U_PAGE_MASK;
		zynq_qspi_write(xqspi, ZYNQ_QSPI_LINEAR_CFG_OFFSET,
				lqspi_cfg_reg);
#endif

	if (is_high) {
		/* Deselect the slave */
		config_reg |= ZYNQ_QSPI_CONFIG_SSCTRL_MASK;
	} else {
		/* Select the slave */
		config_reg &= ~ZYNQ_QSPI_CONFIG_SSCTRL_MASK;
		if (gpio_is_valid(qspi->cs_gpio)) {
			config_reg |= (((~(BIT(0))) <<
					ZYNQ_QSPI_SS_SHIFT) &
					ZYNQ_QSPI_CONFIG_SSCTRL_MASK);
		} else {
			config_reg |= (((~(BIT(qspi->chip_select))) <<
					ZYNQ_QSPI_SS_SHIFT) &
					ZYNQ_QSPI_CONFIG_SSCTRL_MASK);
		}
		xqspi->is_instr = 1;
	}

	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);
}

/**
 * zynq_qspi_setup_transfer - Configure QSPI controller for specified transfer
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
static int zynq_qspi_setup_transfer(struct spi_device *qspi,
				    struct spi_transfer *transfer)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(qspi->master);
	u32 config_reg, req_hz, baud_rate_val = 0;

	if (transfer)
		req_hz = transfer->speed_hz;
	else
		req_hz = qspi->max_speed_hz;

	/* Set the clock frequency */
	/* If req_hz == 0, default to lowest speed */
	while ((baud_rate_val < ZYNQ_QSPI_BAUD_DIV_MAX)  &&
	       (clk_get_rate(xqspi->refclk) / (2 << baud_rate_val)) > req_hz)
		baud_rate_val++;

	config_reg = zynq_qspi_read(xqspi, ZYNQ_QSPI_CONFIG_OFFSET);

	/* Set the QSPI clock phase and clock polarity */
	config_reg &= (~ZYNQ_QSPI_CONFIG_CPHA_MASK) &
		      (~ZYNQ_QSPI_CONFIG_CPOL_MASK);
	if (qspi->mode & SPI_CPHA)
		config_reg |= ZYNQ_QSPI_CONFIG_CPHA_MASK;
	if (qspi->mode & SPI_CPOL)
		config_reg |= ZYNQ_QSPI_CONFIG_CPOL_MASK;

	config_reg &= ~ZYNQ_QSPI_CONFIG_BDRATE_MASK;
	config_reg |= (baud_rate_val << ZYNQ_QSPI_BAUD_DIV_SHIFT);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_CONFIG_OFFSET, config_reg);

	return 0;
}

/**
 * zynq_qspi_setup - Configure the QSPI controller
 * @qspi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of QSPI controller for the next QSPI transfer, baud
 * rate and divisor value to setup the requested qspi clock.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_setup(struct spi_device *qspi)
{
	struct device *dev = &qspi->master->dev;
	int ret;

	if (gpio_is_valid(qspi->cs_gpio)) {
		ret = devm_gpio_request(dev, qspi->cs_gpio, dev_name(dev));
		if (ret) {
			dev_err(dev, "Invalid cs_gpio\n");
			return ret;
		}

		gpio_direction_output(qspi->cs_gpio,
				!(qspi->mode & SPI_CS_HIGH));
	}

	if (qspi->master->busy)
		return -EBUSY;

	return zynq_qspi_setup_transfer(qspi, NULL);
}

/**
 * zynq_qspi_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xqspi:	Pointer to the zynq_qspi structure
 * @txcount:	Maximum number of words to write
 * @txempty:	Indicates that TxFIFO is empty
 */
static void zynq_qspi_fill_tx_fifo(struct zynq_qspi *xqspi, int txcount,
				   bool txempty)
{
	int count, len, k;

	len = xqspi->bytes_to_transfer;
	if (len && len < 4) {
		/*
		 * We must empty the TxFIFO between accesses to TXD0,
		 * TXD1, TXD2, TXD3.
		 */
		if (txempty)
			zynq_qspi_write_tx_fifo(xqspi, len);
		return;
	}

	count = len / 4;
	if (count > txcount)
		count = txcount;

	if (xqspi->txbuf) {
		writesl(xqspi->regs + ZYNQ_QSPI_TXD_00_00_OFFSET,
			xqspi->txbuf, count);
		xqspi->txbuf += count * 4;
	} else {
		for (k = 0; k < count; k++)
			writel_relaxed(0, xqspi->regs +
					  ZYNQ_QSPI_TXD_00_00_OFFSET);
	}
	xqspi->bytes_to_transfer -= count * 4;
}

/**
 * zynq_qspi_drain_rx_fifo - Drains the RX FIFO by as many bytes as possible
 * @xqspi:	Pointer to the zynq_qspi structure
 * @rxcount:	Maximum number of words to read
 */
static void zynq_qspi_drain_rx_fifo(struct zynq_qspi *xqspi, int rxcount)
{
	int count, len, k;

	len = xqspi->bytes_to_receive - xqspi->bytes_to_transfer;
	count = len / 4;
	if (count > rxcount)
		count = rxcount;

	if (xqspi->rxbuf) {
		readsl(xqspi->regs + ZYNQ_QSPI_RXD_OFFSET,
		       xqspi->rxbuf, count);
		xqspi->rxbuf += count * 4;
	} else {
		for (k = 0; k < count; k++)
			readl_relaxed(xqspi->regs + ZYNQ_QSPI_RXD_OFFSET);
	}
	xqspi->bytes_to_receive -= count * 4;
	len -= count * 4;

	if (len && len < 4 && count < rxcount)
		zynq_qspi_read_rx_fifo(xqspi, len);
}

/**
 * zynq_qspi_irq - Interrupt service routine of the QSPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xqspi structure
 *
 * This function handles TX empty only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 *
 * Return:	IRQ_HANDLED when interrupt is handled; IRQ_NONE otherwise.
 */
static irqreturn_t zynq_qspi_irq(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);
	u32 intr_status;
	bool txempty;

	intr_status = zynq_qspi_read(xqspi, ZYNQ_QSPI_STATUS_OFFSET);
	zynq_qspi_write(xqspi, ZYNQ_QSPI_STATUS_OFFSET, intr_status);

	if ((intr_status & ZYNQ_QSPI_IXR_TXNFULL_MASK) ||
	    (intr_status & ZYNQ_QSPI_IXR_RXNEMTY_MASK)) {
		/*
		 * This bit is set when Tx FIFO has < THRESHOLD entries.
		 * We have the THRESHOLD value set to 1,
		 * so this bit indicates Tx FIFO is empty.
		 */
		txempty = !!(intr_status & ZYNQ_QSPI_IXR_TXNFULL_MASK);

		/* Read out the data from the RX FIFO */
		zynq_qspi_drain_rx_fifo(xqspi, ZYNQ_QSPI_RX_THRESHOLD);

		if (xqspi->bytes_to_transfer) {
			/* There is more data to send */
			zynq_qspi_fill_tx_fifo(xqspi, ZYNQ_QSPI_RX_THRESHOLD,
					       txempty);
		} else {
			/*
			 * If transfer and receive is completed then only send
			 * complete signal.
			 */
			if (!xqspi->bytes_to_receive) {
				zynq_qspi_write(xqspi,
						ZYNQ_QSPI_IDIS_OFFSET,
						ZYNQ_QSPI_IXR_ALL_MASK);
				spi_finalize_current_transfer(master);
				xqspi->is_instr = 0;
			}
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/**
 * zynq_qspi_start_transfer - Initiates the QSPI transfer
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
static int zynq_qspi_start_transfer(struct spi_master *master,
				    struct spi_device *qspi,
				    struct spi_transfer *transfer)
{
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);

	xqspi->txbuf = transfer->tx_buf;
	xqspi->rxbuf = transfer->rx_buf;
	xqspi->bytes_to_transfer = transfer->len;
	xqspi->bytes_to_receive = transfer->len;

	if (!transfer->stripe)
		xqspi->is_instr = true;
	else
		xqspi->is_instr = false;
	zynq_qspi_setup_transfer(qspi, transfer);

	zynq_qspi_fill_tx_fifo(xqspi, ZYNQ_QSPI_FIFO_DEPTH, true);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_IEN_OFFSET,
			ZYNQ_QSPI_IXR_ALL_MASK);

	return transfer->len;
}

/**
 * zynq_qspi_suspend - Suspend method for the QSPI driver
 * @_dev:	Address of the platform_device structure
 *
 * This function stops the QSPI driver queue and disables the QSPI controller
 *
 * Return:	Always 0
 */
static int __maybe_unused zynq_qspi_suspend(struct device *_dev)
{
	struct platform_device *pdev = container_of(_dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);

	spi_master_suspend(master);

	zynq_unprepare_transfer_hardware(master);

	return 0;
}

/**
 * zynq_qspi_resume - Resume method for the QSPI driver
 * @dev:	Address of the platform_device structure
 *
 * The function starts the QSPI driver queue and initializes the QSPI controller
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused zynq_qspi_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);
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

static SIMPLE_DEV_PM_OPS(zynq_qspi_dev_pm_ops, zynq_qspi_suspend,
			 zynq_qspi_resume);

/**
 * zynq_qspi_probe - Probe method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_master *master;
	struct zynq_qspi *xqspi;
	struct resource *res;
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

	if (of_property_read_u32(pdev->dev.of_node, "is-dual",
				 &xqspi->is_dual)) {
		dev_warn(&pdev->dev, "couldn't determine configuration info");
		dev_warn(&pdev->dev, "about dual memories. defaulting to single memory\n");
	}

	xqspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xqspi->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		ret = PTR_ERR(xqspi->pclk);
		goto remove_master;
	}

	xqspi->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xqspi->refclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xqspi->refclk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto remove_master;
	}

	ret = clk_prepare_enable(xqspi->refclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_pclk;
	}

	/* QSPI controller initializations */
	zynq_qspi_init_hw(xqspi);

	xqspi->irq = platform_get_irq(pdev, 0);
	if (xqspi->irq <= 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "irq resource not found\n");
		goto remove_master;
	}
	ret = devm_request_irq(&pdev->dev, xqspi->irq, zynq_qspi_irq,
			       0, pdev->name, master);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto remove_master;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "num-cs",
				   &num_cs);
	if (ret < 0)
		master->num_chipselect = ZYNQ_QSPI_DEFAULT_NUM_CS;
	else
		master->num_chipselect = num_cs;

	master->setup = zynq_qspi_setup;
	master->set_cs = zynq_qspi_chipselect;
	master->transfer_one = zynq_qspi_start_transfer;
	master->prepare_transfer_hardware = zynq_prepare_transfer_hardware;
	master->unprepare_transfer_hardware = zynq_unprepare_transfer_hardware;
	master->flags = SPI_MASTER_QUAD_MODE | SPI_MASTER_GPIO_SS;

	master->max_speed_hz = clk_get_rate(xqspi->refclk) / 2;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD |
			    SPI_TX_DUAL | SPI_TX_QUAD;

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto clk_dis_all;
	}

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
 * zynq_qspi_remove - Remove method for the QSPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on failure
 */
static int zynq_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_qspi *xqspi = spi_master_get_devdata(master);

	zynq_qspi_write(xqspi, ZYNQ_QSPI_ENABLE_OFFSET, 0);

	clk_disable_unprepare(xqspi->refclk);
	clk_disable_unprepare(xqspi->pclk);

	spi_unregister_master(master);

	return 0;
}

static const struct of_device_id zynq_qspi_of_match[] = {
	{ .compatible = "xlnx,zynq-qspi-1.0", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, zynq_qspi_of_match);

/*
 * zynq_qspi_driver - This structure defines the QSPI platform driver
 */
static struct platform_driver zynq_qspi_driver = {
	.probe = zynq_qspi_probe,
	.remove = zynq_qspi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = zynq_qspi_of_match,
		.pm = &zynq_qspi_dev_pm_ops,
	},
};

module_platform_driver(zynq_qspi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq QSPI driver");
MODULE_LICENSE("GPL");
