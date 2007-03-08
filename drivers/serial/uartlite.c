/*
 * drivers/char/uartlite.c -- Xilinx OPB UART Lite driver
 *
 *  Copyright (C) 2005	Atmark Techno, Inc. <yashi@atmark-techno.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Note:
 *
 * - We don't want to hangup serial line if console is active
 * - don't use any functions prefixed with __ in functions *NOT* prefixed with _
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/console.h>


#include <asm/io.h>
#include <asm/termios.h>

/* #define XUL_SERIAL_MAJOR	204 */
/* #define XUL_SERIAL_MINORS	100 */
/* #define XUL_SERIAL_NAME		"ttySX" */
#define XUL_SERIAL_MAJOR	4
#define XUL_SERIAL_MINORS	64
#define XUL_SERIAL_NAME		"ttyS"
#define XUL_SERIAL_NR		1

enum XulRegister {
	XUL_RX_FIFO = 0,
	XUL_TX_FIFO = 4,
	XUL_STATUS = 8,
	XUL_CONTROL = 12,
};

#define XUL_STATUS_PAR_ERROR	      (1<<7)
#define XUL_STATUS_FRAME_ERROR	      (1<<6)
#define XUL_STATUS_OVERUN_ERROR	      (1<<5)
#define XUL_STATUS_INTR_ENABLED	      (1<<4)
#define XUL_STATUS_TX_FIFO_FULL	      (1<<3)
#define XUL_STATUS_TX_FIFO_EMPTY      (1<<2)
#define XUL_STATUS_RX_FIFO_FULL	      (1<<1)
#define XUL_STATUS_RX_FIFO_VALID_DATA (1<<0)

#define XUL_CONTROL_ENABLE_INTR	      (1<<4)
#define XUL_CONTROL_RST_RX_FIFO	      (1<<1)
#define XUL_CONTROL_RST_TX_FIFO	      (1<<0)

/* these in_be32 and out_be32 will be renamed to ioread32be and iowrite32be */
static inline u32 __xul_get_reg(struct uart_port *port, enum XulRegister reg)
{
	void __iomem *ioaddr = port->membase;

	return ioread32(ioaddr + reg);
}

static inline void __xul_set_reg(struct uart_port *port, enum XulRegister reg,
				 u32 val)
{
	void __iomem *ioaddr = port->membase;

	iowrite32(val, ioaddr + reg);
}

static inline u32 __xul_get_rx_fifo(struct uart_port *port)
{
	return __xul_get_reg(port, XUL_RX_FIFO);
}

static inline u32 __xul_get_status(struct uart_port *port)
{
	return __xul_get_reg(port, XUL_STATUS);
}

static inline u32 __xul_get_control(struct uart_port *port)
{
	return __xul_get_reg(port, XUL_CONTROL);
}

static inline void __xul_set_tx_fifo(struct uart_port *port, u32 val)
{
	__xul_set_reg(port, XUL_TX_FIFO, val);
}

static inline void __xul_set_status(struct uart_port *port, u32 val)
{
	__xul_set_reg(port, XUL_STATUS, val);
}

static inline void __xul_set_control(struct uart_port *port, u32 val)
{
	__xul_set_reg(port, XUL_CONTROL, val);
}


static inline void _xul_enable_interrupt(struct uart_port *port)
{
	__xul_set_control(port,
			  __xul_get_control(port) | XUL_CONTROL_ENABLE_INTR);
}

static inline int _xul_has_valid_data(struct uart_port *port)
{
	return __xul_get_status(port) & XUL_STATUS_RX_FIFO_VALID_DATA;
}

static inline int _xul_is_tx_fifo_full(struct uart_port *port)
{
	return __xul_get_status(port) & XUL_STATUS_TX_FIFO_FULL;
}

static inline u8 _xul_getchar(struct uart_port *port)
{
	return (u8) __xul_get_rx_fifo(port);
}

static inline void _xul_putchar(struct uart_port *port, int c)
{
	while (_xul_is_tx_fifo_full(port));
	__xul_set_tx_fifo(port, c);
}

static irqreturn_t _xul_irq_handler(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *) dev_id;
	struct uart_info *info = port->info;
	struct tty_struct *tty = info->tty;

	pr_debug("Got interrupt: %d for tty @0x%p\n", irq, info->tty);
	pr_debug("		status: %#x\n", __xul_get_status(port));

	if (_xul_has_valid_data(port)) {
		while (_xul_has_valid_data(port)) {
			u8 c = _xul_getchar(port);

			pr_debug("================> '%#x'\n", c);
			tty_insert_flip_char(tty, c, TTY_NORMAL);
		}
		tty_flip_buffer_push(tty);
	}

	return IRQ_HANDLED;
}

static unsigned int xul_op_tx_empty(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
	return 0;
}

static void xul_op_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	pr_debug("%s: Not Supported\n", __FUNCTION__);
}

static unsigned int xul_op_get_mctrl(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
	return TIOCM_CAR;
}

static void xul_op_stop_tx(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_start_tx(struct uart_port *port)
{
	struct uart_info *info = port->info;
	struct circ_buf *circ = &info->xmit;

	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);

	while (uart_circ_chars_pending(circ)) {
		_xul_putchar(port, circ->buf[circ->tail]);
		circ->tail = (circ->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}

static void xul_op_send_xchar(struct uart_port *port, char ch)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_stop_rx(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_enable_ms(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_break_ctl(struct uart_port *port, int ctl)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static int xul_op_startup(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);

	_xul_enable_interrupt(port);

	return 0;
}

static void xul_op_shutdown(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_set_termios(struct uart_port *port, struct ktermios *new,
			       struct ktermios *old)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static void xul_op_pm(struct uart_port *port, unsigned int state,
		      unsigned int oldstate)
{
	pr_debug("%s: Not Supported\n", __FUNCTION__);
}

static int xul_op_set_wake(struct uart_port *port, unsigned int state)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
	return 0;
}

static const char *xul_op_type(struct uart_port *port)
{
	return "Xilinx OPB UART Lite";
}

static void xul_op_release_port(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
}

static int xul_op_request_port(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
	return 0;
}

#define REGION_SIZE (256)

static void xul_op_config_port(struct uart_port *port, int flags)
{
	int err;
	struct resource *res;

	port->type = PORT_UARTLITE;

	/* sanity checks */
	if ((port->iotype != UPIO_MEM32) || (!port->mapbase)) {
		err = -ENXIO;
		goto err;
	}

	/* first reserve physical memory region */
	res = request_mem_region(port->mapbase, REGION_SIZE, "uartlite");
	if (!res) {
		err = -EBUSY;
		goto err;
	}

	/* then map the region to virtual memory */
	port->membase = ioremap_nocache(port->mapbase, REGION_SIZE);
	if (!port->membase) {
		printk(KERN_ERR "XUL: Cannot map new port at phys %#lx\n",
		       port->mapbase);
		err = -ENOMEM;
		goto err_remap;
	}

/* FIXME */
	/* finally, request the irq for the port */
	err = request_irq(port->irq, _xul_irq_handler, 0, "uartlite", port);
	if (err) {
		printk(KERN_ERR
		       "XUL: Cannot acquire given irq (%d) for new port at phys %#lx\n",
		       port->irq, port->mapbase);
		err = -ENODEV;
		goto err_irq;
	}
	return;

      err_irq:
	iounmap(port->membase);
	port->membase = NULL;
      err_remap:
	release_mem_region(port->mapbase, REGION_SIZE);
      err:
	if (err < 0) {
		/* FIXME: remove this port from the list */
		pr_debug("%s: oops %d\n", __FUNCTION__, err);
	}
	return;
}

static int xul_op_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line,
		 __FUNCTION__);
	return 0;
}

static struct uart_ops xul_ops = {
	.tx_empty = xul_op_tx_empty,
	.set_mctrl = xul_op_set_mctrl,
	.get_mctrl = xul_op_get_mctrl,
	.stop_tx = xul_op_stop_tx,
	.start_tx = xul_op_start_tx,
	.send_xchar = xul_op_send_xchar,
	.stop_rx = xul_op_stop_rx,
	.enable_ms = xul_op_enable_ms,
	.break_ctl = xul_op_break_ctl,
	.startup = xul_op_startup,
	.shutdown = xul_op_shutdown,
	.set_termios = xul_op_set_termios,
	.pm = xul_op_pm,
	.set_wake = xul_op_set_wake,
	.type = xul_op_type,
	.release_port = xul_op_release_port,
	.request_port = xul_op_request_port,
	.config_port = xul_op_config_port,
	.verify_port = xul_op_verify_port,
	.ioctl = NULL,
};

/* just define the port for console. every other ports on uart lite
 * needs to be manually binded */
static struct uart_port xul_port = {
	.mapbase = CONFIG_XILINX_UARTLITE_0_BASEADDR,
	.irq = CONFIG_XILINX_UARTLITE_0_IRQ,
	.iotype = UPIO_MEM32,
	.flags = UPF_BOOT_AUTOCONF,
	.type = PORT_UARTLITE,
	.ops = &xul_ops,
};

#ifdef CONFIG_SERIAL_XILINX_UARTLITE_CONSOLE

static void xul_console_write(struct console *console, const char *str,
			      unsigned len)
{
	int i;
	unsigned long flags;

	local_irq_save(flags);

	for (i = 0; i < len; i++) {
		if (str[i] == '\n')
			_xul_putchar(&xul_port, '\r');
		_xul_putchar(&xul_port, str[i]);
	}

	local_irq_restore(flags);
}

static int xul_console_read(struct console *console, char *str, unsigned len)
{
	return 0;
}

static void xul_console_unblank(void)
{
	pr_debug("%s\n", __FUNCTION__);
}

static int __init xul_console_setup(struct console *console, char *options)
{
	pr_debug("%s\n", __FUNCTION__);
	spin_lock_init(&xul_port.lock);
	return 0;
}

static struct uart_driver xul_driver;
static struct console xul_console = {
	.name = XUL_SERIAL_NAME,
	.write = xul_console_write,
	.read = xul_console_read,
	.device = uart_console_device,
	.unblank = xul_console_unblank,
	.setup = xul_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.cflag = 0,
	.data = &xul_driver,
	.next = NULL,
};

static int __init xul_console_init(void)
{
	xul_port.membase = ioremap_nocache(xul_port.mapbase, 256);
	printk(KERN_INFO "Console: Xilinx OPB UART Lite\n");
	register_console(&xul_console);
	return 0;
}

console_initcall(xul_console_init);

#define XUL_SERIAL_CONSOLE	&xul_console
#else
#define XUL_SERIAL_CONSOLE	NULL
#endif /* CONFIG_XILINX_UARTLITE_CONSOLE */

static struct uart_driver xul_driver = {
	.owner = THIS_MODULE,
	.driver_name = XUL_SERIAL_NAME,
	.dev_name = XUL_SERIAL_NAME,
	.major = XUL_SERIAL_MAJOR,
	.minor = XUL_SERIAL_MINORS,
	.nr = XUL_SERIAL_NR,
	.cons = XUL_SERIAL_CONSOLE,
};

static void xul_exit(void)
{
}

static int xul_init(void)
{
	int res;

	res = uart_register_driver(&xul_driver);
	if (res)
		return res;

	res = uart_add_one_port(&xul_driver, &xul_port);
	if (res)
		uart_unregister_driver(&xul_driver);

	return res;
}

module_init(xul_init);
module_exit(xul_exit);


MODULE_AUTHOR("Yasushi SHOJI <yashi@atmark-techno.com>");
MODULE_DESCRIPTION("Xilinx OPB UART Lite Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(XUL_SERIAL_MAJOR);
/*
 * uartlite.c: Serial driver for Xilinx uartlite serial controller
 *
 * Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#define ULITE_MAJOR		204
#define ULITE_MINOR		187
#define ULITE_NR_UARTS		4

/* For register details see datasheet:
   http://www.xilinx.com/bvdocs/ipcenter/data_sheet/opb_uartlite.pdf
*/
#define ULITE_RX		0x00
#define ULITE_TX		0x04
#define ULITE_STATUS		0x08
#define ULITE_CONTROL		0x0c

#define ULITE_REGION		16

#define ULITE_STATUS_RXVALID	0x01
#define ULITE_STATUS_RXFULL	0x02
#define ULITE_STATUS_TXEMPTY	0x04
#define ULITE_STATUS_TXFULL	0x08
#define ULITE_STATUS_IE		0x10
#define ULITE_STATUS_OVERRUN	0x20
#define ULITE_STATUS_FRAME	0x40
#define ULITE_STATUS_PARITY	0x80

#define ULITE_CONTROL_RST_TX	0x01
#define ULITE_CONTROL_RST_RX	0x02
#define ULITE_CONTROL_IE	0x10


static struct uart_port ports[ULITE_NR_UARTS];

static int ulite_receive(struct uart_port *port, int stat)
{
	struct tty_struct *tty = port->info->tty;
	unsigned char ch = 0;
	char flag = TTY_NORMAL;

	if ((stat & (ULITE_STATUS_RXVALID | ULITE_STATUS_OVERRUN
		     | ULITE_STATUS_FRAME)) == 0)
		return 0;

	/* stats */
	if (stat & ULITE_STATUS_RXVALID) {
		port->icount.rx++;
		ch = readb(port->membase + ULITE_RX);

		if (stat & ULITE_STATUS_PARITY)
			port->icount.parity++;
	}

	if (stat & ULITE_STATUS_OVERRUN)
		port->icount.overrun++;

	if (stat & ULITE_STATUS_FRAME)
		port->icount.frame++;


	/* drop byte with parity error if IGNPAR specificed */
	if (stat & port->ignore_status_mask & ULITE_STATUS_PARITY)
		stat &= ~ULITE_STATUS_RXVALID;

	stat &= port->read_status_mask;

	if (stat & ULITE_STATUS_PARITY)
		flag = TTY_PARITY;


	stat &= ~port->ignore_status_mask;

	if (stat & ULITE_STATUS_RXVALID)
		tty_insert_flip_char(tty, ch, flag);

	if (stat & ULITE_STATUS_FRAME)
		tty_insert_flip_char(tty, 0, TTY_FRAME);

	if (stat & ULITE_STATUS_OVERRUN)
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);

	return 1;
}

static int ulite_transmit(struct uart_port *port, int stat)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (stat & ULITE_STATUS_TXFULL)
		return 0;

	if (port->x_char) {
		writeb(port->x_char, port->membase + ULITE_TX);
		port->x_char = 0;
		port->icount.tx++;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return 0;

	writeb(xmit->buf[xmit->tail], port->membase + ULITE_TX);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	/* wake up */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return 1;
}

static irqreturn_t ulite_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *) dev_id;
	int busy;

	do {
		int stat = readb(port->membase + ULITE_STATUS);

		busy = ulite_receive(port, stat);
		busy |= ulite_transmit(port, stat);
	} while (busy);

	tty_flip_buffer_push(port->info->tty);

	return IRQ_HANDLED;
}

static unsigned int ulite_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&port->lock, flags);
	ret = readb(port->membase + ULITE_STATUS);
	spin_unlock_irqrestore(&port->lock, flags);

	return ret & ULITE_STATUS_TXEMPTY ? TIOCSER_TEMT : 0;
}

static unsigned int ulite_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void ulite_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* N/A */
}

static void ulite_stop_tx(struct uart_port *port)
{
	/* N/A */
}

static void ulite_start_tx(struct uart_port *port)
{
	ulite_transmit(port, readb(port->membase + ULITE_STATUS));
}

static void ulite_stop_rx(struct uart_port *port)
{
	/* don't forward any more data (like !CREAD) */
	port->ignore_status_mask = ULITE_STATUS_RXVALID | ULITE_STATUS_PARITY
		| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;
}

static void ulite_enable_ms(struct uart_port *port)
{
	/* N/A */
}

static void ulite_break_ctl(struct uart_port *port, int ctl)
{
	/* N/A */
}

static int ulite_startup(struct uart_port *port)
{
	int ret;

	ret = request_irq(port->irq, ulite_isr,
			  IRQF_DISABLED | IRQF_SAMPLE_RANDOM, "uartlite", port);
	if (ret)
		return ret;

	writeb(ULITE_CONTROL_RST_RX | ULITE_CONTROL_RST_TX,
	       port->membase + ULITE_CONTROL);
	writeb(ULITE_CONTROL_IE, port->membase + ULITE_CONTROL);

	return 0;
}

static void ulite_shutdown(struct uart_port *port)
{
	writeb(0, port->membase + ULITE_CONTROL);
	readb(port->membase + ULITE_CONTROL);	/* dummy */
	free_irq(port->irq, port);
}

static void ulite_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud;

	spin_lock_irqsave(&port->lock, flags);

	port->read_status_mask = ULITE_STATUS_RXVALID | ULITE_STATUS_OVERRUN
		| ULITE_STATUS_TXFULL;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |=
			ULITE_STATUS_PARITY | ULITE_STATUS_FRAME;

	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= ULITE_STATUS_PARITY
			| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;

	/* ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |=
			ULITE_STATUS_RXVALID | ULITE_STATUS_PARITY
			| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;

	/* update timeout */
	baud = uart_get_baud_rate(port, termios, old, 0, 460800);
	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *ulite_type(struct uart_port *port)
{
	return port->type == PORT_UARTLITE ? "uartlite" : NULL;
}

static void ulite_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, ULITE_REGION);
	iounmap(port->membase);
	port->membase = 0;
}

static int ulite_request_port(struct uart_port *port)
{
	if (!request_mem_region(port->mapbase, ULITE_REGION, "uartlite")) {
		dev_err(port->dev, "Memory region busy\n");
		return -EBUSY;
	}

	port->membase = ioremap(port->mapbase, ULITE_REGION);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, ULITE_REGION);
		return -EBUSY;
	}

	return 0;
}

static void ulite_config_port(struct uart_port *port, int flags)
{
	if (!ulite_request_port(port))
		port->type = PORT_UARTLITE;
}

static int ulite_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}

static struct uart_ops ulite_ops = {
	.tx_empty = ulite_tx_empty,
	.set_mctrl = ulite_set_mctrl,
	.get_mctrl = ulite_get_mctrl,
	.stop_tx = ulite_stop_tx,
	.start_tx = ulite_start_tx,
	.stop_rx = ulite_stop_rx,
	.enable_ms = ulite_enable_ms,
	.break_ctl = ulite_break_ctl,
	.startup = ulite_startup,
	.shutdown = ulite_shutdown,
	.set_termios = ulite_set_termios,
	.type = ulite_type,
	.release_port = ulite_release_port,
	.request_port = ulite_request_port,
	.config_port = ulite_config_port,
	.verify_port = ulite_verify_port
};

#ifdef CONFIG_SERIAL_UARTLITE_CONSOLE
static void ulite_console_wait_tx(struct uart_port *port)
{
	int i;

	/* wait up to 10ms for the character(s) to be sent */
	for (i = 0; i < 10000; i++) {
		if (readb(port->membase + ULITE_STATUS) & ULITE_STATUS_TXEMPTY)
			break;
		udelay(1);
	}
}

static void ulite_console_putchar(struct uart_port *port, int ch)
{
	ulite_console_wait_tx(port);
	writeb(ch, port->membase + ULITE_TX);
}

static void ulite_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = &ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	if (oops_in_progress) {
		locked = spin_trylock_irqsave(&port->lock, flags);
	}
	else
		spin_lock_irqsave(&port->lock, flags);

	/* save and disable interrupt */
	ier = readb(port->membase + ULITE_STATUS) & ULITE_STATUS_IE;
	writeb(0, port->membase + ULITE_CONTROL);

	uart_console_write(port, s, count, ulite_console_putchar);

	ulite_console_wait_tx(port);

	/* restore interrupt state */
	if (ier)
		writeb(ULITE_CONTROL_IE, port->membase + ULITE_CONTROL);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int __init ulite_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= ULITE_NR_UARTS)
		return -EINVAL;

	port = &ports[co->index];

	/* not initialized yet? */
	if (!port->membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver ulite_uart_driver;

static struct console ulite_console = {
	.name = "ttyUL",
	.write = ulite_console_write,
	.device = uart_console_device,
	.setup = ulite_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,		/* Specified on the cmdline (e.g. console=ttyUL0 ) */
	.data = &ulite_uart_driver,
};

static int __init ulite_console_init(void)
{
	register_console(&ulite_console);
	return 0;
}

console_initcall(ulite_console_init);

#endif /* CONFIG_SERIAL_UARTLITE_CONSOLE */

static struct uart_driver ulite_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "uartlite",
	.dev_name = "ttyUL",
	.major = ULITE_MAJOR,
	.minor = ULITE_MINOR,
	.nr = ULITE_NR_UARTS,
#ifdef CONFIG_SERIAL_UARTLITE_CONSOLE
	.cons = &ulite_console,
#endif
};

static int __devinit ulite_probe(struct platform_device *pdev)
{
	struct resource *res, *res2;
	struct uart_port *port;

	if (pdev->id < 0 || pdev->id >= ULITE_NR_UARTS)
		return -EINVAL;

	if (ports[pdev->id].membase)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	res2 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res2)
		return -ENODEV;

	port = &ports[pdev->id];

	port->fifosize = 16;
	port->regshift = 2;
	port->iotype = UPIO_MEM;
	port->iobase = 1;	/* mark port in use */
	port->mapbase = res->start;
	port->membase = 0;
	port->ops = &ulite_ops;
	port->irq = res2->start;
	port->flags = UPF_BOOT_AUTOCONF;
	port->dev = &pdev->dev;
	port->type = PORT_UNKNOWN;
	port->line = pdev->id;

	uart_add_one_port(&ulite_uart_driver, port);
	platform_set_drvdata(pdev, port);

	return 0;
}

static int ulite_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (port)
		uart_remove_one_port(&ulite_uart_driver, port);

	/* mark port as free */
	port->membase = 0;

	return 0;
}

static struct platform_driver ulite_platform_driver = {
	.probe = ulite_probe,
	.remove = ulite_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "uartlite",
		   },
};

int __init ulite_init(void)
{
	int ret;

	ret = uart_register_driver(&ulite_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&ulite_platform_driver);
	if (ret)
		uart_unregister_driver(&ulite_uart_driver);

	return ret;
}

void __exit ulite_exit(void)
{
	platform_driver_unregister(&ulite_platform_driver);
	uart_unregister_driver(&ulite_uart_driver);
}

module_init(ulite_init);
module_exit(ulite_exit);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Xilinx uartlite serial driver");
MODULE_LICENSE("GPL");
