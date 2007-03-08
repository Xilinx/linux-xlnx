/*
 *  linux/drivers/serial/serial_s3c4510b.c
 *
 *  Driver for S3C4510B serial ports
 *
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
#include <asm/arch/uart.h>

#define __DRIVER_NAME    "Samsung S3C4510B Internal UART"

#define _SDEBUG
#ifdef _SDEBUG
#  define _DPRINTK(format, args...)  \
          printk (KERN_INFO "%s():%05d "format".\n" , __FUNCTION__ , __LINE__ , ## args);
#else
#  define _DPRINTK(format, args...)
#endif

/**
 **
 ** Internal(private) helper functions
 **
 **/

static void __xmit_char(struct uart_port *port, const char ch) {

	struct uart_regs *uart = (struct uart_regs *)port->iobase;

	while( !uart->m_stat.bf.txBufEmpty);

	uart->m_tx = ch;

	if ( ch == '\n') {
		while( !uart->m_stat.bf.txBufEmpty);
		uart->m_tx = '\r';
	}

}

static void __xmit_string(struct uart_port *port, const char *p, int len)
{
	while( len-- > 0) {
		__xmit_char( port, *p++);
	}
}

static void __s3c4510b_init(const struct uart_port *port, int baud)
{
	struct uart_regs *uart = (struct uart_regs *)port->iobase;
	UART_CTRL      uctrl;
	UART_LINE_CTRL ulctrl;
	UART_BAUD_DIV  ubd;

	/* Reset the UART */
	/* control register */
	uctrl.ui = 0x0;
	uctrl.bf.rxMode = 0x1;
	uctrl.bf.rxIrq = 0x1;
	uctrl.bf.txMode = 0x1;
	uctrl.bf.DSR = 0x1;
	uctrl.bf.sendBreak = 0x0;
	uctrl.bf.loopBack = 0x0;
	uart->m_ctrl.ui = uctrl.ui;
	
	/* Set the line control register into a safe sane state */
	ulctrl.ui  = 0x0;
	ulctrl.bf.wordLen   = 0x3; /* 8 bit data */
	ulctrl.bf.nStop     = 0x0; /* 1 stop bit */
	ulctrl.bf.parity    = 0x0; /* no parity */
	ulctrl.bf.clk       = 0x0; /* internal clock */
	ulctrl.bf.infra_red = 0x0; /* no infra_red */
	uart->m_lineCtrl.ui = ulctrl.ui;

	ubd.ui = 0x0;

	/* see table on page 10-15 in SAMSUNG S3C4510B manual */
	/* get correct divisor */
	switch( baud ? baud : 19200) {

	case 1200:
		ubd.bf.cnt0 = 1301;
		break;

	case 2400:
		ubd.bf.cnt0 = 650;
		break;

	case 4800:
		ubd.bf.cnt0 = 324;
		break;

	case 9600:
		ubd.bf.cnt0 = 162;
		break;

	case 19200:
		ubd.bf.cnt0 = 80;
		break;

	case 38400:
		ubd.bf.cnt0 = 40;
		break;

	case 57600:
		ubd.bf.cnt0 = 26;
		break;

	case 115200:
		ubd.bf.cnt0 = 13;
		break;
	}

	uart->m_baudDiv.ui = ubd.ui;
	uart->m_baudCnt = 0x0;
	uart->m_baudClk = 0x0;

}

/**
 **
 ** struct uart_ops functions below
 **
 **/

static void __s3c4510b_stop_tx(struct uart_port *port)
{

}


static void __s3c4510b_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;
	int count;

	// _DPRINTK("called with info = 0x%08x", (unsigned int) port);

	if ( port->x_char) {
		__xmit_char( port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty( xmit) || uart_tx_stopped( port)) {
		__s3c4510b_stop_tx( port);
		return;
	}

	count = port->fifosize >> 1;
	do {
		__xmit_char( port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup( port);

	if (uart_circ_empty(xmit))
		__s3c4510b_stop_tx( port);
}

static void __s3c4510b_start_tx(struct uart_port *port)
{
	__s3c4510b_tx_chars( port);
}

static void __s3c4510b_send_xchar(struct uart_port *port, char ch)
{
	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
}

static void __s3c4510b_stop_rx(struct uart_port *port)
{
	struct uart_regs *uart = (struct uart_regs *)port->iobase;
	UART_CTRL      uctrl;

	_DPRINTK("called with port = 0x%08x", (unsigned int) port);

	uctrl.ui = uart->m_ctrl.ui;
	uctrl.bf.rxMode = 0x0;
	uart->m_ctrl.ui = uctrl.ui;
}

static void __s3c4510b_enable_ms(struct uart_port *port)
{
	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
}

static void __s3c4510b_rx_char(struct uart_port *port)
{
	struct uart_regs *uart = (struct uart_regs *)port->iobase;
	struct tty_struct *tty = port->info->tty;
	unsigned int ch;
	UART_STAT status;

	status.ui = uart->m_stat.ui;
	if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
		tty->flip.work.func((void *)tty);
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			printk(KERN_WARNING "TTY_DONT_FLIP set\n");
			return;
		}
	}

	ch = uart->m_rx & 0xFF;

	*tty->flip.char_buf_ptr = ch;
	*tty->flip.flag_buf_ptr = TTY_NORMAL;
	port->icount.rx++;

	/*
	 * Note that the error handling code is
	 * out of the main execution path
	 */
	if ( status.bf.breakIrq) {
		port->icount.brk++;
		if (uart_handle_break(port))
			goto ignore_char;
		*tty->flip.flag_buf_ptr = TTY_BREAK;
	}
	else if ( status.bf.parity) {
		port->icount.parity++;
		*tty->flip.flag_buf_ptr = TTY_PARITY;
	}
	else if ( status.bf.frame) {
		port->icount.frame++;
		*tty->flip.flag_buf_ptr = TTY_FRAME;
	}
	else if ( status.bf.overrun) {
		port->icount.overrun++;
		if ( tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character
			 */
			*tty->flip.char_buf_ptr++ = 0;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
			tty->flip.count++;
		}
	}
	else {
		/* no errors */
		tty->flip.flag_buf_ptr++;
		tty->flip.char_buf_ptr++;
		tty->flip.count++;
	}

ignore_char:

	tty_flip_buffer_push(tty);


}

static irqreturn_t __s3c4510b_rx_int(int irq, void *dev_id, struct pt_regs *regs)
{
//	_DPRINTK("called with irq = 0x%08x", irq);

	struct uart_port *port = dev_id;

	LED_SET(2);
	__s3c4510b_rx_char( port);		
	LED_CLR(2);

	return IRQ_HANDLED;
}

static irqreturn_t __s3c4510b_tx_int(int irq, void *dev_id, struct pt_regs *regs)
{
//	_DPRINTK("called with irq = 0x%08x", irq);

	struct uart_port *port = dev_id;

	LED_SET(1);
	__s3c4510b_start_tx( port);		
	LED_CLR(1);

	return IRQ_HANDLED;
}

static unsigned int __s3c4510b_tx_empty(struct uart_port *port)
{
	struct uart_regs *uart = (struct uart_regs *)port->iobase;

//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);

	return uart->m_stat.bf.txBufEmpty ? 1 : 0;
}

static unsigned int __s3c4510b_get_mctrl(struct uart_port *port)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);

	return 0;
}

static void __s3c4510b_set_mctrl(struct uart_port *port, u_int mctrl)
{
//	_DPRINTK("called with port = 0x%08x, mctrl = 0x%08x", (unsigned int) port, mctrl);
}

static void __s3c4510b_break_ctl(struct uart_port *port, int break_state)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
}

static struct irqaction __rx_irqaction[UART_NR] = {
	{
		name:	  "serial0_rx",
		flags:	  SA_INTERRUPT,
		handler:  __s3c4510b_rx_int,
	},
	{
		name:	  "serial1_rx",
		flags:	  SA_INTERRUPT,
		handler:  __s3c4510b_rx_int,
	},
};

static struct irqaction __tx_irqaction[UART_NR] = {
	{
		name:	  "serial0_tx",
		flags:	  SA_INTERRUPT,
		handler:  __s3c4510b_tx_int,
	},
	{
		name:	  "serial1_tx",
		flags:	  SA_INTERRUPT,
		handler:  __s3c4510b_tx_int,
	},
};

static int __s3c4510b_startup(struct uart_port *port)
{
	int status;

//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);

	__s3c4510b_init(port, 19200);

	/*
	 * Allocate the IRQs for TX and RX
	 */
	__tx_irqaction[port->line].dev_id = (void *)port;
	__rx_irqaction[port->line].dev_id = (void *)port;

	status = setup_irq( port->irq, &__tx_irqaction[port->line]);
	if ( status) {
		printk( KERN_ERR "Unabled to hook interrupt for serial %d TX\n", port->line);
		return status;
	}

	status = setup_irq( port->irq+1, &__rx_irqaction[port->line]);
	if ( status) {
		printk( KERN_ERR "Unabled to hook interrupt for serial %d RX\n", port->line);
		return status;
	}

	/*
	 * Finally, enable interrupts
	 */
	spin_lock_irq( &port->lock);
	INT_ENABLE( port->irq);
	INT_ENABLE( port->irq+1);
	spin_unlock_irq( &port->lock);

	return 0;
}

static void __s3c4510b_shutdown(struct uart_port *port)
{
	struct uart_regs *uart = (struct uart_regs *)port->iobase;

	// _DPRINTK("called with port = 0x%08x", (unsigned int) port);

	INT_DISABLE( port->irq);
	INT_DISABLE( port->irq+1);

	/* turn off TX/RX */
	uart->m_ctrl.ui = 0x0;

}

static void __s3c4510b_set_termios(struct uart_port *port, struct termios *termios, struct termios *old)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);

	/**
	 ** Ignore -- only 19200 baud supported
	 **/

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, 19200);

}

static void __s3c4510b_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
//	_DPRINTK("called with port = 0x%08x, state = %u", (unsigned int) port, state);
}

static int __s3c4510b_set_wake(struct uart_port *port, unsigned int state)
{
//	_DPRINTK("called with port = 0x%08x, state = %u", (unsigned int) port, state);
	return 0;
}

static const char *__s3c4510b_type(struct uart_port *port)
{
	return __DRIVER_NAME;
}


static void __s3c4510b_release_port(struct uart_port *port)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
}

static int __s3c4510b_request_port(struct uart_port *port)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
	return 0;
}

static void __s3c4510b_config_port(struct uart_port *port, int config)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
}

static int __s3c4510b_verify_port(struct uart_port *port, struct serial_struct *serial)
{
//	_DPRINTK("called with port = 0x%08x", (unsigned int) port);
	return 0;
}

#if 0
static int __s3c4510b_ioctl(struct uart_port *port, unsigned int cmd, unsigned long arg)
{
//	_DPRINTK("called with port = 0x%08x, cmd %u, arg 0x%08lx", (unsigned int) port, cmd, arg);
	return 0;
}
#endif

static struct uart_ops s3c4510b_pops = {
	tx_empty:	__s3c4510b_tx_empty,
	set_mctrl:	__s3c4510b_set_mctrl,
	get_mctrl:	__s3c4510b_get_mctrl,
	stop_tx:	__s3c4510b_stop_tx,
	start_tx:	__s3c4510b_start_tx,
	send_xchar:     __s3c4510b_send_xchar,
	stop_rx:	__s3c4510b_stop_rx,
	enable_ms:	__s3c4510b_enable_ms,
	break_ctl:	__s3c4510b_break_ctl,
	startup:	__s3c4510b_startup,
	shutdown:	__s3c4510b_shutdown,
	set_termios:    __s3c4510b_set_termios,
	pm:             __s3c4510b_pm,
	set_wake:       __s3c4510b_set_wake,
	type:           __s3c4510b_type,
	release_port:   __s3c4510b_release_port,
	request_port:   __s3c4510b_request_port,
	config_port:    __s3c4510b_config_port,
	verify_port:    __s3c4510b_verify_port,
//	ioctl:          __s3c4510b_ioctl,
};


static struct uart_port __s3c4510b_ports[UART_NR] = {
	{
		iobase:			UART0_BASE,
		line: 			0,
		irq:			INT_UARTTX0,
		fifosize:		1,
		ops:			&s3c4510b_pops,
		ignore_status_mask:	0x0000000F,
		type:			PORT_S3C4510B,
	},
	{
		iobase:			UART1_BASE,
		line:			1,
		irq:			INT_UARTTX1,
		fifosize:		1,
		ops:			&s3c4510b_pops,
		ignore_status_mask:	0x0000000F,
		type:			PORT_S3C4510B,
	}
};

#ifdef CONFIG_SERIAL_S3C4510B_CONSOLE
/************** console driver *****************/

static void __s3c4510b_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = &__s3c4510b_ports[co->index];

	__xmit_string( port, s, count);

}

static int __init __s3c4510b_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 19200;
	int bits = 8;
	int parity = 'n';
	int flow = 0;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(__s3c4510b_ports, UART_NR, co);

//	_DPRINTK("using port = 0x%08x", (unsigned int) port);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	__s3c4510b_init(port, baud);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver __s3c4510b_driver;
static struct console __s3c4510b_console = {
	name:           "ttyS",
	write:		__s3c4510b_console_write,
	device:		uart_console_device,
	setup:		__s3c4510b_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
	data:           &__s3c4510b_driver,
};

static int __init __s3c4510b_console_init(void)
{
	register_console(&__s3c4510b_console);
	return 0;
}

console_initcall(__s3c4510b_console_init);

#endif /* CONFIG_SERIAL_S3C4510B_CONSOLE */


static struct uart_driver __s3c4510b_driver = {
	owner:           THIS_MODULE,
	driver_name:     __DRIVER_NAME,
	dev_name:        "ttyS",
	major:           TTY_MAJOR,
	minor:           64,
	nr:              UART_NR,
#ifdef CONFIG_SERIAL_S3C4510B_CONSOLE
	cons:            &__s3c4510b_console,
#endif
};

static int __init __s3c4510b_serial_init(void)
{

	int    status, i;

//	_DPRINTK("initializing driver with drv = 0x%08x", (unsigned int) &__s3c4510b_driver);

	status = uart_register_driver( &__s3c4510b_driver);

	if ( status) {
		_DPRINTK("uart_register_driver() returned %d", status);
	}

	for ( i = 0; i < UART_NR; i++) {
		status = uart_add_one_port( &__s3c4510b_driver, &__s3c4510b_ports[i]);
		if ( status) {
			_DPRINTK("uart_add_one_port(%d) returned %d", i, status);
		}
	}
	
	return 0;
}

module_init(__s3c4510b_serial_init);

