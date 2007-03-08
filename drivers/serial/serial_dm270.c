/*
 *  linux/drivers/serial/serial_dm270.c
 *
 *  Driver for DM270 serial ports
 *
 *  Copyright (c) 2004	Chee Tim Loh <lohct@pacific.net.sg>
 *
 *  Based on drivers/char/serial_s3c4510b.c
 *  Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 *  Curt Brune <curt@cucy.com>
 *
 *  Based on drivers/char/serial_amba.c
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/arch/hardware.h>

#define UART_NR			2
#define UART_DRIVER_NAME	"TI TMS320DM270 Internal UART"
#define UART_TYPE		"DM270_UART"
#define UART_DEFAULT_BAUD	38400
#define UART_ISR_PASS_LIMIT	256

#define CONSOLE_DEFAULT_BAUD	38400	/* 38,400 buad */
#define CONSOLE_DEFAULT_BITS	8	/* 8 data bits */
#define CONSOLE_DEFAULT_PARITY	'n'	/* no parity */
#define CONSOLE_DEFAULT_FLOW	'n'	/* no flow control */

struct dm270_uart_port {
	struct uart_port	uport;
	unsigned short		msr;
/*	unsigned short		dtrr_break_flag;*/
};

static void dm270_uart_stop_tx(struct uart_port *uport, unsigned int tty_stop);

/**
 **
 ** Platform specific functions
 ** Note: These definitions and routines will surely need to change if porting
 **       this driver to another platform.
 **
 **/

static inline unsigned short
dm270_uart_hwin(struct uart_port *uport, unsigned long offset)
{
	return inw(uport->iobase + offset);
}

static inline void
dm270_uart_hwout(struct uart_port *uport, unsigned long offset, unsigned short value)
{
	outw(value, uport->iobase + offset);
}

static void
dm270_uart_hwreset(struct uart_port *uport)
{
	unsigned short reg;

	/* Disable clock to UART. */
	reg = inw(DM270_CLKC_MOD2);
	outw((reg & ~(DM270_CLKC_MOD2_CUAT << uport->line)), DM270_CLKC_MOD2);

	/* Select CLK_ARM as UART clock source */
	reg = inw(DM270_CLKC_CLKC);
	outw((reg & ~(DM270_CLKC_CLKC_CUAS << uport->line)), DM270_CLKC_CLKC);

	/* Enable clock to UART. */
	reg = inw(DM270_CLKC_MOD2);
	outw((reg | (DM270_CLKC_MOD2_CUAT << uport->line)), DM270_CLKC_MOD2);

	if (uport->line == 1) {
#ifndef CONFIG_SERIAL_DM270_BOOT_CTRL_UART1
		/* Configure GIO23 & GIO24 as RXD1 & TXD1 respectively */
		reg = inw(DM270_GIO_FSEL0);
		outw(reg | DM270_GIO_FSEL_RXD1, DM270_GIO_FSEL0);
		reg = inw(DM270_GIO_FSEL1);
		outw(reg | DM270_GIO_FSEL_TXD1, DM270_GIO_FSEL1);
#endif
	}
}

static inline void
dm270_uart_disable_tx_int(struct uart_port *uport)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr &= ~DM270_UART_MSR_TFTIE;
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_disable_rx_int(struct uart_port *uport)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr &= ~(DM270_UART_MSR_TOIC_MASK | DM270_UART_MSR_REIE |
			DM270_UART_MSR_RFTIE);
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_enable_tx_int(struct uart_port *uport)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr |= DM270_UART_MSR_TFTIE;
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_enable_rx_int(struct uart_port *uport)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr = (dm270_uport->msr & ~DM270_UART_MSR_TOIC_MASK) |
			(DM270_UART_MSR_TIMEOUT_7 | DM270_UART_MSR_REIE |
			 DM270_UART_MSR_RFTIE);
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_disable_ints(struct uart_port *uport, unsigned short *msr)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	*msr = dm270_uport->msr & (DM270_UART_MSR_TOIC_MASK | DM270_UART_MSR_REIE |
			DM270_UART_MSR_TFTIE | DM270_UART_MSR_RFTIE);
	dm270_uport->msr &= ~(DM270_UART_MSR_TOIC_MASK | DM270_UART_MSR_REIE |
			DM270_UART_MSR_TFTIE | DM270_UART_MSR_RFTIE);
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_restore_ints(struct uart_port *uport, unsigned short msr)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr = (dm270_uport->msr & ~(DM270_UART_MSR_TOIC_MASK | DM270_UART_MSR_REIE |
			DM270_UART_MSR_TFTIE | DM270_UART_MSR_RFTIE)) |
			(msr & (DM270_UART_MSR_TOIC_MASK | DM270_UART_MSR_REIE |
			DM270_UART_MSR_TFTIE | DM270_UART_MSR_RFTIE));
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_clear_fifos(struct uart_port *uport)
{
	dm270_uart_hwout(uport, DM270_UART_TFCR,
			dm270_uart_hwin(uport, DM270_UART_TFCR) | DM270_UART_TFCR_CLEAR);
	dm270_uart_hwout(uport, DM270_UART_RFCR,
			dm270_uart_hwin(uport, DM270_UART_RFCR) |
			(DM270_UART_RFCR_RESET | DM270_UART_RFCR_CLEAR));
}

static inline void
dm270_uart_disable_breaks(struct uart_port *uport)
{
	dm270_uart_hwout(uport, DM270_UART_LCR,
			dm270_uart_hwin(uport, DM270_UART_LCR) & ~DM270_UART_LCR_BOC);
}

static inline void
dm270_uart_enable_breaks(struct uart_port *uport)
{
	dm270_uart_hwout(uport, DM270_UART_LCR,
			dm270_uart_hwin(uport, DM270_UART_LCR) | DM270_UART_LCR_BOC);
}

static inline void
dm270_uart_set_rate(struct uart_port *uport, unsigned int rate)
{
	dm270_uart_hwout(uport, DM270_UART_BRSR, DM270_UART_BRSR_VAL(rate));
}

static inline void
dm270_uart_set_mode(struct uart_port *uport, unsigned short mode)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr = (dm270_uport->msr & ~(DM270_UART_MSR_CLS | DM270_UART_MSR_SBLS |
			DM270_UART_MSR_PSB | DM270_UART_MSR_PEB)) | mode;
	dm270_uart_hwout(uport, DM270_UART_MSR, dm270_uport->msr);
}

static inline void
dm270_uart_set_rx_trigger(struct uart_port *uport, unsigned short val)
{
	dm270_uart_hwout(uport, DM270_UART_RFCR, (dm270_uart_hwin(uport, DM270_UART_RFCR) &
			~(DM270_UART_RFCR_RTL_MASK | DM270_UART_RFCR_RESET |
			DM270_UART_RFCR_CLEAR)) | val);
}

static inline void
dm270_uart_set_tx_trigger(struct uart_port *uport, unsigned short val)
{
	dm270_uart_hwout(uport, DM270_UART_TFCR, (dm270_uart_hwin(uport, DM270_UART_TFCR) &
			~(DM270_UART_TFCR_TTL_MASK | DM270_UART_TFCR_CLEAR)) |
			val);
}

static inline void
dm270_uart_char_out(struct uart_port *uport, u8 val)
{
	dm270_uart_hwout(uport, DM270_UART_DTRR, val);
}

static inline unsigned char
dm270_uart_char_in(struct uart_port *uport, unsigned short *status)
{
	unsigned short dtrr;

	dtrr = dm270_uart_hwin(uport, DM270_UART_DTRR);
	*status = (dtrr & 0xff00);
	return ((unsigned char)(dtrr & 0x00ff));
}

static inline int
dm270_uart_error_condition(unsigned short status)
{
	return ((status ^ DM270_UART_DTRR_RVF) &
			(DM270_UART_DTRR_RVF | DM270_UART_DTRR_BF | DM270_UART_DTRR_FE |
			 DM270_UART_DTRR_ORF | DM270_UART_DTRR_PEF));
}

static inline int
dm270_uart_break_condition(unsigned short status)
{
	return (status & DM270_UART_DTRR_BF);
}

static inline int
dm270_uart_parity_error(unsigned short status)
{
	return (status & DM270_UART_DTRR_PEF);
}

static inline int
dm270_uart_framing_error(unsigned short status)
{
	return (status & DM270_UART_DTRR_FE);
}

static inline int
dm270_uart_overrun_error(unsigned short status)
{
	return (status & DM270_UART_DTRR_ORF);
}

static inline int
dm270_uart_received_word_invalid(unsigned short status)
{
	return (status & DM270_UART_DTRR_RVF);
}

static inline unsigned short
dm270_uart_tx_fifo_empty(struct uart_port *uport)
{
	return (dm270_uart_hwin(uport, DM270_UART_SR) & DM270_UART_SR_TREF);
}

static inline unsigned short
dm270_uart_room_in_tx_fifo(struct uart_port *uport)
{
	return ((dm270_uart_hwin(uport, DM270_UART_TFCR) & DM270_UART_TFCR_TWC_MASK) < DM270_UART_TXFIFO_BYTESIZE);
}

static inline unsigned short
dm270_uart_rx_fifo_has_content(struct uart_port *uport)
{
	return (dm270_uart_hwin(uport, DM270_UART_SR) & DM270_UART_SR_RFNEF);
}

static inline void
dm270_uart_save_registers(struct uart_port *uport)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	dm270_uport->msr = dm270_uart_hwin(uport, DM270_UART_MSR);
}

/**
 **
 ** interrupt functions
 **
 **/

static inline void
dm270_uart_rx_chars(struct uart_port *uport, struct pt_regs *ptregs)
{
	struct tty_struct *tty = uport->info->tty;
	unsigned short status;
	unsigned char ch;
	int max_count = 256;

	do {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;

		ch = dm270_uart_char_in(uport, &status);
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		uport->icount.rx++;

		if (dm270_uart_error_condition(status)) {
			/* For statistics only */
			if (dm270_uart_break_condition(status)) {
				status &= ~(DM270_UART_DTRR_FE | DM270_UART_DTRR_PEF);
				uport->icount.brk++;
				/* We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(uport))
					goto ignore_char;
			} else if (dm270_uart_parity_error(status)) {
				uport->icount.parity++;
			} else if (dm270_uart_framing_error(status)) {
				uport->icount.frame++;
			}
			if (dm270_uart_overrun_error(status))
				uport->icount.overrun++;

			/* Now check to see if character should be
			 * ignored, and mask off conditions which
			 * should be ignored.
			 */
			status &= uport->read_status_mask;
#if 0
#ifdef CONFIG_SERIAL_DM270_CONSOLE
			if (uport->line == uport->cons->index) {
				/* Recover the break flag from console xmit */
				status |= dm270_uport->dtrr_break_flag;
				dm270_uport->dtrr_break_flag = 0;
			}
#endif
#endif
			if (dm270_uart_break_condition(status)) {
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			} else if (dm270_uart_parity_error(status)) {
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			} else if (dm270_uart_framing_error(status)) {
				*tty->flip.flag_buf_ptr = TTY_FRAME;
			}
		}
		if (uart_handle_sysrq_char(uport, ch, ptregs))
			goto ignore_char;
		if ((status & uport->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if (dm270_uart_overrun_error(status) &&
				(tty->flip.count < TTY_FLIPBUF_SIZE)) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character
			 */
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
	ignore_char:
	} while (dm270_uart_rx_fifo_has_content(uport) && (max_count-- > 0));

	tty_flip_buffer_push(tty);
}

static inline void
dm270_uart_tx_chars(struct uart_port *uport)
{
	struct circ_buf *xmit = &uport->info->xmit;
	int count;

	if (uport->x_char) {
		dm270_uart_char_out(uport, uport->x_char);
		uport->icount.tx++;
		uport->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(uport)) {
		dm270_uart_stop_tx(uport, 0);
		return;
	}

	/* Send while we still have data & room in the fifo */
	count = uport->fifosize;
	do {
		dm270_uart_char_out(uport, xmit->buf[xmit->tail++]);
		xmit->tail &= (UART_XMIT_SIZE - 1);
		uport->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(uport);

	if (uart_circ_empty(xmit))
		dm270_uart_stop_tx(uport, 0);
}

static irqreturn_t
dm270_uart_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	struct uart_port *uport = dev_id;
	unsigned short    status;
	int               pass_counter = 0;

	status = dm270_uart_hwin(uport, DM270_UART_SR);
	while (status  & (DM270_UART_SR_RFNEF | DM270_UART_SR_TFEF)) {
		if (status & DM270_UART_SR_RFNEF)
			dm270_uart_rx_chars(uport, ptregs);

		if (status & DM270_UART_SR_TFEF)
			dm270_uart_tx_chars(uport);

		if (pass_counter++ > UART_ISR_PASS_LIMIT) {
			break;
		}

		status = dm270_uart_hwin(uport, DM270_UART_SR);
	}

	return IRQ_HANDLED;
}

/**
 **
 ** struct uart_ops functions
 **
 **/

static unsigned int
dm270_uart_tx_empty(struct uart_port *uport)
{
	return dm270_uart_tx_fifo_empty(uport);
}

static void
dm270_uart_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	return;
}

static unsigned int
dm270_uart_get_mctrl(struct uart_port *uport)
{
	return 0;
}

static void
dm270_uart_stop_tx(struct uart_port *uport, unsigned int tty_stop)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	if (dm270_uport->msr & DM270_UART_MSR_TFTIE) {
		dm270_uart_disable_tx_int(uport);
	}
}

static void
dm270_uart_start_tx(struct uart_port *uport, unsigned int tty_start)
{
	struct dm270_uart_port *dm270_uport = (struct dm270_uart_port *)uport;

	if (!(dm270_uport->msr & DM270_UART_MSR_TFTIE)) {
		dm270_uart_enable_tx_int(uport);
	}
}

static void dm270_uart_send_xchar(struct uart_port *uport, char ch)
{
	uport->x_char = ch;
	if (ch) {
		dm270_uart_enable_tx_int(uport);
	}
}

static void
dm270_uart_stop_rx(struct uart_port *uport)
{
	dm270_uart_disable_rx_int(uport);
}

static void dm270_uart_enable_ms(struct uart_port *uport)
{
	return;
}

static void
dm270_uart_break_ctl(struct uart_port *uport, int break_state)
{
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);

	if (break_state == -1)
		dm270_uart_enable_breaks(uport);
	else
		dm270_uart_disable_breaks(uport);

	spin_unlock_irqrestore(&uport->lock, flags);
}

static int
dm270_uart_startup(struct uart_port *uport)
{
	unsigned short junk;
	int retval;

	dm270_uart_hwreset(uport);
	dm270_uart_save_registers(uport);
	dm270_uart_disable_ints(uport, &junk);
	dm270_uart_clear_fifos(uport);
	dm270_uart_disable_breaks(uport);	
	dm270_uart_set_rate(uport, UART_DEFAULT_BAUD);
	dm270_uart_set_tx_trigger(uport, DM270_UART_TFCR_TRG_1);
	dm270_uart_set_rx_trigger(uport, DM270_UART_RFCR_TRG_16);

	/* Allocate the IRQ */
	retval = request_irq(uport->irq, dm270_uart_interrupt, SA_INTERRUPT,
			     UART_TYPE, uport);
	if (retval)
		return retval;

	/* Finally, enable interrupts */
	dm270_uart_enable_rx_int(uport);
	return 0;
}

static void
dm270_uart_shutdown(struct uart_port *uport)
{
	unsigned short junk;

	/* Free the IRQ */
	free_irq(uport->irq, uport);

	dm270_uart_disable_ints(uport, &junk);	/* disable all intrs */
	dm270_uart_disable_breaks(uport);	/* disable break condition */
	dm270_uart_clear_fifos(uport);		/* reset FIFO's */	
}

static void
dm270_uart_set_termios(struct uart_port *uport, struct termios *termios, struct termios *old)
{
	unsigned short cval;
	unsigned long flags;
	unsigned int baud;

	/* Byte size and parity */
	switch (termios->c_cflag & CSIZE) {
	case CS7:
		cval = DM270_UART_MSR_7_DBITS;
		break;
	case CS8:
	default:
		cval = DM270_UART_MSR_8_DBITS;
		break;
	}

	if (termios->c_cflag & CSTOPB) {
		cval |= DM270_UART_MSR_2_SBITS;
	} else {
		cval |= DM270_UART_MSR_1_SBITS;
	}
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD) {
			cval |= DM270_UART_MSR_ODD_PARITY;
		} else {
			cval |= DM270_UART_MSR_EVEN_PARITY;
		}
	} else if (termios->c_cflag & PARODD) {
		cval |= DM270_UART_MSR_ODD_PARITY;
	} else {
		cval |= DM270_UART_MSR_NO_PARITY;
	}

	baud = uart_get_baud_rate(uport, termios, old, 0, uport->uartclk/16);
	if (baud == 0) {
		baud = 9600;
	}

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&uport->lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(uport, termios->c_cflag, baud);

	uport->read_status_mask = (DM270_UART_DTRR_ORF | DM270_UART_DTRR_RVF);
	if (termios->c_iflag & INPCK)
		uport->read_status_mask |= (DM270_UART_DTRR_PEF | DM270_UART_DTRR_FE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		uport->read_status_mask |= DM270_UART_DTRR_BF;

	/* Characters to ignore */
	uport->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		uport->ignore_status_mask |= (DM270_UART_DTRR_PEF | DM270_UART_DTRR_FE);
	if (termios->c_iflag & IGNBRK) {
		uport->ignore_status_mask |= DM270_UART_DTRR_BF;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			uport->ignore_status_mask |= DM270_UART_DTRR_ORF;
	}

	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		uport->ignore_status_mask |= DM270_UART_DTRR_RVF;

	dm270_uart_set_rate(uport, baud);
	dm270_uart_set_mode(uport, cval);

	/*
	 * We should always set the receive FIFO trigger to the lowest value
	 * because we don't poll.
	 */
	dm270_uart_set_rx_trigger(uport, DM270_UART_RFCR_TRG_1);

	spin_unlock_irqrestore(&uport->lock, flags);
}

static void
dm270_uart_pm(struct uart_port *uport, unsigned int state, unsigned int oldstate)
{
	return;
}

static int
dm270_uart_set_wake(struct uart_port *uport, unsigned int state)
{
	return 0;
}

static const char *
dm270_uart_type(struct uart_port *uport)
{
	return UART_TYPE;
}


static void
dm270_uart_release_port(struct uart_port *uport)
{
	return;
}

static int
dm270_uart_request_port(struct uart_port *uport)
{
	return 0;
}

static void
dm270_uart_config_port(struct uart_port *uport, int config)
{
	return;
}

static int dm270_uart_verify_port(struct uart_port *uport, struct serial_struct *serial)
{
	if (serial->port != uport->iobase || serial->irq != uport->irq ||
	    serial->baud_base < 9600 || serial->xmit_fifo_size <= 0 ||
	    serial->io_type != uport->iotype || serial->type != uport->type ||
	    serial->line != uport->line)
		return -EINVAL;
	return 0;
}

static int dm270_uart_ioctl(struct uart_port *uport, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static struct uart_ops dm270_uart_ops = {
	tx_empty:	dm270_uart_tx_empty,
	set_mctrl:	dm270_uart_set_mctrl,
	get_mctrl:	dm270_uart_get_mctrl,
	stop_tx:	dm270_uart_stop_tx,
	start_tx:	dm270_uart_start_tx,
	send_xchar:	dm270_uart_send_xchar,
	stop_rx:	dm270_uart_stop_rx,
	enable_ms:	dm270_uart_enable_ms,
	break_ctl:	dm270_uart_break_ctl,
	startup:	dm270_uart_startup,
	shutdown:	dm270_uart_shutdown,
	set_termios:	dm270_uart_set_termios,
	pm:		dm270_uart_pm,
	set_wake:	dm270_uart_set_wake,
	type:		dm270_uart_type,
	release_port:	dm270_uart_release_port,
	request_port:	dm270_uart_request_port,
	config_port:	dm270_uart_config_port,
	verify_port:	dm270_uart_verify_port,
	ioctl:		dm270_uart_ioctl,
};

static struct uart_port dm270_uart_ports[UART_NR] = {
	{
		iobase:		DM270_UART0_BASE,
		irq:		DM270_INTERRUPT_UART0,
		uartclk:	CONFIG_ARM_CLK,
		fifosize:	DM270_UART_TXFIFO_BYTESIZE,
		iotype:		UPIO_PORT,
		type:		PORT_DM270,
		ops:		&dm270_uart_ops,
		line: 		0,
	},
	{
		iobase:		DM270_UART1_BASE,
		irq:		DM270_INTERRUPT_UART1,
		uartclk:	CONFIG_ARM_CLK,
		fifosize:	DM270_UART_TXFIFO_BYTESIZE,
		iotype:		UPIO_PORT,
		type:		PORT_DM270,
		ops:		&dm270_uart_ops,
		line: 		1,
	}
};

#ifdef CONFIG_SERIAL_DM270_CONSOLE
/************** console driver *****************/

/*
 * This block is enabled when the user has used `make xconfig`
 * to enable kernel printk() to come out the serial port.
 * The register_console(&dm270_console) call below is what hooks in
 * our serial output routine here with the kernel's printk output.
 */

/*
 * Wait for transmitter & holding register to empty
 */

static inline void
dm270_console_wait_for_xmitr(struct uart_port *uport)
{
	int tmp;

	for (tmp = 1000000 ; tmp > 0 ; tmp--)
		if (dm270_uart_room_in_tx_fifo(uport))
			break;
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 * The console_lock must be held when we get here.
 */

static void
dm270_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *uport = &dm270_uart_ports[co->index];
	unsigned short msr;
	unsigned int ii;

	/* First save the MSR then disable the interrupts */
	dm270_uart_disable_ints(uport, &msr);

	/* Now, do each character */
	for (ii = 0; ii < count; ii++, s++) {
		dm270_console_wait_for_xmitr(uport);
		/*
		 * Send the character out.
		 * If a LF, also do CR...
		 */
		dm270_uart_char_out(uport, *s);
		if (*s == 10) {
			/* LF? add CR */
			dm270_console_wait_for_xmitr(uport);
			dm270_uart_char_out(uport, 13);
		}
	}

	/*
	 * Finally, wait for transmitter & holding register to empty
	 * and restore the MSR
	 */
	dm270_console_wait_for_xmitr(uport);
	dm270_uart_restore_ints(uport, msr);
}

/*
 * Setup initial baud/bits/parity/flow control
 */

static int __init
dm270_console_setup(struct console *co, char *options)
{
	struct uart_port *uport;
	int baud = CONSOLE_DEFAULT_BAUD;
	int bits = CONSOLE_DEFAULT_BITS;
	int parity = CONSOLE_DEFAULT_PARITY;
	int flow = CONSOLE_DEFAULT_FLOW;
	unsigned short junk;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	uport = uart_get_console(dm270_uart_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	dm270_uart_hwreset(uport);
	dm270_uart_save_registers(uport);
	dm270_uart_disable_ints(uport, &junk);
	dm270_uart_clear_fifos(uport);
	dm270_uart_disable_breaks(uport);	
	dm270_uart_set_tx_trigger(uport, DM270_UART_TFCR_TRG_1);

	return uart_set_options(uport, co, baud, parity, bits, flow);
}

extern struct uart_driver dm270_uart_driver;

static struct console dm270_console = {
	name:		"ttyS",
	write:		dm270_console_write,
	device:		uart_console_device,
	setup:		dm270_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
	data:		&dm270_uart_driver,
};

static int __init
dm270_console_init(void)
{
	register_console(&dm270_console);
	return 0;
}

console_initcall(dm270_console_init);

#endif /* CONFIG_SERIAL_DM270_CONSOLE */

static struct uart_driver dm270_uart_driver = {
	owner:		THIS_MODULE,
	driver_name:	UART_DRIVER_NAME,
	devfs_name:	"tts/",
	dev_name:	"ttyS",
	major:		TTY_MAJOR,
	minor:		64,
	nr:		UART_NR,
#ifdef CONFIG_SERIAL_DM270_CONSOLE
	cons:		&dm270_console,
#endif
};

static int __init
dm270_uart_init(void)
{
	int retval;
	int ii;

	retval = uart_register_driver(&dm270_uart_driver);
	if (retval)
		return retval;

	for (ii = 0; ii < UART_NR; ii++) {
		retval = uart_add_one_port(&dm270_uart_driver, &dm270_uart_ports[ii]);
		if (retval)
			return retval;
	}

	return 0;
}

static void __exit
dm270_uart_exit(void)
{
	int ii;

	for (ii = 0; ii < UART_NR; ii++) {
		uart_remove_one_port(&dm270_uart_driver, &dm270_uart_ports[ii]);
	}

	uart_unregister_driver(&dm270_uart_driver);
}

module_init(dm270_uart_init);
module_exit(dm270_uart_exit);

MODULE_AUTHOR("Chee Tim Loh <lohct@pacific.net.sg>");
MODULE_DESCRIPTION("DM270 UART driver");
MODULE_LICENSE("GPL");
