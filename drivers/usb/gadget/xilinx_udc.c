/*
 * Xilinx USB peripheral controller driver
 *
 * (c) 2010 Xilinx, Inc.
 *
 * Copyright (C) 2004 by Thomas Rathbone
 * Copyright (C) 2005 by HP Labs
 * Copyright (C) 2005 by David Brownell
 *
 * Some parts of this driver code is based on the driver for at91-series
 * USB peripheral controller (at91_udc.c).
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */
/***************************** Include Files *********************************/

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/prefetch.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/dma-mapping.h>
#include "gadget_chips.h"


 /* Match table for of_platform binding */
static const struct of_device_id usb_of_match[] = {
	{.compatible = "xlnx,xps-usb2-device-4.00.a",},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, usb_of_match);

/****************************************************************************
DEBUG utilities
*****************************************************************************/
#define  DEBUG
#define  VERBOSE_DEBUG

/****************************************************************************
Hardware USB controller register map related constants
****************************************************************************/
/* Register offsets for the USB device.*/
#define XUSB_EP0_CONFIG_OFFSET		0x0000  /* EP0 Config Reg Offset */
#define XUSB_SETUP_PKT_ADDR_OFFSET	0x0080  /* Setup Packet Address */
#define XUSB_ADDRESS_OFFSET		0x0100  /* Address Register */
#define XUSB_CONTROL_OFFSET		0x0104  /* Control Register */
#define XUSB_STATUS_OFFSET		0x0108  /* Status Register */
#define XUSB_FRAMENUM_OFFSET		0x010C	/* Frame Number Register */
#define XUSB_IER_OFFSET			0x0110	/* Interrupt Enable Register */
#define XUSB_BUFFREADY_OFFSET		0x0114	/* Buffer Ready Register */
#define XUSB_TESTMODE_OFFSET		0x0118	/* Test Mode Register */
#define XUSB_DMA_RESET_OFFSET		0x0200  /* DMA Soft Reset Register */
#define XUSB_DMA_CONTROL_OFFSET		0x0204	/* DMA Control Register */
#define XUSB_DMA_DSAR_ADDR_OFFSET	0x0208	/* DMA source Address Reg */
#define XUSB_DMA_DDAR_ADDR_OFFSET	0x020C	/* DMA destination Addr Reg */
#define XUSB_DMA_LENGTH_OFFSET		0x0210	/* DMA Length Register */
#define XUSB_DMA_STATUS_OFFSET		0x0214	/* DMA Status Register */

/* Endpoint Configuration Space offsets */
#define XUSB_EP_CFGSTATUS_OFFSET	0x00	/* Endpoint Config Status  */
#define XUSB_EP_BUF0COUNT_OFFSET	0x08	/* Buffer 0 Count */
#define XUSB_EP_BUF1COUNT_OFFSET	0x0C	/* Buffer 1 Count */


#define XUSB_CONTROL_USB_READY_MASK	0x80000000 /* USB ready Mask */

/* Interrupt register related masks.*/
#define XUSB_STATUS_GLOBAL_INTR_MASK	0x80000000 /* Global Intr Enable */
#define XUSB_STATUS_RESET_MASK		0x00800000 /* USB Reset Mask */
#define XUSB_STATUS_SUSPEND_MASK	0x00400000 /* USB Suspend Mask */
#define XUSB_STATUS_DISCONNECT_MASK	0x00200000 /* USB Disconnect Mask */
#define XUSB_STATUS_FIFO_BUFF_RDY_MASK	0x00100000 /* FIFO Buff Ready Mask */
#define XUSB_STATUS_FIFO_BUFF_FREE_MASK	0x00080000 /* FIFO Buff Free Mask */
#define XUSB_STATUS_SETUP_PACKET_MASK	0x00040000 /* Setup packet received */
#define XUSB_STATUS_EP1_BUFF2_COMP_MASK	0x00000200 /* EP 1 Buff 2 Processed */
#define XUSB_STATUS_EP1_BUFF1_COMP_MASK	0x00000002 /* EP 1 Buff 1 Processed */
#define XUSB_STATUS_EP0_BUFF2_COMP_MASK	0x00000100 /* EP 0 Buff 2 Processed */
#define XUSB_STATUS_EP0_BUFF1_COMP_MASK	0x00000001 /* EP 0 Buff 1 Processed */
#define XUSB_STATUS_HIGH_SPEED_MASK	0x00010000 /* USB Speed Mask */
/* Suspend,Reset and Disconnect Mask */
#define XUSB_STATUS_INTR_EVENT_MASK	0x00E00000
/* Buffers  completion Mask */
#define XUSB_STATUS_INTR_BUFF_COMP_ALL_MASK	0x0000FEFF
/* Mask for buffer 0 and buffer 1 completion for all Endpoints */
#define XUSB_STATUS_INTR_BUFF_COMP_SHIFT_MASK	0x00000101
#define XUSB_STATUS_EP_BUFF2_SHIFT	8	   /* EP buffer offset */

/* Endpoint Configuration Status Register */
#define XUSB_EP_CFG_VALID_MASK		0x80000000 /* Endpoint Valid bit */
#define XUSB_EP_CFG_STALL_MASK		0x40000000 /* Endpoint Stall bit */
#define XUSB_EP_CFG_DATA_TOGGLE_MASK	0x08000000 /* Endpoint Data toggle */

/* USB device specific global configuration constants.*/
#define XUSB_MAX_ENDPOINTS		8	/* Maximum End Points */
#define XUSB_EP_NUMBER_ZERO		0	/* End point Zero */

/* Test Modes (Set Feature).*/
#define TEST_J				1	/* Chirp J Test */
#define TEST_K				2	/* Chirp K Test */
#define TEST_SE0_NAK			3	/* Chirp SE0 Test */
#define TEST_PKT			4	/* Packet Test */

#define CONFIGURATION_ONE		0x01	/* USB device configuration*/
#define STANDARD_OUT_DEVICE		0x00	/* Out device */
#define STANDARD_OUT_ENDPOINT		0x02	/* Standard Out end point */

#define XUSB_DMA_READ_FROM_DPRAM	0x80000000/**< DPRAM is the source
							address for DMA transfer
							*/
#define XUSB_DMA_DMASR_BUSY		0x80000000 /**< DMA busy*/
#define XUSB_DMA_DMASR_ERROR		0x40000000 /**< DMA Error */

/*
 * When this bit is set, the DMA buffer ready bit is set by hardware upon
 * DMA transfer completion.
 */
#define XUSB_DMA_BRR_CTRL		0x40000000 /**< DMA bufready ctrl bit */

/* Phase States */
#define SETUP_PHASE			0x0000	/* Setup Phase */
#define DATA_PHASE			0x0001  /* Data Phase */
#define STATUS_PHASE			0x0002  /* Status Phase */

#define EP_TRANSMIT		0	/* EP is IN endpoint */
#define DRIVER_VERSION  "10 October 2010" /* Driver version date */

/*****************************************************************************
	Structures and variable declarations.
*****************************************************************************/
/**
 * USB end point structure.
 *@ep usb endpoint instance
 *@queue endpoint message queue
 *@udc xilinx usb peripheral driver instance pointer
 *@epnumber endpoint number
 *@is_in endpoint direction (IN or OUT)
 *@stopped endpoint active status
 *@is_iso endpoint type(isochronous or non isochronous)
 *@maxpacket maximum packet size the endpoint can store
 *@rambase the endpoint buffer address
 *@buffer0count the size of the packet recieved in the first buffer
 *@buffer0ready the busy state of first buffer
 *@buffer1count the size of the packet received in the second buffer
 *@buffer1ready the busy state of second buffer
 *@eptype endpoint transfer type (BULK, INTERRUPT)
 *@curbufnum current buffer of endpoint that will be processed next
 *@endpointoffset the endpoint register offset value
 *@desc pointer to the usb endpoint descriptor
 **/
struct xusb_ep {
	struct usb_ep ep;
	struct list_head queue;
	struct xusb_udc *udc;
	u16 epnumber;
	u8 is_in;
	u8 stopped;
	u8 is_iso;
	u16 maxpacket;
	u32 rambase;
	u16 buffer0count;
	u8 buffer0ready;
	u16 buffer1count;
	u8 buffer1ready;
	u8 eptype;
	u8 curbufnum;
	u32 endpointoffset;
	const struct usb_endpoint_descriptor *desc;
};

/**
 * USB peripheral driver structure
 *@gadget USB gadget driver instance
 *@lock instance of spinlock
 *@ep an array of endpoint structures
 *@base_address the usb device base address
 *@driver pointer to the usb gadget driver instance
 *@dma_enabled flag indicating whether the dma is included in the system
 *@status status flag indicating the device cofiguration
 **/
struct xusb_udc {
	struct usb_gadget gadget;
	spinlock_t lock;
	struct xusb_ep ep[8];
	void __iomem *base_address;
	struct usb_gadget_driver *driver;
	bool dma_enabled;
	u8 status;
	unsigned int (*read_fn) (void __iomem *);
	void (*write_fn) (u32, void __iomem *);
};
static struct xusb_udc controller;
/**
 * Xilinx USB device request structure
 *@req Linux usb request structure
 *@queue usb device request queue
 **/
struct xusb_request {
	struct usb_request req;
	struct list_head queue;
};

/*
 * Standard USB Command Buffer Structure defined
 * as unions so that the parameters can be used in the request processing.
 */
static struct {
	struct usb_ctrlrequest setup;
	u8 *contreadptr;
	u8 *contwriteptr;
	u32 contreadcount;
	u32 contwritecount;
	u32 setupseqtx;
	u32 setupseqrx;
	u8 contreaddatabuffer[64];
} ch9_cmdbuf;

/*
 * Initial Fixed locations provided for endpoint memory address
 * in the USB core. The user needs to modify this as per his application.
 */
static u32 rambase[8] = { 0x22, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500,
			0x1600 };

static const char driver_name[] = "xilinx_udc";
static const char ep0name[] = "ep0";

/* Control endpoint configuration.*/
static struct usb_endpoint_descriptor config_bulk_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = __constant_cpu_to_le16(0x40),
};
/**
 *to_udc() - returns the udc instance pointer
 *@g pointer to the usb gadget driver instance
 **/
static inline struct xusb_udc *to_udc(struct usb_gadget *g)
{

	return container_of(g, struct xusb_udc, gadget);
}

static void xusb_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int xusb_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void xusb_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int xusb_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

/**
 * ep_configure() - Configures the given endpoint.
 * @ep:		Pointer to the usb device endpoint structure.
 * @udc:	Pointer to the usb peripheral controller structure.
 *
 * This function configures a specific endpoint with the given configuration
 * data.
 *
 **/
static void ep_configure(struct xusb_ep *ep, struct xusb_udc *udc)
{

	u32 epcfgreg = 0;

	/*
	 * Configure the end point direction, type, Max Packet Size and the
	 * EP buffer location.
	 */
	epcfgreg = ((ep->is_in << 29) | (ep->eptype << 28) |
			(ep->ep.maxpacket << 15) | (ep->rambase));
	udc->write_fn(epcfgreg, (udc->base_address + ep->endpointoffset));

	/* Set the Buffer count and the Buffer ready bits.*/
	udc->write_fn(ep->buffer0count,
			(udc->base_address + ep->endpointoffset +
			XUSB_EP_BUF0COUNT_OFFSET));
	udc->write_fn(ep->buffer1count,
			(udc->base_address + ep->endpointoffset +
			XUSB_EP_BUF1COUNT_OFFSET));
	if (ep->buffer0ready == 1)
		udc->write_fn(1 << ep->epnumber,
			(udc->base_address + XUSB_BUFFREADY_OFFSET));
	if (ep->buffer1ready == 1)
		udc->write_fn(1 << (ep->epnumber + XUSB_STATUS_EP_BUFF2_SHIFT),
			(udc->base_address + XUSB_BUFFREADY_OFFSET));
}

/**
 * ep_sendrecv() - Transmits or receives data to or from an endpoint.
 * @ep:		Pointer to the usb endpoint configuration structure.
 * @bufferptr:	Pointer to buffer containing the data to be sent.
 * @bufferlen:	The number of data bytes to be sent.
 * @direction:	The direction of data transfer (transmit or receive).
 *
 * This function copies the transmit/receive data to/from the end point buffer
 * and enables the buffer for transmission/reception.
 *
 * returns: 0 on success and 1 on failure
 *
 **/
static int ep_sendrecv(struct xusb_ep *ep, u8 *bufferptr, u32 bufferlen,
			u8 direction)
{
	u32 *eprambase;
	u32 bytestosend;
	u8 *temprambase;
	unsigned long timeout;
	u32 srcaddr = 0;
	u32 dstaddr = 0;
	int rc = 0;
	struct xusb_udc *udc = &controller;

	bytestosend = bufferlen;

	/* Put the transmit buffer into the correct ping-pong buffer.*/
	if ((!(ep->curbufnum)) && (!(ep->buffer0ready))) {
		/* Get the Buffer address and copy the transmit data.*/
		eprambase = (u32 __force *)(ep->udc->base_address +
				ep->rambase);

		if (ep->udc->dma_enabled) {
			if (direction == EP_TRANSMIT) {
				srcaddr = dma_map_single(
					ep->udc->gadget.dev.parent,
					bufferptr, bufferlen, DMA_TO_DEVICE);
				dstaddr = virt_to_phys(eprambase);
				udc->write_fn(bufferlen,
						(ep->udc->base_address +
						ep->endpointoffset +
						XUSB_EP_BUF0COUNT_OFFSET));
				udc->write_fn(XUSB_DMA_BRR_CTRL |
						(1 << (ep->epnumber)),
						(ep->udc->base_address +
						XUSB_DMA_CONTROL_OFFSET));
			} else {
				srcaddr = virt_to_phys(eprambase);
				dstaddr = dma_map_single(
					ep->udc->gadget.dev.parent,
					bufferptr, bufferlen, DMA_FROM_DEVICE);
				udc->write_fn(XUSB_DMA_BRR_CTRL |
					XUSB_DMA_READ_FROM_DPRAM |
					(1 << (ep->epnumber)),
					(ep->udc->base_address +
					XUSB_DMA_CONTROL_OFFSET));
			}
			/*
			 * Set the addresses in the DMA source and destination
			 * registers and then set the length into the DMA length
			 * register.
			 */
			udc->write_fn(srcaddr, (ep->udc->base_address +
				XUSB_DMA_DSAR_ADDR_OFFSET));
			udc->write_fn(dstaddr, (ep->udc->base_address +
				XUSB_DMA_DDAR_ADDR_OFFSET));
			udc->write_fn(bufferlen, (ep->udc->base_address +
					XUSB_DMA_LENGTH_OFFSET));
		} else {

			while (bytestosend > 3) {
				if (direction == EP_TRANSMIT)
					*eprambase++ = *(u32 *) bufferptr;
				else
					*(u32 *) bufferptr = *eprambase++;
				bufferptr += 4;
				bytestosend -= 4;
			}
			temprambase = (u8 *) eprambase;
			while (bytestosend--) {
				if (direction == EP_TRANSMIT)
					*temprambase++ = *bufferptr++;
				else
					*bufferptr++ = *temprambase++;
			}
			/*
			 * Set the Buffer count register with transmit length
			 * and enable the buffer for transmission.
			 */
			if (direction == EP_TRANSMIT)
				udc->write_fn(bufferlen,
					(ep->udc->base_address +
					ep->endpointoffset +
					XUSB_EP_BUF0COUNT_OFFSET));
			udc->write_fn(1 << ep->epnumber,
					(ep->udc->base_address +
					XUSB_BUFFREADY_OFFSET));
		}
		ep->buffer0ready = 1;
		ep->curbufnum = 1;

	} else
		if ((ep->curbufnum == 1) && (!(ep->buffer1ready))) {

			/* Get the Buffer address and copy the transmit data.*/
			eprambase = (u32 __force *)(ep->udc->base_address +
					ep->rambase + ep->ep.maxpacket);
			if (ep->udc->dma_enabled) {
				if (direction == EP_TRANSMIT) {
					srcaddr = dma_map_single(
						ep->udc->gadget.dev.parent,
						bufferptr, bufferlen,
						DMA_TO_DEVICE);
					dstaddr = virt_to_phys(eprambase);
					udc->write_fn(bufferlen,
						(ep->udc->base_address +
						ep->endpointoffset +
						XUSB_EP_BUF1COUNT_OFFSET));
					udc->write_fn(XUSB_DMA_BRR_CTRL |
						(1 << (ep->epnumber +
						XUSB_STATUS_EP_BUFF2_SHIFT)),
						(ep->udc->base_address +
						XUSB_DMA_CONTROL_OFFSET));
				} else {
					srcaddr = virt_to_phys(eprambase);
					dstaddr = dma_map_single(
						ep->udc->gadget.dev.parent,
						bufferptr, bufferlen,
						DMA_FROM_DEVICE);
				udc->write_fn(XUSB_DMA_BRR_CTRL |
						XUSB_DMA_READ_FROM_DPRAM |
						(1 << (ep->epnumber +
						XUSB_STATUS_EP_BUFF2_SHIFT)),
						(ep->udc->base_address +
						XUSB_DMA_CONTROL_OFFSET));
				}
				/*
				 * Set the addresses in the DMA source and
				 * destination registers and then set the length
				 * into the DMA length register.
				 */
				udc->write_fn(srcaddr,
					(ep->udc->base_address +
					XUSB_DMA_DSAR_ADDR_OFFSET));
				udc->write_fn(dstaddr,
					(ep->udc->base_address +
					XUSB_DMA_DDAR_ADDR_OFFSET));
				udc->write_fn(bufferlen,
					(ep->udc->base_address +
					XUSB_DMA_LENGTH_OFFSET));
			} else {
				while (bytestosend > 3) {
					if (direction == EP_TRANSMIT)
						*eprambase++ =
							*(u32 *) bufferptr;
					else
						*(u32 *) bufferptr =
							*eprambase++;
					bufferptr += 4;
					bytestosend -= 4;
				}
				temprambase = (u8 *) eprambase;
				while (bytestosend--) {
					if (direction == EP_TRANSMIT)
						*temprambase++ = *bufferptr++;
					else
						*bufferptr++ = *temprambase++;
				}
				/*
				 * Set the Buffer count register with transmit
				 * length and enable the buffer for
				 * transmission.
				 */
				if (direction == EP_TRANSMIT)
					udc->write_fn(bufferlen,
						(ep->udc->base_address +
						ep->endpointoffset +
						XUSB_EP_BUF1COUNT_OFFSET));
				udc->write_fn(1 << (ep->epnumber +
						XUSB_STATUS_EP_BUFF2_SHIFT),
						(ep->udc->base_address +
						XUSB_BUFFREADY_OFFSET));
			}
			ep->buffer1ready = 1;
			ep->curbufnum = 0;
		} else
			/*
			 * None of the ping-pong buffer is free. Return a
			 * failure.
			 */
			return 1;

	if (ep->udc->dma_enabled) {
		/*
		 * Wait till DMA transaction is complete and
		 * check whether the DMA transaction was
		 * successful.
		 */
		while ((udc->read_fn(ep->udc->base_address +
			XUSB_DMA_STATUS_OFFSET) & XUSB_DMA_DMASR_BUSY) ==
			XUSB_DMA_DMASR_BUSY) {
			timeout = jiffies + 10000;

			if (time_after(jiffies, timeout)) {
				rc = -ETIMEDOUT;
				goto clean;
			}

		}
		if ((udc->read_fn(ep->udc->base_address +
			XUSB_DMA_STATUS_OFFSET) & XUSB_DMA_DMASR_ERROR) ==
			XUSB_DMA_DMASR_ERROR)
			dev_dbg(&ep->udc->gadget.dev, "DMA Error\n");
clean:
		if (direction == EP_TRANSMIT) {

			dma_unmap_single(ep->udc->gadget.dev.parent,
				srcaddr, bufferlen, DMA_TO_DEVICE);
		} else {

			dma_unmap_single(ep->udc->gadget.dev.parent,
				dstaddr, bufferlen, DMA_FROM_DEVICE);
		}

	}
	return rc;
}

/**
 * done() -	Exeutes the endpoint data transfer completion tasks.
 * @ep:		Pointer to the usb device endpoint structure.
 * @req:	Pointer to the usb request structure.
 * @status:	Status of the data transfer.
 *
 * Deletes the message from the queue and updates data transfer completion
 * status.
 *
 **/
static void done(struct xusb_ep *ep, struct xusb_request *req, int status)
{
	u8 stopped = ep->stopped;

	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		dev_dbg(&ep->udc->gadget.dev, "%s done %p, status %d\n",
				ep->ep.name, req, status);
	ep->stopped = 1;

	spin_unlock(&ep->udc->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->udc->lock);

	ep->stopped = stopped;
}

/**
 * read_fifo() - Reads the data from the given endpoint buffer.
 * @ep:		Pointer to the usb device endpoint structure.
 * @req:	Pointer to the usb request structure.
 *
 * Pulls OUT packet data from the endpoint buffer.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int read_fifo(struct xusb_ep *ep, struct xusb_request *req)
{
	u8 *buf;
	u32 is_short, count, bufferspace;
	u8 bufoffset;
	u8 two_pkts = 0;
	struct xusb_udc *udc = &controller;

	if ((ep->buffer0ready == 1) && (ep->buffer1ready == 1)) {
		dev_dbg(&ep->udc->gadget.dev, "%s: Packet NOT ready!\n",
				__func__);
		return -EINVAL;
	}
top:
	if (ep->curbufnum)
		bufoffset = 0xC;
	else
		bufoffset = 0x08;
		count = udc->read_fn(ep->udc->base_address +
				ep->endpointoffset + bufoffset);
	if (!ep->buffer0ready && !ep->buffer1ready)
		two_pkts = 1;

	dev_dbg(&ep->udc->gadget.dev,
		"curbufnum is %d  and buf0rdy is %d, buf1rdy is %d\n",
		ep->curbufnum, ep->buffer0ready, ep->buffer1ready);

	buf = req->req.buf + req->req.actual;
	prefetchw(buf);
	bufferspace = req->req.length - req->req.actual;

	req->req.actual += min(count, bufferspace);
	is_short = (count < ep->ep.maxpacket);

	if (count) {
		if (unlikely(!bufferspace)) {
			/* This happens when the driver's buffer
			 * is smaller than what the host sent.
			 * discard the extra data.
			 */
			if (req->req.status != -EOVERFLOW)
				dev_dbg(&ep->udc->gadget.dev,
					"%s overflow %d\n", ep->ep.name, count);
			req->req.status = -EOVERFLOW;
		} else {
			if (!ep_sendrecv(ep, buf, count, 1)) {
				dev_dbg(&ep->udc->gadget.dev,
					"read %s, %d bytes%s req %p %d/%d\n",
					ep->ep.name, count,
					is_short ? "/S" : "", req,
					req->req.actual, req->req.length);
				bufferspace -= count;
				/* Completion */
				if (is_short ||
				    req->req.actual == req->req.length) {
					done(ep, req, 0);
					return 1;
				}

				if (two_pkts) {
					two_pkts = 0;
					goto top;
				}

			} else {
				dev_dbg(&ep->udc->gadget.dev,
				"rcv fail..curbufnum is %d and buf0rdy is"
				"%d, buf1rdy is %d\n", ep->curbufnum,
				ep->buffer0ready, ep->buffer1ready);
				req->req.actual -= min(count, bufferspace);
				return -EINVAL;
			}
		}
	} else
		return -EINVAL;

	return 0;
}

/**
 * write_fifo() - Writes data into the given endpoint buffer.
 * @ep:		Pointer to the usb device endpoint structure.
 * @req:	Pointer to the usb request structure.
 *
 * Loads endpoint buffer for an IN packet.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int write_fifo(struct xusb_ep *ep, struct xusb_request *req)
{
	u8 *buf;
	u32 max;
	u32 length;
	int is_last, is_short = 0;

	max = le16_to_cpu(ep->desc->wMaxPacketSize);

	if (req) {
		buf = req->req.buf + req->req.actual;
		prefetch(buf);
		length = req->req.length - req->req.actual;
	} else {
		buf = NULL;
		length = 0;
	}

	length = min(length, max);
	if (ep_sendrecv(ep, buf, length, EP_TRANSMIT) == 1) {
		buf = req->req.buf - req->req.actual;
		dev_dbg(&ep->udc->gadget.dev, "Send failure\n");
		return 0;
	} else {
		req->req.actual += length;

		if (unlikely(length != max))
			is_last = is_short = 1;
		else {
			if (likely(req->req.length != req->req.actual)
			    || req->req.zero)
				is_last = 0;
			else
				is_last = 1;
		}
		dev_dbg(&ep->udc->gadget.dev,
			"%s: wrote %s %d bytes%s%s %d left %p\n", __func__,
			ep->ep.name, length,
			is_last ? "/L" : "", is_short ? "/S" : "",
			req->req.length - req->req.actual, req);

		if (is_last) {
			done(ep, req, 0);
			return 1;
		}
	}
	return 0;
}

/**
 * nuke() -	Cleans up the data transfer message list.
 * @ep:		Pointer to the usb device endpoint structure.
 * @status:	Status of the data transfer.
 *
 **/
static void nuke(struct xusb_ep *ep, int status)
{
	struct xusb_request *req;

	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct xusb_request, queue);

		done(ep, req, status);
	}
}

/***************************** Endpoint related functions*********************/
/**
 * xusb_ep_set_halt() -	Stalls/unstalls the given endpoint.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @value:	value to indicate stall/unstall.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int xusb_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct xusb_ep *ep = container_of(_ep, struct xusb_ep, ep);
	unsigned long flags;
	u32 epcfgreg;
	struct xusb_udc *udc = &controller;

	if (!_ep || (!ep->desc && ep->epnumber))
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	if (ep->is_in && (!list_empty(&ep->queue)) && value) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EAGAIN;
	}
	if ((ep->buffer0ready == 1) || (ep->buffer1ready == 1)) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EAGAIN;
	}

	if (value) {
		/* Stall the device.*/
		epcfgreg = udc->read_fn(ep->udc->base_address +
				   ep->endpointoffset);
		epcfgreg |= XUSB_EP_CFG_STALL_MASK;

		udc->write_fn(epcfgreg,
			(ep->udc->base_address + ep->endpointoffset));
		ep->stopped = 1;
	} else {

		ep->stopped = 0;
		/* Unstall the device.*/
		epcfgreg = udc->read_fn(ep->udc->base_address +
					    ep->endpointoffset);
		epcfgreg &= ~XUSB_EP_CFG_STALL_MASK;
		udc->write_fn(epcfgreg,
		(ep->udc->base_address + ep->endpointoffset));
		if (ep->epnumber) {
			/* Reset the toggle bit.*/
			epcfgreg = udc->read_fn(ep->udc->base_address +
						    ep->endpointoffset);
			epcfgreg &= ~XUSB_EP_CFG_DATA_TOGGLE_MASK;
			udc->write_fn(epcfgreg, (ep->udc->base_address +
					   ep->endpointoffset));
		}
	}

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;
}

/**
 * xusb_ep_enable() - Enables the given endpoint.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @desc:	Pointer to usb endpoint descriptor.
 *
 * returns: 0 for success and error value on failure
 **/
static int xusb_ep_enable(struct usb_ep *_ep,
			  const struct usb_endpoint_descriptor *desc)
{
	struct xusb_ep *ep = container_of(_ep, struct xusb_ep, ep);
	struct xusb_udc *dev = ep->udc;
	u32 tmp;
	u8 eptype = 0;
	unsigned long flags;
	u32 epcfg;
	struct xusb_udc *udc = &controller;

	/*
	 * The check for _ep->name == ep0name is not done as this enable i used
	 * for enabling ep0 also. In other gadget drivers, this ep name is not
	 * used.
	 */
	if (!_ep || !desc || ep->desc ||
	    desc->bDescriptorType != USB_DT_ENDPOINT) {
		dev_dbg(&ep->udc->gadget.dev, "first check fails\n");
		return -EINVAL;
	}

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_dbg(&ep->udc->gadget.dev, "bogus device state\n");
		return -ESHUTDOWN;
	}


	ep->is_in = ((desc->bEndpointAddress & USB_DIR_IN) != 0);
	/* The address of the endpoint is encoded as follows:
	 * Bit 3...0: The endpoint number
	 * Bit 6...4: Reserved, reset to zero
	 * Bit 7: Direction, ignored for
	 * control endpoints
	 * 0 = OUT endpoint
	 * 1 = IN endpoint
	 */
	ep->epnumber = (desc->bEndpointAddress & 0x0f);
	ep->stopped = 0;
	ep->desc = desc;
	tmp = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	spin_lock_irqsave(&ep->udc->lock, flags);
	ep->ep.maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	switch (tmp) {
	case USB_ENDPOINT_XFER_CONTROL:
		dev_dbg(&ep->udc->gadget.dev, "only one control endpoint\n");
		/* NON- ISO */
		eptype = 0;
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	case USB_ENDPOINT_XFER_INT:
		/* NON- ISO */
		eptype = 0;
		if (ep->ep.maxpacket > 64)
			goto bogus_max;
		break;
	case USB_ENDPOINT_XFER_BULK:
		/* NON- ISO */
		eptype = 0;
		switch (ep->ep.maxpacket) {
		case 8:
		case 16:
		case 32:
		case 64:
		case 512:
		goto ok;
		}
bogus_max:
		dev_dbg(&ep->udc->gadget.dev, "bogus maxpacket %d\n",
			ep->ep.maxpacket);
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	case USB_ENDPOINT_XFER_ISOC:
		/* ISO */
		eptype = 1;
		ep->is_iso = 1;
		break;
	}

ok:	ep->eptype = eptype;
	ep->buffer0ready = 0;
	ep->buffer1ready = 0;
	ep->curbufnum = 0;
	ep->rambase = rambase[ep->epnumber];
	ep_configure(ep, ep->udc);

	dev_dbg(&ep->udc->gadget.dev, "Enable Endpoint %d max pkt is %d\n",
		ep->epnumber, ep->ep.maxpacket);

	/* Enable the End point.*/
	epcfg = udc->read_fn(ep->udc->base_address + ep->endpointoffset);
	epcfg |= XUSB_EP_CFG_VALID_MASK;
	udc->write_fn(epcfg,
		(ep->udc->base_address + ep->endpointoffset));
	if (ep->epnumber)
		ep->rambase <<= 2;

	if (ep->epnumber)
		udc->write_fn((udc->read_fn(ep->udc->base_address +
				XUSB_IER_OFFSET) |
				(XUSB_STATUS_INTR_BUFF_COMP_SHIFT_MASK <<
				ep->epnumber)),
				(ep->udc->base_address + XUSB_IER_OFFSET));
	if ((ep->epnumber) && (!ep->is_in)) {

		/* Set the buffer ready bits.*/
		udc->write_fn(1 << ep->epnumber, (ep->udc->base_address +
				  XUSB_BUFFREADY_OFFSET));
		ep->buffer0ready = 1;
		udc->write_fn((1 << (ep->epnumber +
				XUSB_STATUS_EP_BUFF2_SHIFT)),
				(ep->udc->base_address +
				XUSB_BUFFREADY_OFFSET));
		ep->buffer1ready = 1;
	}

	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return 0;
}

/**
 * xusb_ep_disable() - Disables the given endpoint.
 * @_ep:	Pointer to the usb device endpoint structure.
 *
 * returns: 0 for success and error value on failure
 **/
static int xusb_ep_disable(struct usb_ep *_ep)
{
	struct xusb_ep *ep = container_of(_ep, struct xusb_ep, ep);
	unsigned long flags;
	u32 epcfg;
	struct xusb_udc *udc = &controller;

	if (ep == &ep->udc->ep[XUSB_EP_NUMBER_ZERO]) {
		dev_dbg(&ep->udc->gadget.dev, "Ep0 disable called\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&ep->udc->lock, flags);

	nuke(ep, -ESHUTDOWN);

	/*
	 * Restore the endpoint's pristine config
	 */
	ep->desc = NULL;

	ep->stopped = 1;

	/* The address of the endpoint is encoded as follows:
	   Bit 3...0: The endpoint number
	   Bit 6...4: Reserved, reset to zero
	   Bit 7: Direction, ignored for
	   control endpoints
	   0 = OUT endpoint
	   1 = IN endpoint */
	dev_dbg(&ep->udc->gadget.dev, "USB Ep %d disable\n ", ep->epnumber);

	/* Disable the endpoint.*/
	epcfg = udc->read_fn(ep->udc->base_address + ep->endpointoffset);
	epcfg &= ~XUSB_EP_CFG_VALID_MASK;
	udc->write_fn(epcfg, (ep->udc->base_address + ep->endpointoffset));
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;
}

/**
 * xusb_ep_alloc_request() - Initializes the request queue.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @gfp_flags:	Flags related to the request call.
 *
 * returns: pointer to request structure on success and a NULL on failure.
 **/
static struct usb_request *xusb_ep_alloc_request(struct usb_ep *_ep,
						 gfp_t gfp_flags)
{
	struct xusb_request *req;

	req = kmalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	memset(req, 0, sizeof(*req));
	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

/**
 * xusb_ep_free_request() - Releases the request from queue.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @_req:	Pointer to the usb request structure.
 *
 **/
static void xusb_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct xusb_ep *ep = container_of(_ep, struct xusb_ep, ep);
	struct xusb_request *req;

	req = container_of(_req, struct xusb_request, req);

	if (!list_empty(&req->queue))
		dev_warn(&ep->udc->gadget.dev, "Error: No memory to free");

	kfree(req);
}

/**
 * xusb_ep_queue() - Adds the request to the queue.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @_req:	Pointer to the usb request structure.
 * @gfp_flags:	Flags related to the request call.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int xusb_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct xusb_request *req;
	struct xusb_ep *ep;
	struct xusb_udc *dev;
	unsigned long flags;
	u8 *buf;
	u32 length;
	u8 *corebuf;
	struct xusb_udc *udc = &controller;

	req = container_of(_req, struct xusb_request, req);
	ep = container_of(_ep, struct xusb_ep, ep);

	if (!_req || !_req->complete || !_req->buf ||
	    !list_empty(&req->queue)) {
		dev_dbg(&ep->udc->gadget.dev, "invalid request\n");
		return -EINVAL;
	}

	if (!_ep || (!ep->desc && ep->ep.name != ep0name)) {
		dev_dbg(&ep->udc->gadget.dev, "invalid ep\n");
		return -EINVAL;
	}

	dev = ep->udc;
	if (!dev || !dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_dbg(&ep->udc->gadget.dev, "%s, bogus device state %p\n",
			__func__, dev->driver);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* Try to kickstart any empty and idle queue */
	if (list_empty(&ep->queue)) {
		if (!ep->epnumber) {

			buf = req->req.buf + req->req.actual;
			prefetch(buf);
			length = req->req.length - req->req.actual;

			corebuf = (void __force *) ((ep->rambase << 2) +
					    ep->udc->base_address);
			while (length--)
				*corebuf++ = *buf++;
			udc->write_fn(req->req.length, (ep->udc->base_address +
					   XUSB_EP_BUF0COUNT_OFFSET));
			udc->write_fn(1, (ep->udc->base_address +
					   XUSB_BUFFREADY_OFFSET));
			req = NULL;
		} else {

			if (ep->is_in) {
				dev_dbg(&ep->udc->gadget.dev,
					"write_fifo called from queue\n");
				if (write_fifo(ep, req) == 1)
					req = NULL;
			} else {
				dev_dbg(&ep->udc->gadget.dev,
					"read_fifo called from queue\n");
				if (read_fifo(ep, req) == 1)
					req = NULL;
			}
		}
	}

	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}

/**
 * xusb_ep_dequeue() - Removes the request from the queue.
 * @_ep:	Pointer to the usb device endpoint structure.
 * @_req:	Pointer to the usb request structure.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int xusb_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct xusb_ep *ep;
	struct xusb_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct xusb_ep, ep);

	if (!_ep || ep->ep.name == ep0name)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);
	/* Make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	}

	done(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return 0;
}

static struct usb_ep_ops xusb_ep_ops = {
	.enable = xusb_ep_enable,
	.disable = xusb_ep_disable,

	.alloc_request = xusb_ep_alloc_request,
	.free_request = xusb_ep_free_request,

	.queue = xusb_ep_queue,
	.dequeue = xusb_ep_dequeue,
	.set_halt = xusb_ep_set_halt,
};

/**
 * xusb_get_frame() - Reads the current usb frame number.
 * @gadget:	Pointer to the usb gadget structure.
 *
 * returns: current frame number for success and error value on failure.
 **/
static int xusb_get_frame(struct usb_gadget *gadget)
{

	struct xusb_udc *udc = to_udc(gadget);
	unsigned long flags;
	int retval;

	if (!gadget)
		return -ENODEV;

	local_irq_save(flags);
	retval = udc->read_fn(udc->base_address + XUSB_FRAMENUM_OFFSET);
	local_irq_restore(flags);
	return retval;
}

/**
 * set_testmode() - Sets the usb device into the given test mode.
 * @udc:	Pointer to the usb controller structure.
 * @testmode:	Test mode to which the device is to be set.
 * @bufptr:	Pointer to the buffer containing the test packet.
 *
 *	This function is needed for USB certification tests.
 * returns: This function never returns if the command is successful
 *		and -ENOTSUPP on failure.
 **/
static int set_testmode(struct xusb_udc *udc, u8 testmode, u8 *bufptr)
{
	u32 *src, *dst;
	u32 count;
	u32 crtlreg;

	/* Stop the SIE.*/
	crtlreg = udc->read_fn(udc->base_address + XUSB_CONTROL_OFFSET);
	crtlreg &= ~XUSB_CONTROL_USB_READY_MASK;
	udc->write_fn(crtlreg, (udc->base_address + XUSB_CONTROL_OFFSET));
	if (testmode == TEST_PKT) {

		if (bufptr == NULL)
			/* Null pointer is passed.*/
			return -EINVAL;


		src = (u32 *) bufptr;
		dst = (u32 __force *) (udc->base_address);
		count = 14;

		/* Copy Leurker PKT to DPRAM at 0.*/
		while (count--)
			*dst++ = *src++;
	}

	/* Set the test mode.*/
	udc->write_fn(testmode, (udc->base_address + XUSB_TESTMODE_OFFSET));
	/* Re-start the SIE.*/
	udc->write_fn(XUSB_CONTROL_USB_READY_MASK,
		(udc->base_address + XUSB_CONTROL_OFFSET));

	while (1)
		;		/* Only way out is through hardware reset! */

	return 0;
}

/**
 * xusb_ioctl() - The i/o control function to call the testmode function.
 * @gadget:	Pointer to the usb gadget structure.
 * @code:	Test mode to which the device is to be set.
 * @param:	Parameter to be sent for the test.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int xusb_ioctl(struct usb_gadget *gadget, unsigned code,
		      unsigned long param)
{
	struct xusb_udc *udc = to_udc(gadget);
	u8 *BufPtr;

	BufPtr = (u8 *) param;

	if ((code == TEST_J) || (code == TEST_K) ||
	    (code == TEST_SE0_NAK) || (code == TEST_PKT))

		return set_testmode(udc, code, BufPtr);
	else
		return -EINVAL;

	return 0;
}

static int xudc_start(struct usb_gadget *gadget,
			struct usb_gadget_driver *driver);
static int xudc_stop(struct usb_gadget *gadget,
			struct usb_gadget_driver *driver);
static void xusb_release(struct device *dev);

static const struct usb_gadget_ops xusb_udc_ops = {
	.get_frame = xusb_get_frame,
	.ioctl = xusb_ioctl,
	.udc_start = xudc_start,
	.udc_stop  = xudc_stop,
};


/*-------------------------------------------------------------------------*/
static struct xusb_udc controller = {
	.gadget = {
		.ops = &xusb_udc_ops,
		.ep0 = &controller.ep[XUSB_EP_NUMBER_ZERO].ep,
		.speed = USB_SPEED_HIGH,
		.max_speed = USB_SPEED_HIGH,
		.is_otg = 0,
		.is_a_peripheral = 0,
		.b_hnp_enable = 0,
		.a_hnp_support = 0,
		.a_alt_hnp_support = 0,
		.name = driver_name,
		.dev = {
			.init_name = "xilinx_udc",
			.release = xusb_release,
			},
		},
	.ep[0] = {
		  .ep = {
			 .name = ep0name,
			 .ops = &xusb_ep_ops,
			 .maxpacket = 0x40,
			 },
			.udc = &controller,
		.epnumber = 0,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[1] = {
		  .ep = {
			 .name = "ep1",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 1,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[2] = {
		  .ep = {
			 .name = "ep2",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 2,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[3] = {
		  .ep = {
			 .name = "ep3",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 3,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[4] = {
		  .ep = {
			 .name = "ep4",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 4,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[5] = {
		  .ep = {
			 .name = "ep5",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 5,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.ep[6] = {
		  .ep = {
			 .name = "ep6",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 6,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.eptype = 0,
		.curbufnum = 0,
		.endpointoffset = 0,
		},
	.ep[7] = {
		  .ep = {
			 .name = "ep7",
			 .ops = &xusb_ep_ops,
			 },
		.udc = &controller,
		.epnumber = 7,
		.buffer0count = 0,
		.buffer0ready = 0,
		.buffer1count = 0,
		.buffer1ready = 0,
		.curbufnum = 0,
		.eptype = 0,
		.endpointoffset = 0,
		},
	.status = 0,
	.write_fn = xusb_write32_be,
	.read_fn = xusb_read32_be,
};

/**
 * xudc_reinit() - Restores inital software state.
 * @udc:	Pointer to the usb device controller structure.
 *
 **/
static void xudc_reinit(struct xusb_udc *udc)
{
	u32 ep_number;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	INIT_LIST_HEAD(&udc->gadget.ep0->ep_list);

	for (ep_number = 0; ep_number < XUSB_MAX_ENDPOINTS; ep_number++) {
		struct xusb_ep *ep = &udc->ep[ep_number];

		if (ep_number)
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		ep->desc = NULL;
		ep->stopped = 0;
		/*
		 * The configuration register address offset between
		 * each endpoint is 0x10.
		 */
		ep->endpointoffset = XUSB_EP0_CONFIG_OFFSET +
					(ep_number * 0x10);
		ep->is_in = 0;
		ep->is_iso = 0;
		ep->maxpacket = 0;
		ep_configure(ep, udc);
		udc->status = 0;

		/* Initialize one queue per endpoint */
		INIT_LIST_HEAD(&ep->queue);
	}
}

/**
 * stop_activity() - Stops any further activity on the device.
 * @udc:	Pointer to the usb device controller structure.
 *
 **/
static void stop_activity(struct xusb_udc *udc)
{
	struct usb_gadget_driver *driver = udc->driver;
	int i;

	if (udc->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	udc->gadget.speed = USB_SPEED_HIGH;

	for (i = 0; i < XUSB_MAX_ENDPOINTS; i++) {
		struct xusb_ep *ep = &udc->ep[i];

		ep->stopped = 1;
		nuke(ep, -ESHUTDOWN);
	}
	if (driver) {
		spin_unlock(&udc->lock);
		driver->disconnect(&udc->gadget);
		spin_lock(&udc->lock);
	}

	xudc_reinit(udc);
}

/**
 * startup_intrhandler() - The usb device controller interrupt handler.
 * @callbackref:	Pointer to the reference value being passed.
 * @intrstatus:		The mask value containing the interrupt sources.
 *
 *	This handler handles the RESET, SUSPEND and DISCONNECT interrupts.
 **/
static void startup_intrhandler(void *callbackref, u32 intrstatus)
{
	struct xusb_udc *udc;
	u32 intrreg;

	udc = (struct xusb_udc *) callbackref;

	if (intrstatus & XUSB_STATUS_RESET_MASK) {
		dev_dbg(&udc->gadget.dev, "Reset\n");
		if (intrstatus & XUSB_STATUS_HIGH_SPEED_MASK)
			udc->gadget.speed = USB_SPEED_HIGH;
		else
			udc->gadget.speed = USB_SPEED_FULL;

		if (udc->status == 1) {
			udc->status = 0;
			/* Set device address to 0.*/
			udc->write_fn(0, (udc->base_address +
						XUSB_ADDRESS_OFFSET));
		}
		/* Disable the Reset interrupt.*/
		intrreg = udc->read_fn(udc->base_address +
					XUSB_IER_OFFSET);

		intrreg &= ~XUSB_STATUS_RESET_MASK;
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
		/* Enable thesuspend and disconnect.*/
		intrreg =
			udc->read_fn(udc->base_address + XUSB_IER_OFFSET);
		intrreg |=
			(XUSB_STATUS_SUSPEND_MASK |
			 XUSB_STATUS_DISCONNECT_MASK);
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
	}

	if (intrstatus & XUSB_STATUS_DISCONNECT_MASK) {

		/* Disable the Disconnect interrupt.*/
		intrreg =
			udc->read_fn(udc->base_address + XUSB_IER_OFFSET);
		intrreg &= ~XUSB_STATUS_DISCONNECT_MASK;
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
		dev_dbg(&udc->gadget.dev, "Disconnect\n");
		if (udc->status == 1) {
			udc->status = 0;
			/* Set device address to 0.*/
			udc->write_fn(0, (udc->base_address +
					XUSB_ADDRESS_OFFSET));
			/* Enable the USB device.*/
			udc->write_fn(XUSB_CONTROL_USB_READY_MASK,
				(udc->base_address + XUSB_CONTROL_OFFSET));
		}

		/* Enable the suspend and reset interrupts.*/
		intrreg = (udc->read_fn(udc->base_address + XUSB_IER_OFFSET) |
				(XUSB_STATUS_SUSPEND_MASK |
				XUSB_STATUS_RESET_MASK));
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
		stop_activity(udc);
	}

	if (intrstatus & XUSB_STATUS_SUSPEND_MASK) {
		dev_dbg(&udc->gadget.dev, "Suspend\n");
		/* Disable the Suspend interrupt.*/
		intrreg = (udc->read_fn(udc->base_address + XUSB_IER_OFFSET) &
					~XUSB_STATUS_SUSPEND_MASK);
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
		/* Enable the Disconnect and reset interrupts. */
		intrreg = (udc->read_fn(udc->base_address + XUSB_IER_OFFSET)|
				(XUSB_STATUS_DISCONNECT_MASK |
				XUSB_STATUS_RESET_MASK));
		udc->write_fn(intrreg, (udc->base_address + XUSB_IER_OFFSET));
	}
}

/**
 * setup_ctrl_wr_status_stage() - Sets up the usb device status stages.
 * @udc:		Pointer to the usb device controller structure.
 *
 **/
static void setup_ctrl_wr_status_stage(struct xusb_udc *udc)
{
	u32 epcfgreg;

	epcfgreg = (udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset)|
				XUSB_EP_CFG_DATA_TOGGLE_MASK);
	udc->write_fn(epcfgreg, (udc->base_address +
			udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
	udc->write_fn(0, (udc->base_address +
			udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset +
			  XUSB_EP_BUF0COUNT_OFFSET));
	udc->write_fn(1, (udc->base_address + XUSB_BUFFREADY_OFFSET));
}

/**
 * set_configuration() - Sets the device configuration.
 * @udc:		Pointer to the usb device controller structure.
 *
 *	Processes the SET_CONFIGURATION command recieved during enumeration.
 **/
static void set_configuration(struct xusb_udc *udc)
{
	u32 epcfgreg;

	switch (ch9_cmdbuf.setup.wValue) {
	case 0:
		/*
		 * This configuration value resets the device to the
		 * un configured state like power up.
		 */
		udc->status = 0;
		/* Cause a valid status phase to be issued.*/
		setup_ctrl_wr_status_stage(udc);

		break;
	case CONFIGURATION_ONE:
		udc->status = 1;
		setup_ctrl_wr_status_stage(udc);
		break;

		/* Additional configurations can be added here.*/
	default:
		/* Stall the end point.*/
		epcfgreg = (udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset)|
				XUSB_EP_CFG_STALL_MASK);

		udc->write_fn(epcfgreg, (udc->base_address +
			udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
		break;
	}
}

/**
 * setclearfeature() - Executes the set feature and clear feature commands.
 * @udc:		Pointer to the usb device controller structure.
 * @flag:		Value deciding the set or clear action.
 *
 * Processes the SET_FEATURE and CLEAR_FEATURE commands.
 **/
static void set_clear_feature(struct xusb_udc *udc, int flag)
{
	u8 endpoint;
	u8 outinbit;
	u32 epcfgreg;

	switch (ch9_cmdbuf.setup.bRequestType) {
	case STANDARD_OUT_DEVICE:
		switch (ch9_cmdbuf.setup.wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			/* User needs to add code here.*/
			break;

		case USB_DEVICE_TEST_MODE:
			/*
			 * The Test Mode will be executed
			 * after the status phase.
			 */
			break;

		default:
			epcfgreg = udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset);
			epcfgreg |= XUSB_EP_CFG_STALL_MASK;
			udc->write_fn(epcfgreg, (udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
			break;
		}
		break;

	case STANDARD_OUT_ENDPOINT:
		if (!ch9_cmdbuf.setup.wValue) {
			endpoint = ch9_cmdbuf.setup.wIndex & 0xf;
			outinbit = ch9_cmdbuf.setup.wIndex & 0x80;
			outinbit = outinbit >> 7;

			/* Make sure direction matches.*/
			if (outinbit != udc->ep[endpoint].is_in) {
				epcfgreg = udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset);
				epcfgreg |= XUSB_EP_CFG_STALL_MASK;

				udc->write_fn(epcfgreg, (udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
				return;
			}

			if (!endpoint) {
				/* Clear the stall.*/
				epcfgreg = udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset);

				epcfgreg &= ~XUSB_EP_CFG_STALL_MASK;

				udc->write_fn(epcfgreg, (udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
				break;
			} else {
				if (flag == 1) {
					epcfgreg =
						udc->read_fn(udc->base_address +
					udc->ep[XUSB_EP_NUMBER_ZERO].
					endpointoffset);
					epcfgreg |= XUSB_EP_CFG_STALL_MASK;

					udc->write_fn(epcfgreg,
						(udc->base_address +
						udc->ep[XUSB_EP_NUMBER_ZERO].
						endpointoffset));
				} else {
					/* Unstall the endpoint.*/
					epcfgreg =
						udc->read_fn(udc->base_address +
					udc->ep[endpoint].endpointoffset);
					epcfgreg &=
						~(XUSB_EP_CFG_STALL_MASK |
						  XUSB_EP_CFG_DATA_TOGGLE_MASK);
					udc->write_fn(epcfgreg,
						(udc->base_address +
						udc->ep[endpoint].
						endpointoffset));
				}
			}
		}
		break;

	default:
		epcfgreg = udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset);
		epcfgreg |= XUSB_EP_CFG_STALL_MASK;
		udc->write_fn(epcfgreg, (udc->base_address +
			udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
		return;
	}

	/* Cause and valid status phase to be issued.*/
	setup_ctrl_wr_status_stage(udc);

	return;
}

/**
 * execute_command() - Processes the USB specification chapter 9 commands.
 * @udc:		Pointer to the usb device controller structure.
 *
 * returns: 0 for success and the same reuqest command if it is not handled.
 *
 **/
static int execute_command(struct xusb_udc *udc)
{

	if ((ch9_cmdbuf.setup.bRequestType & USB_TYPE_MASK) ==
	    USB_TYPE_STANDARD) {
		/* Process the chapter 9 command.*/
		switch (ch9_cmdbuf.setup.bRequest) {

		case USB_REQ_CLEAR_FEATURE:
			set_clear_feature(udc, 0);
			break;

		case USB_REQ_SET_FEATURE:
			set_clear_feature(udc, 1);
			break;

		case USB_REQ_SET_ADDRESS:
			setup_ctrl_wr_status_stage(udc);
			break;

		case USB_REQ_SET_CONFIGURATION:
			set_configuration(udc);
			return ch9_cmdbuf.setup.bRequest;

		default:
			/*
			 * Return the same request to application for
			 * handling.
			 */
			return ch9_cmdbuf.setup.bRequest;
		}

	} else
		if ((ch9_cmdbuf.setup.bRequestType & USB_TYPE_MASK) ==
					USB_TYPE_CLASS)
			return ch9_cmdbuf.setup.bRequest;
	return 0;
}

/**
 * process_setup_pkt() - Processes the setup packet.
 * @udc:		Pointer to the usb device controller structure.
 * @ctrl:		Pointer to the usb control endpoint structure.
 *
 * returns: 0 for success and request to be handled by application if
 *		is not handled by the driver.
 *
 **/
static int process_setup_pkt(struct xusb_udc *udc, struct usb_ctrlrequest *ctrl)
{
	u32 *ep0rambase;

	/* Load up the chapter 9 command buffer.*/
	ep0rambase = (u32 __force *) (udc->base_address +
				  XUSB_SETUP_PKT_ADDR_OFFSET);
	memcpy((void *)&ch9_cmdbuf.setup, (void *)ep0rambase, 8);

	ctrl->bRequestType = ch9_cmdbuf.setup.bRequestType;
	ctrl->bRequest     = ch9_cmdbuf.setup.bRequest;
	ctrl->wValue       = ch9_cmdbuf.setup.wValue;
	ctrl->wIndex       = ch9_cmdbuf.setup.wIndex;
	ctrl->wLength      = ch9_cmdbuf.setup.wLength;

	ch9_cmdbuf.setup.wValue = cpu_to_le16(ch9_cmdbuf.setup.wValue);
	ch9_cmdbuf.setup.wIndex = cpu_to_le16(ch9_cmdbuf.setup.wIndex);
	ch9_cmdbuf.setup.wLength = cpu_to_le16(ch9_cmdbuf.setup.wLength);

	/* Restore ReadPtr to data buffer.*/
	ch9_cmdbuf.contreadptr = &ch9_cmdbuf.contreaddatabuffer[0];

	if (ch9_cmdbuf.setup.bRequestType & USB_DIR_IN) {
		/* Execute the get command.*/
		ch9_cmdbuf.setupseqrx = STATUS_PHASE;
		ch9_cmdbuf.setupseqtx = DATA_PHASE;
		return execute_command(udc);
	} else {
		/* Execute the put command.*/
		ch9_cmdbuf.setupseqrx = DATA_PHASE;
		ch9_cmdbuf.setupseqtx = STATUS_PHASE;
		if (!ch9_cmdbuf.setup.wLength)
			return execute_command(udc);
	}
	/* Control should never come here.*/
	return 0;
}

/**
 * ep0_out_token() - Processes the endpoint 0 OUT token.
 * @udc:	Pointer to the usb device controller structure.
 *
 **/
static void ep0_out_token(struct xusb_udc *udc)
{
	u8 count;
	u8 *ep0rambase;
	u16 index;

	switch (ch9_cmdbuf.setupseqrx) {
	case STATUS_PHASE:
		/*
		 * This resets both state machines for the next
		 * Setup packet.
		 */
		ch9_cmdbuf.setupseqrx = SETUP_PHASE;
		ch9_cmdbuf.setupseqtx = SETUP_PHASE;
		break;

	case DATA_PHASE:
		count = udc->read_fn(udc->base_address +
				XUSB_EP_BUF0COUNT_OFFSET);
		/* Copy the data to be received from the DPRAM. */
		ep0rambase =
			(u8 __force *) (udc->base_address +
				(udc->ep[XUSB_EP_NUMBER_ZERO].rambase << 2));

		for (index = 0; index < count; index++)
			*ch9_cmdbuf.contreadptr++ = *ep0rambase++;

		ch9_cmdbuf.contreadcount += count;

		/* Set the Tx packet size and the Tx enable bit.*/
		udc->write_fn(count, (udc->base_address +
						 XUSB_EP_BUF0COUNT_OFFSET));
		udc->write_fn(1, (udc->base_address + XUSB_BUFFREADY_OFFSET));

		if (ch9_cmdbuf.setup.wLength == ch9_cmdbuf.contreadcount)
			execute_command(udc);
		break;

	default:
		break;
	}
}

/**
 * ep0_in_token() - Processes the endpoint 0 IN token.
 * @udc:	Pointer to the usb device controller structure.
 *
 **/
static void ep0_in_token(struct xusb_udc *udc)
{
	u32 epcfgreg;
	u16 count;
	u16 index;
	u8 *ep0rambase;

	switch (ch9_cmdbuf.setupseqtx) {
	case STATUS_PHASE:
		if (ch9_cmdbuf.setup.bRequest == USB_REQ_SET_ADDRESS) {
			/* Set the address of the device.*/
			udc->write_fn(ch9_cmdbuf.setup.wValue,
					(udc->base_address +
					XUSB_ADDRESS_OFFSET));
		} else
			if (ch9_cmdbuf.setup.bRequest == USB_REQ_SET_FEATURE) {
				if (ch9_cmdbuf.setup.bRequestType ==
					STANDARD_OUT_DEVICE) {
					if (ch9_cmdbuf.setup.wValue ==
						USB_DEVICE_TEST_MODE)
						udc->write_fn(TEST_J,
							(udc->base_address +
							XUSB_TESTMODE_OFFSET));
			}
		}
		break;

	case DATA_PHASE:
		if (!ch9_cmdbuf.contwritecount) {
			/*
			 * We're done with data transfer, next
			 * will be zero length OUT with data toggle of
			 * 1. Setup data_toggle.
			 */
			epcfgreg = udc->read_fn(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset);
			epcfgreg |= XUSB_EP_CFG_DATA_TOGGLE_MASK;
			udc->write_fn(epcfgreg,
				(udc->base_address +
				udc->ep[XUSB_EP_NUMBER_ZERO].endpointoffset));
			count = 0;

			ch9_cmdbuf.setupseqtx = STATUS_PHASE;

		} else {
			if (8 >= ch9_cmdbuf.contwritecount)
				count = ch9_cmdbuf.contwritecount;
			else
				count = 8;

			/* Copy the data to be transmitted into the DPRAM. */
			ep0rambase = (u8 __force *) (udc->base_address +
				(udc->ep[XUSB_EP_NUMBER_ZERO].rambase << 2));
			for (index = 0; index < count; index++)
				*ep0rambase++ = *ch9_cmdbuf.contwriteptr++;

			ch9_cmdbuf.contwritecount -= count;
		}
		udc->write_fn(count, (udc->base_address +
						XUSB_EP_BUF0COUNT_OFFSET));
		udc->write_fn(1, (udc->base_address + XUSB_BUFFREADY_OFFSET));
		break;

	default:
		break;
	}
}

/**
 * control_ep_intrhandler() - Endpoint 0 interrupt handler.
 * @callbackref:	Pointer to the call back reference passed by the
 *			main interrupt handler.
 * @intrstatus:	It's the mask value for the interrupt sources on endpoint 0.
 *
 *	Processes the commands received during enumeration phase.
 **/
static void control_ep_intrhandler(void *callbackref, u32 intrstatus)
{
	struct xusb_udc *udc;
	struct usb_ctrlrequest ctrl;
	int status;
	int epnum;
	u32 intrreg;

	udc = (struct xusb_udc *) callbackref;

	/* Process the end point zero buffer interrupt.*/
	if (intrstatus & XUSB_STATUS_EP0_BUFF1_COMP_MASK) {
		if (intrstatus & XUSB_STATUS_SETUP_PACKET_MASK) {
			/*
			 * Enable the Disconnect, suspend and reset
			 * interrupts.
			 */
			intrreg = udc->read_fn(udc->base_address +
					XUSB_IER_OFFSET);
			intrreg |= (XUSB_STATUS_DISCONNECT_MASK |
					 XUSB_STATUS_SUSPEND_MASK |
					 XUSB_STATUS_RESET_MASK);
			udc->write_fn(intrreg,
				(udc->base_address + XUSB_IER_OFFSET));
			status = process_setup_pkt(udc, &ctrl);
			if (status) {

				/*
				 * Request is to be handled by the gadget
				 * driver.
				 */
				spin_unlock(&udc->lock);
				udc->driver->setup(&udc->gadget, &ctrl);
				spin_lock(&udc->lock);
			} else {
				if (ctrl.bRequest == USB_REQ_CLEAR_FEATURE) {
					epnum = ctrl.wIndex & 0xf;
					udc->ep[epnum].stopped = 0;
				}
				if (ctrl.bRequest == USB_REQ_SET_FEATURE) {
					epnum = ctrl.wIndex & 0xf;
					udc->ep[epnum].stopped = 1;
				}
			}
		} else
			if (intrstatus & XUSB_STATUS_FIFO_BUFF_RDY_MASK)
				ep0_out_token(udc);
			else if (intrstatus &
				XUSB_STATUS_FIFO_BUFF_FREE_MASK)
				ep0_in_token(udc);
	}
}

/**
 * noncontrol_ep_intrhandler() - Non control endpoint interrupt handler.
 * @callbackref:	Pointer to the call back reference passed by the
 *			main interrupt handler.
 * @epnum:	End point number for which the interrupt is to be processed
 * @intrstatus:	It's the mask value for the interrupt sources on endpoint 0.
 *
 **/
static void noncontrol_ep_intrhandler(void *callbackref, u8 epnum,
					u32 intrstatus)
{

	struct xusb_request *req;
	struct xusb_udc *udc;
	struct xusb_ep *ep;

	udc = (struct xusb_udc *) callbackref;
	ep = &udc->ep[epnum];

	/* Process the End point interrupts.*/
	if (intrstatus & (XUSB_STATUS_EP0_BUFF1_COMP_MASK << epnum))
		ep->buffer0ready = 0;
	if (intrstatus & (XUSB_STATUS_EP0_BUFF2_COMP_MASK << epnum))
		ep->buffer1ready = 0;

	if (list_empty(&ep->queue))
		req = NULL;
	else
		req = list_entry(ep->queue.next, struct xusb_request, queue);
	if (!req)
		return;
	if (ep->is_in)
		(void)write_fifo(ep, req);
	else
		(void)read_fifo(ep, req);
}

/**
 * xusb_udc_irq() - The main interrupt handler.
 * @irq:	The interrupt number.
 * @_udc:	Pointer to the usb device controller structure.
 *
 * returns: IRQ_HANDLED after the interrupt is handled.
 *
 **/
static irqreturn_t xusb_udc_irq(int irq, void *_udc)
{
	struct xusb_udc *udc = _udc;
	u32 intrstatus;
	u8 index;
	u32 bufintr;

	spin_lock(&(udc->lock));

	/* Read the Interrupt Status Register.*/
	intrstatus = udc->read_fn(udc->base_address + XUSB_STATUS_OFFSET);
	/* Call the handler for the event interrupt.*/
	if (intrstatus & XUSB_STATUS_INTR_EVENT_MASK) {
		/*
		 * Check if there is any action to be done for :
		 * - USB Reset received {XUSB_STATUS_RESET_MASK}
		 * - USB Suspend received {XUSB_STATUS_SUSPEND_MASK}
		 * - USB Disconnect received {XUSB_STATUS_DISCONNECT_MASK}
		 */
		startup_intrhandler(udc, intrstatus);
	}

	/* Check the buffer completion interrupts */
	if (intrstatus & XUSB_STATUS_INTR_BUFF_COMP_ALL_MASK) {
		if (intrstatus & XUSB_STATUS_EP0_BUFF1_COMP_MASK)
			control_ep_intrhandler(udc, intrstatus);

		for (index = 1; index < 8; index++) {
			bufintr = ((intrstatus &
					(XUSB_STATUS_EP1_BUFF1_COMP_MASK <<
							(index - 1))) ||
				   (intrstatus &
					(XUSB_STATUS_EP1_BUFF2_COMP_MASK <<
							(index - 1))));

			if (bufintr)
				noncontrol_ep_intrhandler(udc, index,
						intrstatus);
		}
	}
	spin_unlock(&(udc->lock));

	return IRQ_HANDLED;
}

/**
 * xudc_start() - Starts the device.
 * @driver:	Pointer to the usb gadget sturcutre.
 * @bind:	Function pointer to bind driver
 * returns: 0 for success and error value on failure
 *
 **/
static int xudc_start(struct usb_gadget *gadget,
			struct usb_gadget_driver *driver)
{
	struct xusb_udc *udc = &controller;
	const struct usb_endpoint_descriptor *d = &config_bulk_out_desc;

	driver->driver.bus = NULL;
	/* hook up the driver */
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	udc->gadget.speed = driver->max_speed;

	/* Enable the USB device.*/
	xusb_ep_enable(&udc->ep[XUSB_EP_NUMBER_ZERO].ep, d);
	udc->write_fn(0, (udc->base_address + XUSB_ADDRESS_OFFSET));
	udc->write_fn(XUSB_CONTROL_USB_READY_MASK,
		(udc->base_address + XUSB_CONTROL_OFFSET));

	return 0;
}

/**
 * xudc_stop() - stops the device.
 * @driver:	Pointer to the usb gadget sturcutre.
 *
 * returns: 0 for success and error value on failure
 *
 */
static int xudc_stop(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct xusb_udc *udc = &controller;
	unsigned long flags;
	u32 crtlreg;

	/* Disable USB device.*/
	crtlreg = udc->read_fn(udc->base_address + XUSB_CONTROL_OFFSET);
	crtlreg &= ~XUSB_CONTROL_USB_READY_MASK;
	udc->write_fn(crtlreg, (udc->base_address + XUSB_CONTROL_OFFSET));
	spin_lock_irqsave(&udc->lock, flags);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	stop_activity(udc);
	spin_unlock_irqrestore(&udc->lock, flags);

	udc->gadget.dev.driver = NULL;
	udc->driver = NULL;

	return 0;
}

/**
 * xudc_remove() - Releases the resources allocated during the initialization.
 * @pdev:	Pointer to the platform device structure.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int xudc_remove(struct platform_device *pdev)
{

	struct xusb_udc *udc = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "remove\n");
	usb_del_gadget_udc(&udc->gadget);
	if (udc->driver)
		return -EBUSY;

	platform_set_drvdata(pdev, NULL);
	device_unregister(&udc->gadget.dev);

	return 0;
}

static void xusb_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xusb_udc *udc = platform_get_drvdata(pdev);

	kfree(udc);
}

/**
 * usb_of_probe() - The device probe function for driver initialization.
 * @pdev:		Pointer to the platform device structure.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int
usb_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct xusb_udc *udc = &controller;
	int err;
	int irq;
	int ret;

	dev_dbg(&pdev->dev, "%s(%p)\n", __func__, pdev);

	/* Map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	udc->base_address = devm_ioremap_nocache(&pdev->dev, res->start,
						 resource_size(res));
	if (!udc->base_address)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "unable to get irq\n");
		return irq;
	}
	err = devm_request_irq(&pdev->dev, irq, xusb_udc_irq,
			       0, dev_name(&pdev->dev), udc);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to request irq %d", irq);
		return err;
	}

	udc->dma_enabled = of_property_read_bool(np, "xlnx,include-dma");

	device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;

	spin_lock_init(&udc->lock);

	/* Check for IP endianness */
	udc->write_fn(TEST_J, (udc->base_address + XUSB_TESTMODE_OFFSET));
	if ((udc->read_fn(udc->base_address + XUSB_TESTMODE_OFFSET))
			!= TEST_J) {
		controller.write_fn = xusb_write32;
		controller.read_fn = xusb_read32;
	}
	udc->write_fn(0, (udc->base_address + XUSB_TESTMODE_OFFSET));

	xudc_reinit(udc);

	/* Set device address to 0.*/
	udc->write_fn(0, (udc->base_address + XUSB_ADDRESS_OFFSET));

	ret = device_add(&udc->gadget.dev);
	if (ret)
		dev_dbg(&pdev->dev, "device_add returned %d\n", ret);

	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (ret)
		dev_dbg(&pdev->dev, "usb_add_gadget_udc returned %d\n", ret);

	/* Enable the interrupts.*/
	udc->write_fn((XUSB_STATUS_GLOBAL_INTR_MASK |
		       XUSB_STATUS_RESET_MASK |
		       XUSB_STATUS_DISCONNECT_MASK |
		       XUSB_STATUS_SUSPEND_MASK |
		       XUSB_STATUS_FIFO_BUFF_RDY_MASK |
		       XUSB_STATUS_FIFO_BUFF_FREE_MASK |
		       XUSB_STATUS_EP0_BUFF1_COMP_MASK),
		      (udc->base_address + XUSB_IER_OFFSET));

	platform_set_drvdata(pdev, udc);

	dev_info(&pdev->dev, "%s version %s\n", driver_name, DRIVER_VERSION);
	dev_info(&pdev->dev, "%s #%d at 0x%08X mapped to 0x%08X\n",
		 driver_name, 0, (u32)res->start,
		 (u32 __force)udc->base_address);

	return 0;
}

/**
 * usb_of_remove() - The device driver remove function.
 * @pdev:		Pointer to the platform device structure.
 *
 * returns: 0 for success and error value on failure
 *
 **/
static int usb_of_remove(struct platform_device *pdev)
{
	return xudc_remove(pdev);
}

static struct platform_driver usb_of_driver = {
	.driver = {
		.name = driver_name,
		.owner = THIS_MODULE,
		.of_match_table = usb_of_match,
	},
	.probe = usb_of_probe,
	.remove = usb_of_remove,
};

module_platform_driver(usb_of_driver);

MODULE_DESCRIPTION("Xilinx udc driver");
MODULE_AUTHOR("Xilinx, Inc");
MODULE_LICENSE("GPL");
