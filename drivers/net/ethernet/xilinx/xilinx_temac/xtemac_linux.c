/*
 * Xilinx Ethernet Linux component to interface XTemac component to Linux
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.1.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a xd   12/12/05 First release
 * 2.00a jvb  12/21/05 Added support for checksum offload, and receive side DRE
 * 2.00b wgr  08/17/06 Port to kernel 2.6.10_mvl401.
 * 2.00c rpm  12/12/06 Updated PHY address detection code, as well as PHY
 *			autonegotiation support (still not great - but better). Changed
 *			XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT to MARVELL_88E1111.
 * </pre>
 *
 */

/*
 * This driver is a bit unusual in that it is composed of two logical
 * parts where one part is the OS independent code and the other part is
 * the OS dependent code.  Xilinx provides their drivers split in this
 * fashion.  This file represents the Linux OS dependent part known as
 * the Linux adapter.  The other files in this directory are the OS
 * independent files as provided by Xilinx with no changes made to them.
 * The names exported by those files begin with XTemac_.  All functions
 * in this file that are called by Linux have names that begin with
 * xenet_.  The functions in this file that have Handler in their name
 * are registered as callbacks with the underlying Xilinx OS independent
 * layer.  Any other functions are static helper functions.
 */

/*
 * With the way the hardened PLB Temac works, the driver needs to communicate
 * with the PHY controller. Since each board will have a different
 * type of PHY, the code that communicates with the MII type controller
 * is inside #ifdef MARVELL_88E1111_PHY conditional
 * compilation. For your specific board, you will want to replace this code with
 * code of your own for your specific board.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/xilinx_devices.h>
#include <asm/io.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "xbasic_types.h"
#include "xtemac.h"
#include "xipif_v1_23_b.h"
#include "xpacket_fifo_v2_00_a.h"
#include "xdmav3.h"
#include "xdmabdv3.h"


#define LOCAL_FEATURE_RX_CSUM   0x01
#define LOCAL_FEATURE_RX_DRE    0x02

/*
 * Default SEND and RECV buffer descriptors (BD) numbers.
 * BD Space needed is (XTE_SEND_BD_CNT+XTE_RECV_BD_CNT)*Sizeof(XDmaBdV3).
 * Each XDmaBdV3 instance currently takes 40 bytes.
 */
#define XTE_SEND_BD_CNT 256
#define XTE_RECV_BD_CNT 256

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME         "xilinx_temac"
#define DRIVER_DESCRIPTION  "Xilinx Tri-Mode Ethernet MAC driver"
#define DRIVER_VERSION      "2.00b"

#define TX_TIMEOUT   (3*HZ)	/* Transmission timeout is 3 seconds. */

/*
 * When Xilinx TEMAC is configured to use the TX Data Realignment Engine (DRE),
 * alignment restrictions are as follows:
 *   - SGDMA transmit buffers can be aligned on any boundary, but receive buffers
 *     must be aligned on a 8-byte boundary.
 *
 * Without TX DRE, buffer alignment restrictions are as follows:
 *   - SGDMA transmit and receive buffers must be aligned on a 8-byte boundary
 *
 * There are no alignment restrictions when using XTemac_FifoRead() and
 * XTemac_FifoWrite().
 *
 */
/*
 * ALIGNMENT_RECV = the alignement required to receive (8 required by plb bus w/no DRE)
 * ALIGNMENT_SEND = the alignement required to send (8 required by plb bus w/no DRE)
 * ALIGNMENT_SEND_PERF = tx alignment for better performance
 *
 * ALIGNMENT_SEND is used to see if we *need* to copy the data to re-align.
 * ALIGNMENT_SEND_PERF is used if we've decided we need to copy anyway, we just
 * copy to this alignment for better performance.
 */

#define ALIGNMENT_RECV          32
#define ALIGNMENT_SEND          8
#define ALIGNMENT_SEND_PERF     32


/* SGDMA buffer descriptors must be aligned on a 8-byte boundary. */
#define ALIGNMENT_BD            4

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGNSEND(adr) ((ALIGNMENT_SEND - ((u32) adr)) % ALIGNMENT_SEND)
#define BUFFER_ALIGNSEND_PERF(adr) ((ALIGNMENT_SEND_PERF - ((u32) adr)) % ALIGNMENT_SEND_PERF)
#define BUFFER_ALIGNRECV(adr) ((ALIGNMENT_RECV - ((u32) adr)) % ALIGNMENT_RECV)

/* Default TX/RX Threshold and waitbound values for SGDMA mode */
#define DFT_TX_THRESHOLD  16
#define DFT_TX_WAITBOUND  1
#define DFT_RX_THRESHOLD  2
#define DFT_RX_WAITBOUND  1

#define XTE_AUTOSTRIPPING 1

/* Put Buffer Descriptors in BRAM?
 * NOTE:
 *   Putting BDs in BRAM only works if there is only ONE instance of the TEMAC
 *   in hardware.  The code does not handle multiple instances, e.g. it does
 *   not manage the memory in BRAM.
 */
#define BD_IN_BRAM        0
#define BRAM_BASEADDR     0xffff8000

/*
 * Our private per device data.  When a net_device is allocated we will
 * ask for enough extra space for this.
 */
struct net_local {
	struct list_head rcv;
	struct list_head xmit;

	struct net_device *ndev;	/* this device */
	struct net_device *next_dev;	/* The next device in dev_list */
	struct net_device_stats stats;	/* Statistics for this device */
	struct timer_list phy_timer;	/* PHY monitoring timer */

	u32 index;		/* Which interface is this */
	XInterruptHandler Isr;	/* Pointer to the XTemac ISR routine */
	u8 gmii_addr;		/* The GMII address of the PHY */

	/* The underlying OS independent code needs space as well.  A
	 * pointer to the following XTemac structure will be passed to
	 * any XTemac_ function that requires it.  However, we treat the
	 * data as an opaque object in this file (meaning that we never
	 * reference any of the fields inside of the structure). */
	XTemac Emac;

	unsigned int max_frame_size;

	int cur_speed;

	/* Buffer Descriptor space for both TX and RX BD ring */
	void *desc_space;	/* virtual address of BD space */
	dma_addr_t desc_space_handle;	/* physical address of BD space */
	int desc_space_size;	/* size of BD space */

	/* buffer for one skb in case no room is available for transmission */
	struct sk_buff *deferred_skb;

	/* send buffers for non tx-dre hw */
	void **tx_orig_buffers;	/* Buffer addresses as returned by
				   dma_alloc_coherent() */
	void **tx_buffers;	/* Buffers addresses aligned for DMA */
	dma_addr_t *tx_phys_buffers;	/* Buffer addresses in physical memory */
	size_t tx_buffers_cur;	/* Index of current buffer used */

	/* stats */
	int max_frags_in_a_packet;
	unsigned long realignments;
	unsigned long tx_hw_csums;
	unsigned long rx_hw_csums;
	unsigned long local_features;
#if ! XTE_AUTOSTRIPPING
	unsigned long stripping;
#endif
};

/* for exclusion of all program flows (processes, ISRs and BHs) */
spinlock_t XTE_spinlock;
spinlock_t XTE_tx_spinlock;
spinlock_t XTE_rx_spinlock;

/*
 * ethtool has a status reporting feature where we can report any sort of
 * status information we'd like. This is the list of strings used for that
 * status reporting. ETH_GSTRING_LEN is defined in ethtool.h
 */
static char xenet_ethtool_gstrings_stats[][ETH_GSTRING_LEN] = {
	"txdmaerr", "txpfifoerr", "txstatuserr", "rxrejerr", "rxdmaerr",
	"rxpfifoerror", "fifoerr", "ipiferr", "intr",
	"max_frags", "tx_hw_csums", "rx_hw_csums",
};

#define XENET_STATS_LEN sizeof(xenet_ethtool_gstrings_stats) / ETH_GSTRING_LEN

/* Helper function to determine if a given XTemac error warrants a reset. */
extern inline int status_requires_reset(int s)
{
	return (s == XST_FIFO_ERROR ||
		s == XST_PFIFO_DEADLOCK ||
		s == XST_DMA_ERROR || s == XST_IPIF_ERROR);
}

/* BH statics */
static LIST_HEAD(receivedQueue);
static spinlock_t receivedQueueSpin = __SPIN_LOCK_UNLOCKED(receivedQueueSpin);

static LIST_HEAD(sentQueue);
static spinlock_t sentQueueSpin = __SPIN_LOCK_UNLOCKED(sentQueueSpin);

/* from mii.h
 *
 * Items in mii.h but not in gmii.h
 */
#define ADVERTISE_100FULL       0x0100
#define ADVERTISE_100HALF       0x0080
#define ADVERTISE_10FULL        0x0040
#define ADVERTISE_10HALF        0x0020
#define ADVERTISE_CSMA          0x0001

#define EX_ADVERTISE_1000FULL   0x0200
#define EX_ADVERTISE_1000HALF   0x0100

/*
 * items not in mii.h nor gmii.h but should be
 */
#define MII_EXADVERTISE 0x09

typedef enum DUPLEX { UNKNOWN_DUPLEX, HALF_DUPLEX, FULL_DUPLEX } DUPLEX;

int renegotiate_speed(struct net_device *dev, int speed, DUPLEX duplex)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int status;
	int retries = 2;
	int wait_count;
	u16 phy_reg0 = BMCR_ANENABLE | BMCR_ANRESTART;
	u16 phy_reg1;
	u16 phy_reg4;
	u16 phy_reg9 = 0;


	/*
	 * It appears that the 10baset full and half duplex settings
	 * are overloaded for gigabit ethernet
	 */
	if ((duplex == FULL_DUPLEX) && (speed == 10)) {
		phy_reg4 = ADVERTISE_10FULL | ADVERTISE_CSMA;
	}
	else if ((duplex == FULL_DUPLEX) && (speed == 100)) {
		phy_reg4 = ADVERTISE_100FULL | ADVERTISE_CSMA;
	}
	else if ((duplex == FULL_DUPLEX) && (speed == 1000)) {
		phy_reg4 = ADVERTISE_CSMA;
		phy_reg9 = EX_ADVERTISE_1000FULL;
	}
	else if (speed == 10) {
		phy_reg4 = ADVERTISE_10HALF | ADVERTISE_CSMA;
	}
	else if (speed == 100) {
		phy_reg4 = ADVERTISE_100HALF | ADVERTISE_CSMA;
	}
	else if (speed == 1000) {
		phy_reg4 = ADVERTISE_CSMA;
		phy_reg9 = EX_ADVERTISE_1000HALF;
	}
	else {
		printk(KERN_ERR
		       "%s: XTemac: unsupported speed requested: %d\n",
		       dev->name, speed);
		return -1;
	}

	/*
	 * link status in register 1:
	 * first read / second read:
	 * 0               0           link is down
	 * 0               1           link is up (but it was down earlier)
	 * 1               0           link is down (but it was just up)
	 * 1               1           link is up
	 *
	 */
	status = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg1);
	status |= XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg1);
	status |=
		XTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_ADVERTISE,
				phy_reg4);
	status |=
		XTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_EXADVERTISE,
				phy_reg9);
	if (status != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: error accessing PHY: %d\n", dev->name,
		       status);
		return -1;
	}

	while (retries--) {
		/* initiate an autonegotiation of the speed */
		status = XTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_BMCR,
					 phy_reg0);
		if (status != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: XTemac: error starting autonegotiateion: %d\n",
			       dev->name, status);
			return -1;
		}

		wait_count = 20;	/* so we don't loop forever */
		while (wait_count--) {
			/* wait a bit for the negotiation to complete */
			mdelay(500);
			status = XTemac_PhyRead(&lp->Emac, lp->gmii_addr,
						MII_BMSR, &phy_reg1);
			status |=
				XTemac_PhyRead(&lp->Emac, lp->gmii_addr,
					       MII_BMSR, &phy_reg1);
			if (status != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XTemac: error reading MII status %d\n",
				       dev->name, status);
				return -1;
			}
			if ((phy_reg1 & BMSR_LSTATUS) &&
			    (phy_reg1 & BMSR_ANEGCAPABLE))
				break;

		}

		if (phy_reg1 & BMSR_LSTATUS) {
			printk(KERN_INFO
			       "%s: XTemac: We renegotiated the speed to: %d\n",
			       dev->name, speed);
			return 0;
		}
		else {
			printk(KERN_ERR
			       "%s: XTemac: Not able to set the speed to %d (status: 0x%0x)\n",
			       dev->name, speed, phy_reg1);
			return -1;
		}
	}

	printk(KERN_ERR
	       "%s: XTemac: Not able to set the speed to %d\n", dev->name,
	       speed);
	return -1;
}

/* The following code tries to detect the MAC speed so that the silicon-
 * based TEMAC speed can be set to match. There is some PHY-specific code
 * that works with Marvel PHY (Xilinx ML4xx boards), or some more general
 * code that tries to start autonegotiation and detect the result. If you
 * don't like this or it doesn't work for you, change it or hardcode the speed.
 *
 * Note also a silicon issue with Xilinx V4FX with regards to MDIO access:
 * 	pre-CES4 chips (ML403, pre-production ML405/ML410)
 *		use hard_temac_v3_00_a
 *	CES4 or later chips (production ML405, ML410 boards)
 *		use hard_temac_v3_00_b
 */
#define MARVELL_88E1111_PHY

/*
 * This function sets up MAC's speed according to link speed of PHY
 * This function is specific to MARVELL 88E1111 PHY chip and assumes GMII
 * interface is being used by the TEMAC
 */
void set_mac_speed(struct net_local *lp)
{
	u16 phylinkspeed;
	struct net_device *dev = lp->ndev;
	int ret;

#ifndef MARVELL_88E1111_PHY
	int retry_count = 1;
#endif

	/* See comments at top for an explanation of MARVELL_88E1111_PHY */
#ifdef MARVELL_88E1111_PHY
#define MARVELL_88E1111_PHY_SPECIFIC_STATUS_REG_OFFSET  17
#define MARVELL_88E1111_LINKSPEED_MARK                  0xC000
#define MARVELL_88E1111_LINKSPEED_SHIFT                 14
#define MARVELL_88E1111_LINKSPEED_1000M                 0x0002
#define MARVELL_88E1111_LINKSPEED_100M                  0x0001
#define MARVELL_88E1111_LINKSPEED_10M                   0x0000
	u16 RegValue;

	/* Loop until read of PHY specific status register is successful. */
	do {
		ret = XTemac_PhyRead(&lp->Emac, lp->gmii_addr,
				     MARVELL_88E1111_PHY_SPECIFIC_STATUS_REG_OFFSET,
				     &RegValue);
	} while (ret != XST_SUCCESS);


	/* Get current link speed */
	phylinkspeed = (RegValue & MARVELL_88E1111_LINKSPEED_MARK)
		>> MARVELL_88E1111_LINKSPEED_SHIFT;

	/* Update TEMAC speed accordingly */
	switch (phylinkspeed) {
	case (MARVELL_88E1111_LINKSPEED_1000M):
		XTemac_SetOperatingSpeed(&lp->Emac, 1000);
		printk(KERN_INFO "%s: XTemac: speed set to 1000Mb/s\n",
		       dev->name);
		lp->cur_speed = 1000;
		break;
	case (MARVELL_88E1111_LINKSPEED_100M):
		XTemac_SetOperatingSpeed(&lp->Emac, 100);
		printk(KERN_INFO "%s: XTemac: speed set to 100Mb/s\n",
		       dev->name);
		lp->cur_speed = 100;
		break;
	case (MARVELL_88E1111_LINKSPEED_10M):
		XTemac_SetOperatingSpeed(&lp->Emac, 10);
		printk(KERN_INFO "%s: XTemac: speed set to 10Mb/s\n",
		       dev->name);
		lp->cur_speed = 10;
		break;
	default:
		XTemac_SetOperatingSpeed(&lp->Emac, 1000);
		printk(KERN_INFO "%s: XTemac: speed set to 1000Mb/s\n",
		       dev->name);
		lp->cur_speed = 1000;
		break;
	}

#else /* generic PHY */
	if (XTemac_mGetPhysicalInterface(&lp->Emac) == XTE_PHY_TYPE_MII) {
		phylinkspeed = 100;
	}
	else {
		phylinkspeed = 1000;
	}

	/*
	 * Try to renegotiate the speed until something sticks
	 */
	while (phylinkspeed > 1) {
		ret = renegotiate_speed(dev, phylinkspeed, FULL_DUPLEX);
		/*
		 * ret == 1 - try it again
		 * ret == 0 - it worked
		 * ret <  0 - there was some failure negotiating the speed
		 */
		if (ret == 0) {
			/* it worked, get out of the loop */
			break;
		}

		/* it didn't work this time, but it may work if we try again */
		if ((ret == 1) && (retry_count)) {
			retry_count--;
			printk("trying again...\n");
			continue;
		}
		/* reset the retry_count, becuase we're about to try a lower speed */
		retry_count = 1;
		phylinkspeed /= 10;
	}
	if (phylinkspeed == 1) {
		printk(KERN_INFO "%s: XTemac: could not negotiate speed\n",
		       dev->name);
		lp->cur_speed = 0;
		return;
	}

	XTemac_SetOperatingSpeed(&lp->Emac, phylinkspeed);
	printk(KERN_INFO "%s: XTemac: speed set to %dMb/s\n", dev->name,
	       phylinkspeed);
	lp->cur_speed = phylinkspeed;
#endif
}

/*
 * Helper function to reset the underlying hardware.  This is called
 * when we get into such deep trouble that we don't know how to handle
 * otherwise.
 */

/*
 * This reset function should handle five different reset request types
 * from other functions. The reset request types include
 *      1. FIFO error: FifoWrite()/FifoSend()/FifoRecv()/FifoRead() fails
 *      2. DMA error: SgAlloc()/SgCommit()/SgFree() fails
 *      3. DUPLEX error: MAC DUPLEX is not full duplex or does not match
 *                       PHY setting
 *      4. TX Timeout: Timeout occurs for a TX frame given to this adapter
 *      5. Error Status: Temac Error interrupt occurs and asks for a reset
 *
 */

static void reset(struct net_device *dev, u32 line_num)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u16 TxThreshold, TxWaitBound, RxThreshold, RxWaitBound;
	u32 Options;
	static u32 reset_cnt = 0;

	printk(KERN_INFO "%s: XTemac: resets (#%u) from code line %d\n",
	       dev->name, ++reset_cnt, line_num);

	/* Shouldn't really be necessary, but shouldn't hurt. */
	netif_stop_queue(dev);

	/* Stop device */
	XTemac_Stop(&lp->Emac);

	/*
	 * XTemac_Reset puts the device back to the default state.  We need
	 * to save all the settings we don't already know, reset, restore
	 * the settings, and then restart the temac.
	 */
	Options = XTemac_GetOptions(&lp->Emac);
	if (XTemac_mIsSgDma(&lp->Emac)) {
		/*
		 * The following two functions will return an error if we are
		 * not doing scatter-gather DMA.  We just checked that so we
		 * can safely ignore the return values.
		 */
		(int) XTemac_IntrSgCoalGet(&lp->Emac, XTE_RECV, &RxThreshold,
					   &RxWaitBound);
		(int) XTemac_IntrSgCoalGet(&lp->Emac, XTE_SEND, &TxThreshold,
					   &TxWaitBound);

	}

	/* now we can reset the device */
	XTemac_Reset(&lp->Emac, 0);

	/* Reset on TEMAC also resets PHY. Give it some time to finish negotiation
	 * before we move on */
	mdelay(2000);

	/*
	 * The following four functions will return an error if the
	 * EMAC is already started.  We just stopped it by calling
	 * XTemac_Reset() so we can safely ignore the return values.
	 */
	(int) XTemac_SetMacAddress(&lp->Emac, dev->dev_addr);
	(int) XTemac_SetOptions(&lp->Emac, Options);
	(int) XTemac_ClearOptions(&lp->Emac, ~Options);
	Options = XTemac_GetOptions(&lp->Emac);
	printk(KERN_INFO "%s: XTemac: Options: 0x%x\n", dev->name, Options);

	set_mac_speed(lp);

	if (XTemac_mIsSgDma(&lp->Emac)) {	/* SG DMA mode */
		/*
		 * The following 2 functions will return an error if
		 * we are not doing scatter-gather DMA or if the EMAC is
		 * already started.  We just checked that we are indeed
		 * doing scatter-gather and we just stopped the EMAC so
		 * we can safely ignore the return values.
		 */
		(int) XTemac_IntrSgCoalSet(&lp->Emac, XTE_RECV, RxThreshold,
					   RxWaitBound);
		(int) XTemac_IntrSgCoalSet(&lp->Emac, XTE_SEND, TxThreshold,
					   TxWaitBound);

		/* Enable both SEND and RECV interrupts */
		XTemac_IntrSgEnable(&lp->Emac, XTE_SEND | XTE_RECV);
	}
	else {			/* FIFO interrupt mode */
		XTemac_IntrFifoEnable(&lp->Emac, XTE_RECV | XTE_SEND);
	}

	if (lp->deferred_skb) {
		dev_kfree_skb_any(lp->deferred_skb);
		lp->deferred_skb = NULL;
		lp->stats.tx_errors++;
	}

	/*
	 * XTemac_Start returns an error when: if configured for
	 * scatter-gather DMA and a descriptor list has not yet been created
	 * for the send or receive channel, or if no receive buffer descriptors
	 * have been initialized. Those are not happening. so ignore the returned
	 * result checking.
	 */
	(int) XTemac_Start(&lp->Emac);

	/* We're all ready to go.  Start the queue in case it was stopped. */
	netif_wake_queue(dev);
}

/*
 * The PHY registers read here should be standard registers in all PHY chips
 */
static int get_phy_status(struct net_device *dev, DUPLEX * duplex, int *linkup)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u16 reg;
	int xs;

	xs = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &reg);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read PHY control register; error %d\n",
		       dev->name, xs);
		return -1;
	}
	*duplex = FULL_DUPLEX;

	xs = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &reg);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read PHY status register; error %d\n",
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
static void poll_gmii(unsigned long data)
{
	struct net_device *dev;
	struct net_local *lp;
	DUPLEX phy_duplex;
	int phy_carrier;
	int netif_carrier;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	dev = (struct net_device *) data;
	lp = (struct net_local *) dev->priv;

	/* First, find out what's going on with the PHY. */
	if (get_phy_status(dev, &phy_duplex, &phy_carrier)) {
		printk(KERN_ERR "%s: XTemac: terminating link monitoring.\n",
		       dev->name);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return;
	}

	netif_carrier = netif_carrier_ok(dev) != 0;

	if (phy_carrier != netif_carrier) {
		if (phy_carrier) {
			printk(KERN_INFO
			       "%s: XTemac: PHY Link carrier restored.\n",
			       dev->name);
			netif_carrier_on(dev);
		}
		else {
			printk(KERN_INFO "%s: XTemac: PHY Link carrier lost.\n",
			       dev->name);
			netif_carrier_off(dev);
		}
	}

	/* Set up the timer so we'll get called again in 2 seconds. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	add_timer(&lp->phy_timer);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

/*
 * This routine is registered with the OS as the function to call when
 * the TEMAC interrupts.  It in turn, calls the Xilinx OS independent
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
	struct net_local *lp;
	u32 Options;
	unsigned long flags;

	/*
	 * Just to be safe, stop TX queue and the device first.  If the device is
	 * already stopped, an error will be returned.  In this case, we don't
	 * really care.
	 */
	netif_stop_queue(dev);
	spin_lock_irqsave(&XTE_spinlock, flags);
	lp = (struct net_local *) dev->priv;
	XTemac_Stop(&lp->Emac);

	/* Set the MAC address each time opened. */
	if (XTemac_SetMacAddress(&lp->Emac, dev->dev_addr) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: could not set MAC address.\n",
		       dev->name);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return -EIO;
	}

	/*
	 * If the device is not configured for polled mode, connect to the
	 * interrupt controller and enable interrupts.  Currently, there
	 * isn't any code to set polled mode, so this check is probably
	 * superfluous.
	 */
	Options = XTemac_GetOptions(&lp->Emac);
	Options &= ~XTE_SGEND_INT_OPTION;
	Options &= ~XTE_REPORT_RXERR_OPTION;
	Options |= XTE_FLOW_CONTROL_OPTION;
	Options |= XTE_JUMBO_OPTION;
#if XTE_AUTOSTRIPPING
	Options |= XTE_FCS_STRIP_OPTION;
#endif

	(int) XTemac_SetOptions(&lp->Emac, Options);
	(int) XTemac_ClearOptions(&lp->Emac, ~Options);
	Options = XTemac_GetOptions(&lp->Emac);
	printk(KERN_INFO "%s: XTemac: Options: 0x%x\n", dev->name, Options);

	/* Register interrupt handler */
	if ((Options & XTE_POLLED_OPTION) == 0) {
		int retval;

		/* Grab the IRQ */
		retval = request_irq(dev->irq, &xenet_interrupt, 0, dev->name,
				     dev);
		if (retval) {
			printk(KERN_ERR
			       "%s: XTemac: could not allocate interrupt %d.\n",
			       dev->name, dev->irq);
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return retval;
		}
	}

	/* give the system enough time to establish a link */
	mdelay(2000);

	set_mac_speed(lp);

	INIT_LIST_HEAD(&(lp->rcv));
	INIT_LIST_HEAD(&(lp->xmit));

	/* Enable interrupts if not in polled mode */
	if ((Options & XTE_POLLED_OPTION) == 0) {
		if (!XTemac_mIsSgDma(&lp->Emac)) {	/*fifo direct interrupt driver mode */
			XTemac_IntrFifoEnable(&lp->Emac, XTE_RECV | XTE_SEND);
		}
		else {		/* SG DMA mode */
			XTemac_IntrSgEnable(&lp->Emac, XTE_SEND | XTE_RECV);
		}
	}

	/* Start TEMAC device */
	if (XTemac_Start(&lp->Emac) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: could not start device.\n",
		       dev->name);
		free_irq(dev->irq, dev);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	if (XTemac_mIsSgDma(&lp->Emac)) {
		u16 threshold_s, timer_s, threshold_r, timer_r;

		(int) XTemac_IntrSgCoalGet(&lp->Emac, XTE_SEND, &threshold_s,
					   &timer_s);
		(int) XTemac_IntrSgCoalGet(&lp->Emac, XTE_RECV, &threshold_r,
					   &timer_r);
		printk(KERN_INFO
		       "%s: XTemac: Send Threshold = %d, Receive Threshold = %d\n",
		       dev->name, threshold_s, threshold_r);
		printk(KERN_INFO
		       "%s: XTemac: Send Wait bound = %d, Receive Wait bound = %d\n",
		       dev->name, timer_s, timer_r);
	}

	/* We're ready to go. */
	netif_start_queue(dev);

	/* Set up the PHY monitoring timer. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	lp->phy_timer.data = (unsigned long) dev;
	lp->phy_timer.function = &poll_gmii;
	init_timer(&lp->phy_timer);
	add_timer(&lp->phy_timer);
	return 0;
}

static int xenet_close(struct net_device *dev)
{
	struct net_local *lp;
	unsigned long flags, flags_reset;

	spin_lock_irqsave(&XTE_spinlock, flags_reset);
	lp = (struct net_local *) dev->priv;

	/* Shut down the PHY monitoring timer. */
	del_timer_sync(&lp->phy_timer);

	/* Stop Send queue */
	netif_stop_queue(dev);

	/* Now we could stop the device */
	XTemac_Stop(&lp->Emac);

	/*
	 * If not in polled mode, free the interrupt.  Currently, there
	 * isn't any code to set polled mode, so this check is probably
	 * superfluous.
	 */
	if ((XTemac_GetOptions(&lp->Emac) & XTE_POLLED_OPTION) == 0)
		free_irq(dev->irq, dev);

	spin_unlock_irqrestore(&XTE_spinlock, flags_reset);

	spin_lock_irqsave(&receivedQueueSpin, flags);
	list_del(&(lp->rcv));
	spin_unlock_irqrestore(&receivedQueueSpin, flags);

	spin_lock_irqsave(&sentQueueSpin, flags);
	list_del(&(lp->xmit));
	spin_unlock_irqrestore(&sentQueueSpin, flags);

	return 0;
}

static struct net_device_stats *xenet_get_stats(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	return &lp->stats;
}

static int xenet_change_mtu(struct net_device *dev, int new_mtu)
{
#ifdef CONFIG_XILINX_GIGE_VLAN
	int head_size = XTE_HDR_VLAN_SIZE;
#else
	int head_size = XTE_HDR_SIZE;
#endif
	struct net_local *lp = (struct net_local *) dev->priv;
	int max_frame = new_mtu + head_size + XTE_TRL_SIZE;
	int min_frame = 1 + head_size + XTE_TRL_SIZE;

	if ((max_frame < min_frame) || (max_frame > lp->max_frame_size))
		return -EINVAL;

	dev->mtu = new_mtu;	/* change mtu in net_device structure */
	return 0;
}

static int xenet_FifoSend(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp;
	unsigned int len;
	int result;
	unsigned long flags, fifo_free_bytes;

	/* The following lock is used to protect GetFreeBytes, FifoWrite
	 * and FifoSend sequence which could happen from FifoSendHandler
	 * or other processor in SMP case.
	 */
	spin_lock_irqsave(&XTE_tx_spinlock, flags);
	lp = (struct net_local *) dev->priv;
	len = skb->len;

	fifo_free_bytes = XTemac_FifoGetFreeBytes(&lp->Emac, XTE_SEND);
	if (fifo_free_bytes < len) {
		netif_stop_queue(dev);	/* stop send queue */
		lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
					   it in interrupt context */
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
		return 0;
	}

	/* Write frame data to FIFO */
	result = XTemac_FifoWrite(&lp->Emac, (void *) skb->data, len,
				  XTE_END_OF_PACKET);
	if (result != XST_SUCCESS) {
		reset(dev, __LINE__);
		lp->stats.tx_errors++;
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
		return -EIO;
	}

	/* Initiate transmit */
	if ((result = XTemac_FifoSend(&lp->Emac, len)) != XST_SUCCESS) {
		reset(dev, __LINE__);
		lp->stats.tx_errors++;
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
		return -EIO;
	}
	lp->stats.tx_bytes += len;
	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);

	dev_kfree_skb(skb);	/* free skb */
	dev->trans_start = jiffies;
	return 0;
}

/* Callback function for completed frames sent in FIFO interrupt driven mode */
static void FifoSendHandler(void *CallbackRef)
{
	struct net_device *dev;
	struct net_local *lp;
	int result;
	struct sk_buff *skb;

	spin_lock(&XTE_tx_spinlock);
	dev = (struct net_device *) CallbackRef;
	lp = (struct net_local *) dev->priv;
	lp->stats.tx_packets++;

	/*Send out the deferred skb and wake up send queue if a deferred skb exists */
	if (lp->deferred_skb) {

		skb = lp->deferred_skb;
		/* If no room for the deferred packet, return */
		if (XTemac_FifoGetFreeBytes(&lp->Emac, XTE_SEND) < skb->len) {
			spin_unlock(&XTE_tx_spinlock);
			return;
		}

		/* Write frame data to FIFO */
		result = XTemac_FifoWrite(&lp->Emac, (void *) skb->data,
					  skb->len, XTE_END_OF_PACKET);
		if (result != XST_SUCCESS) {
			reset(dev, __LINE__);
			lp->stats.tx_errors++;
			spin_unlock(&XTE_tx_spinlock);
			return;
		}

		/* Initiate transmit */
		if ((result =
		     XTemac_FifoSend(&lp->Emac, skb->len)) != XST_SUCCESS) {
			reset(dev, __LINE__);
			lp->stats.tx_errors++;
			spin_unlock(&XTE_tx_spinlock);
			return;
		}

		dev_kfree_skb_irq(skb);
		lp->deferred_skb = NULL;
		lp->stats.tx_packets++;
		lp->stats.tx_bytes += skb->len;
		dev->trans_start = jiffies;
		netif_wake_queue(dev);	/* wake up send queue */
	}
	spin_unlock(&XTE_tx_spinlock);
}

#if 0
/*
 * These are used for debugging purposes, left here in case they are useful
 * for further debugging
 */
static unsigned int _xenet_tx_csum(struct sk_buff *skb)
{
	unsigned int csum = 0;
	long csstart = skb->h.raw - skb->data;

	if (csstart != skb->len) {
		csum = skb_checksum(skb, csstart, skb->len - csstart, 0);
	}

	return csum;
}

static inline unsigned int _xenet_rx_csum(struct sk_buff *skb)
{
	return skb_checksum(skb, 0, skb->len, 0);
}
#endif

/*
 * xenet_SgSend_internal is an internal use, send routine.
 * Any locks that need to be acquired, should be acquired
 * prior to calling this routine.
 */
static int xenet_SgSend_internal(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp;
	XDmaBdV3 *bd_ptr;
	int result;
	int total_frags;
	int i;
	void *virt_addr;
	size_t len;
	dma_addr_t phy_addr;
	XDmaBdV3 *first_bd_ptr;
	skb_frag_t *frag;

	lp = (struct net_local *) dev->priv;

	/* get skb_shinfo(skb)->nr_frags + 1 buffer descriptors */
	total_frags = skb_shinfo(skb)->nr_frags + 1;

	/* stats */
	if (lp->max_frags_in_a_packet < total_frags) {
		lp->max_frags_in_a_packet = total_frags;
	}

	if (total_frags < XTE_SEND_BD_CNT) {
		result = XTemac_SgAlloc(&lp->Emac, XTE_SEND, total_frags,
					&bd_ptr);

		if (result != XST_SUCCESS) {
			netif_stop_queue(dev);	/* stop send queue */
			lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
						   it in interrupt context */
			return result;
		}
	}
	else {
		dev_kfree_skb(skb);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XTemac: could not send TX socket buffers (too many fragments).\n",
		       dev->name);
		return XST_FAILURE;
	}

	len = skb_headlen(skb);

	/* get the physical address of the header */
	phy_addr = (u32) dma_map_single(NULL, skb->data, len, DMA_TO_DEVICE);

	/* get the header fragment, it's in the skb differently */
	XDmaBdV3_mSetBufAddrLow(bd_ptr, phy_addr);
	XDmaBdV3_mSetLength(bd_ptr, len);
	XDmaBdV3_mSetId(bd_ptr, skb);
	XDmaBdV3_mClearLast(bd_ptr);

	/*
	 * if tx checksum offloading is enabled, when the ethernet stack
	 * wants us to perform the checksum in hardware,
	 * skb->ip_summed is CHECKSUM_PARTIAL. Otherwise skb->ip_summed is
	 * CHECKSUM_NONE, meaning the checksum is already done, or
	 * CHECKSUM_UNNECESSARY, meaning checksumming is turned off (e.g.
	 * loopback interface)
	 *
	 * skb->csum is an overloaded value. On send, skb->csum is the offset
	 * into the buffer (skb->h.raw) to place the csum value. On receive
	 * this feild gets set to the actual csum value, before it's passed up
	 * the stack.
	 *
	 * When we get here, the ethernet stack above will have already
	 * computed the pseudoheader csum value and have placed it in the
	 * TCP/UDP header.
	 *
	 * The IP header csum has also already been computed and inserted.
	 *
	 * Since the IP header with it's own csum should compute to a null
	 * csum, it should be ok to include it in the hw csum. If it is decided
	 * to change this scheme, skb should be examined before dma_map_single()
	 * is called, which flushes the page from the cpu's cache.
	 *
	 * skb->data points to the beginning of the whole packet
	 * skb->h.raw points to the beginning of the ip header
	 *
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
#if 0
		{
			unsigned int csum = _xenet_tx_csum(skb);

			*((unsigned short *) (skb->h.raw + skb->csum)) =
				csum_fold(csum);
			XTemac_mSgSendBdCsumDisable(bd_ptr);
		}
#else
		XTemac_mSgSendBdCsumEnable(bd_ptr);
		XTemac_mSgSendBdCsumSetup(bd_ptr,
					  skb->transport_header - skb->data,
					  (skb->transport_header - skb->data) +
					  skb->csum);
#endif
		lp->tx_hw_csums++;
	}
	else {
		/*
		 * This routine will do no harm even if hardware checksum capability is
		 * off.
		 */
		XTemac_mSgSendBdCsumDisable(bd_ptr);
	}

	first_bd_ptr = bd_ptr;

	frag = &skb_shinfo(skb)->frags[0];

	for (i = 1; i < total_frags; i++, frag++) {
		bd_ptr = XTemac_mSgSendBdNext(&lp->Emac, bd_ptr);

		virt_addr =
			(void *) page_address(frag->page) + frag->page_offset;
		phy_addr =
			(u32) dma_map_single(NULL, virt_addr, frag->size,
					     DMA_TO_DEVICE);

		XDmaBdV3_mSetBufAddrLow(bd_ptr, phy_addr);
		XDmaBdV3_mSetLength(bd_ptr, frag->size);
		XDmaBdV3_mSetId(bd_ptr, NULL);

		if (i < (total_frags - 1)) {
			XDmaBdV3_mClearLast(bd_ptr);
		}
	}

	XDmaBdV3_mSetLast(bd_ptr);

	/* Enqueue to HW */
	result = XTemac_SgCommit(&lp->Emac, XTE_SEND, total_frags,
				 first_bd_ptr);
	if (result != XST_SUCCESS) {
		netif_stop_queue(dev);	/* stop send queue */
		dev_kfree_skb(skb);
		XDmaBdV3_mSetId(first_bd_ptr, NULL);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XTemac: could not send commit TX buffer descriptor (%d).\n",
		       dev->name, result);
		reset(dev, __LINE__);

		return XST_FAILURE;
	}

	dev->trans_start = jiffies;

	return XST_SUCCESS;
}

/* The send function for frames sent in SGDMA mode and TEMAC has TX DRE. */
static int xenet_SgSend(struct sk_buff *skb, struct net_device *dev)
{
	/* The following spin_lock protects
	 * SgAlloc, SgCommit sequence, which also exists in SgSendHandlerBH Bottom
	 * Half, or triggered by other processor in SMP case.
	 */
	spin_lock_bh(&XTE_tx_spinlock);

	xenet_SgSend_internal(skb, dev);

	spin_unlock_bh(&XTE_tx_spinlock);

	return 0;
}


/* The send function for frames sent in SGDMA mode (and no TX DRE is in TEMAC). */
static int xenet_SgSend_NoDRE(struct sk_buff *skb, struct net_device *dev)
{
	int result;

	void *tx_addr;
	void *cur_addr;
	dma_addr_t phy_addr;
	size_t len;

	XDmaBdV3 *bd_ptr;
	skb_frag_t *frag;
	int nr_frags;
	int total_frags;
	int i;

	struct net_local *lp = (struct net_local *) dev->priv;

	/* Without the DRE hardware engine, DMA transfers must be double word
	 * aligned (8 bytes), front and back. If there are no fragments, and the
	 * main chunk is aligned at the front, let the regular, SgSend handle it.
	 * Otherwise, just go ahead and copy the whole darn thing to the tx ring
	 * buffer before sending it out.
	 *
	 * For better performance the tx rign buffer alignment set in
	 * ALIGNMENT_SEND can be set to 32 which is cache line aligned, on the
	 * PPC405 and PPC440.
	 */
	if (!skb_is_nonlinear(skb) && (0 == BUFFER_ALIGNSEND(skb->data))) {
		/* buffer is linear and already aligned nicely. We can send it using
		 * xenet_SgSend(). Done.
		 */
		return xenet_SgSend(skb, dev);
	}

	/* The buffer is either nonlinear or not aligned. We have to copy it.
	 */
	nr_frags = skb_shinfo(skb)->nr_frags;
	total_frags = nr_frags + 1;

	/* stats */
	lp->realignments++;
	if (lp->max_frags_in_a_packet < total_frags) {
		lp->max_frags_in_a_packet = total_frags;
	}

	/* Copy the skb. Get the address of the next buffer in the ring. Also,
	 * remember the physical address of that buffer for the DMA setup.
	 */
	cur_addr = lp->tx_buffers[lp->tx_buffers_cur];
	phy_addr = lp->tx_phys_buffers[lp->tx_buffers_cur];

	/* set up tx_buffers_cur for the next use */
	lp->tx_buffers_cur++;
	if (lp->tx_buffers_cur >= XTE_SEND_BD_CNT) {
		lp->tx_buffers_cur = 0;
	}

	tx_addr = cur_addr;

	len = skb_headlen(skb);

	cacheable_memcpy(cur_addr, skb->data, len);
	cur_addr += len;

	frag = &skb_shinfo(skb)->frags[0];
	for (i = 1; i < nr_frags; i++, frag++) {
		void *p = (void *) page_address(frag->page) + frag->page_offset;

		len = frag->size;
		cacheable_memcpy(cur_addr, p, len);
		cur_addr += len;
	}

	/*
	 * set up the transfer
	 */
	result = XTemac_SgAlloc(&lp->Emac, XTE_SEND, 1, &bd_ptr);

	if (result != XST_SUCCESS) {
		netif_stop_queue(dev);	/* stop send queue */
		lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
					   it in interrupt context */
		return result;
	}

	/* get the header fragment, it's in the skb differently */
	XDmaBdV3_mSetBufAddrLow(bd_ptr, phy_addr);
	XDmaBdV3_mSetLength(bd_ptr, len);
	XDmaBdV3_mSetId(bd_ptr, skb);
	XDmaBdV3_mClearLast(bd_ptr);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/*
		 * skb->data points to the beginning of the whole packet
		 * skb->h.raw points to the beginning of the ip header
		 * skb->csum, on send, is the offset into the buffer (skb->h.raw)
		 * to place the csum value.
		 * tx_addr is the address where the data is really copied (for
		 * alignment)
		 */
		XTemac_mSgSendBdCsumEnable(bd_ptr);

		XTemac_mSgSendBdCsumSetup(bd_ptr,
					  (u32) (tx_addr +
						 (skb->transport_header -
						  skb->data)),
					  (u32) (tx_addr +
						 (skb->transport_header -
						  skb->data) + skb->csum));
		lp->tx_hw_csums++;
	}
	else {
		/*
		 * This routine will do no harm even if hardware checksum capability is
		 * off.
		 */
		XTemac_mSgSendBdCsumDisable(bd_ptr);
	}
	XDmaBdV3_mSetLast(bd_ptr);

	/* Enqueue to HW */
	result = XTemac_SgCommit(&lp->Emac, XTE_SEND, total_frags, bd_ptr);
	if (result != XST_SUCCESS) {
		netif_stop_queue(dev);	/* stop send queue */
		dev_kfree_skb(skb);
		XDmaBdV3_mSetId(bd_ptr, NULL);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XTemac: could not send commit TX buffer descriptor (%d).\n",
		       dev->name, result);
		reset(dev, __LINE__);

		return XST_FAILURE;
	}

	dev->trans_start = jiffies;

	return XST_SUCCESS;
}

/* The callback function for completed frames sent in SGDMA mode. */
static void SgSendHandlerBH(unsigned long p);
static void SgRecvHandlerBH(unsigned long p);

static DECLARE_TASKLET(SgSendBH, SgSendHandlerBH, 0);
static DECLARE_TASKLET(SgRecvBH, SgRecvHandlerBH, 0);

static void SgSendHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	XDmaBdV3 *BdPtr, *BdCurPtr;
	unsigned long len;
	unsigned long flags;
	struct sk_buff *skb;
	dma_addr_t skb_dma_addr;
	int result = XST_SUCCESS;
	unsigned int bd_processed, bd_processed_save;

	while (1) {
		spin_lock_irqsave(&sentQueueSpin, flags);
		if (list_empty(&sentQueue)) {
			spin_unlock_irqrestore(&sentQueueSpin, flags);
			break;
		}

		lp = list_entry(sentQueue.next, struct net_local, xmit);

		list_del_init(&(lp->xmit));
		spin_unlock_irqrestore(&sentQueueSpin, flags);

		spin_lock(&XTE_tx_spinlock);
		dev = lp->ndev;
		bd_processed_save = 0;
		while ((bd_processed =
			XTemac_SgGetProcessed(&lp->Emac, XTE_SEND,
					      XTE_SEND_BD_CNT, &BdPtr)) > 0) {

			bd_processed_save = bd_processed;
			BdCurPtr = BdPtr;
			do {
				len = XDmaBdV3_mGetLength(BdCurPtr);
				skb_dma_addr =
					(dma_addr_t)
					XDmaBdV3_mGetBufAddrLow(BdCurPtr);
				dma_unmap_single(NULL, skb_dma_addr, len,
						 DMA_TO_DEVICE);

				/* get ptr to skb */
				skb = (struct sk_buff *)
					XDmaBdV3_mGetId(BdCurPtr);
				if (skb)
					dev_kfree_skb(skb);

				/* reset BD id */
				XDmaBdV3_mSetId(BdCurPtr, NULL);

				lp->stats.tx_bytes += len;
				if (XDmaBdV3_mSetLast(&BdCurPtr)) {
					lp->stats.tx_packets++;
				}

				BdCurPtr =
					XTemac_mSgSendBdNext(&lp->Emac,
							     BdCurPtr);
				bd_processed--;
			} while (bd_processed > 0);

			result = XTemac_SgFree(&lp->Emac, XTE_SEND,
					       bd_processed_save, BdPtr);
			if (result != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XTemac: SgFree() error %d.\n",
				       dev->name, result);
				reset(dev, __LINE__);
				spin_unlock(&XTE_tx_spinlock);
				return;
			}
		}
		XTemac_IntrSgEnable(&lp->Emac, XTE_SEND);

		/* Send out the deferred skb if it exists */
		if ((lp->deferred_skb) && bd_processed_save) {
			skb = lp->deferred_skb;
			lp->deferred_skb = NULL;

			result = xenet_SgSend_internal(skb, dev);
		}

		if (result == XST_SUCCESS) {
			netif_wake_queue(dev);	/* wake up send queue */
		}
		spin_unlock(&XTE_tx_spinlock);
	}
}

static void SgSendHandler(void *CallBackRef)
{
	struct net_local *lp;
	struct list_head *cur_lp;

	spin_lock(&sentQueueSpin);

	lp = (struct net_local *) CallBackRef;
	list_for_each(cur_lp, &sentQueue) {
		if (cur_lp == &(lp->xmit)) {
			break;
		}
	}
	if (cur_lp != &(lp->xmit)) {
		list_add_tail(&lp->xmit, &sentQueue);
		XTemac_IntrSgDisable(&lp->Emac, XTE_SEND);
		tasklet_schedule(&SgSendBH);
	}
	spin_unlock(&sentQueueSpin);
}

static void xenet_tx_timeout(struct net_device *dev)
{
	struct net_local *lp;
	unsigned long flags;

	/*
	 * Make sure that no interrupts come in that could cause reentrancy
	 * problems in reset.
	 */
	spin_lock_irqsave(&XTE_tx_spinlock, flags);

	lp = (struct net_local *) dev->priv;
	printk(KERN_ERR
	       "%s: XTemac: exceeded transmit timeout of %lu ms.  Resetting emac.\n",
	       dev->name, TX_TIMEOUT * 1000UL / HZ);
	lp->stats.tx_errors++;

	reset(dev, __LINE__);

	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
}

/* The callback function for frames received when in FIFO mode. */
static void FifoRecvHandler(void *CallbackRef)
{
	struct net_device *dev;
	struct net_local *lp;
	struct sk_buff *skb;
	u32 len;
	int Result;

#define XTE_RX_SINK_BUFFER_SIZE 1024
	static u32 rx_buffer_sink[XTE_RX_SINK_BUFFER_SIZE / sizeof(u32)];

	spin_lock(&XTE_rx_spinlock);
	dev = (struct net_device *) CallbackRef;
	lp = (struct net_local *) dev->priv;

	Result = XTemac_FifoRecv(&lp->Emac, &len);
	if (Result != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read received packet length, error=%d.\n",
		       dev->name, Result);
		lp->stats.rx_errors++;
		reset(dev, __LINE__);
		spin_unlock(&XTE_rx_spinlock);
		return;
	}

	if (!(skb = /*dev_ */ alloc_skb(len + ALIGNMENT_RECV, GFP_ATOMIC))) {
		/* Couldn't get memory. */
		lp->stats.rx_dropped++;
		printk(KERN_ERR
		       "%s: XTemac: could not allocate receive buffer.\n",
		       dev->name);

		/* consume data in Xilinx TEMAC RX data fifo so it is sync with RX length fifo */
		for (; len > XTE_RX_SINK_BUFFER_SIZE;
		     len -= XTE_RX_SINK_BUFFER_SIZE) {
			XTemac_FifoRead(&lp->Emac, rx_buffer_sink,
					XTE_RX_SINK_BUFFER_SIZE,
					XTE_PARTIAL_PACKET);
		}
		XTemac_FifoRead(&lp->Emac, rx_buffer_sink, len,
				XTE_END_OF_PACKET);

		spin_unlock(&XTE_rx_spinlock);
		return;
	}

	/* Read the packet data */
	Result = XTemac_FifoRead(&lp->Emac, skb->data, len, XTE_END_OF_PACKET);
	if (Result != XST_SUCCESS) {
		lp->stats.rx_errors++;
		dev_kfree_skb_irq(skb);
		printk(KERN_ERR
		       "%s: XTemac: could not receive buffer, error=%d.\n",
		       dev->name, Result);
		reset(dev, __LINE__);
		spin_unlock(&XTE_rx_spinlock);
		return;
	}
	lp->stats.rx_packets++;
	lp->stats.rx_bytes += len;
	spin_unlock(&XTE_rx_spinlock);

	skb_put(skb, len);	/* Tell the skb how much data we got. */
	skb->dev = dev;		/* Fill out required meta-data. */
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	netif_rx(skb);		/* Send the packet upstream. */
}


/*
 * _xenet_SgSetupRecvBuffers allocates as many socket buffers (sk_buff's) as it
 * can up to the number of free RX buffer descriptors. Then it sets up the RX
 * buffer descriptors to DMA into the socket_buffers.
 *
 * The net_device, dev, indcates on which device to operate for buffer
 * descriptor allocation.
 */
static void _xenet_SgSetupRecvBuffers(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int free_bd_count = XDmaV3_mSgGetFreeCnt(&(lp->Emac.RecvDma));
	int num_sk_buffs;
	struct sk_buff_head sk_buff_list;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	XDmaBdV3 *BdPtr, *BdCurPtr;
	u32 align;
	int result;
	int align_max = ALIGNMENT_RECV;

	if (lp->local_features & LOCAL_FEATURE_RX_DRE) {
		align_max = 0;
	}

	skb_queue_head_init(&sk_buff_list);
	for (num_sk_buffs = 0; num_sk_buffs < free_bd_count; num_sk_buffs++) {
		new_skb = alloc_skb(lp->max_frame_size + align_max, GFP_ATOMIC);
		if (new_skb == NULL) {
			break;
		}
		/*
		 * I think the XTE_spinlock, and Recv DMA int disabled will protect this
		 * list as well, so we can use the __ version just fine
		 */
		__skb_queue_tail(&sk_buff_list, new_skb);
	}
	if (!num_sk_buffs) {
		printk(KERN_ERR "%s: XTemac: alloc_skb unsuccessful\n",
		       dev->name);
		return;
	}

	/* now we got a bunch o' sk_buffs */
	result = XTemac_SgAlloc(&lp->Emac, XTE_RECV, num_sk_buffs, &BdPtr);
	if (result != XST_SUCCESS) {
		/* we really shouldn't get this */
		skb_queue_purge(&sk_buff_list);
		printk(KERN_ERR "%s: XTemac: SgAlloc unsuccessful (%d)\n",
		       dev->name, result);
		reset(dev, __LINE__);
		return;
	}

	BdCurPtr = BdPtr;

	new_skb = skb_dequeue(&sk_buff_list);
	while (new_skb) {
		/* make sure we're long-word aligned */
		if (lp->local_features & LOCAL_FEATURE_RX_DRE) {
			align = BUFFER_ALIGNRECV(new_skb->data);
			if (align) {
				skb_reserve(new_skb, align);
			}
		}

		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(NULL, new_skb->data,
						     lp->max_frame_size,
						     DMA_FROM_DEVICE);

		XDmaBdV3_mSetBufAddrLow(BdCurPtr, new_skb_baddr);
		XDmaBdV3_mSetLength(BdCurPtr, lp->max_frame_size);
		XDmaBdV3_mSetId(BdCurPtr, new_skb);

		BdCurPtr = XTemac_mSgRecvBdNext(&lp->Emac, BdCurPtr);

		new_skb = skb_dequeue(&sk_buff_list);
	}

	/* enqueue RxBD with the attached skb buffers such that it is
	 * ready for frame reception */
	result = XTemac_SgCommit(&lp->Emac, XTE_RECV, num_sk_buffs, BdPtr);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: (SgSetupRecvBuffers) XTemac_SgCommit unsuccessful (%d)\n",
		       dev->name, result);
		skb_queue_purge(&sk_buff_list);
		BdCurPtr = BdPtr;
		while (num_sk_buffs > 0) {
			XDmaBdV3_mSetId(BdCurPtr, NULL);
			BdCurPtr = XTemac_mSgRecvBdNext(&lp->Emac, BdCurPtr);
			num_sk_buffs--;
		}
		reset(dev, __LINE__);
		return;
	}
}

static void SgRecvHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	struct sk_buff *skb;
	u32 len, skb_baddr;
	int result;
	unsigned long flags;
	XDmaBdV3 *BdPtr, *BdCurPtr;
	unsigned int bd_processed, bd_processed_saved;

	while (1) {
		spin_lock_irqsave(&receivedQueueSpin, flags);
		if (list_empty(&receivedQueue)) {
			spin_unlock_irqrestore(&receivedQueueSpin, flags);
			break;
		}
		lp = list_entry(receivedQueue.next, struct net_local, rcv);

		list_del_init(&(lp->rcv));
		dev = lp->ndev;
		spin_unlock_irqrestore(&receivedQueueSpin, flags);

		spin_lock(&XTE_rx_spinlock);
		if ((bd_processed =
		     XTemac_SgGetProcessed(&lp->Emac, XTE_RECV, XTE_RECV_BD_CNT,
					   &BdPtr)) > 0) {

			bd_processed_saved = bd_processed;
			BdCurPtr = BdPtr;
			do {
				len = XDmaBdV3_mGetLength(BdCurPtr);

				/* get ptr to skb */
				skb = (struct sk_buff *)
					XDmaBdV3_mGetId(BdCurPtr);

				/* get and free up dma handle used by skb->data */
				skb_baddr =
					(dma_addr_t)
					XDmaBdV3_mGetBufAddrLow(BdCurPtr);
				dma_unmap_single(NULL, skb_baddr,
						 lp->max_frame_size,
						 DMA_FROM_DEVICE);

				/* reset ID */
				XDmaBdV3_mSetId(BdCurPtr, NULL);

				/* setup received skb and send it upstream */
				skb_put(skb, len);	/* Tell the skb how much data we got. */
				skb->dev = dev;

				/* this routine adjusts skb->data to skip the header */
				skb->protocol = eth_type_trans(skb, dev);

				/* default the ip_summed value */
				skb->ip_summed = CHECKSUM_NONE;

				/* if we're doing rx csum offload, set it up */
				if (((lp->
				      local_features & LOCAL_FEATURE_RX_CSUM) !=
				     0) &&
				    (skb->protocol ==
				     __constant_htons(ETH_P_IP)) &&
				    (skb->len > 64)) {
					unsigned int csum;

					/*
					 * This hardware only supports proper checksum calculations
					 * on TCP/UDP packets.
					 *
					 * skb->csum is an overloaded value. On send, skb->csum is
					 * the offset into the buffer (skb->h.raw) to place the
					 * csum value. On receive this feild gets set to the actual
					 * csum value, before it's passed up the stack.
					 *
					 * If we set skb->ip_summed to CHECKSUM_PARTIAL, the ethernet
					 * stack above will compute the pseudoheader csum value and
					 * add it to the partial checksum already computed (to be
					 * placed in skb->csum) and verify it.
					 *
					 * Setting skb->ip_summed to CHECKSUM_NONE means that the
					 * cheksum didn't verify and the stack will (re)check it.
					 *
					 * Setting skb->ip_summed to CHECKSUM_UNNECESSARY means
					 * that the cheksum was verified/assumed to be good and the
					 * stack does not need to (re)check it.
					 *
					 * The ethernet stack above will (re)compute the checksum
					 * under the following conditions:
					 * 1) skb->ip_summed was set to CHECKSUM_NONE
					 * 2) skb->len does not match the length of the ethernet
					 *    packet determined by parsing the packet. In this case
					 *    the ethernet stack will assume any prior checksum
					 *    value was miscomputed and throw it away.
					 * 3) skb->ip_summed was set to CHECKSUM_PARTIAL, skb->csum was
					 *    set, but the result does not check out ok by the
					 *    ethernet stack.
					 *
					 * If the TEMAC hardware stripping feature is off, each
					 * packet will contain an FCS feild which will have been
					 * computed by the hardware checksum operation. This 4 byte
					 * FCS value needs to be subtracted back out of the checksum
					 * value computed by hardware as it's not included in a
					 * normal ethernet packet checksum.
					 *
					 * The minimum transfer packet size over the wire is 64
					 * bytes. If the packet is sent as exactly 64 bytes, then
					 * it probably contains some random padding bytes. It's
					 * somewhat difficult to determine the actual length of the
					 * real packet data, so we just let the stack recheck the
					 * checksum for us.
					 *
					 * After the call to eth_type_trans(), the following holds
					 * true:
					 *    skb->data points to the beginning of the ip header
					 */
					csum = XTemac_mSgRecvBdCsumGet
						(BdCurPtr);

#if ! XTE_AUTOSTRIPPING
					if (!lp->stripping) {
						/* take off the FCS */
						u16 *data;

						/* FCS is 4 bytes */
						skb_put(skb, -4);

						data = (u16 *) (&skb->
								data[skb->len]);

						/* subtract out the FCS from the csum value */
						csum = csum_sub(csum,
								*data
								/* & 0xffff */
								);
						data++;
						csum = csum_sub(csum,
								*data
								/* & 0xffff */
								);
					}
#endif
					skb->csum = csum;
					skb->ip_summed = CHECKSUM_PARTIAL;

					lp->rx_hw_csums++;
				}

				lp->stats.rx_packets++;
				lp->stats.rx_bytes += len;
				netif_rx(skb);	/* Send the packet upstream. */

				BdCurPtr =
					XTemac_mSgRecvBdNext(&lp->Emac,
							     BdCurPtr);
				bd_processed--;
			} while (bd_processed > 0);


			/* give the descriptor back to the driver */
			result = XTemac_SgFree(&lp->Emac, XTE_RECV,
					       bd_processed_saved, BdPtr);
			if (result != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XTemac: SgFree unsuccessful (%d)\n",
				       dev->name, result);
				reset(dev, __LINE__);
				spin_unlock(&XTE_rx_spinlock);
				return;
			}

			_xenet_SgSetupRecvBuffers(dev);
		}
		XTemac_IntrSgEnable(&lp->Emac, XTE_RECV);
		spin_unlock(&XTE_rx_spinlock);
	}
}

static void SgRecvHandler(void *CallBackRef)
{
	struct net_local *lp;
	struct list_head *cur_lp;

	spin_lock(&receivedQueueSpin);
	lp = (struct net_local *) CallBackRef;
	list_for_each(cur_lp, &receivedQueue) {
		if (cur_lp == &(lp->rcv)) {
			break;
		}
	}
	if (cur_lp != &(lp->rcv)) {
		list_add_tail(&lp->rcv, &receivedQueue);
		XTemac_IntrSgDisable(&lp->Emac, XTE_RECV);
		tasklet_schedule(&SgRecvBH);
	}
	spin_unlock(&receivedQueueSpin);
}

/* The callback function for errors. */
static void ErrorHandler(void *CallbackRef, int ErrClass, u32 Word1, u32 Word2)
{
	struct net_device *dev;
	struct net_local *lp;
	int need_reset;

	spin_lock(&XTE_spinlock);
	dev = (struct net_device *) CallbackRef;
	lp = (struct net_local *) dev->priv;

	need_reset = status_requires_reset(ErrClass);
	printk(KERN_ERR "%s: XTemac device error %d (%d, %d) %s\n",
	       dev->name, ErrClass, Word1, Word2,
	       need_reset ? ", resetting device." : "");

	if (need_reset)
		reset(dev, __LINE__);

	spin_unlock(&XTE_spinlock);
}

static int descriptor_init(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int recvsize, sendsize;
	int dftsize;
	u32 *recvpoolptr, *sendpoolptr;
	void *recvpoolphy, *sendpoolphy;
	int result;
	XDmaBdV3 bd_tx_template;
	XDmaBdV3 bd_rx_template;
	int XferType = XDMAV3_DMACR_TYPE_BFBURST_MASK;
	int XferWidth = XDMAV3_DMACR_DSIZE_64_MASK;

	/* calc size of descriptor space pool; alloc from non-cached memory */
	dftsize =
		XDmaV3_mSgListMemCalc(ALIGNMENT_BD,
				      XTE_RECV_BD_CNT + XTE_SEND_BD_CNT);
	printk(KERN_INFO "XTemac: buffer descriptor size: %d (0x%0x)\n",
	       dftsize, dftsize);

#if BD_IN_BRAM == 0
	lp->desc_space = dma_alloc_coherent(NULL, dftsize,
					    &lp->desc_space_handle, GFP_KERNEL);
#else
	lp->desc_space_handle = BRAM_BASEADDR;
	lp->desc_space = ioremap(lp->desc_space_handle, dftsize);
#endif
	if (lp->desc_space == 0) {
		return -1;
	}

	lp->desc_space_size = dftsize;

	printk(KERN_INFO
	       "XTemac: (buffer_descriptor_init) phy: 0x%x, virt: 0x%x, size: 0x%x\n",
	       lp->desc_space_handle, (unsigned int) lp->desc_space,
	       lp->desc_space_size);

	/* calc size of send and recv descriptor space */
	recvsize = XDmaV3_mSgListMemCalc(ALIGNMENT_BD, XTE_RECV_BD_CNT);
	sendsize = XDmaV3_mSgListMemCalc(ALIGNMENT_BD, XTE_SEND_BD_CNT);

	recvpoolptr = lp->desc_space;
	sendpoolptr = (void *) ((u32) lp->desc_space + recvsize);

	recvpoolphy = (void *) lp->desc_space_handle;
	sendpoolphy = (void *) ((u32) lp->desc_space_handle + recvsize);

	/* set up descriptor spaces using a template */

	/* rx template */
	/*
	 * Create the ring for Rx descriptors.
	 * The following attributes will be in effect for all RxBDs
	 */
	XDmaBdV3_mClear(&bd_rx_template);
	XDmaBdV3_mSetLast(&bd_rx_template);	/* 1:1 mapping of BDs to buffers */
	XDmaBdV3_mSetBufIncrement(&bd_rx_template);	/* Buffers exist along incrementing
							   addresses */
	XDmaBdV3_mSetBdPage(&bd_rx_template, 0);	/* Default to 32 bit addressing */
	XDmaBdV3_mSetBufAddrHigh(&bd_rx_template, 0);	/* Default to 32 bit addressing */
	XDmaBdV3_mSetDevSel(&bd_rx_template, 0);	/* Always 0 */
	XDmaBdV3_mSetTransferType(&bd_rx_template, XferType, XferWidth);	/* Data bus
										   attributes */


	/* tx template */
	/*
	 * Create the ring for Tx descriptors. If no Tx DRE then buffers must occupy
	 * a single descriptor, so set the "last" field for all descriptors.
	 */
	XDmaBdV3_mClear(&bd_tx_template);
	XDmaBdV3_mUseDre(&bd_tx_template);	/* Always use DRE if available */
	XDmaBdV3_mSetBufIncrement(&bd_tx_template);	/* Buffers exist along incrementing
							   addresses */
	XDmaBdV3_mSetBdPage(&bd_tx_template, 0);	/* Default to 32 bit addressing */
	XDmaBdV3_mSetBufAddrHigh(&bd_tx_template, 0);	/* Default to 32 bit addressing */
	XDmaBdV3_mSetDevSel(&bd_tx_template, 0);	/* Always 0 */
	XDmaBdV3_mSetTransferType(&bd_tx_template, XferType, XferWidth);	/* Data bus
										   attributes */
	XTemac_mSgSendBdCsumDisable(&bd_tx_template);	/* Disable csum offload by default */
	XTemac_mSgSendBdCsumSeed(&bd_tx_template, 0);	/* Don't need csum seed feature */

	if (XTemac_mIsTxDre(&lp->Emac) == FALSE) {
		XDmaBdV3_mSetLast(&bd_tx_template);
	}

	if ((result = XTemac_SgSetSpace(&lp->Emac, XTE_RECV, (u32) recvpoolphy,
					(u32) recvpoolptr, ALIGNMENT_BD,
					XTE_RECV_BD_CNT,
					&bd_rx_template)) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: SgSetSpace RECV ERROR %d\n",
		       dev->name, result);
		return -EIO;
	}

	if ((result = XTemac_SgSetSpace(&lp->Emac, XTE_SEND, (u32) sendpoolphy,
					(u32) sendpoolptr, ALIGNMENT_BD,
					XTE_SEND_BD_CNT,
					&bd_tx_template)) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: SgSetSpace SEND ERROR %d\n",
		       dev->name, result);
		return -EIO;
	}

	_xenet_SgSetupRecvBuffers(dev);
	return 0;
}

/*
 * If DRE is not enabled, allocate a ring buffer to use to aid in transferring
 * aligned packets for DMA.
 */
static int tx_ring_buffer_init(struct net_device *dev, unsigned max_frame_size)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	int idx;

	lp->tx_buffers_cur = -1;

	/* pre-initialize values. The error handling code relies on those. */
	lp->tx_buffers = NULL;
	lp->tx_orig_buffers = NULL;
	lp->tx_phys_buffers = NULL;
	idx = -1;

	if (XTemac_mIsTxDre(&lp->Emac) == FALSE) {
		/* Allocate the space for the buffer pointer array.
		 */
		lp->tx_orig_buffers = vmalloc(sizeof(void *) * XTE_SEND_BD_CNT);
		lp->tx_phys_buffers =
			vmalloc(sizeof(dma_addr_t) * XTE_SEND_BD_CNT);
		lp->tx_buffers = vmalloc(sizeof(void *) * XTE_SEND_BD_CNT);

		/* Handle allocation error
		 */
		if ((!lp->tx_orig_buffers) || (!lp->tx_buffers) ||
		    (!lp->tx_phys_buffers)) {
			printk(KERN_ERR
			       "XTemac: Could not vmalloc descriptor pointer arrays.\n");
			goto error;
		}

		/* Now, allocate the actual buffers.
		 */
		for (idx = 0; idx < XTE_SEND_BD_CNT; idx++) {
			lp->tx_orig_buffers[idx] = dma_alloc_coherent(NULL,
								      max_frame_size
								      +
								      ALIGNMENT_SEND_PERF,
								      &lp->
								      tx_phys_buffers
								      [idx],
								      GFP_KERNEL);
			/* Handle allocation error.
			 */
			if (!lp->tx_orig_buffers[idx]) {
				printk(KERN_ERR
				       "XTemac: Could not alloc TX buffer %d (%d bytes). "
				       "Cleaning up.\n", idx,
				       max_frame_size + ALIGNMENT_SEND_PERF);
				goto error;
			}

			lp->tx_buffers[idx] = lp->tx_orig_buffers[idx] +
				BUFFER_ALIGNSEND_PERF(lp->tx_orig_buffers[idx]);
		}
		lp->tx_buffers_cur = 0;
	}
	return 0;

      error:
	/* Check, if buffers have already been allocated.
	 */
	if (-1 != idx) {
		/* Yes, free them... Note, idx points to the failed allocation.
		 * Therefore the pre-decrement.
		 */
		while (--idx >= 0) {
			dma_free_coherent(NULL,
					  max_frame_size + ALIGNMENT_SEND_PERF,
					  lp->tx_orig_buffers[idx],
					  lp->tx_phys_buffers[idx]);
		}
	}

	/* Free allocated buffer pointer arrays if allocated.
	 */
	if (lp->tx_orig_buffers) {
		vfree(lp->tx_orig_buffers);
	}
	if (lp->tx_phys_buffers) {
		vfree(lp->tx_phys_buffers);
	}
	if (lp->tx_buffers) {
		vfree(lp->tx_buffers);
	}

	lp->tx_orig_buffers = NULL;
	lp->tx_phys_buffers = NULL;
	lp->tx_buffers = NULL;

	return 1;		/* 1 == general error */
}

static void free_descriptor_skb(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	XDmaBdV3 *BdPtr;
	struct sk_buff *skb;
	dma_addr_t skb_dma_addr;
	u32 len, i;

	/* Unmap and free skb's allocated and mapped in descriptor_init() */

	/* Get the virtual address of the 1st BD in the DMA RX BD ring */
	BdPtr = (XDmaBdV3 *) lp->Emac.RecvDma.BdRing.BaseAddr;

	for (i = 0; i < XTE_RECV_BD_CNT; i++) {
		skb = (struct sk_buff *) XDmaBdV3_mGetId(BdPtr);
		if (skb) {
			skb_dma_addr =
				(dma_addr_t) XDmaBdV3_mGetBufAddrLow(BdPtr);
			dma_unmap_single(NULL, skb_dma_addr, lp->max_frame_size,
					 DMA_FROM_DEVICE);
			dev_kfree_skb(skb);
		}
		/* find the next BD in the DMA RX BD ring */
		BdPtr = XTemac_mSgRecvBdNext(&lp->Emac, BdPtr);
	}

	/* Unmap and free TX skb's that have not had a chance to be freed
	 * in SgSendHandlerBH(). This could happen when TX Threshold is larger
	 * than 1 and TX waitbound is 0
	 */

	/* Get the virtual address of the 1st BD in the DMA TX BD ring */
	BdPtr = (XDmaBdV3 *) lp->Emac.SendDma.BdRing.BaseAddr;

	for (i = 0; i < XTE_SEND_BD_CNT; i++) {
		skb = (struct sk_buff *) XDmaBdV3_mGetId(BdPtr);
		if (skb) {
			skb_dma_addr =
				(dma_addr_t) XDmaBdV3_mGetBufAddrLow(BdPtr);
			len = XDmaBdV3_mGetLength(BdPtr);
			dma_unmap_single(NULL, skb_dma_addr, len,
					 DMA_TO_DEVICE);
			dev_kfree_skb(skb);
		}
		/* find the next BD in the DMA TX BD ring */
		BdPtr = XTemac_mSgSendBdNext(&lp->Emac, BdPtr);
	}

#if BD_IN_BRAM == 0
	dma_free_coherent(NULL,
			  lp->desc_space_size,
			  lp->desc_space, lp->desc_space_handle);
#else
	iounmap(lp->desc_space);
#endif
}

static int
xenet_ethtool_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	int ret;
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 mac_options;
	u16 threshold, timer;
	u16 gmii_cmd, gmii_status, gmii_advControl;
	int xs;

	memset(ecmd, 0, sizeof(struct ethtool_cmd));

	mac_options = XTemac_GetOptions(&(lp->Emac));
	xs = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &gmii_cmd);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read gmii command register; error %d\n",
		       dev->name, xs);
		return -1;
	}
	xs = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &gmii_status);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read gmii status register; error %d\n",
		       dev->name, xs);
		return -1;
	}

	xs = XTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_ADVERTISE,
			    &gmii_advControl);
	if (xs != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XTemac: could not read gmii advertisement control register; error %d\n",
		       dev->name, xs);
		return -1;
	}

	ecmd->duplex = DUPLEX_FULL;

	ecmd->supported |= SUPPORTED_MII;

	ecmd->port = PORT_MII;

	ecmd->speed = lp->cur_speed;

	if (gmii_status & BMSR_ANEGCAPABLE) {
		ecmd->supported |= SUPPORTED_Autoneg;
	}
	if (gmii_status & BMSR_ANEGCOMPLETE) {
		ecmd->autoneg = AUTONEG_ENABLE;
		ecmd->advertising |= ADVERTISED_Autoneg;
	}
	else {
		ecmd->autoneg = AUTONEG_DISABLE;
	}
	ecmd->phy_address = lp->Emac.BaseAddress;
	ecmd->transceiver = XCVR_INTERNAL;
	if (XTemac_mIsSgDma(&lp->Emac)) {
		/* get TX threshold */
		if ((ret =
		     XTemac_IntrSgCoalGet(&lp->Emac, XTE_SEND, &threshold,
					  &timer))
		    == XST_SUCCESS) {
			ecmd->maxtxpkt = threshold;
		}
		else {
			return -EIO;
		}

		/* get RX threshold */
		if ((ret =
		     XTemac_IntrSgCoalGet(&lp->Emac, XTE_RECV, &threshold,
					  &timer))
		    == XST_SUCCESS) {
			ecmd->maxrxpkt = threshold;
		}
		else {
			return -EIO;
		}
	}

	ecmd->supported |= SUPPORTED_10baseT_Full | SUPPORTED_100baseT_Full |
		SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg;

	return 0;
}

static int
xenet_ethtool_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	if ((ecmd->duplex != DUPLEX_FULL) ||
	    (ecmd->transceiver != XCVR_INTERNAL) ||
	    (ecmd->phy_address &&
	     (ecmd->phy_address != lp->Emac.BaseAddress))) {
		return -EOPNOTSUPP;
	}

	if ((ecmd->speed != 1000) && (ecmd->speed != 100) &&
	    (ecmd->speed != 10)) {
		printk(KERN_ERR
		       "%s: XTemac: xenet_ethtool_set_settings speed not supported: %d\n",
		       dev->name, ecmd->speed);
		return -EOPNOTSUPP;
	}

	if (ecmd->speed != lp->cur_speed) {
		renegotiate_speed(dev, ecmd->speed, FULL_DUPLEX);
		XTemac_SetOperatingSpeed(&lp->Emac, ecmd->speed);
		lp->cur_speed = ecmd->speed;
	}
	return 0;
}

static int
xenet_ethtool_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	int ret;
	struct net_local *lp = (struct net_local *) dev->priv;
	u16 threshold, waitbound;

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	if ((ret =
	     XTemac_IntrSgCoalGet(&lp->Emac, XTE_RECV, &threshold, &waitbound))
	    != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: IntrSgCoalGet error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	ec->rx_max_coalesced_frames = threshold;
	ec->rx_coalesce_usecs = waitbound;

	if ((ret =
	     XTemac_IntrSgCoalGet(&lp->Emac, XTE_SEND, &threshold, &waitbound))
	    != XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: IntrSgCoalGet error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	ec->tx_max_coalesced_frames = threshold;
	ec->tx_coalesce_usecs = waitbound;

	return 0;
}

#if 0
void disp_bd_ring(XDmaV3_BdRing bd_ring)
{
	int num_bds = bd_ring.AllCnt;
	u32 *cur_bd_ptr = bd_ring.BaseAddr;
	int idx;

	printk("PhysBaseAddr: %p\n", (void *) bd_ring.PhysBaseAddr);
	printk("BaseAddr: %p\n", (void *) bd_ring.BaseAddr);
	printk("HighAddr: %p\n", (void *) bd_ring.HighAddr);
	printk("Length: %d (0x%0x)\n", bd_ring.Length, bd_ring.Length);
	printk("RunState: %d (0x%0x)\n", bd_ring.RunState, bd_ring.RunState);
	printk("Separation: %d (0x%0x)\n", bd_ring.Separation,
	       bd_ring.Separation);
	printk("BD Count: %d\n", bd_ring.AllCnt);

	printk("\n");

	printk("FreeHead: %p\n", (void *) bd_ring.FreeHead);
	printk("PreHead: %p\n", (void *) bd_ring.PreHead);
	printk("HwHead: %p\n", (void *) bd_ring.HwHead);
	printk("HwTail: %p\n", (void *) bd_ring.HwTail);
	printk("PostHead: %p\n", (void *) bd_ring.PostHead);
	printk("BdaRestart: %p\n", (void *) bd_ring.BdaRestart);

	printk("Ring Contents:\n");
	printk("Idx     Addr    DMASR     LSBA      BDA   Length     USR0     USR1     USR5       ID\n");
	printk("--- -------- -------- -------- -------- -------- -------- -------- -------- --------\n");

	for (idx = 0; idx < num_bds; idx++) {
		printk("%3d %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		       idx, cur_bd_ptr,
		       cur_bd_ptr[XDMAV3_BD_DMASR_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_LSBA_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_BDA_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_LENGTH_OFFSET /
				  sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_USR0_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_USR1_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_USR5_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XDMAV3_BD_ID_OFFSET / sizeof(*cur_bd_ptr)]);

		cur_bd_ptr += bd_ring.Separation / sizeof(int);
	}
	printk("--------------------------------------- Done ---------------------------------------\n");
}
#endif

static int
xenet_ethtool_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	int ret;
	struct net_local *lp;
	unsigned long flags;
	int dev_started;

	spin_lock_irqsave(&XTE_spinlock, flags);
	lp = (struct net_local *) dev->priv;

	if ((dev_started = XTemac_mIsStarted(&lp->Emac)) == TRUE)
		XTemac_Stop(&lp->Emac);

	if ((ret = XTemac_IntrSgCoalSet(&lp->Emac, XTE_RECV,
					(u16) (ec->rx_max_coalesced_frames),
					(u16) (ec->rx_coalesce_usecs))) !=
	    XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: IntrSgCoalSet error %d\n",
		       dev->name, ret);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return -EIO;
	}

	if ((ret = XTemac_IntrSgCoalSet(&lp->Emac, XTE_SEND,
					(u16) (ec->tx_max_coalesced_frames),
					(u16) (ec->tx_coalesce_usecs))) !=
	    XST_SUCCESS) {
		printk(KERN_ERR "%s: XTemac: IntrSgCoalSet error %d\n",
		       dev->name, ret);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return -EIO;
	}

	if (dev_started == TRUE) {
		if ((ret = XTemac_Start(&lp->Emac)) != XST_SUCCESS) {
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return -EIO;
		}
	}

	spin_unlock_irqrestore(&XTE_spinlock, flags);
	return 0;
}

static int
xenet_ethtool_get_ringparam(struct net_device *dev,
			    struct ethtool_ringparam *erp)
{
	memset(erp, 0, sizeof(struct ethtool_ringparam));

	erp->rx_max_pending = XTE_RECV_BD_CNT;
	erp->tx_max_pending = XTE_SEND_BD_CNT;
	erp->rx_pending = XTE_RECV_BD_CNT;
	erp->tx_pending = XTE_SEND_BD_CNT;
	return 0;
}

#define EMAC_REGS_N 32
struct mac_regsDump {
	struct ethtool_regs hd;
	u16 data[EMAC_REGS_N];
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
	dump->hd.len = sizeof(dump->data);
	memset(dump->data, 0, sizeof(dump->data));

	for (i = 0; i < EMAC_REGS_N; i++) {
		if ((r =
		     XTemac_PhyRead(&(lp->Emac), lp->gmii_addr, i,
				    &(dump->data[i])))
		    != XST_SUCCESS) {
			printk(KERN_INFO "%s: XTemac: PhyRead ERROR %d\n",
			       dev->name, r);
			*(int *) ret = -EIO;
			return;
		}
	}

	*(int *) ret = 0;
}

static int
xenet_ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *ed)
{
	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strncpy(ed->driver, DRIVER_NAME, sizeof(ed->driver) - 1);
	strncpy(ed->version, DRIVER_VERSION, sizeof(ed->version) - 1);
	/* Also tell how much memory is needed for dumping register values */
	ed->regdump_len = sizeof(u16) * EMAC_REGS_N;
	return 0;
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
	unsigned long flags;
	int ret = -EOPNOTSUPP;
	u32 Options;
	XTemac_SoftStats stat;

	if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd)))
		return -EFAULT;
	switch (ecmd.cmd) {
	case ETHTOOL_GSET:	/* Get setting. No command option needed w/ ethtool */
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret < 0)
			return -EIO;
		if (copy_to_user(rq->ifr_data, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		ret = 0;
		break;
	case ETHTOOL_SSET:	/* Change setting. Use "-s" command option w/ ethtool */
		ret = xenet_ethtool_set_settings(dev, &ecmd);
		break;
	case ETHTOOL_GPAUSEPARAM:	/* Get pause parameter information. Use "-a" w/ ethtool */
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret < 0)
			return ret;
		epp.cmd = ecmd.cmd;
		epp.autoneg = ecmd.autoneg;
		Options = XTemac_GetOptions(&lp->Emac);
		if (Options & XTE_FCS_INSERT_OPTION) {
			epp.rx_pause = 1;
			epp.tx_pause = 1;
		}
		else {
			epp.rx_pause = 0;
			epp.tx_pause = 0;
		}
		if (copy_to_user
		    (rq->ifr_data, &epp, sizeof(struct ethtool_pauseparam)))
			return -EFAULT;
		ret = 0;
		break;
	case ETHTOOL_SPAUSEPARAM:	/* Set pause parameter. Use "-A" w/ ethtool */
		return -EOPNOTSUPP;	/* TODO: To support in next version */
	case ETHTOOL_GRXCSUM:{	/* Get rx csum offload info. Use "-k" w/ ethtool */
			struct ethtool_value edata = { ETHTOOL_GRXCSUM };

			edata.data =
				(lp->local_features & LOCAL_FEATURE_RX_CSUM) !=
				0;
			if (copy_to_user(rq->ifr_data, &edata, sizeof(edata)))
				return -EFAULT;
			ret = 0;
			break;
		}
	case ETHTOOL_SRXCSUM:{	/* Set rx csum offload info. Use "-K" w/ ethtool */
			struct ethtool_value edata;

			if (copy_from_user(&edata, rq->ifr_data, sizeof(edata)))
				return -EFAULT;

			spin_lock_irqsave(&XTE_spinlock, flags);
			if (edata.data) {
				if (XTemac_mIsRxCsum(&lp->Emac) == TRUE) {
					lp->local_features |=
						LOCAL_FEATURE_RX_CSUM;
				}
			}
			else {
				lp->local_features &= ~LOCAL_FEATURE_RX_CSUM;
			}
			spin_unlock_irqrestore(&XTE_spinlock, flags);

			ret = 0;
			break;
		}
	case ETHTOOL_GTXCSUM:{	/* Get tx csum offload info. Use "-k" w/ ethtool */
			struct ethtool_value edata = { ETHTOOL_GTXCSUM };

			edata.data = (dev->features & NETIF_F_IP_CSUM) != 0;
			if (copy_to_user(rq->ifr_data, &edata, sizeof(edata)))
				return -EFAULT;
			ret = 0;
			break;
		}
	case ETHTOOL_STXCSUM:{	/* Set tx csum offload info. Use "-K" w/ ethtool */
			struct ethtool_value edata;

			if (copy_from_user(&edata, rq->ifr_data, sizeof(edata)))
				return -EFAULT;

			if (edata.data) {
				if (XTemac_mIsTxCsum(&lp->Emac) == TRUE) {
					dev->features |= NETIF_F_IP_CSUM;
				}
			}
			else {
				dev->features &= ~NETIF_F_IP_CSUM;
			}

			ret = 0;
			break;
		}
	case ETHTOOL_GSG:{	/* Get ScatterGather info. Use "-k" w/ ethtool */
			struct ethtool_value edata = { ETHTOOL_GSG };

			edata.data = (dev->features & NETIF_F_SG) != 0;
			if (copy_to_user(rq->ifr_data, &edata, sizeof(edata)))
				return -EFAULT;
			ret = 0;
			break;
		}
	case ETHTOOL_SSG:{	/* Set ScatterGather info. Use "-K" w/ ethtool */
			struct ethtool_value edata;

			if (copy_from_user(&edata, rq->ifr_data, sizeof(edata)))
				return -EFAULT;

			if (edata.data) {
				if ((XTemac_mIsTxDre(&lp->Emac) == TRUE) &&
				    (XTemac_mIsSgDma(&lp->Emac) == TRUE)) {
					dev->features |=
						NETIF_F_SG | NETIF_F_FRAGLIST;
				}
			}
			else {
				dev->features &=
					~(NETIF_F_SG | NETIF_F_FRAGLIST);
			}

			ret = 0;
			break;
		}
	case ETHTOOL_GCOALESCE:	/* Get coalescing info. Use "-c" w/ ethtool */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			break;
		eco.cmd = ecmd.cmd;
		ret = xenet_ethtool_get_coalesce(dev, &eco);
		if (ret < 0) {
			return -EIO;
		}
		if (copy_to_user
		    (rq->ifr_data, &eco, sizeof(struct ethtool_coalesce))) {
			return -EFAULT;
		}
		ret = 0;
		break;
	case ETHTOOL_SCOALESCE:	/* Set coalescing info. Use "-C" w/ ethtool */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			break;
		if (copy_from_user
		    (&eco, rq->ifr_data, sizeof(struct ethtool_coalesce)))
			return -EFAULT;
		ret = xenet_ethtool_set_coalesce(dev, &eco);
		break;
	case ETHTOOL_GDRVINFO:	/* Get driver information. Use "-i" w/ ethtool */
		edrv.cmd = edrv.cmd;
		ret = xenet_ethtool_get_drvinfo(dev, &edrv);
		if (ret < 0) {
			return -EIO;
		}
		edrv.n_stats = XENET_STATS_LEN;
		if (copy_to_user
		    (rq->ifr_data, &edrv, sizeof(struct ethtool_drvinfo))) {
			return -EFAULT;
		}
		ret = 0;
		break;
	case ETHTOOL_GREGS:	/* Get register values. Use "-d" with ethtool */
		regs.hd.cmd = edrv.cmd;
		xenet_ethtool_get_regs(dev, &(regs.hd), &ret);
		if (ret < 0) {
			return ret;
		}
		if (copy_to_user
		    (rq->ifr_data, &regs, sizeof(struct mac_regsDump))) {
			return -EFAULT;
		}
		ret = 0;
		break;
	case ETHTOOL_GRINGPARAM:	/* Get RX/TX ring parameters. Use "-g" w/ ethtool */
		erp.cmd = edrv.cmd;
		ret = xenet_ethtool_get_ringparam(dev, &(erp));
		if (ret < 0) {
			return ret;
		}
		if (copy_to_user
		    (rq->ifr_data, &erp, sizeof(struct ethtool_ringparam))) {
			return -EFAULT;
		}
		ret = 0;
		break;
	case ETHTOOL_NWAY_RST:	/* Restart auto negotiation if enabled. Use "-r" w/ ethtool */
		return -EOPNOTSUPP;	/* TODO: To support in next version */
	case ETHTOOL_GSTRINGS:{
			struct ethtool_gstrings gstrings = { ETHTOOL_GSTRINGS };
			void *addr = rq->ifr_data;
			char *strings = NULL;

			if (copy_from_user(&gstrings, addr, sizeof(gstrings))) {
				return -EFAULT;
			}
			switch (gstrings.string_set) {
			case ETH_SS_STATS:
				gstrings.len = XENET_STATS_LEN;
				strings = *xenet_ethtool_gstrings_stats;
				break;
			default:
				return -EOPNOTSUPP;
			}
			if (copy_to_user(addr, &gstrings, sizeof(gstrings))) {
				return -EFAULT;
			}
			addr += offsetof(struct ethtool_gstrings, data);
			if (copy_to_user
			    (addr, strings, gstrings.len * ETH_GSTRING_LEN)) {
				return -EFAULT;
			}
			ret = 0;
			break;
		}
	case ETHTOOL_GSTATS:{
			struct {
				struct ethtool_stats cmd;
				uint64_t data[XENET_STATS_LEN];
			} stats = { {
			ETHTOOL_GSTATS, XENET_STATS_LEN}};

			XTemac_GetSoftStats(&lp->Emac, &stat);
			stats.data[0] = stat.TxDmaErrors;
			stats.data[1] = stat.TxPktFifoErrors;
			stats.data[2] = stat.TxStatusErrors;
			stats.data[3] = stat.RxRejectErrors;
			stats.data[4] = stat.RxDmaErrors;
			stats.data[5] = stat.RxPktFifoErrors;
			stats.data[6] = stat.FifoErrors;
			stats.data[7] = stat.IpifErrors;
			stats.data[8] = stat.Interrupts;
			stats.data[9] = lp->max_frags_in_a_packet;
			stats.data[10] = lp->tx_hw_csums;
			stats.data[11] = lp->rx_hw_csums;

			if (copy_to_user(rq->ifr_data, &stats, sizeof(stats))) {
				return -EFAULT;
			}
			ret = 0;
			break;
		}
	default:
		return -EOPNOTSUPP;	/* All other operations not supported */
	}
	return ret;
}

static int xenet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	/* gmii_ioctl_data has 4 u16 fields: phy_id, reg_num, val_in & val_out */
	struct mii_ioctl_data *data = (struct mii_ioctl_data *) &rq->ifr_data;
	struct {
		__u16 threshold;
		__u32 direction;
	} thr_arg;
	struct {
		__u16 waitbound;
		__u32 direction;
	} wbnd_arg;

	int ret;
	unsigned long flags;
	u16 threshold, timer;
	int dev_started;

	switch (cmd) {
	case SIOCETHTOOL:
		return xenet_do_ethtool_ioctl(dev, rq);
	case SIOCGMIIPHY:	/* Get address of GMII PHY in use. */
	case SIOCDEVPRIVATE:	/* for binary compat, remove in 2.5 */
		data->phy_id = lp->gmii_addr;
		/* Fall Through */

	case SIOCGMIIREG:	/* Read GMII PHY register. */
	case SIOCDEVPRIVATE + 1:	/* for binary compat, remove in 2.5 */
		if (data->phy_id > 31 || data->reg_num > 31)
			return -ENXIO;

		/* Stop the PHY timer to prevent reentrancy. */
		spin_lock_irqsave(&XTE_spinlock, flags);
		del_timer_sync(&lp->phy_timer);

		ret = XTemac_PhyRead(&lp->Emac, data->phy_id,
				     data->reg_num, &data->val_out);

		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		if (ret != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: XTemac: could not read from PHY, error=%d.\n",
			       dev->name, ret);
			return -EBUSY;
		}
		return 0;

	case SIOCSMIIREG:	/* Write GMII PHY register. */
	case SIOCDEVPRIVATE + 2:	/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (data->phy_id > 31 || data->reg_num > 31)
			return -ENXIO;

		spin_lock_irqsave(&XTE_spinlock, flags);
		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);

		ret = XTemac_PhyWrite(&lp->Emac, data->phy_id,
				      data->reg_num, data->val_in);

		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
		spin_unlock_irqrestore(&XTE_spinlock, flags);

		if (ret != XST_SUCCESS) {
			printk(KERN_ERR
			       "%s: XTemac: could not write to PHY, error=%d.\n",
			       dev->name, ret);
			return -EBUSY;
		}
		return 0;

	case SIOCDEVPRIVATE + 3:	/* set THRESHOLD */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg)))
			return -EFAULT;

		spin_lock_irqsave(&XTE_spinlock, flags);
		if ((dev_started = XTemac_mIsStarted(&lp->Emac)) == TRUE)
			XTemac_Stop(&lp->Emac);

		if ((ret = XTemac_IntrSgCoalGet(&lp->Emac, thr_arg.direction,
						&threshold,
						&timer)) != XST_SUCCESS) {
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return -EIO;
		}
		if ((ret = XTemac_IntrSgCoalSet(&lp->Emac, thr_arg.direction,
						thr_arg.threshold,
						timer)) != XST_SUCCESS) {
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return -EIO;
		}
		if (dev_started == TRUE) {
			if ((ret = XTemac_Start(&lp->Emac)) != XST_SUCCESS) {
				spin_unlock_irqrestore(&XTE_spinlock, flags);
				return -EIO;
			}
		}
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return 0;

	case SIOCDEVPRIVATE + 4:	/* set WAITBOUND */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg)))
			return -EFAULT;

		spin_lock_irqsave(&XTE_spinlock, flags);
		if ((dev_started = XTemac_mIsStarted(&lp->Emac)) == TRUE)
			XTemac_Stop(&lp->Emac);

		if ((ret = XTemac_IntrSgCoalGet(&lp->Emac, wbnd_arg.direction,
						&threshold,
						&timer)) != XST_SUCCESS) {
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return -EIO;
		}
		if ((ret =
		     XTemac_IntrSgCoalSet(&lp->Emac, wbnd_arg.direction,
					  threshold,
					  wbnd_arg.waitbound)) != XST_SUCCESS) {
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return -EIO;
		}
		if (dev_started == TRUE) {
			if ((ret = XTemac_Start(&lp->Emac)) != XST_SUCCESS) {
				spin_unlock_irqrestore(&XTE_spinlock, flags);
				return -EIO;
			}
		}
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return 0;

	case SIOCDEVPRIVATE + 5:	/* get THRESHOLD */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg)))
			return -EFAULT;

		if ((ret = XTemac_IntrSgCoalGet(&lp->Emac, thr_arg.direction,
						(u16 *) &(thr_arg.threshold),
						&timer)) != XST_SUCCESS) {
			return -EIO;
		}
		if (copy_to_user(rq->ifr_data, &thr_arg, sizeof(thr_arg))) {
			return -EFAULT;
		}
		return 0;

	case SIOCDEVPRIVATE + 6:	/* get WAITBOUND */
		if (!(XTemac_mIsSgDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg))) {
			return -EFAULT;
		}
		if ((ret = XTemac_IntrSgCoalGet(&lp->Emac, wbnd_arg.direction,
						&threshold,
						(u16 *) &(wbnd_arg.
							  waitbound))) !=
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


/******************************************************************************
 *
 * NEW FUNCTIONS FROM LINUX 2.6
 *
 ******************************************************************************/

static void xtenet_remove_ndev(struct net_device *ndev)
{
	if (ndev) {
		struct net_local *lp = netdev_priv(ndev);

		if (XTemac_mIsSgDma(&lp->Emac) && (lp->desc_space))
			free_descriptor_skb(ndev);

		iounmap((void *) (lp->Emac.BaseAddress));
		free_netdev(ndev);
	}
}

static int xtenet_remove(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	unregister_netdev(ndev);
	xtenet_remove_ndev(ndev);

	return 0;		/* success */
}

/* Detect the PHY address by scanning addresses 0 to 31 and
 * looking at the MII status register (register 1) and assuming
 * the PHY supports 10Mbps full/half duplex. Feel free to change
 * this code to match your PHY, or hardcode the address if needed.
 */
/* Use MII register 1 (MII status register) to detect PHY */
#define PHY_DETECT_REG  1

/* Mask used to verify certain PHY features (or register contents)
 * in the register above:
 *  0x1000: 10Mbps full duplex support
 *  0x0800: 10Mbps half duplex support
 *  0x0008: Auto-negotiation support
 */
#define PHY_DETECT_MASK 0x1808

static int detect_phy(struct net_local *lp, char *dev_name)
{
	int status;
	u16 phy_reg;
	u32 phy_addr;
	int i;

	for (phy_addr = 0; phy_addr <= 31; phy_addr++) {
		status = XTemac_PhyRead(&lp->Emac, phy_addr, PHY_DETECT_REG,
					&phy_reg);

		if ((status == XST_SUCCESS) && (phy_reg != 0xFFFF) &&
		    ((phy_reg & PHY_DETECT_MASK) == PHY_DETECT_MASK)) {
			/* Found a valid PHY address */
			printk(KERN_INFO
			       "%s: XTemac: PHY detected at address %d.\n",
			       dev_name, phy_addr);

			for (i = 0; i < 32; i++) {
				if ((i % 8) == 0) {
					if (i != 0)
						printk("\n");
					printk(KERN_INFO "%.2x: ", i);
				}
				XTemac_PhyRead(&lp->Emac, phy_addr, i,
					       &phy_reg);
				printk(" %.4x", phy_reg);
			}
			printk("\n");
			return phy_addr;
		}
	}

	printk(KERN_WARNING
	       "%s: XTemac: No PHY detected.  Assuming a PHY at address 0\n",
	       dev_name);
	return 0;		/* default to zero */
}

static int xtenet_probe(struct device *dev)
{
	int xs;
	u32 hwid;
	u32 virt_baddr;		/* virtual base address of temac */

	XTemac_Config Config;

	struct resource *r_irq = NULL;	/* Interrupt resources */
	struct resource *r_mem = NULL;	/* IO mem resources */

	struct xtemac_platform_data *pdata;

	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = NULL;
	struct net_local *lp = NULL;

	int rc = 0;


	/* param check */
	if (!pdev) {
		printk(KERN_ERR
		       "XTemac: Internal error. Probe called with NULL param.\n");
		rc = -ENODEV;
		goto error;
	}

	pdata = (struct xtemac_platform_data *) pdev->dev.platform_data;
	if (!pdata) {
		printk(KERN_ERR "xtemac %d: Couldn't find platform data.\n",
		       pdev->id);

		rc = -ENODEV;
		goto error;
	}

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev) {
		printk(KERN_ERR "xtemac %d: Could not allocate net device.\n",
		       pdev->id);
		rc = -ENOMEM;
		goto error;
	}
	dev_set_drvdata(dev, ndev);

	/* Get iospace and an irq for the device */
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_irq || !r_mem) {
		printk(KERN_ERR "xtemac %d: IO resource(s) not found.\n",
		       pdev->id);
		rc = -ENODEV;
		goto error;
	}
	ndev->irq = r_irq->start;


	/* Initialize the private data used by XEmac_LookupConfig().
	 * The private data are zeroed out by alloc_etherdev() already.
	 */
	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->index = pdev->id;

	/* Setup the Config structure for the XTemac_CfgInitialize() call. */
	Config.DeviceId = pdev->id;
	Config.BaseAddress = r_mem->start;
	Config.RxPktFifoDepth = pdata->rx_pkt_fifo_depth;
	Config.TxPktFifoDepth = pdata->tx_pkt_fifo_depth;
	Config.MacFifoDepth = pdata->mac_fifo_depth;
	Config.IpIfDmaConfig = pdata->dma_mode;
#ifdef XPAR_TEMAC_0_INCLUDE_RX_CSUM
	Config.TxDre = pdata->tx_dre;
	Config.RxDre = pdata->rx_dre;
	Config.TxCsum = pdata->tx_csum;
	Config.RxCsum = pdata->rx_csum;
	Config.PhyType = pdata->phy_type;
#endif
//    Config.DcrHost         = pdata->dcr_host;
//    Config.Dre             = pdata->dre;

	/* Get the virtual base address for the device */
	virt_baddr = (u32) ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (0 == virt_baddr) {
		printk(KERN_ERR "XTemac: Could not allocate iomem.\n");
		rc = -EIO;
		goto error;
	}


	if (XTemac_CfgInitialize(&lp->Emac, &Config, virt_baddr) != XST_SUCCESS) {
		printk(KERN_ERR "XTemac: Could not initialize device.\n");
		rc = -ENODEV;
		goto error;
	}

	/* Set the MAC address */
	/* wgr TODO: Get the MAC address right! */
	ndev->dev_addr[0] = 0x01;
	ndev->dev_addr[1] = 0x02;
	ndev->dev_addr[2] = 0x03;
	ndev->dev_addr[3] = 0x04;
	ndev->dev_addr[4] = 0x05;
	ndev->dev_addr[5] = 0x06;
// -wgr-     memcpy(ndev->dev_addr, ((bd_t *) &__res)->bi_enetaddr, 6);
	if (XTemac_SetMacAddress(&lp->Emac, ndev->dev_addr) != XST_SUCCESS) {
		/* should not fail right after an initialize */
		printk(KERN_ERR "XTemac: could not set MAC address.\n");
		rc = -EIO;
		goto error;
	}


	lp->max_frame_size = XTE_MAX_JUMBO_FRAME_SIZE;
	if (ndev->mtu > XTE_JUMBO_MTU)
		ndev->mtu = XTE_JUMBO_MTU;


	if (XTemac_mIsSgDma(&lp->Emac)) {
		int result;

		printk(KERN_ERR "XTemac: using sgDMA mode.\n");
		XTemac_SetHandler(&lp->Emac, XTE_HANDLER_SGSEND, SgSendHandler,
				  lp);
		XTemac_SetHandler(&lp->Emac, XTE_HANDLER_SGRECV, SgRecvHandler,
				  lp);
		lp->Isr = XTemac_IntrSgHandler;

		if (XTemac_mIsTxDre(&lp->Emac) == TRUE) {
			printk(KERN_INFO "XTemac: using TxDRE mode\n");
			ndev->hard_start_xmit = xenet_SgSend;
		}
		else {
			printk(KERN_INFO "XTemac: not using TxDRE mode\n");
			ndev->hard_start_xmit = xenet_SgSend_NoDRE;
		}
		if (XTemac_mIsRxDre(&lp->Emac) == TRUE) {
			printk(KERN_INFO "XTemac: using RxDRE mode\n");
			lp->local_features |= LOCAL_FEATURE_RX_DRE;
		}
		else {
			printk(KERN_INFO "XTemac: not using RxDRE mode\n");
			lp->local_features &= ~LOCAL_FEATURE_RX_DRE;
		}

		result = descriptor_init(ndev);
		if (result) {
			rc = -EIO;
			goto error;
		}

		if (XTemac_mIsTxDre(&lp->Emac) == FALSE) {
			result = tx_ring_buffer_init(ndev, lp->max_frame_size);
			if (result) {
				printk(KERN_ERR
				       "XTemac: Could not allocate TX buffers.\n");
				rc = -EIO;
				goto error;
			}
		}

		/* set the packet threshold and wait bound for both TX/RX directions */
		if ((xs =
		     XTemac_IntrSgCoalSet(&lp->Emac, XTE_SEND, DFT_TX_THRESHOLD,
					  DFT_TX_WAITBOUND)) != XST_SUCCESS) {
			printk(KERN_ERR
			       "XTemac: could not set SEND pkt threshold/waitbound, ERROR %d",
			       xs);
		}
		if ((xs =
		     XTemac_IntrSgCoalSet(&lp->Emac, XTE_RECV, DFT_RX_THRESHOLD,
					  DFT_RX_WAITBOUND)) != XST_SUCCESS) {
			printk(KERN_ERR
			       "XTemac: Could not set RECV pkt threshold/waitbound ERROR %d",
			       xs);
		}
	}
	else {
		printk(KERN_INFO
		       "XTemac: using FIFO direct interrupt driven mode.\n");
		XTemac_SetHandler(&lp->Emac, XTE_HANDLER_FIFORECV,
				  FifoRecvHandler, ndev);
		XTemac_SetHandler(&lp->Emac, XTE_HANDLER_FIFOSEND,
				  FifoSendHandler, ndev);
		ndev->hard_start_xmit = xenet_FifoSend;
		lp->Isr = XTemac_IntrFifoHandler;
	}
	XTemac_SetHandler(&lp->Emac, XTE_HANDLER_ERROR, ErrorHandler, ndev);

	/* Scan to find the PHY */
	lp->gmii_addr = detect_phy(lp, ndev->name);


	/* initialize the netdev structure */
	ndev->open = xenet_open;
	ndev->stop = xenet_close;
	ndev->change_mtu = xenet_change_mtu;
	ndev->get_stats = xenet_get_stats;
	ndev->flags &= ~IFF_MULTICAST;

	/* TX DRE and SGDMA need to go together for this to work right */
	if ((XTemac_mIsTxDre(&lp->Emac) == TRUE) &&
	    (XTemac_mIsSgDma(&lp->Emac) == TRUE)) {
		ndev->features = NETIF_F_SG | NETIF_F_FRAGLIST;
	}

	if (XTemac_mIsTxCsum(&lp->Emac) == TRUE) {
		/*
		 * This hardware only supports proper checksum calculations
		 * on TCP/UDP packets.
		 */
		ndev->features |= NETIF_F_IP_CSUM;
	}
	if (XTemac_mIsRxCsum(&lp->Emac) == TRUE) {
		lp->local_features |= LOCAL_FEATURE_RX_CSUM;
	}

	ndev->do_ioctl = xenet_ioctl;
	ndev->tx_timeout = xenet_tx_timeout;
	ndev->watchdog_timeo = TX_TIMEOUT;

	/* init the stats */
	lp->max_frags_in_a_packet = 0;
	lp->tx_hw_csums = 0;
	lp->rx_hw_csums = 0;

#if ! XTE_AUTOSTRIPPING
	lp->stripping =
		(XTemac_GetOptions(&(lp->Emac)) & XTE_FCS_STRIP_OPTION) != 0;
#endif

	rc = register_netdev(ndev);
	if (rc) {
		printk(KERN_ERR
		       "%s: Cannot register net device, aborting.\n",
		       ndev->name);
		goto error;	/* rc is already set here... */
	}

	printk(KERN_INFO
	       "%s: Xilinx TEMAC #%d at 0x%08X mapped to 0x%08X, irq=%d\n",
	       ndev->name, lp->Emac.Config.DeviceId,
	       lp->Emac.Config.BaseAddress, lp->Emac.BaseAddress, ndev->irq);

	/* print h/w id  */
	hwid = XIo_In32((lp->Emac).BaseAddress + XIIF_V123B_RESETR_OFFSET);

	printk(KERN_INFO
	       "%s: XTemac id %d.%d%c, block id %d, type %d\n",
	       ndev->name, (hwid >> 28) & 0xf, (hwid >> 21) & 0x7f,
	       ((hwid >> 16) & 0x1f) + 'a', (hwid >> 16) & 0xff,
	       (hwid >> 0) & 0xff);

	return 0;

      error:
	if (ndev) {
		xtenet_remove_ndev(ndev);
	}
	return rc;
}



static struct device_driver xtenet_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,

	.probe = xtenet_probe,
	.remove = xtenet_remove
};

static int __init xtenet_init(void)
{
	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	return driver_register(&xtenet_driver);
}

static void __exit xtenet_cleanup(void)
{
	driver_unregister(&xtenet_driver);
}

module_init(xtenet_init);
module_exit(xtenet_cleanup);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
