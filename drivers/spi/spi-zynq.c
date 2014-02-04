/*
 * Xilinx Zynq SPI controller driver (master mode only)
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
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/*
 * Name of this driver
 */
#define ZYNQ_SPI_NAME		"zynq-spi"

/*
 * Register offset definitions
 */
#define ZYNQ_SPI_CR_OFFSET	0x00 /* Configuration  Register, RW */
#define ZYNQ_SPI_ISR_OFFSET	0x04 /* Interrupt Status Register, RO */
#define ZYNQ_SPI_IER_OFFSET	0x08 /* Interrupt Enable Register, WO */
#define ZYNQ_SPI_IDR_OFFSET	0x0c /* Interrupt Disable Register, WO */
#define ZYNQ_SPI_IMR_OFFSET	0x10 /* Interrupt Enabled Mask Register, RO */
#define ZYNQ_SPI_ER_OFFSET	0x14 /* Enable/Disable Register, RW */
#define ZYNQ_SPI_DR_OFFSET	0x18 /* Delay Register, RW */
#define ZYNQ_SPI_TXD_OFFSET	0x1C /* Data Transmit Register, WO */
#define ZYNQ_SPI_RXD_OFFSET	0x20 /* Data Receive Register, RO */
#define ZYNQ_SPI_SICR_OFFSET	0x24 /* Slave Idle Count Register, RW */
#define ZYNQ_SPI_THLD_OFFSET	0x28 /* Transmit FIFO Watermark Register,RW */

/*
 * SPI Configuration Register bit Masks
 *
 * This register contains various control bits that affect the operation
 * of the SPI controller
 */
#define ZYNQ_SPI_CR_MANSTRT_MASK	0x00010000 /* Manual TX Start */
#define ZYNQ_SPI_CR_CPHA_MASK	0x00000004 /* Clock Phase Control */
#define ZYNQ_SPI_CR_CPOL_MASK	0x00000002 /* Clock Polarity Control */
#define ZYNQ_SPI_CR_SSCTRL_MASK	0x00003C00 /* Slave Select Mask */

/*
 * SPI Interrupt Registers bit Masks
 *
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define ZYNQ_SPI_IXR_TXOW_MASK	0x00000004 /* SPI TX FIFO Overwater */
#define ZYNQ_SPI_IXR_MODF_MASK	0x00000002 /* SPI Mode Fault */
#define ZYNQ_SPI_IXR_RXNEMTY_MASK 0x00000010 /* SPI RX FIFO Not Empty */
#define ZYNQ_SPI_IXR_ALL_MASK	(ZYNQ_SPI_IXR_TXOW_MASK | \
				 ZYNQ_SPI_IXR_MODF_MASK)

/*
 * SPI Enable Register bit Masks
 *
 * This register is used to enable or disable the SPI controller
 */
#define ZYNQ_SPI_ER_ENABLE_MASK	0x00000001 /* SPI Enable Bit Mask */

/*
 * Definitions for the status of queue
 */
#define ZYNQ_SPI_QUEUE_STOPPED	0
#define ZYNQ_SPI_QUEUE_RUNNING	1

/*
 * Macros for the SPI controller read/write
 */
#define zynq_spi_read(addr)	__raw_readl(addr)
#define zynq_spi_write(addr, val)	__raw_writel((val), (addr))

/**
 * struct zynq_spi - This definition defines spi driver instance
 * @workqueue:		Queue of all the transfers
 * @work:		Information about current transfer
 * @queue:		Head of the queue
 * @queue_state:	Queue status
 * @regs:		Virtual address of the SPI controller registers
 * @devclk:		Pointer to the peripheral clock
 * @aperclk:		Pointer to the APER clock
 * @clk_rate_change_nb:	Notifier block for clock frequency change callback
 * @irq:		IRQ number
 * @speed_hz:		Current SPI bus clock speed in Hz
 * @trans_queue_lock:	Lock used for accessing transfer queue
 * @ctrl_reg_lock:	Lock used for accessing configuration register
 * @txbuf:		Pointer	to the TX buffer
 * @rxbuf:		Pointer to the RX buffer
 * @remaining_bytes:	Number of bytes left to transfer
 * @dev_busy:		Device busy flag
 * @done:		Transfer complete status
 */
struct zynq_spi {
	struct workqueue_struct *workqueue;
	struct work_struct work;
	struct list_head queue;
	int queue_state;
	void __iomem *regs;
	struct clk *devclk;
	struct clk *aperclk;
	struct notifier_block clk_rate_change_nb;
	int irq;
	u32 speed_hz;
	spinlock_t trans_queue_lock;
	spinlock_t ctrl_reg_lock;
	const u8 *txbuf;
	u8 *rxbuf;
	int remaining_bytes;
	u8 dev_busy;
	struct completion done;
};

/**
 * zynq_spi_init_hw - Initialize the hardware and configure the SPI controller
 * @regs_base:		Base address of SPI controller
 *
 * On reset the SPI controller is configured to be in master mode, baud rate
 * divisor is set to 2, threshold value for TX FIFO not full interrupt is set
 * to 1 and size of the word to be transferred as 8 bit.
 * This function initializes the SPI controller to disable and clear all the
 * interrupts, enable manual slave select and manual start, deselect all the
 * chip select lines, and enable the SPI controller.
 */
static void zynq_spi_init_hw(void __iomem *regs_base)
{
	zynq_spi_write(regs_base + ZYNQ_SPI_ER_OFFSET,
		       ~ZYNQ_SPI_ER_ENABLE_MASK);
	zynq_spi_write(regs_base + ZYNQ_SPI_IDR_OFFSET, 0x7F);

	/* Clear the RX FIFO */
	while (zynq_spi_read(regs_base + ZYNQ_SPI_ISR_OFFSET) &
			ZYNQ_SPI_IXR_RXNEMTY_MASK)
		zynq_spi_read(regs_base + ZYNQ_SPI_RXD_OFFSET);

	zynq_spi_write(regs_base + ZYNQ_SPI_ISR_OFFSET, 0x7F);
	zynq_spi_write(regs_base + ZYNQ_SPI_CR_OFFSET, 0x0000FC01);
	zynq_spi_write(regs_base + ZYNQ_SPI_ER_OFFSET, ZYNQ_SPI_ER_ENABLE_MASK);
}

/**
 * zynq_spi_chipselect - Select or deselect the chip select line
 * @spi:	Pointer to the spi_device structure
 * @is_on:	Select(1) or deselect (0) the chip select line
 */
static void zynq_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct zynq_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ctrl_reg;
	unsigned long flags;

	spin_lock_irqsave(&xspi->ctrl_reg_lock, flags);

	ctrl_reg = zynq_spi_read(xspi->regs + ZYNQ_SPI_CR_OFFSET);

	if (is_on) {
		/* Select the slave */
		ctrl_reg &= ~ZYNQ_SPI_CR_SSCTRL_MASK;
		ctrl_reg |= (((~(0x0001 << spi->chip_select)) << 10) &
				ZYNQ_SPI_CR_SSCTRL_MASK);
	} else {
		/* Deselect the slave */
		ctrl_reg |= ZYNQ_SPI_CR_SSCTRL_MASK;
	}

	zynq_spi_write(xspi->regs + ZYNQ_SPI_CR_OFFSET, ctrl_reg);

	spin_unlock_irqrestore(&xspi->ctrl_reg_lock, flags);
}

/**
 * zynq_spi_setup_transfer - Configure SPI controller for specified transfer
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provides information
 *		about next transfer setup parameters
 *
 * Sets the operational mode of SPI controller for the next SPI transfer and
 * sets the requested clock frequency.
 *
 * Return:	0 on success and error value on error
 *
 * Note: If the requested frequency is not an exact match with what can be
 * obtained using the prescalar value the driver sets the clock frequency which
 * is lower than the requested frequency (maximum lower) for the transfer. If
 * the requested frequency is higher or lower than that is supported by the SPI
 * controller the driver will set the highest or lowest frequency supported by
 * controller.
 */
static int zynq_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *transfer)
{
	struct zynq_spi *xspi = spi_master_get_devdata(spi->master);
	u8 bits_per_word;
	u32 ctrl_reg;
	u32 req_hz;
	u32 baud_rate_val;
	unsigned long flags, frequency;

	bits_per_word = (transfer) ?
			transfer->bits_per_word : spi->bits_per_word;
	req_hz = (transfer) ? transfer->speed_hz : spi->max_speed_hz;

	if (bits_per_word != 8) {
		dev_err(&spi->dev, "%s, unsupported bits per word %x\n",
			__func__, spi->bits_per_word);
		return -EINVAL;
	}

	frequency = clk_get_rate(xspi->devclk);

	spin_lock_irqsave(&xspi->ctrl_reg_lock, flags);

	zynq_spi_write(xspi->regs + ZYNQ_SPI_ER_OFFSET,
		       ~ZYNQ_SPI_ER_ENABLE_MASK);
	ctrl_reg = zynq_spi_read(xspi->regs + ZYNQ_SPI_CR_OFFSET);

	/* Set the SPI clock phase and clock polarity */
	ctrl_reg &= (~ZYNQ_SPI_CR_CPHA_MASK) & (~ZYNQ_SPI_CR_CPOL_MASK);
	if (spi->mode & SPI_CPHA)
		ctrl_reg |= ZYNQ_SPI_CR_CPHA_MASK;
	if (spi->mode & SPI_CPOL)
		ctrl_reg |= ZYNQ_SPI_CR_CPOL_MASK;

	/* Set the clock frequency */
	if (xspi->speed_hz != req_hz) {
		baud_rate_val = 1;	/* first valid value is 1 */
		while ((baud_rate_val < 8) && (frequency /
					(2 << baud_rate_val)) > req_hz)
			baud_rate_val++;

		ctrl_reg &= 0xFFFFFFC7;
		ctrl_reg |= (baud_rate_val << 3);

		xspi->speed_hz = (frequency / (2 << baud_rate_val));
	}

	zynq_spi_write(xspi->regs + ZYNQ_SPI_CR_OFFSET, ctrl_reg);
	zynq_spi_write(xspi->regs + ZYNQ_SPI_ER_OFFSET,
		       ZYNQ_SPI_ER_ENABLE_MASK);

	spin_unlock_irqrestore(&xspi->ctrl_reg_lock, flags);

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w, %u clock speed\n",
		__func__, spi->mode, spi->bits_per_word,
		xspi->speed_hz);

	return 0;
}

/**
 * zynq_spi_setup - Configure the SPI controller
 * @spi:	Pointer to the spi_device structure
 *
 * Sets the operational mode of SPI controller for the next SPI transfer, sets
 * the baud rate and divisor value to setup the requested spi clock.
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_setup(struct spi_device *spi)
{
	if (!spi->max_speed_hz)
		return -EINVAL;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	return zynq_spi_setup_transfer(spi, NULL);
}

/**
 * zynq_spi_fill_tx_fifo - Fills the TX FIFO with as many bytes as possible
 * @xspi:	Pointer to the zynq_spi structure
 */
static void zynq_spi_fill_tx_fifo(struct zynq_spi *xspi)
{
	while ((zynq_spi_read(xspi->regs + ZYNQ_SPI_ISR_OFFSET) &
		0x00000008) == 0
		&& (xspi->remaining_bytes > 0)) {
		if (xspi->txbuf)
			zynq_spi_write(xspi->regs + ZYNQ_SPI_TXD_OFFSET,
					*xspi->txbuf++);
		else
			zynq_spi_write(xspi->regs + ZYNQ_SPI_TXD_OFFSET, 0);

		xspi->remaining_bytes--;
	}
}

/**
 * zynq_spi_irq - Interrupt service routine of the SPI controller
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
static irqreturn_t zynq_spi_irq(int irq, void *dev_id)
{
	struct zynq_spi *xspi = dev_id;
	u32 intr_status;

	intr_status = zynq_spi_read(xspi->regs + ZYNQ_SPI_ISR_OFFSET);
	zynq_spi_write(xspi->regs + ZYNQ_SPI_ISR_OFFSET, intr_status);
	zynq_spi_write(xspi->regs + ZYNQ_SPI_IDR_OFFSET, ZYNQ_SPI_IXR_ALL_MASK);

	if (intr_status & ZYNQ_SPI_IXR_MODF_MASK) {
		/* Indicate that transfer is completed, the SPI subsystem will
		 * identify the error as the remaining bytes to be
		 * transferred is non-zero */
		complete(&xspi->done);
	} else if (intr_status & ZYNQ_SPI_IXR_TXOW_MASK) {
		u32 ctrl_reg;

		/* Read out the data from the RX FIFO */
		while (zynq_spi_read(xspi->regs + ZYNQ_SPI_ISR_OFFSET) &
				ZYNQ_SPI_IXR_RXNEMTY_MASK) {
			u8 data;

			data = zynq_spi_read(xspi->regs + ZYNQ_SPI_RXD_OFFSET);
			if (xspi->rxbuf)
				*xspi->rxbuf++ = data;

			/* Data memory barrier is placed here to ensure that
			 * data read operation is completed before the status
			 * read is initiated. Without dmb, there are chances
			 * that data and status reads will appear at the SPI
			 * peripheral back-to-back which results in an
			 * incorrect status read.
			 */
			dmb();
		}

		if (xspi->remaining_bytes) {
			/* There is more data to send */
			zynq_spi_fill_tx_fifo(xspi);

			zynq_spi_write(xspi->regs + ZYNQ_SPI_IER_OFFSET,
					ZYNQ_SPI_IXR_ALL_MASK);

			spin_lock(&xspi->ctrl_reg_lock);

			ctrl_reg = zynq_spi_read(xspi->regs +
						 ZYNQ_SPI_CR_OFFSET);
			ctrl_reg |= ZYNQ_SPI_CR_MANSTRT_MASK;
			zynq_spi_write(xspi->regs + ZYNQ_SPI_CR_OFFSET,
				       ctrl_reg);

			spin_unlock(&xspi->ctrl_reg_lock);
		} else {
			/* Transfer is completed */
			complete(&xspi->done);
		}
	}

	return IRQ_HANDLED;
}

/**
 * zynq_spi_start_transfer - Initiates the SPI transfer
 * @spi:	Pointer to the spi_device structure
 * @transfer:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * This function fills the TX FIFO, starts the SPI transfer, and waits for the
 * transfer to be completed.
 *
 * Return:	Number of bytes transferred in the last transfer
 */
static int zynq_spi_start_transfer(struct spi_device *spi,
			struct spi_transfer *transfer)
{
	struct zynq_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ctrl_reg;
	unsigned long flags;

	xspi->txbuf = transfer->tx_buf;
	xspi->rxbuf = transfer->rx_buf;
	xspi->remaining_bytes = transfer->len;
	reinit_completion(&xspi->done);

	zynq_spi_fill_tx_fifo(xspi);

	zynq_spi_write(xspi->regs + ZYNQ_SPI_IER_OFFSET, ZYNQ_SPI_IXR_ALL_MASK);

	spin_lock_irqsave(&xspi->ctrl_reg_lock, flags);

	/* Start the transfer by enabling manual start bit */
	ctrl_reg = zynq_spi_read(xspi->regs + ZYNQ_SPI_CR_OFFSET);
	ctrl_reg |= ZYNQ_SPI_CR_MANSTRT_MASK;
	zynq_spi_write(xspi->regs + ZYNQ_SPI_CR_OFFSET, ctrl_reg);

	spin_unlock_irqrestore(&xspi->ctrl_reg_lock, flags);

	wait_for_completion(&xspi->done);

	return (transfer->len) - (xspi->remaining_bytes);
}

/**
 * zynq_spi_work_queue - Perform transfers
 * @work:	Pointer to the work_struct structure
 *
 * Get the transfer request from queue to perform transfers
 */
static void zynq_spi_work_queue(struct work_struct *work)
{
	struct zynq_spi *xspi = container_of(work, struct zynq_spi, work);
	unsigned long flags;

	spin_lock_irqsave(&xspi->trans_queue_lock, flags);
	xspi->dev_busy = 1;

	if (list_empty(&xspi->queue) ||
		xspi->queue_state == ZYNQ_SPI_QUEUE_STOPPED) {
		xspi->dev_busy = 0;
		spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);
		return;
	}

	while (!list_empty(&xspi->queue)) {
		struct spi_message *msg;
		struct spi_device *spi;
		struct spi_transfer *transfer = NULL;
		unsigned cs_change = 1;
		int status = 0;

		msg = container_of(xspi->queue.next, struct spi_message, queue);
		list_del_init(&msg->queue);
		spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);
		spi = msg->spi;

		list_for_each_entry(transfer, &msg->transfers, transfer_list) {
			if ((transfer->bits_per_word || transfer->speed_hz) &&
								cs_change) {
				status = zynq_spi_setup_transfer(spi, transfer);
				if (status < 0)
					break;
			}

			if (cs_change)
				zynq_spi_chipselect(spi, 1);

			cs_change = transfer->cs_change;

			if (!transfer->tx_buf && !transfer->rx_buf &&
				transfer->len) {
				status = -EINVAL;
				break;
			}

			if (transfer->len)
				status = zynq_spi_start_transfer(spi, transfer);

			if (status != transfer->len) {
				if (status > 0)
					status = -EMSGSIZE;
				break;
			}
			msg->actual_length += status;
			status = 0;

			if (transfer->delay_usecs)
				udelay(transfer->delay_usecs);

			if (!cs_change)
				continue;
			if (transfer->transfer_list.next == &msg->transfers)
				break;

			zynq_spi_chipselect(spi, 0);
		}

		msg->status = status;
		msg->complete(msg->context);

		if (!(status == 0 && cs_change))
			zynq_spi_chipselect(spi, 0);

		spin_lock_irqsave(&xspi->trans_queue_lock, flags);
	}
	xspi->dev_busy = 0;
	spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);
}

/**
 * zynq_spi_transfer - Add a new transfer request at the tail of work queue
 * @spi:	Pointer to the spi_device structure
 * @message:	Pointer to the spi_transfer structure which provide information
 *		about next transfer parameters
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_transfer(struct spi_device *spi,
			     struct spi_message *message)
{
	struct zynq_spi *xspi = spi_master_get_devdata(spi->master);
	struct spi_transfer *transfer;
	unsigned long flags;

	if (xspi->queue_state == ZYNQ_SPI_QUEUE_STOPPED)
		return -ESHUTDOWN;

	message->actual_length = 0;
	message->status = -EINPROGRESS;

	/* Check each transfer's parameters */
	list_for_each_entry(transfer, &message->transfers, transfer_list) {
		u8 bits_per_word =
			transfer->bits_per_word ? : spi->bits_per_word;

		bits_per_word = bits_per_word ? : 8;
		if (!transfer->tx_buf && !transfer->rx_buf && transfer->len)
			return -EINVAL;
		if (bits_per_word != 8)
			return -EINVAL;
	}

	spin_lock_irqsave(&xspi->trans_queue_lock, flags);
	list_add_tail(&message->queue, &xspi->queue);
	if (!xspi->dev_busy)
		queue_work(xspi->workqueue, &xspi->work);
	spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);

	return 0;
}

/**
 * zynq_spi_start_queue - Starts the queue of the SPI driver
 * @xspi:	Pointer to the zynq_spi structure
 *
 * Return:	0 on success and error value on error
 */
static inline int zynq_spi_start_queue(struct zynq_spi *xspi)
{
	unsigned long flags;

	spin_lock_irqsave(&xspi->trans_queue_lock, flags);

	if (xspi->queue_state == ZYNQ_SPI_QUEUE_RUNNING || xspi->dev_busy) {
		spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);
		return -EBUSY;
	}

	xspi->queue_state = ZYNQ_SPI_QUEUE_RUNNING;
	spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);

	return 0;
}

/**
 * zynq_spi_stop_queue - Stops the queue of the SPI driver
 * @xspi:	Pointer to the zynq_spi structure
 *
 * This function waits till queue is empty and then stops the queue.
 * Maximum time out is set to 5 seconds.
 *
 * Return:	0 on success and error value on error
 */
static inline int zynq_spi_stop_queue(struct zynq_spi *xspi)
{
	unsigned long flags;
	unsigned limit = 500;
	int ret = 0;

	if (xspi->queue_state != ZYNQ_SPI_QUEUE_RUNNING)
		return ret;

	spin_lock_irqsave(&xspi->trans_queue_lock, flags);

	while ((!list_empty(&xspi->queue) || xspi->dev_busy) && limit--) {
		spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);
		msleep(10);
		spin_lock_irqsave(&xspi->trans_queue_lock, flags);
	}

	if (!list_empty(&xspi->queue) || xspi->dev_busy)
		ret = -EBUSY;

	if (ret == 0)
		xspi->queue_state = ZYNQ_SPI_QUEUE_STOPPED;

	spin_unlock_irqrestore(&xspi->trans_queue_lock, flags);

	return ret;
}

/**
 * zynq_spi_destroy_queue - Destroys the queue of the SPI driver
 * @xspi:	Pointer to the zynq_spi structure
 *
 * Return:	0 on success and error value on error
 */
static inline int zynq_spi_destroy_queue(struct zynq_spi *xspi)
{
	int ret;

	ret = zynq_spi_stop_queue(xspi);
	if (ret != 0)
		return ret;

	destroy_workqueue(xspi->workqueue);

	return 0;
}

static int zynq_spi_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	switch (event) {
	case PRE_RATE_CHANGE:
		/* if a rate change is announced we need to check whether we can
		 * maintain the current frequency by changing the clock
		 * dividers. And we may have to suspend operation and return
		 * after the rate change or its abort
		 */
		return NOTIFY_OK;
	case POST_RATE_CHANGE:
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

/**
 * zynq_spi_probe - Probe method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the driver data structures and the hardware.
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct spi_master *master;
	struct zynq_spi *xspi;
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

	xspi->irq = platform_get_irq(pdev, 0);
	if (xspi->irq < 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "irq number is negative\n");
		goto remove_master;
	}

	ret = devm_request_irq(&pdev->dev, xspi->irq, zynq_spi_irq,
			       0, pdev->name, xspi);
	if (ret != 0) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "request_irq failed\n");
		goto remove_master;
	}

	xspi->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(xspi->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		ret = PTR_ERR(xspi->aperclk);
		goto remove_master;
	}

	xspi->devclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xspi->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		ret = PTR_ERR(xspi->devclk);
		goto remove_master;
	}

	ret = clk_prepare_enable(xspi->aperclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto remove_master;
	}

	ret = clk_prepare_enable(xspi->devclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_aper;
	}

	xspi->clk_rate_change_nb.notifier_call = zynq_spi_clk_notifier_cb;
	xspi->clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(xspi->devclk, &xspi->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	/* SPI controller initializations */
	zynq_spi_init_hw(xspi->regs);

	init_completion(&xspi->done);

	ret = of_property_read_u32(pdev->dev.of_node, "num-chip-select",
				   (u32 *)&master->num_chipselect);
	if (ret < 0) {
		dev_err(&pdev->dev, "couldn't determine num-chip-select\n");
		goto clk_notif_unreg;
	}
	master->setup = zynq_spi_setup;
	master->transfer = zynq_spi_transfer;
	master->mode_bits = SPI_CPOL | SPI_CPHA;

	xspi->speed_hz = clk_get_rate(xspi->devclk) / 2;

	xspi->dev_busy = 0;

	INIT_LIST_HEAD(&xspi->queue);
	spin_lock_init(&xspi->trans_queue_lock);
	spin_lock_init(&xspi->ctrl_reg_lock);

	xspi->queue_state = ZYNQ_SPI_QUEUE_STOPPED;
	xspi->dev_busy = 0;

	INIT_WORK(&xspi->work, zynq_spi_work_queue);
	xspi->workqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!xspi->workqueue) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "problem initializing queue\n");
		goto clk_notif_unreg;
	}

	ret = zynq_spi_start_queue(xspi);
	if (ret != 0) {
		dev_err(&pdev->dev, "problem starting queue\n");
		goto remove_queue;
	}

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto remove_queue;
	}

	dev_info(&pdev->dev, "at 0x%08X mapped to 0x%08X, irq=%d\n", res->start,
			(u32 __force)xspi->regs, xspi->irq);

	return ret;

remove_queue:
	(void)zynq_spi_destroy_queue(xspi);
clk_notif_unreg:
	clk_notifier_unregister(xspi->devclk, &xspi->clk_rate_change_nb);
	clk_disable_unprepare(xspi->devclk);
clk_dis_aper:
	clk_disable_unprepare(xspi->aperclk);
remove_master:
	spi_master_put(master);
	return ret;
}

/**
 * zynq_spi_remove - Remove method for the SPI driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_spi *xspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = zynq_spi_destroy_queue(xspi);
	if (ret != 0)
		return ret;

	zynq_spi_write(xspi->regs + ZYNQ_SPI_ER_OFFSET,
		       ~ZYNQ_SPI_ER_ENABLE_MASK);

	clk_notifier_unregister(xspi->devclk, &xspi->clk_rate_change_nb);
	clk_disable_unprepare(xspi->devclk);
	clk_disable_unprepare(xspi->aperclk);

	spi_unregister_master(master);
	spi_master_put(master);

	dev_dbg(&pdev->dev, "remove succeeded\n");
	return 0;

}

#ifdef CONFIG_PM_SLEEP
/**
 * zynq_spi_suspend - Suspend method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function stops the SPI driver queue and disables the SPI controller
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_spi *xspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = zynq_spi_stop_queue(xspi);
	if (ret != 0)
		return ret;

	zynq_spi_write(xspi->regs + ZYNQ_SPI_ER_OFFSET,
		       ~ZYNQ_SPI_ER_ENABLE_MASK);

	clk_disable(xspi->devclk);
	clk_disable(xspi->aperclk);

	dev_dbg(&pdev->dev, "suspend succeeded\n");
	return 0;
}

/**
 * zynq_spi_resume - Resume method for the SPI driver
 * @dev:	Address of the platform_device structure
 *
 * This function starts the SPI driver queue and initializes the SPI controller
 *
 * Return:	0 on success and error value on error
 */
static int zynq_spi_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
			struct platform_device, dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct zynq_spi *xspi = spi_master_get_devdata(master);
	int ret = 0;

	ret = clk_enable(xspi->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(xspi->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xspi->aperclk);
		return ret;
	}

	zynq_spi_init_hw(xspi->regs);

	ret = zynq_spi_start_queue(xspi);
	if (ret != 0) {
		dev_err(&pdev->dev, "problem starting queue (%d)\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "resume succeeded\n");
	return 0;
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(zynq_spi_dev_pm_ops, zynq_spi_suspend,
			 zynq_spi_resume);

/* Work with hotplug and coldplug */
MODULE_ALIAS("platform:" ZYNQ_SPI_NAME);

static struct of_device_id zynq_spi_of_match[] = {
	{ .compatible = "xlnx,zynq-spi-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, zynq_spi_of_match);

/*
 * zynq_spi_driver - This structure defines the SPI subsystem platform driver
 */
static struct platform_driver zynq_spi_driver = {
	.probe	= zynq_spi_probe,
	.remove	= zynq_spi_remove,
	.driver = {
		.name = ZYNQ_SPI_NAME,
		.owner = THIS_MODULE,
		.of_match_table = zynq_spi_of_match,
		.pm = &zynq_spi_dev_pm_ops,
	},
};

module_platform_driver(zynq_spi_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq SPI driver");
MODULE_LICENSE("GPL");

