/* serial-atmel.h: Definitions for the Atmel serial driver.
 *
 * Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *                     D. Jeff Dionne <jeff@uClinux.org>,
 *                     The Silver Hammer Group, Ltd.
 * Copyright (C) 2004 Hyok S. Choi <hyok.choi@samsung.com>
 *			   2.6 port
 *
 * Based on:
 *
 * drivers/char/68328serial.h
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _ATMEL_SERIAL_H
#define _ATMEL_SERIAL_H

#include <asm/page.h>
#include <asm/arch/hardware.h>

/*
 * For the close wait times, 0 means wait forever for serial port to
 * flush its output.  65535 means don't wait at all.
 */
#define S_CLOSING_WAIT_INF	0
#define S_CLOSING_WAIT_NONE	65535

/*
 * Definitions for S_struct (and serial_struct) flags field
 */
#define S_HUP_NOTIFY 0x0001 /* Notify getty on hangups and closes 
				   on the callout port */
#define S_FOURPORT  0x0002	/* Set OU1, OUT2 per AST Fourport settings */
#define S_SAK	0x0004	/* Secure Attention Key (Orange book) */
#define S_SPLIT_TERMIOS 0x0008 /* Separate termios for dialin/callout */

#define S_SPD_MASK	0x0030
#define S_SPD_HI	0x0010	/* Use 56000 instead of 38400 bps */

#define S_SPD_VHI	0x0020  /* Use 115200 instead of 38400 bps */
#define S_SPD_CUST	0x0030  /* Use user-specified divisor */

#define S_SKIP_TEST	0x0040 /* Skip UART test during autoconfiguration */
#define S_AUTO_IRQ  0x0080 /* Do automatic IRQ during autoconfiguration */
#define S_SESSION_LOCKOUT 0x0100 /* Lock out cua opens based on session */
#define S_PGRP_LOCKOUT    0x0200 /* Lock out cua opens based on pgrp */
#define S_CALLOUT_NOHUP   0x0400 /* Don't do hangups for cua device */

#define S_FLAGS	0x0FFF	/* Possible legal S flags */
#define S_USR_MASK 0x0430	/* Legal flags that non-privileged
				 * users can set or reset */

/* Internal flags used only by kernel/chr_drv/serial.c */
#define S_INITIALIZED	0x80000000 /* Serial port was initialized */
#define S_CALLOUT_ACTIVE	0x40000000 /* Call out device is active */
#define S_NORMAL_ACTIVE	0x20000000 /* Normal device is active */
#define S_BOOT_AUTOCONF	0x10000000 /* Autoconfigure port on bootup */
#define S_CLOSING		0x08000000 /* Serial port is closing */
#define S_CTS_FLOW		0x04000000 /* Do CTS flow control */
#define S_CHECK_CD		0x02000000 /* i.e., CLOCAL */

#define RS_EVENT_WRITE_WAKEUP   0
#define SERIAL_MAGIC 0x5301

/* Software state per channel */

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct atmel_serial {
	char soft_carrier;  /* Use soft carrier on this channel */
	char break_abort;   /* Is serial console in, so process brk/abrt */
	char is_cons;       /* Is this our console. */

	/* We need to know the current clock divisor
	 * to read the bps rate the chip has currently
	 * loaded.
	 */
	unsigned char clk_divisor;  /* May be 1, 16, 32, or 64 */
	int baud;
	int			magic;
	int			baud_base;
	int			port;
	int			irq;
	int			irqmask;
	int			flags; 		/* defined in tty.h */
	int			type; 		/* UART type */
	int			use_ints;
	volatile struct atmel_usart_regs	*usart;
	int			cts_state;
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int			custom_divisor;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	unsigned char 		*xmit_buf;
	unsigned char 		*rx_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct work_struct	tqueue;
	struct work_struct	tqueue_hangup;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
};

#endif /* !(_ATMEL_SERIAL_H) */
