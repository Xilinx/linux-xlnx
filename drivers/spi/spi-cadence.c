/*
 * Cadence SPI controller driver (master mode only)
 *
 * Copyright (C) 2008 - 2014 Xilinx, Inc.
 *
 * based on Blackfin On-Chip SPI Driver (spi_bfin5xx.c)
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

/* Name of this driver */
#define CDNS_SPI_NAME		"cdns-spi"

/* Register offset definitions */
#define CDNS_SPI_CR_OFFSET	0x00 /* Configuration  Register, RW */
#define CDNS_SPI_ISR_OFFSET	0x04 /* Interrupt Status Register, RO */
#define CDNS_SPI_IER_OFFSET	0x08 /* Interrupt Enable Register, WO */
#define CDNS_SPI_IDR_OFFSET	0x0c /* Interrupt Disable Register, WO */
#define CDNS_SPI_IMR_OFFSET	0x10 /* Interrupt Enabled Mask Register, RO */
#define CDNS_SPI_ER_OFFSET	0x14 /* Enable/Disable Register, RW */
#define CDNS_SPI_DR_OFFSET	0x18 /* Delay Register, RW */
#define CDNS_SPI_TXD_OFFSET	0x1C /* Data Transmit Register, WO */
#define CDNS_SPI_RXD_OFFSET	0x20 /* Data Receive Register, RO */
#define CDNS_SPI_SICR_OFFSET	0x24 /* Slave Idle Count Register, RW */
#define CDNS_SPI_THLD_OFFSET	0x28 /* Transmit FIFO Watermark Register,RW */

/*
 * SPI Configuration Register bit Masks
 *
 * This register contains various control bits that affect the operation
 * of the SPI controller
 */
#define CDNS_SPI_CR_MANSTRT_MASK	0x00010000 /* Manual TX Start */
#define CDNS_SPI_CR_CPHA_MASK		0x00000004 /* Clock Phase Control */
#define CDNS_SPI_CR_CPOL_MASK		0x00000002 /* Clock Polarity Control */
#define CDNS_SPI_CR_SSCTRL_MASK		0x00003C00 /* Slave Select Mask */
#define CDNS_SPI_CR_BAUD_DIV_MASK	0x00000038 /* Baud Rate Divisor Mask */
#define CDNS_SPI_CR_MSTREN_MASK		0x00000001 /* Master Enable Mask */
#define CDNS_SPI_CR_MANSTRTEN_MASK	0x00008000 /* Manual TX Enable Mask */
#define CDNS_SPI_CR_SSFORCE_MASK	0x00004000 /* Manual SS Enable Mask */
#define CDNS_SPI_CR_BAUD_DIV_4_MASK	0x00000008 /* Default Baud Div Mask */
#define CDNS_SPI_CR_DEFAULT_MASK	(CDNS_SPI_CR_MSTREN_MASK | \
					CDNS_SPI_CR_SSCTRL_MASK | \
					CDNS_SPI_CR_SSFORCE_MASK | \
					CDNS_SPI_CR_BAUD_DIV_4_MASK)

/*
 * SPI Configuration Register - Baud rate and slave select
 *
 * These are the values used in the calculation of baud rate divisor and
 * setting the slave select.
 */

#define CDNS_SPI_BAUD_DIV_MAX		7 /* Baud rate divisor maximum */
#define CDNS_SPI_BAUD_DIV_MIN		1 /* Baud rate divisor minimum */
#define CDNS_SPI_BAUD_DIV_SHIFT		3 /* Baud rate divisor shift in CR */
#define CDNS_SPI_SS_SHIFT		10 /* Slave Select field shift in CR */
#define CDNS_SPI_SS0			0x1 /* Slave Select zero */

/*
 * SPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define CDNS_SPI_IXR_TXOW_MASK	0x00000004 /* SPI TX FIFO Overwater */
#define CDNS_SPI_IXR_MODF_MASK	0x00000002 /* SPI Mode Fault */
#define CDNS_SPI_IXR_RXNEMTY_MASK 0x00000010 /* SPI RX FIFO Not Empty */
#define CDNS_SPI_IXR_DEFAULT_MASK	(CDNS_SPI_IXR_TXOW_MASK | \
					CDNS_SPI_IXR_MODF_MASK)
#define CDNS_SPI_IXR_TXFULL_MASK	0x00000008 /* SPI TX Full */
#define CDNS_SPI_IXR_ALL_MASK	0x0000007F /* SPI all interrupts */

/*
 * SPI Enable Register bit Masks
 *
 * This register is used to enable or disable the SPI controller
 */
#define CDNS_SPI_ER_ENABLE_MASK	0x00000001 /* SPI Enable Bit Mask */
#define CDNS_SPI_ER_DISABLE_MASK	0x0 /* SPI Disable Bit Mask */

/* SPI timeout value */
#define CDNS_SPI_TIMEOUT	(5 * HZ)

/* SPI FIFO depth in bytes */
#define CDNS_SPI_FIFO_DEPTH	128

/* Macros for the SPI controller read/write */
#define cdns_spi_read(addr)	readl_relaxed(addr)
#define cdns_spi_write(addr, val)	writel_relaxed((val), (addr))

/* Driver state - suspend/ready */
enum driver_state_val {
	CDNS_SPI_DRIVER_STATE_READY = 0,
	CDNS_SPI_DRIVER_STATE_SUSPEND
};

/**
 * struct cdns_spi - This definition defines spi driver instance
 * @regs:		Virtual address of the SPI controller registers
 * @ref_clk:		Pointer to the peripheral clock
 * @pclk:		Pointer to the APB clock
 * @speed_hz:		Current SPI bus clock speed in Hz
 * @txbuf:		Pointer	to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @remaining_bytes:	Number of bytes left to transfer
 * @requested_bytes:	Number of bytes requested
 * @dev_busy:		Device busy flag
 * @done:		Transfer complete status
 * @driver_state:	Describes driver state - ready/suspended
 */
struct cdns_spi {
	void __iomem *regs;
	struct clk *ref_clk;
	struct clk *pclk;
	u32 speed_hz;
	const u8 *txbuf;
	u8 *rxbuf;
	int remaining_bytes;
	int requested_bytes;
	u8 dev_busy;
	struct completion done;
	enum driver_state_val driver_state;
};

/**
 * cdns_spi_init_hw - Initialize the hardware and configure the SPI controller
 * @regs_base:		Base address of SPI controller
 *
 * On reset the SPI controller is configured to be in master mode, baud rate
 * divisor is set to 4, threshold value for TX FIFO not full interrupt is set
 * to 1 and size of the word to be transferred as 8 bit.
 * This function initializes the SPI controller to disable and clear all the
 * interrupts, enable manual slave select and manual start, deselect all the
 * chip select lines, and enable the SPI controller.
 */
static void cdns_spi_init_hw(void __iomem *regs_base)
{
	cdns_spi_write(regs_base + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);
	cdns_spi_write(regs_base + CDNS_SPI_IDR_OFFSET, CDNS_SPI_IXR_ALL_MASK);

	/* Clear the RX FIFO */
	while (cdns_spi_read(regs_base + CDNS_SPI_ISR_OFFSET) &
	       CDNS_SPI_IXR_RXNEMTY_MASK)
		cdns_spi_read(regs_base + CDNS_SPI_RXD_OFFSET);

	cdns_spi_write(regs_base + CDNS_SPI_ISR_OFFSET, CDNS_SPI_IXR_ALL_MASK);
	cdns_spi_write(regs_base + CDNS_SPI_CR_OFFSET,
		       CDNS_SPI_CR_DEFAULT_MASK);
	cdns_spi_write(regs_base + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_ENABLE_MASK);
}

/**
 * cdns_spi_chipselect - Select or deselect the chip select line
 * @spi:	Pointer to the spi_device structure
 * @is_on:	Select(1) or deselect (0) the chip select line
 */
static void cdns_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct cdns_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ctrl_reg;

	ctrl_reg = cdns_spi_read(xspi->regs + CDNS_SPI_CR_OFFSET);

	if (is_on) {
		/* Select the slave */
		ctrl_reg &= ~CDNS_SPI_CR_SSCTRL_MASK;
		ctrl_reg |= ((~(CDNS_SPI_SS0 << spi->chip_select)) <<
			     CDNS_SPI_SS_SHIFT) & CDNS_SPI_CR_SSCTRL_MASK;
	} else {
		/* Deselect the slave */
		ctrl_reg |= CDNS_SPI_CR_SSCTRL_MASK;
	}

	cdns_spi_write(xspi->regs + CDNS_SPI_CR_OFFSET, ctrl_reg);
}

/**
 * cdns_spi_config_clock - Sets clock polarity, phase and frequency
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the requested clock polarity, phase and frequency.
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the SPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static void cdns_spi_config_clock(struct spi_device *spi,
		struct spi_transfer *transfer)
{
	struct cdns_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ctrl_reg, req_hz, baud_rate_val;
	unsigned long frequency;

	if (transfer && transfer->speed_hz)
		req_hz = transfer->speed_hz;
	else
		req_hz = spi->max_speed_hz;

	frequency = clk_get_rate(xspi->ref_clk);

	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);
	ctrl_reg = cdns_spi_read(xspi->regs + CDNS_SPI_CR_OFFSET);

	/* Set the SPI clock phase and clock polarity */
	ctrl_reg &= ~(CDNS_SPI_CR_CPHA_MASK | CDNS_SPI_CR_CPOL_MASK);
	if (spi->mode & SPI_CPHA)
		ctrl_reg |= CDNS_SPI_CR_CPHA_MASK;
	if (spi->mode & SPI_CPOL)
		ctrl_reg |= CDNS_SPI_CR_CPOL_MASK;

	/* Set the clock frequency */
	if (xspi->speed_hz != req_hz) {
		/* first valid value is 1 */
		baud_rate_val = CDNS_SPI_BAUD_DIV_MIN;
		while ((baud_rate_val < CDNS_SPI_BAUD_DIV_MAX) &&
		       (frequency / (2 << baud_rate_val)) > req_hz)
			baud_rate_val++;

		ctrl_reg &= ~CDNS_SPI_CR_BAUD_DIV_MASK;
		ctrl_reg |= baud_rate_val << CDNS_SPI_BAUD_DIV_SHIFT;

		xspi->speed_hz = frequency / (2 << baud_rate_val);
	}

	cdns_spi_write(xspi->regs + CDNS_SPI_CR_OFFSET, ctrl_reg);
	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_ENABLE_MASK);
}

/**
 * cdns_spi_setup_transfer - Configure SPI controller for specified transfer
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer setup parameters
 *
 * Sets the operational mode of SPI controller for the next SPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	0 on success and error value on error
 */
static int cdns_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *transfer)
{
	struct cdns_spi *xspi = spi_master_get_devdata(spi->master);
	u8 bits_per_word;

	bits_per_word = transfer ?
			transfer->bits_per_word : spi->bits_per_word;

	if (bits_per_word != 8) {
		dev_err(&spi->dev, "%s, unsupported bits per word %x\n",
			__func__, spi->bits_per_word);
		return -EINVAL;
	}

	cdns_spi_config_clock(spi, transfer);

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w, %u clock speed\n",
		__func__, spi->mode, spi->bits_per_word,
		xspi->speed_hz);

	return 0;
}

/**
 * cdns_spi_setup - Configure the SPI controller
 * @spi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of SPI controller for the next SPI transfer, sets
 * the baud rate and divisor value to setup the requested spi clock.
 *
 * Return:	0 on success and error value on error
 */
static int cdns_spi_setup(struct spi_device *spi)
{
	if (!spi->max_speed_hz)
		return -EINVAL;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	return cdns_spi_setup_transfer(spi, NULL);
}

/**
 * cdns_spi_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xspi:	Pointer to the cdns_spi structure
 */
static void cdns_spi_fill_tx_fifo(struct cdns_spi *xspi)
{
	unsigned long trans_cnt = 0;

	while ((trans_cnt < CDNS_SPI_FIFO_DEPTH) &&
	       (xspi->remaining_bytes > 0)) {
		if (xspi->txbuf)
			cdns_spi_write(xspi->regs + CDNS_SPI_TXD_OFFSET,
				       *xspi->txbuf++);
		else
			cdns_spi_write(xspi->regs + CDNS_SPI_TXD_OFFSET, 0);

		xspi->remaining_bytes--;
		trans_cnt++;
	}
}

/**
 * cdns_spi_irq - Interrupt service routine of the SPI controller
 * @irq:	IRQ number
 * @dev_id:	Pointer to the xspi structure
 *
 * This function handles TX empty and Mode Fault interrupts only.
 * On TX empty interrupt this function reads the received data from RX FIFO and
 * fills the TX FIFO if there is any data remaining to be transferred.
 * On Mode Fault interrupt this function indicates that transfer is completed,
 * the SPI subsystem will identify the error as the remaining bytes to be
 * transferred is non-zero.
 *
 * Return:	IRQ_HANDLED always
 */
static irqreturn_t cdns_spi_irq(int irq, void *dev_id)
{
	struct cdns_spi *xspi = dev_id;
	u32 intr_status;

	intr_status = cdns_spi_read(xspi->regs + CDNS_SPI_ISR_OFFSET);
	cdns_spi_write(xspi->regs + CDNS_SPI_ISR_OFFSET, intr_status);

	if (intr_status & CDNS_SPI_IXR_MODF_MASK) {
		/* Indicate that transfer is completed, the SPI subsystem will
		 * identify the error as the remaining bytes to be
		 * transferred is non-zero
		 */
		cdns_spi_write(xspi->regs + CDNS_SPI_IDR_OFFSET,
			       CDNS_SPI_IXR_DEFAULT_MASK);
		complete(&xspi->done);
	} else if (intr_status & CDNS_SPI_IXR_TXOW_MASK) {
		unsigned long trans_cnt;

		trans_cnt = xspi->requested_bytes - xspi->remaining_bytes;

		/* Read out the data from the RX FIFO */
		while (trans_cnt) {
			u8 data;

			data = cdns_spi_read(xspi->regs + CDNS_SPI_RXD_OFFSET);
			if (xspi->rxbuf)
				*xspi->rxbuf++ = data;

			xspi->requested_bytes--;
			trans_cnt--;
		}

		if (xspi->remaining_bytes) {
			/* There is more data to send */
			cdns_spi_fill_tx_fifo(xspi);
		} else {
			/* Transfer is completed */
			cdns_spi_write(xspi->regs + CDNS_SPI_IDR_OFFSET,
				       CDNS_SPI_IXR_DEFAULT_MASK);
			complete(&xspi->done);
		}
	}

	return IRQ_HANDLED;
}

/**
 * cdns_spi_reset_controller - Resets SPI controller
 * @spi:	Pointer to the spi_device structure
 *
 * This function disables the interrupts, de-asserts the chip select and
 * disables the SPI controller.
 */
static void cdns_spi_reset_controller(struct spi_device *spi)
{
	struct cdns_spi *xspi = spi_master_get_devdata(spi->master);

	cdns_spi_write(xspi->regs + CDNS_SPI_IDR_OFFSET,
		       CDNS_SPI_IXR_DEFAULT_MASK);
	cdns_spi_chipselect(spi, 0);
	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);
}

/**
 * cdns_spi_start_transfer - Initiates the SPI transfer
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides
 *		information about next transfer parameters
 *
 * This function fills the TX FIFO, starts the SPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int cdns_spi_start_transfer(struct spi_device *spi,
			struct spi_transfer *transfer)
{
	struct cdns_spi *xspi = spi_master_get_devdata(spi->master);
	int ret;

	xspi->txbuf = transfer->tx_buf;
	xspi->rxbuf = transfer->rx_buf;
	xspi->remaining_bytes = transfer->len;
	xspi->requested_bytes = transfer->len;
	reinit_completion(&xspi->done);

	cdns_spi_fill_tx_fifo(xspi);

	cdns_spi_write(xspi->regs + CDNS_SPI_IER_OFFSET,
		       CDNS_SPI_IXR_DEFAULT_MASK);

	ret = wait_for_completion_interruptible_timeout(&xspi->done,
							CDNS_SPI_TIMEOUT);
	if (ret < 1) {
		cdns_spi_reset_controller(spi);
		if (!ret)
			return -ETIMEDOUT;

		return ret;
	}

	return transfer->len - xspi->remaining_bytes;
}

/**
 * cdns_prepare_transfer_hardware - Prepares hardware for transfer.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function enables SPI master controller.
 *
 * Return:	0 on success and error value on error
 */
static int cdns_prepare_transfer_hardware(struct spi_master *master)
{
	struct cdns_spi *xspi = spi_master_get_devdata(master);

	if (xspi->driver_state != CDNS_SPI_DRIVER_STATE_READY)
		return -EINVAL;

	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_ENABLE_MASK);

	return 0;
}

/**
 * cdns_transfer_one_message - Sets up and transfer a message.
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 * @msg:	Pointer to the spi_message which contains the
 *		data to be transferred.
 *
 * This function calls the necessary functions to setup operational mode,
 * clock, control chip select and completes the transfer.
 *
 * Return:	0 on success and error value on error.
 */
static int cdns_transfer_one_message(struct spi_master *master,
				     struct spi_message *msg)
{
	struct spi_device *spi;
	unsigned cs_change = 1;
	int status = 0, length = 0;
	struct spi_transfer *transfer;

	spi = msg->spi;

	list_for_each_entry(transfer, &msg->transfers, transfer_list) {
		if ((transfer->bits_per_word || transfer->speed_hz) &&
		    cs_change) {
			status = cdns_spi_setup_transfer(spi, transfer);
			if (status < 0)
				break;
		}

		if (cs_change)
			cdns_spi_chipselect(spi, 1);

		cs_change = transfer->cs_change;

		if (!transfer->tx_buf && !transfer->rx_buf &&
			transfer->len) {
			status = -EINVAL;
			break;
		}

		if (transfer->len)
			length = cdns_spi_start_transfer(spi, transfer);

		if (length != transfer->len) {
			if (length > 0)
				status = -EMSGSIZE;
			else
				status = length;
			break;
		}
		msg->actual_length += length;
		status = 0;

		if (transfer->delay_usecs)
			udelay(transfer->delay_usecs);

		if (!cs_change)
			continue;

		if (transfer->transfer_list.next == &msg->transfers)
			break;

		cdns_spi_chipselect(spi, 0);
	}

	if (status || !cs_change)
		cdns_spi_chipselect(spi, 0);

	msg->status = status;
	spi_finalize_current_message(master);

	return status;
}

/**
 * cdns_unprepare_transfer_hardware - Relaxes hardware after transfer
 * @master:	Pointer to the spi_master structure which provides
 *		information about the controller.
 *
 * This function disables the SPI master controller.
 *
 * Return:	0 always
 */
static int cdns_unprepare_transfer_hardware(struct spi_master *master)
{
	struct cdns_spi *xspi = spi_master_get_devdata(master);

	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);

	return 0;
}

/**
 * cdns_spi_probe - Probe method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on error
 */
static int cdns_spi_probe(struct platform_device *pdev)
{
	int ret = 0, irq;
	u32 num_cs;
	struct spi_master *master;
	struct cdns_spi *xspi;
	struct resource *res;

	master = spi_alloc_master(&pdev->dev, sizeof(*xspi));
	if (master == NULL)
		return -ENOMEM;

	xspi = spi_master_get_devdata(master);
	master->dev.of_node = pdev->dev.of_node;
	platform_set_drvdata(pdev, master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xspi->regs)) {
		ret = PTR_ERR(xspi->regs);
		goto remove_master;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "irq number is negative\n");
		goto remove_master;
	}

	ret = devm_request_irq(&pdev->dev, irq, cdns_spi_irq,
			       0, pdev->name, xspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto remove_master;
	}

	xspi->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(xspi->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		ret = PTR_ERR(xspi->pclk);
		goto remove_master;
	}

	xspi->ref_clk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xspi->ref_clk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xspi->ref_clk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xspi->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APB clock.\n");
		goto remove_master;
	}

	ret = clk_prepare_enable(xspi->ref_clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_apb;
	}

	/* SPI controller initializations */
	cdns_spi_init_hw(xspi->regs);

	init_completion(&xspi->done);

	ret = of_property_read_u32(pdev->dev.of_node, "num-chip-select",
				   &num_cs);
	master->num_chipselect = num_cs;
	if (ret < 0) {
		dev_err(&pdev->dev, "couldn't determine num-chip-select\n");
		goto clk_dis_all;
	}
	master->setup = cdns_spi_setup;
	master->prepare_transfer_hardware = cdns_prepare_transfer_hardware;
	master->transfer_one_message = cdns_transfer_one_message;
	master->unprepare_transfer_hardware = cdns_unprepare_transfer_hardware;
	master->mode_bits = SPI_CPOL | SPI_CPHA;

	/* Set to default valid value */
	xspi->speed_hz = clk_get_rate(xspi->ref_clk) / 4;

	xspi->driver_state = CDNS_SPI_DRIVER_STATE_READY;

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto clk_dis_all;
	}

	dev_info(&pdev->dev, "at 0x%08X mapped to 0x%08X, irq=%d\n",
		 res->start, (u32 __force)xspi->regs, irq);

	return ret;

clk_dis_all:
	clk_disable_unprepare(xspi->ref_clk);
clk_dis_apb:
	clk_disable_unprepare(xspi->pclk);
remove_master:
	spi_master_put(master);
	return ret;
}

/**
 * cdns_spi_remove - Remove method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on error
 */
static int cdns_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct cdns_spi *xspi = spi_master_get_devdata(master);

	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);

	clk_disable_unprepare(xspi->ref_clk);
	clk_disable_unprepare(xspi->pclk);

	spi_unregister_master(master);
	spi_master_put(master);

	dev_dbg(&pdev->dev, "remove succeeded\n");
	return 0;
}

/**
 * cdns_spi_suspend - Suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function disables the SPI controller and
 * changes the driver state to "suspend"
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused cdns_spi_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct cdns_spi *xspi = spi_master_get_devdata(master);
	u32 ctrl_reg;

	cdns_spi_write(xspi->regs + CDNS_SPI_IDR_OFFSET,
		       CDNS_SPI_IXR_DEFAULT_MASK);
	complete(&xspi->done);

	ctrl_reg = cdns_spi_read(xspi->regs + CDNS_SPI_CR_OFFSET);
	ctrl_reg |= CDNS_SPI_CR_SSCTRL_MASK;
	cdns_spi_write(xspi->regs + CDNS_SPI_CR_OFFSET, ctrl_reg);

	cdns_spi_write(xspi->regs + CDNS_SPI_ER_OFFSET,
		       CDNS_SPI_ER_DISABLE_MASK);

	xspi->driver_state = CDNS_SPI_DRIVER_STATE_SUSPEND;

	clk_disable(xspi->ref_clk);
	clk_disable(xspi->pclk);

	dev_dbg(&pdev->dev, "suspend succeeded\n");
	return 0;
}

/**
 * cdns_spi_resume - Resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function changes the driver state to "ready"
 *
 * Return:	0 on success and error value on error
 */
static int __maybe_unused cdns_spi_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct cdns_spi *xspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = clk_enable(xspi->pclk);
	if (ret) {
		dev_err(dev, "Cannot enable APB clock.\n");
		return ret;
	}

	ret = clk_enable(xspi->ref_clk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xspi->pclk);
		return ret;
	}

	xspi->driver_state = CDNS_SPI_DRIVER_STATE_READY;

	dev_dbg(&pdev->dev, "resume succeeded\n");
	return 0;
}

static SIMPLE_DEV_PM_OPS(cdns_spi_dev_pm_ops, cdns_spi_suspend,
			 cdns_spi_resume);

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:" CDNS_SPI_NAME);

static struct of_device_id cdns_spi_of_match[] = {
	{ .compatible = "cdns,spi-r1p6" },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, cdns_spi_of_match);

/* cdns_spi_driver - This structure defines the SPI subsystem platform driver */
static struct platform_driver cdns_spi_driver = {
	.probe	= cdns_spi_probe,
	.remove	= cdns_spi_remove,
	.driver = {
		.name = CDNS_SPI_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cdns_spi_of_match,
		.pm = &cdns_spi_dev_pm_ops,
	},
};

module_platform_driver(cdns_spi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Cadence SPI driver");
MODULE_LICENSE("GPL");
