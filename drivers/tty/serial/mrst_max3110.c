/*
 *  mrst_max3110.c - spi uart protocol driver for Maxim 3110
 *
 * Copyright (c) 2008-2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Note:
 * 1. From Max3110 spec, the Rx FIFO has 8 words, while the Tx FIFO only has
 *    1 word. If SPI master controller doesn't support sclk frequency change,
 *    then the char need be sent out one by one with some delay
 *
 * 2. Currently only RX available interrupt is used, no need for waiting TXE
 *    interrupt for a low speed UART device
 */

#ifdef CONFIG_MAGIC_SYSRQ
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include <linux/kthread.h>
#include <linux/spi/spi.h>
#include <linux/pm.h>

#include "mrst_max3110.h"

#define PR_FMT	"mrst_max3110: "

#define UART_TX_NEEDED 1
#define CON_TX_NEEDED  2
#define BIT_IRQ_PENDING    3

struct uart_max3110 {
	struct uart_port port;
	struct spi_device *spi;
	char name[SPI_NAME_SIZE];

	wait_queue_head_t wq;
	struct task_struct *main_thread;
	struct task_struct *read_thread;
	struct mutex thread_mutex;
	struct mutex io_mutex;

	u32 baud;
	u16 cur_conf;
	u8 clock;
	u8 parity, word_7bits;
	u16 irq;

	unsigned long uart_flags;

	/* console related */
	struct circ_buf con_xmit;
};

/* global data structure, may need be removed */
static struct uart_max3110 *pmax;

static int receive_chars(struct uart_max3110 *max,
				unsigned short *str, int len);
static int max3110_read_multi(struct uart_max3110 *max);
static void max3110_con_receive(struct uart_max3110 *max);

static int max3110_write_then_read(struct uart_max3110 *max,
		const void *txbuf, void *rxbuf, unsigned len, int always_fast)
{
	struct spi_device *spi = max->spi;
	struct spi_message	message;
	struct spi_transfer	x;
	int ret;

	mutex_lock(&max->io_mutex);
	spi_message_init(&message);
	memset(&x, 0, sizeof x);
	x.len = len;
	x.tx_buf = txbuf;
	x.rx_buf = rxbuf;
	spi_message_add_tail(&x, &message);

	if (always_fast)
		x.speed_hz = spi->max_speed_hz;
	else if (max->baud)
		x.speed_hz = max->baud;

	/* Do the i/o */
	ret = spi_sync(spi, &message);
	mutex_unlock(&max->io_mutex);
	return ret;
}

/* Write a 16b word to the device */
static int max3110_out(struct uart_max3110 *max, const u16 out)
{
	void *buf;
	u16 *obuf, *ibuf;
	int ret;

	buf = kzalloc(8, GFP_KERNEL | GFP_DMA);
	if (!buf)
		return -ENOMEM;

	obuf = buf;
	ibuf = buf + 4;
	*obuf = out;
	ret = max3110_write_then_read(max, obuf, ibuf, 2, 1);
	if (ret) {
		pr_warning(PR_FMT "%s(): get err msg %d when sending 0x%x\n",
				__func__, ret, out);
		goto exit;
	}

	receive_chars(max, ibuf, 1);

exit:
	kfree(buf);
	return ret;
}

/*
 * This is usually used to read data from SPIC RX FIFO, which doesn't
 * need any delay like flushing character out.
 *
 * Return how many valide bytes are read back
 */
static int max3110_read_multi(struct uart_max3110 *max)
{
	void *buf;
	u16 *obuf, *ibuf;
	int ret, blen;

	blen = M3110_RX_FIFO_DEPTH * sizeof(u16);
	buf = kzalloc(blen * 2, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		pr_warning(PR_FMT "%s(): fail to alloc dma buffer\n", __func__);
		return 0;
	}

	/* tx/rx always have the same length */
	obuf = buf;
	ibuf = buf + blen;

	if (max3110_write_then_read(max, obuf, ibuf, blen, 1)) {
		kfree(buf);
		return 0;
	}

	ret = receive_chars(max, ibuf, M3110_RX_FIFO_DEPTH);

	kfree(buf);
	return ret;
}

static void serial_m3110_con_putchar(struct uart_port *port, int ch)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	struct circ_buf *xmit = &max->con_xmit;

	if (uart_circ_chars_free(xmit)) {
		xmit->buf[xmit->head] = (char)ch;
		xmit->head = (xmit->head + 1) & (PAGE_SIZE - 1);
	}
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void serial_m3110_con_write(struct console *co,
				const char *s, unsigned int count)
{
	if (!pmax)
		return;

	uart_console_write(&pmax->port, s, count, serial_m3110_con_putchar);

	if (!test_and_set_bit(CON_TX_NEEDED, &pmax->uart_flags))
		wake_up(&pmax->wq);
}

static int __init
serial_m3110_con_setup(struct console *co, char *options)
{
	struct uart_max3110 *max = pmax;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	pr_info(PR_FMT "setting up console\n");

	if (co->index == -1)
		co->index = 0;

	if (!max) {
		pr_err(PR_FMT "pmax is NULL, return");
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&max->port, co, baud, parity, bits, flow);
}

static struct tty_driver *serial_m3110_con_device(struct console *co,
							int *index)
{
	struct uart_driver *p = co->data;
	*index = co->index;
	return p->tty_driver;
}

static struct uart_driver serial_m3110_reg;
static struct console serial_m3110_console = {
	.name		= "ttyS",
	.write		= serial_m3110_con_write,
	.device		= serial_m3110_con_device,
	.setup		= serial_m3110_con_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial_m3110_reg,
};

static unsigned int serial_m3110_tx_empty(struct uart_port *port)
{
	return 1;
}

static void serial_m3110_stop_tx(struct uart_port *port)
{
	return;
}

/* stop_rx will be called in spin_lock env */
static void serial_m3110_stop_rx(struct uart_port *port)
{
	return;
}

#define WORDS_PER_XFER	128
static void send_circ_buf(struct uart_max3110 *max,
				struct circ_buf *xmit)
{
	void *buf;
	u16 *obuf, *ibuf;
	int i, len, blen, dma_size, left, ret = 0;


	dma_size = WORDS_PER_XFER * sizeof(u16) * 2;
	buf = kzalloc(dma_size, GFP_KERNEL | GFP_DMA);
	if (!buf)
		return;
	obuf = buf;
	ibuf = buf + dma_size/2;

	while (!uart_circ_empty(xmit)) {
		left = uart_circ_chars_pending(xmit);
		while (left) {
			len = min(left, WORDS_PER_XFER);
			blen = len * sizeof(u16);
			memset(ibuf, 0, blen);

			for (i = 0; i < len; i++) {
				obuf[i] = (u8)xmit->buf[xmit->tail] | WD_TAG;
				xmit->tail = (xmit->tail + 1) &
						(UART_XMIT_SIZE - 1);
			}

			/* Fail to send msg to console is not very critical */

			ret = max3110_write_then_read(max, obuf, ibuf, blen, 0);
			if (ret)
				pr_warning(PR_FMT "%s(): get err msg %d\n",
						__func__, ret);

			receive_chars(max, ibuf, len);

			max->port.icount.tx += len;
			left -= len;
		}
	}

	kfree(buf);
}

static void transmit_char(struct uart_max3110 *max)
{
	struct uart_port *port = &max->port;
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	send_circ_buf(max, xmit);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial_m3110_stop_tx(port);
}

/*
 * This will be called by uart_write() and tty_write, can't
 * go to sleep
 */
static void serial_m3110_start_tx(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);

	if (!test_and_set_bit(UART_TX_NEEDED, &max->uart_flags))
		wake_up(&max->wq);
}

static int
receive_chars(struct uart_max3110 *max, unsigned short *str, int len)
{
	struct uart_port *port = &max->port;
	struct tty_port *tport;
	char buf[M3110_RX_FIFO_DEPTH];
	int r, w, usable;

	/* If uart is not opened, just return */
	if (!port->state)
		return 0;

	tport = &port->state->port;

	for (r = 0, w = 0; r < len; r++) {
		if (str[r] & MAX3110_BREAK &&
		    uart_handle_break(port))
			continue;

		if (str[r] & MAX3110_READ_DATA_AVAILABLE) {
			if (uart_handle_sysrq_char(port, str[r] & 0xff))
				continue;

			buf[w++] = str[r] & 0xff;
		}
	}

	if (!w)
		return 0;

	for (r = 0; w; r += usable, w -= usable) {
		usable = tty_buffer_request_room(tport, w);
		if (usable) {
			tty_insert_flip_string(tport, buf + r, usable);
			port->icount.rx += usable;
		}
	}
	tty_flip_buffer_push(tport);

	return r;
}

/*
 * This routine will be used in read_thread or RX IRQ handling,
 * it will first do one round buffer read(8 words), if there is some
 * valid RX data, will try to read 5 more rounds till all data
 * is read out.
 *
 * Use stack space as data buffer to save some system load, and chose
 * 504 Btyes as a threadhold to do a bulk push to upper tty layer when
 * receiving bulk data, a much bigger buffer may cause stack overflow
 */
static void max3110_con_receive(struct uart_max3110 *max)
{
	int loop = 1, num;

	do {
		num = max3110_read_multi(max);

		if (num) {
			loop = 5;
		}
	} while (--loop);
}

static int max3110_main_thread(void *_max)
{
	struct uart_max3110 *max = _max;
	wait_queue_head_t *wq = &max->wq;
	int ret = 0;
	struct circ_buf *xmit = &max->con_xmit;

	pr_info(PR_FMT "start main thread\n");

	do {
		wait_event_interruptible(*wq,
				max->uart_flags || kthread_should_stop());

		mutex_lock(&max->thread_mutex);

		if (test_and_clear_bit(BIT_IRQ_PENDING, &max->uart_flags))
			max3110_con_receive(max);

		/* first handle console output */
		if (test_and_clear_bit(CON_TX_NEEDED, &max->uart_flags))
			send_circ_buf(max, xmit);

		/* handle uart output */
		if (test_and_clear_bit(UART_TX_NEEDED, &max->uart_flags))
			transmit_char(max);

		mutex_unlock(&max->thread_mutex);

	} while (!kthread_should_stop());

	return ret;
}

static irqreturn_t serial_m3110_irq(int irq, void *dev_id)
{
	struct uart_max3110 *max = dev_id;

	/* max3110's irq is a falling edge, not level triggered,
	 * so no need to disable the irq */

	if (!test_and_set_bit(BIT_IRQ_PENDING, &max->uart_flags))
		wake_up(&max->wq);

	return IRQ_HANDLED;
}

/* if don't use RX IRQ, then need a thread to polling read */
static int max3110_read_thread(void *_max)
{
	struct uart_max3110 *max = _max;

	pr_info(PR_FMT "start read thread\n");
	do {
		/*
		 * If can't acquire the mutex, it means the main thread
		 * is running which will also perform the rx job
		 */
		if (mutex_trylock(&max->thread_mutex)) {
			max3110_con_receive(max);
			mutex_unlock(&max->thread_mutex);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 20);
	} while (!kthread_should_stop());

	return 0;
}

static int serial_m3110_startup(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	u16 config = 0;
	int ret = 0;

	if (port->line != 0) {
		pr_err(PR_FMT "uart port startup failed\n");
		return -1;
	}

	/* Disable all IRQ and config it to 115200, 8n1 */
	config = WC_TAG | WC_FIFO_ENABLE
			| WC_1_STOPBITS
			| WC_8BIT_WORD
			| WC_BAUD_DR2;

	/* as we use thread to handle tx/rx, need set low latency */
	port->state->port.low_latency = 1;

	if (max->irq) {
		/* Enable RX IRQ only */
		config |= WC_RXA_IRQ_ENABLE;
	} else {
		/* If IRQ is disabled, start a read thread for input data */
		max->read_thread =
			kthread_run(max3110_read_thread, max, "max3110_read");
		if (IS_ERR(max->read_thread)) {
			ret = PTR_ERR(max->read_thread);
			max->read_thread = NULL;
			pr_err(PR_FMT "Can't create read thread!\n");
			return ret;
		}
	}

	ret = max3110_out(max, config);
	if (ret) {
		if (max->read_thread)
			kthread_stop(max->read_thread);
		max->read_thread = NULL;
		return ret;
	}

	max->cur_conf = config;
	return 0;
}

static void serial_m3110_shutdown(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	u16 config;

	if (max->read_thread) {
		kthread_stop(max->read_thread);
		max->read_thread = NULL;
	}

	/* Disable interrupts from this port */
	config = WC_TAG | WC_SW_SHDI;
	max3110_out(max, config);
}

static void serial_m3110_release_port(struct uart_port *port)
{
}

static int serial_m3110_request_port(struct uart_port *port)
{
	return 0;
}

static void serial_m3110_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_MAX3100;
}

static int
serial_m3110_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}


static const char *serial_m3110_type(struct uart_port *port)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	return max->name;
}

static void
serial_m3110_set_termios(struct uart_port *port, struct ktermios *termios,
		       struct ktermios *old)
{
	struct uart_max3110 *max =
		container_of(port, struct uart_max3110, port);
	unsigned char cval;
	unsigned int baud, parity = 0;
	int clk_div = -1;
	u16 new_conf = max->cur_conf;

	switch (termios->c_cflag & CSIZE) {
	case CS7:
		cval = UART_LCR_WLEN7;
		new_conf |= WC_7BIT_WORD;
		break;
	default:
		/* We only support CS7 & CS8 */
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
	case CS8:
		cval = UART_LCR_WLEN8;
		new_conf |= WC_8BIT_WORD;
		break;
	}

	baud = uart_get_baud_rate(port, termios, old, 0, 230400);

	/* First calc the div for 1.8MHZ clock case */
	switch (baud) {
	case 300:
		clk_div = WC_BAUD_DR384;
		break;
	case 600:
		clk_div = WC_BAUD_DR192;
		break;
	case 1200:
		clk_div = WC_BAUD_DR96;
		break;
	case 2400:
		clk_div = WC_BAUD_DR48;
		break;
	case 4800:
		clk_div = WC_BAUD_DR24;
		break;
	case 9600:
		clk_div = WC_BAUD_DR12;
		break;
	case 19200:
		clk_div = WC_BAUD_DR6;
		break;
	case 38400:
		clk_div = WC_BAUD_DR3;
		break;
	case 57600:
		clk_div = WC_BAUD_DR2;
		break;
	case 115200:
		clk_div = WC_BAUD_DR1;
		break;
	case 230400:
		if (max->clock & MAX3110_HIGH_CLK)
			break;
	default:
		/* Pick the previous baud rate */
		baud = max->baud;
		clk_div = max->cur_conf & WC_BAUD_DIV_MASK;
		tty_termios_encode_baud_rate(termios, baud, baud);
	}

	if (max->clock & MAX3110_HIGH_CLK) {
		clk_div += 1;
		/* High clk version max3110 doesn't support B300 */
		if (baud == 300) {
			baud = 600;
			clk_div = WC_BAUD_DR384;
		}
		if (baud == 230400)
			clk_div = WC_BAUD_DR1;
		tty_termios_encode_baud_rate(termios, baud, baud);
	}

	new_conf = (new_conf & ~WC_BAUD_DIV_MASK) | clk_div;

	if (unlikely(termios->c_cflag & CMSPAR))
		termios->c_cflag &= ~CMSPAR;

	if (termios->c_cflag & CSTOPB)
		new_conf |= WC_2_STOPBITS;
	else
		new_conf &= ~WC_2_STOPBITS;

	if (termios->c_cflag & PARENB) {
		new_conf |= WC_PARITY_ENABLE;
		parity |= UART_LCR_PARITY;
	} else
		new_conf &= ~WC_PARITY_ENABLE;

	if (!(termios->c_cflag & PARODD))
		parity |= UART_LCR_EPAR;
	max->parity = parity;

	uart_update_timeout(port, termios->c_cflag, baud);

	new_conf |= WC_TAG;
	if (new_conf != max->cur_conf) {
		if (!max3110_out(max, new_conf)) {
			max->cur_conf = new_conf;
			max->baud = baud;
		}
	}
}

/* Don't handle hw handshaking */
static unsigned int serial_m3110_get_mctrl(struct uart_port *port)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_DSR;
}

static void serial_m3110_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void serial_m3110_break_ctl(struct uart_port *port, int break_state)
{
}

static void serial_m3110_pm(struct uart_port *port, unsigned int state,
			unsigned int oldstate)
{
}

static void serial_m3110_enable_ms(struct uart_port *port)
{
}

static struct uart_ops serial_m3110_ops = {
	.tx_empty	= serial_m3110_tx_empty,
	.set_mctrl	= serial_m3110_set_mctrl,
	.get_mctrl	= serial_m3110_get_mctrl,
	.stop_tx	= serial_m3110_stop_tx,
	.start_tx	= serial_m3110_start_tx,
	.stop_rx	= serial_m3110_stop_rx,
	.enable_ms	= serial_m3110_enable_ms,
	.break_ctl	= serial_m3110_break_ctl,
	.startup	= serial_m3110_startup,
	.shutdown	= serial_m3110_shutdown,
	.set_termios	= serial_m3110_set_termios,
	.pm		= serial_m3110_pm,
	.type		= serial_m3110_type,
	.release_port	= serial_m3110_release_port,
	.request_port	= serial_m3110_request_port,
	.config_port	= serial_m3110_config_port,
	.verify_port	= serial_m3110_verify_port,
};

static struct uart_driver serial_m3110_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "MRST serial",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= 1,
	.cons		= &serial_m3110_console,
};

#ifdef CONFIG_PM_SLEEP
static int serial_m3110_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct uart_max3110 *max = spi_get_drvdata(spi);

	if (max->irq > 0)
		disable_irq(max->irq);
	uart_suspend_port(&serial_m3110_reg, &max->port);
	max3110_out(max, max->cur_conf | WC_SW_SHDI);
	return 0;
}

static int serial_m3110_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct uart_max3110 *max = spi_get_drvdata(spi);

	max3110_out(max, max->cur_conf);
	uart_resume_port(&serial_m3110_reg, &max->port);
	if (max->irq > 0)
		enable_irq(max->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(serial_m3110_pm_ops, serial_m3110_suspend,
			serial_m3110_resume);
#define SERIAL_M3110_PM_OPS (&serial_m3110_pm_ops)

#else
#define SERIAL_M3110_PM_OPS NULL
#endif

static int serial_m3110_probe(struct spi_device *spi)
{
	struct uart_max3110 *max;
	void *buffer;
	u16 res;
	int ret = 0;

	max = kzalloc(sizeof(*max), GFP_KERNEL);
	if (!max)
		return -ENOMEM;

	/* Set spi info */
	spi->bits_per_word = 16;
	max->clock = MAX3110_HIGH_CLK;

	spi_setup(spi);

	max->port.type = PORT_MAX3100;
	max->port.fifosize = 2;		/* Only have 16b buffer */
	max->port.ops = &serial_m3110_ops;
	max->port.line = 0;
	max->port.dev = &spi->dev;
	max->port.uartclk = 115200;

	max->spi = spi;
	strcpy(max->name, spi->modalias);
	max->irq = (u16)spi->irq;

	mutex_init(&max->thread_mutex);
	mutex_init(&max->io_mutex);

	max->word_7bits = 0;
	max->parity = 0;
	max->baud = 0;

	max->cur_conf = 0;
	max->uart_flags = 0;

	/* Check if reading configuration register returns something sane */

	res = RC_TAG;
	ret = max3110_write_then_read(max, (u8 *)&res, (u8 *)&res, 2, 0);
	if (ret < 0 || res == 0 || res == 0xffff) {
		dev_dbg(&spi->dev, "MAX3111 deemed not present (conf reg %04x)",
									res);
		ret = -ENODEV;
		goto err_get_page;
	}

	buffer = (void *)__get_free_page(GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto err_get_page;
	}
	max->con_xmit.buf = buffer;
	max->con_xmit.head = 0;
	max->con_xmit.tail = 0;

	init_waitqueue_head(&max->wq);

	max->main_thread = kthread_run(max3110_main_thread,
					max, "max3110_main");
	if (IS_ERR(max->main_thread)) {
		ret = PTR_ERR(max->main_thread);
		goto err_kthread;
	}

	if (max->irq) {
		ret = request_irq(max->irq, serial_m3110_irq,
				IRQ_TYPE_EDGE_FALLING, "max3110", max);
		if (ret) {
			max->irq = 0;
			dev_warn(&spi->dev,
			"unable to allocate IRQ, will use polling method\n");
		}
	}

	spi_set_drvdata(spi, max);
	pmax = max;

	/* Give membase a psudo value to pass serial_core's check */
	max->port.membase = (unsigned char __iomem *)0xff110000;
	uart_add_one_port(&serial_m3110_reg, &max->port);

	return 0;

err_kthread:
	free_page((unsigned long)buffer);
err_get_page:
	kfree(max);
	return ret;
}

static int serial_m3110_remove(struct spi_device *dev)
{
	struct uart_max3110 *max = spi_get_drvdata(dev);

	if (!max)
		return 0;

	uart_remove_one_port(&serial_m3110_reg, &max->port);

	free_page((unsigned long)max->con_xmit.buf);

	if (max->irq)
		free_irq(max->irq, max);

	if (max->main_thread)
		kthread_stop(max->main_thread);

	kfree(max);
	return 0;
}

static struct spi_driver uart_max3110_driver = {
	.driver = {
			.name	= "spi_max3111",
			.owner	= THIS_MODULE,
			.pm	= SERIAL_M3110_PM_OPS,
	},
	.probe		= serial_m3110_probe,
	.remove		= serial_m3110_remove,
};

static int __init serial_m3110_init(void)
{
	int ret = 0;

	ret = uart_register_driver(&serial_m3110_reg);
	if (ret)
		return ret;

	ret = spi_register_driver(&uart_max3110_driver);
	if (ret)
		uart_unregister_driver(&serial_m3110_reg);

	return ret;
}

static void __exit serial_m3110_exit(void)
{
	spi_unregister_driver(&uart_max3110_driver);
	uart_unregister_driver(&serial_m3110_reg);
}

module_init(serial_m3110_init);
module_exit(serial_m3110_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:max3110-uart");
