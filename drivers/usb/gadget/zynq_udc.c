/*
 * Xilinx Zynq USB Device Controller Driver.
 *
 * Copyright (C) 2011 - 2014 Xilinx, Inc.
 *
 * This file is based on fsl_udc_core.c file with few minor modifications
 * to support Xilinx Zynq USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published bydrive
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#undef VERBOSE

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/usb/zynq_usb.h>
#include <linux/dmapool.h>
#include <linux/delay.h>

#include <linux/io.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <asm/dma.h>

#include <linux/usb/zynq_otg.h>

#define	DRIVER_DESC	"Xilinx Zynq USB Device Controller driver"
#define	DRIVER_AUTHOR	"Xilinx, Inc."
#define	DRIVER_VERSION	"Apr 01, 2011"

/* USB registers */
#define USB_MAX_CTRL_PAYLOAD		64

 /* USB DR device mode registers (Little Endian) */
struct usb_dr_device {
	/* Capability register */
	u8 res1[256];
	u16 caplength;		/* Capability Register Length */
	u16 hciversion;		/* Host Controller Interface Version */
	u32 hcsparams;		/* Host Controller Structual Parameters */
	u32 hccparams;		/* Host Controller Capability Parameters */
	u8 res2[20];
	u32 dciversion;		/* Device Controller Interface Version */
	u32 dccparams;		/* Device Controller Capability Parameters */
	u8 res3[24];
	/* Operation register */
	u32 usbcmd;		/* USB Command Register */
	u32 usbsts;		/* USB Status Register */
	u32 usbintr;		/* USB Interrupt Enable Register */
	u32 frindex;		/* Frame Index Register */
	u8 res4[4];
	u32 deviceaddr;		/* Device Address */
	u32 endpointlistaddr;	/* Endpoint List Address Register */
	u8 res5[4];
	u32 burstsize;		/* Master Interface Data Burst Size Register */
	u32 txttfilltuning;	/* Transmit FIFO Tuning Controls Register */
	u8 res6[24];
	u32 configflag;		/* Configure Flag Register */
	u32 portsc1;		/* Port 1 Status and Control Register */
	u8 res7[28];
	u32 otgsc;		/* On-The-Go Status and Control */
	u32 usbmode;		/* USB Mode Register */
	u32 endptsetupstat;	/* Endpoint Setup Status Register */
	u32 endpointprime;	/* Endpoint Initialization Register */
	u32 endptflush;		/* Endpoint Flush Register */
	u32 endptstatus;	/* Endpoint Status Register */
	u32 endptcomplete;	/* Endpoint Complete Register */
	u32 endptctrl[6];	/* Endpoint Control Registers */
};

/* ep0 transfer state */
#define WAIT_FOR_SETUP          0
#define DATA_STATE_XMIT         1
#define WAIT_FOR_OUT_STATUS     3
#define DATA_STATE_RECV         4

/* Device Controller Capability Parameter register */
#define DCCPARAMS_DC				0x00000080
#define DCCPARAMS_DEN_MASK			0x0000001f

/* Frame Index Register Bit Masks */
#define	USB_FRINDEX_MASKS			0x3fff
/* USB CMD  Register Bit Masks */
#define  USB_CMD_RUN_STOP                     0x00000001
#define  USB_CMD_CTRL_RESET                   0x00000002
#define  USB_CMD_ASYNC_SCHEDULE_EN            0x00000020
#define  USB_CMD_SUTW                         0x00002000
#define  USB_CMD_ATDTW                        0x00004000

/* USB STS Register Bit Masks */
#define  USB_STS_INT                          0x00000001
#define  USB_STS_ERR                          0x00000002
#define  USB_STS_PORT_CHANGE                  0x00000004
#define  USB_STS_SYS_ERR                      0x00000010
#define  USB_STS_RESET                        0x00000040
#define  USB_STS_SOF                          0x00000080
#define  USB_STS_SUSPEND                      0x00000100

/* USB INTR Register Bit Masks */
#define  USB_INTR_INT_EN                      0x00000001
#define  USB_INTR_ERR_INT_EN                  0x00000002
#define  USB_INTR_PTC_DETECT_EN               0x00000004
#define  USB_INTR_SYS_ERR_EN                  0x00000010
#define  USB_INTR_RESET_EN                    0x00000040
#define  USB_INTR_SOF_EN                      0x00000080
#define  USB_INTR_DEVICE_SUSPEND              0x00000100

/* Device Address bit masks */
#define  USB_DEVICE_ADDRESS_MASK              0xFE000000
#define  USB_DEVICE_ADDRESS_BIT_POS           25

/* endpoint list address bit masks */
#define USB_EP_LIST_ADDRESS_MASK              0xfffff800

/* PORTSCX  Register Bit Masks */
#define  PORTSCX_CURRENT_CONNECT_STATUS       0x00000001
#define  PORTSCX_PORT_ENABLE                  0x00000004
#define  PORTSCX_PORT_EN_DIS_CHANGE           0x00000008
#define  PORTSCX_OVER_CURRENT_CHG             0x00000020
#define  PORTSCX_PORT_FORCE_RESUME            0x00000040
#define  PORTSCX_PORT_SUSPEND                 0x00000080
#define  PORTSCX_PORT_RESET                   0x00000100
#define  PORTSCX_PHY_LOW_POWER_SPD            0x00800000
#define  PORTSCX_PORT_SPEED_MASK              0x0C000000
#define  PORTSCX_PORT_WIDTH                   0x10000000
#define  PORTSCX_PHY_TYPE_SEL                 0xC0000000

/* bit 27-26 are port speed */
#define  PORTSCX_PORT_SPEED_FULL              0x00000000
#define  PORTSCX_PORT_SPEED_LOW               0x04000000
#define  PORTSCX_PORT_SPEED_HIGH              0x08000000
#define  PORTSCX_PORT_SPEED_UNDEF             0x0C000000

/* bit 28 is parallel transceiver width for UTMI interface */
#define  PORTSCX_PTW_16BIT                    0x10000000

/* bit 31-30 are port transceiver select */
#define  PORTSCX_PTS_UTMI                     0x00000000
#define  PORTSCX_PTS_ULPI                     0x80000000
#define  PORTSCX_PTS_FSLS                     0xC0000000

/* otgsc Register Bit Masks */
#define  OTGSC_CTRL_OTG_TERM                  0x00000008

/* USB MODE Register Bit Masks */
#define  USB_MODE_CTRL_MODE_IDLE              0x00000000
#define  USB_MODE_CTRL_MODE_DEVICE            0x00000002
#define  USB_MODE_CTRL_MODE_HOST              0x00000003
#define  USB_MODE_SETUP_LOCK_OFF              0x00000008

/* Endpoint Setup Status bit masks */
#define  EP_SETUP_STATUS_MASK                 0x0000003F
#define  EP_SETUP_STATUS_EP0		      0x00000001

/* ENDPOINTCTRLx  Register Bit Masks */
#define  EPCTRL_TX_ENABLE                     0x00800000
#define  EPCTRL_TX_DATA_TOGGLE_RST            0x00400000	/* Not EP0 */
#define  EPCTRL_TX_EP_STALL                   0x00010000
#define  EPCTRL_RX_ENABLE                     0x00000080
#define  EPCTRL_RX_DATA_TOGGLE_RST            0x00000040	/* Not EP0 */
#define  EPCTRL_RX_EP_STALL                   0x00000001

/* bit 19-18 and 3-2 are endpoint type */
#define  EPCTRL_TX_EP_TYPE_SHIFT              18
#define  EPCTRL_RX_EP_TYPE_SHIFT              2

/* Endpoint Queue Head data struct
 * Rem: all the variables of qh are LittleEndian Mode
 * and NEXT_POINTER_MASK should operate on a LittleEndian, Phy Addr
 */
struct ep_queue_head {
	u32 max_pkt_length;	/* Mult(31-30) , Zlt(29) , Max Pkt len
				   and IOS(15) */
	u32 curr_dtd_ptr;	/* Current dTD Pointer(31-5) */
	u32 next_dtd_ptr;	/* Next dTD Pointer(31-5), T(0) */
	u32 size_ioc_int_sts;	/* Total bytes (30-16), IOC (15),
				   MultO(11-10), STS (7-0)  */
	u32 buff_ptr0;		/* Buffer pointer Page 0 (31-12) */
	u32 buff_ptr1;		/* Buffer pointer Page 1 (31-12) */
	u32 buff_ptr2;		/* Buffer pointer Page 2 (31-12) */
	u32 buff_ptr3;		/* Buffer pointer Page 3 (31-12) */
	u32 buff_ptr4;		/* Buffer pointer Page 4 (31-12) */
	u32 res1;
	u8 setup_buffer[8];	/* Setup data 8 bytes */
	u32 res2[4];
};

/* Endpoint Queue Head Bit Masks */
#define  EP_QUEUE_HEAD_MULT_POS               30
#define  EP_QUEUE_HEAD_ZLT_SEL                0x20000000
#define  EP_QUEUE_HEAD_MAX_PKT_LEN_POS        16
#define  EP_QUEUE_HEAD_IOS                    0x00008000
#define  EP_QUEUE_HEAD_STATUS_HALT	      0x00000040
#define  EP_QUEUE_HEAD_STATUS_ACTIVE          0x00000080
#define  EP_QUEUE_HEAD_NEXT_POINTER_MASK      0xFFFFFFE0
#define  EP_MAX_LENGTH_TRANSFER               0x4000

/* Endpoint Transfer Descriptor data struct */
/* Rem: all the variables of td are LittleEndian Mode */
struct ep_td_struct {
	u32 next_td_ptr;	/* Next TD pointer(31-5), T(0) set
				   indicate invalid */
	u32 size_ioc_sts;	/* Total bytes (30-16), IOC (15),
				   MultO(11-10), STS (7-0)  */
	u32 buff_ptr0;		/* Buffer pointer Page 0 */
	u32 buff_ptr1;		/* Buffer pointer Page 1 */
	u32 buff_ptr2;		/* Buffer pointer Page 2 */
	u32 buff_ptr3;		/* Buffer pointer Page 3 */
	u32 buff_ptr4;		/* Buffer pointer Page 4 */
	u32 res;
	/* 32 bytes */
	dma_addr_t td_dma;	/* dma address for this td */
	/* virtual address of next td specified in next_td_ptr */
	struct ep_td_struct *next_td_virt;
};

/* Endpoint Transfer Descriptor bit Masks */
#define  DTD_NEXT_TERMINATE                   0x00000001
#define  DTD_IOC                              0x00008000
#define  DTD_STATUS_ACTIVE                    0x00000080
#define  DTD_STATUS_HALTED                    0x00000040
#define  DTD_STATUS_DATA_BUFF_ERR             0x00000020
#define  DTD_STATUS_TRANSACTION_ERR           0x00000008
#define  DTD_RESERVED_FIELDS                  0x80007300
#define  DTD_ADDR_MASK                        0xFFFFFFE0
#define  DTD_PACKET_SIZE                      0x7FFF0000
#define  DTD_LENGTH_BIT_POS                   16
#define  DTD_ERROR_MASK                       (DTD_STATUS_HALTED | \
					DTD_STATUS_DATA_BUFF_ERR | \
					DTD_STATUS_TRANSACTION_ERR)
/* Alignment requirements; must be a power of two */
#define DTD_ALIGNMENT				0x20
#define QH_ALIGNMENT				2048

/* Controller dma boundary */
#define UDC_DMA_BOUNDARY			0x1000

/*-------------------------------------------------------------------------*/

/* ### driver private data
 */
struct zynq_req {
	struct usb_request req;
	struct list_head queue;
	/* ep_queue() func will add
	   a request->queue into a udc_ep->queue 'd tail */
	struct zynq_ep *ep;
	unsigned mapped:1;

	struct ep_td_struct *head, *tail;	/* For dTD List
						   cpu endian Virtual addr */
	unsigned int dtd_count;
};

#define REQ_UNCOMPLETE			1

struct zynq_ep {
	struct usb_ep ep;
	struct list_head queue;
	struct zynq_udc *udc;
	struct ep_queue_head *qh;
	struct usb_gadget *gadget;

	char name[14];
	unsigned stopped:1;
	unsigned int wedge;
};

#define EP_DIR_IN	1
#define EP_DIR_OUT	0

struct zynq_udc {
	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct completion *done;	/* to make sure release() is done */
	struct zynq_ep *eps;
	unsigned int max_ep;
	unsigned int irq;

	/* zynq otg transceiver */
	struct zynq_otg	*xotg;

	struct usb_ctrlrequest local_setup_buff;
	spinlock_t lock;
	struct usb_phy *transceiver;
	unsigned softconnect:1;
	unsigned vbus_active:1;
	unsigned stopped:1;
	unsigned remote_wakeup:1;

	struct ep_queue_head *ep_qh;	/* Endpoints Queue-Head */
	struct zynq_req *status_req;	/* ep0 status request */
	struct dma_pool *td_pool;	/* dma pool for DTD */
	enum zynq_usb2_phy_modes phy_mode;

	size_t ep_qh_size;		/* size after alignment adjustment*/
	dma_addr_t ep_qh_dma;		/* dma address of QH */

	u32 max_pipes;          /* Device max pipes */
	u32 resume_state;	/* USB state to resume */
	u32 usb_state;		/* USB current state */
	u32 ep0_state;		/* Endpoint zero state */
	u32 ep0_dir;		/* Endpoint zero direction: can be
				   USB_DIR_IN or USB_DIR_OUT */
	u8 device_address;	/* Device USB address */
};

/*-------------------------------------------------------------------------*/

#ifdef DEBUG
#define DBG(fmt, args...)	pr_debug("[%s]  " fmt "\n", \
				__func__, ## args)
#else
#define DBG(fmt, args...)	do {} while (0)
#endif

#ifdef VERBOSE
#define VDBG		DBG
#else
#define VDBG(stuff...)	do {} while (0)
#endif

#define ERR(stuff...)		pr_err("udc: " stuff)
#define WARNING(stuff...)		pr_warn("udc: " stuff)
#define INFO(stuff...)		pr_info("udc: " stuff)

/*-------------------------------------------------------------------------*/

/* ### Add board specific defines here */

/* pipe direction macro from device view */
#define USB_RECV	0	/* OUT EP */
#define USB_SEND	1	/* IN EP */

/* internal used help routines. */
#define ep_index(EP)		((EP)->ep.desc->bEndpointAddress&0xF)
#define ep_maxpacket(EP)	((EP)->ep.maxpacket)
#define ep_is_in(EP)	((ep_index(EP) == 0) ? (EP->udc->ep0_dir == \
			USB_DIR_IN) : ((EP)->ep.desc->bEndpointAddress \
			& USB_DIR_IN) == USB_DIR_IN)
#define get_ep_by_pipe(udc, pipe)	((pipe == 1) ? &udc->eps[0] : \
					&udc->eps[pipe])
#define get_pipe_by_windex(windex)	((windex & USB_ENDPOINT_NUMBER_MASK) \
					* 2 + ((windex & USB_DIR_IN) ? 1 : 0))

static int zynq_udc_clk_init(struct platform_device *pdev)
{
	struct zynq_usb2_platform_data *pdata = pdev->dev.platform_data;
	int rc;

	rc = clk_prepare_enable(pdata->clk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto err_out_clk_put;
	}

	return 0;

err_out_clk_put:
	clk_put(pdata->clk);

	return rc;
}

static void zynq_udc_clk_release(struct platform_device *pdev)
{
	struct zynq_usb2_platform_data *pdata = pdev->dev.platform_data;

	clk_disable_unprepare(pdata->clk);
}

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

static const char driver_name[] = "zynq-udc";
static const char driver_desc[] = DRIVER_DESC;

static struct usb_dr_device __iomem *dr_regs;

/* it is initialized in probe()  */
static struct zynq_udc *udc_controller;

static const struct usb_endpoint_descriptor
zynq_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	0,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize =	USB_MAX_CTRL_PAYLOAD,
};

static void zynq_ep_fifo_flush(struct usb_ep *_ep);

static inline u32 zynq_readl(const unsigned __iomem *addr)
{
	return readl(addr);
}

static inline void zynq_writel(u32 val32, unsigned __iomem *addr)
{
	writel(val32, addr);
}

/********************************************************************
 *	Internal Used Function
********************************************************************/
/*-----------------------------------------------------------------
 * done() - retire a request; caller blocked irqs
 * @status : request status to be set, only works when
 *	request is still in progress.
 *--------------------------------------------------------------*/
static void done(struct zynq_ep *ep, struct zynq_req *req, int status)
{
	struct zynq_udc *udc = NULL;
	unsigned char stopped = ep->stopped;
	struct ep_td_struct *curr_td, *next_td;
	int j;

	udc = (struct zynq_udc *)ep->udc;
	/* Removed the req from zynq_ep->queue */
	list_del_init(&req->queue);

	/* req.status should be set as -EINPROGRESS in ep_queue() */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	/* Free dtd for the request */
	next_td = req->head;
	for (j = 0; j < req->dtd_count; j++) {
		curr_td = next_td;
		if (j != req->dtd_count - 1)
			next_td = curr_td->next_td_virt;
		dma_pool_free(udc->td_pool, curr_td, curr_td->td_dma);
	}

	if (req->mapped) {
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep_is_in(ep));
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	} else
		dma_sync_single_for_cpu(ep->udc->gadget.dev.parent,
			req->req.dma, req->req.length,
			ep_is_in(ep)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);

	if (status && (status != -ESHUTDOWN))
		VDBG("complete %s req %p stat %d len %u/%u",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	ep->stopped = 1;

	spin_unlock(&ep->udc->lock);
	/* complete() is from gadget layer,
	 * eg fsg->bulk_in_complete() */
	if (req->req.complete)
		req->req.complete(&ep->ep, &req->req);

	spin_lock(&ep->udc->lock);
	ep->stopped = stopped;
}

/*-----------------------------------------------------------------
 * nuke(): delete all requests related to this ep
 * called with spinlock held
 *--------------------------------------------------------------*/
static void nuke(struct zynq_ep *ep, int status)
{
	ep->stopped = 1;

	/* Flush fifo */
	zynq_ep_fifo_flush(&ep->ep);

	/* Whether this eq has request linked */
	while (!list_empty(&ep->queue)) {
		struct zynq_req *req = NULL;

		req = list_entry(ep->queue.next, struct zynq_req, queue);
		done(ep, req, status);
	}
}

/*------------------------------------------------------------------
	Internal Hardware related function
 ------------------------------------------------------------------*/

static int dr_controller_setup(struct zynq_udc *udc)
{
	unsigned int tmp, portctrl;
	unsigned long timeout;
#define ZYNQ_UDC_RESET_TIMEOUT 1000

	/* Config PHY interface */
	portctrl = zynq_readl(&dr_regs->portsc1);
	portctrl &= ~(PORTSCX_PHY_TYPE_SEL | PORTSCX_PORT_WIDTH);
	switch (udc->phy_mode) {
	case ZYNQ_USB2_PHY_ULPI:
		portctrl |= PORTSCX_PTS_ULPI;
		break;
	case ZYNQ_USB2_PHY_UTMI_WIDE:
		portctrl |= PORTSCX_PTW_16BIT;
		/* fall through */
	case ZYNQ_USB2_PHY_UTMI:
		portctrl |= PORTSCX_PTS_UTMI;
		break;
	case ZYNQ_USB2_PHY_SERIAL:
		portctrl |= PORTSCX_PTS_FSLS;
		break;
	default:
		return -EINVAL;
	}
	zynq_writel(portctrl, &dr_regs->portsc1);

	/* Stop and reset the usb controller */
	tmp = zynq_readl(&dr_regs->usbcmd);
	tmp &= ~USB_CMD_RUN_STOP;
	zynq_writel(tmp, &dr_regs->usbcmd);

	tmp = zynq_readl(&dr_regs->usbcmd);
	tmp |= USB_CMD_CTRL_RESET;
	zynq_writel(tmp, &dr_regs->usbcmd);

	/* Wait for reset to complete */
	timeout = jiffies + ZYNQ_UDC_RESET_TIMEOUT;
	while (zynq_readl(&dr_regs->usbcmd) & USB_CMD_CTRL_RESET) {
		if (time_after(jiffies, timeout)) {
			ERR("udc reset timeout!\n");
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	/* Set the controller as device mode */
	tmp = zynq_readl(&dr_regs->usbmode);
	tmp |= USB_MODE_CTRL_MODE_DEVICE;
	/* Disable Setup Lockout */
	tmp |= USB_MODE_SETUP_LOCK_OFF;
	zynq_writel(tmp, &dr_regs->usbmode);

	/* Set OTG Terminate bit */
	tmp = zynq_readl(&dr_regs->otgsc);
	tmp |= OTGSC_CTRL_OTG_TERM;
	zynq_writel(tmp, &dr_regs->otgsc);

	/* Clear the setup status */
	zynq_writel(0, &dr_regs->usbsts);

	tmp = udc->ep_qh_dma;
	tmp &= USB_EP_LIST_ADDRESS_MASK;
	zynq_writel(tmp, &dr_regs->endpointlistaddr);

	VDBG("vir[qh_base] is %p phy[qh_base] is 0x%8x reg is 0x%8x",
		udc->ep_qh, (int)tmp,
		zynq_readl(&dr_regs->endpointlistaddr));

	return 0;
}

/* Enable DR irq and set controller to run state */
static void dr_controller_run(struct zynq_udc *udc)
{
	u32 temp;

#ifdef CONFIG_USB_ZYNQ_PHY
	if (gadget_is_otg(&udc->gadget)) {
		/* Enable DR irq reg except suspend interrupt */
		temp = USB_INTR_INT_EN | USB_INTR_ERR_INT_EN
			| USB_INTR_PTC_DETECT_EN | USB_INTR_RESET_EN
			| USB_INTR_SYS_ERR_EN;
	} else {
		/* Enable DR irq reg */
		temp = USB_INTR_INT_EN | USB_INTR_ERR_INT_EN
			| USB_INTR_PTC_DETECT_EN | USB_INTR_RESET_EN
			| USB_INTR_DEVICE_SUSPEND | USB_INTR_SYS_ERR_EN;
	}
#else
	/* Enable DR irq reg */
	temp = USB_INTR_INT_EN | USB_INTR_ERR_INT_EN
		| USB_INTR_PTC_DETECT_EN | USB_INTR_RESET_EN
		| USB_INTR_DEVICE_SUSPEND | USB_INTR_SYS_ERR_EN;
#endif

	zynq_writel(temp, &dr_regs->usbintr);

	/*
	 * Enable disconnect notification using B session end interrupt.
	 * This is a SW workaround for USB disconnect detection as mentioned
	 * in AR# 47538
	 */
	if (!gadget_is_otg(&udc->gadget)) {
		temp = zynq_readl(&dr_regs->otgsc);
		temp |= OTGSC_BSEIE;
		zynq_writel(temp, &dr_regs->otgsc);
	}

	/* Clear stopped bit */
	udc->stopped = 0;

	/* Set the controller as device mode */
	temp = zynq_readl(&dr_regs->usbmode);
	temp |= USB_MODE_CTRL_MODE_DEVICE;
	temp |= USB_MODE_SETUP_LOCK_OFF;
	zynq_writel(temp, &dr_regs->usbmode);

	/* Set OTG Terminate bit */
	temp = zynq_readl(&dr_regs->otgsc);
	temp |= OTGSC_CTRL_OTG_TERM;
	zynq_writel(temp, &dr_regs->otgsc);

	/* Set controller to Run */
	temp = zynq_readl(&dr_regs->usbcmd);
	temp |= USB_CMD_RUN_STOP;
	zynq_writel(temp, &dr_regs->usbcmd);
}

static void dr_controller_stop(struct zynq_udc *udc)
{
	unsigned int tmp;

	/* disable all INTR */
	zynq_writel(0, &dr_regs->usbintr);

	/* Set stopped bit for isr */
	udc->stopped = 1;

	/* disable IO output */
/*	usb_sys_regs->control = 0; */

	/* set controller to Stop */
	tmp = zynq_readl(&dr_regs->usbcmd);
	tmp &= ~USB_CMD_RUN_STOP;
	zynq_writel(tmp, &dr_regs->usbcmd);
}

static void dr_ep_setup(unsigned char ep_num, unsigned char dir,
			unsigned char ep_type)
{
	unsigned int tmp_epctrl = 0;

	tmp_epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);
	if (dir) {
		if (ep_num)
			tmp_epctrl |= EPCTRL_TX_DATA_TOGGLE_RST;
		tmp_epctrl |= EPCTRL_TX_ENABLE;
		tmp_epctrl |= ((unsigned int)(ep_type)
				<< EPCTRL_TX_EP_TYPE_SHIFT);
	} else {
		if (ep_num)
			tmp_epctrl |= EPCTRL_RX_DATA_TOGGLE_RST;
		tmp_epctrl |= EPCTRL_RX_ENABLE;
		tmp_epctrl |= ((unsigned int)(ep_type)
				<< EPCTRL_RX_EP_TYPE_SHIFT);
	}

	zynq_writel(tmp_epctrl, &dr_regs->endptctrl[ep_num]);
}

static void
dr_ep_change_stall(unsigned char ep_num, unsigned char dir, int value)
{
	u32 tmp_epctrl = 0;

	tmp_epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);

	if (value) {
		/* set the stall bit */
		if (dir)
			tmp_epctrl |= EPCTRL_TX_EP_STALL;
		else
			tmp_epctrl |= EPCTRL_RX_EP_STALL;
	} else {
		/* clear the stall bit and reset data toggle */
		if (dir) {
			tmp_epctrl &= ~EPCTRL_TX_EP_STALL;
			tmp_epctrl |= EPCTRL_TX_DATA_TOGGLE_RST;
		} else {
			tmp_epctrl &= ~EPCTRL_RX_EP_STALL;
			tmp_epctrl |= EPCTRL_RX_DATA_TOGGLE_RST;
		}
	}
	zynq_writel(tmp_epctrl, &dr_regs->endptctrl[ep_num]);
}

/* Get stall status of a specific ep
   Return: 0: not stalled; 1:stalled */
static int dr_ep_get_stall(unsigned char ep_num, unsigned char dir)
{
	u32 epctrl;

	epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);
	if (dir)
		return (epctrl & EPCTRL_TX_EP_STALL) ? 1 : 0;
	else
		return (epctrl & EPCTRL_RX_EP_STALL) ? 1 : 0;
}

/********************************************************************
	Internal Structure Build up functions
********************************************************************/

/*------------------------------------------------------------------
* struct_ep_qh_setup(): set the Endpoint Capabilites field of QH
 * @zlt: Zero Length Termination Select (1: disable; 0: enable)
 * @mult: Mult field
 ------------------------------------------------------------------*/
static void struct_ep_qh_setup(struct zynq_udc *udc, unsigned char ep_num,
		unsigned char dir, unsigned char ep_type,
		unsigned int max_pkt_len,
		unsigned int zlt, unsigned char mult)
{
	struct ep_queue_head *p_QH = &udc->ep_qh[2 * ep_num + dir];
	unsigned int tmp = 0;

	/* set the Endpoint Capabilites in QH */
	switch (ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
		/* Interrupt On Setup (IOS). for control ep  */
		tmp = (max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS)
			| EP_QUEUE_HEAD_IOS;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		tmp = (max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS)
			| (mult << EP_QUEUE_HEAD_MULT_POS);
		break;
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		tmp = max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS;
		break;
	default:
		VDBG("error ep type is %d", ep_type);
		return;
	}
	if (zlt)
		tmp |= EP_QUEUE_HEAD_ZLT_SEL;

	p_QH->max_pkt_length = cpu_to_le32(tmp);
	p_QH->next_dtd_ptr = 1;
	p_QH->size_ioc_int_sts = 0;
}

/* Setup qh structure and ep register for ep0. */
static void ep0_setup(struct zynq_udc *udc)
{
	/* the intialization of an ep includes: fields in QH, Regs,
	 * zynq_ep struct */
	struct_ep_qh_setup(udc, 0, USB_RECV, USB_ENDPOINT_XFER_CONTROL,
			USB_MAX_CTRL_PAYLOAD, 1, 0);
	struct_ep_qh_setup(udc, 0, USB_SEND, USB_ENDPOINT_XFER_CONTROL,
			USB_MAX_CTRL_PAYLOAD, 1, 0);
	dr_ep_setup(0, USB_RECV, USB_ENDPOINT_XFER_CONTROL);
	dr_ep_setup(0, USB_SEND, USB_ENDPOINT_XFER_CONTROL);

	return;

}

/***********************************************************************
		Endpoint Management Functions
***********************************************************************/

/*-------------------------------------------------------------------------
 * when configurations are set, or when interface settings change
 * for example the do_set_interface() in gadget layer,
 * the driver will enable or disable the relevant endpoints
 * ep0 doesn't use this routine. It is always enabled.
-------------------------------------------------------------------------*/
static int zynq_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct zynq_udc *udc = NULL;
	struct zynq_ep *ep = NULL;
	unsigned short max = 0;
	unsigned char mult = 0, zlt;
	int retval = -EINVAL;
	unsigned long flags = 0;

	ep = container_of(_ep, struct zynq_ep, ep);

	/* catch various bogus parameters */
	if (!_ep || !desc
			|| (desc->bDescriptorType != USB_DT_ENDPOINT))
		return -EINVAL;

	udc = ep->udc;

	if (!udc->driver || (udc->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	max = usb_endpoint_maxp(desc);

	/* Disable automatic zlp generation.  Driver is reponsible to indicate
	 * explicitly through req->req.zero.  This is needed to enable multi-td
	 * request. */
	zlt = 1;

	/* Assume the max packet size from gadget is always correct */
	switch (desc->bmAttributes & 0x03) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		/* mult = 0.  Execute N Transactions as demonstrated by
		 * the USB variable length packet protocol where N is
		 * computed using the Maximum Packet Length (dQH) and
		 * the Total Bytes field (dTD) */
		mult = 0;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		/* Calculate transactions needed for high bandwidth iso */
		mult = (unsigned char)(1 + ((max >> 11) & 0x03));
		max = max & 0x7ff;	/* bit 0~10 */
		/* 3 transactions at most */
		if (mult > 3)
			goto en_done;
		break;
	default:
		goto en_done;
	}

	spin_lock_irqsave(&udc->lock, flags);
	ep->ep.maxpacket = max;
	ep->ep.desc = desc;
	ep->stopped = 0;
	ep->wedge = 0;

	/* Controller related setup */
	/* Init EPx Queue Head (Ep Capabilites field in QH
	 * according to max, zlt, mult) */
	struct_ep_qh_setup(udc, (unsigned char) ep_index(ep),
			(unsigned char) ((desc->bEndpointAddress & USB_DIR_IN)
					?  USB_SEND : USB_RECV),
			(unsigned char) (desc->bmAttributes
					& USB_ENDPOINT_XFERTYPE_MASK),
			max, zlt, mult);

	/* Init endpoint ctrl register */
	dr_ep_setup((unsigned char) ep_index(ep),
			(unsigned char) ((desc->bEndpointAddress & USB_DIR_IN)
					? USB_SEND : USB_RECV),
			(unsigned char) (desc->bmAttributes
					& USB_ENDPOINT_XFERTYPE_MASK));

	spin_unlock_irqrestore(&udc->lock, flags);
	retval = 0;

	VDBG("enabled %s (ep%d%s) maxpacket %d", ep->ep.name,
			ep->ep.desc->bEndpointAddress & 0x0f,
			(desc->bEndpointAddress & USB_DIR_IN)
				? "in" : "out", max);
en_done:
	return retval;
}

/*---------------------------------------------------------------------
 * @ep : the ep being unconfigured. May not be ep0
 * Any pending and uncomplete req will complete with status (-ESHUTDOWN)
*---------------------------------------------------------------------*/
static int zynq_ep_disable(struct usb_ep *_ep)
{
	struct zynq_udc *udc = NULL;
	struct zynq_ep *ep = NULL;
	unsigned long flags = 0;
	u32 epctrl;
	int ep_num;

	ep = container_of(_ep, struct zynq_ep, ep);
	if (!_ep || !ep->ep.desc) {
		VDBG("%s not enabled", _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	/* disable ep on controller */
	ep_num = ep_index(ep);
	epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);
	if (ep_is_in(ep))
		epctrl &= ~EPCTRL_TX_ENABLE;
	else
		epctrl &= ~EPCTRL_RX_ENABLE;
	zynq_writel(epctrl, &dr_regs->endptctrl[ep_num]);

	udc = (struct zynq_udc *)ep->udc;
	spin_lock_irqsave(&udc->lock, flags);

	/* nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);

	ep->ep.desc = NULL;
	ep->stopped = 1;
	spin_unlock_irqrestore(&udc->lock, flags);

	VDBG("disabled %s OK", _ep->name);
	return 0;
}

/*---------------------------------------------------------------------
 * allocate a request object used by this endpoint
 * the main operation is to insert the req->queue to the eq->queue
 * Returns the request, or null if one could not be allocated
*---------------------------------------------------------------------*/
static struct usb_request *
zynq_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct zynq_req *req = NULL;

	req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void zynq_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct zynq_req *req = NULL;

	req = container_of(_req, struct zynq_req, req);

	if (_req)
		kfree(req);
}

/*-------------------------------------------------------------------------*/
static void zynq_queue_td(struct zynq_ep *ep, struct zynq_req *req)
{
	int i = ep_index(ep) * 2 + ep_is_in(ep);
	u32 temp, bitmask, tmp_stat;
	struct ep_queue_head *dQH = &ep->udc->ep_qh[i];

	/* VDBG("QH addr Register 0x%8x", dr_regs->endpointlistaddr);
	VDBG("ep_qh[%d] addr is 0x%8x", i, (u32)&(ep->udc->ep_qh[i])); */

	bitmask = ep_is_in(ep)
		? (1 << (ep_index(ep) + 16))
		: (1 << (ep_index(ep)));

	/* check if the pipe is empty */
	if (!(list_empty(&ep->queue))) {
		/* Add td to the end */
		struct zynq_req *lastreq;
		lastreq = list_entry(ep->queue.prev, struct zynq_req, queue);
		lastreq->tail->next_td_ptr =
			cpu_to_le32(req->head->td_dma & DTD_ADDR_MASK);
		wmb();
		/* Read prime bit, if 1 goto done */
		if (zynq_readl(&dr_regs->endpointprime) & bitmask)
			goto out;

		do {
			/* Set ATDTW bit in USBCMD */
			temp = zynq_readl(&dr_regs->usbcmd);
			zynq_writel(temp | USB_CMD_ATDTW, &dr_regs->usbcmd);

			/* Read correct status bit */
			tmp_stat = zynq_readl(&dr_regs->endptstatus) &
				bitmask;

#ifdef CONFIG_USB_ZYNQ_ERRATA_DT654401
			/* Workaround for USB errata DT# 654401 */
			temp = zynq_readl(&dr_regs->usbcmd);
			if (temp & USB_CMD_ATDTW) {
				udelay(5);
				if (zynq_readl(&dr_regs->usbcmd) &
				    USB_CMD_ATDTW)
					break;
			}
		} while (1);
#else
		} while (!(zynq_readl(&dr_regs->usbcmd) & USB_CMD_ATDTW));
#endif

		/* Write ATDTW bit to 0 */
		temp = zynq_readl(&dr_regs->usbcmd);
		zynq_writel(temp & ~USB_CMD_ATDTW, &dr_regs->usbcmd);

		if (tmp_stat)
			goto out;
	}

	/* Write dQH next pointer and terminate bit to 0 */
	temp = req->head->td_dma & EP_QUEUE_HEAD_NEXT_POINTER_MASK;
	dQH->next_dtd_ptr = cpu_to_le32(temp);

	/* Clear active and halt bit */
	temp = cpu_to_le32(~(EP_QUEUE_HEAD_STATUS_ACTIVE
			| EP_QUEUE_HEAD_STATUS_HALT));
	dQH->size_ioc_int_sts &= temp;

	/* Ensure that updates to the QH will occure before priming. */
	wmb();

	/* Prime endpoint by writing 1 to ENDPTPRIME */
	temp = ep_is_in(ep)
		? (1 << (ep_index(ep) + 16))
		: (1 << (ep_index(ep)));
	zynq_writel(temp, &dr_regs->endpointprime);
out:
	return;
}

/* Fill in the dTD structure
 * @req: request that the transfer belongs to
 * @length: return actually data length of the dTD
 * @dma: return dma address of the dTD
 * @is_last: return flag if it is the last dTD of the request
 * return: pointer to the built dTD */
static struct ep_td_struct *zynq_build_dtd(struct zynq_req *req, unsigned
		*length, dma_addr_t *dma, int *is_last)
{
	u32 swap_temp;
	struct ep_td_struct *dtd;

	/* how big will this transfer be? */
	*length = min(req->req.length - req->req.actual,
			(unsigned)EP_MAX_LENGTH_TRANSFER);

	dtd = dma_pool_alloc(udc_controller->td_pool, GFP_ATOMIC, dma);
	if (dtd == NULL)
		return dtd;

	dtd->td_dma = *dma;
	/* Clear reserved field */
	swap_temp = cpu_to_le32(dtd->size_ioc_sts);
	swap_temp &= ~DTD_RESERVED_FIELDS;
	dtd->size_ioc_sts = cpu_to_le32(swap_temp);

	/* Init all of buffer page pointers */
	swap_temp = (u32) (req->req.dma + req->req.actual);
	dtd->buff_ptr0 = cpu_to_le32(swap_temp);
	dtd->buff_ptr1 = cpu_to_le32(swap_temp + 0x1000);
	dtd->buff_ptr2 = cpu_to_le32(swap_temp + 0x2000);
	dtd->buff_ptr3 = cpu_to_le32(swap_temp + 0x3000);
	dtd->buff_ptr4 = cpu_to_le32(swap_temp + 0x4000);

	req->req.actual += *length;

	/* zlp is needed if req->req.zero is set */
	if (req->req.zero) {
		if (*length == 0 || (*length % req->ep->ep.maxpacket) != 0)
			*is_last = 1;
		else
			*is_last = 0;
	} else if (req->req.length == req->req.actual)
		*is_last = 1;
	else
		*is_last = 0;

	if ((*is_last) == 0)
		VDBG("multi-dtd request!");
	/* Fill in the transfer size; set active bit */
	swap_temp = ((*length << DTD_LENGTH_BIT_POS) | DTD_STATUS_ACTIVE);

	/* Enable interrupt for the last dtd of a request */
	if (*is_last && !req->req.no_interrupt)
		swap_temp |= DTD_IOC;

	dtd->size_ioc_sts = cpu_to_le32(swap_temp);

	mb();

	VDBG("length = %d address= 0x%x", *length, (int)*dma);

	return dtd;
}

/* Generate dtd chain for a request */
static int zynq_req_to_dtd(struct zynq_req *req)
{
	unsigned	count;
	int		is_last;
	int		is_first = 1;
	struct ep_td_struct	*last_dtd = NULL, *dtd;
	dma_addr_t dma;

	do {
		dtd = zynq_build_dtd(req, &count, &dma, &is_last);
		if (dtd == NULL)
			return -ENOMEM;

		if (is_first) {
			is_first = 0;
			req->head = dtd;
		} else {
			last_dtd->next_td_ptr = cpu_to_le32(dma);
			last_dtd->next_td_virt = dtd;
		}
		last_dtd = dtd;

		req->dtd_count++;
	} while (!is_last);

	dtd->next_td_ptr = cpu_to_le32(DTD_NEXT_TERMINATE);

	mb();
	req->tail = dtd;

	return 0;
}

/* queues (submits) an I/O request to an endpoint */
static int
zynq_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct zynq_ep *ep = container_of(_ep, struct zynq_ep, ep);
	struct zynq_req *req = container_of(_req, struct zynq_req, req);
	struct zynq_udc *udc;
	unsigned long flags;

	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		VDBG("%s, bad params", __func__);
		return -EINVAL;
	}
	if (unlikely(!_ep || !ep->ep.desc)) {
		VDBG("%s, bad ep", __func__);
		return -EINVAL;
	}
	if (usb_endpoint_xfer_isoc(ep->ep.desc)) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	req->ep = ep;

	/* map virtual address to hardware */
	if (req->req.dma == DMA_ADDR_INVALID) {
		int ret;

		ret = usb_gadget_map_request(&udc->gadget, _req, ep_is_in(ep));
		if (ret)
			return ret;
		req->mapped = 1;
	} else {
		dma_sync_single_for_device(ep->udc->gadget.dev.parent,
					req->req.dma, req->req.length,
					ep_is_in(ep)
						? DMA_TO_DEVICE
						: DMA_FROM_DEVICE);
		req->mapped = 0;
	}

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->dtd_count = 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* build dtds and push them to device queue */
	if (!zynq_req_to_dtd(req)) {
		zynq_queue_td(ep, req);
	} else {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -ENOMEM;
	}

	/* Update ep0 state */
	if ((ep_index(ep) == 0))
		udc->ep0_state = DATA_STATE_XMIT;

	/* irq handler advances the queue */
	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int zynq_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct zynq_ep *ep = container_of(_ep, struct zynq_ep, ep);
	struct zynq_req *req;
	unsigned long flags;
	int ep_num, stopped, ret = 0;
	u32 epctrl;

	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);
	stopped = ep->stopped;

	/* Stop the ep before we deal with the queue */
	ep->stopped = 1;
	ep_num = ep_index(ep);
	epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);
	if (ep_is_in(ep))
		epctrl &= ~EPCTRL_TX_ENABLE;
	else
		epctrl &= ~EPCTRL_RX_ENABLE;
	zynq_writel(epctrl, &dr_regs->endptctrl[ep_num]);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		ret = -EINVAL;
		goto out;
	}

	/* The request is in progress, or completed but not dequeued */
	if (ep->queue.next == &req->queue) {
		_req->status = -ECONNRESET;
		zynq_ep_fifo_flush(_ep);	/* flush current transfer */

		/* The request isn't the last request in this ep queue */
		if (req->queue.next != &ep->queue) {
			struct ep_queue_head *qh;
			struct zynq_req *next_req;

			qh = ep->qh;
			next_req = list_entry(req->queue.next, struct
					zynq_req, queue);

			/* Point the QH to the first TD of next request */
			zynq_writel((u32) next_req->head,
				(void __force __iomem *)&qh->curr_dtd_ptr);
		}

		/* The request hasn't been processed, patch up the TD chain */
	} else {
		struct zynq_req *prev_req;

		prev_req = list_entry(req->queue.prev, struct zynq_req,
				queue);
		zynq_writel(
		zynq_readl((void __force __iomem *)&req->tail->next_td_ptr),
			(void __force __iomem *)&prev_req->tail->next_td_ptr);

	}

	done(ep, req, -ECONNRESET);

	/* Enable EP */
out:	epctrl = zynq_readl(&dr_regs->endptctrl[ep_num]);
	if (ep_is_in(ep))
		epctrl |= EPCTRL_TX_ENABLE;
	else
		epctrl |= EPCTRL_RX_ENABLE;
	zynq_writel(epctrl, &dr_regs->endptctrl[ep_num]);
	ep->stopped = stopped;

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return ret;
}

/*-------------------------------------------------------------------------*/

/*-----------------------------------------------------------------
 * modify the endpoint halt feature
 * @ep: the non-isochronous endpoint being stalled
 * @value: 1--set halt  0--clear halt
 * Returns zero, or a negative error code.
*----------------------------------------------------------------*/
static int zynq_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct zynq_ep *ep = NULL;
	unsigned long flags = 0;
	int status = -EOPNOTSUPP;	/* operation not supported */
	unsigned char ep_dir = 0, ep_num = 0;
	struct zynq_udc *udc = NULL;

	ep = container_of(_ep, struct zynq_ep, ep);
	udc = ep->udc;
	if (!_ep || !ep->ep.desc) {
		status = -EINVAL;
		goto out;
	}

	if (usb_endpoint_xfer_isoc(ep->ep.desc)) {
		status = -EOPNOTSUPP;
		goto out;
	}

	/* Attempt to halt IN ep will fail if any transfer requests
	 * are still queue */
	if (value && ep_is_in(ep) && !list_empty(&ep->queue)) {
		status = -EAGAIN;
		goto out;
	}

	status = 0;
	ep_dir = ep_is_in(ep) ? USB_SEND : USB_RECV;
	ep_num = (unsigned char)(ep_index(ep));
	spin_lock_irqsave(&ep->udc->lock, flags);
	if (!value)
		ep->wedge = 0;
	dr_ep_change_stall(ep_num, ep_dir, value);
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	if (ep_index(ep) == 0) {
		udc->ep0_state = WAIT_FOR_SETUP;
		udc->ep0_dir = 0;
	}
out:
	VDBG(" %s %s halt stat %d", ep->ep.name,
			value ?  "set" : "clear", status);

	return status;
}

static int zynq_ep_set_wedge(struct usb_ep *_ep)
{
	struct zynq_ep *ep = NULL;
	unsigned long flags = 0;

	ep = container_of(_ep, struct zynq_ep, ep);

	if (!ep || !ep->ep.desc)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);
	ep->wedge = 1;
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return usb_ep_set_halt(_ep);
}

static void zynq_ep_fifo_flush(struct usb_ep *_ep)
{
	struct zynq_ep *ep;
	int ep_num, ep_dir;
	u32 bits;
	unsigned long timeout;
#define ZYNQ_UDC_FLUSH_TIMEOUT 1000

	if (!_ep) {
		return;
	} else {
		ep = container_of(_ep, struct zynq_ep, ep);
		if (!ep->ep.desc)
			return;
	}
	ep_num = ep_index(ep);
	ep_dir = ep_is_in(ep) ? USB_SEND : USB_RECV;

	if (ep_num == 0)
		bits = (1 << 16) | 1;
	else if (ep_dir == USB_SEND)
		bits = 1 << (16 + ep_num);
	else
		bits = 1 << ep_num;

	timeout = jiffies + ZYNQ_UDC_FLUSH_TIMEOUT;
	do {
		zynq_writel(bits, &dr_regs->endptflush);

		/* Wait until flush complete */
		while (zynq_readl(&dr_regs->endptflush)) {
			if (time_after(jiffies, timeout)) {
				ERR("ep flush timeout\n");
				return;
			}
			cpu_relax();
		}
		/* See if we need to flush again */
	} while (zynq_readl(&dr_regs->endptstatus) & bits);
}

static struct usb_ep_ops zynq_ep_ops = {
	.enable = zynq_ep_enable,
	.disable = zynq_ep_disable,

	.alloc_request = zynq_alloc_request,
	.free_request = zynq_free_request,

	.queue = zynq_ep_queue,
	.dequeue = zynq_ep_dequeue,

	.set_halt = zynq_ep_set_halt,
	.set_wedge = zynq_ep_set_wedge,
	.fifo_flush = zynq_ep_fifo_flush,	/* flush fifo */
};

/*-------------------------------------------------------------------------
		Gadget Driver Layer Operations
-------------------------------------------------------------------------*/

/*----------------------------------------------------------------------
 * Get the current frame number (from DR frame_index Reg )
 *----------------------------------------------------------------------*/
static int zynq_get_frame(struct usb_gadget *gadget)
{
	return (int)(zynq_readl(&dr_regs->frindex) & USB_FRINDEX_MASKS);
}

/*-----------------------------------------------------------------------
 * Tries to wake up the host connected to this gadget
 -----------------------------------------------------------------------*/
static int zynq_wakeup(struct usb_gadget *gadget)
{
	struct zynq_udc *udc = container_of(gadget, struct zynq_udc,
			gadget);
	u32 portsc;

	/* Remote wakeup feature not enabled by host */
	if (!udc->remote_wakeup)
		return -ENOTSUPP;

	portsc = zynq_readl(&dr_regs->portsc1);
	/* not suspended? */
	if (!(portsc & PORTSCX_PORT_SUSPEND))
		return 0;
	/* trigger force resume */
	portsc |= PORTSCX_PORT_FORCE_RESUME;
	zynq_writel(portsc, &dr_regs->portsc1);
	return 0;
}

static int can_pullup(struct zynq_udc *udc)
{
	return udc->driver && udc->softconnect && udc->vbus_active;
}

/* Notify controller that VBUS is powered, Called by whatever
   detects VBUS sessions */
static int zynq_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct zynq_udc	*udc;
	unsigned long	flags;

	udc = container_of(gadget, struct zynq_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);
	VDBG("VBUS %s", is_active ? "on" : "off");
	udc->vbus_active = (is_active != 0);
	if (can_pullup(udc))
		zynq_writel((zynq_readl(&dr_regs->usbcmd) |
					USB_CMD_RUN_STOP), &dr_regs->usbcmd);
	else
		zynq_writel((zynq_readl(&dr_regs->usbcmd) &
					~USB_CMD_RUN_STOP), &dr_regs->usbcmd);
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/* constrain controller's VBUS power usage
 * This call is used by gadget drivers during SET_CONFIGURATION calls,
 * reporting how much power the device may consume.  For example, this
 * could affect how quickly batteries are recharged.
 *
 * Returns zero on success, else negative errno.
 */
static int zynq_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct zynq_udc *udc;

	udc = container_of(gadget, struct zynq_udc, gadget);
	if (udc->transceiver)
		return usb_phy_set_power(udc->transceiver, mA);
	return -ENOTSUPP;
}

/* Change Data+ pullup status
 * this func is used by usb_gadget_connect/disconnet
 */
static int zynq_pullup(struct usb_gadget *gadget, int is_on)
{
	struct zynq_udc *udc;

	udc = container_of(gadget, struct zynq_udc, gadget);
	udc->softconnect = (is_on != 0);
	if (can_pullup(udc))
		zynq_writel((zynq_readl(&dr_regs->usbcmd) |
					USB_CMD_RUN_STOP), &dr_regs->usbcmd);
	else
		zynq_writel((zynq_readl(&dr_regs->usbcmd) &
					~USB_CMD_RUN_STOP), &dr_regs->usbcmd);

	return 0;
}

static void udc_reset_ep_queue(struct zynq_udc *udc, u8 pipe)
{
	struct zynq_ep *ep = get_ep_by_pipe(udc, pipe);

	if (ep->name)
		nuke(ep, -ESHUTDOWN);
}

/* Clear up all ep queues */
static int reset_queues(struct zynq_udc *udc)
{
	u8 pipe;

	for (pipe = 0; pipe < udc->max_pipes; pipe++)
		udc_reset_ep_queue(udc, pipe);

	/* report disconnect; the driver is already quiesced */
	spin_unlock(&udc->lock);
	udc->driver->disconnect(&udc->gadget);
	spin_lock(&udc->lock);

	return 0;
}

#ifdef CONFIG_USB_ZYNQ_PHY
/*----------------------------------------------------------------
 * OTG Related changes
 *--------------------------------------------------------------*/
static int zynq_udc_start_peripheral(struct usb_phy  *otg)
{
	struct usb_gadget	*gadget = otg->otg->gadget;
	struct zynq_udc *udc = container_of(gadget, struct zynq_udc,
						gadget);
	unsigned long flags = 0;
	unsigned int tmp;

	spin_lock_irqsave(&udc->lock, flags);

	if (!otg->otg->default_a) {
		dr_controller_setup(udc);
		reset_queues(udc);
	} else {
		/* A-device HABA resets the controller */
		tmp = udc->ep_qh_dma;
		tmp &= USB_EP_LIST_ADDRESS_MASK;
		zynq_writel(tmp, &dr_regs->endpointlistaddr);
	}
	ep0_setup(udc);
	dr_controller_run(udc);

	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = 0;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int zynq_udc_stop_peripheral(struct usb_phy *otg)
{
	struct usb_gadget	*gadget = otg->otg->gadget;
	struct zynq_udc *udc = container_of(gadget, struct zynq_udc,
						gadget);

	dr_controller_stop(udc);

	/* refer to USB OTG 6.6.2.3 b_hnp_en is cleared */
	if (!udc->xotg->otg.otg->default_a)
		udc->xotg->hsm.b_hnp_enable = 0;

	return 0;
}
#endif

/*----------------------------------------------------------------*
 * Hook to gadget drivers
 * Called by initialization code of gadget drivers
*----------------------------------------------------------------*/
static int zynq_udc_start(struct usb_gadget *g,
				struct usb_gadget_driver *driver)
{
	int retval = 0;
	unsigned long flags = 0;

	/* lock is needed but whether should use this lock or another */
	spin_lock_irqsave(&udc_controller->lock, flags);

	driver->driver.bus = NULL;
	/* hook up the driver */
	udc_controller->driver = driver;
	udc_controller->gadget.dev.driver = &driver->driver;
	spin_unlock_irqrestore(&udc_controller->lock, flags);

#ifdef CONFIG_USB_ZYNQ_PHY
	if (gadget_is_otg(&udc_controller->gadget)) {
		retval = otg_set_peripheral(udc_controller->transceiver->otg,
				&udc_controller->gadget);
		if (retval < 0) {
			VDBG("can't bind to otg transceiver\n");
			driver->unbind(&udc_controller->gadget);
			udc_controller->gadget.dev.driver = NULL;
			udc_controller->driver = NULL;
			return retval;
		}
		/* Exporting start and stop routines */
		udc_controller->xotg->start_peripheral =
					zynq_udc_start_peripheral;
		udc_controller->xotg->stop_peripheral =
					zynq_udc_stop_peripheral;

		if (!udc_controller->transceiver->otg->default_a &&
					udc_controller->stopped &&
				udc_controller->xotg->hsm.b_sess_vld) {
			dr_controller_setup(udc_controller);
			ep0_setup(udc_controller);
			/* Enable DR IRQ reg and Set usbcmd reg  Run bit */
			dr_controller_run(udc_controller);
			udc_controller->usb_state = USB_STATE_ATTACHED;
			udc_controller->ep0_state = WAIT_FOR_SETUP;
			udc_controller->ep0_dir = 0;
			zynq_update_transceiver();
		}
	} else {
		/* Enable DR IRQ reg and Set usbcmd reg  Run bit */
		dr_controller_run(udc_controller);
		udc_controller->usb_state = USB_STATE_ATTACHED;
		udc_controller->ep0_state = WAIT_FOR_SETUP;
		udc_controller->ep0_dir = 0;
	}
#else
	/* Enable DR IRQ reg and Set usbcmd reg  Run bit */
	dr_controller_run(udc_controller);
	udc_controller->usb_state = USB_STATE_ATTACHED;
	udc_controller->ep0_state = WAIT_FOR_SETUP;
	udc_controller->ep0_dir = 0;
#endif

	pr_info("%s: bind to driver %s\n",
			udc_controller->gadget.name, driver->driver.name);
	if (retval)
		pr_warn("gadget driver register failed %d\n", retval);

	return retval;
}

/* Disconnect from gadget driver */
static int zynq_udc_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct zynq_ep *loop_ep;
	unsigned long flags;

	if (udc_controller->transceiver)
		otg_set_peripheral(udc_controller->transceiver->otg, NULL);

	/* stop DR, disable intr */
	dr_controller_stop(udc_controller);

	/* in fact, no needed */
	udc_controller->usb_state = USB_STATE_ATTACHED;
	udc_controller->ep0_state = WAIT_FOR_SETUP;
	udc_controller->ep0_dir = 0;

	/* stand operation */
	spin_lock_irqsave(&udc_controller->lock, flags);
	udc_controller->gadget.speed = USB_SPEED_UNKNOWN;
	nuke(&udc_controller->eps[0], -ESHUTDOWN);
	list_for_each_entry(loop_ep, &udc_controller->gadget.ep_list,
			ep.ep_list)
		nuke(loop_ep, -ESHUTDOWN);
	spin_unlock_irqrestore(&udc_controller->lock, flags);

#ifdef CONFIG_USB_ZYNQ_PHY
	if (gadget_is_otg(&udc_controller->gadget)) {
		udc_controller->xotg->start_peripheral = NULL;
		udc_controller->xotg->stop_peripheral = NULL;
	}
#endif
	udc_controller->gadget.dev.driver = NULL;
	udc_controller->driver = NULL;

	return 0;
}

/* defined in gadget.h */
static struct usb_gadget_ops zynq_gadget_ops = {
	.get_frame = zynq_get_frame,
	.wakeup = zynq_wakeup,
/*	.set_selfpowered = zynq_set_selfpowered, */ /* Always selfpowered */
	.vbus_session = zynq_vbus_session,
	.vbus_draw = zynq_vbus_draw,
	.pullup = zynq_pullup,
	.udc_start = zynq_udc_start,
	.udc_stop = zynq_udc_stop,
};

/* Set protocol stall on ep0, protocol stall will automatically be cleared
   on new transaction */
static void ep0stall(struct zynq_udc *udc)
{
	u32 tmp;

	/* must set tx and rx to stall at the same time */
	tmp = zynq_readl(&dr_regs->endptctrl[0]);
	tmp |= EPCTRL_TX_EP_STALL | EPCTRL_RX_EP_STALL;
	zynq_writel(tmp, &dr_regs->endptctrl[0]);
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = 0;
}

/* Prime a status phase for ep0 */
static int ep0_prime_status(struct zynq_udc *udc, int direction)
{
	struct zynq_req *req = udc->status_req;
	struct zynq_ep *ep;
	int ret;

	if (direction == EP_DIR_IN)
		udc->ep0_dir = USB_DIR_IN;
	else
		udc->ep0_dir = USB_DIR_OUT;

	ep = &udc->eps[0];
	udc->ep0_state = WAIT_FOR_OUT_STATUS;

	req->ep = ep;
	req->req.length = 0;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;
	ret = usb_gadget_map_request(&udc->gadget, &req->req, ep_is_in(ep));
	if (ret)
		return ret;

	req->mapped = 1;

	if (zynq_req_to_dtd(req) == 0)
		zynq_queue_td(ep, req);
	else
		return -ENOMEM;

	list_add_tail(&req->queue, &ep->queue);

	return 0;
}

/*
 * ch9 Set address
 */
static void ch9setaddress(struct zynq_udc *udc, u16 value, u16 index, u16
		length)
{
	/* Save the new address to device struct */
	udc->device_address = (u8) value;
	/* Update usb state */
	udc->usb_state = USB_STATE_ADDRESS;
	/* Status phase */
	if (ep0_prime_status(udc, EP_DIR_IN))
		ep0stall(udc);
}

/*
 * ch9 Get status
 */
static void ch9getstatus(struct zynq_udc *udc, u8 request_type, u16 value,
		u16 index, u16 length)
{
	u16 tmp = 0;		/* Status, cpu endian */
	struct zynq_req *req;
	struct zynq_ep *ep;

	ep = &udc->eps[0];

	if ((request_type & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		/* Get device status */
		tmp = 1 << USB_DEVICE_SELF_POWERED;
		tmp |= udc->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		/* Get interface status */
		/* We don't have interface information in udc driver */
		tmp = 0;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
		/* Get endpoint status */
		struct zynq_ep *target_ep;

		target_ep = get_ep_by_pipe(udc, get_pipe_by_windex(index));

		/* stall if endpoint doesn't exist */
		if (!target_ep->ep.desc)
			goto stall;
		tmp = dr_ep_get_stall(ep_index(target_ep), ep_is_in(target_ep))
				<< USB_ENDPOINT_HALT;
	}

	udc->ep0_dir = USB_DIR_IN;
	/* Borrow the per device status_req */
	req = udc->status_req;
	/* Fill in the reqest structure */
	*((u16 *) req->req.buf) = cpu_to_le16(tmp);
	req->ep = ep;
	req->req.length = 2;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;
	req->req.dma = dma_map_single(ep->udc->gadget.dev.parent,
				req->req.buf, req->req.length,
				ep_is_in(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	req->mapped = 1;

	/* prime the data phase */
	if ((zynq_req_to_dtd(req) == 0))
		zynq_queue_td(ep, req);
	else			/* no mem */
		goto stall;

	list_add_tail(&req->queue, &ep->queue);
	udc->ep0_state = DATA_STATE_XMIT;
	return;
stall:
	ep0stall(udc);
}

static void setup_received_irq(struct zynq_udc *udc,
		struct usb_ctrlrequest *setup)
{
	u16 wValue = le16_to_cpu(setup->wValue);
	u16 wIndex = le16_to_cpu(setup->wIndex);
	u16 wLength = le16_to_cpu(setup->wLength);
	u16 testsel = 0;

	udc_reset_ep_queue(udc, 0);

	/* We process some stardard setup requests here */
	switch (setup->bRequest) {
	case USB_REQ_GET_STATUS:
		/* Data+Status phase from udc */
		if ((setup->bRequestType & (USB_DIR_IN | USB_TYPE_MASK))
					!= (USB_DIR_IN | USB_TYPE_STANDARD))
			break;
		ch9getstatus(udc, setup->bRequestType, wValue, wIndex, wLength);
		return;

	case USB_REQ_SET_ADDRESS:
		/* Status phase from udc */
		if (setup->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD
						| USB_RECIP_DEVICE))
			break;
		ch9setaddress(udc, wValue, wIndex, wLength);
		return;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* Status phase from udc */
	{
		int rc = -EOPNOTSUPP;

		if ((setup->bRequestType & (USB_RECIP_MASK | USB_TYPE_MASK))
				== (USB_RECIP_ENDPOINT | USB_TYPE_STANDARD)) {
			int pipe = get_pipe_by_windex(wIndex);
			struct zynq_ep *ep;

			if (wValue != 0 || wLength != 0 || pipe > udc->max_ep)
				break;
			ep = get_ep_by_pipe(udc, pipe);

			spin_unlock(&udc->lock);
			if (setup->bRequest == USB_REQ_SET_FEATURE) {
				rc = zynq_ep_set_halt(&ep->ep, 1);
			} else {
				if (!ep->wedge)
					rc = zynq_ep_set_halt(&ep->ep, 0);
				else
					rc = 0;
			}
			spin_lock(&udc->lock);

		} else if ((setup->bRequestType & (USB_RECIP_MASK
				| USB_TYPE_MASK)) == (USB_RECIP_DEVICE
				| USB_TYPE_STANDARD)) {
			/* TEST MODE feature */
			if (wValue == USB_DEVICE_TEST_MODE) {
				testsel = (wIndex >> 8) & 0xff;
				rc = 0;
				goto status_phase;
			}

			/* Note: The driver has not include OTG support yet.
			 * This will be set when OTG support is added */
			if (!gadget_is_otg(&udc->gadget))
				break;
			else if (setup->bRequest == USB_DEVICE_B_HNP_ENABLE) {
				udc->gadget.b_hnp_enable = 1;
#ifdef CONFIG_USB_ZYNQ_PHY
				if (!udc->xotg->otg.otg->default_a)
					udc->xotg->hsm.b_hnp_enable = 1;
#endif
			} else if (setup->bRequest == USB_DEVICE_A_HNP_SUPPORT)
				udc->gadget.a_hnp_support = 1;
			else if (setup->bRequest ==
					USB_DEVICE_A_ALT_HNP_SUPPORT)
				udc->gadget.a_alt_hnp_support = 1;
			else
				break;
			rc = 0;
		} else
			break;

status_phase:
		if (rc == 0) {
			if (ep0_prime_status(udc, EP_DIR_IN)) {
				ep0stall(udc);
			} else {
				if (testsel) {
					u32 tmp;
					/* Wait for status phase to complete */
					mdelay(1);
					tmp = zynq_readl(&dr_regs->portsc1);
					tmp |= (testsel << 16);
					zynq_writel(tmp, &dr_regs->portsc1);
				}
			}
		}
		return;
	}

	default:
		break;
	}

	/* Requests handled by gadget */
	if (wLength) {
		/* Data phase from gadget, status phase from udc */
		udc->ep0_dir = (setup->bRequestType & USB_DIR_IN)
				?  USB_DIR_IN : USB_DIR_OUT;
		spin_unlock(&udc->lock);
		if (udc->driver->setup(&udc->gadget,
				&udc->local_setup_buff) < 0)
			ep0stall(udc);
		spin_lock(&udc->lock);
		udc->ep0_state = (setup->bRequestType & USB_DIR_IN)
				?  DATA_STATE_XMIT : DATA_STATE_RECV;
	} else {
		/* No data phase, IN status from gadget */
		udc->ep0_dir = USB_DIR_IN;
		spin_unlock(&udc->lock);
		if (udc->driver->setup(&udc->gadget,
				&udc->local_setup_buff) < 0)
			ep0stall(udc);
		spin_lock(&udc->lock);
		udc->ep0_state = WAIT_FOR_OUT_STATUS;
	}
}

/* Process request for Data or Status phase of ep0
 * prime status phase if needed */
static void ep0_req_complete(struct zynq_udc *udc, struct zynq_ep *ep0,
		struct zynq_req *req)
{
	if (udc->usb_state == USB_STATE_ADDRESS) {
		/* Set the new address */
		u32 new_address = (u32) udc->device_address;
		zynq_writel(new_address << USB_DEVICE_ADDRESS_BIT_POS,
				&dr_regs->deviceaddr);
	}

	done(ep0, req, 0);

	switch (udc->ep0_state) {
	case DATA_STATE_XMIT:
		/* receive status phase */
		if (ep0_prime_status(udc, EP_DIR_OUT))
			ep0stall(udc);
		break;
	case DATA_STATE_RECV:
		/* send status phase */
		if (ep0_prime_status(udc, EP_DIR_IN))
			ep0stall(udc);
		break;
	case WAIT_FOR_OUT_STATUS:
		udc->ep0_state = WAIT_FOR_SETUP;
		break;
	case WAIT_FOR_SETUP:
		ERR("Unexpect ep0 packets\n");
		break;
	default:
		ep0stall(udc);
		break;
	}
}

/* Tripwire mechanism to ensure a setup packet payload is extracted without
 * being corrupted by another incoming setup packet */
static void tripwire_handler(struct zynq_udc *udc, u8 ep_num, u8 *buffer_ptr)
{
	u32 temp;
	struct ep_queue_head *qh;

	qh = &udc->ep_qh[ep_num * 2 + EP_DIR_OUT];

	/* Clear bit in ENDPTSETUPSTAT */
	temp = zynq_readl(&dr_regs->endptsetupstat);
	zynq_writel(temp | (1 << ep_num), &dr_regs->endptsetupstat);

	/* while a hazard exists when setup package arrives */
	do {
		/* Set Setup Tripwire */
		temp = zynq_readl(&dr_regs->usbcmd);
		zynq_writel(temp | USB_CMD_SUTW, &dr_regs->usbcmd);

		/* Copy the setup packet to local buffer */
		memcpy(buffer_ptr, (u8 *) qh->setup_buffer, 8);
	} while (!(zynq_readl(&dr_regs->usbcmd) & USB_CMD_SUTW));

	/* Clear Setup Tripwire */
	temp = zynq_readl(&dr_regs->usbcmd);
	zynq_writel(temp & ~USB_CMD_SUTW, &dr_regs->usbcmd);
}

/* process-ep_req(): free the completed Tds for this req */
static int process_ep_req(struct zynq_udc *udc, int pipe,
		struct zynq_req *curr_req)
{
	struct ep_td_struct *curr_td;
	int	td_complete, actual, remaining_length, j, tmp;
	int	status = 0;
	int	errors = 0;
	struct  ep_queue_head *curr_qh = &udc->ep_qh[pipe];
	int direction = pipe % 2;

	curr_td = curr_req->head;
	td_complete = 0;
	actual = curr_req->req.length;

	for (j = 0; j < curr_req->dtd_count; j++) {
		remaining_length = (le32_to_cpu(curr_td->size_ioc_sts)
					& DTD_PACKET_SIZE)
				>> DTD_LENGTH_BIT_POS;
		actual -= remaining_length;
		errors = le32_to_cpu(curr_td->size_ioc_sts) &
						DTD_ERROR_MASK;
		if (errors) {
			if (errors & DTD_STATUS_HALTED) {
				ERR("dTD error %08x QH=%d\n", errors, pipe);
				/* Clear the errors and Halt condition */
				tmp = le32_to_cpu(curr_qh->size_ioc_int_sts);
				tmp &= ~errors;
				curr_qh->size_ioc_int_sts = cpu_to_le32(tmp);
				status = -EPIPE;
				/* FIXME: continue with next queued TD? */

				break;
			}
			if (errors & DTD_STATUS_DATA_BUFF_ERR) {
				VDBG("Transfer overflow");
				status = -EPROTO;
				break;
			} else if (errors & DTD_STATUS_TRANSACTION_ERR) {
				VDBG("ISO error");
				status = -EILSEQ;
				break;
			} else
				ERR("Unknown error has occured (0x%x)!\n",
					errors);

		} else if (le32_to_cpu(curr_td->size_ioc_sts)
				& DTD_STATUS_ACTIVE) {
			VDBG("Request not complete");
			status = REQ_UNCOMPLETE;
			return status;
		} else if (remaining_length) {
			if (direction) {
				VDBG("Transmit dTD remaining length not zero");
				status = -EPROTO;
				break;
			} else {
				td_complete++;
				break;
			}
		} else {
			td_complete++;
			VDBG("dTD transmitted successful");
		}

		if (j != curr_req->dtd_count - 1)
			curr_td = (struct ep_td_struct *)curr_td->next_td_virt;
	}

	if (status)
		return status;

	curr_req->req.actual = actual;

	return 0;
}

/* Process a DTD completion interrupt */
static void dtd_complete_irq(struct zynq_udc *udc)
{
	u32 bit_pos;
	int i, ep_num, direction, bit_mask, status;
	struct zynq_ep *curr_ep;
	struct zynq_req *curr_req, *temp_req;

	/* Clear the bits in the register */
	bit_pos = zynq_readl(&dr_regs->endptcomplete);
	zynq_writel(bit_pos, &dr_regs->endptcomplete);

	if (!bit_pos)
		return;

	for (i = 0; i < udc->max_ep; i++) {
		ep_num = i >> 1;
		direction = i % 2;

		bit_mask = 1 << (ep_num + 16 * direction);

		if (!(bit_pos & bit_mask))
			continue;

		curr_ep = get_ep_by_pipe(udc, i);

		/* If the ep is configured */
		if (curr_ep->name == NULL) {
			WARNING("Invalid EP?");
			continue;
		}

		/* process the req queue until an uncomplete request */
		list_for_each_entry_safe(curr_req, temp_req, &curr_ep->queue,
				queue) {
			status = process_ep_req(udc, i, curr_req);

			VDBG("status of process_ep_req= %d, ep = %d",
					status, ep_num);
			if (status == REQ_UNCOMPLETE)
				break;
			/* Clear the endpoint complete events */
			zynq_writel(bit_mask, &dr_regs->endptcomplete);
			/* write back status to req */
			curr_req->req.status = status;

			if (ep_num == 0) {
				ep0_req_complete(udc, curr_ep, curr_req);
				break;
			} else
				done(curr_ep, curr_req, status);
		}
	}
}

/* Process a port change interrupt */
static void port_change_irq(struct zynq_udc *udc)
{
	u32 speed;

	/* Bus resetting is finished */
	if (!(zynq_readl(&dr_regs->portsc1) & PORTSCX_PORT_RESET)) {
		/* Get the speed */
		speed = (zynq_readl(&dr_regs->portsc1)
				& PORTSCX_PORT_SPEED_MASK);
		switch (speed) {
		case PORTSCX_PORT_SPEED_HIGH:
			udc->gadget.speed = USB_SPEED_HIGH;
			break;
		case PORTSCX_PORT_SPEED_FULL:
			udc->gadget.speed = USB_SPEED_FULL;
			break;
		case PORTSCX_PORT_SPEED_LOW:
			udc->gadget.speed = USB_SPEED_LOW;
			break;
		default:
			udc->gadget.speed = USB_SPEED_UNKNOWN;
			break;
		}
	}

	/* Update USB state */
	if (!udc->resume_state)
		udc->usb_state = USB_STATE_DEFAULT;
}

/* Process suspend interrupt */
static void suspend_irq(struct zynq_udc *udc)
{
	udc->resume_state = udc->usb_state;
	udc->usb_state = USB_STATE_SUSPENDED;

#ifdef CONFIG_USB_ZYNQ_PHY
	if (gadget_is_otg(&udc->gadget)) {
		if (udc->xotg->otg.otg->default_a) {
			udc->xotg->hsm.b_bus_suspend = 1;
			/* notify transceiver the state changes */
			if (spin_trylock(&udc->xotg->wq_lock)) {
				zynq_update_transceiver();
				spin_unlock(&udc->xotg->wq_lock);
			}
		} else {
			if (!udc->xotg->hsm.a_bus_suspend) {
				udc->xotg->hsm.a_bus_suspend = 1;
				udc->xotg->hsm.b_bus_req = 1;
				/* notify transceiver the state changes */
				if (spin_trylock(&udc->xotg->wq_lock)) {
					zynq_update_transceiver();
					spin_unlock(&udc->xotg->wq_lock);
				}
			}
		}
	}
#endif
	/* report suspend to the driver, serial.c does not support this */
	if (udc->driver->suspend)
		udc->driver->suspend(&udc->gadget);
}

static void bus_resume(struct zynq_udc *udc)
{
	udc->usb_state = udc->resume_state;
	udc->resume_state = 0;

	/* report resume to the driver, serial.c does not support this */
	if (udc->driver->resume)
		udc->driver->resume(&udc->gadget);
}

/* Process reset interrupt */
static void reset_irq(struct zynq_udc *udc)
{
	u32 temp;
	unsigned long timeout;

	/* Clear the device address */
	temp = zynq_readl(&dr_regs->deviceaddr);
	zynq_writel(temp & ~USB_DEVICE_ADDRESS_MASK, &dr_regs->deviceaddr);

	udc->device_address = 0;

	/* Clear usb state */
	udc->resume_state = 0;
	udc->ep0_dir = 0;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->remote_wakeup = 0;	/* default to 0 on reset */
	udc->gadget.b_hnp_enable = 0;
	udc->gadget.a_hnp_support = 0;
	udc->gadget.a_alt_hnp_support = 0;

	/* Clear all the setup token semaphores */
	temp = zynq_readl(&dr_regs->endptsetupstat);
	zynq_writel(temp, &dr_regs->endptsetupstat);

	/* Clear all the endpoint complete status bits */
	temp = zynq_readl(&dr_regs->endptcomplete);
	zynq_writel(temp, &dr_regs->endptcomplete);

	timeout = jiffies + 100;
	while (zynq_readl(&dr_regs->endpointprime)) {
		/* Wait until all endptprime bits cleared */
		if (time_after(jiffies, timeout)) {
			ERR("Timeout for reset\n");
			break;
		}
		cpu_relax();
	}

	/* Write 1s to the flush register */
	zynq_writel(0xffffffff, &dr_regs->endptflush);

	VDBG("Bus reset");
	/* Reset all the queues, include XD, dTD, EP queue
	 * head and TR Queue */
	reset_queues(udc);
	udc->usb_state = USB_STATE_DEFAULT;
}

/*
 * USB device controller interrupt handler
 */
static irqreturn_t zynq_udc_irq(int irq, void *_udc)
{
	struct zynq_udc *udc = _udc;
	u32 irq_src, otg_sts;
	irqreturn_t status = IRQ_NONE;
	unsigned long flags;
#ifdef CONFIG_USB_ZYNQ_PHY
	unsigned long temp;
#endif

	/* Disable ISR for OTG host mode */
	if (udc->stopped)
		return IRQ_NONE;
#ifdef CONFIG_USB_ZYNQ_PHY
	if (gadget_is_otg(&udc->gadget)) {
		/* A-device */
		if (udc->transceiver->otg->default_a &&
			(udc->transceiver->state != OTG_STATE_A_PERIPHERAL))
			return IRQ_NONE;
		/* B-device */
		if ((udc->transceiver->state == OTG_STATE_B_WAIT_ACON) ||
			(udc->transceiver->state == OTG_STATE_B_HOST))
			return IRQ_NONE;
	}
#endif
	spin_lock_irqsave(&udc->lock, flags);
	irq_src = zynq_readl(&dr_regs->usbsts) &
		zynq_readl(&dr_regs->usbintr);

	/* Clear notification bits */
	zynq_writel(irq_src, &dr_regs->usbsts);

	/*
	 * Check disconnect event from B session end interrupt.
	 * This is a SW workaround for USB disconnect detection as mentioned
	 * in AR# 47538
	 */
	if (!gadget_is_otg(&udc->gadget)) {
		otg_sts = zynq_readl(&dr_regs->otgsc);
		if (otg_sts & OTGSC_BSEIS) {
			zynq_writel(otg_sts, &dr_regs->otgsc);
			reset_queues(udc);
			status = IRQ_HANDLED;
		}
	}

	/* VDBG("irq_src [0x%8x]", irq_src); */

	/* Need to resume? */
	if (udc->usb_state == USB_STATE_SUSPENDED)
		if ((zynq_readl(&dr_regs->portsc1) &
					PORTSCX_PORT_SUSPEND) == 0)
			bus_resume(udc);

	/* USB Interrupt */
	if (irq_src & USB_STS_INT) {
		VDBG("Packet int");
		/* Setup package, we only support ep0 as control ep */
		if (zynq_readl(&dr_regs->endptsetupstat) &
						EP_SETUP_STATUS_EP0) {
			tripwire_handler(udc, 0,
					(u8 *) (&udc->local_setup_buff));
			setup_received_irq(udc, &udc->local_setup_buff);
			status = IRQ_HANDLED;
		}

		/* completion of dtd */
		if (zynq_readl(&dr_regs->endptcomplete)) {
			dtd_complete_irq(udc);
			status = IRQ_HANDLED;
		}
	}

	/* SOF (for ISO transfer) */
	if (irq_src & USB_STS_SOF)
		status = IRQ_HANDLED;

	/* Port Change */
	if (irq_src & USB_STS_PORT_CHANGE) {
		port_change_irq(udc);
		status = IRQ_HANDLED;
	}

	/* Reset Received */
	if (irq_src & USB_STS_RESET) {
		reset_irq(udc);
#ifdef CONFIG_USB_ZYNQ_PHY
		if (gadget_is_otg(&udc->gadget)) {
			/* Clear any previous suspend status bit */
			temp = zynq_readl(&dr_regs->usbsts);
			if (temp & USB_INTR_DEVICE_SUSPEND) {
				udc->usb_state = USB_STATE_SUSPENDED;
				temp |= USB_INTR_DEVICE_SUSPEND;
				zynq_writel(temp, &dr_regs->usbsts);
			}
			/* Enable suspend interrupt */
			temp = zynq_readl(&dr_regs->usbintr);
			temp |= USB_INTR_DEVICE_SUSPEND;
			zynq_writel(temp, &dr_regs->usbintr);
		}
#endif
		status = IRQ_HANDLED;
	}

	/* Sleep Enable (Suspend) */
	if (irq_src & USB_STS_SUSPEND) {
		suspend_irq(udc);
		status = IRQ_HANDLED;
	}

	if (irq_src & (USB_STS_ERR | USB_STS_SYS_ERR))
		VDBG("Error IRQ %x", irq_src);

	spin_unlock_irqrestore(&udc->lock, flags);
	return status;
}

/*-------------------------------------------------------------------------
		PROC File System Support
-------------------------------------------------------------------------*/
#ifdef CONFIG_USB_GADGET_DEBUG_FILES

#include <linux/seq_file.h>

static const char proc_filename[] = "driver/zynq_udc";

static int zynq_proc_read(struct seq_file *m, void *v)
{
	unsigned long flags;
	int i;
	u32 tmp_reg;
	struct zynq_ep *ep = NULL;
	struct zynq_req *req;
	struct zynq_udc *udc = udc_controller;

	spin_lock_irqsave(&udc->lock, flags);

	/* ------basic driver information ---- */
	seq_printf(m, DRIVER_DESC "\n"
			"%s version: %s\n"
			"Gadget driver: %s\n\n",
			driver_name, DRIVER_VERSION,
			udc->driver ? udc->driver->driver.name : "(none)");

	/* ------ DR Registers ----- */
	tmp_reg = zynq_readl(&dr_regs->usbcmd);
	seq_printf(m, "USBCMD reg:\n"
			"SetupTW: %d\n"
			"Run/Stop: %s\n\n",
			(tmp_reg & USB_CMD_SUTW) ? 1 : 0,
			(tmp_reg & USB_CMD_RUN_STOP) ? "Run" : "Stop");

	tmp_reg = zynq_readl(&dr_regs->usbsts);
	seq_printf(m, "USB Status Reg:\n"
			"Dr Suspend: %d Reset Received: %d System Error: %s "
			"USB Error Interrupt: %s\n\n",
			(tmp_reg & USB_STS_SUSPEND) ? 1 : 0,
			(tmp_reg & USB_STS_RESET) ? 1 : 0,
			(tmp_reg & USB_STS_SYS_ERR) ? "Err" : "Normal",
			(tmp_reg & USB_STS_ERR) ? "Err detected" : "No err");

	tmp_reg = zynq_readl(&dr_regs->usbintr);
	seq_printf(m, "USB Intrrupt Enable Reg:\n"
			"Sleep Enable: %d SOF Received Enable: %d "
			"Reset Enable: %d\n"
			"System Error Enable: %d "
			"Port Change Dectected Enable: %d\n"
			"USB Error Intr Enable: %d USB Intr Enable: %d\n\n",
			(tmp_reg & USB_INTR_DEVICE_SUSPEND) ? 1 : 0,
			(tmp_reg & USB_INTR_SOF_EN) ? 1 : 0,
			(tmp_reg & USB_INTR_RESET_EN) ? 1 : 0,
			(tmp_reg & USB_INTR_SYS_ERR_EN) ? 1 : 0,
			(tmp_reg & USB_INTR_PTC_DETECT_EN) ? 1 : 0,
			(tmp_reg & USB_INTR_ERR_INT_EN) ? 1 : 0,
			(tmp_reg & USB_INTR_INT_EN) ? 1 : 0);

	tmp_reg = zynq_readl(&dr_regs->frindex);
	seq_printf(m, "USB Frame Index Reg: Frame Number is 0x%x\n\n",
			(tmp_reg & USB_FRINDEX_MASKS));

	tmp_reg = zynq_readl(&dr_regs->deviceaddr);
	seq_printf(m, "USB Device Address Reg: Device Addr is 0x%x\n\n",
			(tmp_reg & USB_DEVICE_ADDRESS_MASK));

	tmp_reg = zynq_readl(&dr_regs->endpointlistaddr);
	seq_printf(m, "USB Endpoint List Address Reg: "
			"Device Addr is 0x%x\n\n",
			(tmp_reg & USB_EP_LIST_ADDRESS_MASK));

	tmp_reg = zynq_readl(&dr_regs->portsc1);
	seq_printf(m, "USB Port Status&Control Reg:\n"
		"Port Transceiver Type : %s Port Speed: %s\n"
		"PHY Low Power Suspend: %s Port Reset: %s "
		"Port Suspend Mode: %s\n"
		"Over-current Change: %s "
		"Port Enable/Disable Change: %s\n"
		"Port Enabled/Disabled: %s "
		"Current Connect Status: %s\n\n", ({
			char *s;
			switch (tmp_reg & PORTSCX_PTS_FSLS) {
			case PORTSCX_PTS_UTMI:
				s = "UTMI"; break;
			case PORTSCX_PTS_ULPI:
				s = "ULPI "; break;
			case PORTSCX_PTS_FSLS:
				s = "FS/LS Serial"; break;
			default:
				s = "None"; break;
			}
			s; }), ({
			char *s;
			switch (tmp_reg & PORTSCX_PORT_SPEED_UNDEF) {
			case PORTSCX_PORT_SPEED_FULL:
				s = "Full Speed"; break;
			case PORTSCX_PORT_SPEED_LOW:
				s = "Low Speed"; break;
			case PORTSCX_PORT_SPEED_HIGH:
				s = "High Speed"; break;
			default:
				s = "Undefined"; break;
			}
			s;
		}),
		(tmp_reg & PORTSCX_PHY_LOW_POWER_SPD) ?
		"Normal PHY mode" : "Low power mode",
		(tmp_reg & PORTSCX_PORT_RESET) ? "In Reset" :
		"Not in Reset",
		(tmp_reg & PORTSCX_PORT_SUSPEND) ? "In " : "Not in",
		(tmp_reg & PORTSCX_OVER_CURRENT_CHG) ? "Dected" : "No",
		(tmp_reg & PORTSCX_PORT_EN_DIS_CHANGE) ? "Disable" :
		"Not change",
		(tmp_reg & PORTSCX_PORT_ENABLE) ? "Enable" : "Not correct",
		(tmp_reg & PORTSCX_CURRENT_CONNECT_STATUS) ?
		"Attached" : "Not-Att");

	tmp_reg = zynq_readl(&dr_regs->usbmode);
	seq_printf(m, "USB Mode Reg: Controller Mode is: %s\n\n", ({
				char *s;
				switch (tmp_reg & USB_MODE_CTRL_MODE_HOST) {
				case USB_MODE_CTRL_MODE_IDLE:
					s = "Idle"; break;
				case USB_MODE_CTRL_MODE_DEVICE:
					s = "Device Controller"; break;
				case USB_MODE_CTRL_MODE_HOST:
					s = "Host Controller"; break;
				default:
					s = "None"; break;
				}
				s;
			}));

	tmp_reg = zynq_readl(&dr_regs->endptsetupstat);
	seq_printf(m, "Endpoint Setup Status Reg: SETUP on ep 0x%x\n\n",
			(tmp_reg & EP_SETUP_STATUS_MASK));

	for (i = 0; i < udc->max_ep / 2; i++) {
		tmp_reg = zynq_readl(&dr_regs->endptctrl[i]);
		seq_printf(m, "EP Ctrl Reg [0x%x]: = [0x%x]\n",
				i, tmp_reg);
	}
	tmp_reg = zynq_readl(&dr_regs->endpointprime);
	seq_printf(m, "EP Prime Reg = [0x%x]\n\n", tmp_reg);

	/* ------zynq_udc, zynq_ep, zynq_request structure information
	 * ----- */
	ep = &udc->eps[0];
	seq_printf(m, "For %s Maxpkt is 0x%x index is 0x%x\n",
			ep->ep.name, ep_maxpacket(ep), ep_index(ep));

	if (list_empty(&ep->queue)) {
		seq_puts(m, "its req queue is empty\n\n");
	} else {
		list_for_each_entry(req, &ep->queue, queue) {
			seq_printf(m,
				"req %p actual 0x%x length 0x%x buf %p\n",
				&req->req, req->req.actual,
				req->req.length, req->req.buf);
		}
	}
	/* other gadget->eplist ep */
	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		if (ep->ep.desc) {
			seq_printf(m, "\nFor %s Maxpkt is 0x%x "
					"index is 0x%x\n",
					ep->ep.name, ep_maxpacket(ep),
					ep_index(ep));

			if (list_empty(&ep->queue)) {
				seq_puts(m, "its req queue is empty\n\n");
			} else {
				list_for_each_entry(req, &ep->queue, queue) {
					seq_printf(m,
						"req %p actual 0x%x length "
						"0x%x  buf %p\n",
						&req->req, req->req.actual,
						req->req.length, req->req.buf);
					} /* end for each_entry of ep req */
				}	/* end for else */
			}	/* end for if(ep->queue) */
		}		/* end (ep->desc) */

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*
 * seq_file wrappers for procfile show routines.
 */
static int zynq_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, zynq_proc_read, NULL);
}

static const struct file_operations proc_zynq_fops = {
	.open		= zynq_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#define create_proc_file()	proc_create(proc_filename, \
					0, NULL, &proc_zynq_fops)

#define remove_proc_file()	remove_proc_entry(proc_filename, NULL)

#else				/* !CONFIG_USB_GADGET_DEBUG_FILES */

#define create_proc_file()	do {} while (0)
#define remove_proc_file()	do {} while (0)

#endif				/* CONFIG_USB_GADGET_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

/* Release udc structures */
static void zynq_udc_release(struct device *dev)
{
	complete(udc_controller->done);
	dma_free_coherent(dev->parent, udc_controller->ep_qh_size,
			udc_controller->ep_qh, udc_controller->ep_qh_dma);
	kfree(udc_controller);
}

/******************************************************************
	Internal structure setup functions
*******************************************************************/
/*------------------------------------------------------------------
 * init resource for globle controller
 * Return the udc handle on success or NULL on failure
 ------------------------------------------------------------------*/
static int struct_udc_setup(struct zynq_udc *udc,
		struct platform_device *pdev)
{
	struct zynq_usb2_platform_data *pdata;
	size_t size;

	pdata = pdev->dev.platform_data;
	udc->phy_mode = pdata->phy_mode;

	udc->eps = kzalloc(sizeof(*udc->eps) * udc->max_ep, GFP_KERNEL);
	if (!udc->eps) {
		dev_err(&pdev->dev, "malloc zynq_ep failed\n");
		return -1;
	}

	/* initialized QHs, take care of alignment */
	size = udc->max_ep * sizeof(struct ep_queue_head);
	if (size < QH_ALIGNMENT)
		size = QH_ALIGNMENT;
	else if ((size % QH_ALIGNMENT) != 0) {
		size += QH_ALIGNMENT + 1;
		size &= ~(QH_ALIGNMENT - 1);
	}
	udc->ep_qh = dma_alloc_coherent(&pdev->dev, size,
					&udc->ep_qh_dma, GFP_KERNEL);
	if (!udc->ep_qh) {
		dev_err(&pdev->dev, "malloc QHs for udc failed\n");
		kfree(udc->eps);
		return -1;
	}

	udc->ep_qh_size = size;

	/* Initialize ep0 status request structure */
	/* FIXME: zynq_alloc_request() ignores ep argument */
	udc->status_req = container_of(zynq_alloc_request(NULL, GFP_KERNEL),
			struct zynq_req, req);
	/* allocate a small amount of memory to get valid address */
	udc->status_req->req.buf = kmalloc(8, GFP_KERNEL);

	udc->resume_state = USB_STATE_NOTATTACHED;
	udc->usb_state = USB_STATE_POWERED;
	udc->ep0_dir = 0;
	udc->remote_wakeup = 0;	/* default to 0 on reset */

	return 0;
}

/*----------------------------------------------------------------
 * Setup the zynq_ep struct for eps
 * Link zynq_ep->ep to gadget->ep_list
 * ep0out is not used so do nothing here
 * ep0in should be taken care
 *--------------------------------------------------------------*/
static int struct_ep_setup(struct zynq_udc *udc,
				unsigned char index, char *name, int link)
{
	struct zynq_ep *ep = &udc->eps[index];

	ep->udc = udc;
	strcpy(ep->name, name);
	ep->ep.name = ep->name;

	ep->ep.ops = &zynq_ep_ops;
	ep->stopped = 0;

	/* for ep0: maxP defined in desc
	 * for other eps, maxP is set by epautoconfig() called by gadget layer
	 */
	usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);

	/* the queue lists any req for this ep */
	INIT_LIST_HEAD(&ep->queue);

	/* gagdet.ep_list used for ep_autoconfig so no ep0 */
	if (link)
		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
	ep->gadget = &udc->gadget;
	ep->qh = &udc->ep_qh[index];

	return 0;
}

/* Driver probe function
 * all intialization operations implemented here except enabling usb_intr reg
 * board setup should have been done in the platform code
 */
static int zynq_udc_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	unsigned int i;
	u32 dccparams;
	struct zynq_usb2_platform_data *pdata;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		VDBG("Wrong device");
		return -ENODEV;
	}

	if (strcmp(pdev->name, driver_name)) {
		VDBG("Wrong device");
		return -ENODEV;
	}

	udc_controller = kzalloc(sizeof(*udc_controller), GFP_KERNEL);
	if (udc_controller == NULL) {
		dev_err(&pdev->dev, "malloc udc failed\n");
		return -ENOMEM;
	}

	spin_lock_init(&udc_controller->lock);
	udc_controller->stopped = 1;

	dr_regs = (struct usb_dr_device __iomem *)pdata->regs;
	if (!dr_regs) {
		ret = -ENOMEM;
		goto err_kfree;
	}
#ifdef CONFIG_USB_ZYNQ_PHY
	if (pdata->otg) {
		udc_controller->transceiver = pdata->otg;
		udc_controller->xotg =
			xceiv_to_xotg(udc_controller->transceiver);
	}
#endif
	/* Initialize USB clocks */
	ret = zynq_udc_clk_init(pdev);
	if (ret < 0)
		goto err_kfree;

	/* Read Device Controller Capability Parameters register */
	dccparams = zynq_readl(&dr_regs->dccparams);
	if (!(dccparams & DCCPARAMS_DC)) {
		dev_err(&pdev->dev, "This SOC doesn't support device role\n");
		ret = -ENODEV;
		goto err_iounmap;
	}
	/* Get max device endpoints */
	/* DEN is bidirectional ep number, max_ep doubles the number */
	udc_controller->max_ep = (dccparams & DCCPARAMS_DEN_MASK) * 2;

	udc_controller->irq = pdata->irq;
	if (!udc_controller->irq) {
		ret = -ENODEV;
		goto err_iounmap;
	}

	ret = devm_request_irq(&pdev->dev, udc_controller->irq, zynq_udc_irq,
				IRQF_SHARED, driver_name, udc_controller);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot request irq %d err %d\n",
				udc_controller->irq, ret);
		goto err_iounmap;
	}

	/* Initialize the udc structure including QH member and other member */
	if (struct_udc_setup(udc_controller, pdev)) {
		dev_err(&pdev->dev, "Can't initialize udc data structure\n");
		ret = -ENOMEM;
		goto err_iounmap;
	}

	/* initialize usb hw reg except for regs for EP,
	 * leave usbintr reg untouched */
#ifdef CONFIG_USB_ZYNQ_PHY
	if (!pdata->otg)
		dr_controller_setup(udc_controller);
#else
	dr_controller_setup(udc_controller);
#endif

	/* Setup gadget structure */
	udc_controller->gadget.ops = &zynq_gadget_ops;
	udc_controller->gadget.max_speed = USB_SPEED_HIGH;
	udc_controller->gadget.ep0 = &udc_controller->eps[0].ep;
	INIT_LIST_HEAD(&udc_controller->gadget.ep_list);
	udc_controller->gadget.name = driver_name;
#ifdef CONFIG_USB_ZYNQ_PHY
	udc_controller->gadget.is_otg = (pdata->otg != NULL);
#endif

	/* Setup gadget.dev and register with kernel */
	dev_set_name(&udc_controller->gadget.dev, "gadget");
	udc_controller->gadget.dev.release = zynq_udc_release;
	udc_controller->gadget.dev.parent = &pdev->dev;

	/* setup QH and epctrl for ep0 */
	ep0_setup(udc_controller);

	/* setup udc->eps[] for ep0 */
	struct_ep_setup(udc_controller, 0, "ep0", 0);
	/* for ep0: the desc defined here;
	 * for other eps, gadget layer called ep_enable with defined desc
	 */
	udc_controller->eps[0].ep.desc = &zynq_ep0_desc;
	usb_ep_set_maxpacket_limit(&udc_controller->eps[0].ep,
				   USB_MAX_CTRL_PAYLOAD);

	/* setup the udc->eps[] for non-control endpoints and link
	 * to gadget.ep_list */
	for (i = 1; i < (int)(udc_controller->max_ep / 2); i++) {
		char name[14];

		sprintf(name, "ep%dout", i);
		struct_ep_setup(udc_controller, i * 2, name, 1);
		sprintf(name, "ep%din", i);
		struct_ep_setup(udc_controller, i * 2 + 1, name, 1);
	}

	/* use dma_pool for TD management */
	udc_controller->td_pool = dma_pool_create("udc_td", &pdev->dev,
			sizeof(struct ep_td_struct),
			DTD_ALIGNMENT, UDC_DMA_BOUNDARY);
	if (udc_controller->td_pool == NULL) {
		ret = -ENOMEM;
		goto err_unregister;
	}

	/* TODO: Check if VBUS can be dynamically detected by VBUS session
	 * interrupts using OTGSC register */
	udc_controller->vbus_active = 1;

	ret = usb_add_gadget_udc(&pdev->dev, &udc_controller->gadget);
	if (ret)
		goto err_del_udc;

	create_proc_file();
	return 0;

err_del_udc:
	dma_pool_destroy(udc_controller->td_pool);
err_unregister:
	device_unregister(&udc_controller->gadget.dev);
err_iounmap:
	zynq_udc_clk_release(pdev);
err_kfree:
	kfree(udc_controller);
	udc_controller = NULL;
	return ret;
}

/* Driver removal function
 * Free resources and finish pending transactions
 */
static int __exit zynq_udc_remove(struct platform_device *pdev)
{
	DECLARE_COMPLETION(done);

	if (!udc_controller)
		return -ENODEV;

	usb_del_gadget_udc(&udc_controller->gadget);
	udc_controller->done = &done;

	zynq_udc_clk_release(pdev);

	/* DR has been stopped in usb_gadget_unregister_driver() */
	remove_proc_file();

	/* Free allocated memory */
	kfree(udc_controller->status_req->req.buf);
	kfree(udc_controller->status_req);
	kfree(udc_controller->eps);

	dma_pool_destroy(udc_controller->td_pool);
	free_irq(udc_controller->irq, udc_controller);
	device_unregister(&udc_controller->gadget.dev);
	/* free udc --wait for the release() finished */
	wait_for_completion(&done);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*-----------------------------------------------------------------
 * Modify Power management attributes
 * Used by OTG statemachine to disable gadget temporarily
 -----------------------------------------------------------------*/
static int zynq_udc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_usb2_platform_data *pdata = pdev->dev.platform_data;

	dr_controller_stop(udc_controller);

	clk_disable(pdata->clk);

	return 0;
}

/*-----------------------------------------------------------------
 * Invoked on USB resume. May be called in_interrupt.
 * Here we start the DR controller and enable the irq
 *-----------------------------------------------------------------*/
static int zynq_udc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_usb2_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	ret = clk_enable(pdata->clk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	/* Enable DR irq reg and set controller Run */
	if (udc_controller->stopped) {
		dr_controller_setup(udc_controller);
		dr_controller_run(udc_controller);
	}
	udc_controller->usb_state = USB_STATE_ATTACHED;
	udc_controller->ep0_state = WAIT_FOR_SETUP;
	udc_controller->ep0_dir = 0;
	return 0;
}

static const struct dev_pm_ops zynq_udc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynq_udc_suspend, zynq_udc_resume)
};
#define ZYNQ_UDC_PM	(&zynq_udc_dev_pm_ops)

#else /* ! CONFIG_PM_SLEEP */
#define ZYNQ_UDC_PM	NULL
#endif /* ! CONFIG_PM_SLEEP */

/*-------------------------------------------------------------------------
	Register entry point for the peripheral controller driver
--------------------------------------------------------------------------*/

static struct platform_driver udc_driver = {
	.probe   = zynq_udc_probe,
	.remove  = zynq_udc_remove,
	/* these suspend and resume are not usb suspend and resume */
	.driver  = {
		.name = (char *)driver_name,
		.owner = THIS_MODULE,
		.pm = ZYNQ_UDC_PM,
	},
};

module_platform_driver(udc_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:zynq-udc");
