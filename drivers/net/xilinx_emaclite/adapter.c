/*
 * adapter.c
 *
 * Xilinx Ethernet MAC Lite Adapter component to interface XEmac component 
 * to Linux
 *
 * Author: John Williams <john.williams@petalogix.com>
 *
 * based on Xilinx enet driver which is by 
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.1.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 *
 * (c) Copyright 2007 Xilinx Inc.
 * 
 */

/*
 * This driver is a bit unusual in that it is composed of two logical
 * parts where one part is the OS independent code and the other part is
 * the OS dependent code.  Xilinx provides their drivers split in this
 * fashion.  This file represents the Linux OS dependent part known as
 * the Linux adapter.  The other files in this directory are the OS
 * independent files as provided by Xilinx with no changes made to them.
 * The names exported by those files begin with XEmacLite_.  All functions
 * in this file that are called by Linux have names that begin with
 * xemaclite_.  The functions in this file that have Handler in their name
 * are registered as callbacks with the underlying Xilinx OS independent
 * layer.  Any other functions are static helper functions.
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/mii.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <linux/ethtool.h>

#include <linux/xilinx_devices.h>

#include <xbasic_types.h>
#include "xemaclite.h"
#include "xemaclite_i.h"
#include "xipif_v1_23_b.h"

#ifdef CONFIG_OF
// For open firmware.
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

#define DRIVER_NAME "xilinx_emaclite"
#define DRIVER_VERSION "1.0"

MODULE_AUTHOR("John Williams <john.williams@petalogix.com>");
MODULE_DESCRIPTION("Xilinx Ethernet MAC Lite driver");
MODULE_LICENSE("GPL");

#define TX_TIMEOUT   (60*HZ)	/* Transmission timeout is 60 seconds. */

#define ALIGNMENT         4

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGN(adr) ((ALIGNMENT - ((u32) adr)) % ALIGNMENT)

/* physical to virtual pointer conversion */
#define P_TO_V(InstancePtr, p) \
	((p) ? \
	 ((InstancePtr)->VirtPtr + ((u32)(p) - (u32)(InstancePtr)->PhyPtr)) : \
	 0)

/*
 * Our private per device data.  When a net_device is allocated we will
 * ask for enough extra space for this.
 */
struct net_local {
	struct list_head rcv;
	struct list_head xmit;

	struct net_device_stats stats;	/* Statistics for this device */
	struct net_device *ndev;	/* this device */
	u32 index;		/* Which interface is this */
	XInterruptHandler Isr;	/* Pointer to the XEmac ISR routine */
	u8 mii_addr;		/* The MII address of the PHY */
	/*
	 * The underlying OS independent code needs space as well.  A
	 * pointer to the following XEmacLite structure will be passed to
	 * any XEmacLite_ function that requires it.  However, we treat the
	 * data as an opaque object in this file (meaning that we never
	 * reference any of the fields inside of the structure).
	 */
	XEmacLite EmacLite;

	void *desc_space;
	dma_addr_t desc_space_handle;
	int desc_space_size;

	u8 *ddrVirtPtr;
	u32 ddrOffset;
	u32 ddrSize;

	struct sk_buff *deferred_skb;
};

/* for exclusion of all program flows (processes, ISRs and BHs) possible to share data with current one */
static spinlock_t reset_lock = SPIN_LOCK_UNLOCKED;

/* Helper function to determine if a given XEmac error warrants a reset. */
extern inline int status_requires_reset(int s)
{
	return (s == XST_DMA_ERROR || s == XST_FIFO_ERROR ||
		s == XST_RESET_ERROR || s == XST_DMA_SG_NO_LIST ||
		s == XST_DMA_SG_LIST_EMPTY);
}

/* BH statics */
static spinlock_t rcvSpin = SPIN_LOCK_UNLOCKED;
static spinlock_t xmitSpin = SPIN_LOCK_UNLOCKED;

/*
 * The following are notes regarding the critical sections in this
 * driver and how they are protected.
 *
 *
 * XEmacLite_EnableInterrupts, XEmacLite_DisableInterrupts and XEmacLite_SetOptions are not thread safe.
 * These functions are called from xemaclite_open(), xemaclite_close(), reset(),
 * and xemaclite_set_multicast_list().  xemaclite_open() and xemaclite_close()
 * should be safe because when they do start and stop, they don't have
 * interrupts or timers enabled.  The other side is that they won't be
 * called while a timer or interrupt is being handled.
 *
 * XEmacLite_PhyRead and XEmacLite_PhyWrite are not thread safe.
 * These functions are called from get_phy_status(), xemaclite_ioctl() and
 * probe().  probe() is only called from xemaclite_init() so it is not an
 * issue (nothing is really up and running yet).  get_phy_status() is
 * called from both poll_mii() (a timer bottom half) and xemaclite_open().
 * These shouldn't interfere with each other because xemaclite_open() is
 * what starts the poll_mii() timer.  xemaclite_open() and xemaclite_ioctl()
 * should be safe as well because they will be sequential.  That leaves
 * the interaction between poll_mii() and xemaclite_ioctl().  While the
 * timer bottom half is executing, a new ioctl won't come in so that is
 * taken care of.  That leaves the one case of the poll_mii timer
 * popping while handling an ioctl.  To take care of that case, the
 * timer is deleted when the ioctl comes in and then added back in after
 * the ioctl is finished.
 */

typedef enum DUPLEX { UNKNOWN_DUPLEX, HALF_DUPLEX, FULL_DUPLEX } DUPLEX;
static void reset(struct net_device *dev, DUPLEX duplex)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	/* Shouldn't really be necessary, but shouldn't hurt. */
	netif_stop_queue(dev);

	XEmacLite_DisableInterrupts(&lp->EmacLite);
	XEmacLite_EnableInterrupts(&lp->EmacLite);

	if (lp->deferred_skb) {
		dev_kfree_skb(lp->deferred_skb);
		lp->deferred_skb = NULL;
		lp->stats.tx_errors++;
	}

	dev->trans_start = 0xffffffff - TX_TIMEOUT - TX_TIMEOUT;	/* to exclude tx timeout */

	/* We're all ready to go.  Start the queue in case it was stopped. */
	netif_wake_queue(dev);
}

/*
 * This routine is registered with the OS as the function to call when
 * the EMAC interrupts.  It in turn, calls the Xilinx OS independent
 */
static irqreturn_t
xemaclite_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;

	/* Call it. */
	(*(lp->Isr)) (&lp->EmacLite);

	return IRQ_HANDLED;
}

static int xemaclite_open(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int retval;

	/*
	 * Just to be safe, stop the device first.  
	 */
	XEmacLite_DisableInterrupts(&lp->EmacLite);

	/* Set the MAC address each time opened. */
	XEmacLite_SetMacAddress(&lp->EmacLite, dev->dev_addr);

	/* Grab the IRQ */
	retval = request_irq(dev->irq, &xemaclite_interrupt, 0, dev->name, dev);
	if (retval) {
		printk(KERN_ERR
		       "%s: Could not allocate interrupt %d.\n",
		       dev->name, dev->irq);
		return retval;
	}

	INIT_LIST_HEAD(&(lp->rcv));
	INIT_LIST_HEAD(&(lp->xmit));

	if (XEmacLite_EnableInterrupts(&lp->EmacLite) != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not start device.\n", dev->name);
		free_irq(dev->irq, dev);
		return -EBUSY;
	}

	/* We're ready to go. */
	netif_start_queue(dev);

	return 0;
}
static int xemaclite_close(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	netif_stop_queue(dev);
	XEmacLite_DisableInterrupts(&lp->EmacLite);

	free_irq(dev->irq, dev);

	spin_lock_irqsave(&rcvSpin, flags);
	list_del(&(lp->rcv));
	spin_unlock_irqrestore(&rcvSpin, flags);
	spin_lock_irqsave(&xmitSpin, flags);
	list_del(&(lp->xmit));
	spin_unlock_irqrestore(&xmitSpin, flags);

	return 0;
}
static struct net_device_stats *xemaclite_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	return &lp->stats;
}

static int xemaclite_Send(struct sk_buff *orig_skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct sk_buff *new_skb;
	unsigned int len;
	unsigned long flags;

	len = orig_skb->len;

	new_skb = orig_skb;

	spin_lock_irqsave(&reset_lock, flags);
	if (XEmacLite_Send(&lp->EmacLite, (u8 *) new_skb->data, len) !=
	    XST_SUCCESS) {
		netif_stop_queue(dev);
		lp->deferred_skb = new_skb;
		spin_unlock_irqrestore(&reset_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&reset_lock, flags);

	lp->stats.tx_bytes += len;
	dev_kfree_skb(new_skb);
	dev->trans_start = jiffies;

	return 0;
}

/* The callback function for completed frames sent. */
static void SendHandler(void *CallbackRef)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	struct net_local *lp = (struct net_local *) dev->priv;

	if (lp->deferred_skb) {
		if (XEmacLite_Send
		    (&lp->EmacLite, (u8 *) lp->deferred_skb->data,
		     lp->deferred_skb->len) != XST_SUCCESS) {
			return;
		}
		else {
			dev_kfree_skb(lp->deferred_skb);
			lp->deferred_skb = NULL;
			netif_wake_queue(dev);
		}
	}
	lp->stats.tx_packets++;
}

static void xemaclite_tx_timeout(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	printk("%s: Exceeded transmit timeout of %lu ms.\n",
	       dev->name, TX_TIMEOUT * 1000UL / HZ);

	lp->stats.tx_errors++;
	spin_lock_irqsave(&reset_lock, flags);
	reset(dev, UNKNOWN_DUPLEX);
	spin_unlock_irqrestore(&reset_lock, flags);
}

/* The callback function for frames received. */
static void RecvHandler(void *CallbackRef)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	struct net_local *lp = (struct net_local *) dev->priv;
	struct sk_buff *skb;
	unsigned int align;
	u32 len;

	len = XEL_MAX_FRAME_SIZE;
	if (!(skb = /*dev_ */ alloc_skb(len + ALIGNMENT, GFP_ATOMIC))) {
		/* Couldn't get memory. */
		lp->stats.rx_dropped++;
		printk(KERN_ERR "%s: Could not allocate receive buffer.\n",
		       dev->name);
		return;
	}

	/*
	 * A new skb should have the data halfword aligned, but this code is
	 * here just in case that isn't true...  Calculate how many
	 * bytes we should reserve to get the data to start on a word
	 * boundary.  */
	align = BUFFER_ALIGN(skb->data);
	if (align)
		skb_reserve(skb, align);

	skb_reserve(skb, 2);

	len = XEmacLite_Recv(&lp->EmacLite, (u8 *) skb->data);

	if (!len) {

		lp->stats.rx_errors++;
		dev_kfree_skb(skb);
		//printk(KERN_ERR "%s: Could not receive buffer\n",dev->name);
		spin_lock(&reset_lock);
		//reset(dev, UNKNOWN_DUPLEX);
		spin_unlock(&reset_lock);

		return;
	}

	skb_put(skb, len);	/* Tell the skb how much data we got. */
	skb->dev = dev;		/* Fill out required meta-data. */


	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;

	lp->stats.rx_packets++;
	lp->stats.rx_bytes += len;

	netif_rx(skb);		/* Send the packet upstream. */
}

static int xemaclite_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct hw_addr_data *hw_addr = (struct sockaddr *) &rq->ifr_hwaddr;

	switch (cmd) {
	case SIOCETHTOOL:
		return -EIO;

	case SIOCSIFHWADDR:
		{
			printk(KERN_INFO "%s: SIOCSIFHWADDR\n", dev->name);

			/* Copy MAC address in from user space */
			copy_from_user(dev->dev_addr, (void *) hw_addr,
				       IFHWADDRLEN);
			XEmacLite_SetMacAddress(&lp->EmacLite, dev->dev_addr);
			break;
		}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void xemaclite_remove_ndev(struct net_device *ndev)
{
	if (ndev) {
		struct net_local *lp = netdev_priv(ndev);

		iounmap((void *) (lp->EmacLite.BaseAddress));
		free_netdev(ndev);
	}
}

static int xemaclite_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	unregister_netdev(ndev);
	xemaclite_remove_ndev(ndev);

        release_mem_region(ndev->mem_start, ndev->mem_end-ndev->mem_start+1);

	free_netdev(ndev);

	dev_set_drvdata(dev, NULL);

	return 0;		/* success */
}


/** Shared device initialization code */
static int xemaclite_setup(
		struct device *dev,
		struct resource *r_mem,
		struct resource *r_irq,
		struct xemaclite_platform_data *pdata) {

	u32 virt_baddr;		/* virtual base address of emac */

	XEmacLite_Config Config;

	struct net_device *ndev = NULL;
	struct net_local *lp = NULL;

	int rc = 0;

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev) {
		dev_err(dev, "XEmacLite: Could not allocate net device.\n");
		rc = -ENOMEM;
		goto error;
	}
	dev_set_drvdata(dev, ndev);

	ndev->irq = r_irq->start;
        ndev->mem_start = r_mem->start;
        ndev->mem_end = r_mem->end;

	if (!request_mem_region(ndev->mem_start,ndev->mem_end - ndev->mem_start+1, DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)ndev->mem_start);
		rc = -EBUSY;
		goto error;
	}

	/* Initialize the private netdev structure
	 */
	lp = netdev_priv(ndev);
	lp->ndev = ndev;

	/* Setup the Config structure for the XEmacLite_CfgInitialize() call. */
	Config.BaseAddress	= r_mem->start;	/* Physical address */
	Config.TxPingPong	= pdata->tx_ping_pong;
	Config.RxPingPong	= pdata->rx_ping_pong;


	/* Get the virtual base address for the device */
	virt_baddr = (u32) ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (0 == virt_baddr) {
		dev_err(dev, "XEmacLite: Could not allocate iomem.\n");
		rc = -EIO;
		goto error;
	}


	if (XEmacLite_CfgInitialize(&lp->EmacLite, &Config, virt_baddr) != XST_SUCCESS) {
		dev_err(dev, "XEmacLite: Could not initialize device.\n");
		rc = -ENODEV;
		goto error;
	}

	/* Set the MAC address */
	memcpy(ndev->dev_addr, pdata->mac_addr, 6);
        
        /* Note: in the xemac driver, SetMacAddress returns a success code. */
        XEmacLite_SetMacAddress(&lp->EmacLite, ndev->dev_addr);

	dev_info(dev,
			"MAC address is now %2x:%2x:%2x:%2x:%2x:%2x\n",
			pdata->mac_addr[0], pdata->mac_addr[1],
			pdata->mac_addr[2], pdata->mac_addr[3],
			pdata->mac_addr[4], pdata->mac_addr[5]);

	dev_err(dev, "using fifo mode.\n");
	XEmacLite_SetRecvHandler(&lp->EmacLite, ndev, RecvHandler);
	XEmacLite_SetSendHandler(&lp->EmacLite, ndev, SendHandler);
	ndev->hard_start_xmit = xemaclite_Send;
	lp->Isr = XEmacLite_InterruptHandler;

	lp->mii_addr = 0;
	dev_warn(dev, 
	       "No PHY detected.  Assuming a PHY at address %d.\n",
                lp->mii_addr);

	ndev->open = xemaclite_open;
	ndev->stop = xemaclite_close;
	ndev->get_stats = xemaclite_get_stats;
	ndev->flags &= ~IFF_MULTICAST;
	ndev->do_ioctl = xemaclite_ioctl;
	ndev->tx_timeout = xemaclite_tx_timeout;
	ndev->watchdog_timeo = TX_TIMEOUT;

	/* Finally, register the device.
	 */
	rc = register_netdev(ndev);
	if (rc) {
		printk(KERN_ERR
		       "%s: Cannot register net device, aborting.\n",
		       ndev->name);
		goto error;	/* rc is already set here... */
	}

	dev_info(dev,
	       "Xilinx EMACLite at 0x%08X mapped to 0x%08X, irq=%d\n",
	       lp->EmacLite.PhysAddress,
	       lp->EmacLite.BaseAddress, ndev->irq);
	return 0;
 error:
	if (ndev) {
		xemaclite_remove_ndev(ndev);
	}
	return rc;
}

static int xemaclite_probe(struct device *dev)
{
	struct resource *r_irq = NULL;	/* Interrupt resources */
	struct resource *r_mem = NULL;	/* IO mem resources */
	struct xemaclite_platform_data *pdata;
	struct platform_device *pdev = to_platform_device(dev);

	/* param check */
	if (!pdev) {
		printk(KERN_ERR
		       "XEmac: Internal error. Probe called with NULL param.\n");
		return -ENODEV;
	}

	pdata = (struct xemaclite_platform_data *) pdev->dev.platform_data;
	if (!pdata) {
		printk(KERN_ERR "XEmac %d: Couldn't find platform data.\n",
		       pdev->id);

		return -ENODEV;
	}

	/* Get iospace and an irq for the device */
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_irq || !r_mem) {
		printk(KERN_ERR "XEmac %d: IO resource(s) not found.\n",
		       pdev->id);
		return -ENODEV;
	}

        return xemaclite_setup(dev, r_mem, r_irq, pdata);
}

static struct device_driver xemaclite_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,

	.probe = xemaclite_probe,
	.remove = xemaclite_remove
};

#ifdef CONFIG_OF
static u32 get_u32(struct of_device *ofdev, const char *s) {
	u32 *p = (u32 *)of_get_property(ofdev->node, s, NULL);
	if(p) {
		return *p;
	} else {
		dev_warn(&ofdev->dev, "Parameter %s not found, defaulting to false.\n", s);
		return FALSE;
	}
}

static bool get_bool(struct of_device *ofdev, const char *s) {
	u32 *p = (u32 *)of_get_property(ofdev->node, s, NULL);
	if(p) {
		return (bool)*p;
	} else {
		dev_warn(&ofdev->dev, "Parameter %s not found, defaulting to false.\n", s);
		return FALSE;
	}
}

static int __devinit xemaclite_of_probe(struct of_device *ofdev, const struct of_device_id *match)
{
	struct xemaclite_platform_data pdata_struct;
	struct resource r_irq_struct;
	struct resource r_mem_struct;

	struct resource *r_irq = &r_irq_struct;	/* Interrupt resources */
	struct resource *r_mem = &r_mem_struct;	/* IO mem resources */
	struct xemaclite_platform_data *pdata = &pdata_struct;
	int rc = 0;

	dev_info(&ofdev->dev, "Device Tree Probing \'%s\'\n",
                        ofdev->node->name);

	/* Get iospace for the device */
	rc = of_address_to_resource(ofdev->node, 0, r_mem);
	if(rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		return rc;
	}

	/* Get IRQ for the device */
	rc = of_irq_to_resource(ofdev->node, 0, r_irq);
	if(rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found.\n");
		return rc;
	}

	pdata_struct.tx_ping_pong	= get_bool(ofdev, "C_TX_PING_PONG");
	pdata_struct.rx_ping_pong	= get_bool(ofdev, "C_RX_PING_PONG");
	memcpy(pdata_struct.mac_addr, of_get_mac_address(ofdev->node), 6);

        return xemaclite_setup(&ofdev->dev, r_mem, r_irq, pdata);
}

static int __devexit xemaclite_of_remove(struct of_device *dev)
{
	return xemaclite_remove(&dev->dev);
}

static struct of_device_id xemaclite_of_match[] = {
	{ .compatible = "opb_ethernetlite", },
	{ .compatible = "xps_ethernetlite", },
	{ .compatible = "emaclite", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xemaclite_of_match);

static struct of_platform_driver xemaclite_of_driver = {
	.name		= DRIVER_NAME,
	.match_table	= xemaclite_of_match,
	.probe		= xemaclite_of_probe,
	.remove		= __devexit_p(xemaclite_of_remove),
};
#endif

static int __init xemaclite_init(void)
{
	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	int status = driver_register(&xemaclite_driver);
#ifdef CONFIG_OF
	status |= of_register_platform_driver(&xemaclite_of_driver);
#endif
	return status;
}

static void __exit xemaclite_cleanup(void)
{
	driver_unregister(&xemaclite_driver);
#ifdef CONFIG_OF
	of_unregister_platform_driver(&xemaclite_of_driver);
#endif
}

module_init(xemaclite_init);
module_exit(xemaclite_cleanup);

