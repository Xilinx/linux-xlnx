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
	XUL_STATUS  = 8,
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

static inline void __xul_set_reg(struct uart_port *port, enum XulRegister reg, u32 val)
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
	__xul_set_control(port, __xul_get_control(port) | XUL_CONTROL_ENABLE_INTR);
}

static inline void _xul_disable_interrupt(struct uart_port *port)
{
	__xul_set_control(port, __xul_get_control(port) & ~XUL_CONTROL_ENABLE_INTR);
}

static inline void _xul_reset_rx_fifo(struct uart_port *port)
{
	__xul_set_control(port, __xul_get_control(port) | XUL_CONTROL_RST_RX_FIFO);
}

static inline void _xul_reset_tx_fifo(struct uart_port *port)
{
	__xul_set_control(port, __xul_get_control(port) | XUL_CONTROL_RST_TX_FIFO);
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
	return (u8)__xul_get_rx_fifo(port);
}

static inline void _xul_putchar(struct uart_port *port, int c)
{
	while (_xul_is_tx_fifo_full(port))
		/* NOP */;
	__xul_set_tx_fifo(port, c);
}

static irqreturn_t _xul_irq_handler(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	struct uart_info *info = port->info;
	struct tty_struct *tty = info->tty;

	pr_debug("Got interrupt: %d for tty @0x%p\n", irq, info->tty);
	pr_debug("		status: %#x\n", __xul_get_status(port));

	if (_xul_has_valid_data(port)) {
		while (_xul_has_valid_data(port)) {
			u8 c = _xul_getchar(port);
			pr_debug("================> '%#x'\n", c);
			if (tty) {
				tty_insert_flip_char(tty, c, TTY_NORMAL);
			}
		}
		if (tty) {
			tty_flip_buffer_push(tty);
		}
	}

	return IRQ_HANDLED;
}

static unsigned int xul_op_tx_empty(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
	return 0;
}

static void xul_op_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	pr_debug("%s: Not Supported\n", __FUNCTION__);
}

static unsigned int xul_op_get_mctrl(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
	return TIOCM_CAR;
}

static void xul_op_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static void xul_op_start_tx(struct uart_port *port, unsigned int tty_start)
{
	struct uart_info *info = port->info;
	struct circ_buf *circ = &info->xmit;

	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);

	while (uart_circ_chars_pending(circ)) {
		_xul_putchar(port, circ->buf[circ->tail]);
		circ->tail = (circ->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}

static void xul_op_send_xchar(struct uart_port *port, char ch)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static void xul_op_stop_rx(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static void xul_op_enable_ms(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static void xul_op_break_ctl(struct uart_port *port, int ctl)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static int xul_op_startup(struct uart_port *port)
{
	unsigned long flags;

	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);

	local_irq_save(flags);
	_xul_reset_rx_fifo(port);
	_xul_enable_interrupt(port);
	local_irq_restore(flags);

	return 0;
}

static void xul_op_shutdown(struct uart_port *port)
{
	unsigned long flags;

	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);

	local_irq_save(flags);
	_xul_disable_interrupt(port);
	local_irq_restore(flags);
}

static void xul_op_set_termios(struct uart_port *port, struct ktermios *new, struct ktermios *old)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static void xul_op_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
	pr_debug("%s: Not Supported\n", __FUNCTION__);
}

static int xul_op_set_wake(struct uart_port *port, unsigned int state)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
	return 0;
}

static const char *xul_op_type(struct uart_port *port)
{
	return "Xilinx OPB UART Lite";
}

static void xul_op_release_port(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
}

static int xul_op_request_port(struct uart_port *port)
{
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
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
		printk(KERN_ERR "XUL: Cannot map new port at phys %#lx\n", port->mapbase);
		err = -ENOMEM;
		goto err_remap;
	}

	/* finally, request the irq for the port */
	err = request_irq(port->irq, _xul_irq_handler, 0, "uartlite", port);
	if (err) {
		printk(KERN_ERR "XUL: Cannot acquire given irq (%d) for new port at phys %#lx\n",
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
	pr_debug("port @ %#lx, line %d: %s\n", port->mapbase, port->line, __FUNCTION__);
	return 0;
}

static struct uart_ops xul_ops = {
	.tx_empty	= xul_op_tx_empty,
	.set_mctrl	= xul_op_set_mctrl,
	.get_mctrl	= xul_op_get_mctrl,
	.stop_tx	= xul_op_stop_tx,
	.start_tx	= xul_op_start_tx,
	.send_xchar	= xul_op_send_xchar,
	.stop_rx	= xul_op_stop_rx,
	.enable_ms	= xul_op_enable_ms,
	.break_ctl	= xul_op_break_ctl,
	.startup	= xul_op_startup,
	.shutdown	= xul_op_shutdown,
	.set_termios	= xul_op_set_termios,
	.pm		= xul_op_pm,
	.set_wake	= xul_op_set_wake,
	.type		= xul_op_type,
	.release_port	= xul_op_release_port,
	.request_port	= xul_op_request_port,
	.config_port	= xul_op_config_port,
	.verify_port	= xul_op_verify_port,
	.ioctl		= NULL,
};

/* just define the port for console. every other ports on uart lite
 * needs to be manually binded */
static struct uart_port xul_port = {
	.mapbase	= XPAR_UARTLITE_0_BASEADDR,
	.irq		= XPAR_UARTLITE_0_IRQ,
	.iotype		= UPIO_MEM32,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_UARTLITE,
	.ops		= &xul_ops,
};

#ifdef CONFIG_SERIAL_XILINX_UARTLITE_CONSOLE

static void xul_console_write(struct console *console, const char *str, unsigned len)
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
	.name		= XUL_SERIAL_NAME,
	.write		= xul_console_write,
	.read		= xul_console_read,
	.device		= uart_console_device,
	.unblank	= xul_console_unblank,
	.setup		= xul_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.cflag		= 0,
	.data		= &xul_driver,
	.next		= NULL,
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
	.owner		= THIS_MODULE,
	.driver_name	= XUL_SERIAL_NAME,
	.dev_name	= XUL_SERIAL_NAME,
	.major		= XUL_SERIAL_MAJOR,
	.minor		= XUL_SERIAL_MINORS,
	.nr		= XUL_SERIAL_NR,
	.cons		= XUL_SERIAL_CONSOLE,
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
