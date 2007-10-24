/*
 * emac_linux.c
 *
 * Xilinx Ethernet Adapter component to interface XEmac component to Linux
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.1.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.01a ecm  12/19/04 Added TX and RX DRE and Checksum offload functionality.
 *                     This adapter now only works with v1.01.a of the XEmac
 *                     driver. Also enables stripping of the PAD and FCS and
 *                     The adpater requires this feature to function properly
 *
 * 1.01a rpm  06/09/06 Fixed the FCS subtraction above for bridging by using
 *                     skb_put when setting the skb length instead of directly
 *                     assigning the skb->len field. Directly assigning it
 *                     broke IP forwarding.
 * 1.01a wgr  09/14/06 Ported to Linux 2.6
 * 1.01a xd   10/13/06 Add support to change MTU on the fly using tools like
 *                     ifconfig
 * 1.01a wgr  10/13/06 Added workaround for PHY detection
 * 1.11a wgr  03/22/07 Converted to new coding style.
 */

/*
 * This driver is a bit unusual in that it is composed of two logical
 * parts where one part is the OS independent code and the other part is
 * the OS dependent code.  Xilinx provides their drivers split in this
 * fashion.  This file represents the Linux OS dependent part known as
 * the Linux adapter.  The other files in this directory are the OS
 * independent files as provided by Xilinx with no changes made to them.
 * The names exported by those files begin with XEmac_.  All functions
 * in this file that are called by Linux have names that begin with
 * xenet_.  The functions in this file that have Handler in their name
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
#include <linux/string.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/checksum.h>
#include <linux/ethtool.h>

#include <linux/xilinx_devices.h>

#include <xbasic_types.h>
#include "xemac.h"
#include "xemac_i.h"
#include "xipif_v1_23_b.h"

#ifdef CONFIG_OF
// For open firmware.
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

/*
 * Add a delay (in ms) after resetting the EMAC since it
 * also resets the PHY - which needs a delay before using it. - RPM
 */
#define RESET_DELAY 1500

#ifdef RESET_DELAY
#   include <linux/delay.h>
#endif

#undef XEM_DFT_SEND_DESC
#define XEM_DFT_SEND_DESC       256
#define DFT_LOCAL_SEND_DESC     64

#undef XEM_DFT_RECV_DESC
#define XEM_DFT_RECV_DESC       256

#define DRIVER_NAME "xilinx_emac"
#define DRIVER_VERSION "1.0"

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx Ethernet MAC driver");
MODULE_LICENSE("GPL");

#define TX_TIMEOUT   (60*HZ)	/* Transmission timeout is 60 seconds. */

/* On the OPB, the 10/100 EMAC requires data to be aligned to 4 bytes.
 * On the PLB, the 10/100 EMAC requires data to be aligned to 8 bytes.
 * For simplicity, we always align to 8 bytes.
 */
#define ALIGNMENT           32

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGN(adr) ((ALIGNMENT - ((u32) adr)) % ALIGNMENT)

#define ENET_HDR_SIZ        14
#define ETHERTYPE_IP        0x0800	/* IP protocol */

#define RX_PSEUDO_HEADER_DATA_START     12
#define RX_PSEUDO_HEADER_DATA_END       18

/* physical to virtual pointer conversion */
#define P_TO_V(InstancePtr, p) \
    ((p) ? \
     ((InstancePtr)->VirtPtr + ((u32)(p) - (u32)(InstancePtr)->PhyPtr)) : \
     0)

int bh_entry = 0;

/*
 * Our private per device data.  When a net_device is allocated we will
 * ask for enough extra space for this.
 */
struct net_local {
	struct list_head rcv;
	XBufDescriptor *rcvBdPtr;
	int rcvBds;
	struct list_head xmit;
	XBufDescriptor *xmitBdPtr;
	int xmitBds;

	struct net_device_stats stats;	/* Statistics for this device */
	struct net_device *ndev;	/* this device */
	struct timer_list phy_timer;	/* PHY monitoring timer */
	XInterruptHandler Isr;	/* Pointer to the XEmac ISR routine */
	u8 mii_addr;		/* The MII address of the PHY */
	/*
	 * The underlying OS independent code needs space as well.  A
	 * pointer to the following XEmac structure will be passed to
	 * any XEmac_ function that requires it.  However, we treat the
	 * data as an opaque object in this file (meaning that we never
	 * reference any of the fields inside of the structure).
	 */
	XEmac Emac;

	void *desc_space;		/* Virtual address */
	dma_addr_t desc_space_handle;	/* Physical address */
	int desc_space_size;

	u8 *ddrVirtPtr;
	u32 ddrOffset;
	u32 ddrSize;

	struct sk_buff *deferred_skb;

	atomic_t availSendBds;
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
static LIST_HEAD(receivedQueue);
static spinlock_t rcvSpin = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(sentQueue);
static spinlock_t xmitSpin = SPIN_LOCK_UNLOCKED;

/*
 * The following are notes regarding the critical sections in this
 * driver and how they are protected.
 *
 *
 * XEmac_Start, XEmac_Stop and XEmac_SetOptions are not thread safe.
 * These functions are called from xenet_open(), xenet_close(), reset(),
 * and xenet_set_multicast_list().  xenet_open() and xenet_close()
 * should be safe because when they do start and stop, they don't have
 * interrupts or timers enabled.  The other side is that they won't be
 * called while a timer or interrupt is being handled.
 *
 * XEmac_PhyRead and XEmac_PhyWrite are not thread safe.
 * These functions are called from get_phy_status(), xenet_ioctl() and
 * xenet_probe().  xenet_probe() is only called from xenet_init() so it is not
 * an issue (nothing is really up and running yet).  get_phy_status() is called
 * from both poll_mii() (a timer bottom half) and xenet_open().  These
 * shouldn't interfere with each other because xenet_open() is what starts the
 * poll_mii() timer.  xenet_open() and xenet_ioctl() should be safe as well
 * because they will be sequential.  That leaves the interaction between
 * poll_mii() and xenet_ioctl().  While the timer bottom half is executing, a
 * new ioctl won't come in so that is taken care of.  That leaves the one case
 * of the poll_mii timer popping while handling an ioctl.  To take care of that
 * case, the timer is deleted when the ioctl comes in and then added back in
 * after the ioctl is finished.
 */

typedef enum DUPLEX { UNKNOWN_DUPLEX, HALF_DUPLEX, FULL_DUPLEX } DUPLEX;
static void reset(struct net_device *dev, DUPLEX duplex)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 Options;
	u8 IfgPart1;
	u8 IfgPart2;
	u8 SendThreshold;
	u32 SendWaitBound;
	u8 RecvThreshold;
	u32 RecvWaitBound;
	int dma_works;

	/* Shouldn't really be necessary, but shouldn't hurt. */
	netif_stop_queue(dev);

	/*
	 * XEmac_Reset puts the device back to the default state.  We need
	 * to save all the settings we don't already know, reset, restore
	 * the settings, and then restart the emac.
	 */
	XEmac_GetInterframeGap(&lp->Emac, &IfgPart1, &IfgPart2);
	Options = XEmac_GetOptions(&lp->Emac);
	switch (duplex) {
	case HALF_DUPLEX:
		Options &= ~XEM_FDUPLEX_OPTION;
		break;
	case FULL_DUPLEX:
		Options |= XEM_FDUPLEX_OPTION;
		break;
	case UNKNOWN_DUPLEX:
		break;
	}

	if (XEmac_mIsSgDma(&lp->Emac)) {
		/*
		 * The following four functions will return an error if we are
		 * not doing scatter-gather DMA.  We just checked that so we
		 * can safely ignore the return values.  We cast them to void
		 * to make that explicit.
		 */
		dma_works = 1;
		(void) XEmac_GetPktThreshold(&lp->Emac, XEM_SEND,
					     &SendThreshold);
		(void) XEmac_GetPktWaitBound(&lp->Emac, XEM_SEND,
					     &SendWaitBound);
		(void) XEmac_GetPktThreshold(&lp->Emac, XEM_RECV,
					     &RecvThreshold);
		(void) XEmac_GetPktWaitBound(&lp->Emac, XEM_RECV,
					     &RecvWaitBound);
	}
	else
		dma_works = 0;

	XEmac_Reset(&lp->Emac);
 
#ifdef RESET_DELAY
	mdelay(RESET_DELAY);
#endif

	/*
	 * The following three functions will return an error if the
	 * EMAC is already started.  We just stopped it by calling
	 * XEmac_Reset() so we can safely ignore the return values.
	 * We cast them to void to make that explicit.
	 */
	(void) XEmac_SetMacAddress(&lp->Emac, dev->dev_addr);
	(void) XEmac_SetInterframeGap(&lp->Emac, IfgPart1, IfgPart2);
	(void) XEmac_SetOptions(&lp->Emac, Options);
	if (XEmac_mIsSgDma(&lp->Emac)) {
		/*
		 * The following four functions will return an error if
		 * we are not doing scatter-gather DMA or if the EMAC is
		 * already started.  We just checked that we are indeed
		 * doing scatter-gather and we just stopped the EMAC so
		 * we can safely ignore the return values.  We cast them
		 * to void to make that explicit.
		 */
		(void) XEmac_SetPktThreshold(&lp->Emac, XEM_SEND,
					     SendThreshold);
		(void) XEmac_SetPktWaitBound(&lp->Emac, XEM_SEND,
					     SendWaitBound);
		(void) XEmac_SetPktThreshold(&lp->Emac, XEM_RECV,
					     RecvThreshold);
		(void) XEmac_SetPktWaitBound(&lp->Emac, XEM_RECV,
					     RecvWaitBound);
	}

	/*
	 * XEmac_Start returns an error when: it is already started, the send
	 * and receive handlers are not set, or a scatter-gather DMA list is
	 * missing.  None of these can happen at this point, so we cast the
	 * return to void to make that explicit.
	 */

	if (dma_works) {
		int avail_plus = 0;

		while (!(XDmaChannel_IsSgListEmpty(&(lp->Emac.SendChannel)))) {	/* list isn't empty, has to be cleared */
			int ret;
			XBufDescriptor *BdPtr;

			if ((ret =
			     XDmaChannel_GetDescriptor(&(lp->Emac.SendChannel),
						       &BdPtr)) !=
			    XST_SUCCESS) {
				printk(KERN_ERR
				       "SgDma ring structure ERROR %d\n", ret);
				break;
			}
			avail_plus++;
			XBufDescriptor_Unlock(BdPtr);
			pci_unmap_single(NULL,
					 (u32)
					 XBufDescriptor_GetSrcAddress(BdPtr),
					 XBufDescriptor_GetLength(BdPtr),
					 DMA_TO_DEVICE);
			lp->stats.tx_errors++;
		}
		atomic_add(avail_plus, &lp->availSendBds);
	}
	else {
		if (lp->deferred_skb) {
			dev_kfree_skb(lp->deferred_skb);
			lp->deferred_skb = NULL;
			lp->stats.tx_errors++;
		}
	}

	dev->trans_start = 0xffffffff - TX_TIMEOUT - TX_TIMEOUT;	/* to exclude tx timeout */
	(void) XEmac_Start(&lp->Emac);
	/* We're all ready to go.  Start the queue in case it was stopped. */
	if (!bh_entry)
		netif_wake_queue(dev);
}

/******************************************************************************
*
* FUNCTION:
*
* AddCsumRxPseudoHeader
*
* DESCRIPTION:
*
* Calculate the Pseudo header checksum of the provided IP packet
*
* ARGUMENTS:
*
* skb is the buffer containing the received packet. The entire packet is
* within this skb.
*
* Initial Checksum - Checksum to start with, InitCSum
*
* Length of the Data, IpPayloadLen
*
* ProtoTTL is the data from the IP header containing the Time To Live (TTL)
*          and the protocol type, 6 = TCP and 16 = UDP
*
*
* RETURN VALUE:
*
* Completed checksum or 0 if not an IP/TCP or IP/UDP packet
*
******************************************************************************/
inline static u16 AddCsumRxPseudoHeader(struct sk_buff *skb, u16 InitCSum,
					u16 IpPayloadLen, u16 ProtoTTL)
{
	register u32 Csum;
	int i;

	Csum = InitCSum;

	/*
	 * Add in the pseudoheader source address and destination address info
	 */

	for (i = RX_PSEUDO_HEADER_DATA_START;
	     i <= RX_PSEUDO_HEADER_DATA_END; i = i + 2) {
		Csum += (u32) (*(u16 *) (skb->data + i));
	}

	Csum += (u32) (ProtoTTL & 0x00FF);

	/* Add in the length of the TCP/UDP data payload */
	Csum += (u32) (IpPayloadLen);

	/* Handle the carries */
	Csum += ((Csum & 0xFFFF0000) >> 16);


	return (Csum);

}

static int get_phy_status(struct net_device *dev, DUPLEX * duplex, int *linkup)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u16 reg;
	int xs;

	xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_BMCR, &reg);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: Could not read PHY control register; error %d\n",
		       dev->name, xs);
		return -1;
	}

	if (!(reg & BMCR_ANENABLE)) {
		/*
		 * Auto-negotiation is disabled so the full duplex bit in
		 * the control tells us if the PHY is running
		 * half or full duplex.
		 */
		*duplex = (reg & BMCR_FULLDPLX) ? FULL_DUPLEX : HALF_DUPLEX;
	}
	else {
		/*
		 * Auto-negotiation is enabled.  Figure out what was
		 * negotiated by looking for the best mode in the union
		 * of what we and our partner advertise.
		 */
		u16 advertise, partner, negotiated;

		xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr,
				   MII_ADVERTISE, &advertise);
		if (xs != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: Could not read PHY advertisement; error %d\n",
			       dev->name, xs);
			return -1;
		}
		xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_LPA, &partner);
		if (xs != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: Could not read PHY LPA; error %d\n",
			       dev->name, xs);
			return -1;
		}

		negotiated = advertise & partner & ADVERTISE_ALL;
		if (negotiated & ADVERTISE_100FULL)
			*duplex = FULL_DUPLEX;
		else if (negotiated & ADVERTISE_100HALF)
			*duplex = HALF_DUPLEX;
		else if (negotiated & ADVERTISE_10FULL)
			*duplex = FULL_DUPLEX;
		else
			*duplex = HALF_DUPLEX;
	}

	xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_BMSR, &reg);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: Could not read PHY status register; error %d\n",
		       dev->name, xs);
		return -1;
	}

	*linkup = (reg & BMSR_LSTATUS) != 0;

	return 0;
}

/*
 * This routine is used for two purposes.  The first is to keep the
 * EMAC's duplex setting in sync with the PHY's.  The second is to keep
 * the system apprised of the state of the link.  Note that this driver
 * does not configure the PHY.  Either the PHY should be configured for
 * auto-negotiation or it should be handled by something like mii-tool.
 */
static void poll_mii(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 Options;
	DUPLEX phy_duplex, mac_duplex;
	int phy_carrier, netif_carrier;
	unsigned long flags;

	/* First, find out what's going on with the PHY. */
	if (get_phy_status(dev, &phy_duplex, &phy_carrier)) {
		printk(KERN_ERR "%s: Terminating link monitoring.\n",
		       dev->name);
		return;
	}

	/* Second, figure out if we have the EMAC in half or full duplex. */
	Options = XEmac_GetOptions(&lp->Emac);
	mac_duplex = (Options & XEM_FDUPLEX_OPTION) ? FULL_DUPLEX : HALF_DUPLEX;

	/* Now see if there is a mismatch. */
	if (mac_duplex != phy_duplex) {
		/*
		 * Make sure that no interrupts come in that could cause
		 * reentrancy problems in reset.
		 */
		spin_lock_irqsave(&reset_lock, flags);
		reset(dev, phy_duplex);	/* the function sets Emac options to match the PHY */
		spin_unlock_irqrestore(&reset_lock, flags);
		if (mac_duplex == FULL_DUPLEX)
			printk(KERN_INFO
			       "%s: Duplex has been changed: now %s\n",
			       dev->name, "HALF_DUPLEX");
		else
			printk(KERN_INFO
			       "%s: Duplex has been changed: now %s\n",
			       dev->name, "FULL_DUPLEX");
	}
	netif_carrier = netif_carrier_ok(dev) != 0;

	if (phy_carrier != netif_carrier) {
		if (phy_carrier) {
			printk(KERN_INFO "%s: Link carrier restored.\n",
			       dev->name);
			netif_carrier_on(dev);
		}
		else {
			printk(KERN_INFO "%s: Link carrier lost.\n", dev->name);
			netif_carrier_off(dev);
		}
	}

	/* Set up the timer so we'll get called again in 2 seconds. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	add_timer(&lp->phy_timer);
}

/*
 * This routine is registered with the OS as the function to call when
 * the EMAC interrupts.  It in turn, calls the Xilinx OS independent
 * interrupt function.  There are different interrupt functions for FIFO
 * and scatter-gather so we just set a pointer (Isr) into our private
 * data so we don't have to figure it out here.  The Xilinx OS
 * independent interrupt function will in turn call any callbacks that
 * we have registered for various conditions.
 */
static irqreturn_t xenet_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;

	/* Call it. */
	(*(lp->Isr)) (&lp->Emac);

	/* Right now, our IRQ handlers do not return a status. Let's always return
	 * IRQ_HANDLED here for now.
	 */
	return IRQ_HANDLED;
}

static int xenet_open(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 Options;
	DUPLEX phy_duplex, mac_duplex;
	int phy_carrier;

	/*
	 * Just to be safe, stop the device first.  If the device is already
	 * stopped, an error will be returned.  In this case, we don't really
	 * care, so cast it to void to make it explicit.
	 */
	(void) XEmac_Stop(&lp->Emac);
	/* Set the MAC address each time opened. */
	if (XEmac_SetMacAddress(&lp->Emac, dev->dev_addr) != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not set MAC address.\n", dev->name);
		return -EIO;
	}

	/*
	 * If the device is not configured for polled mode, connect to the
	 * interrupt controller and enable interrupts.  Currently, there
	 * isn't any code to set polled mode, so this check is probably
	 * superfluous.
	 */
	Options = XEmac_GetOptions(&lp->Emac);
	if ((Options & XEM_POLLED_OPTION) == 0) {
		int retval;

		/* Grab the IRQ */
		retval = request_irq(dev->irq, xenet_interrupt, 0, dev->name, dev);
		if (retval) {
			printk(KERN_ERR
			       "%s: Could not allocate interrupt %d.\n",
			       dev->name, dev->irq);
			return retval;
		}
	}

        /* Only advertise 10/100 modes, since we can't talk to a
         * Tri-mode PHY if it autonegotiates a gigabit link. (e.g. ML403, ML410)
         */
        XEmac_PhyWrite(&lp->Emac, lp->mii_addr, MII_ADVERTISE, ADVERTISE_ALL | ADVERTISE_CSMA);
        XEmac_PhyWrite(&lp->Emac, lp->mii_addr, MII_CTRL1000, 0);

        /* Give the system enough time to establish a link */
        mdelay(2000);

	/* Set the EMAC's duplex setting based upon what the PHY says. */
	if (!get_phy_status(dev, &phy_duplex, &phy_carrier)) {
		/* We successfully got the PHY status. */
		mac_duplex = ((Options & XEM_FDUPLEX_OPTION)
			      ? FULL_DUPLEX : HALF_DUPLEX);
		if (mac_duplex != phy_duplex) {
			switch (phy_duplex) {
			case HALF_DUPLEX:
				Options &= ~XEM_FDUPLEX_OPTION;
				break;
			case FULL_DUPLEX:
				Options |= XEM_FDUPLEX_OPTION;
				break;
			case UNKNOWN_DUPLEX:
				break;
			}
			/*
			 * The following function will return an error
			 * if the EMAC is already started.  We know it
			 * isn't started so we can safely ignore the
			 * return value.  We cast it to void to make
			 * that explicit.
			 */
		}
	}
	Options |= XEM_FLOW_CONTROL_OPTION;
	(void) XEmac_SetOptions(&lp->Emac, Options);

	INIT_LIST_HEAD(&(lp->rcv));
	lp->rcvBds = 0;
	INIT_LIST_HEAD(&(lp->xmit));
	lp->xmitBds = 0;

	if (XEmac_Start(&lp->Emac) != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not start device.\n", dev->name);
		free_irq(dev->irq, dev);
		return -EBUSY;
	}

	/* We're ready to go. */
	netif_start_queue(dev);

	/* Set up the PHY monitoring timer. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	lp->phy_timer.data = (unsigned long) dev;
	lp->phy_timer.function = &poll_mii;
	init_timer(&lp->phy_timer);
	add_timer(&lp->phy_timer);
	return 0;
}
static int xenet_close(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	/* Shut down the PHY monitoring timer. */
	del_timer_sync(&lp->phy_timer);

	netif_stop_queue(dev);

	/*
	 * If not in polled mode, free the interrupt.  Currently, there
	 * isn't any code to set polled mode, so this check is probably
	 * superfluous.
	 */
	if ((XEmac_GetOptions(&lp->Emac) & XEM_POLLED_OPTION) == 0)
		free_irq(dev->irq, dev);

	spin_lock_irqsave(&rcvSpin, flags);
	list_del(&(lp->rcv));
	spin_unlock_irqrestore(&rcvSpin, flags);
	spin_lock_irqsave(&xmitSpin, flags);
	list_del(&(lp->xmit));
	spin_unlock_irqrestore(&xmitSpin, flags);

	if (XEmac_Stop(&lp->Emac) != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not stop device.\n", dev->name);
		return -EBUSY;
	}

	return 0;
}
static int xenet_change_mtu(struct net_device *dev, int new_mtu)
{
	int head_size = XEM_HDR_SIZE;
	int max_frame = new_mtu + head_size + XEM_TRL_SIZE;
	int min_frame = 1 + head_size + XEM_TRL_SIZE;

	if ((max_frame < min_frame) || (max_frame > XEM_MAX_FRAME_SIZE))
		return -EINVAL;

	dev->mtu = new_mtu;	/* change mtu in net_device structure */
	return 0;
}
static struct net_device_stats *xenet_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	return &lp->stats;
}

static int xenet_FifoSend(struct sk_buff *orig_skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct sk_buff *new_skb;
	unsigned int len, align;
	unsigned long flags;

	len = orig_skb->len;

	/* PR FIXME: what follows can be removed if the asserts in the Xilinx
	 * independent drivers change. There is really no need to align the
	 * buffers in FIFO mode. The story is different for simple DMA.
	 */

	/*
	 * The packet FIFO requires the buffers to be 32/64 bit aligned.
	 * The sk_buff data is not 32/64 bit aligned, so we have to do this
	 * copy.  As you probably well know, this is not optimal.
	 */
	if (!(new_skb = alloc_skb(len + ALIGNMENT, GFP_ATOMIC))) {
		/* We couldn't get another skb. */
		dev_kfree_skb(orig_skb);
		lp->stats.tx_dropped++;
		printk(KERN_ERR "%s: Could not allocate transmit buffer.\n",
		       dev->name);
		netif_wake_queue(dev);
		return -EBUSY;
	}
	/*
	 * A new skb should have the data word aligned, but this code is
	 * here just in case that isn't true...  Calculate how many
	 * bytes we should reserve to get the data to start on a word
	 * boundary.  */
	align = BUFFER_ALIGN(new_skb->data);
	if (align)
		skb_reserve(new_skb, align);

	/* Copy the data from the original skb to the new one. */
	skb_put(new_skb, len);
	memcpy(new_skb->data, orig_skb->data, len);

	/* Get rid of the original skb. */
	dev_kfree_skb(orig_skb);
	spin_lock_irqsave(&reset_lock, flags);
	if (XEmac_FifoSend(&lp->Emac, (u8 *) new_skb->data, len) != XST_SUCCESS) {
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

/* The callback function for completed frames sent in FIFO mode. */
static void FifoSendHandler(void *CallbackRef)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	struct net_local *lp = (struct net_local *) dev->priv;

	if (lp->deferred_skb) {
		if (XEmac_FifoSend
		    (&lp->Emac, (u8 *) lp->deferred_skb->data,
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

/* The send function for frames sent in DMA mode. */
static int xenet_SgSend(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned int len;
	XBufDescriptor bd;
	int result;
	u32 physAddr;
	unsigned long flags;
	u8 *virtAddr;

	len = skb->len;
	virtAddr = lp->ddrVirtPtr + lp->ddrOffset;

	if (skb->ip_summed == CHECKSUM_NONE)
#ifdef CONFIG_PPC32
		cacheable_memcpy(virtAddr, skb->data, len);
#else
		memcpy(virtAddr, skb->data, len);
#endif
	else
		skb_copy_and_csum_dev(skb, virtAddr);

	dev_kfree_skb(skb);
	physAddr = (u32) dma_map_single(NULL, virtAddr, len, DMA_TO_DEVICE);

	/*
	 * lock the buffer descriptor to prevent lower layers from reusing
	 * it before the adapter has a chance to deallocate the buffer
	 * attached to it. The adapter will unlock it in the callback function
	 * that handles confirmation of transmits
	 */
	XBufDescriptor_Initialize(&bd);
	XBufDescriptor_Lock(&bd);
	XBufDescriptor_SetSrcAddress(&bd, physAddr);
	XBufDescriptor_SetLength(&bd, len);
	XBufDescriptor_SetLast(&bd);

	lp->ddrOffset += len + BUFFER_ALIGN(len);
	if (lp->ddrOffset + XEM_MAX_FRAME_SIZE > lp->ddrSize)
		lp->ddrOffset = 0;

	spin_lock_irqsave(&reset_lock, flags);

	result = XEmac_SgSend(&lp->Emac, &bd, XEM_SGDMA_NODELAY);
	if (result != XST_SUCCESS) {
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: ERROR, could not send transmit buffer (%d).\n",
		       dev->name, result);
		/* we should never get here in the first place, but
		 * for some reason the kernel doesn't like -EBUSY here,
		 * so just return 0 and let the stack handle dropped packets.
		 */
		/*      return -EBUSY; */
		spin_unlock_irqrestore(&reset_lock, flags);
		return 0;
	}

	if (atomic_dec_and_test(&lp->availSendBds)) {
		netif_stop_queue(dev);
	}

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&reset_lock, flags);
	return 0;
}

/*
 * The send function for frames sent in DMA mode using DRE and
 * Checksum offload in the DMA.
 */

static XBufDescriptor bd[10];

static int xenet_SgSendDre(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned int len;
	int result;
	u32 physAddr;
	u8 *virtAddr;
	u32 i;
	unsigned long flags;
	u16 csum_insert_offset;
	u16 IpHeaderLength;
	u16 ProtoTTL;
	volatile u32 num_frag;
	skb_frag_t *frag;
	XBufDescriptor *prev_p;
	XBufDescriptor *cur_p;

	virtAddr = lp->ddrVirtPtr + lp->ddrOffset;

	num_frag = skb_shinfo(skb)->nr_frags;
	frag = &skb_shinfo(skb)->frags[0];

	cur_p = &bd[0];
	prev_p = 0;

	if (num_frag > 9) {
		printk("num_frag:%8.8x \n", num_frag);
	}

	/*
	 * Return to the default configuration for the driver
	 */
	XEmac_mDisableTxHwCsum(&lp->Emac);

	/*
	 * Queue up the buffer descriptors only if there is space in the ring for this
	 * batch with extra room. The netif_stop_queue will not occur until after the
	 * next call to this function, therefore I need to leave enough for at least
	 * one more call in the descriptor ring.
	 */
	if ((lp->availSendBds.counter) < (num_frag + 4)) {
		netif_stop_queue(dev);
	}


	/* Assign dummy values to 'csum_insert_offset', 'IpHeaderLength' and 'len'.
	 * This will quiet down gcc warnings about unassigned variables in the
	 * "else" case of "if (i==0)" below.
	 *
	 * 'csum_insert_offset', 'IpHeaderLength' and 'len' are assigned in the
	 * iteration for the first fragment (i==0) case and will not change for
	 * subsequent fragments. As we ALWAYS execute the (i==0) case the variables
	 * will never be uninitialized.
	 */
	csum_insert_offset = 0;
	IpHeaderLength = 0;
	len = 0;

	for (i = 0; i < (num_frag + 1); ++i) {
		/*
		 * Initialize the buffer desctiptor and then
		 * lock the buffer descriptor to prevent lower layers from reusing
		 * it before the adapter has a chance to deallocate the buffer
		 * attached to it. The adapter will unlock it in the callback function
		 * that handles confirmation of transmits
		 */
		XBufDescriptor_Initialize(cur_p);
		XBufDescriptor_Lock(cur_p);

		if (prev_p) {
			XBufDescriptor_SetNextPtr(prev_p, cur_p);
		}

		if (i == 0) {

			/*
			 * Set the ID for the first descriptor to be the
			 * address of the skbuffer to be freed in the BH
			 */
			XBufDescriptor_SetId(cur_p, skb);

			/* Grab protocol */
			ProtoTTL = (*(u16 *) (skb->data + 22));

			/*
			 * The kernel does not send frames down with
			 * CHECKSUM_COMPLETE set unless they are TCP, UDP is
			 * always CHECKSUM_NONE due to the TCP_SENDFILE test
			 * requirement
			 */
			virtAddr = skb->data;
			len = skb_headlen(skb);

			if ((XEmac_mIsTxHwCsum(&lp->Emac)) &&
			    ((ProtoTTL & 0x00FF) == 6)) {

				if (skb->ip_summed == CHECKSUM_COMPLETE) {
					XEmac_mEnableTxHwCsum(&lp->Emac);

					/*
					 * Determine the length of the IP header which is used
					 * for the offset into the data for the protocol field.
					 */

					IpHeaderLength =
						((((*(u16 *) (skb->data + 14)) &
						   0x0F00) >> 8) * 4);

					/*
					 * Determine the proper offset for the insert
					 * TCP offset is 16, UDP offset is 6 but the
					 * 2.4 stack does not use this for UDP
					 */
					csum_insert_offset =
						IpHeaderLength + 16 +
						ENET_HDR_SIZ;

					/*
					 * 0 works for the TCP TX checksum offload initial value
					 */
					XBufDescriptor_SetCSInit(cur_p, 0);
					XBufDescriptor_SetCSInsertLoc(cur_p,
								      csum_insert_offset);
					XBufDescriptor_SetCSBegin(cur_p,
								  IpHeaderLength
								  +
								  ENET_HDR_SIZ);

				}

				XBufDescriptor_SetLength(cur_p, len);

				physAddr =
					(u32) dma_map_single(NULL, virtAddr,
							     len,
							     DMA_TO_DEVICE);
				XBufDescriptor_SetSrcAddress(cur_p, physAddr);

			}
			else {
				/*
				 * First fragment, no hardware checksum offload or is it not TCP
				 */
				XBufDescriptor_SetLength(cur_p, len);

				physAddr =
					(u32) dma_map_single(NULL, virtAddr,
							     len,
							     DMA_TO_DEVICE);
				XBufDescriptor_SetSrcAddress(cur_p, physAddr);
			}
		}
		else {
			/*
			 * Fragment is not number 0
			 */
			virtAddr = ((void *) page_address(frag->page) +
				    frag->page_offset);

			len = frag->size;

			/* NOTE:
			 *   'csum_insert_offset', 'IpHeaderLength' and 'len' will be
			 *   initialized in the first iteration of the loop in the (i==0)
			 *   case, so they are valid in this iteration of the loop.
			 */
			physAddr =
				(u32) dma_map_single(NULL, virtAddr, len,
						     DMA_TO_DEVICE);
			XBufDescriptor_SetSrcAddress(cur_p, physAddr);
			XBufDescriptor_SetCSInit(cur_p, 0);

			XBufDescriptor_SetCSInsertLoc(cur_p,
						      csum_insert_offset);
			XBufDescriptor_SetCSBegin(cur_p,
						  IpHeaderLength +
						  ENET_HDR_SIZ);
			XBufDescriptor_SetLength(cur_p, len);

			frag++;
		}

		if (i == num_frag) {
			/*
			 * This is the last descriptor in the chain
			 */
			XBufDescriptor_SetLast(cur_p);
		}
		prev_p = cur_p;
		cur_p++;
	}

	spin_lock_irqsave(&reset_lock, flags);
	for (i = 0; i < (num_frag + 1); ++i) {

		result = XEmac_SgSend(&lp->Emac, &bd[i], XEM_SGDMA_NODELAY);
		if (result != XST_SUCCESS) {
			lp->stats.tx_dropped++;
			printk( /*KERN_ERR */
			       "%s: ERROR, could not send transmit buffer (%d).\n",
			       dev->name, result);
			/* we should never get here in the first place, but
			 * for some reason the kernel doesn't like -EBUSY here,
			 * so just return 0 and let the stack handle dropped packets.
			 */
			/*      return -EBUSY; */
			spin_unlock_irqrestore(&reset_lock, flags);
			return 0;
		}

	}

	if ((atomic_sub_return((num_frag + 1), &lp->availSendBds)) == 0) {
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&reset_lock, flags);
	dev->trans_start = jiffies;

	return 0;
}


/* The callback function for completed frames sent in DMA mode. */
static void SgSendHandlerBH(unsigned long p);
static void SgRecvHandlerBH(unsigned long p);

DECLARE_TASKLET(SgSendBH, SgSendHandlerBH, 0);
DECLARE_TASKLET(SgRecvBH, SgRecvHandlerBH, 0);

static void SgSendHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	XBufDescriptor *BdPtr;
	u32 NumBds;
	u32 len;
	XBufDescriptor *curbd;
	unsigned long flags;
	struct sk_buff *skb;

	while (1) {
		spin_lock_irqsave(&xmitSpin, flags);
		if (list_empty(&sentQueue)) {
			spin_unlock_irqrestore(&xmitSpin, flags);
			break;
		}
		lp = list_entry(sentQueue.next, struct net_local, xmit);

		list_del_init(&(lp->xmit));
		NumBds = lp->xmitBds;
		BdPtr = lp->xmitBdPtr;
		dev = lp->ndev;
		atomic_add(NumBds, &lp->availSendBds);
		while (NumBds != 0) {
			NumBds--;

			len = XBufDescriptor_GetLength(BdPtr);
			pci_unmap_single(NULL,
					 (u32)
					 XBufDescriptor_GetSrcAddress(BdPtr),
					 len, DMA_TO_DEVICE);

			lp->stats.tx_bytes += len;
			lp->stats.tx_packets++;

			curbd = BdPtr;
			BdPtr = P_TO_V(&lp->Emac.SendChannel,
				       XBufDescriptor_GetNextPtr(BdPtr));
			XBufDescriptor_Unlock(curbd);
			/*
			 * If the descriptor was part of a fragment list, the ID is
			 * the skbuffer which can be freed at this point
			 */
			skb = (struct sk_buff *) XBufDescriptor_GetId(curbd);
			if (skb != 0UL) {
				dev_kfree_skb(skb);
			}

		}
		spin_unlock_irqrestore(&xmitSpin, flags);
		netif_wake_queue(dev);
	}
	bh_entry = 0;
}

static void SgSendHandler(void *CallBackRef, XBufDescriptor * BdPtr, u32 NumBds)
{
	struct net_device *dev = (struct net_device *) CallBackRef;
	struct net_local *lp = (struct net_local *) dev->priv;
	struct list_head *cur_lp = NULL;

	spin_lock(&xmitSpin);
	list_for_each(cur_lp, &sentQueue) {
		if (cur_lp == &(lp->xmit)) {
			lp->xmitBds += NumBds;
			break;
		}
	}
	if (cur_lp != &(lp->xmit)) {
		lp->xmitBds = NumBds;
		lp->xmitBdPtr = BdPtr;
		list_add_tail(&lp->xmit, &sentQueue);
		bh_entry++;
		tasklet_schedule(&SgSendBH);
	}
	spin_unlock(&xmitSpin);
}

static void SgRecvHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	XBufDescriptor *BdPtr;
	int NumBds;
	struct sk_buff *skb, *new_skb;
	u32 len, new_skb_vaddr;
	dma_addr_t skb_vaddr;
	u32 align;
	int result;
	XBufDescriptor *curbd;
	unsigned long flags;
	u16 HwCSum, PhCSum;
	u16 IpDataLen, IpHeaderLength, ProtoTTL;
	u32 CalcCSum;
	u32 EmacFCS;
	u8 *EmacFCSPtr;

	while (1) {
		spin_lock_irqsave(&rcvSpin, flags);
		if (list_empty(&receivedQueue)) {
			spin_unlock_irqrestore(&rcvSpin, flags);
			break;
		}
		lp = list_entry(receivedQueue.next, struct net_local, rcv);

		list_del_init(&(lp->rcv));
		NumBds = lp->rcvBds;
		BdPtr = lp->rcvBdPtr;
		dev = lp->ndev;
		spin_unlock_irqrestore(&rcvSpin, flags);
		while (NumBds != 0) {
			NumBds--;

			/* get ptr to skb */
			skb = (struct sk_buff *) XBufDescriptor_GetId(BdPtr);
			len = XBufDescriptor_GetLength(BdPtr);

			/*
			 * Retrieve hardware Checksum regardless, check later if
			 * valid to use
			 */

			HwCSum = XBufDescriptor_GetCSRaw(BdPtr);

			/* we have all the information we need - move on */
			curbd = BdPtr;
			BdPtr = P_TO_V(&lp->Emac.RecvChannel,
				       XBufDescriptor_GetNextPtr(curbd));

			skb_vaddr =
				(dma_addr_t)
				XBufDescriptor_GetDestAddress(curbd);
			pci_unmap_single(NULL, skb_vaddr, len, DMA_FROM_DEVICE);

			/* replace skb with a new one */
			new_skb =
				alloc_skb(XEM_MAX_FRAME_SIZE + ALIGNMENT,
					  GFP_ATOMIC);
			if (new_skb == 0) {
				printk("SgRecvHandler: no mem for new_skb\n");
				return;
			}

			if (!(XEmac_mIsRxDre(&lp->Emac))) {
				/* make sure we're long-word aligned */
				align = BUFFER_ALIGN(new_skb->data);
				if (align) {
					skb_reserve(new_skb, align);
				}
			}

			new_skb_vaddr =
				(u32) dma_map_single(NULL, new_skb->data,
						     XEM_MAX_FRAME_SIZE,
						     DMA_FROM_DEVICE);

			XBufDescriptor_SetDestAddress(curbd, new_skb_vaddr);
			XBufDescriptor_SetLength(curbd, XEM_MAX_FRAME_SIZE);
			XBufDescriptor_SetId(curbd, new_skb);
			XBufDescriptor_Unlock(curbd);

			/* give the descriptor back to the driver */
			result = XEmac_SgRecv(&lp->Emac, curbd);
			if (result != XST_SUCCESS) {
				printk("SgRecvHandler: SgRecv unsuccessful\n");
				return;
			}

			/* back to the original skb
			 * NOTE:
			 *   The following line should read
			 *     skb_put(skb, len);
			 *   However, doing this causes the driver not to work
			 *   anymore.  If you want to fix this and put in the
			 *   skb_pu() again, you need to delete the
			 *     skb->len -= 4;
			 *   line below.
			 * wgr 09/14/2006
			 */
			skb->len = len;
			skb->dev = dev;
			skb->protocol = eth_type_trans(skb, dev);
			skb->ip_summed = CHECKSUM_NONE;

			lp->stats.rx_packets++;
			lp->stats.rx_bytes += len;

			/*
			 * Check if Checksum offload in in the hardware, if so
			 * verify the checksum here and then sent up the stack
			 */
			if ((len > 76 /*64 */ ) &&
			    (skb->protocol == ETHERTYPE_IP) &&
			    (XEmac_mIsRxHwCsum(&lp->Emac))) {

				EmacFCS = 0;
				EmacFCSPtr = (u8 *) &EmacFCS;

				IpHeaderLength = ((((*(u16 *) (skb->data)) & 0x0F00) >> 8) * 4);

				/* Grab protocol */
				ProtoTTL = (*(u16 *) (skb->data + 8)) & 0x00FF;

				/*
				 * Set the length of the IP payload for the CS calculation
				 */
				IpDataLen = len - IpHeaderLength - ENET_HDR_SIZ;

				/*
				 * Adjust the hardware checksum due to the fact that it ALWAYS includes
				 * the FCS field in the RX data, regardless of whether the.
				 * XEM_STRIP_PAD_FCS_OPTION is set or not set around 2400.
				 */

				CalcCSum = HwCSum;

				if (((IpDataLen & 0x0003) == 2) ||
				    ((IpDataLen & 0x0003) == 0)) {
					/*
					 * 16-bit alignment case
					 */
					EmacFCSPtr[0] = skb_mac_header(skb)[len-4];
					EmacFCSPtr[1] = skb_mac_header(skb)[len-3];
					EmacFCSPtr[2] = skb_mac_header(skb)[len-2];
					EmacFCSPtr[3] = skb_mac_header(skb)[len-1];
				}
				else if ((IpDataLen & 0x0003) == 1) {
					/*
					 * 8-bit alignment case one
					 */
					EmacFCSPtr[0] = skb_mac_header(skb)[len-3];
					EmacFCSPtr[1] = skb_mac_header(skb)[len-2];
					EmacFCSPtr[2] = skb_mac_header(skb)[len-1];
					EmacFCSPtr[3] = skb_mac_header(skb)[len-4];
				}
				else if ((IpDataLen & 0x0003) == 3) {
					/*
					 * 8-bit alignment case two
					 */
					EmacFCSPtr[0] = skb_mac_header(skb)[len-1];
					EmacFCSPtr[1] = skb_mac_header(skb)[len-4];
					EmacFCSPtr[2] = skb_mac_header(skb)[len-3];
					EmacFCSPtr[3] = skb_mac_header(skb)[len-2];
				}

				CalcCSum +=
					(u32) ((*(u16 *) (&(EmacFCSPtr[0]))) ^
					       0xFFFF);
				CalcCSum +=
					(u32) ((*(u16 *) (&(EmacFCSPtr[2]))) ^
					       0xFFFF);
				CalcCSum += (u32) (0xFFFB);	/* this is the subtraction of 4, trust me */

				HwCSum = ((CalcCSum >> 16) +
					  (CalcCSum & 0x0000FFFF));

				PhCSum = AddCsumRxPseudoHeader(skb, HwCSum,
							       IpDataLen,
							       ProtoTTL);

				/*
				 * The resulting checksum should be equal to 0xFFFF. If not, the upper
				 * layers can calculate where the error is and retransmit if needed.
				 */

				if (PhCSum == 0xFFFF) {

					skb->ip_summed = CHECKSUM_UNNECESSARY;
					/* NOTE:
					 *   The following line goes together with the changed
					 *     skb_put(skb, len);
					 *   line above. If you change the line above to skb_put(),
					 *   you need to delete the following line.
					 * wgr 09/14/2006
					 */
					skb->len -= 4;
					skb->csum = 0xFFFF;
				}
			}

			netif_rx(skb);	/* Send the packet upstream. */
		}
	}
}

static void SgRecvHandler(void *CallBackRef, XBufDescriptor * BdPtr, u32 NumBds)
{
	struct net_device *dev = (struct net_device *) CallBackRef;
	struct net_local *lp = (struct net_local *) dev->priv;
	struct list_head *cur_lp = NULL;

	spin_lock(&rcvSpin);
	list_for_each(cur_lp, &receivedQueue) {
		if (cur_lp == &(lp->rcv)) {
			lp->rcvBds += NumBds;
			break;
		}
	}
	if (cur_lp != &(lp->rcv)) {
		lp->rcvBds = NumBds;
		lp->rcvBdPtr = BdPtr;
		list_add_tail(&lp->rcv, &receivedQueue);
		tasklet_schedule(&SgRecvBH);
	}
	spin_unlock(&rcvSpin);
}

static void xenet_tx_timeout(struct net_device *dev)
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

/* The callback function for frames received when in FIFO mode. */
static void FifoRecvHandler(void *CallbackRef)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	struct net_local *lp = (struct net_local *) dev->priv;
	struct sk_buff *skb;
	unsigned int align;
	u32 len;
	int Result;

	/*
	 * The OS independent Xilinx EMAC code does not provide a
	 * function to get the length of an incoming packet and a
	 * separate call to actually get the packet data.  It does this
	 * because they didn't add any code to keep the hardware's
	 * receive length and data FIFOs in sync.  Instead, they require
	 * that you send a maximal length buffer so that they can read
	 * the length and data FIFOs in a single chunk of code so that
	 * they can't get out of sync.  So, we need to allocate an skb
	 * that can hold a maximal sized packet.  The OS independent
	 * code needs to see the data 32/64-bit aligned, so we tack on an
	 * extra four just in case we need to do an skb_reserve to get
	 * it that way.
	 */
	len = XEM_MAX_FRAME_SIZE;
	if (!(skb = alloc_skb(len + ALIGNMENT, GFP_ATOMIC))) {
		/* Couldn't get memory. */
		lp->stats.rx_dropped++;
		printk(KERN_ERR "%s: Could not allocate receive buffer.\n",
		       dev->name);
		return;
	}

	/*
	 * A new skb should have the data word aligned, but this code is
	 * here just in case that isn't true...  Calculate how many
	 * bytes we should reserve to get the data to start on a word
	 * boundary.  */
	align = BUFFER_ALIGN(skb->data);
	if (align)
		skb_reserve(skb, align);

	Result = XEmac_FifoRecv(&lp->Emac, (u8 *) skb->data, &len);
	if (Result != XST_SUCCESS) {
		int need_reset = status_requires_reset(Result);

		lp->stats.rx_errors++;
		dev_kfree_skb(skb);
		printk(KERN_ERR "%s: Could not receive buffer, error=%d%s.\n",
		       dev->name, Result,
		       need_reset ? ", resetting device." : "");
		if (need_reset) {
			spin_lock(&reset_lock);
			reset(dev, UNKNOWN_DUPLEX);
			spin_unlock(&reset_lock);
		}

		return;
	}

	skb_put(skb, len - 4);	/* Tell the skb how much data we got,
				   crop FCS (the last four bytes). */
	skb->dev = dev;		/* Fill out required meta-data. */
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;

	lp->stats.rx_packets++;
	lp->stats.rx_bytes += len;

	netif_rx(skb);		/* Send the packet upstream. */
}

/* The callback function for errors. */
static void ErrorHandler(void *CallbackRef, int Code)
{
	struct net_device *dev = (struct net_device *) CallbackRef;
	int need_reset = status_requires_reset(Code);
	unsigned long flags;

	/* ignore some errors */
	if (Code == XST_DMA_ERROR)
		return;
	printk(KERN_ERR "%s: device error %d%s\n",
	       dev->name, Code, need_reset ? ", resetting device." : "");
	if (need_reset) {
		spin_lock_irqsave(&reset_lock, flags);
		reset(dev, UNKNOWN_DUPLEX);
		spin_unlock_irqrestore(&reset_lock, flags);
	}
}

static int descriptor_init(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int i, recvsize, sendsize;
	int dftsize;
	u32 *recvpoolptr, *sendpoolptr;
	void *recvpoolphy, *sendpoolphy;

	/* calc size of descriptor space pool; alloc from non-cached memory */
	dftsize = (XEM_DFT_RECV_DESC + XEM_DFT_SEND_DESC) *
		sizeof(XBufDescriptor);

	lp->desc_space = dma_alloc_coherent(NULL, dftsize,
					    &lp->desc_space_handle, GFP_KERNEL);
	if (lp->desc_space == 0) {
		return -1;
	}

	lp->desc_space_size = dftsize;

	lp->ddrSize = DFT_LOCAL_SEND_DESC * (XEM_MAX_FRAME_SIZE + ALIGNMENT);
	lp->ddrOffset = 0;
	lp->ddrVirtPtr = kmalloc(lp->ddrSize, GFP_ATOMIC);

	if (lp->ddrVirtPtr == 0)
		return -1;

	if (XEmac_mIsTxDre(&lp->Emac)) {
		atomic_set(&lp->availSendBds, XEM_DFT_SEND_DESC);
	}
	else {
		atomic_set(&lp->availSendBds, DFT_LOCAL_SEND_DESC);
	}



	/* calc size of send and recv descriptor space */
	recvsize = XEM_DFT_RECV_DESC * sizeof(XBufDescriptor);
	sendsize = XEM_DFT_SEND_DESC * sizeof(XBufDescriptor);

	recvpoolptr = lp->desc_space;
	sendpoolptr = (void *) ((u32) lp->desc_space + recvsize);

	recvpoolphy = (void *) lp->desc_space_handle;
	sendpoolphy = (void *) ((u32) lp->desc_space_handle + recvsize);

	/* add ptr to descriptor space to the driver */
	XEmac_SetSgRecvSpace(&lp->Emac, recvpoolptr, recvsize, recvpoolphy);
	XEmac_SetSgSendSpace(&lp->Emac, sendpoolptr, sendsize, sendpoolphy);

	/* allocate skb's and give them to the dma engine */
	for (i = 0; i < XEM_DFT_RECV_DESC; i++) {
		struct sk_buff *skb;
		XBufDescriptor bd;
		int result;
		u32 skb_vaddr, align;

		skb = alloc_skb(XEM_MAX_FRAME_SIZE + ALIGNMENT, GFP_ATOMIC);
		if (skb == 0) {
			return -1;
		}

		align = BUFFER_ALIGN(skb->data);
		if (align)
			skb_reserve(skb, align);

		skb_vaddr = (u32) dma_map_single(NULL, skb->data,
						 XEM_MAX_FRAME_SIZE,
						 DMA_FROM_DEVICE);

		/*
		 * initialize descriptors and set buffer address
		 * buffer length gets max frame size
		 */
		XBufDescriptor_Initialize(&bd);
		XBufDescriptor_Lock(&bd);
		XBufDescriptor_SetDestAddress(&bd, skb_vaddr);
		XBufDescriptor_SetLength(&bd, XEM_MAX_FRAME_SIZE);
		XBufDescriptor_SetId(&bd, skb);

		/*
		 * descriptor with attached buffer to the driver and
		 * let it make it ready for frame reception
		 */
		result = XEmac_SgRecv(&lp->Emac, &bd);
		if (result != XST_SUCCESS) {
			return -1;
		}
	}

	return 0;
}

static void free_descriptor_skb(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int i;
	XBufDescriptor *BdPtr;
	struct sk_buff *skb;

	BdPtr = (XBufDescriptor *) lp->Emac.RecvChannel.VirtPtr;
	for (i = 0; i < XEM_DFT_RECV_DESC; i++) {
		skb = (struct sk_buff *) XBufDescriptor_GetId(BdPtr);
		pci_unmap_single(NULL, virt_to_bus(skb->data),
				 XBufDescriptor_GetLength(BdPtr),
				 DMA_FROM_DEVICE);
		dev_kfree_skb(skb);
		BdPtr = P_TO_V(&lp->Emac.RecvChannel,
			       XBufDescriptor_GetNextPtr(BdPtr));
	}
}

static void xenet_set_multicast_list(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 Options;
	unsigned long flags;
	int ret = 0;

	/*
	 * XEmac_Start, XEmac_Stop and XEmac_SetOptions are supposed to
	 * be protected by a semaphore. We do have one area in which
	 * this is a problem.
	 *
	 * xenet_set_multicast_list() is called while the link is up and
	 * interrupts are enabled, so at any point in time we could get
	 * an error that causes our reset() to be called.  reset() calls
	 * the aforementioned functions, and we need to call them from
	 * here as well.
	 *
	 * The solution is to make sure that we don't get interrupts or
	 * timers popping while we are in this function.
	 */
	spin_lock_irqsave(&reset_lock, flags);

	if ((ret = XEmac_Stop(&lp->Emac)) == XST_SUCCESS) {

		Options = XEmac_GetOptions(&lp->Emac);

		/* Clear out the bits we may set. */
		Options &= ~(XEM_PROMISC_OPTION | XEM_MULTICAST_OPTION);

		if (dev->flags & IFF_PROMISC)
			Options |= XEM_PROMISC_OPTION;
#if 0
		else {
			/*
			 * SAATODO: Xilinx is going to add multicast support to their
			 * VxWorks adapter and OS independent layer.  After that is done,
			 * this skeleton code should be fleshed out.  Note that
			 * IFF_MULTICAST is being masked out from dev->flags in
			 * xenet_probe, so that will need to be removed to actually do
			 * multidrop.
			 */
			if ((dev->flags & IFF_ALLMULTI)
			    || dev->mc_count > MAX_MULTICAST ? ? ?) {
				xemac_get_all_multicast ? ? ? ();
				Options |= XEM_MULTICAST_OPTION;
			}
			else if (dev->mc_count != 0) {
				struct dev_mc_list *mc;

				XEmac_MulticastClear(&lp->Emac);
				for (mc = dev->mc_list; mc; mc = mc->next)
					XEmac_MulticastAdd(&lp->Emac,
							   mc->dmi_addr);
				Options |= XEM_MULTICAST_OPTION;
			}
		}
#endif

		/*
		 * The following function will return an error if the EMAC is already
		 * started.  We know it isn't started so we can safely ignore the
		 * return value.  We cast it to void to make that explicit.
		 */
		(void) XEmac_SetOptions(&lp->Emac, Options);

		/*
		 * XEmac_Start returns an error when: it is already started, the send
		 * and receive handlers are not set, or a scatter-gather DMA list is
		 * missing.  None of these can happen at this point, so we cast the
		 * return to void to make that explicit.
		 */
		(void) XEmac_Start(&lp->Emac);
	}
	/* All done, get those interrupts and timers going again. */
	spin_unlock_irqrestore(&reset_lock, flags);
}


static int
xenet_ethtool_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	int ret;
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 mac_options;
	u8 threshold;
	u16 mii_cmd;
	u16 mii_status;
	u16 mii_advControl;
	int xs;

	memset(ecmd, 0, sizeof(struct ethtool_cmd));
	mac_options = XEmac_GetOptions(&(lp->Emac));
	xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_BMCR, &mii_cmd);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: Could not read mii command register; error %d\n",
		       dev->name, xs);
		return -1;
	}
	xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_BMSR, &mii_status);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: Could not read mii status register; error %d\n",
		       dev->name, xs);
		return -1;
	}
	xs = XEmac_PhyRead(&lp->Emac, lp->mii_addr, MII_ADVERTISE,
			   &mii_advControl);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: Could not read mii advertisement control register; error %d\n",
		       dev->name, xs);
		return -1;
	}

	if (mac_options & XEM_FDUPLEX_OPTION)
		ecmd->duplex = DUPLEX_FULL;
	else
		ecmd->duplex = DUPLEX_HALF;
	if (mii_status & BMSR_100FULL)
		ecmd->supported |= SUPPORTED_100baseT_Full;
	if (mii_status & BMSR_100HALF)
		ecmd->supported |= SUPPORTED_100baseT_Half;
	if (mii_status & BMSR_10FULL)
		ecmd->supported |= SUPPORTED_10baseT_Full;
	if (mii_status & BMSR_10HALF)
		ecmd->supported |= SUPPORTED_10baseT_Half;
	if (lp->Emac.Config.HasMii)
		ecmd->supported |= SUPPORTED_MII;
	else
		ecmd->supported &= (~SUPPORTED_MII);
	if (mii_status & BMSR_ANEGCAPABLE)
		ecmd->supported |= SUPPORTED_Autoneg;
	if (mii_status & BMSR_ANEGCOMPLETE) {
		ecmd->autoneg = AUTONEG_ENABLE;
		ecmd->advertising |= ADVERTISED_Autoneg;
		if ((mii_advControl & ADVERTISE_100FULL) ||
		    (mii_advControl & ADVERTISE_100HALF))
			ecmd->speed = SPEED_100;
		else
			ecmd->speed = SPEED_10;
	}
	else {
		ecmd->autoneg = AUTONEG_DISABLE;
		if (mii_cmd & BMCR_SPEED100)
			ecmd->speed = SPEED_100;
		else
			ecmd->speed = SPEED_10;
	}
	if (mii_advControl & ADVERTISE_10FULL)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (mii_advControl & ADVERTISE_10HALF)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (mii_advControl & ADVERTISE_100FULL)
		ecmd->advertising |= ADVERTISED_100baseT_Full;
	if (mii_advControl & ADVERTISE_100HALF)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	ecmd->advertising |= ADVERTISED_MII;
	ecmd->port = PORT_MII;
	ecmd->phy_address = lp->Emac.PhysAddress;
	ecmd->transceiver = XCVR_INTERNAL;
	if (XEmac_mIsSgDma(&lp->Emac)) {
		if ((ret =
		     XEmac_GetPktThreshold(&lp->Emac, XEM_SEND,
					   &threshold)) == XST_SUCCESS) {
			ecmd->maxtxpkt = threshold;
		}
		else
			return -EIO;
		if ((ret =
		     XEmac_GetPktThreshold(&lp->Emac, XEM_RECV,
					   &threshold)) == XST_SUCCESS) {
			ecmd->maxrxpkt = threshold;
		}
		else
			return -EIO;
	}
	return 0;
}

static int
xenet_ethtool_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	int ret;
	struct net_local *lp = (struct net_local *) dev->priv;
	u8 threshold;

	memset(ec, 0, sizeof(struct ethtool_coalesce));
	if ((ret =
	     XEmac_GetPktThreshold(&lp->Emac, XEM_RECV,
				   &threshold)) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_GetPktThreshold error %d\n", ret);
		return -EIO;
	}
	ec->rx_max_coalesced_frames = threshold;
	if ((ret =
	     XEmac_GetPktWaitBound(&lp->Emac, XEM_RECV,
				   &(ec->rx_coalesce_usecs))) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_GetPktWaitBound error %d\n", ret);
		return -EIO;
	}
	if ((ret =
	     XEmac_GetPktThreshold(&lp->Emac, XEM_SEND,
				   &threshold)) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_GetPktThreshold send error %d\n", ret);
		return -EIO;
	}
	ec->tx_max_coalesced_frames = threshold;
	if ((ret =
	     XEmac_GetPktWaitBound(&lp->Emac, XEM_SEND,
				   &(ec->tx_coalesce_usecs))) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_GetPktWaitBound send error %d\n", ret);
		return -EIO;
	}
	return 0;
}

static int
xenet_ethtool_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	int ret;
	struct net_local *lp = (struct net_local *) dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&reset_lock, flags);
	if ((ret = XEmac_Stop(&lp->Emac)) != XST_SUCCESS)
		return -EIO;
	if ((ret =
	     XEmac_SetPktThreshold(&lp->Emac, XEM_RECV,
				   ec->rx_max_coalesced_frames)) !=
	    XST_SUCCESS) {
		printk(KERN_INFO "XEmac_SetPktThreshold error %d\n", ret);
		return -EIO;
	}
	if ((ret =
	     XEmac_SetPktWaitBound(&lp->Emac, XEM_RECV,
				   ec->rx_coalesce_usecs)) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_SetPktWaitBound error %d\n", ret);
		return -EIO;
	}
	if ((ret =
	     XEmac_SetPktThreshold(&lp->Emac, XEM_SEND,
				   ec->tx_max_coalesced_frames)) !=
	    XST_SUCCESS) {
		printk(KERN_INFO "XEmac_SetPktThreshold send error %d\n", ret);
		return -EIO;
	}
	if ((ret =
	     XEmac_SetPktWaitBound(&lp->Emac, XEM_SEND,
				   ec->tx_coalesce_usecs)) != XST_SUCCESS) {
		printk(KERN_INFO "XEmac_SetPktWaitBound send error %d\n", ret);
		return -EIO;
	}
	if ((ret = XEmac_Start(&lp->Emac)) != XST_SUCCESS)
		return -EIO;
	spin_unlock_irqrestore(&reset_lock, flags);
	return 0;
}

static int
xenet_ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *ed)
{
	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strcpy(ed->driver, DRIVER_NAME);
	strcpy(ed->version, DRIVER_VERSION);
	return 0;
}

static int
xenet_ethtool_get_ringparam(struct net_device *dev,
			    struct ethtool_ringparam *erp)
{
	memset(erp, 0, sizeof(struct ethtool_ringparam));
	erp->rx_max_pending = XEM_DFT_RECV_DESC;
	erp->tx_max_pending = XEM_DFT_SEND_DESC;
	erp->rx_pending = XEM_DFT_RECV_DESC;
	erp->tx_pending = XEM_DFT_SEND_DESC;
	return 0;
}

#define EMAG_REGS_N 32
struct mac_regsDump {
	struct ethtool_regs hd;
	u16 data[EMAG_REGS_N];
};

static void
xenet_ethtool_get_regs(struct net_device *dev, struct ethtool_regs *regs,
		       void *ret)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct mac_regsDump *dump = (struct mac_regsDump *) regs;
	int i;
	int r;

	dump->hd.version = 0;
	dump->hd.len = EMAG_REGS_N * sizeof(dump->data);
	for (i = 0; i < EMAG_REGS_N; i++) {
		if ((r =
		     XEmac_PhyRead(&(lp->Emac), lp->mii_addr, i,
				   &(dump->data[i]))) != XST_SUCCESS) {
			printk(KERN_INFO "PhyRead ERROR %d\n", r);
			*(int *) ret = -EIO;
			return;
		}
	}
	*(int *) ret = 0;
}

static int xenet_do_ethtool_ioctl(struct net_device *dev, struct ifreq *rq)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	struct ethtool_cmd ecmd;
	struct ethtool_coalesce eco;
	struct ethtool_drvinfo edrv;
	struct ethtool_ringparam erp;
	struct ethtool_pauseparam epp;
	struct mac_regsDump regs;
	int ret = -EOPNOTSUPP;
	int result;
	u32 Options;
	u16 mii_reg_sset;
	u16 mii_reg_spause;
	u16 mii_reg_autoneg;
	unsigned long flags;

	if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd.cmd)))
		return -EFAULT;
	switch (ecmd.cmd) {
	case ETHTOOL_GSET:
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret >= 0) {
			if (copy_to_user(rq->ifr_data, &ecmd, sizeof(ecmd)))
				ret = -EFAULT;
		}
		break;
	case ETHTOOL_SSET:
		if (copy_from_user
		    (&ecmd, rq->ifr_data, sizeof(struct ethtool_cmd)))
			return -EFAULT;
		mii_reg_sset = 0;
		if (ecmd.speed == SPEED_100)
			mii_reg_sset |= BMCR_SPEED100;
		if (ecmd.duplex == DUPLEX_FULL)
			mii_reg_sset |= BMCR_FULLDPLX;
		if (ecmd.autoneg == AUTONEG_ENABLE) {
			mii_reg_sset |= (BMCR_ANENABLE | BMCR_ANRESTART);
			spin_lock_irqsave(&reset_lock, flags);
			result = XEmac_PhyWrite(&lp->Emac, lp->mii_addr,
						MII_BMCR, mii_reg_sset);
			if (result != XST_SUCCESS) {
				spin_unlock_irqrestore(&reset_lock, flags);
				ret = -EIO;
				break;
			}
			result = XEmac_PhyRead(&lp->Emac, lp->mii_addr,
					       MII_ADVERTISE, &mii_reg_sset);
			if (result != XST_SUCCESS) {
				spin_unlock_irqrestore(&reset_lock, flags);
				ret = -EIO;
				break;
			}
			if (ecmd.speed == SPEED_100) {
				if (ecmd.duplex == DUPLEX_FULL) {
					mii_reg_sset |=
						(ADVERTISE_10FULL |
						 ADVERTISE_100FULL |
						 ADVERTISE_10HALF |
						 ADVERTISE_100HALF);
				}
				else {
					mii_reg_sset |=
						(ADVERTISE_10HALF |
						 ADVERTISE_100HALF);
					mii_reg_sset &=
						~(ADVERTISE_10FULL |
						  ADVERTISE_100FULL);
				}
			}
			else {
				if (ecmd.duplex == DUPLEX_FULL) {
					mii_reg_sset |=
						(ADVERTISE_10FULL |
						 ADVERTISE_10HALF);
					mii_reg_sset &=
						~(ADVERTISE_100FULL |
						  ADVERTISE_100HALF);
				}
				else {
					mii_reg_sset |= (ADVERTISE_10HALF);
					mii_reg_sset &=
						~(ADVERTISE_100FULL |
						  ADVERTISE_100HALF |
						  ADVERTISE_10FULL);
				}
			}
			result = XEmac_PhyWrite(&lp->Emac, lp->mii_addr,
						MII_ADVERTISE, mii_reg_sset);
			spin_unlock_irqrestore(&reset_lock, flags);
			if (result != XST_SUCCESS) {
				ret = -EIO;
				break;
			}
		}
		else {
			mii_reg_sset &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
			if (ecmd.duplex == DUPLEX_FULL) {
				mii_reg_sset |= BMCR_FULLDPLX;
			}
			else {
				mii_reg_sset &= ~BMCR_FULLDPLX;
			}
			if (ecmd.speed == SPEED_100) {
				mii_reg_sset |= BMCR_SPEED100;
			}
			else {
				mii_reg_sset &= ~BMCR_SPEED100;
			}
			spin_lock_irqsave(&reset_lock, flags);
			result = XEmac_PhyWrite(&lp->Emac, lp->mii_addr,
						MII_BMCR, mii_reg_sset);
			spin_unlock_irqrestore(&reset_lock, flags);
			if (result != XST_SUCCESS) {
				ret = -EIO;
				break;
			}
		}
		ret = 0;
		break;
	case ETHTOOL_GPAUSEPARAM:
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret < 0) {
			break;
		}
		epp.cmd = ecmd.cmd;
		epp.autoneg = ecmd.autoneg;
		Options = XEmac_GetOptions(&lp->Emac);
		if (Options & XEM_INSERT_PAD_OPTION) {
			epp.rx_pause = 1;
			epp.tx_pause = 1;
		}
		else {
			epp.rx_pause = 0;
			epp.tx_pause = 0;
		}
		if (copy_to_user
		    (rq->ifr_data, &epp, sizeof(struct ethtool_pauseparam)))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	case ETHTOOL_SPAUSEPARAM:
		if (copy_from_user
		    (&epp, rq->ifr_data, sizeof(struct ethtool_pauseparam)))
			return -EFAULT;
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret < 0) {
			break;
		}
		epp.cmd = ecmd.cmd;
		mii_reg_spause = 0;
		if (epp.autoneg == AUTONEG_ENABLE) {
			mii_reg_spause |= (BMCR_ANENABLE | BMCR_ANRESTART);
		}
		else {
			if (ecmd.speed == SPEED_100)
				mii_reg_spause |= BMCR_SPEED100;
			if (ecmd.duplex == DUPLEX_FULL)
				mii_reg_spause |= BMCR_FULLDPLX;
		}
		spin_lock_irqsave(&reset_lock, flags);
		result = XEmac_PhyWrite(&lp->Emac, lp->mii_addr,
					MII_BMCR, mii_reg_spause);
		spin_unlock_irqrestore(&reset_lock, flags);
		if (result != XST_SUCCESS) {
			ret = -EIO;
			break;
		}
		if (epp.rx_pause != epp.tx_pause) {
			ret = 0;
			break;
		}
		else {
			spin_lock_irqsave(&reset_lock, flags);
			(void) XEmac_Stop(&(lp->Emac));
			Options = XEmac_GetOptions(&lp->Emac);
			if (epp.rx_pause)
				Options |= XEM_INSERT_PAD_OPTION;
			else
				Options &= ~XEM_INSERT_PAD_OPTION;
			(void) XEmac_SetOptions(&lp->Emac, Options);
			(void) XEmac_Start(&(lp->Emac));
			spin_unlock_irqrestore(&reset_lock, flags);
		}
		ret = 0;
		break;
	case ETHTOOL_GCOALESCE:
		eco.cmd = ecmd.cmd;
		ret = xenet_ethtool_get_coalesce(dev, &eco);
		if (ret >= 0) {
			if (copy_to_user
			    (rq->ifr_data, &eco,
			     sizeof(struct ethtool_coalesce)))
				ret = -EFAULT;
		}
		break;
	case ETHTOOL_SCOALESCE:
		if (copy_from_user
		    (&eco, rq->ifr_data, sizeof(struct ethtool_coalesce)))
			return -EFAULT;
		ret = xenet_ethtool_set_coalesce(dev, &eco);
		break;
	case ETHTOOL_GDRVINFO:
		edrv.cmd = edrv.cmd;
		ret = xenet_ethtool_get_drvinfo(dev, &edrv);
		if (ret >= 0) {
			if (copy_to_user
			    (rq->ifr_data, &edrv,
			     sizeof(struct ethtool_drvinfo)))
				ret = -EFAULT;
		}
		break;
	case ETHTOOL_GREGS:
		regs.hd.cmd = edrv.cmd;
		xenet_ethtool_get_regs(dev, &(regs.hd), &ret);
		if (ret >= 0) {
			if (copy_to_user
			    (rq->ifr_data, &regs, sizeof(struct mac_regsDump)))
				ret = -EFAULT;
		}
		break;
	case ETHTOOL_GRINGPARAM:
		erp.cmd = edrv.cmd;
		ret = xenet_ethtool_get_ringparam(dev, &(erp));
		if (ret >= 0) {
			if (copy_to_user
			    (rq->ifr_data, &erp,
			     sizeof(struct ethtool_ringparam)))
				ret = -EFAULT;
		}
		break;
	case ETHTOOL_NWAY_RST:
		epp.cmd = ecmd.cmd;
		mii_reg_autoneg = 0;
		mii_reg_autoneg |= (BMCR_ANENABLE | BMCR_ANRESTART);
		spin_lock_irqsave(&reset_lock, flags);
		result = XEmac_PhyWrite(&lp->Emac, lp->mii_addr,
					MII_BMCR, mii_reg_autoneg);
		spin_unlock_irqrestore(&reset_lock, flags);
		if (result != XST_SUCCESS) {
			ret = -EIO;
			break;
		}
		ret = 0;
		break;
	default:
		break;
	}
	return ret;
}

static int xenet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	/* mii_ioctl_data has 4 u16 fields: phy_id, reg_num, val_in & val_out */
	struct mii_ioctl_data *data = (struct mii_ioctl_data *) &rq->ifr_data;
	struct {
		__u8 threshold;
		__u32 direction;
	} thr_arg;
	struct {
		__u32 waitbound;
		__u32 direction;
	} wbnd_arg;
	int ret;
	unsigned long flags;

	int Result;

	switch (cmd) {
	case SIOCETHTOOL:
		return xenet_do_ethtool_ioctl(dev, rq);
	case SIOCGMIIPHY:	/* Get address of MII PHY in use. */
	case SIOCDEVPRIVATE:	/* for binary compat, remove in 2.5 */
		data->phy_id = lp->mii_addr;
		/* Fall Through */

	case SIOCGMIIREG:	/* Read MII PHY register. */
	case SIOCDEVPRIVATE + 1:	/* for binary compat, remove in 2.5 */
		if (data->phy_id > 31 || data->reg_num > 31)
			return -ENXIO;

		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);
		spin_lock_irqsave(&reset_lock, flags);
		Result = XEmac_PhyRead(&lp->Emac, data->phy_id,
				       data->reg_num, &data->val_out);
		/* Start the PHY timer up again. */
		spin_unlock_irqrestore(&reset_lock, flags);
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);

		if (Result != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: Could not read from PHY, error=%d.\n",
			       dev->name, Result);
			return (Result == XST_EMAC_MII_BUSY) ? -EBUSY : -EIO;
		}
		return 0;

	case SIOCSMIIREG:	/* Write MII PHY register. */
	case SIOCDEVPRIVATE + 2:	/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (data->phy_id > 31 || data->reg_num > 31)
			return -ENXIO;

		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);
		spin_lock_irqsave(&reset_lock, flags);
		Result = XEmac_PhyWrite(&lp->Emac, data->phy_id,
					data->reg_num, data->val_in);
		spin_unlock_irqrestore(&reset_lock, flags);
		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);

		if (Result != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: Could not write to PHY, error=%d.\n",
			       dev->name, Result);
			return (Result == XST_EMAC_MII_BUSY) ? -EBUSY : -EIO;
		}
		return 0;

	case SIOCDEVPRIVATE + 3:	/* set THRESHOLD */
		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg))) {
			return -EFAULT;
		}
		spin_lock_irqsave(&reset_lock, flags);
		if ((ret = XEmac_Stop(&lp->Emac)) != XST_SUCCESS) {
			return -EIO;
		}
		if ((ret =
		     XEmac_SetPktThreshold(&lp->Emac, thr_arg.direction,
					   thr_arg.threshold)) != XST_SUCCESS) {
			return -EIO;
		}
		if ((ret = XEmac_Start(&lp->Emac)) != XST_SUCCESS) {
			return -EIO;
		}
		spin_unlock_irqrestore(&reset_lock, flags);
		return 0;

	case SIOCDEVPRIVATE + 4:	/* set WAITBOUND */
		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg))) {
			return -EFAULT;
		}
		spin_lock_irqsave(&reset_lock, flags);
		if ((ret = XEmac_Stop(&lp->Emac)) != XST_SUCCESS) {
			return -EIO;
		}
		if ((ret =
		     XEmac_SetPktWaitBound(&lp->Emac, wbnd_arg.direction,
					   wbnd_arg.waitbound)) !=
		    XST_SUCCESS) {
			return -EIO;
		}
		if ((ret = XEmac_Start(&lp->Emac)) != XST_SUCCESS) {
			return -EIO;
		}
		spin_unlock_irqrestore(&reset_lock, flags);
		return 0;

	case SIOCDEVPRIVATE + 5:	/* get THRESHOLD */
		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg))) {
			return -EFAULT;
		}
		if ((ret =
		     XEmac_GetPktThreshold(&lp->Emac, thr_arg.direction,
					   &(thr_arg.threshold))) !=
		    XST_SUCCESS) {
			return -EIO;
		}
		if (copy_to_user(rq->ifr_data, &thr_arg, sizeof(thr_arg))) {
			return -EFAULT;
		}
		return 0;


	case SIOCDEVPRIVATE + 6:	/* get WAITBOUND */
		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg))) {
			return -EFAULT;
		}
		if ((ret =
		     XEmac_GetPktWaitBound(&lp->Emac, wbnd_arg.direction,
					   &(wbnd_arg.waitbound))) !=
		    XST_SUCCESS) {
			return -EIO;
		}
		if (copy_to_user(rq->ifr_data, &wbnd_arg, sizeof(wbnd_arg))) {
			return -EFAULT;
		}
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}


static void xenet_remove_ndev(struct net_device *ndev)
{
	if (ndev) {
		struct net_local *lp = netdev_priv(ndev);

		if (XEmac_mIsSgDma(&lp->Emac) && (lp->desc_space)) {
			free_descriptor_skb(ndev);
			dma_free_coherent(NULL,
					  lp->desc_space_size,
					  lp->desc_space,
					  lp->desc_space_handle);
		}

		if (lp->ddrVirtPtr) {
			kfree(lp->ddrVirtPtr);
		}

		iounmap((void *) (lp->Emac.BaseAddress));
		free_netdev(ndev);
	}
}

/** Shared device initialization code */
static int xenet_setup(
		struct device *dev,
		struct resource *r_mem,
		struct resource *r_irq,
		struct xemac_platform_data *pdata) {

	u32 virt_baddr;		/* virtual base address of emac */

	XEmac_Config Config;

	struct net_device *ndev = NULL;
	struct net_local *lp = NULL;

	int rc;

	u32 phy_addr;		/* used for scanning PHY address */
	u32 hwid;		/* used for informational HW ID output */

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev) {
		dev_err(dev, "XEmac: Could not allocate net device.\n");
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

	/* Setup the Config structure for the XEmac_CfgInitialize() call. */
	Config.BaseAddress	= r_mem->start;	/* Physical address */
	Config.IpIfDmaConfig	= pdata->dma_mode;
	Config.HasMii		= pdata->has_mii;
	Config.HasCam		= pdata->has_cam;
	Config.HasJumbo		= pdata->has_jumbo;
	Config.TxDre		= pdata->tx_dre;
	Config.RxDre		= pdata->rx_dre;
	Config.TxHwCsum		= pdata->tx_hw_csum;
	Config.RxHwCsum		= pdata->rx_hw_csum;


	/* Get the virtual base address for the device */
	virt_baddr = (u32) ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (0 == virt_baddr) {
		dev_err(dev, "XEmac: Could not allocate iomem.\n");
		rc = -EIO;
		goto error;
	}


	if (XEmac_CfgInitialize(&lp->Emac, &Config, virt_baddr) != XST_SUCCESS) {
		dev_err(dev, "XEmac: Could not initialize device.\n");
		rc = -ENODEV;
		goto error;
	}

	/* Set the MAC address */
	memcpy(ndev->dev_addr, pdata->mac_addr, 6);
	if (XEmac_SetMacAddress(&lp->Emac, ndev->dev_addr) != XST_SUCCESS) {
		/* should not fail right after an initialize */
		dev_err(dev, "XEmac: could not set MAC address.\n");
		rc = -EIO;
		goto error;
	}
	dev_info(dev,
			"MAC address is now %2x:%2x:%2x:%2x:%2x:%2x\n",
			pdata->mac_addr[0], pdata->mac_addr[1],
			pdata->mac_addr[2], pdata->mac_addr[3],
			pdata->mac_addr[4], pdata->mac_addr[5]);

	if (XEmac_mIsSgDma(&lp->Emac)) {
		int result;

		printk(KERN_ERR "XEmac: using sgDMA mode.\n");
		XEmac_SetSgSendHandler(&lp->Emac, ndev, SgSendHandler);
		XEmac_SetSgRecvHandler(&lp->Emac, ndev, SgRecvHandler);

		if (XEmac_mIsTxDre(&lp->Emac) == TRUE) {
			printk(KERN_INFO "XEmac: using TxDRE mode\n");
			ndev->hard_start_xmit = xenet_SgSendDre;
		}
		else {
			printk(KERN_INFO "XEmac: not using TxDRE mode\n");
			if (XEmac_mIsTxHwCsum(&lp->Emac)) {
				printk(KERN_ERR
				       "XEmac: HW CONFIGURATION ERROR, "
				       "Checksum offload without TX DRE!\n");
				rc = -EIO;
				goto error;
			}
			ndev->hard_start_xmit = xenet_SgSend;
		}
		if (XEmac_mIsRxDre(&lp->Emac) == TRUE) {
			printk(KERN_INFO "XEmac: using RxDRE mode\n");
		}
		else {
			printk(KERN_INFO "XEmac: not using RxDRE mode\n");
		}

		if (XEmac_mIsTxHwCsum(&lp->Emac)) {
			printk(KERN_ERR
			       "XEmac: TX Checksum offload Mode enabled.\n");
		}

		if (XEmac_mIsRxHwCsum(&lp->Emac)) {
			printk(KERN_ERR
			       "XEmac: RX Checksum offload Mode enabled.\n");
		}


		/* Set up Interrupt handler.
		 */
		lp->Isr = XEmac_IntrHandlerDma;

		/* Set up SG DMA descriptors.
		 */
		result = descriptor_init(ndev);
		if (result) {
			rc = -EIO;
			goto error;
		}

		/* set the packet threshold and waitbound */
		XEmac_SetPktThreshold(&lp->Emac, XEM_SEND, 31);
		XEmac_SetPktThreshold(&lp->Emac, XEM_RECV, 31);
		(void) XEmac_SetPktWaitBound(&lp->Emac, XEM_SEND, 5);
		(void) XEmac_SetPktWaitBound(&lp->Emac, XEM_RECV, 5);


		/* disable SGEND interrupt and enable stripping of FCS and PAD */
		XEmac_SetOptions(&lp->Emac, XEmac_GetOptions(&lp->Emac) |
				 (XEM_NO_SGEND_INT_OPTION
				  /*| XEM_STRIP_PAD_FCS_OPTION */ ));

	}
	else {
		printk(KERN_ERR "XEmac: using fifo mode.\n");
		XEmac_SetFifoRecvHandler(&lp->Emac, ndev, FifoRecvHandler);
		XEmac_SetFifoSendHandler(&lp->Emac, ndev, FifoSendHandler);
		ndev->hard_start_xmit = xenet_FifoSend;
		lp->Isr = XEmac_IntrHandlerFifo;
	}
	XEmac_SetErrorHandler(&lp->Emac, ndev, ErrorHandler);


	/* Scan for the PHY.
	 */
	lp->mii_addr = 0xFF;
	for (phy_addr = 0; phy_addr < 31; phy_addr++) {
		int Result;
		u16 reg;
		int repeat;

		Result = XEmac_PhyRead(&lp->Emac, phy_addr, MII_PHYSID1, &reg);
		if (XST_SUCCESS != Result) {
			continue;
		}

		/* Even if we get here, we can not be sure that we actually found a PHY
		 * at this address. Once in a while, XEmac_PhyRead() returns
		 * XST_SUCCESS for reads at non-exsiting PHY addresses. This seems to
		 * be a bug in the hardware.  Fortunately, we can work around this
		 * problem by reading the PHY register several times and checking that
		 * we always get the same value. If we do, it is VERY likely that a PHY
		 * exists at that address.
		 */
		for (repeat = 0; repeat < 4; repeat++) {
			u16 check_reg;

			Result = XEmac_PhyRead(&lp->Emac, phy_addr, MII_PHYSID1,
					       &check_reg);
			if ((XST_SUCCESS != Result) || (check_reg != reg)) {
				break;
			}
		}

		/* If we read the same value 4 times, we can be pretty sure that there
		 * is a PHY out there at this address.
		 */
		if (4 == repeat) {
			u16 rev;

			/* Reading the revision # after successfully reading the
			 * manufacturer ID should NEVER fail. If it does, something really
			 * bad is happening.
			 */
			Result = XEmac_PhyRead(&lp->Emac, phy_addr, MII_PHYSID2,
					       &rev);
			if (XST_SUCCESS != Result) {
				printk("XEmac: Error reading PHY revision for PHY at address %d! " "THIS SHOULD NOT HAPPEN! Ignoring this PHY.\n", phy_addr);
				continue;
			}

			/* If we get here, we know that we successfully detected a PHY.
			 */
			printk("XEmac: Detected PHY at address %d, ManufID 0x%04x, Rev. 0x%04x.\n", phy_addr, reg, rev);
			lp->mii_addr = phy_addr;
			break;
		}
		/* If we get here, the repeated reads returned inconsistent results ->
		 * No PHY.
		 */
	}
	if (lp->mii_addr == 0xFF) {
		printk(KERN_WARNING
		       "XEmac: No PHY detected. Assuming PHY at address 0.\n");
		lp->mii_addr = 0;
	}

	/* initialize the netdev structure */
	ndev->open = xenet_open;
	ndev->stop = xenet_close;
	ndev->change_mtu = xenet_change_mtu;
	ndev->get_stats = xenet_get_stats;
	ndev->set_multicast_list = xenet_set_multicast_list;
	ndev->do_ioctl = xenet_ioctl;
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->flags &= ~IFF_MULTICAST;
	ndev->tx_timeout = xenet_tx_timeout;
	ndev->features = NETIF_F_SG | NETIF_F_FRAGLIST;

	if ((XEmac_mIsTxHwCsum(&lp->Emac)) && (XEmac_mIsTxDre(&lp->Emac))) {
		ndev->features |= NETIF_F_IP_CSUM;
	}

	/* Finally, register the device.
	 */
	rc = register_netdev(ndev);
	if (rc) {
		printk(KERN_ERR
		       "%s: Cannot register net device, aborting.\n",
		       ndev->name);
		goto error;	/* rc is already set here... */
	}

	printk(KERN_INFO
               "%s: Xilinx 10/100 EMAC at 0x%08X mapped to 0x%08X, irq=%d\n",
               ndev->name,
               lp->Emac.PhysAddress,
               lp->Emac.BaseAddress, ndev->irq);

	/* print h/w id  */
	hwid = XIo_In32((lp->Emac).BaseAddress + XEM_EMIR_OFFSET);

	printk(KERN_INFO
	       "%s: XEmac id %d.%d%c, block id %d, type %d\n",
	       ndev->name, (hwid >> 28) & 0xf, (hwid >> 21) & 0x7f,
	       ((hwid >> 16) & 0x1f) + 'a', (hwid >> 16) & 0xff,
	       (hwid >> 0) & 0xff);

	return 0;

error:
	return rc;
}

static int xenet_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	unregister_netdev(ndev);
	xenet_remove_ndev(ndev);

        release_mem_region(ndev->mem_start, ndev->mem_end-ndev->mem_start+1);

	free_netdev(ndev);

	dev_set_drvdata(dev, NULL);

	return 0;		/* success */
}


static int xenet_probe(struct device *dev)
{
	struct resource *r_irq = NULL;	/* Interrupt resources */
	struct resource *r_mem = NULL;	/* IO mem resources */
	struct xemac_platform_data *pdata;
	struct platform_device *pdev = to_platform_device(dev);

	/* param check */
	if (!pdev) {
		printk(KERN_ERR
		       "XEmac: Internal error. Probe called with NULL param.\n");
		return -ENODEV;
	}

	pdata = (struct xemac_platform_data *) pdev->dev.platform_data;
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

        return xenet_setup(dev, r_mem, r_irq, pdata);
}

static struct device_driver xenet_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,

	.probe = xenet_probe,
	.remove = xenet_remove
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

static int __devinit xenet_of_probe(struct of_device *ofdev, const struct of_device_id *match)
{
	struct xemac_platform_data pdata_struct;
	struct resource r_irq_struct;
	struct resource r_mem_struct;

	struct resource *r_irq = &r_irq_struct;	/* Interrupt resources */
	struct resource *r_mem = &r_mem_struct;	/* IO mem resources */
	struct xemac_platform_data *pdata = &pdata_struct;
	int rc = 0;

	printk(KERN_ERR "Device Tree Probing \'%s\'\n",
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

	pdata_struct.dma_mode           = get_u32(ofdev, "xlnx,dma-present");
	pdata_struct.has_mii		= get_u32(ofdev, "xlnx,mii-exist");
	pdata_struct.has_cam		= get_u32(ofdev, "xlnx,cam-exist");
	pdata_struct.has_err_cnt	= get_u32(ofdev, "xlnx,err-count-exist");
	pdata_struct.has_jumbo		= get_u32(ofdev, "xlnx,jumbo-exist");
	pdata_struct.tx_dre		= get_u32(ofdev, "xlnx,tx-dre-type");
	pdata_struct.rx_dre		= get_u32(ofdev, "xlnx,rx-dre-type");
	pdata_struct.tx_hw_csum		= get_u32(ofdev, "xlnx,tx-include-csum");
	pdata_struct.rx_hw_csum		= get_u32(ofdev, "xlnx,rx-include-csum");
	memcpy(pdata_struct.mac_addr, of_get_mac_address(ofdev->node), 6);

        return xenet_setup(&ofdev->dev, r_mem, r_irq, pdata);
}

static int __devexit xenet_of_remove(struct of_device *dev)
{
	return xenet_remove(&dev->dev);
}

static struct of_device_id xenet_of_match[] = {
	{ .compatible = "opb_ethernet", },
	{ .compatible = "plb_ethernet", },
	{ .compatible = "emac", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xenet_of_match);

static struct of_platform_driver xenet_of_driver = {
	.name		= DRIVER_NAME,
	.match_table	= xenet_of_match,
	.probe		= xenet_of_probe,
	.remove		= __devexit_p(xenet_of_remove),
};
#endif

static int __init xenet_init(void)
{
	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	int status = driver_register(&xenet_driver);
#ifdef CONFIG_OF
	status |= of_register_platform_driver(&xenet_of_driver);
#endif
	return status;
}

static void __exit xenet_cleanup(void)
{
	driver_unregister(&xenet_driver);
#ifdef CONFIG_OF
	of_unregister_platform_driver(&xenet_of_driver);
#endif
}

module_init(xenet_init);
module_exit(xenet_cleanup);
