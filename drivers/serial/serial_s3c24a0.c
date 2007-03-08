/*
 * drivser/serial/serial_s3c24a0.c
 *
 * device for S3C24A0
 *
 * $Id: serial_s3c24a0.c,v 1.3 2006/12/12 13:38:51 gerg Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/serial_core.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/arch/clocks.h>

#define CONFIG_USE_ERR_IRQ        0

#define __DRIVER_NAME    "Samsung S3C24A0 Internal UART"


#ifdef CONFIG_BOARD_S3C24A0_SMDK
#define UART_NR                   1
#else
#define UART_NR                   2
#endif

#define UART_ULCON(port)          __REG((port)->iobase + oULCON)
#define UART_UCON(port)           __REG((port)->iobase + oUCON)
#define UART_UFCON(port)          __REG((port)->iobase + oUFCON)
#define UART_UTRSTAT(port)        __REG((port)->iobase + oUTRSTAT)
#define UART_UERSTAT(port)        __REG((port)->iobase + oUERSTAT)
#define UART_UTXH(port)           __REG((port)->iobase + oUTXH)
#define UART_URXH(port)           __REG((port)->iobase + oURXH)
#define UART_UBRDIV(port)         __REG((port)->iobase + oUBRDIV)

#define ERR_IRQ(port)             ((port)->irq + 2)
#define TX_IRQ(port)              ((port)->irq + 1)
#define RX_IRQ(port)              ((port)->irq)

#define INT_DISABLE(port)         disable_irq(port);
#define INT_ENABLE(port)          enable_irq(port);
/*
 * Internal helper function
 */
static void __xmit_char(struct uart_port *port, const char ch)
{
        while (!(UART_UTRSTAT(port) & UTRSTAT_TX_EMP));
        UART_UTXH(port) = ch;
        if (ch == '\n') {
                while (!(UART_UTRSTAT(port) & UTRSTAT_TX_EMP));
                UART_UTXH(port) = '\r';
        }
}

static void __xmit_string(struct uart_port *port, const char *p, int len)
{
        while( len-- > 0) {
                __xmit_char( port, *p++);
        }
}



static void elfinuart_stop_tx(struct uart_port *port)
{
}

static void elfinuart_start_tx(struct uart_port *port)
{
        struct uart_info *info = port->info;
        struct circ_buf *xmit = &port->info->xmit;

        int count;

        if (port->x_char) {
                __xmit_char(port, port->x_char);
                port->icount.tx++;
                port->x_char = 0;
                return;
        }

        if (uart_circ_empty( xmit) || uart_tx_stopped( port)) {
                elfinuart_stop_tx(port);
                return;
        }

        count = port->fifosize >> 1;
        do {
                __xmit_char( port, xmit->buf[xmit->tail]);
                info->xmit.tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
                port->icount.tx++;
                if (uart_circ_empty(xmit))
                        break;
        } while (--count > 0);

        if (uart_circ_chars_pending( xmit) < WAKEUP_CHARS)
                uart_write_wakeup( port);

        if (uart_circ_empty(xmit))
                elfinuart_stop_tx( port);
}

static void elfinuart_stop_rx(struct uart_port *port)
{
}

static void elfinuart_enable_ms(struct uart_port *port)
{
}

static void elfinuart_rx_char(struct uart_port *port)
{
        unsigned int status, ch, max_count = 256;
        struct tty_struct *tty = port->info->tty;

        status = UART_UTRSTAT(port);
        while ((status & UTRSTAT_RX_RDY) && max_count--) {
                if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
                        tty->flip.work.func((void *) tty);
                        if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
                                printk(KERN_WARNING "TTY_DONT_FLIP set\n");
                                return;
                        }
                }

                ch = UART_URXH(port);

                *tty->flip.char_buf_ptr = ch;
                *tty->flip.flag_buf_ptr = TTY_NORMAL;
                port->icount.rx++;
                tty->flip.flag_buf_ptr++;
                tty->flip.char_buf_ptr++;
                tty->flip.count++;
                /* No error handling just yet.
                 * On the MX1 these are seperate
                 * IRQs, so we need to deal with
                 * the sanity of 5 IRQs for one
                 * serial port before we deal
                 * with the error path properly.
                 */
                status = UART_UTRSTAT(port);
        }
        tty_flip_buffer_push(tty);
}

static u_int elfinuart_tx_empty(struct uart_port *port)
{
        return (UART_UTRSTAT(port) & UTRSTAT_TR_EMP ? 0 : TIOCSER_TEMT);
}

static u_int elfinuart_get_mctrl(struct uart_port *port)
{
        return (TIOCM_CTS | TIOCM_DSR | TIOCM_CAR);
}

static void elfinuart_set_mctrl(struct uart_port *port, u_int mctrl)
{
}

static void elfinuart_break_ctl(struct uart_port *port, int break_state)
{
        u_int ucon;

        ucon = UART_UCON(port);

        if (break_state == -1)
                ucon |= UCON_BRK_SIG;
        else
                ucon &= ~UCON_BRK_SIG;

        UART_UCON(port) = ucon;
}

static irqreturn_t elfinuart_rx_int(int irq, void *dev_id, struct pt_regs *regs)
{
        struct uart_port *port = dev_id;
        elfinuart_rx_char(port);

        return IRQ_HANDLED;
}

static irqreturn_t elfinuart_tx_int(int irq, void *dev_id, struct pt_regs *regs)
{
        struct uart_port *port = dev_id;
        elfinuart_start_tx(port);
        return IRQ_HANDLED;
}

#ifdef CONFIG_USE_ERR_IRQ
static irqreturn_t elfinuart_err_int(int irq, void *dev_id,
                                      struct pt_regs *reg)
{
        struct uart_port *port = dev_id;
        struct uart_info *info = port->info;
        struct tty_struct *tty = info->tty;
        unsigned char err = UART_UERSTAT(port) & UERSTAT_ERR_MASK;
        unsigned int ch, flg =  TTY_NORMAL;

        ch = UART_URXH(port);
        if (!err)
                return IRQ_HANDLED;

        if (err & UERSTAT_OVERRUN)
                port->icount.overrun++;

        err &= port->read_status_mask;

        if (err & UERSTAT_OVERRUN) {
                *tty->flip.char_buf_ptr = ch;
                *tty->flip.flag_buf_ptr = flg;
                tty->flip.flag_buf_ptr++;
                tty->flip.char_buf_ptr++;
                tty->flip.count++;
                if (tty->flip.count < TTY_FLIPBUF_SIZE) {
                        ch = 0;
                        flg = TTY_OVERRUN;
                }
        }

        *tty->flip.flag_buf_ptr++ = flg;
        *tty->flip.char_buf_ptr++ = ch;
        tty->flip.count++;
        return IRQ_HANDLED;
}
#endif

static struct irqaction __rx_irqaction[UART_NR] = {
        {
                name:     "serial0_rx",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_rx_int,
        },
        {
                name:     "serial1_rx",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_rx_int,
        },
};

static struct irqaction __tx_irqaction[UART_NR] = {
        {
                name:     "serial0_tx",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_tx_int,
        },
        {
                name:     "serial1_tx",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_tx_int,
        },
};

static struct irqaction __err_irqaction[UART_NR] = {
        {
                name:     "serial0_err",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_err_int,
        },
        {
                name:     "serial1_err",
                flags:    SA_INTERRUPT,
                handler:  elfinuart_err_int,
        },
};

static int elfinuart_startup(struct uart_port *port)
{
        int ret;
        u_int ucon;

        /*
         * Allocate the IRQs for TX and RX
         */
        __tx_irqaction[port->line].dev_id = (void *)port;
        __rx_irqaction[port->line].dev_id = (void *)port;
        __err_irqaction[port->line].dev_id = (void *)port;

        ret = setup_irq( RX_IRQ(port), &__rx_irqaction[port->line]);
        if (ret) goto rx_failed;

#if 0
        ret = setup_irq( TX_IRQ(port), &__tx_irqaction[port->line]);
        if (ret) goto tx_failed;
#endif

#ifdef CONFIG_USE_ERR_IRQ
        ret = setup_irq( ERR_IRQ(port), &__err_irqaction[port->line]);
        if (ret) goto err_failed;
#endif

        ucon = (UCON_TX_INT_LVL | UCON_RX_INT_LVL |
                        UCON_TX_INT | UCON_RX_INT | UCON_RX_TIMEOUT);


        spin_lock_irq( &port->lock);

        UART_UCON(port) = ucon;

        spin_unlock_irq( &port->lock);

        return 0;

#ifdef CONFIG_USE_ERR_IRQ
err_failed:
        printk(KERN_ERR "%s: err failed\n", __FUNCTION__);
        INT_DISABLE( ERR_IRQ(port));
#endif
tx_failed:
        printk(KERN_ERR "%s: tx  failed\n", __FUNCTION__);
        INT_DISABLE( TX_IRQ(port));
rx_failed:
        printk(KERN_ERR "%s: rx  failed\n", __FUNCTION__);
        INT_DISABLE( RX_IRQ(port));
        return ret;
}

static void elfinuart_shutdown(struct uart_port *port)
{
#ifdef CONFIG_USE_ERR_IRQ
        INT_DISABLE( ERR_IRQ(port));
#endif
        INT_DISABLE( TX_IRQ(port));
        INT_DISABLE( RX_IRQ(port));

        UART_UCON(port) = 0x0;
}

#if 0
static void elfinuart_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
        u_int ulcon, ufcon;
        int flags;

        ufcon = UART_UFCON(port);

        switch (cflag & CSIZE) {
                case CS5:
                        ulcon = ULCON_WL5;
                break;
                case CS6:
                        ulcon = ULCON_WL6;
                break;
                case CS7:
                        ulcon = ULCON_WL7;
                break;
                default:
                        ulcon = ULCON_WL8;
                break;
        }

        if (cflag & CSTOPB)
                ulcon |= ULCON_STOP;
        if (cflag & PARENB) {
                if (!(cflag & PARODD))
            ulcon |= ULCON_PAR_EVEN;
        }

        if (port->fifosize > 1)
                ufcon |= UFCON_FIFO_EN;

        port->read_status_mask =  UERSTAT_OVERRUN;

        port->ignore_status_mask = 0;
        if (iflag & IGNBRK) {
                if (iflag & IGNPAR)
                    port->ignore_status_mask |= UERSTAT_OVERRUN;
        }

        quot -= 1;

        spin_lock_irqsave( &port->lock, flags );

        UART_UFCON(port) = ufcon;
        UART_ULCON(port) = ulcon;
        UART_UBRDIV(port) = quot;

        spin_unlock_irqrestore(&port->lock, flags);
}

#endif

static void elfinuart_set_termios(struct uart_port *port, struct termios *termios, struct termios *old)
{
        int quot;

        uart_update_timeout(port, termios->c_cflag, 115200);
#if 0
        quot = uart_get_divisor(port, 115200);
        elfinuart_change_speed(port, termios->c_cflag, 0, quot);
#endif

}
static void elfinuart_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
}

static int elfinuart_set_wake(struct uart_port *port, unsigned int state)
{
        return 0;
}




static const char *elfinuart_type(struct uart_port *port)
{
        return __DRIVER_NAME;
}

static void elfinuart_config_port(struct uart_port *port, int flags)
{
        if (flags & UART_CONFIG_TYPE)
                port->type = PORT_S3C24A0;
}

static void elfinuart_release_port(struct uart_port *port)
{
}

static int elfinuart_request_port(struct uart_port *port)
{
        return 0;
}

static int elfinuart_verify_port(struct uart_port *port, struct serial_struct *serial)
{
        return 0;
}

static struct uart_ops elfin_pops = {
        tx_empty     : elfinuart_tx_empty,
        set_mctrl    : elfinuart_set_mctrl,
        get_mctrl    : elfinuart_get_mctrl,
        stop_tx      : elfinuart_stop_tx,
        start_tx     : elfinuart_start_tx,
        stop_rx      : elfinuart_stop_rx,
        enable_ms    : elfinuart_enable_ms,
        break_ctl    : elfinuart_break_ctl,
        startup      : elfinuart_startup,
        shutdown     : elfinuart_shutdown,
        set_termios:    elfinuart_set_termios,
        pm:             elfinuart_pm,
        set_wake:       elfinuart_set_wake,
        type         : elfinuart_type,
        config_port  : elfinuart_config_port,
        release_port : elfinuart_release_port,
        request_port : elfinuart_request_port,
        verify_port:    elfinuart_verify_port,
};

static struct uart_port elfin_ports[UART_NR] = {
        {
                iobase   : (unsigned long)(UART0_CTL_BASE),
                irq      : IRQ_RXD0,
                uartclk  : 130252800,
                fifosize : 64,
                ops      : &elfin_pops,
                type     : PORT_S3C24A0,
                flags    : ASYNC_BOOT_AUTOCONF,
        },
#ifndef CONFIG_BOARD_S3C24A0_SMDK
        {
                iobase   : (unsigned long)(UART1_CTL_BASE),
                irq      : IRQ_RXD1,
                uartclk  : 130252800,
                fifosize : 64,
                ops      : &elfin_pops,
                type     : PORT_S3C24A0,
                flags    : ASYNC_BOOT_AUTOCONF,
        }
#endif /* !CONFIG_BOARD_S3C24A0_SMDK */
};

void __init elfin_register_uart(int idx, int port)
{
        if (idx >= UART_NR) {
                printk(KERN_ERR "%s: bad index number %d\n"
                        , __FUNCTION__, idx);
                return;
        }
        elfin_ports[idx].uartclk = elfin_get_bus_clk(GET_PCLK);

        switch (port) {
                case 0:
                        elfin_ports[idx].iobase = (unsigned long)(UART0_CTL_BASE);
                        elfin_ports[idx].irq  = IRQ_RXD0;
                break;
                case 1:
                        elfin_ports[idx].iobase = (unsigned long)(UART1_CTL_BASE);
                        elfin_ports[idx].irq  = IRQ_RXD1;
                break;
                default:
                        printk(KERN_ERR "%s : bad port number %d\n", __FUNCTION__, port);
        }
}



#ifdef CONFIG_SERIAL_S3C24A0_CONSOLE

static void elfin_console_write(struct console *co, const char *s, u_int count)
{
        struct uart_port *port = elfin_ports + co->index;
        __xmit_string( port, s, count);
}

static int __init elfin_console_setup(struct console *co, char *options)
{
        struct uart_port *port;
        int baud = 115200;
        int bits = 8;
        int parity = 'n';
        int flow = 0;

        port = uart_get_console(elfin_ports, UART_NR, co);

        if (options)
                uart_parse_options(options, &baud, &parity, &bits, &flow);

        return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct uart_driver elfin_reg;
static struct console elfin_cons = {
        name     : "ttyS",
        write    : elfin_console_write,
        device   : uart_console_device,
        setup    : elfin_console_setup,
        flags    : CON_PRINTBUFFER,
        index    : -1,
        data     : &elfin_reg,
};

static int __init elfin_console_init(void)
{
        register_console(&elfin_cons);
        return 0;
}

console_initcall(elfin_console_init);

#define S3C24A0_CONSOLE         &elfin_cons
#else   /* CONFIG_SERIAL_S3C24A0_CONSOLE */
#define S3C24A0_CONSOLE         NULL
#endif  /* CONFIG_SERIAL_S3C24A0_CONSOLE */


static struct uart_driver elfin_reg = {
        owner          : THIS_MODULE,
        driver_name    : "ttyS",
        dev_name       : "ttyS",
        major   : TTY_MAJOR,
        minor   : 64,
        nr      : UART_NR,
        cons           : S3C24A0_CONSOLE,
};

static int __init elfinuart_init(void)
{
        int ret;

        printk("Initializing %s\n", __DRIVER_NAME);
        ret = uart_register_driver(&elfin_reg);
        if (ret == 0) {
                int i;

                for (i = 0; i < UART_NR; i++)
                        uart_add_one_port(&elfin_reg, &elfin_ports[i]);
        }
        return ret;

}

static void __exit elfinuart_exit(void)
{
        uart_unregister_driver(&elfin_reg);
}

module_init(elfinuart_init);
module_exit(elfinuart_exit);


MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("S3C24A0 generic serial port driver");
MODULE_SUPPORTED_DEVICE("ttyS");
MODULE_LICENSE("GPL");
