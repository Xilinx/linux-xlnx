// SPDX-License-Identifier: GPL-2.0
/*
 * uartlite.c: Serial driver for Xilinx uartlite serial controller
 *
 * Copyright (C) 2006 Peter Korsgaard <jacmet@sunsite.dk>
 * Copyright (C) 2007 Secret Lab Technologies Ltd.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#define ULITE_NAME		"ttyULR"
#define ULITE_DRV_NAME		"uartlite_rs485"
#define ULITE_MAJOR		205
#define ULITE_MINOR		187
#define ULITE_NR_UARTS		CONFIG_SERIAL_UARTLITE_NR_UARTS

/* ---------------------------------------------------------------------
 * Register definitions
 *
 * For register details see datasheet:
 * https://www.xilinx.com/support/documentation/ip_documentation/opb_uartlite.pdf
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
#define UART_AUTOSUSPEND_TIMEOUT	3000	/* ms */

struct uartlite_data {
	const struct uartlite_reg_ops *reg_ops;
	struct clk *clk;
};

struct uartlite_reg_ops {
	u32 (*in)(void __iomem *addr);
	void (*out)(u32 val, void __iomem *addr);
};

static u32 uartlite_inbe32(void __iomem *addr)
{
	return ioread32be(addr);
}

static void uartlite_outbe32(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static const struct uartlite_reg_ops uartlite_be = {
	.in = uartlite_inbe32,
	.out = uartlite_outbe32,
};

static u32 uartlite_inle32(void __iomem *addr)
{
	return ioread32(addr);
}

static void uartlite_outle32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static const struct uartlite_reg_ops uartlite_le = {
	.in = uartlite_inle32,
	.out = uartlite_outle32,
};

static inline u32 uart_in32(u32 offset, struct uart_port *port)
{
	struct uartlite_data *pdata = port->private_data;

	return pdata->reg_ops->in(port->membase + offset);
}

static inline void uart_out32(u32 val, u32 offset, struct uart_port *port)
{
	struct uartlite_data *pdata = port->private_data;

	pdata->reg_ops->out(val, port->membase + offset);
}

static struct uart_port ulite_ports[ULITE_NR_UARTS];

/* ---------------------------------------------------------------------
 * Core UART driver operations
 */

static int ulite_receive(struct uart_port *port, int stat)
{
	struct tty_port *tport = &port->state->port;
	unsigned char ch = 0;
	char flag = TTY_NORMAL;

	if ((stat & (ULITE_STATUS_RXVALID | ULITE_STATUS_OVERRUN
		     | ULITE_STATUS_FRAME)) == 0)
		return 0;

	/* stats */
	if (stat & ULITE_STATUS_RXVALID) {
		port->icount.rx++;
		ch = uart_in32(ULITE_RX, port);

		if (stat & ULITE_STATUS_PARITY)
			port->icount.parity++;
	}

	if (stat & ULITE_STATUS_OVERRUN)
		port->icount.overrun++;

	if (stat & ULITE_STATUS_FRAME)
		port->icount.frame++;

	/* drop byte with parity error if IGNPAR specified */
	if (stat & port->ignore_status_mask & ULITE_STATUS_PARITY)
		stat &= ~ULITE_STATUS_RXVALID;

	stat &= port->read_status_mask;

	if (stat & ULITE_STATUS_PARITY)
		flag = TTY_PARITY;

	stat &= ~port->ignore_status_mask;

	if (stat & ULITE_STATUS_RXVALID)
		tty_insert_flip_char(tport, ch, flag);

	if (stat & ULITE_STATUS_FRAME)
		tty_insert_flip_char(tport, 0, TTY_FRAME);

	if (stat & ULITE_STATUS_OVERRUN)
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);

	return 1;
}

static int ulite_transmit(struct uart_port *port, int stat)
{
	struct circ_buf *xmit  = &port->state->xmit;

	if (stat & ULITE_STATUS_TXFULL)
		return 0;

	if (port->x_char) {
		uart_out32(port->x_char, ULITE_TX, port);
		port->x_char = 0;
		port->icount.tx++;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return 0;

	uart_out32(xmit->buf[xmit->tail], ULITE_TX, port);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	/* wake up */
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	return 1;
}

static irqreturn_t ulite_isr(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	int stat, busy, n = 0;
	unsigned long flags;

	do {
		spin_lock_irqsave(&port->lock, flags);
		stat = uart_in32(ULITE_STATUS, port);
		busy  = ulite_receive(port, stat);
		busy |= ulite_transmit(port, stat);
		spin_unlock_irqrestore(&port->lock, flags);
		n++;
	} while (busy);

	/* work done? */
	if (n > 1) {
		tty_flip_buffer_push(&port->state->port);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static unsigned int ulite_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&port->lock, flags);
	ret = uart_in32(ULITE_STATUS, port);
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
	ulite_transmit(port, uart_in32(ULITE_STATUS, port));
}

static void ulite_stop_rx(struct uart_port *port)
{
	/* don't forward any more data (like !CREAD) */
	port->ignore_status_mask = ULITE_STATUS_RXVALID | ULITE_STATUS_PARITY
		| ULITE_STATUS_FRAME | ULITE_STATUS_OVERRUN;
}

static void ulite_break_ctl(struct uart_port *port, int ctl)
{
	/* N/A */
}

static int ulite_startup(struct uart_port *port)
{
	struct uartlite_data *pdata = port->private_data;
	int ret;

	ret = clk_enable(pdata->clk);
	if (ret) {
		dev_err(port->dev, "Failed to enable clock\n");
		return ret;
	}

	ret = request_irq(port->irq, ulite_isr, IRQF_SHARED | IRQF_TRIGGER_RISING,
			  ULITE_DRV_NAME, port);
	if (ret)
		return ret;

	uart_out32(ULITE_CONTROL_RST_RX | ULITE_CONTROL_RST_TX,
		   ULITE_CONTROL, port);
	uart_out32(ULITE_CONTROL_IE, ULITE_CONTROL, port);

	return 0;
}

static void ulite_shutdown(struct uart_port *port)
{
	struct uartlite_data *pdata = port->private_data;

	uart_out32(0, ULITE_CONTROL, port);
	free_irq(port->irq, port);
	clk_disable(pdata->clk);
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
	return port->type == PORT_UARTLITE ? ULITE_DRV_NAME : NULL;
}

static void ulite_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, ULITE_REGION);
	iounmap(port->membase);
	port->membase = NULL;
}

static int ulite_request_port(struct uart_port *port)
{
	struct uartlite_data *pdata = port->private_data;
	int ret;

	pr_debug("ulite console: port=%p; port->mapbase=%llx\n",
		 port, (unsigned long long)port->mapbase);

	if (!request_mem_region(port->mapbase, ULITE_REGION, ULITE_DRV_NAME)) {
		dev_err(port->dev, "Memory region busy\n");
		return -EBUSY;
	}

	port->membase = ioremap(port->mapbase, ULITE_REGION);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, ULITE_REGION);
		return -EBUSY;
	}

	pdata->reg_ops = &uartlite_be;
	uart_out32(ULITE_CONTROL_RST_TX, ULITE_CONTROL, port);
	ret = uart_in32(ULITE_STATUS, port);
	/* Endianness detection */
	if ((ret & ULITE_STATUS_TXEMPTY) != ULITE_STATUS_TXEMPTY)
		pdata->reg_ops = &uartlite_le;

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

static void ulite_pm(struct uart_port *port, unsigned int state,
		     unsigned int oldstate)
{
	int ret;

	if (!state) {
		ret = pm_runtime_get_sync(port->dev);
		if (ret < 0)
			dev_err(port->dev, "Failed to enable clocks\n");
	} else {
		pm_runtime_mark_last_busy(port->dev);
		pm_runtime_put_autosuspend(port->dev);
	}
}

static int ulite_config_rs485(struct uart_port *port,
			      struct serial_rs485 *rs485conf)
{
	port->rs485 = *rs485conf;

	if (rs485conf->flags & SER_RS485_ENABLED)
		dev_dbg(port->dev, "Setting UART to RS485\n");
	else
		dev_dbg(port->dev, "Setting UART to RS232\n");

	return 0;
}

static const struct uart_ops ulite_ops = {
	.tx_empty	= ulite_tx_empty,
	.set_mctrl	= ulite_set_mctrl,
	.get_mctrl	= ulite_get_mctrl,
	.stop_tx	= ulite_stop_tx,
	.start_tx	= ulite_start_tx,
	.stop_rx	= ulite_stop_rx,
	.break_ctl	= ulite_break_ctl,
	.startup	= ulite_startup,
	.shutdown	= ulite_shutdown,
	.set_termios	= ulite_set_termios,
	.type		= ulite_type,
	.release_port	= ulite_release_port,
	.request_port	= ulite_request_port,
	.config_port	= ulite_config_port,
	.verify_port	= ulite_verify_port,
	.pm		= ulite_pm,
};

static struct uart_driver ulite_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= ULITE_DRV_NAME,
	.dev_name	= ULITE_NAME,
	.major		= ULITE_MAJOR,
	.minor		= ULITE_MINOR,
	.nr		= ULITE_NR_UARTS,
};

/* ---------------------------------------------------------------------
 * Port assignment functions (mapping devices to uart_port structures)
 */

/** ulite_assign: register a uartlite device with the driver
 *
 * @dev: pointer to device structure
 * @id: requested id number.  Pass -1 for automatic port assignment
 * @base: base address of uartlite registers
 * @irq: irq number for uartlite
 * @pdata: private data for uartlite
 *
 * Returns: 0 on success, <0 otherwise
 */
static int ulite_assign(struct device *dev, int id, phys_addr_t base, int irq,
			struct uartlite_data *pdata)
{
	struct uart_port *port;
	int rc;

	/* if id = -1; then scan for a free id and use that */
	if (id < 0) {
		for (id = 0; id < ULITE_NR_UARTS; id++)
			if (ulite_ports[id].mapbase == 0)
				break;
	}
	if (id < 0 || id >= ULITE_NR_UARTS) {
		dev_err(dev, "%s%i too large\n", ULITE_NAME, id);
		return -EINVAL;
	}

	if ((ulite_ports[id].mapbase) && (ulite_ports[id].mapbase != base)) {
		dev_err(dev, "cannot assign to %s%i; it is already in use\n",
			ULITE_NAME, id);
		return -EBUSY;
	}

	port = &ulite_ports[id];

	spin_lock_init(&port->lock);
	port->fifosize = 16;
	port->regshift = 2;
	port->iotype = UPIO_MEM;
	port->iobase = 1; /* mark port in use */
	port->mapbase = base;
	port->membase = NULL;
	port->ops = &ulite_ops;
	port->irq = irq;
	port->flags = UPF_BOOT_AUTOCONF;
	port->dev = dev;
	port->type = PORT_UNKNOWN;
	port->line = id;
	port->private_data = pdata;

	port->rs485.flags |= SER_RS485_ENABLED;
	port->rs485_config = ulite_config_rs485;

	dev_set_drvdata(dev, port);

	/* Register the port */
	rc = uart_add_one_port(&ulite_uart_driver, port);
	if (rc) {
		dev_err(dev, "uart_add_one_port() failed; err=%i\n", rc);
		port->mapbase = 0;
		dev_set_drvdata(dev, NULL);
		return rc;
	}

	return 0;
}

/** ulite_release: register a uartlite device with the driver
 *
 * @dev: pointer to device structure
 */
static int ulite_release(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	int rc = 0;

	if (port) {
		rc = uart_remove_one_port(&ulite_uart_driver, port);
		dev_set_drvdata(dev, NULL);
		port->mapbase = 0;
	}

	return rc;
}

/**
 * ulite_suspend - Stop the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 always.
 */
static int __maybe_unused ulite_suspend(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	if (port)
		uart_suspend_port(&ulite_uart_driver, port);

	return 0;
}

/**
 * ulite_resume - Resume the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 on success, errno otherwise.
 */
static int __maybe_unused ulite_resume(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	if (port)
		uart_resume_port(&ulite_uart_driver, port);

	return 0;
}

static int __maybe_unused ulite_runtime_suspend(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct uartlite_data *pdata = port->private_data;

	clk_disable(pdata->clk);
	return 0;
};

static int __maybe_unused ulite_runtime_resume(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct uartlite_data *pdata = port->private_data;
	int ret;

	ret = clk_enable(pdata->clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}
	return 0;
}

/* ---------------------------------------------------------------------
 * Platform bus binding
 */

static const struct dev_pm_ops ulite_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ulite_suspend, ulite_resume)
	SET_RUNTIME_PM_OPS(ulite_runtime_suspend,
			   ulite_runtime_resume, NULL)
};

#if defined(CONFIG_OF)
/* Match table for of_platform binding */
static const struct of_device_id ulite_of_match[] = {
	{ .compatible = "xlnx,axi-uartlite-rs485", },
	{}
};
MODULE_DEVICE_TABLE(of, ulite_of_match);
#endif /* CONFIG_OF */

static int ulite_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct uartlite_data *pdata;
	int irq, ret;
	int id = pdev->id;
#ifdef CONFIG_OF
	const __be32 *prop;

	prop = of_get_property(pdev->dev.of_node, "port-number", NULL);
	if (prop)
		id = be32_to_cpup(prop);
#endif
	pdata = devm_kzalloc(&pdev->dev, sizeof(struct uartlite_data),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	pdata->clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(pdata->clk)) {
		if (PTR_ERR(pdata->clk) != -ENOENT)
			return PTR_ERR(pdata->clk);

		/*
		 * Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		pdata->clk = NULL;
	}

	ret = clk_prepare_enable(pdata->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare clock\n");
		return ret;
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, UART_AUTOSUSPEND_TIMEOUT);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (!ulite_uart_driver.state) {
		dev_dbg(&pdev->dev, "uartlite: calling uart_register_driver()\n");
		ret = uart_register_driver(&ulite_uart_driver);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to register driver\n");
			clk_disable_unprepare(pdata->clk);
			return ret;
		}
	}

	ret = ulite_assign(&pdev->dev, id, res->start, irq, pdata);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return ret;
}

static int ulite_remove(struct platform_device *pdev)
{
	struct uart_port *port = dev_get_drvdata(&pdev->dev);
	struct uartlite_data *pdata = port->private_data;
	int rc;

	clk_disable_unprepare(pdata->clk);
	rc = ulite_release(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	return rc;
}

static struct platform_driver ulite_platform_driver = {
	.probe = ulite_probe,
	.remove = ulite_remove,
	.driver = {
		.name  = ULITE_DRV_NAME,
		.of_match_table = of_match_ptr(ulite_of_match),
		.pm = &ulite_pm_ops,
	},
};

/* ---------------------------------------------------------------------
 * Module setup/teardown
 */

static int __init ulite_init(void)
{
	pr_debug("uartlite: calling platform_driver_register()\n");
	return platform_driver_register(&ulite_platform_driver);
}

static void __exit ulite_exit(void)
{
	platform_driver_unregister(&ulite_platform_driver);
	if (ulite_uart_driver.state)
		uart_unregister_driver(&ulite_uart_driver);
}

module_init(ulite_init);
module_exit(ulite_exit);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Xilinx uartlite serial driver");
MODULE_LICENSE("GPL");
