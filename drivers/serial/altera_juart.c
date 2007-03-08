/*
 *  linux/drivers/serial/altera_juart.c
 *
 *  Driver for Altera JTAG UART core with Avalon interface
 *
 *  Copyright 2004 Microtronix Datacom Ltd
 *
 *  Based on linux/drivers/serial/amba.c, which is
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
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
 * Written by Wentao Xu <wentao@microtronix.com>
 * Jun/20/2005   DGT Microtronix Datacom - support for
 *                    arch/kernel/start.c - boot time error
 *                    message(s).
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/irq.h>

#include <asm/io.h>

#if defined(CONFIG_SERIAL_AJUART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#include <asm/altera_juart.h>               //;dgt;

#define UART_NR		1

#define SERIAL_JUART_MAJOR	232
#define SERIAL_JUART_MINOR	16
#define SERIAL_JUART_NR		UART_NR

#define JUART_ISR_PASS_LIMIT	16

/*
 * Register maps 
 */
#define JTAG_UARTDR		0
#define JTAG_UARTCR		4

/* 
 * Control Register bit definition
 */
#define JTAG_UARTCR_RIE		1
#define JTAG_UARTCR_TIE		2
#define JTAG_UARTCR_RIS		(1<<8)
#define JTAG_UARTCR_TIS		(1<<9)
#define JTAG_UARTCR_AC		(1<<10)

/* 
 * Data Register
 */
#define JTAG_UARTDR_RVALID	(1<<15)
#define JTAG_UARTDR_DATA	255
/*
 * Access macros for the JTAG UARTs
 */
#define UART_GET_DR(p)	readl((p)->membase + JTAG_UARTDR)
#define UART_PUT_DR(p, c)	writel((c), (p)->membase + JTAG_UARTDR)
#define UART_GET_CR(p)		readl((p)->membase + JTAG_UARTCR)
#define UART_PUT_CR(p,c)	writel((c), (p)->membase + JTAG_UARTCR)

#define UART_PORT_SIZE		8


/*
 * We wrap our port structure around the generic uart_port.
 */
struct juart_port {
	struct uart_port	port;
};

static void jtaguart_stop_tx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr &= ~JTAG_UARTCR_TIE;
	UART_PUT_CR(port, cr);
}

static void jtaguart_start_tx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr |= JTAG_UARTCR_TIE;
	UART_PUT_CR(port, cr);
}

static void jtaguart_stop_rx(struct uart_port *port)
{
	unsigned int cr;

	cr = UART_GET_CR(port);
	cr &= ~(JTAG_UARTCR_RIE);
	UART_PUT_CR(port, cr);
}

static void jtaguart_enable_ms(struct uart_port *port)
{
}

static void
#ifdef SUPPORT_SYSRQ
jtaguart_rx_chars(struct uart_port *port)
#else
jtaguart_rx_chars(struct uart_port *port)
#endif
{
	struct tty_struct *tty = port->info->tty;
	unsigned int data, max_count = 256;
	unsigned char flag;

	do {
		data = UART_GET_DR(port);
		if (!(data & JTAG_UARTDR_RVALID))
			return;

		port->icount.rx++;

		if(!tty)
			goto clear_and_exit;

		flag = TTY_NORMAL;

		if (!uart_handle_sysrq_char(port, data & JTAG_UARTDR_DATA)) {
		  tty_insert_flip_char(tty, data & JTAG_UARTDR_DATA, flag);
		}

	} while ((data >> 16) && (max_count--));
	

	tty_schedule_flip(tty);

clear_and_exit:
	return;
}

static void jtaguart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		UART_PUT_DR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		jtaguart_stop_tx(port);
		return;
	}

	do {
		UART_PUT_DR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (UART_GET_CR(port) >> 16);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		jtaguart_stop_tx(port);
}

static irqreturn_t jtaguart_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int status, pass_counter = JUART_ISR_PASS_LIMIT;

	status = UART_GET_CR(port);
	do {
		if (status & JTAG_UARTCR_RIS)
#ifdef SUPPORT_SYSRQ
			jtaguart_rx_chars(port, regs);
#else
			jtaguart_rx_chars(port);
#endif
		if (status & JTAG_UARTCR_TIS)
			jtaguart_tx_chars(port);

		if (pass_counter-- == 0)
			break;

		status = UART_GET_CR(port);
	} while ((status & JTAG_UARTCR_RIS) ||
	((status & JTAG_UARTCR_TIS) && (status & JTAG_UARTCR_TIE)));

	return IRQ_HANDLED;
}

static unsigned int jtaguart_tx_empty(struct uart_port *port)
{
	return ((UART_GET_CR(port)>>16) > 0 ) ? TIOCSER_TEMT : 0;
}

static unsigned int jtaguart_get_mctrl(struct uart_port *port)
{
	return (TIOCM_CAR | TIOCM_DSR | TIOCM_CTS);
}

static void jtaguart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void jtaguart_break_ctl(struct uart_port *port, int break_state)
{
}

static int jtaguart_startup(struct uart_port *port)
{
	//struct juart_port *uap = (struct juart_port *)port;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, jtaguart_int, 0, "jtag_uart", port);
	if (retval)
		return retval;

	/*
	 * Finally, enable reception interrupts
	 */
	UART_PUT_CR(port, JTAG_UARTCR_RIE);

	return 0;
}

static void jtaguart_shutdown(struct uart_port *port)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, port);

	/*
	 * disable all interrupts, disable the port
	 */
	UART_PUT_CR(port, 0);
}

static void
jtaguart_set_termios(struct uart_port *port, struct termios *termios,
		     struct termios *old)
{
	port->read_status_mask = 0;
	port->ignore_status_mask = 0;
}

static const char *jtaguart_type(struct uart_port *port)
{
	return port->type == PORT_JTAG_UART ? "jtag_uart" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void jtaguart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int jtaguart_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE, "jtag_uart")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void jtaguart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_JTAG_UART;
		jtaguart_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int jtaguart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_JTAG_UART)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops juart_pops = {
	.tx_empty	= jtaguart_tx_empty,
	.set_mctrl	= jtaguart_set_mctrl,
	.get_mctrl	= jtaguart_get_mctrl,
	.stop_tx	= jtaguart_stop_tx,
	.start_tx	= jtaguart_start_tx,
	.stop_rx	= jtaguart_stop_rx,
	.enable_ms	= jtaguart_enable_ms,
	.break_ctl	= jtaguart_break_ctl,
	.startup	= jtaguart_startup,
	.shutdown	= jtaguart_shutdown,
	.set_termios	= jtaguart_set_termios,
	.type		= jtaguart_type,
	.release_port	= jtaguart_release_port,
	.request_port	= jtaguart_request_port,
	.config_port	= jtaguart_config_port,
	.verify_port	= jtaguart_verify_port,
};

static struct juart_port juart_ports[UART_NR] = {
	{
		.port	= {
			.membase	= (char*)na_jtag_uart,
			.mapbase	= na_jtag_uart,
			.iotype		= SERIAL_IO_MEM,
			.irq		= na_jtag_uart_irq,
			.uartclk	= 14745600,
			.fifosize	= 64,
			.ops		= &juart_pops,
			.flags		= ASYNC_BOOT_AUTOCONF,
			.line		= 0,
		},
	}
};

#ifdef CONFIG_SERIAL_AJUART_CONSOLE

//;dgt;static
 void
jtaguart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = &juart_ports[co->index].port;
	unsigned int status, old_cr;
	int i;

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_cr = UART_GET_CR(port);
	UART_PUT_CR(port, 0);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_CR(port);
		} while (!(status>>16));
		UART_PUT_DR(port, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_CR(port);
			} while (!(status>>16));
			UART_PUT_DR(port, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the CR
	 *  We don't have to wait until fifo is empty since
	 *  the operation is irrevocable: all the chars can
	 *  be deemed as "done"
	 */
	/*do {
		status = UART_GET_CR(port);
	} while ((status>>16)<port->fifosize);*/
	UART_PUT_CR(port, old_cr);
}

static void __init
jtaguart_console_get_options(struct uart_port *port, int *baud,
			     int *parity, int *bits)
{
	*parity = 'n';
	*bits = 8;
	*baud = port->uartclk / 16;
}

static int __init jtaguart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	port = &juart_ports[co->index].port;

	jtaguart_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver juart_reg;
//;dgt;static
 struct console juart_console = {
	.name		= "ttyJ",
	.write		= jtaguart_console_write,
	.device		= uart_console_device,
	.setup		= jtaguart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &juart_reg,
};

static int __init jtaguart_console_init(void)
{
	register_console(&juart_console);
	return 0;
}
console_initcall(jtaguart_console_init);

#define JTAG_CONSOLE	&juart_console
#else
#define JTAG_CONSOLE	NULL
#endif

static struct uart_driver juart_reg = {
	.owner			= THIS_MODULE,
	.driver_name	= "ttyJ",
	.dev_name		= "ttyJ",
	.major			= SERIAL_JUART_MAJOR,
	.minor			= SERIAL_JUART_MINOR,
	.nr				= SERIAL_JUART_NR,
	.cons			= JTAG_CONSOLE,
};

static int __init jtaguart_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: JTAG UART driver $Revision: 1.3 $\n");

	ret = uart_register_driver(&juart_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < UART_NR; i++)
			uart_add_one_port(&juart_reg, &juart_ports[i].port);
	}
	return ret;
}

static void __exit jtaguart_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++)
		uart_remove_one_port(&juart_reg, &juart_ports[i].port);

	uart_unregister_driver(&juart_reg);
}

module_init(jtaguart_init);
module_exit(jtaguart_exit);

MODULE_AUTHOR("Microtronix Datacom");
MODULE_DESCRIPTION("Driver for Altera JTAG UART $Revision 1.0");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV(SERIAL_JUART_MAJOR, SERIAL_JUART_MINOR);
