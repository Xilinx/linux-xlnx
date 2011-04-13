/*
 *
 * Xilinx PSS UART driver
 *
 * 2008 (c) Xilinx Inc.
 *
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */


#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>

#define XUARTPSS_NR_PORTS	2
#define XUARTPSS_FIFO_SIZE	16 /* FIFO size */
#define XUARTPSS_REGISTER_SPACE	0xFFF

#define xuartpss_readreg(offset)	__raw_readl(port->membase + offset)
#define xuartpss_writereg(val, offset) \
				__raw_writel(val, port->membase + offset)

/********************************Register Map********************************/

/** UART
 *
 * Register offsets for the UART.
 *
 */
#define XUARTPSS_CR_OFFSET	0x00  /* Control Register [8:0] */
#define XUARTPSS_MR_OFFSET	0x04  /* Mode Register [10:0] */
#define XUARTPSS_IER_OFFSET	0x08  /* Interrupt Enable [10:0] */
#define XUARTPSS_IDR_OFFSET	0x0C  /* Interrupt Disable [10:0] */
#define XUARTPSS_IMR_OFFSET	0x10  /* Interrupt Mask [10:0] */
#define XUARTPSS_ISR_OFFSET	0x14  /* Interrupt Status [10:0]*/
#define XUARTPSS_BAUDGEN_OFFSET	0x18  /* Baud Rate Generator [15:0] */
#define XUARTPSS_RXTOUT_OFFSET	0x1C  /* RX Timeout [7:0] */
#define XUARTPSS_RXWM_OFFSET	0x20  /* RX FIFO Trigger Level [5:0] */
#define XUARTPSS_MODEMCR_OFFSET	0x24  /* Modem Control [5:0] */
#define XUARTPSS_MODEMSR_OFFSET	0x28  /* Modem Status [8:0] */
#define XUARTPSS_SR_OFFSET	0x2C  /* Channel Status [11:0] */
#define XUARTPSS_FIFO_OFFSET	0x30  /* FIFO [15:0] or [7:0] */
#define XUARTPSS_BAUDDIV_OFFSET	0x34  /* Baud Rate Divider [7:0] */
#define XUARTPSS_FLOWDEL_OFFSET	0x38  /* Flow Delay [15:0] */
#define XUARTPSS_IRRX_PWIDTH_OFFSET 0x3C /* IR Minimum Received Pulse
						Width [15:0] */
#define XUARTPSS_IRTX_PWIDTH_OFFSET 0x40 /* IR Transmitted pulse
						Width [7:0] */
#define XUARTPSS_TXWM_OFFSET	0x44  /* TX FIFO Trigger Level [5:0] */

/** Control Register
 *
 * The Control register (CR) controls the major functions of the device.
 *
 * Control Register Bit Definitions
 */
#define XUARTPSS_CR_STOPBRK	0x00000100  /* Stop TX break */
#define XUARTPSS_CR_STARTBRK	0x00000080  /* Set TX break */
#define XUARTPSS_CR_TX_DIS	0x00000020  /* TX disabled. */
#define XUARTPSS_CR_TX_EN	0x00000010  /* TX enabled */
#define XUARTPSS_CR_RX_DIS	0x00000008  /* RX disabled. */
#define XUARTPSS_CR_RX_EN	0x00000004  /* RX enabled */
#define XUARTPSS_CR_TXRST	0x00000002  /* TX logic reset */
#define XUARTPSS_CR_RXRST	0x00000001  /* RX logic reset */
#define XUARTPSS_CR_RST_TO	0x00000040  /* Restart Timeout Counter */

/** Mode Register
 *
 * The mode register (MR) defines the mode of transfer as well as the data
 * format. If this register is modified during transmission or reception,
 * data validity cannot be guaranteed.
 *
 * Mode Register Bit Definitions
 *
 */
#define XUARTPSS_MR_CLKSEL		0x00000001  /* Pre-scalar selection */
#define XUARTPSS_MR_CHMODE_L_LOOP	0x00000200  /* Local loop back mode */
#define XUARTPSS_MR_CHMODE_NORM		0x00000000  /* Normal mode */

#define XUARTPSS_MR_STOPMODE_2_BIT	0x00000080  /* 2 stop bits */
#define XUARTPSS_MR_STOPMODE_1_BIT	0x00000000  /* 1 stop bit */

#define XUARTPSS_MR_PARITY_NONE		0x00000020  /* No parity mode */
#define XUARTPSS_MR_PARITY_MARK		0x00000018  /* Mark parity mode */
#define XUARTPSS_MR_PARITY_SPACE	0x00000010  /* Space parity mode */
#define XUARTPSS_MR_PARITY_ODD		0x00000008  /* Odd parity mode */
#define XUARTPSS_MR_PARITY_EVEN		0x00000000  /* Even parity mode */

#define XUARTPSS_MR_CHARLEN_6_BIT	0x00000006  /* 6 bits data */
#define XUARTPSS_MR_CHARLEN_7_BIT	0x00000004  /* 7 bits data */
#define XUARTPSS_MR_CHARLEN_8_BIT	0x00000000  /* 8 bits data */

/** Interrupt Registers
 *
 * Interrupt control logic uses the interrupt enable register (IER) and the
 * interrupt disable register (IDR) to set the value of the bits in the
 * interrupt mask register (IMR). The IMR determines whether to pass an
 * interrupt to the interrupt status register (ISR).
 * Writing a 1 to IER Enables an interrupt, writing a 1 to IDR disables an
 * interrupt. IMR and ISR are read only, and IER and IDR are write only.
 * Reading either IER or IDR returns 0x00.
 *
 * All four registers have the same bit definitions.
 */
#define XUARTPSS_IXR_TOUT	0x00000100 /* RX Timeout error interrupt */
#define XUARTPSS_IXR_PARITY	0x00000080 /* Parity error interrupt */
#define XUARTPSS_IXR_FRAMING	0x00000040 /* Framing error interrupt */
#define XUARTPSS_IXR_OVERRUN	0x00000020 /* Overrun error interrupt */
#define XUARTPSS_IXR_TXFULL	0x00000010 /* TX FIFO Full interrupt */
#define XUARTPSS_IXR_TXEMPTY	0x00000008 /* TX FIFO empty interrupt */
#define XUARTPSS_ISR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt */
#define XUARTPSS_IXR_RXTRIG	0x00000001 /* RX FIFO trigger interrupt */
#define XUARTPSS_IXR_RXFULL	0x00000004 /* RX FIFO full interrupt. */
#define XUARTPSS_IXR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt. */
#define XUARTPSS_IXR_MASK	0x00001FFF	/* Valid bit mask */


/** Channel Status Register
 *
 * The channel status register (CSR) is provided to enable the control logic
 * to monitor the status of bits in the channel interrupt status register,
 * even if these are masked out by the interrupt mask register.
 */
#define XUARTPSS_SR_RXEMPTY	0x00000002 /* RX FIFO empty */
#define XUARTPSS_SR_TXEMPTY	0x00000008 /* TX FIFO empty */
#define XUARTPSS_SR_TXFULL	0x00000010 /* TX FIFO full */
#define XUARTPSS_SR_RXTRIG	0x00000001 /* Rx Trigger */


/**
 * xuartpss_isr - Interrupt handler
 * @irq: Irq number
 * @dev_id: Id of the port
 *
 * Returns IRQHANDLED
 **/
static irqreturn_t xuartpss_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	struct tty_struct *tty = port->state->port.tty;
	unsigned long flags;
	unsigned int isrstatus, numbytes, iprstatus;
	static unsigned int data;
	char status = TTY_NORMAL;

	spin_lock_irqsave(&port->lock, flags);

	/* Read the interrupt status register to determine which
	 * interrupt(s) is/are active.
	 */
	isrstatus = xuartpss_readreg(XUARTPSS_ISR_OFFSET);
	iprstatus = isrstatus;
	isrstatus &= xuartpss_readreg(XUARTPSS_IMR_OFFSET);

	/* drop byte with parity error if IGNPAR specified */
	if (isrstatus & port->ignore_status_mask & XUARTPSS_IXR_PARITY)
		isrstatus &= ~(XUARTPSS_IXR_RXTRIG | XUARTPSS_IXR_TOUT);

	isrstatus &= port->read_status_mask;
	isrstatus &= ~port->ignore_status_mask;

	if (isrstatus & XUARTPSS_IXR_RXTRIG) {
		numbytes = port->fifosize - 2;
		/* Receive interrupt */
		while (numbytes--) {
			data = xuartpss_readreg(XUARTPSS_FIFO_OFFSET);
			port->icount.rx++;

			if (isrstatus & XUARTPSS_IXR_PARITY) {
				port->icount.parity++;
				status = TTY_PARITY;
			} else if (isrstatus & XUARTPSS_IXR_FRAMING) {
				port->icount.frame++;
				status = TTY_FRAME;
			} else if (isrstatus & XUARTPSS_IXR_OVERRUN)
				port->icount.overrun++;

			uart_insert_char(port, isrstatus, XUARTPSS_IXR_OVERRUN,
						data, status);
		}
		tty_flip_buffer_push(tty);
	}

	if (isrstatus & XUARTPSS_IXR_TOUT) {
		/* Receive Timeout Interrupt */
		while ((xuartpss_readreg(XUARTPSS_SR_OFFSET) &
			XUARTPSS_SR_RXEMPTY) != XUARTPSS_SR_RXEMPTY) {
			data = xuartpss_readreg(XUARTPSS_FIFO_OFFSET);
			port->icount.rx++;

			if (isrstatus & XUARTPSS_IXR_PARITY) {
				port->icount.parity++;
				status = TTY_PARITY;
			} else if (isrstatus & XUARTPSS_IXR_FRAMING) {
				port->icount.frame++;
				status = TTY_FRAME;
			} else if (isrstatus & XUARTPSS_IXR_OVERRUN)
				port->icount.overrun++;

			uart_insert_char(port, isrstatus, XUARTPSS_IXR_OVERRUN,
						data, status);
		}
		spin_unlock(&port->lock);
		tty_flip_buffer_push(tty);
		spin_lock(&port->lock);

	}

	/* Dispatch an appropriate handler */
	if ((isrstatus & XUARTPSS_IXR_TXEMPTY) == XUARTPSS_IXR_TXEMPTY) {
		if (uart_circ_empty(&port->state->xmit)) {
			xuartpss_writereg(XUARTPSS_IXR_TXEMPTY,
						XUARTPSS_IDR_OFFSET);
		} else {
			numbytes = port->fifosize;
			/* Break if no more data available in the UART buffer */
			while (numbytes--) {
				if (uart_circ_empty(&port->state->xmit))
					break;
				/* Get the data from the UART circular buffer
				 * and write it to the xuartpss's TX_FIFO
				 * register.
				 */
				xuartpss_writereg(
					port->state->xmit.buf[port->state->xmit.
					tail], XUARTPSS_FIFO_OFFSET);

				port->icount.tx++;

				/* Adjust the tail of the UART buffer and wrap
				 * the buffer if it reaches limit.
				 */
				port->state->xmit.tail =
					(port->state->xmit.tail + 1) & \
						(UART_XMIT_SIZE - 1);
			}

			if (uart_circ_chars_pending(
					&port->state->xmit) < WAKEUP_CHARS)
				uart_write_wakeup(port);
		}
	}

	xuartpss_writereg(isrstatus, XUARTPSS_ISR_OFFSET);
	spin_unlock_irqrestore(&port->lock, flags);
	return IRQ_HANDLED;
}

/**
 * xuartpss_set_baud_rate - Calculate and set the baud rate
 * @port: Handle to the uart port structure
 * @baud: Baud rate to set
 *
 **/
static void xuartpss_set_baud_rate(struct uart_port *port, unsigned int baud)
{
	unsigned int sel_clk;
	unsigned int calc_baud;
	unsigned int baud_percent_err = 3;
	unsigned int brgr_val, brdiv_val;
	unsigned int bauderror, percent_err;
	unsigned int best_brgr = 0, best_brdiv = 0;

	/* Formula to obtain baud rate is
	 *	baud_tx/rx rate = sel_clk/CD * (BDIV + 1)
	 *	input_clk = (Uart User Defined Clock or Apb Clock)
	 *		depends on UCLKEN in MR Reg
	 *	sel_clk = input_clk or input_clk/8;
	 *		depends on CLKS in MR reg
	 *	CD and BDIV depends on values in
	 *			baud rate generate register
	 *			baud rate clock divisor register
	 */
	sel_clk = port->uartclk;
	if (xuartpss_readreg(XUARTPSS_MR_OFFSET) & XUARTPSS_MR_CLKSEL)
		sel_clk = sel_clk/8;

	/* Check for the values of baud clk divisor value ranging from 4 to 255
	 * where the percent_err for the given baud rate is under 5.
	 */
	for (brdiv_val = 4; brdiv_val < 255; brdiv_val++) {

		brgr_val = sel_clk/(baud * (brdiv_val + 0x1));
		if (brgr_val < 2 || brgr_val > 65535)
			continue;

		calc_baud = sel_clk/(brgr_val * (brdiv_val + 0x1));

		if (baud > calc_baud)
			bauderror = baud - calc_baud;
		else
			bauderror = calc_baud - baud;

		percent_err = (bauderror * 100) / baud;

		if (percent_err < baud_percent_err) {
			best_brgr = brgr_val;
			best_brdiv = brdiv_val;
			break;
		}
	}

	/* Set the values for the new baud rate */
	xuartpss_writereg(best_brgr, XUARTPSS_BAUDGEN_OFFSET);
	xuartpss_writereg(best_brdiv, XUARTPSS_BAUDDIV_OFFSET);
}


/*----------------------Uart Operations---------------------------*/

/**
 * xuartpss_start_tx -  Start transmitting bytes
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_start_tx(struct uart_port *port)
{
	unsigned int status, numbytes = port->fifosize;

	if (uart_circ_empty(&port->state->xmit) || uart_tx_stopped(port))
		return;

	status = xuartpss_readreg(XUARTPSS_CR_OFFSET);
	/* Set the TX enable bit and clear the TX disable bit to enable the
	 * transmitter.
	 */
	xuartpss_writereg((status & ~XUARTPSS_CR_TX_DIS) | XUARTPSS_CR_TX_EN,
		XUARTPSS_CR_OFFSET);

	while (numbytes-- && ((xuartpss_readreg(XUARTPSS_SR_OFFSET)
		& XUARTPSS_SR_TXFULL)) != XUARTPSS_SR_TXFULL) {

		/* Break if no more data available in the UART buffer */
		if (uart_circ_empty(&port->state->xmit))
			break;

		/* Get the data from the UART circular buffer and
		 * write it to the xuartpss's TX_FIFO register.
		 */
		xuartpss_writereg(
			port->state->xmit.buf[port->state->xmit.tail],
			XUARTPSS_FIFO_OFFSET);
		port->icount.tx++;

		/* Adjust the tail of the UART buffer and wrap
		 * the buffer if it reaches limit.
		 */
		port->state->xmit.tail = (port->state->xmit.tail + 1) &
					(UART_XMIT_SIZE - 1);
	}

	/* Enable the TX Empty interrupt */
	xuartpss_writereg(XUARTPSS_IXR_TXEMPTY, XUARTPSS_IER_OFFSET);

	if (uart_circ_chars_pending(&port->state->xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

}

/**
 * xuartpss_stop_tx - Stop TX
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_stop_tx(struct uart_port *port)
{
	unsigned int regval;

	regval = xuartpss_readreg(XUARTPSS_CR_OFFSET);
	regval |= XUARTPSS_CR_TX_DIS;
	/* Disable the transmitter */
	xuartpss_writereg(regval, XUARTPSS_CR_OFFSET);
}

/**
 * xuartpss_stop_rx - Stop RX
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_stop_rx(struct uart_port *port)
{
	unsigned int regval;

	regval = xuartpss_readreg(XUARTPSS_CR_OFFSET);
	regval |= XUARTPSS_CR_RX_DIS;
	/* Disable the receiver */
	xuartpss_writereg(regval, XUARTPSS_CR_OFFSET);
}

/**
 * xuartpss_tx_empty -  Check whether TX is empty
 * @port: Handle to the uart port structure
 *
 * Returns TIOCSER_TEMT on success, 0 otherwise
 **/
static unsigned int xuartpss_tx_empty(struct uart_port *port)
{
	unsigned int status;

	status = xuartpss_readreg(XUARTPSS_ISR_OFFSET) & XUARTPSS_IXR_TXEMPTY;
	return status ? TIOCSER_TEMT : 0;

}


/**
 * xuartpss_break_ctl - Based on the input ctl we have to start or stop
 *			transmitting char breaks
 * @port: Handle to the uart port structure
 * @ctl: Value based on which start or stop decision is taken
 *
 **/
static void xuartpss_break_ctl(struct uart_port *port, int ctl)
{
	unsigned int status;

	status = xuartpss_readreg(XUARTPSS_CR_OFFSET);

	if (ctl == -1)
		xuartpss_writereg(XUARTPSS_CR_STARTBRK | status,
					XUARTPSS_CR_OFFSET);
	else {
		if ((status & XUARTPSS_CR_STOPBRK) == 0)
			xuartpss_writereg(XUARTPSS_CR_STOPBRK | status,
					 XUARTPSS_CR_OFFSET);
	}
}

/**
 * xuartpss_set_termios - termios operations, handling data length, parity,
 *				stop bits, flow control, baud rate
 * @port: Handle to the uart port structure
 * @termios: Handle to the input termios structure
 * @old: Values of the previously saved termios structure
 *
 **/
static void xuartpss_set_termios(struct uart_port *port,
				struct ktermios *termios, struct ktermios *old)
{
	unsigned int cval = 0;
	unsigned int baud;
	unsigned long flags;
	unsigned int ctrl_reg, mode_reg;

	spin_lock_irqsave(&port->lock, flags);

	while ((xuartpss_readreg(XUARTPSS_SR_OFFSET) &
		 XUARTPSS_SR_TXEMPTY) != XUARTPSS_SR_TXEMPTY) {
		}

	while ((xuartpss_readreg(XUARTPSS_SR_OFFSET) &
		 XUARTPSS_SR_RXEMPTY) != XUARTPSS_SR_RXEMPTY) {
		xuartpss_readreg(XUARTPSS_FIFO_OFFSET);
	}

	/* Disable the TX and RX to set baud rate */
	xuartpss_writereg(xuartpss_readreg(XUARTPSS_CR_OFFSET) |
			(XUARTPSS_CR_TX_DIS | XUARTPSS_CR_RX_DIS),
			XUARTPSS_CR_OFFSET);

	/* Min baud rate = 6bps and Max Baud Rate is 10Mbps for 100Mhz clk */
	baud = uart_get_baud_rate(port, termios, old, 0, 460800);
	xuartpss_set_baud_rate(port, baud);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* Set TX/RX Reset */
	xuartpss_writereg(xuartpss_readreg(XUARTPSS_CR_OFFSET) |
			(XUARTPSS_CR_TXRST | XUARTPSS_CR_RXRST),
			XUARTPSS_CR_OFFSET);

	/* Wait until TX and RX reset is done */
	while ((xuartpss_readreg(XUARTPSS_CR_OFFSET) &
			 (XUARTPSS_CR_TXRST | XUARTPSS_CR_RXRST))) {
		}

	ctrl_reg = xuartpss_readreg(XUARTPSS_CR_OFFSET);

	/* Clear the RX disable and TX disable bits and then set the TX enable
	 * bit and RX enable bit to enable the transmitter and receiver.
	 */
	xuartpss_writereg(
		(ctrl_reg & ~(XUARTPSS_CR_TX_DIS | XUARTPSS_CR_RX_DIS))
			| (XUARTPSS_CR_TX_EN | XUARTPSS_CR_RX_EN),
			XUARTPSS_CR_OFFSET);

	xuartpss_writereg(10, XUARTPSS_RXTOUT_OFFSET);

	port->read_status_mask = XUARTPSS_IXR_TXEMPTY | XUARTPSS_IXR_RXTRIG |
			XUARTPSS_IXR_OVERRUN | XUARTPSS_IXR_TOUT;
	port->ignore_status_mask = 0;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |= XUARTPSS_IXR_PARITY |
		XUARTPSS_IXR_FRAMING;

	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= XUARTPSS_IXR_PARITY |
			XUARTPSS_IXR_FRAMING | XUARTPSS_IXR_OVERRUN;

	/* ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= XUARTPSS_IXR_RXTRIG |
			XUARTPSS_IXR_TOUT | XUARTPSS_IXR_PARITY |
			XUARTPSS_IXR_FRAMING | XUARTPSS_IXR_OVERRUN;

	mode_reg = xuartpss_readreg(XUARTPSS_MR_OFFSET);

	/* Handling Data Size */
	switch (termios->c_cflag & CSIZE) {
	case CS6:
		cval |= XUARTPSS_MR_CHARLEN_6_BIT;
		break;
	case CS7:
		cval |= XUARTPSS_MR_CHARLEN_7_BIT;
		break;
	default:
	case CS8:
		cval |= XUARTPSS_MR_CHARLEN_8_BIT;
		break;
	}

	/* Handling Parity and Stop Bits length */
	if (termios->c_cflag & CSTOPB)
		cval |= XUARTPSS_MR_STOPMODE_2_BIT; /* 2 STOP bits */
	else
		cval |= XUARTPSS_MR_STOPMODE_1_BIT; /* 1 STOP bit */

	if (termios->c_cflag & PARENB) {
		/* Mark or Space parity */
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				cval |= XUARTPSS_MR_PARITY_MARK;
			else
				cval |= XUARTPSS_MR_PARITY_SPACE;
		} else if (termios->c_cflag & PARODD)
				cval |= XUARTPSS_MR_PARITY_ODD;
			else
				cval |= XUARTPSS_MR_PARITY_EVEN;
	} else
		cval |= XUARTPSS_MR_PARITY_NONE;
	xuartpss_writereg(cval , XUARTPSS_MR_OFFSET);

	spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * xuartpss_startup - Called when an application opens a xuartpss port
 * @port: Handle to the uart port structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int xuartpss_startup(struct uart_port *port)
{
	unsigned int retval = 0, status = 0;

	/* Request IRQ */
	retval = request_irq(port->irq, xuartpss_isr, 0, "xuartpss",
								(void *)port);
	if (retval)
		return retval;

	/* Disable the TX and RX to set baud rate */
	xuartpss_writereg(XUARTPSS_CR_TX_DIS | XUARTPSS_CR_RX_DIS,
						XUARTPSS_CR_OFFSET);

	/* Set the initial baud rate to 9600 */
	xuartpss_set_baud_rate(port, 9600);

	/* Set the Control Register with TX/RX Enable, TX/RX Reset,
	 * no break chars.
	 */
	xuartpss_writereg(XUARTPSS_CR_TXRST | XUARTPSS_CR_RXRST,
				XUARTPSS_CR_OFFSET);

	/* Wait until TX and RX reset is done */
	while ((xuartpss_readreg(XUARTPSS_CR_OFFSET)
		& (XUARTPSS_CR_TXRST | XUARTPSS_CR_RXRST))) {
		}

	status = xuartpss_readreg(XUARTPSS_CR_OFFSET);

	/* Clear the RX disable and TX disable bits and then set the TX enable
	 * bit and RX enable bit to enable the transmitter and receiver.
	 */
	xuartpss_writereg((status & ~(XUARTPSS_CR_TX_DIS | XUARTPSS_CR_RX_DIS))
			| (XUARTPSS_CR_TX_EN | XUARTPSS_CR_RX_EN |
			XUARTPSS_CR_STOPBRK), XUARTPSS_CR_OFFSET);

	/* Set the Mode Register with normal mode,8 data bits,1 stop bit,
	 * no parity.
	 */
	xuartpss_writereg(XUARTPSS_MR_CHMODE_NORM | XUARTPSS_MR_STOPMODE_1_BIT
		| XUARTPSS_MR_PARITY_NONE | XUARTPSS_MR_CHARLEN_8_BIT,
		 XUARTPSS_MR_OFFSET);

	/* Set the RX FIFO Trigger level to 14 assuming FIFO size as 16 */
	xuartpss_writereg(14, XUARTPSS_RXWM_OFFSET);

	/* Receive Timeout register is enabled with value of 10 */
	xuartpss_writereg(10, XUARTPSS_RXTOUT_OFFSET);


	/* Set the Interrupt Mask Register with desired interrupts */
	xuartpss_writereg(XUARTPSS_IXR_TXEMPTY | XUARTPSS_IXR_PARITY |
		XUARTPSS_IXR_FRAMING | XUARTPSS_IXR_OVERRUN |
		XUARTPSS_IXR_RXTRIG | XUARTPSS_IXR_TOUT, XUARTPSS_IER_OFFSET);
	/* Set the Interrupt Mask Register with desired interrupts */
	xuartpss_writereg(~(XUARTPSS_IXR_TXEMPTY | XUARTPSS_IXR_PARITY |
		XUARTPSS_IXR_FRAMING | XUARTPSS_IXR_OVERRUN |
		XUARTPSS_IXR_RXTRIG | XUARTPSS_IXR_TOUT), XUARTPSS_IDR_OFFSET);

	return retval;
}

/**
 * xuartpss_shutdown - Called when an application closes a xuartpss port
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_shutdown(struct uart_port *port)
{
	int status;

	/* Read the IMR Register and write the same to IDR Register.
	 * Disable interrupts by writing to appropriate registers.
	 */
	status = xuartpss_readreg(XUARTPSS_IMR_OFFSET);
	xuartpss_writereg(status, XUARTPSS_IDR_OFFSET);

	/* Disable the TX and RX */
	xuartpss_writereg(XUARTPSS_CR_TX_DIS | XUARTPSS_CR_RX_DIS,
				 XUARTPSS_CR_OFFSET);
	/* Free IRQ */
	free_irq(port->irq, port);
}

/**
 * xuartpss_type - Set UART type to xuartpss port
 * @port: Handle to the uart port structure
 *
 * Returns string on success, NULL otherwise
 **/
static const char *xuartpss_type(struct uart_port *port)
{
	return port->type == PORT_XUARTPSS ? "xuartpss" : NULL;
}

/**
 * xuartpss_verify_port - Verify the port params
 * @port: Handle to the uart port structure
 * @ser: Handle to the structure whose members are compared
 *
 * Returns 0 if success otherwise -EINVAL
 **/
static int xuartpss_verify_port(struct uart_port *port,
					struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_XUARTPSS)
		return -EINVAL;
	if (port->irq != ser->irq)
		return -EINVAL;
	if (ser->io_type != UPIO_MEM)
		return -EINVAL;
	if (port->iobase != ser->port)
		return -EINVAL;
	if (ser->hub6 != 0)
		return -EINVAL;
	return 0;
}

/**
 * xuartpss_request_port - Claim the memory region attached to xuartpss port,
 *			   called when the driver adds a xuartpss port via
 *			   uart_add_one_port()
 * @port: Handle to the uart port structure
 *
 * Returns 0, -ENOMEM if request fails
 **/
static int xuartpss_request_port(struct uart_port *port)
{
	if (!request_mem_region(port->mapbase, XUARTPSS_REGISTER_SPACE,
					 "xuartpss")) {
		return -ENOMEM;
	}

	port->membase = ioremap(port->mapbase, XUARTPSS_REGISTER_SPACE);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, XUARTPSS_REGISTER_SPACE);
		return -ENOMEM;
	}
	return 0;
}

/**
 * xuartpss_release_port - Release the memory region attached to a xuartpss
 * 			   port, called when the driver removes a xuartpss port
 * 			   via uart_remove_one_port().
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, XUARTPSS_REGISTER_SPACE);
	iounmap(port->membase);
	port->membase = NULL;
}

/**
 * xuartpss_config_port - Configure xuartpss, called when the driver adds a
 *				xuartpss port
 * @port: Handle to the uart port structure
 * @flags: If any
 *
 **/
static void xuartpss_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && xuartpss_request_port(port) == 0)
		port->type = PORT_XUARTPSS;

}
static unsigned int xuartpss_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void xuartpss_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* N/A */
}

static void xuartpss_enable_ms(struct uart_port *port)
{
	/* N/A */
}

/** The UART operations structure
 */
static struct uart_ops xuartpss_ops = {
	.set_mctrl	= xuartpss_set_mctrl,
	.get_mctrl	= xuartpss_get_mctrl,
	.enable_ms	= xuartpss_enable_ms,

	.start_tx	= xuartpss_start_tx,	/* Start transmitting */
	.stop_tx	= xuartpss_stop_tx,	/* Stop transmission */
	.stop_rx	= xuartpss_stop_rx,	/* Stop reception */
	.tx_empty	= xuartpss_tx_empty,	/* Transmitter busy? */
	.break_ctl	= xuartpss_break_ctl,	/* Start/stop
						 * transmitting break
						 */
	.set_termios	= xuartpss_set_termios,	/* Set termios */
	.startup	= xuartpss_startup,	/* App opens xuartpss */
	.shutdown	= xuartpss_shutdown,	/* App closes xuartpss */
	.type		= xuartpss_type,		/* Set UART type */
	.verify_port	= xuartpss_verify_port,	/* Verification of port
						 * params
						 */
	.request_port	= xuartpss_request_port,/* Claim resources
						 * associated with a
						 * xuartpss port
						 */
	.release_port	= xuartpss_release_port,/* Release resources
						 * associated with a
						 * xuartpss port
						 */
	.config_port	= xuartpss_config_port,	/* Configure when driver
						 * adds a xuartpss port
						 */
};

static struct uart_port xuartpss_port[2];


/**
 * xuartpss_get_port - Configure the port from the platform device resource
 * 			info
 * @pdev: Pointer to platfrom device structure
 * @id: Requested id number
 *
 **/
static struct uart_port *xuartpss_get_port(int id)
{
	struct uart_port *port;

	if (id == -1) {
		for (id = 0; id < XUARTPSS_NR_PORTS; id++)
			if (xuartpss_port[id].mapbase == 0)
				break;
	}

	if (id < 0 || id >= XUARTPSS_NR_PORTS) {
		printk(KERN_WARNING "xuartpss: invalid id: %i\n", id);
		return NULL;
	}

	port = &xuartpss_port[id];

	/* Is the structure is already initialized? */
	if (port->mapbase)
		return port;

	/* At this point, we've got an empty uart_port struct, initialize it */
	spin_lock_init(&port->lock);
	port->membase	= NULL;
	port->iobase	= 1; /* mark port in use */
	port->irq	= NO_IRQ;
	port->type	= PORT_UNKNOWN;
	port->iotype	= UPIO_MEM32;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &xuartpss_ops;
	port->fifosize	= XUARTPSS_FIFO_SIZE;
	port->line	= id;
	port->dev	= NULL;
	return port;
}

/*-----------------------Console driver operations--------------------------*/

#ifdef CONFIG_SERIAL_XILINX_PSS_UART_CONSOLE
/**
 * xuartpss_console_wait_tx - Wait for the TX to be full
 * @port: Handle to the uart port structure
 *
 **/
static void xuartpss_console_wait_tx(struct uart_port *port)
{
	while ((xuartpss_readreg(XUARTPSS_SR_OFFSET) & XUARTPSS_SR_TXEMPTY)
				!= XUARTPSS_SR_TXEMPTY)
		barrier();
}

/**
 * xuartpss_console_putchar - write the character to the FIFO buffer
 * @port: Handle to the uart port structure
 * @ch: Character to be written
 *
 **/
static void xuartpss_console_putchar(struct uart_port *port, int ch)
{
	xuartpss_console_wait_tx(port);
	xuartpss_writereg(ch, XUARTPSS_FIFO_OFFSET);
}

/**
 * xuartpss_console_write - perform write operation
 * @port: Handle to the uart port structure
 * @s: Pointer to character array
 * @count: No of characters
 **/
static void xuartpss_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = &xuartpss_port[co->index];
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	uart_console_write(port, s, count, xuartpss_console_putchar);
	xuartpss_console_wait_tx(port);

	spin_unlock_irqrestore(&port->lock, flags);

}

/**
 * xuartpss_console_setup - Initialize the uart to default config
 * @co: Console handle
 * @options: Initial settings of uart
 *
 * Returns 0, -ENODEV if no device
 **/
static int __init xuartpss_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &xuartpss_port[co->index];
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= XUARTPSS_NR_PORTS)
		return -EINVAL;

	if (!port->mapbase) {
		pr_debug("console on ttyPSS%i not present\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver xuartpss_uart_driver;

static struct console xuartpss_console = {
	.name	= "ttyDF",
	.write	= xuartpss_console_write,
	.device	= uart_console_device,
	.setup	= xuartpss_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1, /* Specified on the cmdline (e.g. console=ttyPSS ) */
	.data	= &xuartpss_uart_driver,
};

/**
 * xuartpss_console_init - Initialization call
 *
 * Returns 0 on success, negative error otherwise
 **/
static int __init xuartpss_console_init(void)
{
	register_console(&xuartpss_console);
	return 0;
}

/* console_initcall(xuartpss_console_init);*/

#endif /* CONFIG_SERIAL_XILINX_PSS_UART_CONSOLE */

/** Structure Definitions
 */
static struct uart_driver xuartpss_uart_driver = {
	.owner		= THIS_MODULE,		/* Owner */
	.driver_name	= "xuartpss",		/* Driver name */
	.dev_name	= "ttyDF",		/* Node name */
	.major		= 204,			/* Major number */
	.minor		= 100,			/* Minor number */
	.nr		= XUARTPSS_NR_PORTS,	/* Number of UART ports */
#ifdef CONFIG_SERIAL_XILINX_PSS_UART_CONSOLE
	.cons		= &xuartpss_console,	/* Console */
#endif
};

/**
 * xuartpss_get_data - Get the clock and ID data from platform data 
 *			or the device tree when OF is used 
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int __devinit xuartpss_get_data(struct platform_device *pdev, 
					int *clk, int *id)
{
	/* handle the platform specific data based on platform bus
	   or device tree depending on how the kernel is configured,
	   the address and irq info are handled automatically
	*/
#ifndef CONFIG_OF
	*clk = *((unsigned int *)(pdev->dev.platform_data));
	*id = pdev->id;
#else
	const unsigned int *temp_ptr = 0;

	temp_ptr = of_get_property(pdev->dev.of_node, "clock", NULL);
	if (!temp_ptr) {
		dev_err(&pdev->dev, "no clock specified\n");		
		return -ENODEV;
	} else {
		*clk = be32_to_cpup(temp_ptr);
	}				

	temp_ptr = of_get_property(pdev->dev.of_node, "port-number", NULL);
	if (!temp_ptr) {
		dev_err(&pdev->dev, "no port-number specified\n");		
		return -ENODEV;
	} else {
		*id = be32_to_cpup(temp_ptr);
	}
#endif
	return 0;
}

/* ---------------------------------------------------------------------
 * Platform bus binding
 */
/**
 * xuartpss_probe - Platform driver probe
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int __devinit xuartpss_probe(struct platform_device *pdev)
{
	int rc;
	struct uart_port *port;
	struct resource *res, *res2;
	int id, clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 	if (!res)
		return -ENODEV;

	res2 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res2)
		return -ENODEV;

	rc = xuartpss_get_data(pdev, &clk, &id);
	if (rc)	return rc;

	/* Initialize the port structure */
	port = xuartpss_get_port(id);
	if (!port) {
		dev_err(&pdev->dev, "Cannot get uart_port structure\n");
		return -ENODEV;
	} else {
		/* Register the port.
		 * This function also registers this device with the tty layer
		 * and triggers invocation of the config_port() entry point.
		 */
		port->mapbase = res->start;
		port->irq = res2->start;
		port->dev = &pdev->dev;
		port->uartclk = clk;
		dev_set_drvdata(&pdev->dev, port);
		rc = uart_add_one_port(&xuartpss_uart_driver, port);
		if (rc) {
			dev_err(&pdev->dev, "uart_add_one_port() failed; \
						err=%i\n", rc);
			dev_set_drvdata(&pdev->dev, NULL);
			return rc;
		}
		return 0;
	}
}

/**
 * xuartpss_remove - called when the platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int __devexit xuartpss_remove(struct platform_device *pdev)
{
	struct uart_port *port = dev_get_drvdata(&pdev->dev);
	int rc = 0;

	/* Remove the xuartpss port from the serial core */
	if (port) {
		rc = uart_remove_one_port(&xuartpss_uart_driver, port);
		dev_set_drvdata(&pdev->dev, NULL);
		port->mapbase = 0;
	}
	return rc;
}

/**
 * xuartpss_suspend - suspend event
 * @pdev: Pointer to the platform device structure
 * @state: State of the device
 *
 * Returns 0
 **/
static int xuartpss_suspend(struct platform_device *pdev, pm_message_t state)
{
	/* Call the API provided in serial_core.c file which handles
	 * the suspend.
	 */
	uart_suspend_port(&xuartpss_uart_driver, &xuartpss_port[pdev->id]);
	return 0;
}

/**
 * xuartpss_resume - Resume after a previous suspend
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0
 **/
static int xuartpss_resume(struct platform_device *pdev)
{
	uart_resume_port(&xuartpss_uart_driver, &xuartpss_port[pdev->id]);
	return 0;
}

/* Match table for of_platform binding */

#ifdef CONFIG_OF
static struct of_device_id xuartpss_of_match[] __devinitdata = {
	{ .compatible = "xlnx,xuartpss", },
	{}
};
MODULE_DEVICE_TABLE(of, xuartpss_of_match);
#endif

static struct platform_driver xuartpss_platform_driver = {
	.probe   = xuartpss_probe,		/* Probe method */
	.remove  = __exit_p(xuartpss_remove),	/* Detach method */
	.suspend = xuartpss_suspend,		/* Suspend */
	.resume  = xuartpss_resume,		/* Resume after a suspend */
	.driver  = {
		.owner = THIS_MODULE,
		.name = "xuartpss",		/* Driver name */
#ifdef CONFIG_OF
		.of_match_table = xuartpss_of_match,
#endif
		},
};


/* ---------------------------------------------------------------------
 * Module Init and Exit
 */
/**
 * xuartpss_init - Initial driver registration call
 *
 * Returns whether the registration was successful or not
 **/
static int __init xuartpss_init(void)
{
	int retval = 0;

	/* Register the xuartpss driver with the serial core */
	retval = uart_register_driver(&xuartpss_uart_driver);
	if (retval)
		return retval;

	/* Register the platform driver */
	retval = platform_driver_register(&xuartpss_platform_driver);
	if (retval)
		uart_unregister_driver(&xuartpss_uart_driver);

	return retval;
}

/**
 * xuartpss_exit - Driver unregistration call
 **/
static void __exit xuartpss_exit(void)
{
	/* The order of unregistration is important. Unregister the
	 * UART driver before the platform driver crashes the system.
	 */

	/* Unregister the platform driver */
	platform_driver_unregister(&xuartpss_platform_driver);

	/* Unregister the xuartpss driver */
	uart_unregister_driver(&xuartpss_uart_driver);
}

module_init(xuartpss_init);
module_exit(xuartpss_exit);

MODULE_DESCRIPTION("Driver for PSS UART");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL");




