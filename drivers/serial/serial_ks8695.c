/*
 *  linux/drivers/char/serial_ks8695.c
 *
 *  Driver for KS8695 serial port
 *
 *  Based on drivers/serial/serial_ks8695uart.c, by Kam Lee.
 *
 *  Copyright 2002 Micrel Inc.
 *  (C) Copyrght 2006 Greg Ungerer <gerg@snapgear.com>
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
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#define KS8695_UART_NR	        1

#ifdef CONFIG_SERIAL_KS8695_COM
#define KS8695_SERIAL_MAJOR	4
#define KS8695_SERIAL_MINOR	64
#define	KS8695_SERIAL_DEV_NAME	"ttyS"
#else
#define KS8695_SERIAL_MAJOR	204
#define KS8695_SERIAL_MINOR	16
#define	KS8695_SERIAL_DEV_NAME	"ttyAM"
#endif

/*
 * Access macros for the KS8695 UART
 */
#define UART_GET_INT_STATUS(p)    (*(volatile unsigned int *)((p)->membase + KS8695_INT_STATUS))
#define UART_CLR_INT_STATUS(p, c) (*(unsigned int *)((p)->membase + KS8695_INT_STATUS) = (c))
#define UART_GET_CHAR(p)	  ((*(volatile unsigned int *)((p)->membase + KS8695_UART_RX_BUFFER)) & 0xFF)
#define UART_PUT_CHAR(p, c)       (*(unsigned int *)((p)->membase + KS8695_UART_TX_HOLDING) = (c))
#define UART_GET_IER(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_INT_ENABLE))
#define UART_PUT_IER(p, c)        (*(unsigned int *)((p)->membase + KS8695_INT_ENABLE) = (c))
#define UART_GET_FCR(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_UART_FIFO_CTRL))
#define UART_PUT_FCR(p, c)        (*(unsigned int *)((p)->membase + KS8695_UART_FIFO_CTRL) = (c))
#define UART_GET_MSR(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_UART_MODEM_STATUS))
#define UART_GET_LSR(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_UART_LINE_STATUS))
#define UART_GET_LCR(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_UART_LINE_CTRL))
#define UART_PUT_LCR(p, c)        (*(unsigned int *)((p)->membase + KS8695_UART_LINE_CTRL) = (c))
#define UART_GET_MCR(p)	          (*(volatile unsigned int *)((p)->membase + KS8695_UART_MODEM_CTRL))
#define UART_PUT_MCR(p, c)        (*(unsigned int *)((p)->membase + KS8695_UART_MODEM_CTRL) = (c))
#define UART_GET_BRDR(p)	  (*(volatile unsigned int *)((p)->membase + KS8695_UART_DIVISOR))
#define UART_PUT_BRDR(p, c)       (*(unsigned int *)((p)->membase + KS8695_UART_DIVISOR) = (c))
#define UART_RX_DATA(s)		  (((s) & KS8695_UART_LINES_RXFE) != 0)
#define UART_TX_READY(s)	  (((s) & KS8695_UART_LINES_TXFE) != 0)

#define UART_DUMMY_LSR_RX	0x100

static void ks8695uart_stop_tx(struct uart_port *port)
{
	unsigned int ier;

	ier = UART_GET_IER(port);
        if (ier &  KS8695_INT_ENABLE_TX)
		disable_irq(8);
}

static void ks8695uart_start_tx(struct uart_port *port)
{
	unsigned int ier;

        ier = UART_GET_IER(port);
        if ((ier &  KS8695_INT_ENABLE_TX ) == 0)
		enable_irq(8);
}

static void ks8695uart_stop_rx(struct uart_port *port)
{
	unsigned int ier;

	ier = UART_GET_IER(port);
	ier &= ~KS8695_INT_ENABLE_RX;
	UART_PUT_IER(port, ier);
}

static void ks8695uart_enable_ms(struct uart_port *port)
{
       UART_PUT_IER(port, UART_GET_IER(port) | KS8695_INT_ENABLE_MODEM);
}

static irqreturn_t ks8695uart_tx_chars(int irq, void *data)
{
	struct uart_port *port = data;
	struct circ_buf *xmit = &port->info->xmit;
	int i;

	if (port->x_char) {
		UART_CLR_INT_STATUS(port, KS8695_INTMASK_UART_TX);
		UART_PUT_CHAR(port, (unsigned int) port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return IRQ_HANDLED;
	}

	for (i = 0; (i < 16); i++) {
		if (xmit->head == xmit->tail)
			break;
		UART_CLR_INT_STATUS(port, KS8695_INTMASK_UART_TX);
		UART_PUT_CHAR(port, (unsigned int) (xmit->buf[xmit->tail]));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (xmit->head == xmit->tail)
		disable_irq(irq);

	return IRQ_HANDLED;
}

static irqreturn_t ks8695uart_rx_chars(int irq, void *data)
{
	struct uart_port *port = data;
	struct tty_struct *tty = port->info->tty;
	unsigned int status, ch, lsr, max_count = 256;
	char flag;

	status = UART_GET_LSR(port);
	while (UART_RX_DATA(status) && max_count--) {

		ch = UART_GET_CHAR(port);
		flag = TTY_NORMAL;
		port->icount.rx++;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		lsr = UART_GET_LSR(port) | UART_DUMMY_LSR_RX;
		if (lsr & KS8695_UART_LINES_ANY) {
			if (lsr & KS8695_UART_LINES_BE) {
				lsr &= ~(KS8695_UART_LINES_FE | KS8695_UART_LINES_FE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (lsr & KS8695_UART_LINES_PE)
				port->icount.parity++;
			else if (lsr & KS8695_UART_LINES_FE)
				port->icount.frame++;
			if (lsr & KS8695_UART_LINES_OE)
				port->icount.overrun++;

			lsr &= port->read_status_mask;

			if (lsr & KS8695_UART_LINES_BE)
				flag = TTY_BREAK;
			else if (lsr & KS8695_UART_LINES_PE)
				flag = TTY_PARITY;
			else if (lsr & KS8695_UART_LINES_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, KS8695_UART_LINES_OE, ch, flag);

ignore_char:
		status = UART_GET_LSR(port);
	}

	tty_flip_buffer_push(tty);
	return IRQ_HANDLED;
}


static irqreturn_t ks8695uart_modem(int irq, void *data)
{
	struct uart_port *port = data;
	unsigned int status, delta;

	/* clear modem interrupt by reading MSR */
	status = UART_GET_MSR(port);
	delta = status & 0x0B;

	if (!delta)
		return IRQ_NONE;

	if (delta & KS8695_UART_MODEM_DDCD)
		uart_handle_dcd_change(port, status & KS8695_UART_MODEM_DDCD);
	if (delta & KS8695_UART_MODEM_DDSR)
		port->icount.dsr++;
	if (delta & KS8695_UART_MODEM_DCTS)
		uart_handle_cts_change(port, status & KS8695_UART_MODEM_DCTS);
	wake_up_interruptible(&port->info->delta_msr_wait);

	return IRQ_HANDLED;
}

static unsigned int ks8695uart_tx_empty(struct uart_port *port)
{
	unsigned int status;

	status = UART_GET_LSR(port);
	return UART_TX_READY(status) ? TIOCSER_TEMT : 0; 
}

static unsigned int ks8695uart_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;

	status = UART_GET_MSR(port);
	if (status & KS8695_UART_MODEM_DCD)
		result |= TIOCM_CAR;
	if (status & KS8695_UART_MODEM_DSR)
		result |= TIOCM_DSR;
	if (status & KS8695_UART_MODEM_CTS)
		result |= TIOCM_CTS;

	return result;
}

static void ks8695uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int mcr;

	mcr = UART_GET_MCR(port);
	if (mctrl & TIOCM_RTS)
		mcr |= KS8695_UART_MODEMC_RTS;
	else
		mcr &= ~KS8695_UART_MODEMC_RTS;

	if (mctrl & TIOCM_DTR)
		mcr |= KS8695_UART_MODEMC_DTR;
	else
		mcr &= ~KS8695_UART_MODEMC_DTR;

	UART_PUT_MCR(port, mcr);
}

static void ks8695uart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int lcr;

	lcr = UART_GET_LCR(port);
	if (break_state == -1)
		lcr |= KS8695_UART_LINEC_BRK;
	else
		lcr &= ~KS8695_UART_LINEC_BRK;
	UART_PUT_LCR(port, lcr);
}

static int ks8695uart_startup(struct uart_port *port)
{
	int retval;

	retval = request_irq(KS8695_INT_UART_TX, ks8695uart_tx_chars, SA_SHIRQ | SA_INTERRUPT, "KS8695 uart(TX)", port);
	if (retval)
		return retval;

        retval = request_irq(KS8695_INT_UART_RX, ks8695uart_rx_chars, SA_SHIRQ | SA_INTERRUPT, "KS8695 uart(RX)", port);
        if (retval)
                return retval;

        retval = request_irq(KS8695_INT_UART_LINE_ERR, ks8695uart_rx_chars, SA_SHIRQ | SA_INTERRUPT, "KS8695 uart(error)", port);
        if (retval)
                return retval;

        retval = request_irq(KS8695_INT_UART_MODEMS, ks8695uart_modem, SA_SHIRQ | SA_INTERRUPT, "KS8695 uart(modem)", port);
        if (retval)
                return retval;
	
	return 0;
}

static void ks8695uart_shutdown(struct uart_port *port)
{
        /* disable break condition and fifos */
        UART_PUT_LCR(port, UART_GET_LCR(port) & ~KS8695_UART_LINEC_BRK);
        UART_PUT_FCR(port, UART_GET_FCR(port) & ~KS8695_UART_FIFO_FEN);

        free_irq(KS8695_INT_UART_RX, port);
        free_irq(KS8695_INT_UART_TX, port);
        free_irq(KS8695_INT_UART_LINE_ERR, port);
        free_irq(KS8695_INT_UART_MODEMS, port);
}

static void ks8695uart_set_termios(struct uart_port *port, struct termios *termios, struct termios *old)
{
	unsigned int baud, lcr, fcr = 0;
	unsigned long flags;

	/* byte size and parity */
	switch (termios->c_cflag & CSIZE) {
	case CS5: lcr = KS8695_UART_LINEC_WLEN5; break;
	case CS6: lcr = KS8695_UART_LINEC_WLEN6; break;
	case CS7: lcr = KS8695_UART_LINEC_WLEN7; break;
	default:  lcr = KS8695_UART_LINEC_WLEN8; break; // CS8
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= KS8695_UART_LINEC_STP2;
	if (termios->c_cflag & PARENB) {
		lcr |= KS8695_UART_LINEC_PEN;
		if (!(termios->c_cflag & PARODD))
			lcr |= KS8695_UART_LINEC_EPS;
	}

	if (port->fifosize > 1)
		fcr = KS8695_UART_FIFO_TRIG04 | KS8695_UART_FIFO_TXRST | KS8695_UART_FIFO_RXRST | KS8695_UART_FIFO_FEN;

	port->read_status_mask = KS8695_UART_LINES_OE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= (KS8695_UART_LINES_FE | KS8695_UART_LINES_PE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= KS8695_UART_LINES_BE;

	/* Characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= (KS8695_UART_LINES_FE | KS8695_UART_LINES_PE);
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= KS8695_UART_LINES_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= KS8695_UART_LINES_OE;
	}

	/* Ignore all characters if CREAD is not set.  */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_DUMMY_LSR_RX;

	baud = uart_get_baud_rate(port, termios, old, 50, 230400);

	/* first, disable everything */
	local_irq_save(flags);

	if ((port->flags & ASYNC_HARDPPS_CD) ||
	    (termios->c_cflag & CRTSCTS) || !(termios->c_cflag & CLOCAL))
		ks8695uart_enable_ms(port);

	UART_PUT_BRDR(port, port->uartclk / baud); 
	UART_PUT_LCR(port, lcr);
	UART_PUT_FCR(port, fcr);

	local_irq_restore(flags);
}

static const char *ks8695uart_type(struct uart_port *port)
{
	return port->type == PORT_KS8695 ? "KS8695" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void ks8695uart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, 0x24);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int ks8695uart_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, 0x24, "KS8695 UART") != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void ks8695uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_KS8695;
		ks8695uart_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int ks8695uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if ((ser->type != PORT_UNKNOWN) && (ser->type != PORT_KS8695))
		return -EINVAL;
	if ((ser->irq < 0) || (ser->irq >= NR_IRQS))
		return -EINVAL;
	if (ser->baud_base < 9600)
		return -EINVAL;
	return 0;
}

static struct uart_ops ks8695uart_ops = {
	.tx_empty	= ks8695uart_tx_empty,
	.set_mctrl	= ks8695uart_set_mctrl,
	.get_mctrl	= ks8695uart_get_mctrl,
	.stop_tx	= ks8695uart_stop_tx,
	.start_tx	= ks8695uart_start_tx,
	.stop_rx	= ks8695uart_stop_rx,
	.enable_ms	= ks8695uart_enable_ms,
	.break_ctl	= ks8695uart_break_ctl,
	.startup	= ks8695uart_startup,
	.shutdown	= ks8695uart_shutdown,
	.set_termios	= ks8695uart_set_termios,
	.type		= ks8695uart_type,
	.release_port	= ks8695uart_release_port,
	.request_port	= ks8695uart_request_port,
	.config_port	= ks8695uart_config_port,
	.verify_port	= ks8695uart_verify_port,
};

static struct uart_port ks8695uart_ports[KS8695_UART_NR] = {
	{
		.line		= 0,
		.membase	= (void *) KS8695_IO_VIRT,
		.mapbase	= KS8695_IO_BASE + KS8695_UART_RX_BUFFER,
		.iotype		= SERIAL_IO_MEM,
		.irq		= KS8695_INT_UART_RX,
		.uartclk	= 25000000,
		.fifosize	= 16,
		.ops		= &ks8695uart_ops,
		.flags		= ASYNC_BOOT_AUTOCONF,
	}
};

#ifdef CONFIG_SERIAL_KS8695_CONSOLE

/*
 * Force a single char out the serial. It must go out, poll the ready
 * register until we can send it, and make sure it is sent.
 */
static void ks8695uart_console_putc(struct console *co, const char c)
{
	struct uart_port *port = ks8695uart_ports + co->index;

	while (!UART_TX_READY(UART_GET_LSR(port)))
		;

	UART_PUT_CHAR(port, (unsigned int) c);

	while (!UART_TX_READY(UART_GET_LSR(port)))
		;
}

static void ks8695uart_console_write(struct console *co, const char *s, unsigned int count)
{
	int i;

	for (i = 0; i < count; i++, s++) {
		ks8695uart_console_putc(co, *s);
		if (*s == '\n')
			ks8695uart_console_putc(co, '\r');
	}
}

static int __init ks8695uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	port = uart_get_console(ks8695uart_ports, KS8695_UART_NR, co);
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver ks8695uart_reg;
static struct console ks8695uart_console = {
	.name		= KS8695_SERIAL_DEV_NAME,
	.write		= ks8695uart_console_write,
	.device		= uart_console_device,
	.setup		= ks8695uart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &ks8695uart_reg,
};

static int __init ks8695uart_console_init(void)
{
	register_console(&ks8695uart_console);
	return 0;
}

console_initcall(ks8695uart_console_init);

#define KS8695UART_CONSOLE	&ks8695uart_console
#else
#define KS8695UART_CONSOLE	NULL
#endif

static struct uart_driver ks8695uart_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "serial_ks8695",
	.dev_name	= KS8695_SERIAL_DEV_NAME,
	.major		= KS8695_SERIAL_MAJOR,
	.minor		= KS8695_SERIAL_MINOR,
	.nr		= KS8695_UART_NR,
	.cons		= KS8695UART_CONSOLE,
};

static int __init ks8695uart_init(void)
{
	int i, rc;
   
	rc = uart_register_driver(&ks8695uart_reg);
	if (rc == 0) {
		for (i = 0; (i < KS8695_UART_NR); i++)
			uart_add_one_port(&ks8695uart_reg, &ks8695uart_ports[i]);
	}

	return rc;
}

static void __exit ks8695uart_exit(void)
{
	int i;

	for (i = 0; (i < KS8695_UART_NR); i++)
		uart_remove_one_port(&ks8695uart_reg, &ks8695uart_ports[i]);
	uart_unregister_driver(&ks8695uart_reg);
}

module_init(ks8695uart_init);
module_exit(ks8695uart_exit);

MODULE_AUTHOR("Micrel Semiconductor");
MODULE_DESCRIPTION("KS8695 serial port driver");
MODULE_LICENSE("GPL");
