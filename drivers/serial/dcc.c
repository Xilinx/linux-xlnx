/*
 *  linux/drivers/serial/dcc.c
 *
 *  serial port emulation driver for the JTAG DCC Terminal.
 *
 * DCC(JTAG1) protocol version for JTAG ICE/ICD Debuggers:
 * 	Copyright (C) 2003, 2004, 2005 Hyok S. Choi (hyok.choi@samsung.com)
 * 	SAMSUNG ELECTRONICS Co.,Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   Oct-2003 Hyok S. Choi	Created
 *   Feb-2004 Hyok S. Choi 	Updated for serial_core.c and 2.6 kernel
 *   Apr-2004 Hyok S. Choi 	xmit_string_CR added
 *   Mar-2005 Hyok S. Choi	renamed from T32 to DCC
 *
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/major.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <linux/serial_core.h>

#if 0
	/* real irq interrupt used */
	#define DCC_IRQ_USED
#else
	/* scheduled work used */
	#undef DCC_IRQ_USED
#endif

#ifndef  DCC_IRQ_USED
static struct work_struct dcc_poll_task;
static void dcc_poll(void *data);
#endif

#define UART_NR			1	/* we have only one JTAG port */

#define SERIAL_DCC_NAME	"ttyJ"
#define SERIAL_DCC_MAJOR	4
#define SERIAL_DCC_MINOR	64

static int __inline__ __check_JTAG_RX_FLAG(void)
{
	int __ret=0;
	__asm__ __volatile__(
		"	mrc		p14, 0, %0, c0, c0 	@ read comms control reg\n"
		"	and		%0, %0, #1		@ jtag read buffer status"
		: "=r" (__ret)
		);

	/* if   __ret 	== 0 : no input yet
				== 1 : a character pending */
	return __ret;	
}

static void __inline__ __get_JTAG_RX(volatile char *p)
{
	__asm__ __volatile__(
		"	mrc		p14, 0, r3, c1, c0 	@ read comms data reg to r5\n"
		"	strb 		r3, 	[%0]		@ str a char"
		: /* no output */
		: "r" (p)
		: "r3",  "memory");
}


static int __inline__ __check_JTAG_TX_FLAG(void)
{
	int __ret=0;
	__asm__ __volatile__(
		"	mrc		p14, 0, %0, c0, c0 	@ read comms control reg\n"
		"	and 		%0, %0, #2		@ the read buffer status"
		: "=r" (__ret)
		);

	/* if   __ret 	== 0 : tx is available
				== 2 : tx busy */
	return __ret;	
}

void xmit_string(char *p, int len)
{
#ifndef CONFIG_JTAG_DCC_OUTPUT_DISABLE
	/*
		r0 = string	; string address
		r1 = 2		; state check bit (write)
		r4 = *string	; character
		r7 = 0		; count
	*/
		__asm__ __volatile__(
			"	mov r7, #0\n"
			"1: 	mrc	p14, 0, r3, c0, c0 	@ read comms control reg\n"
			"	and r3, r3, #2			@ the write buffer status\n"
			"	cmp r3, #2			@ is it available?\n"
			"	beq 1b				@ is not, wait till then\n"
			"	ldrb r4, [%0]			@ load a char\n"
			"	mcr p14, 0, r4, c1, c0		@ write it\n"
			"	add %0, %0, #1			@ str address increase one\n"
			"	add r7, r7, #1			@ count increase\n"
			"	cmp r7, %1			@ compare with str length\n"
			"	bne 1b				@ if it is not yet, loop"
			: /* no output register */
			: "r" (p), "r" (len)
			: "r7", "r3", "r4");
#endif
}

void xmit_string_CR(char *p, int len)
{
#ifndef CONFIG_JTAG_DCC_OUTPUT_DISABLE
	/*
		r0 = string	; string address
		r1 = 2		; state check bit (write)
		r4 = *string	; character
		r7 = 0		; count
	*/
		__asm__ __volatile__(
			"	mov r7, #0\n"
			"	ldrb r4, [%0]			@ load a char\n"
			"1: 	mrc	p14, 0, r3, c0, c0 	@ read comms control reg\n"
			"	and r3, r3, #2			@ the write buffer status\n"
			"	cmp r3, #2			@ is it available?\n"
			"	beq 1b				@ is not, wait till then\n"
			"	mcr p14, 0, r4, c1, c0		@ write it\n"
			"	cmp r4, #0x0a			@ is it LF?\n"
			"	bne 2f				@ if it is not, continue\n"
			"	mov r4, #0x0d			@ set the CR\n"
			"	b   1b				@ loop for writing CR\n"			
			"2:	ldrb r4, [%0, #1]!		@ load a char\n"
			"	add r7, r7, #1			@ count increase\n"
			"	cmp r7, %1			@ compare with str length\n"
			"	bne 1b				@ if it is not yet, loop"
			: /* no output register */
			: "r" (p), "r" (len)
			: "r7", "r3", "r4");
#endif
}


static void
dcc_stop_tx(struct uart_port *port)
{
}

static inline void
dcc_transmit_buffer(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	int pendings = uart_circ_chars_pending(xmit);

	if(pendings + xmit->tail > UART_XMIT_SIZE)
	{
		xmit_string(&(xmit->buf[xmit->tail]), UART_XMIT_SIZE - xmit->tail);
		xmit_string(&(xmit->buf[0]), xmit->head);
	} else
		xmit_string(&(xmit->buf[xmit->tail]), pendings);
	
	xmit->tail = (xmit->tail + pendings) & (UART_XMIT_SIZE-1);
        port->icount.tx += pendings;

	if (uart_circ_empty(xmit))
		dcc_stop_tx(port); 
}

static inline void
dcc_transmit_x_char(struct uart_port *port)
{
	xmit_string(&port->x_char, 1);
	port->icount.tx++;
	port->x_char = 0;
}

static void
dcc_start_tx(struct uart_port *port)
{
    dcc_transmit_buffer(port);
}

static void
dcc_stop_rx(struct uart_port *port)
{
}

static void
dcc_enable_ms(struct uart_port *port)
{
}

static inline void
dcc_rx_chars(struct uart_port *port)
{
	unsigned char ch;
	struct tty_struct *tty = port->info->tty;

	/*
	 * check input.
	 * checking JTAG flag is better to resolve the status test.
	 * incount is NOT used for JTAG1 protocol.
	 */

	if (__check_JTAG_RX_FLAG())
		/* if   __ret 	== 0 : no input yet
					== 1 : a character pending */
	{
		/* for JTAG 1 protocol, incount is always 1. */
		__get_JTAG_RX(&ch);
		if (tty->flip.count < TTY_FLIPBUF_SIZE) {
			*tty->flip.char_buf_ptr++ = ch;
			*tty->flip.flag_buf_ptr++ = TTY_NORMAL;
			port->icount.rx++;
			tty->flip.count++;
		} 
		tty_flip_buffer_push(tty);
	}
}

static inline void
dcc_overrun_chars(struct uart_port *port)
{
	port->icount.overrun++;
}

static inline void
dcc_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		dcc_transmit_x_char(port);
		return; 
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		dcc_stop_tx(port);
		return;
	}

	dcc_transmit_buffer(port);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

#ifdef DCC_IRQ_USED /* real IRQ used */
static irqreturn_t
dcc_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	int handled = 0;

	spin_lock(&port->lock);
	
	dcc_rx_chars(port);
	dcc_tx_chars(port);

	handled = 1;
	spin_unlock(&port->lock);
	
	return IRQ_RETVAL(handled);
}

#else /* emulation by scheduled work */
static void
dcc_poll(void *data)
{
	struct uart_port *port = data;

	spin_lock(&port->lock);
	
	dcc_rx_chars(port);
	dcc_tx_chars(port);

	schedule_delayed_work(&dcc_poll_task, 1);

	spin_unlock(&port->lock);
	
}
#endif /* end of DCC_IRQ_USED */

static unsigned int
dcc_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

static unsigned int
dcc_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void
dcc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void
dcc_break_ctl(struct uart_port *port, int break_state)
{
}

static int dcc_startup(struct uart_port *port)
{
#ifdef DCC_IRQ_USED /* real IRQ used */
	int retval;

	/* Allocate the IRQ */
	retval = request_irq(port->irq, dcc_int, SA_INTERRUPT,
			     "serial_dcc", port);
	if (retval)
		return retval;
#else /* emulation */
	/* Initialize the work, and shcedule it. */
	INIT_WORK(&dcc_poll_task, dcc_poll, port);
	schedule_delayed_work(&dcc_poll_task, 1);
#endif

	return 0;
}

static void dcc_shutdown(struct uart_port *port)
{
}

static void
dcc_set_termios(struct uart_port *port, struct termios *termios,
		   struct termios *old)
{
#ifdef DCC_IRQ_USED
	unsigned long flags;
#endif
	unsigned int baud, quot;

	/*
	 * We don't support parity, stop bits, or anything other
	 * than 8 bits, so clear these termios flags.
	 */
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD | CREAD);
	termios->c_cflag |= CS8;

	/*
	 * We don't appear to support any error conditions either.
	 */
	termios->c_iflag &= ~(INPCK | IGNPAR | IGNBRK | BRKINT);

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

#ifdef DCC_IRQ_USED
	spin_lock_irqsave(&port->lock, flags);
#endif

	uart_update_timeout(port, termios->c_cflag, baud);

#ifdef DCC_IRQ_USED
	spin_unlock_irqrestore(&port->lock, flags);
#endif
}

static const char *dcc_type(struct uart_port *port)
{
	return port->type == PORT_DCC_JTAG1 ? "DCC" : NULL;
}

static void dcc_release_port(struct uart_port *port)
{
}

static int dcc_request_port(struct uart_port *port)
{
	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void dcc_config_port(struct uart_port *port, int flags)
{
        if (flags & UART_CONFIG_TYPE) {
                port->type = PORT_DCC_JTAG1;
                dcc_request_port(port);
        }
}
        
/*      
 * verify the new serial_struct (for TIOCSSERIAL).
 */     
static int dcc_verify_port(struct uart_port *port, struct serial_struct *ser)
{
        int ret = 0;
        if (ser->type != PORT_UNKNOWN && ser->type != PORT_DCC_JTAG1)
                ret = -EINVAL;
        if (ser->irq < 0 || ser->irq >= NR_IRQS)
                ret = -EINVAL;
        if (ser->baud_base < 9600)
                ret = -EINVAL;
	return ret;
}

static struct uart_ops dcc_pops = {
	.tx_empty	= dcc_tx_empty,
	.set_mctrl	= dcc_set_mctrl,
	.get_mctrl	= dcc_get_mctrl,
	.stop_tx	= dcc_stop_tx,
	.start_tx	= dcc_start_tx,
	.stop_rx	= dcc_stop_rx,
	.enable_ms	= dcc_enable_ms,
	.break_ctl	= dcc_break_ctl,
	.startup	= dcc_startup,
	.shutdown	= dcc_shutdown,
	.set_termios	= dcc_set_termios,
	.type		= dcc_type,
	.release_port  = dcc_release_port,
	.request_port = dcc_request_port,
	.config_port = dcc_config_port,
	.verify_port = dcc_verify_port,
};

static struct uart_port dcc_ports[UART_NR] = {
	{
		.membase	= (char*)0x12345678,	/* we need these garbages */
		.mapbase	= 0x12345678,		/* for serial_core.c */
		.iotype		= SERIAL_IO_MEM,	
#ifdef DCC_IRQ_USED
		.irq			= INT_N_EXT0,
#else
		.irq			= 0,
#endif
		.uartclk		= 14745600,			 
		.fifosize		= 0,
		.ops			= &dcc_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line			= 0,
	},
};


#ifdef CONFIG_SERIAL_DCC_CONSOLE

static void
dcc_console_write(struct console *co, const char *s, unsigned int count)
{
	xmit_string_CR((char*)s, count);
}

/*
 * Read the current UART setup.
 */
static void __init
dcc_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	*baud = 9600;
	*parity = 'n';
	*bits = 8;
}

static int __init
dcc_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= UART_NR)
		co->index = 0;
	port = &dcc_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		dcc_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver dcc_reg;
static struct console dcc_console = {
	.name		= SERIAL_DCC_NAME,
	.write		= dcc_console_write,
	.device		= uart_console_device,
	.setup		= dcc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &dcc_reg,
};

static int __init dcc_console_init(void)
{
	register_console(&dcc_console);
	return 0;
}
console_initcall(dcc_console_init);

#define DCC_CONSOLE		&dcc_console
#else
#define DCC_CONSOLE		NULL
#endif

static struct uart_driver dcc_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= SERIAL_DCC_NAME,
	.dev_name		= SERIAL_DCC_NAME,
	.major			= SERIAL_DCC_MAJOR,
	.minor			= SERIAL_DCC_MINOR,
	.nr			= UART_NR,
	.cons			= DCC_CONSOLE,
};

static int __init
dcc_init(void)
{
	int ret;

	printk(KERN_INFO "DCC: JTAG1 Serial emulation driver driver $Revision: 1.3 $\n");

	ret = uart_register_driver(&dcc_reg);
	if (ret == 0) {
		int i;

		for (i = 0; i < UART_NR; i++)
			uart_add_one_port(&dcc_reg, &dcc_ports[i]);
	}
	return ret;
}

__initcall(dcc_init);

MODULE_DESCRIPTION("DCC(JTAG1) JTAG debugger console emulation driver");
MODULE_AUTHOR("Hyok S. Choi <hyok.choi@samsung.com>");
MODULE_SUPPORTED_DEVICE("ttyJ");
MODULE_LICENSE("GPL");
