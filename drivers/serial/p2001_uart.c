/*
 *  linux/drivers/char/p2001_uart.c
 *
 *  Driver for P2001 uart port
 *
 *  Copyright (C) 2004 Tobias Lorenz
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

/*
 * Version 1.0: First working version
 * Version 1.1: Removed all READ_REG/WRITE_REG
 * Version 1.2: Break handling
 * Version 1.3: Hardware handshake
 *              Device naming, major/minor nr for setserial
 *              ISR cleanups
 * Version 1.4: cpu frequency scaling
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/serial_core.h>

#ifdef CONFIG_CPU_FREQ
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#endif

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#if defined(CONFIG_SERIAL_P2001_UART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif


/**************************************************************************
 * Definitions
 **************************************************************************/
static const char *version =
	"p2001_uart.c:v1.4 12/29/2004 Tobias Lorenz (tobias.lorenz@gmx.net)\n";

static const char p2001_uart_name[] = "P2001 uart";

#define TX_MIN_BUF	10

#define tx_enabled(port)	((port)->unused[0])
#define rx_enabled(port)	((port)->unused[1])

extern struct uart_driver	p2001_uart_driver;	/* UART Driver     */
extern struct uart_port		p2001_uart_port;	/* UART Port       */
extern struct uart_ops		p2001_uart_ops;		/* UART Operations */
extern struct console		p2001_console;		/* Console         */

#define DEFAULT_BAUD 57600
static unsigned int baud;	/* Current baudrate */



/**************************************************************************
 * UART Driver
 **************************************************************************/

static struct uart_driver p2001_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "serial",		/* name of tty/console device */
	.dev_name	= "ttyS",		/* name of tty/console device */
#ifdef CONFIG_DEVFS_FS
	.devfs_name	= "tts/",		/* name of tty/console device */
#endif
	.major		= 4,			/* major number for the driver */
	.minor		= 64,			/* starting minor number */
	.nr		= 1,			/* maximum number of serial ports this driver supports */
#ifdef CONFIG_SERIAL_P2001_UART_CONSOLE
	.cons		= &p2001_console,
#endif
};



/**************************************************************************
 * UART Port
 **************************************************************************/

static struct uart_port p2001_uart_port = {
	.membase	= (void*)P2001_UART,		/* read/write[bwl] */
	.mapbase	= (unsigned int)P2001_UART,	/* for ioremap */
	.iotype		= UPIO_MEM,			/* io access style */
	.irq		= IRQ_UART,			/* irq number */
	.uartclk	= CONFIG_SYSCLK/8,		/* base uart clock */
	.fifosize	= 32,				/* tx fifo size */
	.ops		= &p2001_uart_ops,
	.flags		= UPF_BOOT_AUTOCONF,
	.line		= 0,				/* port index */
};



/**************************************************************************
 * UART interrupt routine
 **************************************************************************/

/* uart interrupt send routine */
static void p2001_uart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int count;

	if (port->x_char) {
		while ((P2001_UART->r.STATUS & 0x3f) > TX_MIN_BUF)
			barrier();
		P2001_UART->w.TX[0] = port->x_char;
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		tx_enabled(port) = 0;
		return;
	}

	count = port->fifosize >> 1;
	do {
		while ((P2001_UART->r.STATUS & 0x3f) > TX_MIN_BUF)
			barrier();
		P2001_UART->w.TX[0] = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		tx_enabled(port) = 0;

	P2001_UART->w.IRQ_Status |= (1<<0);
}

/* uart interrupt receive routine */
static void p2001_uart_rx_chars(struct uart_port *port, struct pt_regs *regs)
{
	struct tty_struct *tty = port->info->tty;
	unsigned int status;
	unsigned int rxddelta;
	unsigned int rx;
	unsigned int max_count = 256;

	status = P2001_UART->r.IRQ_Status;
	rxddelta = (P2001_UART->r.STATUS >> 6) & 0x3f;
	while (rxddelta && max_count--) {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				printk(KERN_WARNING "TTY_DONT_FLIP set\n");
				return;
			}
		}

		rx = P2001_UART->r.RX[0];

		*tty->flip.char_buf_ptr = rx & 0xff;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		if (status & ((1<<7)|(1<<6)|(1<<9))) {
			if (status & (1<<7)) {			/* RxD_BRK */
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_chars;
			} else if (status & (1<<6))		/* RxD_FIFO_PAR_ERR */
				port->icount.parity++;
			if (status & (1<<9))			/* RxD_LOST */
				port->icount.overrun++;

			status &= port->read_status_mask;

			if (status & (1<<7))			/* RxD_BRK */
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			else if (status & (1<<6))		/* RxD_FIFO_PAR_ERR */
				*tty->flip.flag_buf_ptr = TTY_PARITY;
				
		}

		if (uart_handle_sysrq_char(port, ch, regs))
			goto ignore_chars;

		if (rx && port->ignore_status_mask == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((status & (1<<9)) &&		/* RxD_LOST */
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character
			 */
			*tty->flip.char_buf_ptr++ = 0;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
			tty->flip.count++;
		}
	ignore_chars:
		rxddelta = (P2001_UART->r.STATUS >> 6) & 0x3f;
	}
	tty_flip_buffer_push(tty);

	P2001_UART->w.IRQ_Status |= (1<<3) | (1<<6) | (1<<7) | (1<<9);
}

/* uart interrupt routine */
static irqreturn_t p2001_uart_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	unsigned int status;

	spin_lock(&port->lock);

	status = P2001_UART->r.IRQ_Status;
	// TXD_SEND | TXD_LAST_DATA
	if (status & (1<<0)) // (1<<2)
		p2001_uart_tx_chars(port);
	// RXD_DATA | RxD_FIFO_PAR_ERR | RxD_BRK | RxD_LOST
	if (status & ((1<<3) | (1<<6) | (1<<7) | (1<<9)))
		p2001_uart_rx_chars(port, regs);

	//status &= ~((1<<0) | (1<<2) | (1<<3));
	//if (status & 0x3ff)
	//	printk(KERN_INFO "p2001_uart_interrupt: status=0x%8.8x\n", status);

	P2001_UART->w.IRQ_Status &= ~0x3ff;

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}



/**************************************************************************
 * UART Operations
 **************************************************************************/

/* returns if the port transmitter is empty or not. */
static unsigned int p2001_uart_ops_tx_empty(struct uart_port *port)
{
	unsigned int txddelta = P2001_UART->r.STATUS & 0x3f;
	return (txddelta > 0) ? 0 : TIOCSER_TEMT;
}

/* sets a new value for the MCR UART register. */
static void p2001_uart_ops_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* no modem control lines */
}

/* gets the current MCR UART register value. */
static unsigned int p2001_uart_ops_get_mctrl(struct uart_port *port)
{
	/* no modem control lines */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* stops the port from sending data. */
static void p2001_uart_ops_stop_tx(struct uart_port *port)
{
	if (tx_enabled(port)) {
		P2001_UART->w.IRQ_Status &= ~((1<<20) | (1<<22));	// TXD_SEND | TXD_LAST_DATA
		tx_enabled(port) = 0;
	}
}

/* starts the port sending data. */
static void p2001_uart_ops_start_tx(struct uart_port *port)
{
	if (!tx_enabled(port)) {
		P2001_UART->w.IRQ_Status |= (1<<20) | (1<<22);	// TXD_SEND | TXD_LAST_DATA
		tx_enabled(port) = 1;
	}
	p2001_uart_tx_chars(port);
}

/* tells the port to send the XOFF character to the host. */
#if 0
static void p2001_uart_ops_send_xchar(struct uart_port *port, char ch)
{
#warning "p2001_uart_ops_send_xchar is not implemented."
}
#endif

/* stops receiving data. */
static void p2001_uart_ops_stop_rx(struct uart_port *port)
{
	if (rx_enabled(port)) {
		P2001_UART->w.IRQ_Status &= ~(1<<23);	// RXD_DATA
		rx_enabled(port) = 0;
	}
}

/* enables the modem status interrupts. */
static void p2001_uart_ops_enable_ms(struct uart_port *port)
{
	/* empty */
}

/* sends the BREAK value over the port. */
static void p2001_uart_ops_break_ctl(struct uart_port *port, int ctl)
{
	/* no break signal */
}

/* called once each time the open call happens */
static int p2001_uart_ops_startup(struct uart_port *port)
{
	int ret;

	tx_enabled(port) = 1;
	rx_enabled(port) = 1;

	ret = request_irq(port->irq, p2001_uart_interrupt, 0, p2001_uart_name, port);

	P2001_UART->w.Clear    = 0;
	// TXD_SEND | TXD_LAST_DATA
	P2001_UART->w.IRQ_Status |= (1<<20) | (1<<22);
	// RXD_DATA | RxD_FIFO_PAR_ERR | RxD_BRK | RxD_LOST
	P2001_UART->w.IRQ_Status |= (1<<23) | (1<<26) | (1<<27) | (1<<29);

	return ret;
}

/* called when the port is closed */
static void p2001_uart_ops_shutdown(struct uart_port *port)
{
	free_irq(port->irq, port);
}

/* called whenever the port line settings need to be modified */
static void p2001_uart_ops_set_termios(struct uart_port *port, struct termios *new, struct termios *old)
{
	unsigned int config;
	unsigned long flags;
	unsigned int prod, quot;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk);	// min:0/max:port->uartclk
	prod = 3;
	quot = (port->uartclk * prod)/baud;

	/* interrupt level */
	config = (12 << 11) | (12 << 17);	/* RXDHIGHWATER = 12, TXDLOWWATER = 12 */

	/* data bits */
	switch (new->c_cflag & CSIZE) {
		case CS5:
			config |= (5 << 5);	/* WORDLENGTH = 5 */
			break;
		case CS6:
			config |= (6 << 5);	/* WORDLENGTH = 6 */
			break;
		case CS7:
			config |= (7 << 5);	/* WORDLENGTH = 7 */
			break;
		default: /* CS8 */
			config |= (8 << 5);	/* WORDLENGTH = 8 */
			break;
	}

	/* parity */
	if (new->c_cflag & PARENB) {
		if (!(new->c_cflag & PARODD))
			config |= (1 << 2);	/* PARITYMODE = 1 (Even Parity) */
		else
			config |= (2 << 2);	/* PARITYMODE = 2 (Odd Parity) */
	}

	/* stop bits */
	if (new->c_cflag & CSTOPB)
		config |= (1 << 0);		/* STOPBITAW = 1 (1 Stopbit) */

	/* hardware flow control */
	if (new->c_cflag & CRTSCTS) {
		config |= (1 << 10);		/* USECTS = 1 */
		P2001_GPIO->PIN_MUX |= (1<<5);
	} else {
		P2001_GPIO->PIN_MUX &= ~(1<<5);
	}

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, new->c_cflag, baud);

	port->read_status_mask = (1<<9);			/* RXD_DATA_LOST */
	if (new->c_iflag & INPCK)
		port->read_status_mask |= (1<<6);		/* RxD_FIFO_PAR_ERR */
	if (new->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= (1<<7);		/* RxD_BRK */

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (new->c_iflag & IGNPAR)
		port->ignore_status_mask |= (1<<6);		/* RxD_FIFO_PAR_ERR */
	if (new->c_iflag & IGNBRK) {
		port->ignore_status_mask |= (1<<7);		/* RxD_BRK */
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (new->c_iflag & IGNPAR)
			port->ignore_status_mask |= (1<<9);	/* RxD_LOST */
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((new->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= (1<<3);		/* RXD_DATA */

	/* Set baud rate */
	P2001_UART->w.Baudrate = (quot<<16)+prod;
	P2001_UART->w.Config   = config;

	spin_unlock_irqrestore(&port->lock, flags);
}

/* power management: power the hardware down */
#ifdef CONFIG_PM
static void p2001_uart_ops_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
#warning "p2001_uart_ops_pm is not implemented."
}
#endif

/* power management: power the hardware up */
#ifdef CONFIG_PM
static int p2001_uart_ops_set_wake(struct uart_port *port, unsigned int state)
{
#warning "p2001_uart_ops_set_wake is not implemented."
}
#endif

/*
 * Return a string describing the port type
 */
static const char *p2001_uart_ops_type(struct uart_port *port)
{
	return port->type == PORT_P2001 ? "P2001" : NULL;
}

/*
 * Release IO and memory resources used by
 * the port. This includes iounmap if necessary.
 */
static void p2001_uart_ops_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, 0x30);
}

/*
 * Request IO and memory resources used by the
 * port. This includes iomapping the port if
 * necessary.
 */
static int p2001_uart_ops_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, 0x30, p2001_uart_name)
			 != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void p2001_uart_ops_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && p2001_uart_ops_request_port(port) == 0)
		port->type = PORT_P2001;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int p2001_uart_ops_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_P2001)
		ret = -EINVAL;
	if (ser->irq != NO_IRQ)
		ret = -EINVAL;
	return ret;
}

/* device specific ioctl calls */
#if 0
static int p2001_uart_ops_ioctl(struct uart_port *port, unsigned int, unsigned long)
{
#warning "p2001_uart_ops_ioctl is not implemented."
}
#endif


static struct uart_ops p2001_uart_ops = {
	.tx_empty	= p2001_uart_ops_tx_empty,	/* returns if the port transmitter is empty or not. */
	.set_mctrl	= p2001_uart_ops_set_mctrl,	/* sets a new value for the MCR UART register. */
	.get_mctrl	= p2001_uart_ops_get_mctrl,	/* gets the current MCR UART register value. */
	.stop_tx	= p2001_uart_ops_stop_tx,	/* stops the port from sending data. */
	.start_tx	= p2001_uart_ops_start_tx,	/* starts the port sending data. */
//	.send_xchar	= p2001_uart_ops_send_xchar,	/* tells the port to send the XOFF character to the host. */
	.stop_rx	= p2001_uart_ops_stop_rx,	/* stops receiving data. */
	.enable_ms	= p2001_uart_ops_enable_ms,	/* enables the modem status interrupts. */
	.break_ctl	= p2001_uart_ops_break_ctl,	/* sends the BREAK value over the port. */
	.startup	= p2001_uart_ops_startup,	/* called once each time the open call happens */
	.shutdown	= p2001_uart_ops_shutdown,	/* called when the port is closed */
	.set_termios	= p2001_uart_ops_set_termios,	/* called whenever the port line settings need to be modified */
#ifdef CONFIG_PM
	.pm		= p2001_uart_ops_pm,		/* power management: power the hardware down */
	.set_wake	= p2001_uart_ops_set_wake,	/* power management: power the hardware up */
#endif
	.type		= p2001_uart_ops_type,		/* Return a string describing the port type */
	.release_port	= p2001_uart_ops_release_port,	/* Release the region(s) being used by 'port' */
	.request_port	= p2001_uart_ops_request_port,	/* Request the region(s) being used by 'port' */
	.config_port	= p2001_uart_ops_config_port,	/* Configure/autoconfigure the port. */
	.verify_port	= p2001_uart_ops_verify_port,	/* Verify the new serial_struct (for TIOCSSERIAL). */
//	.ioctl		= p2001_uart_ops_ioctl,		/* device specific ioctl calls */
};



/**************************************************************************
 * CPU frequency scaling
 **************************************************************************/

#ifdef CONFIG_CPU_FREQ

/*
 * Starts receiving data.
 */
static void p2001_uart_ops_start_rx(struct uart_port *port)
{
	if (!rx_enabled(port)) {
		P2001_UART->w.IRQ_Status |= (1<<23);	// RXD_DATA
		rx_enabled(port) = 1;
	}
}

/*
 * Here we define a transistion notifier so that we can update all of our
 * ports' baud rate when the peripheral clock changes.
 */
static int p2001_uart_notifier(struct notifier_block *self, unsigned long phase, void *data)
{
	struct cpufreq_freqs *cf = data;
	unsigned int prod, quot;
	struct uart_port *port = &p2001_uart_port;

	if (phase == CPUFREQ_PRECHANGE) {
		/* Stop transceiver */
		p2001_uart_ops_stop_rx(&p2001_uart_port);
		p2001_uart_ops_stop_tx(&p2001_uart_port);

		while (!p2001_uart_ops_tx_empty(port))
			barrier();
	}

	if ((phase == CPUFREQ_POSTCHANGE) ||
	    (phase == CPUFREQ_RESUMECHANGE)){
		/* Set new baud rate */
		port->uartclk = 1000 * cf->new / 8;	// in MHz
		prod = 3;
		quot = (port->uartclk * prod)/baud;
		P2001_UART->w.Baudrate = (quot<<16)+prod;

		/* Start transceiver */
		p2001_uart_ops_start_rx(&p2001_uart_port);
		p2001_uart_ops_start_tx(&p2001_uart_port);
	}

	return NOTIFY_OK;
}

static struct notifier_block p2001_uart_nb = { &p2001_uart_notifier, NULL, 0 };

#endif /* CONFIG_CPU_FREQ */



/**************************************************************************
 * Console
 **************************************************************************/

#ifdef CONFIG_SERIAL_P2001_UART_CONSOLE

/* the function used to print kernel messages */
static void p2001_console_write(struct console *co, const char *s, unsigned int count)
{
	int i;

	for (i = 0; i < count; i++) {
		while ((P2001_UART->r.STATUS & 0x3f) > TX_MIN_BUF)
			barrier();
		P2001_UART->w.TX[0] = s[i];
		if (s[i] == '\n') {
			while ((P2001_UART->r.STATUS & 0x3f) > TX_MIN_BUF)
				barrier();
			P2001_UART->w.TX[0] = '\r';
		}
	}
}

/* the function is called when the console=  command-line argument matches the name for this console structure. */
static int __init p2001_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &p2001_uart_port;
	int parity = 'n';
	int bits   = 8;
	int flow   = 'n';

	baud = DEFAULT_BAUD;
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	
	return uart_set_options(port, co, baud, parity, bits, flow);
}


static struct console p2001_console = {
	.name		= "ttyS",			/* the name of the console device is used to parse the console= command line option. */
	.write		= p2001_console_write,		/* the function used to print kernel messages */
//	.read		= p2001_console_read,		/* ??? */
	.device		= uart_console_device,		/* a function that returns the device number for the underlying tty device that is currently acting as a console */
//	.unblank	= p2001_console_unblank,	/* the function, if defined, is used to unblank the screen. */
	.setup		= p2001_console_setup,		/* the function is called when the console=  command-line argument matches the name for this console structure. */
	.flags		= CON_PRINTBUFFER,		/* various console flags */
	.index		= -1,				/* the number of the device acting as a console in an array of devices. */
//	.cflag		= 0,
	.data		= &p2001_uart_driver,
};

static int __init p2001_console_init(void)
{
	register_console(&p2001_console);
	return 0;
}
console_initcall(p2001_console_init);
#endif



/**************************************************************************
 * Module functions
 **************************************************************************/

static int __init p2001_uart_module_init(void)
{
	int ret;

	printk(version);

	ret = uart_register_driver(&p2001_uart_driver);
	if (ret != 0) return ret;

	ret = uart_add_one_port(&p2001_uart_driver, &p2001_uart_port);
	if (ret != 0) return ret;

#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_register_notifier(&p2001_uart_nb, CPUFREQ_TRANSITION_NOTIFIER);
	printk("p2001_uart: CPU frequency notifier registered\n");
#endif

	return ret;
}

static void __exit p2001_uart_module_exit(void)
{
#ifdef CONFIG_CPU_FREQ
        cpufreq_unregister_notifier(&p2001_uart_nb, CPUFREQ_TRANSITION_NOTIFIER);
        printk("p2001_uart: CPU frequency notifier unregistered\n");
#endif
	uart_remove_one_port(&p2001_uart_driver, &p2001_uart_port);
	uart_unregister_driver(&p2001_uart_driver);
}

module_init(p2001_uart_module_init);
module_exit(p2001_uart_module_exit);

MODULE_AUTHOR("Tobias Lorenz");
MODULE_DESCRIPTION("P2001 uart driver");
MODULE_LICENSE("GPL");
