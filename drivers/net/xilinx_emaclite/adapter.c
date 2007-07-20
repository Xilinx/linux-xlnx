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

#include <xbasic_types.h>
#include "asm/xparameters.h"
#include "xemaclite.h"
#include "xemaclite_i.h"
#include "xipif_v1_23_b.h"

#define DRIVER_NAME "Xilinx Eth MACLite driver"
#define DRIVER_VERSION "1.0"

MODULE_AUTHOR("John Williams <john.williams@petalogix.com>");
MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");

/* FIXME hardcoded MAC addresses */
#define XPAR_ETHERNETLITE_0_MACADDR {0x00, 0x00, 0xC0, 0xA3, 0xE5, 0x44}
#define XPAR_ETHERNETLITE_1_MACADDR {0x00, 0x00, 0xC0, 0xA3, 0xE5, 0x45}
#define XPAR_ETHERNETLITE_2_MACADDR {0x00, 0x00, 0xC0, 0xA3, 0xE5, 0x46}

#define TX_TIMEOUT   (60*HZ)	/* Transmission timeout is 60 seconds. */

struct ethernet_desc {
	unsigned baseaddr;
	unsigned irq;
	unsigned char macaddr[6];
};

static struct ethernet_desc ether_table[] = {
#ifdef XPAR_XEMACLITE_NUM_INSTANCES
	{XPAR_ETHERNETLITE_0_BASEADDR, 
		XPAR_ETHERNETLITE_0_IRQ,
		XPAR_ETHERNETLITE_0_MACADDR },
#endif
#ifdef CONFIG_XILINX_ETHERNETLITE_1_INSTANCE
	{XPAR_ETHERNETLITE_1_BASEADDR, 
		XPAR_ETHERNETLITE_1_IRQ,
		XPAR_ETHERNETLITE_1_MACADDR },
#endif
#ifdef CONFIG_XILINX_ETHERNETLITE_2_INSTANCE
	{XPAR_ETHERNETLITE_2_BASEADDR, 
		XPAR_ETHERNETLITE_2_IRQ,
		XPAR_ETHERNETLITE_2_MACADDR },
#endif
};

static int num_ether_devices = sizeof(ether_table)/sizeof(struct ethernet_desc);

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
	struct net_device *next_dev;	/* The next device in dev_list */
	struct net_device *dev;		/* this device */
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

	struct sk_buff* deferred_skb;
};

/* List of devices we're handling and a lock to give us atomic access. */
static struct net_device *dev_list = NULL;
static spinlock_t dev_lock = SPIN_LOCK_UNLOCKED;

/* for exclusion of all program flows (processes, ISRs and BHs) possible to share data with current one */
static spinlock_t reset_lock = SPIN_LOCK_UNLOCKED;

/* Helper function to determine if a given XEmac error warrants a reset. */
extern inline int
status_requires_reset(XStatus s)
{
	return (s == XST_DMA_ERROR || s == XST_FIFO_ERROR ||
		s == XST_RESET_ERROR || s == XST_DMA_SG_NO_LIST ||
		s == XST_DMA_SG_LIST_EMPTY);
}

/* BH statics */
static spinlock_t rcvSpin = SPIN_LOCK_UNLOCKED;
static spinlock_t xmitSpin = SPIN_LOCK_UNLOCKED;

/* SAATODO: This function will be moved into the Xilinx code. */
/*****************************************************************************/
/**
*
* Lookup the device configuration based on the emac instance.  The table
* EmacConfigTable contains the configuration info for each device in the system.
*
* @param Instance is the index of the emac being looked up.
*
* @return
*
* A pointer to the configuration table entry corresponding to the given
* device ID, or NULL if no match is found.
*
* @note
*
* None.
*
******************************************************************************/
XEmacLite_Config *
XEmacLite_GetConfig(int Instance)
{
	if (Instance < 0 || Instance >= XPAR_XEMACLITE_NUM_INSTANCES) {
		return NULL;
	}

	return &XEmacLite_ConfigTable[Instance];
}

/*
 * The following are notes regarding the critical sections in this
 * driver and how they are protected.
 *
 * dev_list
 * There is a spinlock protecting the device list.  It isn't really
 * necessary yet because the list is only manipulated at init and
 * cleanup, but it's there because it is basically free and if we start
 * doing hot add and removal of ethernet devices when the FPGA is
 * reprogrammed while the system is up, we'll need to protect the list.
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
static void
reset(struct net_device *dev, DUPLEX duplex)
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
xemaclite_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;

	/* Call it. */
	(*(lp->Isr)) (&lp->EmacLite);

	return IRQ_HANDLED;
}

static int
xemaclite_open(struct net_device *dev)
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
static int
xemaclite_close(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	netif_stop_queue(dev);
	XEmacLite_DisableInterrupts(&lp->EmacLite);

	free_irq(dev->irq, dev);

	spin_lock_irqsave(rcvSpin, flags);
	list_del(&(lp->rcv));
	spin_unlock_irqrestore(rcvSpin, flags);
	spin_lock_irqsave(xmitSpin, flags);
	list_del(&(lp->xmit));
	spin_unlock_irqrestore(xmitSpin, flags);

	return 0;
}
static struct net_device_stats *
xemaclite_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	return &lp->stats;
}

static int
xemaclite_Send(struct sk_buff *orig_skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct sk_buff *new_skb;
	unsigned int len;
	unsigned long flags;

	len = orig_skb->len;

        new_skb = orig_skb;

        spin_lock_irqsave(reset_lock, flags);
	if (XEmacLite_Send(&lp->EmacLite, (u8 *) new_skb->data, len) != XST_SUCCESS) {
		netif_stop_queue(dev);
		lp->deferred_skb = new_skb;
		spin_unlock_irqrestore(reset_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(reset_lock, flags);

	lp->stats.tx_bytes += len;
        dev_kfree_skb(new_skb);
	dev->trans_start = jiffies;

	return 0;
}

/* The callback function for completed frames sent. */
static void
SendHandler(void *CallbackRef)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	struct net_local *lp = (struct net_local *) dev->priv;

	if (lp->deferred_skb) {
		if (XEmacLite_Send(&lp->EmacLite, (u8 *) lp->deferred_skb->data, lp->deferred_skb->len) != XST_SUCCESS) {
			return;
		} else {
			dev_kfree_skb(lp->deferred_skb);
			lp->deferred_skb = NULL;
			netif_wake_queue(dev);
		}
	}
	lp->stats.tx_packets++;
}

static void
xemaclite_tx_timeout(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	printk("%s: Exceeded transmit timeout of %lu ms.\n",
	       dev->name, TX_TIMEOUT * 1000UL / HZ);

	lp->stats.tx_errors++;
	spin_lock_irqsave(reset_lock, flags);
	reset(dev, UNKNOWN_DUPLEX);
	spin_unlock_irqrestore(reset_lock, flags);
}

/* The callback function for frames received. */
static void
RecvHandler(void *CallbackRef)
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

	skb_reserve(skb,2);

	len = XEmacLite_Recv(&lp->EmacLite, (u8 *) skb->data);

	if (!len) {

		lp->stats.rx_errors++;
		dev_kfree_skb(skb);
		//printk(KERN_ERR "%s: Could not receive buffer\n",dev->name);
		spin_lock(reset_lock);
		//reset(dev, UNKNOWN_DUPLEX);
		spin_unlock(reset_lock);

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

/* Take kernel cmdline option macaddr=... and set MAC address */
static int __init xilinx_emac_hw_addr_setup(char *addrs)
{
	unsigned int hw_addr[6];
	int count;
	static int interface=0;

	/* Scan the kernel param for HW MAC address */
	count=sscanf(addrs, "%2x:%2x:%2x:%2x:%2x:%2x",hw_addr+0, hw_addr+1,
						 hw_addr+2, hw_addr+3,
						 hw_addr+4, hw_addr+5);
	/* Did we get 6 hex digits? */
	if(count!=6)
		return 0;

	for(count=0;count<6;count++)
	{
		ether_table[interface].macaddr[count] = hw_addr[count] & 0xFF;
	}

	/* Increase interface number, for next time */
	interface++;
        return 1;
}


static int
xemaclite_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct hw_addr_data *hw_addr= (struct sockaddr *) &rq->ifr_hwaddr;

	switch (cmd) {
	case SIOCETHTOOL:
		return -EIO;

        case SIOCSIFHWADDR:
            {
            	printk(KERN_INFO "%s: SIOCSIFHWADDR\n", dev->name);

		/* Copy MAC address in from user space*/
        	copy_from_user(dev->dev_addr, (void *)hw_addr, IFHWADDRLEN);
		XEmacLite_SetMacAddress(&lp->EmacLite, dev->dev_addr);
	    	break;
	    }
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void
remove_head_dev(void)
{
	struct net_local *lp;
	struct net_device *dev;
	XEmacLite_Config *cfg;

	/* Pull the head off of dev_list. */
	spin_lock(&dev_lock);
	dev = dev_list;
	lp = (struct net_local *) dev->priv;
	dev_list = lp->next_dev;
	spin_unlock(&dev_lock);

	/* Put the physical address back */
	cfg = XEmacLite_GetConfig(lp->index);
	iounmap((void *) cfg->BaseAddress);
	cfg->BaseAddress = cfg->PhysAddress;

        if (lp->ddrVirtPtr) {
	 	kfree (lp->ddrVirtPtr);
	}

	unregister_netdev(dev);
	kfree(dev);
}

static int __init
probe(int index)
{
	static const unsigned long remap_size
	    = XPAR_ETHERNETLITE_0_HIGHADDR - 
		  XPAR_ETHERNETLITE_0_BASEADDR + 1;
	struct net_device *dev;
	struct net_local *lp;
	XEmacLite_Config *cfg;
	unsigned int irq;
	int err;
	u32 maddr;

	if(index>=num_ether_devices)
		return -ENODEV;
	else
		irq=ether_table[index].irq;

	/* Find the config for our device. */
	cfg = XEmacLite_GetConfig(index);
	if (!cfg)
		return -ENODEV;

    dev = alloc_etherdev(sizeof (struct net_local));
	if (!dev) {
		printk(KERN_ERR "Could not allocate Xilinx enet device %d.\n",
		       index);
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);

	ether_setup(dev);
	dev->irq = irq;

	/* Initialize our private data. */
	lp = (struct net_local *) dev->priv;
	memset(lp, 0, sizeof (struct net_local));
	lp->index = index;
	lp->dev = dev;

	/* Make it the head of dev_list. */
	spin_lock(&dev_lock);
	lp->next_dev = dev_list;
	dev_list = dev;
	spin_unlock(&dev_lock);

	/* Change the addresses to be virtual */
	cfg->PhysAddress = ether_table[index].baseaddr;
	cfg->BaseAddress = (u32) ioremap(cfg->PhysAddress, remap_size);

	if (XEmacLite_Initialize(&lp->EmacLite, cfg->DeviceId) != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not initialize device.\n",
		       dev->name);
		remove_head_dev();
		return -ENODEV;
	}

	/* Copy MAC address in from descriptor table */
	memcpy(dev->dev_addr, ether_table[index].macaddr, IFHWADDRLEN);
	XEmacLite_SetMacAddress(&lp->EmacLite, ether_table[index].macaddr);

    err = register_netdev(dev);
    if (err) {
    	remove_head_dev();
		return err;
    }


	printk(KERN_ERR "%s: using fifo mode.\n", dev->name);
	XEmacLite_SetRecvHandler(&lp->EmacLite, dev, RecvHandler);
	XEmacLite_SetSendHandler(&lp->EmacLite, dev, SendHandler);
	dev->hard_start_xmit = xemaclite_Send;
	lp->Isr = XEmacLite_InterruptHandler;
	
	lp->mii_addr = 0;
	printk(KERN_WARNING
	       "%s: No PHY detected.  Assuming a PHY at address %d.\n",
	       dev->name, lp->mii_addr);

	dev->open = xemaclite_open;
	dev->stop = xemaclite_close;
	dev->get_stats = xemaclite_get_stats;
	dev->flags &= ~IFF_MULTICAST;
	dev->do_ioctl = xemaclite_ioctl;
	dev->tx_timeout = xemaclite_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	printk(KERN_INFO
	       "%s: Xilinx EMACLite #%d at 0x%08X mapped to 0x%08X, irq=%d\n",
	       dev->name, index, cfg->PhysAddress, 
		ether_table[index].baseaddr, dev->irq); 
	return 0;
}

static int __init
xemaclite_init(void)
{
	int index = 0;

	while (probe(index++) == 0) ;
	/* If we found at least one, report success. */
	return (index > 1) ? 0 : -ENODEV;
}

static void __exit
xemaclite_cleanup(void)
{
	while (dev_list)
		remove_head_dev();
}

module_init(xemaclite_init);
module_exit(xemaclite_cleanup);

__setup("macaddr=", xilinx_emac_hw_addr_setup);

