/****************************************************************************/

/*
 *	mcf.c -- Freescale ColdFire UART driver
 *
 *	(C) Copyright 2003-2006, Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/****************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/nettel.h>
#include <asm/io.h>

/****************************************************************************/

#ifdef DEBUG
#define	dprintk(x...)	printk(x)
#else
#define	dprintk(...)	do { } while (0)
#endif

/****************************************************************************/

/*
 *	On some ColdFire parts the IRQs for serial use are completely
 *	software programmable. On others they are pretty much fixed.
 *	On the programmed CPU's we arbitarily choose 73 (known not to
 *	interfere with anything else programed).
 */
#if defined(MCFINT_VECBASE) && defined(MCFINT_UART0)
#define	IRQBASE		(MCFINT_VECBASE + MCFINT_UART0)
#else
#define	IRQBASE		73
#endif

/*
 *	Some boards implement the DTR/DCD lines using GPIO lines, most
 *	don't. Dummy out the access macros for those that don't. Those
 *	that do should define these macros somewhere in there board
 *	specific inlude files.
 */
#if !defined(mcf_getppdcd)
#define	mcf_getppdcd(p)		(1)
#endif
#if !defined(mcf_getppdtr)
#define	mcf_getppdtr(p)		(1)
#endif
#if !defined(mcf_setppdtr)
#define	mcf_setppdtr(p,v)	do { } while (0)
#endif

/****************************************************************************/

/*
 *	Local per-uart structure.
 */
struct mcf_uart {
	struct uart_port	port;
	unsigned int		sigs;		/* Local copy of line sigs */
	unsigned char		imr;		/* Local IMR mirror */
};

/****************************************************************************/

static inline unsigned int mcf_getreg(struct uart_port *port, unsigned int reg)
{
	return readb(port->membase + reg);
}

static inline void mcf_setreg(struct uart_port *port, unsigned int reg, unsigned int val)
{
	writeb(val, port->membase + reg);
}

/****************************************************************************/

unsigned int mcf_tx_empty(struct uart_port *port)
{
	dprintk("mcf_tx_empty(port=%x)\n", (int)port);

	return (mcf_getreg(port, MCFUART_USR) & MCFUART_USR_TXEMPTY) ? TIOCSER_TEMT : 0;
}

/****************************************************************************/

unsigned int mcf_get_mctrl(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;
	unsigned int sigs;

	dprintk(" mcf_get_mctrl(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	sigs = (mcf_getreg(port, MCFUART_UIPR) & MCFUART_UIPR_CTS) ? 0 : TIOCM_CTS;
	sigs |= (pp->sigs & TIOCM_RTS);
	sigs |= (mcf_getppdcd(port->line) ? TIOCM_CD : 0);
	sigs |= (mcf_getppdtr(port->line) ? TIOCM_DTR : 0);
	spin_unlock_irqrestore(&port->lock, flags);
	return sigs;
}

/****************************************************************************/

void mcf_set_mctrl(struct uart_port *port, unsigned int sigs)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_set_mctrl(port=%x,sigs=%x)\n", (int)port, sigs);

	spin_lock_irqsave(&port->lock, flags);
	pp->sigs = sigs;
	mcf_setppdtr(port->line, (sigs & TIOCM_DTR));
	if (sigs & TIOCM_RTS)
		mcf_setreg(port, MCFUART_UOP1, MCFUART_UOP_RTS);
	else
		mcf_setreg(port, MCFUART_UOP0, MCFUART_UOP_RTS);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_start_tx(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_start_tx(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	pp->imr |= MCFUART_UIR_TXREADY;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_stop_tx(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_stop_tx(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	pp->imr &= ~MCFUART_UIR_TXREADY;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_start_rx(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_start_rx(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	pp->imr |= MCFUART_UIR_RXREADY;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_stop_rx(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_stop_rx(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	pp->imr &= ~MCFUART_UIR_RXREADY;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;

	dprintk("mcf_break_ctl(port=%x,break_state=%x)\n", (int)port, break_state);

	spin_lock_irqsave(&port->lock, flags);
	if (break_state == -1)
		mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDBREAKSTART);
	else
		mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDBREAKSTOP);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_enable_ms(struct uart_port *port)
{
	dprintk("mcf_enable_ms(port=%x)\n", (int)port);
}

/****************************************************************************/

int mcf_startup(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_startup(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);
	
	/* Reset UART, get it into known state... */
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETRX);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETTX);

	/* Enable the UART transmitter and receiver */
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE);

	/* Enable RX interrupts now */
	pp->imr = MCFUART_UIR_RXREADY;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

/****************************************************************************/

void mcf_shutdown(struct uart_port *port)
{
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned long flags;

	dprintk("mcf_shutdown(port=%x)\n", (int)port);

	spin_lock_irqsave(&port->lock, flags);

	/* Disable all interrupts now */
	pp->imr = 0;
	mcf_setreg(port, MCFUART_UIMR, pp->imr);

	/* Disable UART transmitter and receiver */
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETRX);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETTX);

	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

void mcf_set_termios(struct uart_port *port, struct termios *termios,
	struct termios *old)
{
	unsigned long flags;
	unsigned int baud, baudclk;
	unsigned char mr1, mr2;

	dprintk("mcf_set_termios(port=%x,termios=%x,old=%x)\n", (int)port, (int)termios, (int)old);

	baud = uart_get_baud_rate(port, termios, old, 0, 230400);
	baudclk = ((MCF_BUSCLK / baud) + 16) / 32;

	mr1 = MCFUART_MR1_RXIRQRDY | MCFUART_MR1_RXERRCHAR;
	mr2 = 0;

	switch (termios->c_cflag & CSIZE) {
	case CS5: mr1 |= MCFUART_MR1_CS5; break;
	case CS6: mr1 |= MCFUART_MR1_CS6; break;
	case CS7: mr1 |= MCFUART_MR1_CS7; break;
	case CS8:
	default:  mr1 |= MCFUART_MR1_CS8; break;
	}

	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				mr1 |= MCFUART_MR1_PARITYMARK;
			else
				mr1 |= MCFUART_MR1_PARITYSPACE;
		} else {
			if (termios->c_cflag & PARODD)
				mr1 |= MCFUART_MR1_PARITYODD;
			else
				mr1 |= MCFUART_MR1_PARITYEVEN;
		}
	} else {
		mr1 |= MCFUART_MR1_PARITYNONE;
	}

	if (termios->c_cflag & CSTOPB)
		mr2 |= MCFUART_MR2_STOP2;
	else
		mr2 |= MCFUART_MR2_STOP1;

	if (termios->c_cflag & CRTSCTS) {
		mr1 |= MCFUART_MR1_RXRTS;
		mr2 |= MCFUART_MR2_TXCTS;
	}

#if 0
	printk("%s(%d): mr1=%x mr2=%x baudclk=%x\n", __FILE__, __LINE__,
		mr1, mr2, baudclk);
#endif
	spin_lock_irqsave(&port->lock, flags);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETRX);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETTX);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETMRPTR);
	mcf_setreg(port, MCFUART_UMR, mr1);
	mcf_setreg(port, MCFUART_UMR, mr2);
	mcf_setreg(port, MCFUART_UBG1, (baudclk & 0xff00) >> 8);
	mcf_setreg(port, MCFUART_UBG2, (baudclk & 0xff));
	mcf_setreg(port, MCFUART_UCSR, MCFUART_UCSR_RXCLKTIMER | MCFUART_UCSR_TXCLKTIMER);
	mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_rx_chars(struct mcf_uart *pp)
{
	struct uart_port *port = (struct uart_port *) pp;
	unsigned char status, ch, flag;

	while ((status = mcf_getreg(port, MCFUART_USR)) & MCFUART_USR_RXREADY) {
		ch = mcf_getreg(port, MCFUART_URB);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (status & MCFUART_USR_RXERR) {
			mcf_setreg(port, MCFUART_UCR, MCFUART_UCR_CMDRESETERR);
			if (status & MCFUART_USR_RXBREAK) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (status & MCFUART_USR_RXPARITY) {
				port->icount.parity++;
			} else if (status & MCFUART_USR_RXOVERRUN) {
				port->icount.overrun++;
			} else if (status & MCFUART_USR_RXFRAMING) {
				port->icount.frame++;
			}

			status &= port->read_status_mask;

			if (status & MCFUART_USR_RXBREAK)
				flag = TTY_BREAK;
			else if (status & MCFUART_USR_RXPARITY)
				flag = TTY_PARITY;
			else if (status & MCFUART_USR_RXFRAMING)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, status, MCFUART_USR_RXOVERRUN, ch, flag);
	}

	tty_flip_buffer_push(port->info->tty);
}

/****************************************************************************/

static void mcf_tx_chars(struct mcf_uart *pp)
{
	struct uart_port *port = (struct uart_port *) pp;
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		/* Send special char - probably flow control */
		mcf_setreg(port, MCFUART_UTB, port->x_char);
		port->x_char = 0;
		port->icount.tx++;
		return;
	}

	while (mcf_getreg(port, MCFUART_USR) & MCFUART_USR_TXREADY) {
		if (xmit->head == xmit->tail)
			break;
		mcf_setreg(port, MCFUART_UTB, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE -1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (xmit->head == xmit->tail) {
		pp->imr &= ~MCFUART_UIR_TXREADY;
		mcf_setreg(port, MCFUART_UIMR, pp->imr);
	}
}

/****************************************************************************/

static irqreturn_t mcf_interrupt(int irq, void *data)
{
	struct uart_port *port = data;
	struct mcf_uart *pp = (struct mcf_uart *) port;
	unsigned int isr;

	isr = mcf_getreg(port, MCFUART_UISR) & pp->imr;
	if (isr & MCFUART_UIR_RXREADY)
		mcf_rx_chars(pp);
	if (isr & MCFUART_UIR_TXREADY)
		mcf_tx_chars(pp);
	return IRQ_HANDLED;
}

/****************************************************************************/

void mcf_config_port(struct uart_port *port, int flags)
{
#if defined(CONFIG_M5272)
	unsigned long iop;

	switch (port->line) {
	case 0:
		writel(0xe0000000, (MCF_MBAR + MCFSIM_ICR2));
		break;
	case 1:
		writel(0x0e000000, (MCF_MBAR + MCFSIM_ICR2));;
		break;
	default:
		printk("MCF: don't know how to handle UART %d interrupt?\n",
			port->line);
		return;
	}

	/* Enable the output lines for the serial ports */
	iop = readl(MCF_MBAR + MCFSIM_PBCNT);
	iop = (iop & ~0x000000ff) | 0x00000055;
	writel(iop, MCF_MBAR + MCFSIM_PBCNT);

	iop = readl(MCF_MBAR + MCFSIM_PDCNT);
	iop = (iop & ~0x000003fc) | 0x000002a8;
	writel(iop, MCF_MBAR + MCFSIM_PBCNT);

#elif defined(CONFIG_M523x) || defined(CONFIG_M528x)
	unsigned long imr;

	/* level 6, line based priority */
	writeb(0x30 + port->line, MCF_MBAR + MCFICM_INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + port->line);

	imr = readl(MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (port->irq - 64)) | 1);
	writel(imr, MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);

#elif defined(CONFIG_M527x)
	unsigned long imr;
	unsigned short sem;

	/* level 6, line based priority */
	writeb(0x30 + port->line, MCF_MBAR + MCFICM_INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + port->line);

	imr = readl(MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (port->irq - 64)) | 1);
	writel(imr, MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);

	sem = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
	if (port->line == 0)
		sem |= UART0_ENABLE_MASK;
	else if (port->line == 1)
		sem |= UART1_ENABLE_MASK;
	else if (port->line == 2)
		sem |= UART2_ENABLE_MASK;
	writew(sem, MCF_IPSBAR + MCF_GPIO_PAR_UART);

#elif defined(CONFIG_M520x)
	unsigned long imr;
	unsigned short par;
	unsigned char par2;

	writeb(0x03, MCF_MBAR + MCFICM_INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + port->line);

	imr = readl(MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (port->irq - MCFINT_VECBASE)) | 1);
	writel(imr, MCF_MBAR + MCFICM_INTC0 + MCFINTC_IMRL);

	switch (port->line) {
	case 0:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD0 |
			MCF_GPIO_PAR_UART_PAR_URXD0;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 1:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD1 |
			MCF_GPIO_PAR_UART_PAR_URXD1;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 2:
		par2 = readb(MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		par2 &= ~0x0F;
		par2 |= MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2 |
			MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2;
		writeb(par2, MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		break;
	default:
		printk("MCF: don't know how to handle UART %d interrupt?\n",
			port->line);
		break;
	}

#elif defined(CONFIG_M532x)
	switch (port->line) {
	case 0:
		MCF_INTC0_ICR26 = 0x3;
		MCF_INTC0_CIMR = 26;
		/* GPIO initialization */
		MCF_GPIO_PAR_UART |= 0x000F;
		break;
	case 1:
		MCF_INTC0_ICR27 = 0x3;
		MCF_INTC0_CIMR = 27;
		/* GPIO initialization */
		MCF_GPIO_PAR_UART |= 0x0FF0;
		break;
	case 2:
		MCF_INTC0_ICR28 = 0x3;
		MCF_INTC0_CIMR = 28;
		/* GPIOs also must be initalized, depends on board */
		break;
	}

#else
	volatile unsigned char *icrp;

	switch (port->line) {
	case 0:
		writel(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
		break;
	case 1:
		writel(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
		mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_UART2);
		break;
	default:
		printk("MCF: don't know how to handle UART %d interrupt?\n",
			info->line);
		return;
	}

	mcfsetreg(port, MCFUART_UIVR, port->irq);
#endif

	port->type = PORT_MCF;

	/* Clear mask, so no surprise interrupts. */
	mcf_setreg(port, MCFUART_UIMR, 0);

	if (request_irq(port->irq, mcf_interrupt, SA_INTERRUPT,
	    "ColdFire UART", port)) {
		printk("MCF: Unable to attach ColdFire UART %d interrupt "
			"vector=%d\n", port->line, port->irq);
	}

}

/****************************************************************************/

static const char *mcf_type(struct uart_port *port)
{
	dprintk("mcf_type()\n");
	return ((port->type == PORT_MCF) ? "ColdFire UART" : NULL);
}

/****************************************************************************/

int mcf_request_port(struct uart_port *port)
{
	/* UARTs always present */
	dprintk("mcf_request_port()\n");
	return 0;
}

/****************************************************************************/

void mcf_release_port(struct uart_port *port)
{
	/* Nothing to release... */
	dprintk("mcf_release_port()\n");
}

/****************************************************************************/

int mcf_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	dprintk("mcf_verify_port()\n");
	if ((ser->type != PORT_UNKNOWN) && (ser->type != PORT_MCF))
		return -EINVAL;
	return 0;
}

/****************************************************************************/

/*
 *	Define the basic serial functions we support.
 */
static struct uart_ops mcf_uart_ops= {
	.tx_empty	= mcf_tx_empty,
	.get_mctrl	= mcf_get_mctrl,
	.set_mctrl	= mcf_set_mctrl,
	.start_tx	= mcf_start_tx,
	.stop_tx	= mcf_stop_tx,
	.stop_rx	= mcf_stop_rx,
	.enable_ms	= mcf_enable_ms,
	.break_ctl	= mcf_break_ctl,
	.startup	= mcf_startup,
	.shutdown	= mcf_shutdown,
	.set_termios	= mcf_set_termios,
	.type		= mcf_type,
	.request_port	= mcf_request_port,
	.release_port	= mcf_release_port,
	.config_port	= mcf_config_port,
	.verify_port	= mcf_verify_port,
};

/*
 *	Define the port structures, 1 per port/uart.
 */
static struct mcf_uart mcf_ports[] = {
	{
		.port = {
			.line		= 0,
			.type		= PORT_MCF,
			.membase	= (void *) MCF_MBAR + MCFUART_BASE1,
			.mapbase	= MCF_MBAR + MCFUART_BASE1,
			.iotype		= SERIAL_IO_MEM,
			.irq		= IRQBASE,
			.uartclk	= MCF_BUSCLK,
			.ops		= &mcf_uart_ops,
			.flags		= ASYNC_BOOT_AUTOCONF,
		},
	},
#if defined(MCFUART_BASE2)
	{
		.port = {
			.line		= 1,
			.type		= PORT_MCF,
			.membase	= (void *) MCF_MBAR + MCFUART_BASE2,
			.mapbase	= MCF_MBAR + MCFUART_BASE2,
			.iotype		= SERIAL_IO_MEM,
			.irq		= IRQBASE + 1,
			.uartclk	= MCF_BUSCLK,
			.ops		= &mcf_uart_ops,
			.flags		= ASYNC_BOOT_AUTOCONF,
		},
	},
#endif
#if defined(MCFUART_BASE3)
	{
		.port = {
			.line		= 2,
			.type		= PORT_MCF,
			.membase	= (void *) MCF_MBAR + MCFUART_BASE3,
			.mapbase	= MCF_MBAR + MCFUART_BASE3,
			.iotype		= SERIAL_IO_MEM,
			.irq		= IRQBASE + 2,
			.uartclk	= MCF_BUSCLK,
			.ops		= &mcf_uart_ops,
			.flags		= ASYNC_BOOT_AUTOCONF,
		},
	}
#endif
};

#define	MCF_MAXPORTS	(sizeof(mcf_ports) / sizeof(struct mcf_uart))

/****************************************************************************/
#if defined(CONFIG_SERIAL_MCF_CONSOLE)
/****************************************************************************/

static void mcf_console_putc(struct console *co, const char c)
{
	struct uart_port *port = &(mcf_ports + co->index)->port;
	int i;

	for (i = 0; (i < 0x10000); i++) {
		if (mcf_getreg(port, MCFUART_USR) & MCFUART_USR_TXREADY)
			break;
	}
	mcf_setreg(port, MCFUART_UTB, c);
	for (i = 0; (i < 0x10000); i++) {
		if (mcf_getreg(port, MCFUART_USR) & MCFUART_USR_TXREADY)
			break;
	}
}

/****************************************************************************/

static void mcf_console_write(struct console *co, const char *s, unsigned int count)
{
	for ( ; (count); count--, s++) {
		mcf_console_putc(co, *s);
		if (*s == '\n')
			mcf_console_putc(co, '\r');
	}
}

/****************************************************************************/

static int __init mcf_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_SERIAL_MCF_BAUDRATE;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if ((co->index >= 0) && (co->index <= MCF_MAXPORTS))
		co->index = 0;
	port = &mcf_ports[co->index].port;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

/****************************************************************************/

static struct uart_driver mcf_driver;

static struct console mcf_console = {
	.name		= "ttyS",
	.write		= mcf_console_write,
	.device		= uart_console_device,
	.setup		= mcf_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &mcf_driver,
};

static int __init mcf_console_init(void)
{
	register_console(&mcf_console);
	return 0;
}

console_initcall(mcf_console_init);

#define	MCF_CONSOLE	&mcf_console

/****************************************************************************/
#else
/****************************************************************************/

#define	MCF_CONSOLE	NULL

/****************************************************************************/
#endif /* CONFIG_MCF_CONSOLE */
/****************************************************************************/

/*
 *	Define the mcf UART driver structure.
 */
static struct uart_driver mcf_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "mcf",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= MCF_MAXPORTS,
	.cons		= MCF_CONSOLE,
};

/****************************************************************************/

static int __init mcf_init(void)
{
	int i, rc;

	printk("ColdFire internal UART serial driver\n");

	if ((rc = uart_register_driver(&mcf_driver)))
		return rc;

	for (i = 0; (i < MCF_MAXPORTS); i++) {
		if ((rc = uart_add_one_port(&mcf_driver, &mcf_ports[i].port)) < 0)
			printk("mcf: failed to add UART, %d\n", rc);
	}
	return 0;
}

/****************************************************************************/

static void __exit mcf_exit(void)
{
	int i;

	for (i = 0; (i < MCF_MAXPORTS); i++)
		uart_remove_one_port(&mcf_driver, &mcf_ports[i].port);
	uart_unregister_driver(&mcf_driver);
}

/****************************************************************************/

module_init(mcf_init);
module_exit(mcf_exit);

MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_DESCRIPTION("Freescale ColdFire UART driver");
MODULE_LICENSE("GPL");

/****************************************************************************/
