/*
 * Xilinx I2C bus driver for the Zynq I2C Interfaces.
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * Workaround in Receive Mode
 *	If there is only one message to be processed, then based on length of
 *	the message we set the HOLD bit.
 *	If the length is less than the FIFO depth, then we will directly
 *	receive a COMP interrupt and the transaction is done.
 *	If the length is more than the FIFO depth, then we enable the HOLD bit.
 *	if the requested data is greater than the  max transfer size(252 bytes)
 *	update the transfer size register with max transfer size else update
 *	with the requested size.
 *	We will receive the DATA interrupt, if the transfer size register value
 *	is zero then repeat the above step for the remaining bytes (if any) and
 *	process the data in the fifo.
 *
 *	The bus hold flag logic provides support for repeated start.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * Register Map
 * Register offsets for the I2C device.
 */
#define ZYNQ_I2C_CR_OFFSET	0x00 /* Control Register, RW */
#define ZYNQ_I2C_SR_OFFSET	0x04 /* Status Register, RO */
#define ZYNQ_I2C_ADDR_OFFSET	0x08 /* I2C Address Register, RW */
#define ZYNQ_I2C_DATA_OFFSET	0x0C /* I2C Data Register, RW */
#define ZYNQ_I2C_ISR_OFFSET	0x10 /* Interrupt Status Register, RW */
#define ZYNQ_I2C_XFER_SIZE_OFFSET	0x14 /* Transfer Size Register, RW */
#define ZYNQ_I2C_TIME_OUT_OFFSET	0x1C /* Time Out Register, RW */
#define ZYNQ_I2C_IER_OFFSET	0x24 /* Interrupt Enable Register, WO */
#define ZYNQ_I2C_IDR_OFFSET	0x28 /* Interrupt Disable Register, WO */

/*
 * Control Register Bit mask definitions
 * This register contains various control bits that affect the operation of the
 * I2C controller.
 */
#define ZYNQ_I2C_CR_HOLD_BUS_MASK	0x00000010 /* Hold Bus bit */
/* Read or Write Master transfer 0= Transmitter, 1= Receiver */
#define ZYNQ_I2C_CR_RW_MASK		0x00000001
/* 1 = Auto init FIFO to zeroes */
#define ZYNQ_I2C_CR_CLR_FIFO_MASK	0x00000040

/*
 * I2C Address Register Bit mask definitions
 * Normal addressing mode uses [6:0] bits. Extended addressing mode uses [9:0]
 * bits. A write access to this register always initiates a transfer if the I2C
 * is in master mode.
 */
#define ZYNQ_I2C_ADDR_MASK	0x000003FF /* I2C Address Mask */

/*
 * I2C Interrupt Registers Bit mask definitions
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define ZYNQ_I2C_IXR_ALL_INTR_MASK	0x000002FF /* All ISR Mask */

#define ZYNQ_I2C_FIFO_DEPTH	16		/* FIFO Depth */
#define ZYNQ_I2C_TIMEOUT	(50 * HZ)	/* Timeout for bus busy check */
#define ZYNQ_I2C_ENABLED_INTR	0x2EF		/* Enabled Interrupts */

#define ZYNQ_I2C_DATA_INTR_DEPTH (ZYNQ_I2C_FIFO_DEPTH - 2)/* FIFO depth at which
							 * the DATA interrupt
							 * occurs
							 */
#define ZYNQ_I2C_MAX_TRANSFER_SIZE	255 /* Max transfer size */
/* Transfer size in multiples of data interrupt depth */
#define ZYNQ_I2C_TRANSFER_SIZE	(ZYNQ_I2C_MAX_TRANSFER_SIZE - 3)

#define DRIVER_NAME		"zynq-i2c"

#define ZYNQ_I2C_SPEED_MAX	400000

#define zynq_i2c_readreg(offset)	__raw_readl(id->membase + offset)
#define zynq_i2c_writereg(val, offset)	__raw_writel(val, id->membase + offset)

/**
 * struct zynq_i2c - I2C device private data structure
 * @membase:		Base address of the I2C device
 * @adap:		I2C adapter instance
 * @p_msg:		Message pointer
 * @err_status:		Error status in Interrupt Status Register
 * @xfer_done:		Transfer complete status
 * @p_send_buf:		Pointer to transmit buffer
 * @p_recv_buf:		Pointer to receive buffer
 * @suspended:		Flag holding the device's PM status
 * @send_count:		Number of bytes still expected to send
 * @recv_count:		Number of bytes still expected to receive
 * @irq:		IRQ number
 * @cur_timeout:	The current timeout value used by the device
 * @input_clk:		Input clock to I2C controller
 * @i2c_clk:		Maximum I2C clock speed
 * @bus_hold_flag:	Flag used in repeated start for clearing HOLD bit
 * @clk:		Pointer to struct clk
 * @clk_rate_change_nb:	Notifier block for clock rate changes
 */
struct zynq_i2c {
	void __iomem *membase;
	struct i2c_adapter adap;
	struct i2c_msg	*p_msg;
	int err_status;
	struct completion xfer_done;
	unsigned char *p_send_buf;
	unsigned char *p_recv_buf;
	u8 suspended;
	unsigned int send_count;
	unsigned int recv_count;
	int irq;
	int cur_timeout;
	unsigned long input_clk;
	unsigned int i2c_clk;
	unsigned int bus_hold_flag;
	struct clk	*clk;
	struct notifier_block	clk_rate_change_nb;
};

#define to_zynq_i2c(_nb)	container_of(_nb, struct zynq_i2c, \
					     clk_rate_change_nb)
#define MAX_F_ERR 10000

/**
 * zynq_i2c_isr - Interrupt handler for the I2C device
 * @irq:	irq number for the I2C device
 * @ptr:	void pointer to zynq_i2c structure
 *
 * Return: IRQ_HANDLED always
 *
 * This function handles the data interrupt, transfer complete interrupt and
 * the error interrupts of the I2C device.
 */
static irqreturn_t zynq_i2c_isr(int irq, void *ptr)
{
	unsigned int isr_status, avail_bytes;
	unsigned int bytes_to_recv, bytes_to_send;
	unsigned int ctrl_reg;
	struct zynq_i2c *id = ptr;

	isr_status = zynq_i2c_readreg(ZYNQ_I2C_ISR_OFFSET);

	/* Handling Nack interrupt */
	if (isr_status & 0x4)
		complete(&id->xfer_done);

	/* Handling Arbitration lost interrupt */
	if (isr_status & 0x200)
		complete(&id->xfer_done);

	/* Handling Data interrupt */
	if (isr_status & 0x2) {
		if (id->recv_count >= ZYNQ_I2C_DATA_INTR_DEPTH) {
			/* Always read data interrupt threshold bytes */
			bytes_to_recv = ZYNQ_I2C_DATA_INTR_DEPTH;
			id->recv_count = id->recv_count -
						ZYNQ_I2C_DATA_INTR_DEPTH;
			avail_bytes = zynq_i2c_readreg(
						ZYNQ_I2C_XFER_SIZE_OFFSET);
			/*
			 * if the tranfer size register value is zero, then
			 * check for the remaining bytes and update the
			 * transfer size register.
			 */
			if (!avail_bytes) {
				if (id->recv_count > ZYNQ_I2C_TRANSFER_SIZE)
					zynq_i2c_writereg(
						ZYNQ_I2C_TRANSFER_SIZE,
						ZYNQ_I2C_XFER_SIZE_OFFSET);
				else
					zynq_i2c_writereg(id->recv_count,
						ZYNQ_I2C_XFER_SIZE_OFFSET);
			}
			/* Process the data received */
			while (bytes_to_recv) {
				*(id->p_recv_buf)++ =
					zynq_i2c_readreg(ZYNQ_I2C_DATA_OFFSET);
				bytes_to_recv = bytes_to_recv - 1;
			}

			if (!id->bus_hold_flag &&
				(id->recv_count <= ZYNQ_I2C_FIFO_DEPTH)) {
				/* Clear the hold bus bit */
				zynq_i2c_writereg(
					zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET) &
					~ZYNQ_I2C_CR_HOLD_BUS_MASK,
					ZYNQ_I2C_CR_OFFSET);
			}
		}
	}

	/* Handling Transfer Complete interrupt */
	if (isr_status & 1) {
		if (!id->p_recv_buf) {
			/*
			 * If the device is sending data If there is further
			 * data to be sent. Calculate the available space
			 * in FIFO and fill the FIFO with that many bytes.
			 */
			if (id->send_count) {
				avail_bytes = ZYNQ_I2C_FIFO_DEPTH -
				zynq_i2c_readreg(ZYNQ_I2C_XFER_SIZE_OFFSET);
				if (id->send_count > avail_bytes)
					bytes_to_send = avail_bytes;
				else
					bytes_to_send = id->send_count;

				while (bytes_to_send--) {
					zynq_i2c_writereg(
						(*(id->p_send_buf)++),
						 ZYNQ_I2C_DATA_OFFSET);
					id->send_count--;
				}
			} else {
				/*
				 * Signal the completion of transaction and
				 * clear the hold bus bit if there are no
				 * further messages to be processed.
				 */
				complete(&id->xfer_done);
			}
			if (!id->send_count) {
				if (!id->bus_hold_flag) {
					/* Clear the hold bus bit */
					ctrl_reg =
					zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
					if (ctrl_reg &
					     ZYNQ_I2C_CR_HOLD_BUS_MASK)
						zynq_i2c_writereg(ctrl_reg &
						~ZYNQ_I2C_CR_HOLD_BUS_MASK,
						ZYNQ_I2C_CR_OFFSET);
				}
			}
		} else {
			if (!id->bus_hold_flag) {
				/* Clear the hold bus bit */
				ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
				if (ctrl_reg & ZYNQ_I2C_CR_HOLD_BUS_MASK)
					zynq_i2c_writereg(ctrl_reg &
						~ZYNQ_I2C_CR_HOLD_BUS_MASK,
						ZYNQ_I2C_CR_OFFSET);
			}
			/*
			 * If the device is receiving data, then signal
			 * the completion of transaction and read the data
			 * present in the FIFO. Signal the completion of
			 * transaction.
			 */
			while (zynq_i2c_readreg(ZYNQ_I2C_SR_OFFSET) & 0x20) {
				*(id->p_recv_buf)++ =
					zynq_i2c_readreg(ZYNQ_I2C_DATA_OFFSET);
				id->recv_count--;
			}
			complete(&id->xfer_done);
		}
	}

	/* Update the status for errors */
	id->err_status = isr_status & 0x2EC;
	zynq_i2c_writereg(isr_status, ZYNQ_I2C_ISR_OFFSET);
	return IRQ_HANDLED;
}

/**
 * zynq_i2c_mrecv - Prepare and start a master receive operation
 * @id:		pointer to the i2c device structure
 */
static void zynq_i2c_mrecv(struct zynq_i2c *id)
{
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = id->p_msg->buf;
	id->recv_count = id->p_msg->len;

	/*
	 * Set the controller in master receive mode and clear the FIFO.
	 * Set the slave address in address register.
	 * Check for the message size against FIFO depth and set the
	 * HOLD bus bit if it is more than FIFO depth.
	 * Clear the interrupts in interrupt status register.
	 */
	ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
	ctrl_reg |= ZYNQ_I2C_CR_RW_MASK | ZYNQ_I2C_CR_CLR_FIFO_MASK;

	if ((id->p_msg->flags & I2C_M_RECV_LEN) == I2C_M_RECV_LEN)
		id->recv_count = I2C_SMBUS_BLOCK_MAX + 1;

	if (id->recv_count > ZYNQ_I2C_FIFO_DEPTH)
		ctrl_reg |= ZYNQ_I2C_CR_HOLD_BUS_MASK;

	zynq_i2c_writereg(ctrl_reg, ZYNQ_I2C_CR_OFFSET);

	isr_status = zynq_i2c_readreg(ZYNQ_I2C_ISR_OFFSET);
	zynq_i2c_writereg(isr_status, ZYNQ_I2C_ISR_OFFSET);

	zynq_i2c_writereg(id->p_msg->addr & ZYNQ_I2C_ADDR_MASK,
						ZYNQ_I2C_ADDR_OFFSET);
	/*
	 * The no. of bytes to receive is checked against the limit of
	 * max transfer size. Set transfer size register with no of bytes
	 * receive if it is less than transfer size and transfer size if
	 * it is more. Enable the interrupts.
	 */
	if (id->recv_count > ZYNQ_I2C_TRANSFER_SIZE)
		zynq_i2c_writereg(ZYNQ_I2C_TRANSFER_SIZE,
				  ZYNQ_I2C_XFER_SIZE_OFFSET);
	else
		zynq_i2c_writereg(id->recv_count, ZYNQ_I2C_XFER_SIZE_OFFSET);
	/*
	 * Clear the bus hold flag if bytes to receive is less than FIFO size.
	 */
	if (!id->bus_hold_flag &&
		((id->p_msg->flags & I2C_M_RECV_LEN) != I2C_M_RECV_LEN) &&
		(id->recv_count <= ZYNQ_I2C_FIFO_DEPTH)) {
			/* Clear the hold bus bit */
			ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
			if (ctrl_reg & ZYNQ_I2C_CR_HOLD_BUS_MASK)
				zynq_i2c_writereg(ctrl_reg &
						~ZYNQ_I2C_CR_HOLD_BUS_MASK,
						ZYNQ_I2C_CR_OFFSET);
	}
	zynq_i2c_writereg(ZYNQ_I2C_ENABLED_INTR, ZYNQ_I2C_IER_OFFSET);
}

/**
 * zynq_i2c_msend - Prepare and start a master send operation
 * @id:		pointer to the i2c device
 */
static void zynq_i2c_msend(struct zynq_i2c *id)
{
	unsigned int avail_bytes;
	unsigned int bytes_to_send;
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = NULL;
	id->p_send_buf = id->p_msg->buf;
	id->send_count = id->p_msg->len;

	/*
	 * Set the controller in Master transmit mode and clear the FIFO.
	 * Set the slave address in address register.
	 * Check for the message size against FIFO depth and set the
	 * HOLD bus bit if it is more than FIFO depth.
	 * Clear the interrupts in interrupt status register.
	 */
	ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
	ctrl_reg &= ~ZYNQ_I2C_CR_RW_MASK;
	ctrl_reg |= ZYNQ_I2C_CR_CLR_FIFO_MASK;

	if (id->send_count > ZYNQ_I2C_FIFO_DEPTH)
		ctrl_reg |= ZYNQ_I2C_CR_HOLD_BUS_MASK;
	zynq_i2c_writereg(ctrl_reg, ZYNQ_I2C_CR_OFFSET);

	isr_status = zynq_i2c_readreg(ZYNQ_I2C_ISR_OFFSET);
	zynq_i2c_writereg(isr_status, ZYNQ_I2C_ISR_OFFSET);

	/*
	 * Calculate the space available in FIFO. Check the message length
	 * against the space available, and fill the FIFO accordingly.
	 * Enable the interrupts.
	 */
	avail_bytes = ZYNQ_I2C_FIFO_DEPTH -
				zynq_i2c_readreg(ZYNQ_I2C_XFER_SIZE_OFFSET);

	if (id->send_count > avail_bytes)
		bytes_to_send = avail_bytes;
	else
		bytes_to_send = id->send_count;

	while (bytes_to_send--) {
		zynq_i2c_writereg((*(id->p_send_buf)++), ZYNQ_I2C_DATA_OFFSET);
		id->send_count--;
	}

	zynq_i2c_writereg(id->p_msg->addr & ZYNQ_I2C_ADDR_MASK,
						ZYNQ_I2C_ADDR_OFFSET);

	/*
	 * Clear the bus hold flag if there is no more data
	 * and if it is the last message.
	 */
	if (!id->bus_hold_flag && !id->send_count) {
		/* Clear the hold bus bit */
		ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
		if (ctrl_reg & ZYNQ_I2C_CR_HOLD_BUS_MASK)
			zynq_i2c_writereg(ctrl_reg & ~ZYNQ_I2C_CR_HOLD_BUS_MASK,
				ZYNQ_I2C_CR_OFFSET);
	}
	zynq_i2c_writereg(ZYNQ_I2C_ENABLED_INTR, ZYNQ_I2C_IER_OFFSET);
}

/**
 * zynq_i2c_master_reset - Reset the interface
 * @adap:	pointer to the i2c adapter driver instance
 *
 * Return: none
 *
 * This function cleanup the fifos, clear the hold bit and status
 * and disable the interrupts.
 */
static void zynq_i2c_master_reset(struct i2c_adapter *adap)
{
	struct zynq_i2c *id = adap->algo_data;
	u32 regval;

	/* Disable the interrupts */
	zynq_i2c_writereg(ZYNQ_I2C_IXR_ALL_INTR_MASK, ZYNQ_I2C_IDR_OFFSET);
	/* Clear the hold bit and fifos */
	regval = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
	regval &= ~ZYNQ_I2C_CR_HOLD_BUS_MASK;
	regval |= ZYNQ_I2C_CR_CLR_FIFO_MASK;
	zynq_i2c_writereg(regval, ZYNQ_I2C_CR_OFFSET);
	/* Update the transfercount register to zero */
	zynq_i2c_writereg(0, ZYNQ_I2C_XFER_SIZE_OFFSET);
	/* Clear the interupt status register */
	regval = zynq_i2c_readreg(ZYNQ_I2C_ISR_OFFSET);
	zynq_i2c_writereg(regval, ZYNQ_I2C_ISR_OFFSET);
	/* Clear the status register */
	regval = zynq_i2c_readreg(ZYNQ_I2C_SR_OFFSET);
	zynq_i2c_writereg(regval, ZYNQ_I2C_SR_OFFSET);
}

/**
 * zynq_i2c_master_xfer - The main i2c transfer function
 * @adap:	pointer to the i2c adapter driver instance
 * @msgs:	pointer to the i2c message structure
 * @num:	the number of messages to transfer
 *
 * Return: number of msgs processed on success, negative error otherwise
 *
 * This function waits for the bus idle condition and updates the timeout if
 * modified by user. Then initiates the send/recv activity based on the
 * transfer message received.
 */
static int zynq_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	struct zynq_i2c *id = adap->algo_data;
	unsigned int retries;
	unsigned long timeout;
	int ret, count;

	/* Waiting for bus-ready. If bus not ready, it returns after timeout */
	timeout = jiffies + ZYNQ_I2C_TIMEOUT;
	while (zynq_i2c_readreg(ZYNQ_I2C_SR_OFFSET) & 0x100) {
		if (time_after(jiffies, timeout)) {
			dev_warn(id->adap.dev.parent,
					"timedout waiting for bus ready\n");
			zynq_i2c_master_reset(adap);
			return -ETIMEDOUT;
		}
		schedule_timeout(1);
	}

	/* The bus is free. Set the new timeout value if updated */
	if (id->adap.timeout != id->cur_timeout) {
		zynq_i2c_writereg((id->adap.timeout & 0xFF),
					ZYNQ_I2C_TIME_OUT_OFFSET);
		id->cur_timeout = id->adap.timeout;
	}

	/*
	 * Set the flag to one when multiple messages are to be
	 * processed with a repeated start.
	 */
	if (num > 1) {
		id->bus_hold_flag = 1;
		zynq_i2c_writereg((zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET) |
				ZYNQ_I2C_CR_HOLD_BUS_MASK), ZYNQ_I2C_CR_OFFSET);
	} else {
		id->bus_hold_flag = 0;
	}

	/* Process the msg one by one */
	for (count = 0; count < num; count++, msgs++) {
		if (count == (num - 1))
			id->bus_hold_flag = 0;
		retries = adap->retries;
retry:
		id->err_status = 0;
		id->p_msg = msgs;
		init_completion(&id->xfer_done);

		/* Check for the TEN Bit mode on each msg */
		if (msgs->flags & I2C_M_TEN) {
			zynq_i2c_writereg(
				zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET) & ~0x4,
				ZYNQ_I2C_CR_OFFSET);
		} else {
			if (!(zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET) & 0x4))
				zynq_i2c_writereg(
					zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET) |
					 0x4, ZYNQ_I2C_CR_OFFSET);
		}

		/* Check for the R/W flag on each msg */
		if (msgs->flags & I2C_M_RD)
			zynq_i2c_mrecv(id);
		else
			zynq_i2c_msend(id);

		/* Wait for the signal of completion */
		ret = wait_for_completion_interruptible_timeout(
							&id->xfer_done, HZ);
		if (!ret) {
			dev_err(id->adap.dev.parent,
				 "timeout waiting on completion\n");
			zynq_i2c_master_reset(adap);
			return -ETIMEDOUT;
		}
		zynq_i2c_writereg(ZYNQ_I2C_IXR_ALL_INTR_MASK,
				  ZYNQ_I2C_IDR_OFFSET);

		/* If it is bus arbitration error, try again */
		if (id->err_status & 0x200) {
			dev_dbg(id->adap.dev.parent,
				 "Lost ownership on bus, trying again\n");
			if (retries--) {
				mdelay(2);
				goto retry;
			}
			dev_err(id->adap.dev.parent,
					 "Retries completed, exit\n");
			num = -EREMOTEIO;
			break;
		}
		/* Report the other error interrupts to application as EIO */
		if (id->err_status & 0xE4) {
			zynq_i2c_master_reset(adap);
			num = -EIO;
			break;
		}
	}

	id->p_msg = NULL;
	id->err_status = 0;

	return num;
}

/**
 * zynq_i2c_func - Returns the supported features of the I2C driver
 * @adap:	pointer to the i2c adapter structure
 *
 * Return: 32 bit value, each bit corresponding to a feature
 */
static u32 zynq_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR |
		(I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
		I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm zynq_i2c_algo = {
	.master_xfer	= zynq_i2c_master_xfer,
	.functionality	= zynq_i2c_func,
};

/**
 * zynq_i2c_calc_divs - Calculate clock dividers
 * @f:		I2C clock frequency
 * @input_clk:	Input clock frequency
 * @a:		First divider (return value)
 * @b:		Second divider (return value)
 * @err:	Frequency error
 *
 * Return: 0 on success, negative errno otherwise.
 *
 * f is used as input and output variable. As input it is used as target I2C
 * frequency. On function exit f holds the actually resulting I2C frequency.
 */
static int zynq_i2c_calc_divs(unsigned long *f, unsigned long input_clk,
		unsigned int *a, unsigned int *b, unsigned int *err)
{
	unsigned long fscl = *f, best_fscl = *f, actual_fscl, temp;
	unsigned int div_a, div_b, calc_div_a = 0, calc_div_b = 0;
	unsigned int last_error, current_error;

	/* calculate (divisor_a+1) x (divisor_b+1) */
	temp = input_clk / (22 * fscl);

	/*
	 * If the calculated value is negative or 0, the fscl input is out of
	 * range. Return error.
	 */
	if (!temp)
		return -EINVAL;

	last_error = -1;
	for (div_b = 0; div_b < 64; div_b++) {
		div_a = input_clk / (22 * fscl * (div_b + 1));

		if (div_a)
			div_a = div_a - 1;

		if (div_a > 3)
			continue;

		actual_fscl = input_clk / (22 * (div_a + 1) * (div_b + 1));

		current_error = ((actual_fscl > fscl) ? (actual_fscl - fscl) :
							(fscl - actual_fscl));

		if (last_error > current_error) {
			calc_div_a = div_a;
			calc_div_b = div_b;
			best_fscl = actual_fscl;
			last_error = current_error;
		}
	}

	*err = last_error;
	*a = calc_div_a;
	*b = calc_div_b;
	*f = best_fscl;

	return 0;
}

/**
 * zynq_i2c_setclk - This function sets the serial clock rate for the I2C device
 * @fscl:	The clock frequency in Hz
 * @id:		Pointer to the I2C device structure
 *
 * Return: zero on success, negative error otherwise
 *
 * The device must be idle rather than busy transferring data before setting
 * these device options.
 * The data rate is set by values in the control register.
 * The formula for determining the correct register values is
 *	Fscl = Fpclk/(22 x (divisor_a+1) x (divisor_b+1))
 * See the hardware data sheet for a full explanation of setting the serial
 * clock rate. The clock can not be faster than the input clock divide by 22.
 * The two most common clock rates are 100KHz and 400KHz.
 */
static int zynq_i2c_setclk(unsigned long fscl, struct zynq_i2c *id)
{
	unsigned int div_a, div_b;
	unsigned int ctrl_reg;
	unsigned int err;
	int ret = 0;

	ret = zynq_i2c_calc_divs(&fscl, id->input_clk, &div_a, &div_b, &err);
	if (ret)
		return ret;

	ctrl_reg = zynq_i2c_readreg(ZYNQ_I2C_CR_OFFSET);
	ctrl_reg &= ~(0xC000 | 0x3F00);
	ctrl_reg |= ((div_a << 14) | (div_b << 8));
	zynq_i2c_writereg(ctrl_reg, ZYNQ_I2C_CR_OFFSET);

	return 0;
}

/**
 * zynq_i2c_clk_notifier_cb - Clock rate change callback
 * @nb:		Pointer to notifier block
 * @event:	Notification reason
 * @data:	Pointer to notification data object
 * Return: NOTIFY_STOP if the rate change should be aborted, NOTIFY_OK
 * otherwise.
 *
 * This function is called when the zynq_i2c input clock frequency changes.
 * In the pre-rate change notification here it is determined if the rate change
 * may be allowed or not.
 * In th post-change case necessary adjustments are conducted.
 */
static int zynq_i2c_clk_notifier_cb(struct notifier_block *nb, unsigned long
		event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct zynq_i2c *id = to_zynq_i2c(nb);

	if (id->suspended)
		return NOTIFY_OK;

	switch (event) {
	case PRE_RATE_CHANGE:
	{
		/*
		 * if a rate change is announced we need to check whether we can
		 * maintain the current frequency by changing the clock
		 * dividers. Probably we could also define an acceptable
		 * frequency range.
		 */
		unsigned long input_clk = ndata->new_rate;
		unsigned long fscl = id->i2c_clk;
		unsigned int div_a, div_b, err = 0;
		int ret;

		ret = zynq_i2c_calc_divs(&fscl, input_clk, &div_a, &div_b,
					 &err);
		if (ret)
			return NOTIFY_STOP;
		if (err > MAX_F_ERR)
			return NOTIFY_STOP;

		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		id->input_clk = ndata->new_rate;
		/* We probably need to stop the HW before this and restart
		 * afterwards */
		zynq_i2c_setclk(id->i2c_clk, id);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

#ifdef CONFIG_PM_SLEEP
/**
 * zynq_i2c_suspend - Suspend method for the driver
 * @_dev:	Address of the platform_device structure
 * Return: 0 on success and error value on error
 *
 * Put the driver into low power mode.
 */
static int zynq_i2c_suspend(struct device *_dev)
{
	struct platform_device *pdev = container_of(_dev,
			struct platform_device, dev);
	struct zynq_i2c *xi2c = platform_get_drvdata(pdev);

	clk_disable(xi2c->clk);
	xi2c->suspended = 1;

	return 0;
}

/**
 * zynq_i2c_resume - Resume from suspend
 * @_dev:	Address of the platform_device structure
 * Return: 0 on success and error value on error
 *
 * Resume operation after suspend.
 */
static int zynq_i2c_resume(struct device *_dev)
{
	struct platform_device *pdev = container_of(_dev,
			struct platform_device, dev);
	struct zynq_i2c *xi2c = platform_get_drvdata(pdev);
	int ret;

	ret = clk_enable(xi2c->clk);
	if (ret) {
		dev_err(_dev, "Cannot enable clock.\n");
		return ret;
	}

	xi2c->suspended = 0;

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(zynq_i2c_dev_pm_ops, zynq_i2c_suspend,
			 zynq_i2c_resume);

/************************/
/* Platform bus binding */
/************************/

/**
 * zynq_i2c_probe - Platform registration call
 * @pdev:	Handle to the platform device structure
 *
 * Return: zero on success, negative error otherwise
 *
 * This function does all the memory allocation and registration for the i2c
 * device. User can modify the address mode to 10 bit address mode using the
 * ioctl call with option I2C_TENBIT.
 */
static int zynq_i2c_probe(struct platform_device *pdev)
{
	struct resource *r_mem;
	struct zynq_i2c *id;
	int ret;
	/*
	 * Allocate memory for zynq_i2c structure.
	 * Initialize the structure to zero and set the platform data.
	 * Obtain the resource base address from platform data and remap it.
	 * Get the irq resource from platform data.Initialize the adapter
	 * structure members and also zynq_i2c structure.
	 */
	id = devm_kzalloc(&pdev->dev, sizeof(*id), GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	platform_set_drvdata(pdev, id);

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	id->membase = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(id->membase))
		return PTR_ERR(id->membase);

	id->irq = platform_get_irq(pdev, 0);

	id->adap.nr = pdev->id;
	id->adap.dev.of_node = pdev->dev.of_node;
	id->adap.algo = &zynq_i2c_algo;
	id->adap.timeout = 0x1F;	/* Default timeout value */
	id->adap.retries = 3;		/* Default retry value. */
	id->adap.algo_data = id;
	id->adap.dev.parent = &pdev->dev;
	snprintf(id->adap.name, sizeof(id->adap.name),
		 "Zynq I2C at %08lx", (unsigned long)r_mem->start);

	id->cur_timeout = id->adap.timeout;
	id->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(id->clk)) {
		dev_err(&pdev->dev, "input clock not found.\n");
		return PTR_ERR(id->clk);
	}
	ret = clk_prepare_enable(id->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}
	id->clk_rate_change_nb.notifier_call = zynq_i2c_clk_notifier_cb;
	id->clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(id->clk, &id->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");
	id->input_clk = clk_get_rate(id->clk);

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
			&id->i2c_clk);
	if (ret || (id->i2c_clk > ZYNQ_I2C_SPEED_MAX))
		id->i2c_clk = ZYNQ_I2C_SPEED_MAX;

	/*
	 * Set Master Mode,Normal addressing mode (7 bit address),
	 * Enable Transmission of Ack in Control Register.
	 * Set the timeout and I2C clock and request the IRQ(ISR mapped).
	 * Call to the i2c_add_numbered_adapter registers the adapter.
	 */
	zynq_i2c_writereg(0xE, ZYNQ_I2C_CR_OFFSET);
	zynq_i2c_writereg(id->adap.timeout, ZYNQ_I2C_TIME_OUT_OFFSET);

	ret = zynq_i2c_setclk(id->i2c_clk, id);
	if (ret) {
		dev_err(&pdev->dev, "invalid SCL clock: %u Hz\n", id->i2c_clk);
		ret = -EINVAL;
		goto err_clk_dis;
	}

	ret = devm_request_irq(&pdev->dev, id->irq, zynq_i2c_isr, 0,
				 DRIVER_NAME, id);
	if (ret) {
		dev_err(&pdev->dev, "cannot get irq %d\n", id->irq);
		goto err_clk_dis;
	}

	ret = i2c_add_numbered_adapter(&id->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "reg adap failed: %d\n", ret);
		goto err_clk_dis;
	}

	dev_info(&pdev->dev, "%u kHz mmio %08lx irq %d\n",
		 id->i2c_clk / 1000, (unsigned long)r_mem->start, id->irq);

	return 0;

err_clk_dis:
	clk_disable_unprepare(id->clk);
	return ret;
}

/**
 * zynq_i2c_remove - Unregister the device after releasing the resources
 * @pdev:	Handle to the platform device structure
 *
 * Return: zero always
 *
 * This function frees all the resources allocated to the device.
 */
static int zynq_i2c_remove(struct platform_device *pdev)
{
	struct zynq_i2c *id = platform_get_drvdata(pdev);

	i2c_del_adapter(&id->adap);
	clk_notifier_unregister(id->clk, &id->clk_rate_change_nb);
	clk_disable_unprepare(id->clk);

	return 0;
}

static const struct of_device_id zynq_i2c_of_match[] = {
	{ .compatible = "xlnx,ps7-i2c-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, zynq_i2c_of_match);

static struct platform_driver zynq_i2c_drv = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = zynq_i2c_of_match,
		.pm = &zynq_i2c_dev_pm_ops,
	},
	.probe  = zynq_i2c_probe,
	.remove = zynq_i2c_remove,
};

module_platform_driver(zynq_i2c_drv);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
