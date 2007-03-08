/* serial port driver for the Atmel AT91 series builtin USARTs
 *
 * Copyright (C) 2000, 2001  Erik Andersen <andersen@lineo.com>
 * Copyright (C) 2004 Hyok S. Choi <hyok.choi@samsung.com>
 *
 * Based on:
 * drivers/char/68302serial.c
 * and also based on trioserial.c from Aplio, though this driver
 * has been extensively changed since then.  No author was 
 * listed in trioserial.c.
 * 
 * Phil Wilshire 12/31/2002  Fixed multiple ^@ chars on TCSETA 
 * Hyok S. Choi  03/22/2004  2.6 port
 */

/* Enable this to force this driver to always operate at 57600 */
#undef FORCE_57600

#include <linux/version.h>
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/arch/irq.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include "serial_atmel.h"

#define USE_INTS	1

static volatile struct atmel_usart_regs *usarts[AT91_USART_CNT] = {
	(volatile struct atmel_usart_regs *) AT91_USART0_BASE,
	(volatile struct atmel_usart_regs *) AT91_USART1_BASE
};

#define SERIAL_XMIT_SIZE	PAGE_SIZE
#define RX_SERIAL_SIZE		256

static struct atmel_serial atmel_info[AT91_USART_CNT];
static struct tty_struct *serial_table[AT91_USART_CNT];
struct atmel_serial *atmel_consinfo = 0;

#define UART_CLOCK	(ARM_CLK/16)

static struct work_struct serialpoll;

#ifdef CONFIG_CONSOLE
extern wait_queue_head_t keypress_wait; 
#endif

struct tty_driver *serial_driver;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Debugging... DEBUG_INTR is bad to use when one of the zs
 * lines is your console ;(
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define RS_ISR_PASS_LIMIT 256

#define _INLINE_ inline

static inline int serial_paranoia_check(struct atmel_serial *info,
					char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct %s in %s\n";
	static const char *badinfo =
		"Warning: null atmel_serial struct for %s in %s\n";

	if (!info) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}

static unsigned char * rx_buf_table[AT91_USART_CNT];

static unsigned char rx_buf1[RX_SERIAL_SIZE];
static unsigned char rx_buf2[RX_SERIAL_SIZE];

/* Console hooks... */

static void serpoll(void *data);

static void change_speed(struct atmel_serial *info);

static char prompt0;
static void xmit_char(struct atmel_serial *info, char ch);
static void xmit_string(struct atmel_serial *info, char *p, int len);
static void start_rx(struct atmel_serial *info);
static void wait_EOT(volatile struct atmel_usart_regs *);
static void uart_init(struct atmel_serial *info);
static void uart_speed(struct atmel_serial *info, unsigned cflag);

static void tx_enable(volatile struct atmel_usart_regs *uart);
static void rx_enable(volatile struct atmel_usart_regs *uart);
static void tx_disable(volatile struct atmel_usart_regs *uart);
static void rx_disable(volatile struct atmel_usart_regs *uart);
static void tx_stop(volatile struct atmel_usart_regs *uart);
static void tx_start(volatile struct atmel_usart_regs *uart);
static void rx_stop(volatile struct atmel_usart_regs *uart);
static void rx_start(volatile struct atmel_usart_regs *uart, int ints);
static void set_ints_mode(int yes, struct atmel_serial *info);
static irqreturn_t rs_interrupt(struct atmel_serial *info);
extern void show_net_buffers(void);
extern void hard_reset_now(void);
static void handle_termios_tcsets(struct termios * ptermios, struct atmel_serial * ptty);

static int global;

static void coucou1(void)
{
	global = 0;
}

static void coucou2(void)
{
	global = 1;
}
static void _INLINE_ tx_enable(volatile struct atmel_usart_regs *uart)
{
	uart->ier = US_TXEMPTY;
}
static void _INLINE_ rx_enable(volatile struct atmel_usart_regs *uart)
{
	uart->ier = US_ENDRX | US_TIMEOUT;
}
static void _INLINE_ tx_disable(volatile struct atmel_usart_regs *uart)
{
	uart->idr = US_TXEMPTY;
}
static void _INLINE_ rx_disable(volatile struct atmel_usart_regs *uart)
{
	uart->idr = US_ENDRX | US_TIMEOUT;
}
static void _INLINE_ tx_stop(volatile struct atmel_usart_regs *uart)
{
	tx_disable(uart);
	uart->tcr = 0;
	uart->cr = US_TXDIS;
}
static void _INLINE_ tx_start(volatile struct atmel_usart_regs *uart)
{
	tx_enable(uart);
	uart->cr = US_TXEN;
}
static void _INLINE_ rx_stop(volatile struct atmel_usart_regs *uart)
{
	rx_disable(uart);
	uart->rtor = 0;
	// PSW fixes slew of ^@ chars on a TCSETA ioctl 
        //uart->rcr = 0;
	uart->cr = US_RXDIS;
}
static void _INLINE_ rx_start(volatile struct atmel_usart_regs *uart, int ints)
{
	uart->cr = US_RXEN | US_STTO;
	uart->rtor = 20;
	if (ints) {
		rx_enable(uart);
	}
}
static void _INLINE_ reset_status(volatile struct atmel_usart_regs *uart)
{
	uart->cr = US_RSTSTA;
}
static void set_ints_mode(int yes, struct atmel_serial *info)
{
	info->use_ints = yes;
// FIXME: check
#if 0
	(yes) ? unmask_irq(info->irq) : mask_irq(info->irq);
#endif
}

#ifdef US_RTS
static void atmel_cts_off(struct atmel_serial *info)
{
	volatile struct atmel_usart_regs *uart;

	uart = info->usart;
	uart->mc &= ~(unsigned long) US_RTS;
	info->cts_state = 0;
}
static void atmel_cts_on(struct atmel_serial *info)
{
	volatile struct atmel_usart_regs *uart;

	uart = info->usart;
	uart->mc |= US_RTS;
	info->cts_state = 1;
}
/* Sets or clears DTR/RTS on the requested line */
static inline void atmel_rtsdtr(struct atmel_serial *ss, int set)
{
        volatile struct atmel_usart_regs *uart;

        uart = ss->usart;
        if (set) {
              uart->mc |= US_DTR | US_RTS;
        } else {
              uart->mc &= ~(unsigned long) (US_DTR | US_RTS);
        }
        return;
}
#endif	/* US_RTS */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;

	local_irq_save(flags);
	tx_stop(info->usart);
	rx_stop(info->usart);
	local_irq_restore(flags);
}

static void rs_put_char(struct atmel_serial *info, char ch)
{
	unsigned long flags;

	local_irq_save(flags);
	xmit_char(info, ch);
	wait_EOT(info->usart);
	local_irq_restore(flags);
}

static void rs_start(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_start"))
		return;

	local_irq_save(flags);
	tx_start(info->usart);
	rx_start(info->usart, info->use_ints);
	/* FIXME */
//	start_rx(info);
	local_irq_restore(flags);
}

/* Drop into either the boot monitor or kadb upon receiving a break
 * from keyboard/console input.
 */
static void batten_down_hatches(void)
{
	/* Drop into the debugger */
}

static _INLINE_ void status_handle(struct atmel_serial *info, unsigned long status)
{
#if 0
	if (status & DCD) {
		if ((info->tty->termios->c_cflag & CRTSCTS) &&
			((info->curregs[3] & AUTO_ENAB) == 0)) {
			info->curregs[3] |= AUTO_ENAB;
			info->pendregs[3] |= AUTO_ENAB;
			write_zsreg(info->atmel_channel, 3, info->curregs[3]);
		}
	} else {
		if ((info->curregs[3] & AUTO_ENAB)) {
			info->curregs[3] &= ~AUTO_ENAB;
			info->pendregs[3] &= ~AUTO_ENAB;
			write_zsreg(info->atmel_channel, 3, info->curregs[3]);
		}
	}
#endif
	/* Whee, if this is console input and this is a
	 * 'break asserted' status change interrupt, call
	 * the boot prom.
	 */
	if ((status & US_RXBRK) && info->break_abort)
		batten_down_hatches();

	/* XXX Whee, put in a buffer somewhere, the status information
	 * XXX whee whee whee... Where does the information go...
	 */
	reset_status(info->usart);
	return;
}

static _INLINE_ void receive_chars(struct atmel_serial *info, unsigned long status)
{
	int count;
	volatile struct atmel_usart_regs *uart = info->usart;
	char *rxp, ch, flag;
	struct tty_struct *tty = info->tty;

	if (!(info->flags & S_INITIALIZED))
		return;
	count = RX_SERIAL_SIZE - uart->rcr;
	// hack to receive chars by polling only BD fields
	if (!count) {
		return;
	}

	if (!tty)
		goto clear_and_exit;

#if 0
	if (tty->flip.count >= TTY_FLIPBUF_SIZE)
		schedule_work(&tty->flip.work);

	if ((count + tty->flip.count) >= TTY_FLIPBUF_SIZE) {
#ifdef US_RTS
		atmel_cts_off(info);
#endif
		serialpoll.data = (void *) info;
		schedule_work(&serialpoll);
	}
	memset(tty->flip.flag_buf_ptr, 0, count);
	memcpy(tty->flip.char_buf_ptr, info->rx_buf, count);
	tty->flip.char_buf_ptr += count;

	if (status & US_PARE)
		*(tty->flip.flag_buf_ptr - 1) = TTY_PARITY;
	else if (status & US_OVRE)
		*(tty->flip.flag_buf_ptr - 1) = TTY_OVERRUN;
	else if (status & US_FRAME)
		*(tty->flip.flag_buf_ptr - 1) = TTY_FRAME;

	tty->flip.count += count;
#endif
#if 1
	rxp = info->rx_buf;
	for (; (count > 0); count--) {

		ch = *rxp++;
		flag = TTY_NORMAL;
		if (status & US_PARE)
			flag = TTY_PARITY;
		else if (status & US_OVRE)
			flag = TTY_OVERRUN;
		else if (status & US_FRAME)
			flag = TTY_FRAME;


		tty_insert_flip_char(tty, ch, flag);
	}
#endif

	tty_schedule_flip(tty);

  clear_and_exit:
	start_rx(info);
	return;
}

static _INLINE_ void transmit_chars(struct atmel_serial *info)
{
	if (info->x_char) {
		/* Send next char */
		xmit_char(info, info->x_char);
		info->x_char = 0;
		goto clear_and_return;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped) {
		/* That's peculiar... */
		tx_stop(info->usart);
		goto clear_and_return;
	}

	if (info->xmit_tail + info->xmit_cnt < SERIAL_XMIT_SIZE) {
		xmit_string(info, info->xmit_buf + info->xmit_tail,
					info->xmit_cnt);
		info->xmit_tail =
			(info->xmit_tail + info->xmit_cnt) & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt = 0;
	} else {
		coucou1();
		xmit_string(info, info->xmit_buf + info->xmit_tail,
					SERIAL_XMIT_SIZE - info->xmit_tail);
		//xmit_string(info, info->xmit_buf, info->xmit_tail + info->xmit_cnt - SERIAL_XMIT_SIZE);
		info->xmit_cnt =
			info->xmit_cnt - (SERIAL_XMIT_SIZE - info->xmit_tail);
		info->xmit_tail = 0;
	}

	if (info->xmit_cnt < WAKEUP_CHARS)
		schedule_work(&info->tqueue);

	if (info->xmit_cnt <= 0) {
		//tx_stop(info->usart);
		goto clear_and_return;
	}

  clear_and_return:
	/* Clear interrupt (should be auto) */
	return;
}

/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t rs_interrupta(int irq, void *dev_id, struct pt_regs *regs)
{
	return rs_interrupt(&atmel_info[0]);
}
static irqreturn_t rs_interruptb(int irq, void *dev_id, struct pt_regs *regs)
{
	return rs_interrupt(&atmel_info[1]);
}
static irqreturn_t rs_interrupt(struct atmel_serial *info)
{
	unsigned long status;

	status = info->usart->csr;
	if (status & (US_ENDRX | US_TIMEOUT)) {
		receive_chars(info, status);
	}
	if (status & (US_TXEMPTY)) {
		transmit_chars(info);
	}
	status_handle(info, status);

#ifdef US_RTS
	if (!info->cts_state) {
		if (info->tty->flip.count < TTY_FLIPBUF_SIZE - RX_SERIAL_SIZE) {
			atmel_cts_on(info);
		}
	}
#endif
	if (!info->use_ints) {
		serialpoll.data = (void *) info;
		schedule_work(&serialpoll);
	}
	return IRQ_HANDLED;
}
static void serpoll(void *data)
{
	struct atmel_serial *info = data;

	rs_interrupt(info);
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */


static void do_softint(void *private_)
{
	struct atmel_serial *info = (struct atmel_serial *) private_;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty)
		return;
#if 0 	// FIXME - CHECK
	if (clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			tty->ldisc.write_wakeup) (tty->ldisc.write_wakeup) (tty);
		wake_up_interruptible(&tty->write_wait);
	}
#endif
}

/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> rs_hangup()
 *
 */
static void do_serial_hangup(void *private_)
{
	struct atmel_serial *info = (struct atmel_serial *) private_;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}


/*
 * This subroutine is called when the RS_TIMER goes off.  It is used
 * by the serial driver to handle ports that do not have an interrupt
 * (irq=0).  This doesn't work at all for 16450's, as a sun has a Z8530.
 */
#if 0
static void rs_timer(void)
{
	panic("rs_timer called\n");
	return;
}
#endif

static unsigned long calcCD(unsigned long br)
{
	return (UART_CLOCK / br);
}

static void uart_init(struct atmel_serial *info)
{
	volatile struct atmel_usart_regs *uart;

	if (info) {
		uart = info->usart;
	} else {
		uart = usarts[0];
	}

	/* Reset the USART */
	uart->cr = US_TXDIS | US_RXDIS | US_RSTTX | US_RSTRX;
	/* clear Rx receive and Tx sent counters */
	uart->rcr = 0;
	uart->tcr = 0;

	/* Disable interrups till we say we want them */
	tx_disable(info->usart);
	rx_disable(info->usart);
	
	/* Set the serial port into a safe sane state */
	uart->mr = US_USCLKS(0) | US_CLK0 | US_CHMODE(0) | US_NBSTOP(0) |
		    US_PAR(4) | US_CHRL(3);

#ifndef FORCE_57600
	uart->brgr = calcCD(9600);
#else
	uart->brgr = calcCD(57600);
#endif

	uart->rtor = 20;			// timeout = value * 4 *bit period
	uart->ttgr = 0;				// no guard time
	uart->rcr = 0;
	uart->rpr = 0;
	uart->tcr = 0;
	uart->tpr = 0;
#ifdef US_RTS
	uart->mc = 0; 
#endif
}

/* It is the responsibilty of whoever calls this function to be sure
 * that that have called
 *	tx_stop(uart); rx_stop(uart);
 * before calling the function.  Failure to do this will cause messy
 * things to happen.  You have been warned.   */
static void uart_speed(struct atmel_serial *info, unsigned cflag)
{
	unsigned baud = info->baud;
	volatile struct atmel_usart_regs *uart = info->usart;

	// disable tx and rx
	uart->cr = US_TXDIS | US_RXDIS;
	
	// disable interrupts
	tx_disable(uart);
	rx_disable(uart);
	
#ifndef FORCE_57600
	uart->brgr = calcCD(baud);
#else
	uart->brgr = calcCD(57600);
#endif
/* FIXME */
#if 0
	/* probably not needed */
	uart->US_RTOR = 20;			// timeout = value * 4 *bit period
	uart->US_TTGR = 0;				// no guard time
	uart->US_RPR = 0;
	uart->US_RCR = 0;
	uart->US_TPR = 0;
	uart->US_TCR = 0;
#endif


/* FIXME */
#if 0
	uart->mc = 0;
	if (cflag != 0xffff) {
		uart->mr = US_USCLKS(0) | US_CLK0 | US_CHMODE(0) | US_NBSTOP(0) |
		    US_PAR(0);

		if ((cflag & CSIZE) == CS8)
			uart->mr |= US_CHRL(3);	// 8 bit char
		else
			uart->mr |= US_CHRL(2);	// 7 bit char

		if (cflag & CSTOPB)
			uart->mr |= US_NBSTOP(2);	// 2 stop bits

		if (!(cflag & PARENB))
			uart->mr |= US_PAR(4);	// parity disabled
		else if (cflag & PARODD)
			uart->mr |= US_PAR(1);	// odd parity
	}
#endif	
	
/* FIXME */
#if 0
	// enable tx and rx
	uart->cr = US_TXEN | US_RXEN;
	
	// enable interrupts
	tx_enable();
	rx_enable();
#endif
	tx_start(uart);
	start_rx(info);
}

static void wait_EOT(volatile struct atmel_usart_regs *uart)
{
	// make sure tx is enabled
	uart->cr = US_TXEN;
	
	// wait until all chars sent FIXME - is this sane ?
	while (1) {
		if (uart->csr & US_TXEMPTY)
			break;
	}
}
static int startup(struct atmel_serial *info)
{
	unsigned long flags;

	if (info->flags & S_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}
	if (!info->rx_buf) {
	    //info->rx_buf = (unsigned char *) ))__get_free_page(GFP_KERNEL);
		//info->rx_buf = rx_buf1;
		if (!info->rx_buf)
			return -ENOMEM;
	}
	local_irq_save(flags);
#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (irq %d)...\n", info->line, info->irq);
#endif
	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */

	uart_init(info);
	//set_ints_mode(0, info);
	change_speed(info);
	info->flags |= S_INITIALIZED;
	local_irq_restore(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct atmel_serial *info)
{
	unsigned long flags;

	tx_disable(info->usart);
	rx_disable(info->usart);
	rx_stop(info->usart);		/* All off! */
	if (!(info->flags & S_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....\n", info->line,
		   info->irq);
#endif

	local_irq_save(flags);

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~S_INITIALIZED;
	local_irq_restore(flags);
}

/* rate = 1036800 / ((65 - prescale) * (1<<divider)) */

static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0
};

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct atmel_serial *info)
{
	unsigned cflag;
	int      i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	// disable tx and rx
	info->usart->cr = US_TXDIS | US_RXDIS;
	
	/* First disable the interrupts */
	tx_stop(info->usart);
	rx_stop(info->usart);

	/* set the baudrate */
	i = cflag & CBAUD;

	info->baud = baud_table[i];
	uart_speed(info, cflag);
	tx_start(info->usart);
	rx_start(info->usart, info->use_ints);

	// enable tx and rx
	info->usart->cr = US_TXEN | US_RXEN;
	
	return;
}

static void start_rx(struct atmel_serial *info)
{
	volatile struct atmel_usart_regs *uart = info->usart;

	rx_stop(uart);
/*  FIXME - rehnberg
	if (info->rx_buf == rx_buf1) {
		info->rx_buf = rx_buf2;
	} else {
		info->rx_buf = rx_buf1;
	}
*/
	uart->rpr = (unsigned long) info->rx_buf;
	uart->rcr = (unsigned long) RX_SERIAL_SIZE;
	rx_start(uart, info->use_ints);
}
static void xmit_char(struct atmel_serial *info, char ch)
{
	prompt0 = ch;
	xmit_string(info, &prompt0, 1);
}
static void xmit_string(struct atmel_serial *info, char *p, int len)
{
	info->usart->tcr = 0;
	info->usart->tpr = (unsigned long) p;
	info->usart->tcr = (unsigned long) len;
	tx_start(info->usart);
}

/*
 * atmel_console_print is registered for printk.
 */
int atmel_console_initialized;

static void init_console(struct atmel_serial *info)
{
	memset(info, 0, sizeof(struct atmel_serial));

#ifdef CONFIG_SWAP_ATMEL_PORTS
	info->usart = (volatile struct atmel_usart_regs *) AT91_USART1_BASE;
	info->irqmask = AIC_URT1;
	info->irq = IRQ_USART1;
#else
	info->usart = (volatile struct atmel_usart_regs *) AT91_USART0_BASE;
	info->irqmask = 1<<IRQ_USART0;
	info->irq = IRQ_USART0;
#endif
	info->tty = 0;
	info->port = 0;
	info->use_ints = 0;
	info->cts_state = 1;
	info->is_cons = 1;
	atmel_console_initialized = 1;
}


void console_print_atmel(const char *p)
{
	char c;
	struct atmel_serial *info;

#ifdef CONFIG_SWAP_ATMEL_PORTS
	info = &atmel_info[1];
#else
	info = &atmel_info[0];
#endif

	if (!atmel_console_initialized) {
		init_console(info);
		uart_init(info);
		info->baud = 9600;
		tx_stop(info->usart);
		rx_stop(info->usart);
		uart_speed(info, 0xffff);
		tx_start(info->usart);
		rx_start(info->usart, info->use_ints);
	}

	while ((c = *(p++)) != 0) {
		if (c == '\n')
			rs_put_char(info, '\r');
		rs_put_char(info, c);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
#if 0
	if (!info->use_ints)
	    rs_fair_output(info);
#endif

	return;
}

static void rs_set_ldisc(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_set_ldisc"))
		return;

	info->is_cons = (tty->termios->c_line == N_TTY);

	printk("ttyS%d console mode %s\n", info->line,
		   info->is_cons ? "on" : "off");
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_flush_chars"))
		return;
	if (!info->use_ints) {
		for (;;) {
			if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
				!info->xmit_buf) return;

			/* Enable transmitter */
			local_irq_save(flags);
			tx_start(info->usart);
		}
	} else {
		if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
			!info->xmit_buf) return;

		/* Enable transmitter */
		local_irq_save(flags);
		tx_start(info->usart);
	}

	if (!info->use_ints)
		wait_EOT(info->usart);
	/* Send char */
	xmit_char(info, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
	info->xmit_cnt--;

	local_irq_restore(flags);
}

extern void console_printn(const char *b, int count);

static int rs_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int c, total = 0;
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;

	local_save_flags(flags);
	while (1) {
		local_irq_disable();
		c = min(count, (int) min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
			SERIAL_XMIT_SIZE - info->xmit_head));
		local_irq_restore(flags);

		if (c <= 0)
			break;

		memcpy(info->xmit_buf + info->xmit_head, buf, c);

		local_irq_disable();
		info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt += c;
		local_irq_restore(flags);

		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		/* Enable transmitter */

		local_irq_disable();
		/*printk("Enabling transmitter\n"); */

		if (!info->use_ints) {
			while (info->xmit_cnt) {
				wait_EOT(info->usart);
				/* Send char */
				xmit_char(info, info->xmit_buf[info->xmit_tail++]);
				wait_EOT(info->usart);
				info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
				info->xmit_cnt--;
			}
		} else {
			if (info->xmit_cnt) {
				/* Send char */
				wait_EOT(info->usart);
				if (info->xmit_tail + info->xmit_cnt < SERIAL_XMIT_SIZE) {
					xmit_string(info, info->xmit_buf + info->xmit_tail,
								info->xmit_cnt);
					info->xmit_tail =
						(info->xmit_tail +
						 info->xmit_cnt) & (SERIAL_XMIT_SIZE - 1);
					info->xmit_cnt = 0;
				} else {
					coucou2();
					xmit_string(info, info->xmit_buf + info->xmit_tail,
								SERIAL_XMIT_SIZE - info->xmit_tail);
					//xmit_string(info, info->xmit_buf, info->xmit_tail + info->xmit_cnt - SERIAL_XMIT_SIZE);
					info->xmit_cnt =
						info->xmit_cnt - (SERIAL_XMIT_SIZE - info->xmit_tail);
					info->xmit_tail = 0;
				}
			}
		}
	} else {
		/*printk("Skipping transmit\n"); */
	}

#if 0
	printk("Enabling stuff anyhow\n");
	tx_start(0);

	if (SCC_EOT(0, 0)) {
		printk("TX FIFO empty.\n");
		/* Send char */
		atmel_xmit_char(info->usart, info->xmit_buf[info->xmit_tail++]);
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt--;
	}
#endif

	local_irq_restore(flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;
	int ret;

	if (serial_paranoia_check(info, tty->name, "rs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	unsigned long flags;
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;
	local_irq_save(flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	local_irq_restore(flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		tty->ldisc.write_wakeup) (tty->ldisc.write_wakeup) (tty);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

#ifdef SERIAL_DEBUG_THROTTLE
	char buf[64];

	printk("throttle %s: %d....\n", _tty_name(tty, buf),
		   tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;

	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line (do this atomic) */
}

static void rs_unthrottle(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

#ifdef SERIAL_DEBUG_THROTTLE
	char buf[64];

	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
		   tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_unthrottle"))
		return;

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}

	/* Assert RTS line (do this atomic) */
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct atmel_serial *info,
						   struct serial_struct *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	copy_to_user(retinfo, &tmp, sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct atmel_serial *info,
						   struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct atmel_serial old_info;
	int retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial, new_info, sizeof(new_serial));
	old_info = *info;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != info->baud_base) ||
			(new_serial.type != info->type) ||
			(new_serial.close_delay != info->close_delay) ||
			((new_serial.flags & ~S_USR_MASK) !=
		     (info->flags & ~S_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~S_USR_MASK) |
					   (new_serial.flags & S_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~S_FLAGS) |
				   (new_serial.flags & S_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

  check_and_exit:
	//retval = startup(info);
	change_speed(info);
	retval = 0;
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 */
static int get_lsr_info(struct atmel_serial *info, unsigned int *value)
{
	unsigned char status;
	unsigned long flags;

	local_irq_save(flags);
	status = info->usart->csr;
	status &= US_TXEMPTY;
	local_irq_restore(flags);
	put_user(status, value);
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(struct atmel_serial *info, int duration)
{
        unsigned long flags;
        if (!info->port)  return;

	current->state = TASK_INTERRUPTIBLE;
	local_irq_save(flags);
	info->usart->cr = US_STTBRK;
	if (!info->use_ints) {
		while (US_TXRDY != (info->usart->csr & US_TXRDY)) {
			;					// this takes max 2ms at 9600
		}
		info->usart->cr = US_STPBRK;
	}
	local_irq_restore(flags);
}

static int rs_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	int error;
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;
	int retval;

	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
		(cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD) &&
		(cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
	case TCSBRK:				/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			send_break(info, HZ / 4);	/* 1/4 second */
		return 0;
	case TCSBRKP:				/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		send_break(info, arg ? arg * (HZ / 10) : HZ / 4);
		return 0;
	case TIOCGSOFTCAR:
		error = access_ok(VERIFY_WRITE, (void *) arg, sizeof(long));
		if (error)
			return error;
		put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long *) arg);
		return 0;
	case TIOCSSOFTCAR:
		arg = get_user(arg,(unsigned long *) arg);
		tty->termios->c_cflag = ((tty->termios->c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0));
		return 0;
	case TIOCGSERIAL:
		error = access_ok(VERIFY_WRITE, (void *) arg, sizeof(struct serial_struct));
		if (error)
			return error;
		return get_serial_info(info, (struct serial_struct *) arg);
	case TIOCSSERIAL:
		return set_serial_info(info, (struct serial_struct *) arg);
	case TIOCSERGETLSR:		/* Get line status register */
		error = access_ok(VERIFY_WRITE, (void *) arg,
			sizeof(unsigned int));
		if (error)
			return error;
		else
			return get_lsr_info(info, (unsigned int *) arg);

	case TIOCSERGSTRUCT:
		error = access_ok(VERIFY_WRITE, (void *) arg,
			sizeof(struct atmel_serial));
		if (error)
			return error;
		copy_to_user((struct atmel_serial *) arg, info,
			sizeof(struct atmel_serial));
		return 0;

	case TCSETS:	
		handle_termios_tcsets((struct termios *)arg, info);
		//		return set_serial_info(info, (struct serial_struct *) arg);
		break;	
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void handle_termios_tcsets(struct termios * ptermios, struct atmel_serial * pinfo )
{
	/*
	 * hmmmm....
	 */
	if (pinfo->tty->termios->c_cflag != ptermios->c_cflag)
		pinfo->tty->termios->c_cflag = ptermios->c_cflag;
	change_speed(pinfo);
}
	  
static void rs_set_termios(struct tty_struct *tty,
	struct termios *old_termios)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
		!(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}

}

/*
 * ------------------------------------------------------------
 * rs_close()
 *
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * S structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file *filp)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;

	local_irq_save(flags);

	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttyS%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
			   "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("rs_close: bad serial port count for ttyS%d: %d\n",
			   info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		local_irq_restore(flags);
		return;
	}
	// closing port so disable interrupts
	set_ints_mode(0, info);

	info->flags |= S_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != S_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */

	shutdown(info);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
#if 0
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close) (tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open) (tty);
	}
#endif
	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(S_NORMAL_ACTIVE | S_CALLOUT_ACTIVE | S_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rs_hangup(struct tty_struct *tty)
{
	struct atmel_serial *info = (struct atmel_serial *) tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_hangup"))
		return;

	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~S_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file *filp,
						   struct atmel_serial *info)
{
	DECLARE_WAITQUEUE(wait, current); 
	int retval;
	int do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & S_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & S_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= S_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyS%d, count = %d\n",
		   info->line, info->count);
#endif
	info->count--;
	info->blocked_open++;
	while (1) {
#ifdef US_RTS
		local_irq_save(flags);
		atmel_rtsdtr(info, 1);
		local_irq_restore(flags);
#endif
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
	    	     !(info->flags & S_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & S_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & S_CLOSING) && do_clocal)
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyS%d, count = %d\n",
			   info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyS%d, count = %d\n",
		   info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= S_NORMAL_ACTIVE;
	if (!info->use_ints) {
		serialpoll.data = (void *) info;
		schedule_work(&serialpoll);
	}
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its S structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file *filp)
{
	struct atmel_serial *info;
	int retval, line;

	line = tty->index;

	// check if line is sane
	if (line < 0 || line >= AT91_USART_CNT)
		return -ENODEV;

	info = &atmel_info[line];
	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	set_ints_mode(1, info);
	
	retval = startup(info);
	if (retval)
		return retval;

	return block_til_ready(tty, filp, info);
}


static struct irqaction irq_usart0 = { 
	.handler = 	rs_interrupta,
	.name =		"usart0",
};
static struct irqaction irq_usart1 = {
	.handler =	rs_interruptb,
	.name =		"usart1",
};

static void interrupts_init(void)
{
	setup_irq(IRQ_USART0, &irq_usart0);
	setup_irq(IRQ_USART1, &irq_usart1);
}

static void show_serial_version(void)
{
	printk("Atmel USART driver version 0.99\n");
}

static struct tty_operations rs_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.chars_in_buffer = rs_chars_in_buffer,
	.flush_buffer = rs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,
	.unthrottle = rs_unthrottle,
	.set_termios = rs_set_termios,
	.stop = rs_stop,
	.start = rs_start,
	.hangup = rs_hangup,
	.set_ldisc = rs_set_ldisc,
};

/* rs_init inits the driver */
static int __init
rs_atmel_init(void)
{
	struct atmel_serial *info;
	unsigned long flags;
	int i;

	/* initialise PIO for serial port */
	HW_AT91_USART_INIT 

	serial_driver = alloc_tty_driver(2);
	if (!serial_driver)
		return -ENOMEM;

	// FIXME - do this right
	rx_buf_table[0] = rx_buf1;
	rx_buf_table[1] = rx_buf2;
	
	show_serial_version();

	/* Initialize the tty_driver structure */

	// set the tty_struct pointers to NULL to let the layer
	// above allocate the structs.
	for (i=0; i < AT91_USART_CNT; i++)
		serial_table[i] = NULL;
		
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag = 
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &rs_ops);

	if (tty_register_driver(serial_driver)) {
		put_tty_driver(serial_driver);
		printk(KERN_ERR "Couldn't register serial driver\n");
		return -ENOMEM;
	}

	local_irq_save(flags);
	for (i = 0; i < 2; i++) {
		info = &atmel_info[i];
		info->magic = SERIAL_MAGIC;
		info->usart = usarts[i];
		info->tty = 0;
		info->irqmask = (i) ? (1<<IRQ_USART1) : (1<<IRQ_USART0);
		info->irq = (i) ? IRQ_USART1 : IRQ_USART0;
#ifdef CONFIG_SWAP_ATMEL_PORTS
		info->port = (i) ? 2 : 1;
		info->line = !i;
#ifdef CONFIG_ATMEL_CONSOLE
		info->is_cons = i;
#else
		info->is_cons = 0;
#endif	
#else
		info->port = (i) ? 1 : 2;
		info->line = i;
#ifdef CONFIG_ATMEL_CONSOLE
		info->is_cons = !i;
#else
		info->is_cons = 0;
#endif	
#endif
#ifdef CONFIG_CONSOLE_ON_SC28L91
		info->line += 1;
#endif
		set_ints_mode(0, info);
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->cts_state = 1;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
	    	INIT_WORK(&info->tqueue, do_softint, info);
	    	INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->rx_buf = rx_buf_table[i];

		printk("%s%d at 0x%p (irq = %d)", serial_driver->name, info->line,
			   info->usart, info->irq);
		printk(" is a builtin Atmel APB USART\n");
	}
	
	// FIXME
	info->usart->cr = 0x1ac; // reset, disable
	info->usart->idr = 0xffffffff; // disable all interrupts
	info->usart->tcr = 0; // stop transmit
	info->usart->rcr = 0; // stop receive
	
	interrupts_init();

	local_irq_restore(flags);
	// hack to do polling
	serialpoll.func = serpoll;
	serialpoll.data = 0;

	return 0;
}

module_init(rs_atmel_init);

#if 0
/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
/* SPARC: Unused at this time, just here to make things link. */
static int register_serial(struct serial_struct *req)
{
	return -1;
}

static void unregister_serial(int line)
{
	return;
}

static void dbg_putc(int ch)
{
	static char tmp[2];
#define US_TPR  (0x38) /* Transmit Pointer Register */
#define US_TCR  (0x3C) /* Transmit Counter Register */

	tmp[0] = ch;

	outl_t((unsigned long) tmp, (USART0_BASE + US_TPR) );
	outl_t(1, (USART0_BASE + US_TCR) );

	while (inl_t((USART0_BASE + US_TCR) )) {
	}
}

static void dbg_print(const char *str)
{
	const char *p;

	for (p = str; *p; p++) {
		if (*p == '\n') {
			dbg_putc('\r');
		}
		dbg_putc(*p);
	}
}

static void dbg_printk(const char *fmt, ...)
{
	char tmp[256];
	va_list args;

	va_start(args, fmt);
	vsprintf(tmp, fmt, args);
	va_end(args);
	dbg_print(tmp);
}

static void rs_atmel_print(const char *str)
{
	dbg_printk(str);
}

static void dump_a(unsigned long a, unsigned int s)
{
	unsigned long q;

	for (q = 0; q < s; q++) {
		if (q % 16 == 0) {
			dbg_printk("%08X: ", q + a);
		}
		if (q % 16 == 7) {
			dbg_printk("%02X-", *(unsigned char *) (q + a));
		} else {
			dbg_printk("%02X ", *(unsigned char *) (q + a));
		}
		if (q % 16 == 15) {
			dbg_printk(" :\n");
		}
	}
	if (q % 16) {
		dbg_printk(" :\n");
	}
}
#endif

int atmel_console_setup(struct console *cp, char *arg)
{
  if (!cp)
       return(-1);
  HW_AT91_USART_INIT
  return 0;
}

static struct tty_driver *atmel_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}

void atmel_console_write (struct console *co, const char *str,
			   unsigned int count)
{
	struct atmel_serial *info;

#ifdef CONFIG_SWAP_ATMEL_PORTS
	info = &atmel_info[1];
#else
	info = &atmel_info[0];
#endif

	if (!atmel_console_initialized) {
		init_console(info);
		uart_init(info);
		info->baud = 9600;
		tx_stop(info->usart);
		rx_stop(info->usart);
		uart_speed(info, 0xffff);
		tx_start(info->usart);
		rx_start(info->usart, info->use_ints);
	}

    	while (count--) {
        	if (*str == '\n')
           		rs_put_char(info,'\r');
        	rs_put_char(info, *str++ );
    	}
}

static struct console atmel_driver = {
	name:		"ttyS",
	write:		atmel_console_write,
	device:		atmel_console_device,
	setup:		atmel_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};


static int __init atmel_console_init(void)
{
	register_console(&atmel_driver);
	return 0;
}

console_initcall(atmel_console_init);
