/*
 * Synopsys DesignWare 8250 driver.
 *
 * Copyright 2011 Picochip, Jamie Iles.
 * Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Synopsys DesignWare 8250 has an extra feature whereby it detects if the
 * LCR is written whilst busy.  If it is, then a busy detect interrupt is
 * raised, the LCR needs to be rewritten and the uart status register read.
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include <asm/byteorder.h>

#include "8250.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_USR	0x1f /* UART Status Register */
#define DW_UART_CPR	0xf4 /* Component Parameter Register */
#define DW_UART_UCV	0xf8 /* UART Component Version */

/* Component Parameter Register bits */
#define DW_UART_CPR_ABP_DATA_WIDTH	(3 << 0)
#define DW_UART_CPR_AFCE_MODE		(1 << 4)
#define DW_UART_CPR_THRE_MODE		(1 << 5)
#define DW_UART_CPR_SIR_MODE		(1 << 6)
#define DW_UART_CPR_SIR_LP_MODE		(1 << 7)
#define DW_UART_CPR_ADDITIONAL_FEATURES	(1 << 8)
#define DW_UART_CPR_FIFO_ACCESS		(1 << 9)
#define DW_UART_CPR_FIFO_STAT		(1 << 10)
#define DW_UART_CPR_SHADOW		(1 << 11)
#define DW_UART_CPR_ENCODED_PARMS	(1 << 12)
#define DW_UART_CPR_DMA_EXTRA		(1 << 13)
#define DW_UART_CPR_FIFO_MODE		(0xff << 16)
/* Helper for fifo size calculation */
#define DW_UART_CPR_FIFO_SIZE(a)	(((a >> 16) & 0xff) * 16)


struct dw8250_data {
	u8			usr_reg;
	int			last_mcr;
	int			line;
	struct clk		*clk;
	struct uart_8250_dma	dma;
};

static inline int dw8250_modify_msr(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = p->private_data;

	/* If reading MSR, report CTS asserted when auto-CTS/RTS enabled */
	if (offset == UART_MSR && d->last_mcr & UART_MCR_AFE) {
		value |= UART_MSR_CTS;
		value &= ~UART_MSR_DCTS;
	}

	return value;
}

static void dw8250_force_idle(struct uart_port *p)
{
	serial8250_clear_and_reinit_fifos(container_of
					  (p, struct uart_8250_port, port));
	(void)p->serial_in(p, UART_RX);
}

static void dw8250_serial_out(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = p->private_data;

	if (offset == UART_MCR)
		d->last_mcr = value;

	writeb(value, p->membase + (offset << p->regshift));

	/* Make sure LCR write wasn't ignored */
	if (offset == UART_LCR) {
		int tries = 1000;
		while (tries--) {
			unsigned int lcr = p->serial_in(p, UART_LCR);
			if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
				return;
			dw8250_force_idle(p);
			writeb(value, p->membase + (UART_LCR << p->regshift));
		}
		dev_err(p->dev, "Couldn't set LCR to %d\n", value);
	}
}

static unsigned int dw8250_serial_in(struct uart_port *p, int offset)
{
	unsigned int value = readb(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

/* Read Back (rb) version to ensure register access ording. */
static void dw8250_serial_out_rb(struct uart_port *p, int offset, int value)
{
	dw8250_serial_out(p, offset, value);
	dw8250_serial_in(p, UART_LCR);
}

static void dw8250_serial_out32(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = p->private_data;

	if (offset == UART_MCR)
		d->last_mcr = value;

	writel(value, p->membase + (offset << p->regshift));

	/* Make sure LCR write wasn't ignored */
	if (offset == UART_LCR) {
		int tries = 1000;
		while (tries--) {
			unsigned int lcr = p->serial_in(p, UART_LCR);
			if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
				return;
			dw8250_force_idle(p);
			writel(value, p->membase + (UART_LCR << p->regshift));
		}
		dev_err(p->dev, "Couldn't set LCR to %d\n", value);
	}
}

static unsigned int dw8250_serial_in32(struct uart_port *p, int offset)
{
	unsigned int value = readl(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

static int dw8250_handle_irq(struct uart_port *p)
{
	struct dw8250_data *d = p->private_data;
	unsigned int iir = p->serial_in(p, UART_IIR);

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/* Clear the USR */
		(void)p->serial_in(p, d->usr_reg);

		return 1;
	}

	return 0;
}

static void
dw8250_do_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(port->dev);

	serial8250_do_pm(port, state, old);

	if (state)
		pm_runtime_put_sync_suspend(port->dev);
}

static bool dw8250_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw8250_data *data = param;

	return chan->chan_id == data->dma.tx_chan_id ||
	       chan->chan_id == data->dma.rx_chan_id;
}

static void dw8250_setup_port(struct uart_8250_port *up)
{
	struct uart_port	*p = &up->port;
	u32			reg = readl(p->membase + DW_UART_UCV);

	/*
	 * If the Component Version Register returns zero, we know that
	 * ADDITIONAL_FEATURES are not enabled. No need to go any further.
	 */
	if (!reg)
		return;

	dev_dbg_ratelimited(p->dev, "Designware UART version %c.%c%c\n",
		(reg >> 24) & 0xff, (reg >> 16) & 0xff, (reg >> 8) & 0xff);

	reg = readl(p->membase + DW_UART_CPR);
	if (!reg)
		return;

	/* Select the type based on fifo */
	if (reg & DW_UART_CPR_FIFO_MODE) {
		p->type = PORT_16550A;
		p->flags |= UPF_FIXED_TYPE;
		p->fifosize = DW_UART_CPR_FIFO_SIZE(reg);
		up->tx_loadsz = p->fifosize;
		up->capabilities = UART_CAP_FIFO;
	}

	if (reg & DW_UART_CPR_AFCE_MODE)
		up->capabilities |= UART_CAP_AFE;
}

static int dw8250_probe_of(struct uart_port *p,
			   struct dw8250_data *data)
{
	struct device_node	*np = p->dev->of_node;
	u32			val;
	bool has_ucv = true;

	if (of_device_is_compatible(np, "cavium,octeon-3860-uart")) {
#ifdef __BIG_ENDIAN
		/*
		 * Low order bits of these 64-bit registers, when
		 * accessed as a byte, are 7 bytes further down in the
		 * address space in big endian mode.
		 */
		p->membase += 7;
#endif
		p->serial_out = dw8250_serial_out_rb;
		p->flags = ASYNC_SKIP_TEST | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
		p->type = PORT_OCTEON;
		data->usr_reg = 0x27;
		has_ucv = false;
	} else if (!of_property_read_u32(np, "reg-io-width", &val)) {
		switch (val) {
		case 1:
			break;
		case 4:
			p->iotype = UPIO_MEM32;
			p->serial_in = dw8250_serial_in32;
			p->serial_out = dw8250_serial_out32;
			break;
		default:
			dev_err(p->dev, "unsupported reg-io-width (%u)\n", val);
			return -EINVAL;
		}
	}
	if (has_ucv)
		dw8250_setup_port(container_of(p, struct uart_8250_port, port));

	if (!of_property_read_u32(np, "reg-shift", &val))
		p->regshift = val;

	/* clock got configured through clk api, all done */
	if (p->uartclk)
		return 0;

	/* try to find out clock frequency from DT as fallback */
	if (of_property_read_u32(np, "clock-frequency", &val)) {
		dev_err(p->dev, "clk or clock-frequency not defined\n");
		return -EINVAL;
	}
	p->uartclk = val;

	return 0;
}

#ifdef CONFIG_ACPI
static int dw8250_probe_acpi(struct uart_8250_port *up,
			     struct dw8250_data *data)
{
	const struct acpi_device_id *id;
	struct uart_port *p = &up->port;

	dw8250_setup_port(up);

	id = acpi_match_device(p->dev->driver->acpi_match_table, p->dev);
	if (!id)
		return -ENODEV;

	p->iotype = UPIO_MEM32;
	p->serial_in = dw8250_serial_in32;
	p->serial_out = dw8250_serial_out32;
	p->regshift = 2;

	if (!p->uartclk)
		p->uartclk = (unsigned int)id->driver_data;

	up->dma = &data->dma;

	up->dma->rxconf.src_maxburst = p->fifosize / 4;
	up->dma->txconf.dst_maxburst = p->fifosize / 4;

	return 0;
}
#else
static inline int dw8250_probe_acpi(struct uart_8250_port *up,
				    struct dw8250_data *data)
{
	return -ENODEV;
}
#endif /* CONFIG_ACPI */

static int dw8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	struct dw8250_data *data;
	int err;

	if (!regs || !irq) {
		dev_err(&pdev->dev, "no registers/irq defined\n");
		return -EINVAL;
	}

	spin_lock_init(&uart.port.lock);
	uart.port.mapbase = regs->start;
	uart.port.irq = irq->start;
	uart.port.handle_irq = dw8250_handle_irq;
	uart.port.pm = dw8250_do_pm;
	uart.port.type = PORT_8250;
	uart.port.flags = UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;
	uart.port.dev = &pdev->dev;

	uart.port.membase = devm_ioremap(&pdev->dev, regs->start,
					 resource_size(regs));
	if (!uart.port.membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->usr_reg = DW_UART_USR;
	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(data->clk)) {
		clk_prepare_enable(data->clk);
		uart.port.uartclk = clk_get_rate(data->clk);
	}

	data->dma.rx_chan_id = -1;
	data->dma.tx_chan_id = -1;
	data->dma.rx_param = data;
	data->dma.tx_param = data;
	data->dma.fn = dw8250_dma_filter;

	uart.port.iotype = UPIO_MEM;
	uart.port.serial_in = dw8250_serial_in;
	uart.port.serial_out = dw8250_serial_out;
	uart.port.private_data = data;

	if (pdev->dev.of_node) {
		err = dw8250_probe_of(&uart.port, data);
		if (err)
			return err;
	} else if (ACPI_HANDLE(&pdev->dev)) {
		err = dw8250_probe_acpi(&uart, data);
		if (err)
			return err;
	} else {
		return -ENODEV;
	}

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0)
		return data->line;

	platform_set_drvdata(pdev, data);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int dw8250_remove(struct platform_device *pdev)
{
	struct dw8250_data *data = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	serial8250_unregister_port(data->line);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int dw8250_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int dw8250_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME
static int dw8250_runtime_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	return 0;
}

static int dw8250_runtime_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->clk))
		clk_prepare_enable(data->clk);

	return 0;
}
#endif

static const struct dev_pm_ops dw8250_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw8250_suspend, dw8250_resume)
	SET_RUNTIME_PM_OPS(dw8250_runtime_suspend, dw8250_runtime_resume, NULL)
};

static const struct of_device_id dw8250_of_match[] = {
	{ .compatible = "snps,dw-apb-uart" },
	{ .compatible = "cavium,octeon-3860-uart" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw8250_of_match);

static const struct acpi_device_id dw8250_acpi_match[] = {
	{ "INT33C4", 0 },
	{ "INT33C5", 0 },
	{ "INT3434", 0 },
	{ "INT3435", 0 },
	{ "80860F0A", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dw8250_acpi_match);

static struct platform_driver dw8250_platform_driver = {
	.driver = {
		.name		= "dw-apb-uart",
		.owner		= THIS_MODULE,
		.pm		= &dw8250_pm_ops,
		.of_match_table	= dw8250_of_match,
		.acpi_match_table = ACPI_PTR(dw8250_acpi_match),
	},
	.probe			= dw8250_probe,
	.remove			= dw8250_remove,
};

module_platform_driver(dw8250_platform_driver);

MODULE_AUTHOR("Jamie Iles");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synopsys DesignWare 8250 serial port driver");
