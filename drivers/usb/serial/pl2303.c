/*
 * Prolific PL2303 USB to serial adaptor driver
 *
 * Copyright (C) 2001-2007 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2003 IBM Corp.
 *
 * Original driver for 2.2.x by anonymous
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <asm/unaligned.h>
#include "pl2303.h"

/*
 * Version Information
 */
#define DRIVER_DESC "Prolific PL2303 USB to serial adaptor driver"

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_RSAQ2) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_DCU11) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_RSAQ3) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_PHAROS) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_ALDIGA) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_MMX) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_GPRS) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_HCR331) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_MOTOROLA) },
	{ USB_DEVICE(IODATA_VENDOR_ID, IODATA_PRODUCT_ID) },
	{ USB_DEVICE(IODATA_VENDOR_ID, IODATA_PRODUCT_ID_RSAQ5) },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_ID) },
	{ USB_DEVICE(ATEN_VENDOR_ID2, ATEN_PRODUCT_ID) },
	{ USB_DEVICE(ELCOM_VENDOR_ID, ELCOM_PRODUCT_ID) },
	{ USB_DEVICE(ELCOM_VENDOR_ID, ELCOM_PRODUCT_ID_UCSGT) },
	{ USB_DEVICE(ITEGNO_VENDOR_ID, ITEGNO_PRODUCT_ID) },
	{ USB_DEVICE(ITEGNO_VENDOR_ID, ITEGNO_PRODUCT_ID_2080) },
	{ USB_DEVICE(MA620_VENDOR_ID, MA620_PRODUCT_ID) },
	{ USB_DEVICE(RATOC_VENDOR_ID, RATOC_PRODUCT_ID) },
	{ USB_DEVICE(TRIPP_VENDOR_ID, TRIPP_PRODUCT_ID) },
	{ USB_DEVICE(RADIOSHACK_VENDOR_ID, RADIOSHACK_PRODUCT_ID) },
	{ USB_DEVICE(DCU10_VENDOR_ID, DCU10_PRODUCT_ID) },
	{ USB_DEVICE(SITECOM_VENDOR_ID, SITECOM_PRODUCT_ID) },
	{ USB_DEVICE(ALCATEL_VENDOR_ID, ALCATEL_PRODUCT_ID) },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_PRODUCT_ID) },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_SX1) },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_X65) },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_X75) },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_EF81) },
	{ USB_DEVICE(BENQ_VENDOR_ID, BENQ_PRODUCT_ID_S81) }, /* Benq/Siemens S81 */
	{ USB_DEVICE(SYNTECH_VENDOR_ID, SYNTECH_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_CA42_VENDOR_ID, NOKIA_CA42_PRODUCT_ID) },
	{ USB_DEVICE(CA_42_CA42_VENDOR_ID, CA_42_CA42_PRODUCT_ID) },
	{ USB_DEVICE(SAGEM_VENDOR_ID, SAGEM_PRODUCT_ID) },
	{ USB_DEVICE(LEADTEK_VENDOR_ID, LEADTEK_9531_PRODUCT_ID) },
	{ USB_DEVICE(SPEEDDRAGON_VENDOR_ID, SPEEDDRAGON_PRODUCT_ID) },
	{ USB_DEVICE(DATAPILOT_U2_VENDOR_ID, DATAPILOT_U2_PRODUCT_ID) },
	{ USB_DEVICE(BELKIN_VENDOR_ID, BELKIN_PRODUCT_ID) },
	{ USB_DEVICE(ALCOR_VENDOR_ID, ALCOR_PRODUCT_ID) },
	{ USB_DEVICE(WS002IN_VENDOR_ID, WS002IN_PRODUCT_ID) },
	{ USB_DEVICE(COREGA_VENDOR_ID, COREGA_PRODUCT_ID) },
	{ USB_DEVICE(YCCABLE_VENDOR_ID, YCCABLE_PRODUCT_ID) },
	{ USB_DEVICE(SUPERIAL_VENDOR_ID, SUPERIAL_PRODUCT_ID) },
	{ USB_DEVICE(HP_VENDOR_ID, HP_LD220_PRODUCT_ID) },
	{ USB_DEVICE(CRESSI_VENDOR_ID, CRESSI_EDY_PRODUCT_ID) },
	{ USB_DEVICE(ZEAGLE_VENDOR_ID, ZEAGLE_N2ITION3_PRODUCT_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_QN3USB_PRODUCT_ID) },
	{ USB_DEVICE(SANWA_VENDOR_ID, SANWA_PRODUCT_ID) },
	{ USB_DEVICE(ADLINK_VENDOR_ID, ADLINK_ND6530_PRODUCT_ID) },
	{ USB_DEVICE(SMART_VENDOR_ID, SMART_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

#define SET_LINE_REQUEST_TYPE		0x21
#define SET_LINE_REQUEST		0x20

#define SET_CONTROL_REQUEST_TYPE	0x21
#define SET_CONTROL_REQUEST		0x22
#define CONTROL_DTR			0x01
#define CONTROL_RTS			0x02

#define BREAK_REQUEST_TYPE		0x21
#define BREAK_REQUEST			0x23
#define BREAK_ON			0xffff
#define BREAK_OFF			0x0000

#define GET_LINE_REQUEST_TYPE		0xa1
#define GET_LINE_REQUEST		0x21

#define VENDOR_WRITE_REQUEST_TYPE	0x40
#define VENDOR_WRITE_REQUEST		0x01

#define VENDOR_READ_REQUEST_TYPE	0xc0
#define VENDOR_READ_REQUEST		0x01

#define UART_STATE			0x08
#define UART_STATE_TRANSIENT_MASK	0x74
#define UART_DCD			0x01
#define UART_DSR			0x02
#define UART_BREAK_ERROR		0x04
#define UART_RING			0x08
#define UART_FRAME_ERROR		0x10
#define UART_PARITY_ERROR		0x20
#define UART_OVERRUN_ERROR		0x40
#define UART_CTS			0x80


enum pl2303_type {
	type_0,		/* don't know the difference between type 0 and */
	type_1,		/* type 1, until someone from prolific tells us... */
	HX,		/* HX version of the pl2303 chip */
};

struct pl2303_serial_private {
	enum pl2303_type type;
};

struct pl2303_private {
	spinlock_t lock;
	u8 line_control;
	u8 line_status;
};

static int pl2303_vendor_read(__u16 value, __u16 index,
		struct usb_serial *serial, unsigned char *buf)
{
	int res = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			VENDOR_READ_REQUEST, VENDOR_READ_REQUEST_TYPE,
			value, index, buf, 1, 100);
	dev_dbg(&serial->interface->dev, "0x%x:0x%x:0x%x:0x%x  %d - %x\n",
		VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, value, index,
		res, buf[0]);
	return res;
}

static int pl2303_vendor_write(__u16 value, __u16 index,
		struct usb_serial *serial)
{
	int res = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			VENDOR_WRITE_REQUEST, VENDOR_WRITE_REQUEST_TYPE,
			value, index, NULL, 0, 100);
	dev_dbg(&serial->interface->dev, "0x%x:0x%x:0x%x:0x%x  %d\n",
		VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, value, index,
		res);
	return res;
}

static int pl2303_startup(struct usb_serial *serial)
{
	struct pl2303_serial_private *spriv;
	enum pl2303_type type = type_0;
	unsigned char *buf;

	spriv = kzalloc(sizeof(*spriv), GFP_KERNEL);
	if (!spriv)
		return -ENOMEM;

	buf = kmalloc(10, GFP_KERNEL);
	if (!buf) {
		kfree(spriv);
		return -ENOMEM;
	}

	if (serial->dev->descriptor.bDeviceClass == 0x02)
		type = type_0;
	else if (serial->dev->descriptor.bMaxPacketSize0 == 0x40)
		type = HX;
	else if (serial->dev->descriptor.bDeviceClass == 0x00)
		type = type_1;
	else if (serial->dev->descriptor.bDeviceClass == 0xFF)
		type = type_1;
	dev_dbg(&serial->interface->dev, "device type: %d\n", type);

	spriv->type = type;
	usb_set_serial_data(serial, spriv);

	pl2303_vendor_read(0x8484, 0, serial, buf);
	pl2303_vendor_write(0x0404, 0, serial);
	pl2303_vendor_read(0x8484, 0, serial, buf);
	pl2303_vendor_read(0x8383, 0, serial, buf);
	pl2303_vendor_read(0x8484, 0, serial, buf);
	pl2303_vendor_write(0x0404, 1, serial);
	pl2303_vendor_read(0x8484, 0, serial, buf);
	pl2303_vendor_read(0x8383, 0, serial, buf);
	pl2303_vendor_write(0, 1, serial);
	pl2303_vendor_write(1, 0, serial);
	if (type == HX)
		pl2303_vendor_write(2, 0x44, serial);
	else
		pl2303_vendor_write(2, 0x24, serial);

	kfree(buf);
	return 0;
}

static void pl2303_release(struct usb_serial *serial)
{
	struct pl2303_serial_private *spriv;

	spriv = usb_get_serial_data(serial);
	kfree(spriv);
}

static int pl2303_port_probe(struct usb_serial_port *port)
{
	struct pl2303_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;

	return 0;
}

static int pl2303_port_remove(struct usb_serial_port *port)
{
	struct pl2303_private *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static int pl2303_set_control_lines(struct usb_serial_port *port, u8 value)
{
	struct usb_device *dev = port->serial->dev;
	int retval;

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 SET_CONTROL_REQUEST, SET_CONTROL_REQUEST_TYPE,
				 value, 0, NULL, 0, 100);
	dev_dbg(&port->dev, "%s - value = %d, retval = %d\n", __func__,
		value, retval);
	return retval;
}

static void pl2303_encode_baudrate(struct tty_struct *tty,
					struct usb_serial_port *port,
					u8 buf[4])
{
	const int baud_sup[] = { 75, 150, 300, 600, 1200, 1800, 2400, 3600,
	                         4800, 7200, 9600, 14400, 19200, 28800, 38400,
	                         57600, 115200, 230400, 460800, 500000, 614400,
	                         921600, 1228800, 2457600, 3000000, 6000000 };

	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	int baud;
	int i;

	/*
	 * NOTE: Only the values defined in baud_sup are supported!
	 *       => if unsupported values are set, the PL2303 seems to use
	 *          9600 baud (at least my PL2303X always does)
	 */
	baud = tty_get_baud_rate(tty);
	dev_dbg(&port->dev, "baud requested = %d\n", baud);
	if (!baud)
		return;

	/* Set baudrate to nearest supported value */
	for (i = 0; i < ARRAY_SIZE(baud_sup); ++i) {
		if (baud_sup[i] > baud)
			break;
	}

	if (i == ARRAY_SIZE(baud_sup))
		baud = baud_sup[i - 1];
	else if (i > 0 && (baud_sup[i] - baud) > (baud - baud_sup[i - 1]))
		baud = baud_sup[i - 1];
	else
		baud = baud_sup[i];

	/* type_0, type_1 only support up to 1228800 baud */
	if (spriv->type != HX)
		baud = min_t(int, baud, 1228800);

	if (baud <= 115200) {
		put_unaligned_le32(baud, buf);
	} else {
		/*
		 * Apparently the formula for higher speeds is:
		 * baudrate = 12M * 32 / (2^buf[1]) / buf[0]
		 */
		unsigned tmp = 12000000 * 32 / baud;
		buf[3] = 0x80;
		buf[2] = 0;
		buf[1] = (tmp >= 256);
		while (tmp >= 256) {
			tmp >>= 2;
			buf[1] <<= 1;
		}
		buf[0] = tmp;
	}

	/* Save resulting baud rate */
	tty_encode_baud_rate(tty, baud, baud);
	dev_dbg(&port->dev, "baud set = %d\n", baud);
}

static void pl2303_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned char *buf;
	int i;
	u8 control;

	/*
	 * The PL2303 is reported to lose bytes if you change serial settings
	 * even to the same values as before. Thus we actually need to filter
	 * in this specific case.
	 */
	if (old_termios && !tty_termios_hw_change(&tty->termios, old_termios))
		return;

	buf = kzalloc(7, GFP_KERNEL);
	if (!buf) {
		dev_err(&port->dev, "%s - out of memory.\n", __func__);
		/* Report back no change occurred */
		if (old_termios)
			tty->termios = *old_termios;
		return;
	}

	i = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			    GET_LINE_REQUEST, GET_LINE_REQUEST_TYPE,
			    0, 0, buf, 7, 100);
	dev_dbg(&port->dev, "0xa1:0x21:0:0  %d - %7ph\n", i, buf);

	switch (C_CSIZE(tty)) {
	case CS5:
		buf[6] = 5;
		break;
	case CS6:
		buf[6] = 6;
		break;
	case CS7:
		buf[6] = 7;
		break;
	default:
	case CS8:
		buf[6] = 8;
	}
	dev_dbg(&port->dev, "data bits = %d\n", buf[6]);

	/* For reference buf[0]:buf[3] baud rate value */
	pl2303_encode_baudrate(tty, port, &buf[0]);

	/* For reference buf[4]=0 is 1 stop bits */
	/* For reference buf[4]=1 is 1.5 stop bits */
	/* For reference buf[4]=2 is 2 stop bits */
	if (C_CSTOPB(tty)) {
		/*
		 * NOTE: Comply with "real" UARTs / RS232:
		 *       use 1.5 instead of 2 stop bits with 5 data bits
		 */
		if (C_CSIZE(tty) == CS5) {
			buf[4] = 1;
			dev_dbg(&port->dev, "stop bits = 1.5\n");
		} else {
			buf[4] = 2;
			dev_dbg(&port->dev, "stop bits = 2\n");
		}
	} else {
		buf[4] = 0;
		dev_dbg(&port->dev, "stop bits = 1\n");
	}

	if (C_PARENB(tty)) {
		/* For reference buf[5]=0 is none parity */
		/* For reference buf[5]=1 is odd parity */
		/* For reference buf[5]=2 is even parity */
		/* For reference buf[5]=3 is mark parity */
		/* For reference buf[5]=4 is space parity */
		if (C_PARODD(tty)) {
			if (tty->termios.c_cflag & CMSPAR) {
				buf[5] = 3;
				dev_dbg(&port->dev, "parity = mark\n");
			} else {
				buf[5] = 1;
				dev_dbg(&port->dev, "parity = odd\n");
			}
		} else {
			if (tty->termios.c_cflag & CMSPAR) {
				buf[5] = 4;
				dev_dbg(&port->dev, "parity = space\n");
			} else {
				buf[5] = 2;
				dev_dbg(&port->dev, "parity = even\n");
			}
		}
	} else {
		buf[5] = 0;
		dev_dbg(&port->dev, "parity = none\n");
	}

	i = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    SET_LINE_REQUEST, SET_LINE_REQUEST_TYPE,
			    0, 0, buf, 7, 100);
	dev_dbg(&port->dev, "0x21:0x20:0:0  %d\n", i);

	/* change control lines if we are switching to or from B0 */
	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	if (C_BAUD(tty) == B0)
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	if (control != priv->line_control) {
		control = priv->line_control;
		spin_unlock_irqrestore(&priv->lock, flags);
		pl2303_set_control_lines(port, control);
	} else {
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	memset(buf, 0, 7);
	i = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			    GET_LINE_REQUEST, GET_LINE_REQUEST_TYPE,
			    0, 0, buf, 7, 100);
	dev_dbg(&port->dev, "0xa1:0x21:0:0  %d - %7ph\n", i, buf);

	if (C_CRTSCTS(tty)) {
		if (spriv->type == HX)
			pl2303_vendor_write(0x0, 0x61, serial);
		else
			pl2303_vendor_write(0x0, 0x41, serial);
	} else {
		pl2303_vendor_write(0x0, 0x0, serial);
	}

	kfree(buf);
}

static void pl2303_dtr_rts(struct usb_serial_port *port, int on)
{
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	/* Change DTR and RTS */
	if (on)
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	else
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);
	pl2303_set_control_lines(port, control);
}

static void pl2303_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
}

static int pl2303_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	int result;

	if (spriv->type != HX) {
		usb_clear_halt(serial->dev, port->write_urb->pipe);
		usb_clear_halt(serial->dev, port->read_urb->pipe);
	} else {
		/* reset upstream data pipes */
		pl2303_vendor_write(8, 0, serial);
		pl2303_vendor_write(9, 0, serial);
	}

	/* Setup termios */
	if (tty)
		pl2303_set_termios(tty, port, NULL);

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed submitting interrupt urb,"
			" error %d\n", __func__, result);
		return result;
	}

	result = usb_serial_generic_open(tty, port);
	if (result) {
		usb_kill_urb(port->interrupt_in_urb);
		return result;
	}

	return 0;
}

static int pl2303_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->line_control |= CONTROL_RTS;
	if (set & TIOCM_DTR)
		priv->line_control |= CONTROL_DTR;
	if (clear & TIOCM_RTS)
		priv->line_control &= ~CONTROL_RTS;
	if (clear & TIOCM_DTR)
		priv->line_control &= ~CONTROL_DTR;
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	ret = pl2303_set_control_lines(port, control);
	if (ret)
		return usb_translate_errors(ret);

	return 0;
}

static int pl2303_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int mcr;
	unsigned int status;
	unsigned int result;

	spin_lock_irqsave(&priv->lock, flags);
	mcr = priv->line_control;
	status = priv->line_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((mcr & CONTROL_DTR)		? TIOCM_DTR : 0)
		  | ((mcr & CONTROL_RTS)	? TIOCM_RTS : 0)
		  | ((status & UART_CTS)	? TIOCM_CTS : 0)
		  | ((status & UART_DSR)	? TIOCM_DSR : 0)
		  | ((status & UART_RING)	? TIOCM_RI  : 0)
		  | ((status & UART_DCD)	? TIOCM_CD  : 0);

	dev_dbg(&port->dev, "%s - result = %x\n", __func__, result);

	return result;
}

static int pl2303_carrier_raised(struct usb_serial_port *port)
{
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	if (priv->line_status & UART_DCD)
		return 1;
	return 0;
}

static int pl2303_tiocmiwait(struct tty_struct *tty, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int prevstatus;
	unsigned int status;
	unsigned int changed;

	spin_lock_irqsave(&priv->lock, flags);
	prevstatus = priv->line_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	while (1) {
		interruptible_sleep_on(&port->port.delta_msr_wait);
		/* see if a signal did it */
		if (signal_pending(current))
			return -ERESTARTSYS;

		if (port->serial->disconnected)
			return -EIO;

		spin_lock_irqsave(&priv->lock, flags);
		status = priv->line_status;
		spin_unlock_irqrestore(&priv->lock, flags);

		changed = prevstatus ^ status;

		if (((arg & TIOCM_RNG) && (changed & UART_RING)) ||
		    ((arg & TIOCM_DSR) && (changed & UART_DSR)) ||
		    ((arg & TIOCM_CD)  && (changed & UART_DCD)) ||
		    ((arg & TIOCM_CTS) && (changed & UART_CTS))) {
			return 0;
		}
		prevstatus = status;
	}
	/* NOTREACHED */
	return 0;
}

static int pl2303_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg)
{
	struct serial_struct ser;
	struct usb_serial_port *port = tty->driver_data;

	dev_dbg(&port->dev, "%s cmd = 0x%04x\n", __func__, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		memset(&ser, 0, sizeof ser);
		ser.type = PORT_16654;
		ser.line = port->minor;
		ser.port = port->port_number;
		ser.baud_base = 460800;

		if (copy_to_user((void __user *)arg, &ser, sizeof ser))
			return -EFAULT;

		return 0;
	default:
		dev_dbg(&port->dev, "%s not supported = 0x%04x\n", __func__, cmd);
		break;
	}
	return -ENOIOCTLCMD;
}

static void pl2303_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	u16 state;
	int result;

	if (break_state == 0)
		state = BREAK_OFF;
	else
		state = BREAK_ON;
	dev_dbg(&port->dev, "%s - turning break %s\n", __func__,
			state == BREAK_OFF ? "off" : "on");

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 BREAK_REQUEST, BREAK_REQUEST_TYPE, state,
				 0, NULL, 0, 100);
	if (result)
		dev_err(&port->dev, "error sending break = %d\n", result);
}

static void pl2303_update_line_status(struct usb_serial_port *port,
				      unsigned char *data,
				      unsigned int actual_length)
{

	struct pl2303_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned long flags;
	u8 status_idx = UART_STATE;
	u8 length = UART_STATE + 1;
	u8 prev_line_status;
	u16 idv, idp;

	idv = le16_to_cpu(port->serial->dev->descriptor.idVendor);
	idp = le16_to_cpu(port->serial->dev->descriptor.idProduct);


	if (idv == SIEMENS_VENDOR_ID) {
		if (idp == SIEMENS_PRODUCT_ID_X65 ||
		    idp == SIEMENS_PRODUCT_ID_SX1 ||
		    idp == SIEMENS_PRODUCT_ID_X75) {

			length = 1;
			status_idx = 0;
		}
	}

	if (actual_length < length)
		return;

	/* Save off the uart status for others to look at */
	spin_lock_irqsave(&priv->lock, flags);
	prev_line_status = priv->line_status;
	priv->line_status = data[status_idx];
	spin_unlock_irqrestore(&priv->lock, flags);
	if (priv->line_status & UART_BREAK_ERROR)
		usb_serial_handle_break(port);
	wake_up_interruptible(&port->port.delta_msr_wait);

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;
	if ((priv->line_status ^ prev_line_status) & UART_DCD)
		usb_serial_handle_dcd_change(port, tty,
				priv->line_status & UART_DCD);
	tty_kref_put(tty);
}

static void pl2303_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned int actual_length = urb->actual_length;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__,
			      urb->actual_length, urb->transfer_buffer);

	pl2303_update_line_status(port, data, actual_length);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&port->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
}

static void pl2303_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	char tty_flag = TTY_NORMAL;
	unsigned long flags;
	u8 line_status;
	int i;

	/* update line status */
	spin_lock_irqsave(&priv->lock, flags);
	line_status = priv->line_status;
	priv->line_status &= ~UART_STATE_TRANSIENT_MASK;
	spin_unlock_irqrestore(&priv->lock, flags);
	wake_up_interruptible(&port->port.delta_msr_wait);

	if (!urb->actual_length)
		return;

	/* break takes precedence over parity, */
	/* which takes precedence over framing errors */
	if (line_status & UART_BREAK_ERROR)
		tty_flag = TTY_BREAK;
	else if (line_status & UART_PARITY_ERROR)
		tty_flag = TTY_PARITY;
	else if (line_status & UART_FRAME_ERROR)
		tty_flag = TTY_FRAME;

	if (tty_flag != TTY_NORMAL)
		dev_dbg(&port->dev, "%s - tty_flag = %d\n", __func__,
								tty_flag);
	/* overrun is special, not associated with a char */
	if (line_status & UART_OVERRUN_ERROR)
		tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);

	if (port->port.console && port->sysrq) {
		for (i = 0; i < urb->actual_length; ++i)
			if (!usb_serial_handle_sysrq_char(port, data[i]))
				tty_insert_flip_char(&port->port, data[i],
						tty_flag);
	} else {
		tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	}

	tty_flip_buffer_push(&port->port);
}

/* All of the device info needed for the PL2303 SIO serial converter */
static struct usb_serial_driver pl2303_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"pl2303",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.bulk_in_size =		256,
	.bulk_out_size =	256,
	.open =			pl2303_open,
	.close =		pl2303_close,
	.dtr_rts = 		pl2303_dtr_rts,
	.carrier_raised =	pl2303_carrier_raised,
	.ioctl =		pl2303_ioctl,
	.break_ctl =		pl2303_break_ctl,
	.set_termios =		pl2303_set_termios,
	.tiocmget =		pl2303_tiocmget,
	.tiocmset =		pl2303_tiocmset,
	.tiocmiwait =		pl2303_tiocmiwait,
	.process_read_urb =	pl2303_process_read_urb,
	.read_int_callback =	pl2303_read_int_callback,
	.attach =		pl2303_startup,
	.release =		pl2303_release,
	.port_probe =		pl2303_port_probe,
	.port_remove =		pl2303_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&pl2303_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
