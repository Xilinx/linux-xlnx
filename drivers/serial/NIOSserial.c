/*--------------------------------------------------------------------
 *
 * drivers\serial\NIOSserial.c
 *
 * Serial port driver for builtin NIOS UART.
 *
 * Copyright (C) 1995       David S. Miller    <davem@caip.rutgers.edu>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 * Copyright (C) 1998-2000  D. Jeff Dionne     <jeff@lineo.ca>
 * Copyright (C) 1999       Vladimir Gurevich  <vgurevic@cisco.com>
 * Copyright (C) 2001       Vic Phillips       <vic@microtronix.com>
 * Copyright (C) 2001       Wentao Xu          <wentao@microtronix.com>
 * Copyright (C) 2004   Microtronix Datacom Ltd
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
 *
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


#include <linux/module.h>
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
#include <linux/console.h>
#include <linux/reboot.h>
#include <linux/keyboard.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/serial.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include "NIOSserial.h"

#define DEBUG 1

//struct tty_struct NIOS_ttys;
//struct NIOS_serial *NIOS_consinfo = 0;

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

static void change_speed(struct NIOS_serial *info);

/*
 *	Configuration table, UARTs to look for at startup.
 */
static struct NIOS_serial nios_soft[] = {
//	{ 0,0,1,0,0,0,0, (nasys_printf_uart), (nasys_printf_uart_irq) }, /* ttyS0 */
	{ 0,0,1,0,0,0,0, (int) (na_uart0), (na_uart0_irq) },		/* ttyS0 */
#ifdef na_uart1
//	{ 0,0,0,0,0,0,0, (nasys_gdb_uart), (nasys_gdb_uart_irq) },	/* ttyS1 */
	{ 0,0,0,0,0,0,0, (int) (na_uart1), (na_uart1_irq) },		/* ttyS1 */
#endif
#ifdef na_uart2
	{ 0,0,0,0,0,0,0, (int) (na_uart2), (na_uart2_irq) },		/* ttyS2 */
#endif
#ifdef na_uart3
	{ 0,0,0,0,0,0,0, (int) (na_uart3), (na_uart3_irq) },		/* ttyS3 */
#endif
};

#define	NR_PORTS	(sizeof(nios_soft) / sizeof(struct NIOS_serial))


/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the memcpy_fromfs blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char tmp_buf[SERIAL_XMIT_SIZE]; /* This is cheating */
DECLARE_MUTEX(tmp_buf_sem);

static inline int serial_paranoia_check(struct NIOS_serial *info,
					char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct %s in %s\n";
	static const char *badinfo =
		"Warning: null nios_serial for %s in %s\n";

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

/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };

/* Sets or clears DTR/RTS on the requested line */
static inline void NIOS_rtsdtr(struct NIOS_serial *ss, int set)
{
	if (set) {
		/* set the RTS/CTS line */
	} else {
		/* clear it */
	}
	return;
}

/* Utility routines */
static inline int get_baud(struct NIOS_serial *ss)
{
	return 0;	/* not implemented yet */
}

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
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	np_uart *	uart= (np_uart *)(info->port);
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;

	local_irq_save(flags);
	uart->np_uartcontrol &= ~np_uartcontrol_itrdy_mask;
	local_irq_restore(flags);
}

static void rs_put_char(char ch, struct NIOS_serial *info)
{
	int flags, loops = 0;
	np_uart *	uart= (np_uart *)(info->port);

	local_irq_save(flags);

	while (!(uart->np_uartstatus & np_uartstatus_trdy_mask) && (loops < 100000)) {
		loops++;
	}

	uart->np_uarttxdata = ch;
	local_irq_restore(flags);
}

static void rs_start(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	unsigned long flags;
	np_uart *	uart= (np_uart *)(info->port);

	if (serial_paranoia_check(info, tty->name, "rs_start"))
		return;

	local_irq_save(flags);
	if (info->xmit_cnt && info->xmit_buf && !(uart->np_uartcontrol & np_uartcontrol_itrdy_mask)) {
#ifdef USE_INTS
		uart->np_uartcontrol &= np_uartcontrol_itrdy_mask;
#endif
	}
	local_irq_restore(flags);
}

/* Drop into either the boot monitor or gdb upon receiving a break
 * from keyboard/console input.
 */
#ifdef CONFIG_MAGIC_SYSRQ
static void batten_down_hatches(void)
{
	/* Drop into the debugger */
  //	nios_gdb_breakpoint();
}
#endif // CONFIG_MAGIC_SYSRQ

static _INLINE_ void status_handle(struct NIOS_serial *info, unsigned short status)
{
	return;
}

static _INLINE_ void receive_chars(struct NIOS_serial *info, unsigned short rx)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, flag;
	np_uart *	uart= (np_uart *)(info->port);

	/*
	 * This do { } while() loop will get ALL chars out of Rx FIFO
         */
	do {
		ch = uart->np_uartrxdata;

		if(info->is_cons) {
#ifdef CONFIG_MAGIC_SYSRQ
			if(rx & np_uartstatus_brk_mask) {
				batten_down_hatches();
				return;
			} else if (ch == 0x10) { /* ^P */
				show_state();
				show_mem();
				return;
#ifdef DEBUG
			} else if (ch == 0x02) { /* ^B */
				batten_down_hatches();
				return;
#endif
			}
#endif /* CONFIG_MAGIC_SYSRQ */
			/* It is a 'keyboard interrupt' ;-) */
#ifdef CONFIG_CONSOLE
			wake_up(&keypress_wait);
#endif
		}

		if(!tty)
			goto clear_and_exit;

		flag = TTY_NORMAL;

		if(rx & np_uartstatus_pe_mask) {
			flag = TTY_PARITY;
			status_handle(info, rx);
		} else if(rx & np_uartstatus_roe_mask) {
			flag = TTY_OVERRUN;
			status_handle(info, rx);
		} else if(rx & np_uartstatus_fe_mask) {
			flag = TTY_FRAME;
			status_handle(info, rx);
		} else if(rx & np_uartstatus_brk_mask) {
			flag = TTY_BREAK;
			status_handle(info, rx);
		}
		tty_insert_flip_char(tty, ch, flag);

	} while((rx = uart->np_uartstatus) & np_uartstatus_rrdy_mask);

	tty_schedule_flip(tty);

clear_and_exit:
	return;
}

static _INLINE_ void transmit_chars(struct NIOS_serial *info)
{
	struct tty_struct	*tty = info->tty;
	np_uart *	uart= (np_uart *)(info->port);
	if (info->x_char) {
		/* Send next char */
		uart->np_uarttxdata = info->x_char;
		info->x_char = 0;
		goto clear_and_return;
	}

	if((info->xmit_cnt <= 0) || info->tty->stopped) {
		/* That's peculiar... TX ints off */
		uart->np_uartcontrol &= ~np_uartcontrol_itrdy_mask;
		goto clear_and_return;
	}

	/* Send char */
	uart->np_uarttxdata = info->xmit_buf[info->xmit_tail++];
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;

	if (info->xmit_cnt < WAKEUP_CHARS)
		schedule_work(&info->tqueue);

	if(info->xmit_cnt <= 0) {
		/* All done for now... TX ints off */
		uart->np_uartcontrol &= ~np_uartcontrol_itrdy_mask;
		wake_up_interruptible(&tty->write_wait);
		goto clear_and_return;
	}

clear_and_return:
	/* Clear interrupt (should be auto)*/
	return;
}

/*
 * This is the serial driver's generic interrupt routine
 */
irqreturn_t rs_interrupt(int irq, void *dev_id)
{
	struct NIOS_serial * info = (struct NIOS_serial *) dev_id;
	np_uart *	uart= (np_uart *)(info->port);
	unsigned short stat = uart->np_uartstatus;
	uart->np_uartstatus = 0;		/* clear any error status */

	if (stat & np_uartstatus_rrdy_mask) receive_chars(info, stat);
	if (stat & np_uartstatus_trdy_mask) transmit_chars(info);
	return IRQ_HANDLED;
}

static void do_softint(void *private)
{
	struct NIOS_serial	*info = (struct NIOS_serial *) private;
	struct tty_struct	*tty;

	tty = info->tty;
	if (!tty)
		return;

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
	struct NIOS_serial	*info = (struct NIOS_serial *) private_;
	struct tty_struct	*tty;

	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}



static int startup(struct NIOS_serial * info)
{
	unsigned long flags;
	np_uart *	uart= (np_uart *)(info->port);

	if (info->flags & S_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	local_irq_save(flags);

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	change_speed(info);

	info->xmit_fifo_size = 1;
	uart->np_uartcontrol = np_uartcontrol_itrdy_mask
			     | np_uartcontrol_irrdy_mask
			     | np_uartcontrol_ibrk_mask;
	uart->np_uartrxdata;

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	info->flags |= S_INITIALIZED;
	local_irq_restore(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct NIOS_serial * info)
{
	unsigned long	flags;
	np_uart *	uart= (np_uart *)(info->port);

	uart->np_uartcontrol = 0; /* All off! */
	if (!(info->flags & S_INITIALIZED))
		return;

	local_irq_save(flags);  /* Disable interrupts */

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~S_INITIALIZED;
	local_irq_restore(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct NIOS_serial *info)
{
#if 1		/* NIOS usually has a fixed speed */

//	unsigned long ustcnt;
	unsigned cflag;
	unsigned divisor;
	int	i;
	np_uart *	uart= (np_uart *)(info->port);

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

//	ustcnt = uart->np_uartcontrol;
//	uart->np_uartcontrol = ustcnt & ~UCTRL_TE;

	i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i = (i & ~CBAUDEX) + B38400;
	}

	divisor = (unsigned)(((unsigned)nasys_clock_freq *1.0 / baud_table[i]) + 1) -1;
	uart->np_uartdivisor = divisor;
	if (uart->np_uartdivisor == divisor)
		info->baud = baud_table[i];

//	ustcnt &= ~(UCTRL_PE | UCTRL_PS);
//	if ((cflag & CSIZE) == CS8)
//		ustcnt |= USTCNT_8_7;
//	if (cflag & CSTOPB)
//		ustcnt |= USTCNT_STOP;
//	if (cflag & PARENB)
//		ustcnt |= UCTRL_PE;
//	if (!(cflag & PARODD))
//		ustcnt |= UCTRL_PS;
//	if (cflag & CRTSCTS) {
//		ustcnt |= UCTRL_FL;
//	} else {
//		ustcnt &= ~UCTRL_FL;
//	}
//	ustcnt |= UCTRL_TE;
//	uart->np_uartcontrol = ustcnt;

#endif
}

/*
 * Fair output driver allows a process to speak.
 */
#if 0
static void rs_fair_output(void)
{
	int left;		/* Output no more than that */
	unsigned long flags;
	struct NIOS_serial *info = &nios_soft;
	char c;

	if (info == 0) return;
	if (info->xmit_buf == 0) return;

	local_irq_save(flags);
	left = info->xmit_cnt;
	while (left != 0) {
		c = info->xmit_buf[info->xmit_tail];
		info->xmit_tail = (info->xmit_tail+1) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
		local_irq_restore(flags);

		rs_put_char(c, info);

		local_irq_save(flags);
		left = min(info->xmit_cnt, left-1);
	}

	/* Last character is being transmitted now (hopefully). */
	udelay(5);

	local_irq_restore(flags);
	return;
}
#endif

/*
 * console_print_NIOS is registered for printk.
 */
void console_print_NIOS(const char *p)
{
	char c;

	while((c=*(p++)) != 0) {
		if(c == '\n')
			rs_put_char('\r', nios_soft);
		rs_put_char(c, nios_soft);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
	/* rs_fair_output(); */

	return;
}

static void rs_set_ldisc(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_set_ldisc"))
		return;

	info->is_cons = (tty->termios->c_line == N_TTY);

	printk("ttyS%d console mode %s\n", info->line, info->is_cons ? "on" : "off");
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	unsigned long flags;
	np_uart *	uart= (np_uart *)(info->port);

	if (serial_paranoia_check(info, tty->name, "rs_flush_chars"))
		return;

	local_irq_save(flags);

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		goto flush_exit;

	/* Enable transmitter */

	uart->np_uartcontrol |= np_uartcontrol_itrdy_mask;

	if (uart->np_uartstatus & np_uartstatus_trdy_mask) {
		/* Send char */
		uart->np_uarttxdata = info->xmit_buf[info->xmit_tail++];
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
	}

flush_exit:
	local_irq_restore(flags);
}

extern void console_printn(const char * b, int count);

static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	unsigned long flags;
	np_uart *	uart= (np_uart *)(info->port);

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;

	local_save_flags(flags);
	while (1) {
		local_irq_disable();
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		memcpy(info->xmit_buf + info->xmit_head, buf, c);
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

		uart->np_uartcontrol |= np_uartcontrol_itrdy_mask;

		if (uart->np_uartstatus & np_uartstatus_trdy_mask) {
			uart->np_uarttxdata = info->xmit_buf[info->xmit_tail++];
			info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
			info->xmit_cnt--;
		}

		local_irq_restore(flags);
	}
	local_irq_restore(flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	int	ret;

	if (serial_paranoia_check(info, tty->name, "rs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;
	local_irq_disable();
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	local_irq_enable();
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;

	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line (do this atomic) */
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;

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

static int get_serial_info(struct NIOS_serial * info,
			   struct serial_struct * retinfo)
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
	copy_to_user(retinfo,&tmp,sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct NIOS_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct NIOS_serial old_info;
	int 			retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial,new_info,sizeof(new_serial));
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
	retval = startup(info);
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
static int get_lsr_info(struct NIOS_serial * info, unsigned int *value)
{
	unsigned char status;

	status = 0;
	put_user(status,value);
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(	struct NIOS_serial * info, int duration)
{
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error;
	struct NIOS_serial * info = (struct NIOS_serial *)tty->driver_data;
	int retval;

	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
		case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!arg)
				send_break(info, HZ/4);	/* 1/4 second */
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			send_break(info, arg ? arg*(HZ/10) : HZ/4);
			return 0;
		case TIOCGSOFTCAR:
			error = put_user(C_CLOCAL(tty) ? 1 : 0,
				    (unsigned long *) arg);
			if (error)
				return error;
			return 0;
		case TIOCSSOFTCAR:
			get_user(arg, (unsigned long *) arg);
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
		case TIOCGSERIAL:
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct serial_struct));
			if (error)
				return error;
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			error = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned int));
			if (error)
				return error;
			else
			    return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct NIOS_serial));
			if (error)
				return error;
			copy_to_user((struct NIOS_serial *) arg,
				    info, sizeof(struct NIOS_serial));
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct NIOS_serial *info = (struct NIOS_serial *)tty->driver_data;
	int	oldbaud = info->baud;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed(info);

	if (info->baud == oldbaud) /* can not change */
		tty->termios->c_cflag = old_termios->c_cflag;
	else	/* only speed can be changed */
		tty->termios->c_cflag = (tty->termios->c_cflag & CBAUD) | CS8 | CREAD | HUPCL | CLOCAL;
#if 0
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
#endif
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
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct NIOS_serial * info = (struct NIOS_serial *)tty->driver_data;
	unsigned long flags;
	np_uart *	uart= (np_uart *)(info->port);

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;

	local_irq_save(flags);

	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}

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

	uart->np_uartcontrol &= ~(np_uartcontrol_irrdy_mask);

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
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open)(tty);
	}
#endif
	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(S_NORMAL_ACTIVE|S_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void rs_hangup(struct tty_struct *tty)
{
	struct NIOS_serial * info = (struct NIOS_serial *)tty->driver_data;

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
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct NIOS_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	int		do_clocal = 0;

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

	info->count--;
	info->blocked_open++;
	while (1) {
		local_irq_disable();
		NIOS_rtsdtr(info, 1);
		local_irq_enable();
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
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;

	if (retval)
		return retval;
	info->flags |= S_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its S structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct NIOS_serial	*info;
	int 			retval, line;

	line = tty->index;

	if (line >=serial_driver->num) /* too many */
		return -ENODEV;

	info = &nios_soft[line];

	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	return block_til_ready(tty, filp, info);
}

/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	printk("NIOS serial driver version 0.0\n");
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
static int __init rs_nios_init(void)
{
	int flags;
	struct NIOS_serial *info;
	int			i;

	serial_driver = alloc_tty_driver(NR_PORTS);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* SPARC: Not all of this is exactly right for us. */

	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag = 
		B115200 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &rs_ops);

	if (tty_register_driver(serial_driver)) {
		put_tty_driver(serial_driver);
		printk(KERN_ERR "Couldn't register serial driver\n");
		return -ENOMEM;
	}

	local_irq_save(flags);

	/*
	 *	Configure all the attached serial ports.
	 */
	for (i = 0, info = nios_soft; (i < NR_PORTS); i++, info++) {
		info->magic = SERIAL_MAGIC;
		info->tty = 0;
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		INIT_WORK(&info->tqueue, do_softint, info);
		INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->line = i;

		printk("%s%d (irq = %d) is a builtin NIOS UART\n",
			serial_driver->name, info->line, info->irq);

//		rs_setsignals(info, 0, 0);
		if (request_irq(info->irq, rs_interrupt, 0, "NIOS serial", info))
			panic("Unable to attach NIOS serial interrupt\n");
	}

	local_irq_restore(flags);
	return 0;
}

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
/* NIOS: Unused at this time, just here to make things link. */
int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
	return;
}

module_init(rs_nios_init);

#ifdef CONFIG_NIOS_SERIAL_CONSOLE
int nios_console_setup(struct console *cp, char *arg)
{
	return 0;
}


static struct tty_driver *nios_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}


void nios_console_write (struct console *co, const char *str,
			   unsigned int count)
{
    while (count--) {
        if (*str == '\n')
           rs_put_char('\r', nios_soft);
        rs_put_char( *str++, nios_soft);
    }
}


static struct console nios_driver = {
	.name		= "ttyS",
	.write		= nios_console_write,
	.device		= nios_console_device,
	.setup		= nios_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};


static int __init nios_console_init(void)
{
	register_console(&nios_driver);
	return 0;
}

console_initcall(nios_console_init);
#endif /* CONFIG_NIOS_SERIAL_CONSOLE */
