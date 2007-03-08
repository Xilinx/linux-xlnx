/*
 *  linux/drivers/char/p2001_eth.c
 *
 *  Driver for P2001 ethernet unit
 *
 *  Copyright (C) 2004 Tobias Lorenz
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Version 1.0: First working version
 * Version 1.1: mdio ioctl calls
 * Version 1.2: statistics counter
 * Version 1.3: skbuff direct instead of buffer-copy for sending
 * Version 1.4: ethtool calls
 * Version 1.5: interrupt driven transmit with transmit buffer ring
 * Version 1.6: support for all interfaces with phy connected
 * Version 1.7: generic mii infrastructure
 * Version 1.8: automatic MDIO CLK calculation
 * Version 1.9: added request_mem_region
 * Version 1.10: bug fix for main isp
 * Version 1.11: removed all READ_REG/WRITE_REG
 * Version 1.12: transmit timeout, no boot via eth1
 * Version 1.13: initialisation fix, which results in transmission errors,
 *               some pings are 10 times longer than the regular ones (glitches)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>

#include <asm/processor.h>      /* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>	/* User space memory access functions */



/**************************************************************************
 * Definitions
 **************************************************************************/
static const char *version =
	"p2001_eth.c:v1.13 10/13/2004 Tobias Lorenz (tobias.lorenz@gmx.net)\n";

static const char p2001_eth_name[] = "P2001 eth";

/* Hardware lookup table */
struct {
	unsigned int nr;		/* ethernet unit number / dma channel number */
	unsigned int base_addr;		/* device I/O address */
	unsigned int irq;		/* device data IRQ number (error IRQ +1) */
	unsigned int phy_id;		/* assigned phy address */
	unsigned char mac_hw_addr[6];	/* fixed MAC address */
} p2001_eth_dev_list[4] = {
	{ 0, (unsigned int)P2001_EU0, IRQ_EU0_DATA, 0, {0x00,0x09,0x4F,0x00,0x00,0x02} },
	{ 1, (unsigned int)P2001_EU1, IRQ_EU1_DATA, 1, {0x00,0x09,0x4F,0x00,0x00,0x03} },
	{ 2, (unsigned int)P2001_EU2, IRQ_EU2_DATA, 2, {0x00,0x09,0x4F,0x00,0x00,0x04} },
	{ 3, (unsigned int)P2001_EU3, IRQ_EU3_DATA, 3, {0x00,0x09,0x4F,0x00,0x00,0x05} },
};

/* DMA descriptions and buffers */
#define NUM_RX_DESC	16		/* Number of RX descriptor registers. */
#define NUM_TX_DESC	16		/* Number of TX descriptor registers. */
#define DMA_BUF_SIZE	2048		/* Buffer size */

/* Drivers private structure */
struct p2001_eth_private {
	struct net_device_stats stats;

	/* DMA descriptors and buffers */
	DMA_DSC rxd[NUM_RX_DESC] __attribute__ ((aligned(16)));
	DMA_DSC txd[NUM_TX_DESC] __attribute__ ((aligned(16)));
	char rxb[NUM_RX_DESC * DMA_BUF_SIZE] __attribute__ ((aligned(16)));
	struct sk_buff *txb[NUM_TX_DESC];
	/* producer/comsumer pointers for Tx/Rx ring */
	int cur_tx, dirty_tx;
	int cur_rx, dirty_rx;

	/* Device selectors */
	unsigned int nr;	/* NR/DMA channel: 0..3  */
	char adapter_name[11];	/* P2001 ethx\0 */

	spinlock_t lock;

	/* The Tx queue is full. */
	unsigned int tx_full;

	/* MII interface info */
	struct mii_if_info mii;
};

/* mdio handling */
static void mdio_hard_reset(void);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int val);

/* net_device functions */
static struct net_device_stats * p2001_eth_get_stats(struct net_device *dev);
static int p2001_eth_open(struct net_device *dev);
static int p2001_eth_stop(struct net_device *dev);
static int p2001_eth_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void p2001_eth_tx_timeout(struct net_device *dev);
static int p2001_eth_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

/* interrupt routines */
static irqreturn_t p2001_eth_data_interrupt(int irq, struct net_device *dev, struct pt_regs *regs);
static irqreturn_t p2001_eth_error_interrupt(int irq, struct net_device *dev, struct pt_regs *regs);

/* ethtool ops */
static struct ethtool_ops p2001_eth_ethtool_ops;

/* driver functions (pci_driver, isa_driver) */
struct net_device * __init p2001_eth_probe(int unit);
static void __devexit p2001_eth_remove(struct net_device *dev);



/**************************************************************************
 * PHY MANAGEMENT UNIT - Read/write
 **************************************************************************/

/**
 *	mdio_hard_reset - hardware reset all MII PHYs and set MDIO CLK
 */
static void mdio_hard_reset()
{
	/* GPIO24/25: TX_ER2/TX_ER0 */
	/* GPIO26/27: PHY_RESET/TX_ER1 */
	P2001_GPIO->PIN_MUX |= 0x0018;
	// 31-16: 0000 1111 0000 0000
	P2001_GPIO->GPIO2_En |= 0x0400;

	P2001_GPIO->GPIO2_Out |= 0x04000000;
	P2001_GPIO->GPIO2_Out &= ~0x0400;
	mdelay(500);
	P2001_GPIO->GPIO2_Out |= 0x0400;

	/* set management unit clock divisor */
	// max. MDIO CLK = 2.048 MHz (EU.doc)
	// max. MDIO CLK = 8.000 MHz (LXT971A)
	// sysclk/(2*(n+1)) = MDIO CLK <= 2.048 MHz
	// n >= sysclk/4.096 MHz - 1
	P2001_MU->MU_DIV = (CONFIG_SYSCLK/4096000)-1;	// 2.048 MHz
	//asm("nop \n nop");
}


/**
 *	mdio_read - read MII PHY register
 *	@dev: the net device to read
 *	@regadr: the phy register id to read
 *
 *	Read MII registers through MDIO and MDC
 *	using MDIO management frame structure and protocol(defined by ISO/IEC).
 */
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	do {
		/* Warten bis Hardware inaktiv (MIU = "0") */
		while (P2001_MU->MU_CNTL & 0x8000)
			barrier();

		/* Schreiben MU_CNTL */
		P2001_MU->MU_CNTL = location + (phy_id<<5) + (2<<10);

		/* Warten bis Hardware aktiv (MIU = "1") */
		while ((P2001_MU->MU_CNTL & 0x8000) == 0)
			barrier();
		//asm("nop \r\n nop");

		/* Warten bis Hardware inaktiv (MIU = "0") */
		while (P2001_MU->MU_CNTL & 0x8000)
			barrier();

		/* Fehler, wenn MDIO Read Error (MRE = "1") */
	} while (P2001_MU->MU_CNTL & 0x4000);

	/* Lesen MU_DATA */
	return P2001_MU->MU_DATA;
}


/**
 *	mdio_write - write MII PHY register
 *	@dev: the net device to write
 *	@regadr: the phy regiester id to write
 *	@value: the register value to write with
 *
 *	Write MII registers with @value through MDIO and MDC
 *	using MDIO management frame structure and protocol(defined by ISO/IEC)
 */
static void mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	/* Warten bis Hardware inaktiv (MIU = "0") */
	while (P2001_MU->MU_CNTL & 0x8000)
		barrier();

	/* Schreiben MU_DATA */
	P2001_MU->MU_DATA = val;

	/* Schreiben MU_CNTL */
	P2001_MU->MU_CNTL = location + (phy_id<<5) + (1<<10);

	/* Warten bis Hardware aktiv (MIU = "1") */
	while ((P2001_MU->MU_CNTL & 0x8000) == 0)
		barrier();
	//asm("nop \r\n nop");

	/* Warten bis Hardware inaktiv (MIU = "0") */
	while (P2001_MU->MU_CNTL & 0x8000)
		barrier();
}


//	mdio_write(dev, priv->mii.phy_id, MII_BMCR, BMCR_RESET);



/**************************************************************************
 * GET_STATS - Get read/write statistics
 **************************************************************************/

/**
 *	p2001_eth_get_stats - Get p2001 read/write statistics 
 *	@dev: the net device to get statistics for
 *
 *	get tx/rx statistics for p2001
 */
static struct net_device_stats * p2001_eth_get_stats(struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;

	return &priv->stats;
}



/**************************************************************************
 * OPEN - Open network device
 **************************************************************************/

/**
 *	p2001_eth_open - open p2001 ethernet device
 *	@dev: the net device to open
 *
 *	Do some initialization and start net interface.
 *	enable interrupts and set timer.
 */
static int p2001_eth_open(struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	int i, ret;

//	printk("%s: p2001_eth_open\n", dev->name);

	/* request data and error interrupts */
	ret = request_irq(dev->irq, (void *) &p2001_eth_data_interrupt, 0, dev->name, dev);
	if (ret)
		return ret;
	ret = request_irq(dev->irq+1, (void *) &p2001_eth_error_interrupt, 0, dev->name, dev);
	if (ret)
		return ret;

	/* set rx filter (physical mac address) */
	EU->RMAC_PHYU =
		(dev->dev_addr[0]<< 8) +
		(dev->dev_addr[1]<< 0);
	EU->RMAC_PHYL =
		(dev->dev_addr[2]<<24) +
		(dev->dev_addr[3]<<16) +
		(dev->dev_addr[4]<<8 ) +
		(dev->dev_addr[5]<<0 );

	/* initialize the tx descriptor ring */
	priv->tx_full = 0;
	priv->cur_tx = 0;
	priv->dirty_tx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		priv->txd[i].stat = 0;					// DSC0
		priv->txd[i].cntl = 0;					// DSC1
		priv->txd[i].buf = 0;					// DSC2 BUFFER (EU-TX data)
		priv->txd[i].next = &priv->txd[(i+1) % NUM_TX_DESC];	// DSC3 NEXTDSC @next/@first
	}
	EU->TMAC_DMA_DESC = &priv->txd[0];

	/* initialize the rx descriptor ring */
	priv->cur_rx = 0;
	priv->dirty_rx = 0;
	for (i = 0; i < NUM_RX_DESC; i++) {
		priv->rxd[i].stat = (1<<31) | (1<<30) | (1<<29);	// DSC0 OWN|START|END
		priv->rxd[i].cntl = (1<<30) | (1<<23);			// DSC1 INT|RECEIVE
		priv->rxd[i].cntl |= priv->nr << 16;			// DSC1 CHANNEL
		priv->rxd[i].cntl |= DMA_BUF_SIZE;			// DSC1 LEN
		priv->rxd[i].buf = &priv->rxb[i*DMA_BUF_SIZE];		// DSC2 BUFFER (EU-RX data)
		priv->rxd[i].next = &priv->rxd[(i+1) % NUM_RX_DESC];	// DSC3 NEXTDSC @next/@first
	}
	EU->RMAC_DMA_DESC = &priv->rxd[0];

	/* set transmitter mode */
	EU->TMAC_CNTL = (1<<4) |	/* COI: Collision ignore */
			//(1<<3) |	/* CSI: Carrier Sense ignore */
			(1<<2);		/* ATP: Automatic Transmit Padding */

	/* set receive mode */
	EU->RMAC_CNTL = (1<<3) |	/* BROAD: Broadcast packets */
			(1<<1);		/* PHY  : Packets to out MAC address */

	/* enable receiver */
	EU->RMAC_DMA_EN = 1;

	netif_start_queue(dev);

	return 0;
}



/**************************************************************************
 * STOP - Close network device
 **************************************************************************/

/**
 *	p2001_eth_stop - close p2001 ethernet device 
 *	@dev: the net device to be closed
 *
 *	Disable interrupts, stop the Tx and Rx Status Machine 
 *	free Tx and RX socket buffer
 */
static int p2001_eth_stop(struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	struct sk_buff *skb;
	unsigned int i;

//	printk("%s: p2001_eth_stop\n", dev->name);

	netif_stop_queue(dev);

	/* Stop the chip's Tx and Rx Status Machine */
	EU->TMAC_DMA_EN = 0;
	EU->RMAC_DMA_EN = 0;

	free_irq(dev->irq, dev);
	free_irq(dev->irq+1, dev);

	/* Free Tx skbuff */
	for (i = 0; i < NUM_TX_DESC; i++) {
		skb = priv->txb[i];
		if (skb) {
			dev_kfree_skb(skb);
			priv->txb[i] = 0;
		}
	}


	/* Green! Put the chip in low-power mode. */

	return 0;
}



/**************************************************************************
 * HARD START XMIT - Force start sending packets
 **************************************************************************/

/**
 *	p2001_eth_hard_start_xmit - start transmit routine
 *	@skb: socket buffer pointer to put the data being transmitted
 *	@dev: the net device to transmit with
 *
 *	Set the transmit buffer descriptor,
 *	and write TxENA to enable transmit state machine.
 *	tell upper layer if the buffer is full
 */
static int p2001_eth_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	unsigned int entry;
	unsigned long flags;
	unsigned int index_cur_tx, index_dirty_tx;
	unsigned int count_dirty_tx;

	spin_lock_irqsave(&priv->lock, flags);
	EU->TMAC_DMA_EN   = 0;		/* clear run bit */

//	printk("%s: p2001_eth_hard_start_xmit: size=%d\n", dev->name, skb->len);

	/* Calculate the next Tx descriptor entry. */
	entry = priv->cur_tx % NUM_TX_DESC;
	priv->txb[entry] = skb;

	/* set the transmit buffer descriptor and enable Transmit State Machine */
	priv->txd[entry].stat = (1<<31) | (1<<30) | (1<<29);	// DSC0 OWN|START|END
	priv->txd[entry].cntl = priv->nr << 16;			// DSC1 CHANNEL
	priv->txd[entry].cntl |= (1<<30);			// DSC1 INT
	priv->txd[entry].cntl |= skb->len;			// DSC1 LEN
	priv->txd[entry].buf = skb->data;			// DSC2 BUFFER (EU-TX data)

	priv->cur_tx++;
	index_cur_tx = priv->cur_tx;
	index_dirty_tx = priv->dirty_tx;

	for (count_dirty_tx = 0; index_cur_tx != index_dirty_tx; index_dirty_tx++)
		count_dirty_tx++;

//	printk("%s: entry=%d, cur_tx=%d, dirty_tx=%d, count_dirty_tx=%d\n", dev->name,
//		entry, priv->cur_tx % NUM_TX_DESC, priv->dirty_tx % NUM_TX_DESC, count_dirty_tx);

	EU->TMAC_DMA_DESC = &priv->txd[priv->dirty_tx % NUM_TX_DESC];

	EU->TMAC_DMA_EN   = 1;		/* set run bit */
	spin_unlock_irqrestore(&priv->lock, flags);

	dev->trans_start = jiffies;

//	printk(KERN_INFO "%s: Queued Tx packet at %p size %d to slot %d.\n",
//		dev->name, skb->data, (int)skb->len, entry);

	return 0;
}



/**************************************************************************
 * DO_IOCTL - Process MII i/o control command
 **************************************************************************/

/**
 *	p2001_eth_do_ioctl - process MII i/o control command 
 *	@dev: the net device to command for
 *	@rq: parameter for command
 *	@cmd: the i/o command
 *
 *	Process MII command like read/write MII register
 */
static int p2001_eth_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct p2001_eth_private *priv = dev->priv;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&rq->ifr_data;

	return generic_mii_ioctl(&priv->mii, data, cmd, NULL);
}



/**************************************************************************
 * TX_TIMEOUT - Transmit timeout routine
 **************************************************************************/

/**
 *	p2001_eth_tx_timeout - transmit timeout routine
 *	@dev: the net device to command for
 *
 *	print transmit timeout status
 *	disable interrupts and do some tasks
 */
static void p2001_eth_tx_timeout(struct net_device *dev)
{
//	struct p2001_eth_private *priv = dev->priv;

	printk(KERN_INFO "%s: Transmit timeout\n", dev->name);
}



/**************************************************************************
 * TX - interrupt transmit routine
 **************************************************************************/

/**
 *	p2001_eth_tx - finish up transmission of packets
 *	@net_dev: the net device to be transmitted on
 *
 *	Check for error condition and free socket buffer etc 
 *	schedule for more transmission as needed
 *	Note: This fucntion is called by interrupt handler, 
 *	don't do "too much" work here
 */

static void p2001_eth_tx(struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	unsigned int index_cur_tx, index_dirty_tx;
	unsigned int count_dirty_tx;

//	printk("%s: p2001_eth_tx\n", dev->name);
	for (; priv->dirty_tx != priv->cur_tx; priv->dirty_tx++) {
		struct sk_buff *skb;
		unsigned int entry;
		unsigned int status;

		entry = priv->dirty_tx % NUM_TX_DESC;
		status = priv->txd[entry].stat;

		if (status & (1<<31)) {	// OWN
			/* The packet is not transmitted yet (owned by hardware) !
			   Note: the interrupt is generated only when Tx Machine
			   is idle, so this is an almost impossible case */
//			printk("%s: p2001_eth_tx: nothing more to do\n", dev->name);
			break;
		}

//		if (status & 0x0000000f) {	// CRS|ED|OWC|EC
		if (status & 0x00000007) {	// ED|OWC|EC
			/* packet unsuccessfully transmitted */
			printk("%s: Transmit error, Tx status %8.8x.\n", dev->name, status);
			priv->stats.tx_errors++;
//			if (status & (1<<3))	// CRS
//				priv->stats.tx_carrier_errors++;
			if (status & (1<<1))	// OWC
				priv->stats.tx_window_errors++;
		} else {
			/* packet successfully transmitted */
//			printk("%s: p2001_eth_tx: success\n", dev->name);
			priv->stats.collisions += (status & 0x00000f00) >> 8;
			priv->stats.tx_bytes += priv->txd[entry].cntl & 0xffff;
			priv->stats.tx_packets++;
		}
		/* Free the original skb. */
		skb = priv->txb[entry];
		dev_kfree_skb_irq(skb);
		priv->txb[entry] = 0;
		priv->txd[entry].stat = 0;	// DSC0
		priv->txd[entry].cntl = 0;	// DSC1
		priv->txd[entry].buf = 0;	// DSC2 BUFFER (EU-TX data)
	}

	if (priv->tx_full && netif_queue_stopped(dev)) {
		index_cur_tx = priv->cur_tx;
		index_dirty_tx = priv->dirty_tx;

		for (count_dirty_tx = 0; index_cur_tx != index_dirty_tx; index_dirty_tx++)
			count_dirty_tx++;

		if (count_dirty_tx < NUM_TX_DESC - 4) {
			/* The ring is no longer full, clear tx_full and
			   schedule more transmissions by netif_wake_queue(dev) */
			priv->tx_full = 0;
			netif_wake_queue(dev);
		}
	}

	EU->TMAC_DMA_STAT |= (1<<8);
}



/**************************************************************************
 * RX - interrupt receive routine
 **************************************************************************/

/**
 *	p2001_eth_rx - p2001_eth receive routine
 *	@dev: the net device which receives data
 *
 *	Process receive interrupt events, 
 *	put buffer to higher layer and refill buffer pool
 *	Note: This fucntion is called by interrupt handler, 
 *	don't do "too much" work here
 */
static void p2001_eth_rx(struct net_device *dev)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	unsigned int entry;
	unsigned int status;
	struct sk_buff *skb;
	unsigned int pkt_len;

//	printk("%s: p2001_eth_rx\n", dev->name);
	while(1) {
		entry = priv->cur_rx % NUM_RX_DESC;
		status = priv->rxd[entry].stat;
		if (status & (1<<31))
			break;

		if (status & 0x07c00000) { 	// NOBYTE|CRCERR|COL|ISE|ILEN
			/* corrupted packet received */
			printk(KERN_INFO "%s: Corrupted packet "
			       "received, buffer status = 0x%8.8x.\n",
			       dev->name, status);
			priv->stats.rx_errors++;
		} else {
			/* give the socket buffer to the upper layers */
			pkt_len = priv->rxd[entry].cntl & 0xffff;
			skb = dev_alloc_skb(pkt_len);
			if (skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
				break;
			}

			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte align the IP fields. */

			eth_copy_and_sum(skb, priv->rxd[entry].buf, pkt_len, 0);
			skb_put(skb, pkt_len);

			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);

			/* some network statistics */
			dev->last_rx = jiffies;
			priv->stats.rx_bytes += pkt_len;
			priv->stats.rx_packets++;
		}

		/* disable receiver */
		// FIXME: is that ok? it can produce grave errors.
		EU->RMAC_DMA_EN = 0;			/* clear run bit */
		EU->RMAC_DMA_STAT = EU->RMAC_DMA_STAT;

		/* return the descriptor and buffer to receive ring */
		priv->rxd[entry].stat = (1<<31) | (1<<30) | (1<<29);	// DSC0 OWN|START|END
		priv->rxd[entry].cntl = (1<<30) | (1<<23);		// DSC1 INT|RECEIVE
		priv->rxd[entry].cntl |= priv->nr << 16;		// DSC1 CHANNEL
		priv->rxd[entry].cntl |= DMA_BUF_SIZE;			// DSC1 LEN

		/* enable receiver */
		EU->RMAC_DMA_EN = 0x01;			/* set run bit */

		priv->cur_rx++;
	}
}



/**************************************************************************
 * INTERRUPT - Interrupt routines
 **************************************************************************/

/**
 *	p2001_eth_data_interrupt - p2001_eth data interrupt handler
 *	@irq: the irq number
 *	@dev: the client data object
 *	@regs: snapshot of processor context
 *
 *	The interrupt handler does all of the Rx thread work, 
 *	and cleans up after the Tx thread
 */
static irqreturn_t p2001_eth_data_interrupt(int irq, struct net_device *dev, struct pt_regs *regs)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	int boguscnt = 10;	// max_interrupt_work
	unsigned int handled = 0;
	unsigned int rx_status;
	unsigned int tx_status;

//	printk("%s: p2001_eth_data_interrupt: start\n", dev->name);

	spin_lock(&priv->lock);

	do {
		/* Rx interrupt */
		rx_status = EU->RMAC_DMA_STAT;
		if (rx_status & (1<<8)) {
			// usally there is only one interrupt for multiple receives
			p2001_eth_rx(dev);
			handled = 1;
		}

		/* Tx interrupt */
		tx_status = EU->TMAC_DMA_STAT;
		if (tx_status & (1<<8)) {
			// usally there is only one interrupt for multiple transmits
			p2001_eth_tx(dev);
			handled = 1;
		}
	} while (--boguscnt && ((rx_status & (1<<8)) | (tx_status & (1<<8))));

	if (!handled) {
		printk(KERN_INFO "%s: p2001_eth_data_interrupt: interrupt not handled\n",
			dev->name);
		printk(KERN_INFO "%s: p2001_eth_data_interrupt: (rx=%#8.8x tx=%#8.8x)\n",
			dev->name, rx_status, tx_status);
		handled = 1;
	}

	spin_unlock(&priv->lock);
	return IRQ_RETVAL(handled);
}


/**
 *	p2001_eth_error_interrupt - p2001_eth error interrupt handler
 *	@irq: the irq number
 *	@dev: the client data object
 *	@regs: snapshot of processor context
 *
 *	The interrupt handler does all error tasks
 */
static irqreturn_t p2001_eth_error_interrupt(int irq, struct net_device *dev, struct pt_regs *regs)
{
	struct p2001_eth_private *priv = dev->priv;
	P2001_ETH_regs_ptr EU = (P2001_ETH_regs_ptr) dev->base_addr;
	unsigned int handled = 1;

	spin_lock(&priv->lock);
	if (EU->RMAC_DMA_STAT) {
		printk("%s: p2001_eth_error_interrupt: rmac_dma_stat=%#8.8x\n", dev->name, EU->RMAC_DMA_STAT);
		EU->RMAC_DMA_STAT |= (1<<7);
	}
	if (EU->TMAC_DMA_STAT) {
		printk("%s: p2001_eth_error_interrupt: tmac_dma_stat=%#8.8x\n", dev->name, EU->TMAC_DMA_STAT);
		EU->TMAC_DMA_STAT |= (1<<7);
	}
	spin_unlock(&priv->lock);
	return IRQ_RETVAL(handled);
}



/**************************************************************************
 * PROBE - Look for an adapter, this routine's visible to the outside
 **************************************************************************/

/**
 *	p2001_eth_probe - Probe for p2001 ethernet device
 *	@unit: the p2001 ethernet unit number
 *
 *	Check and probe for p2001 net device.
 *	Get mac address and assign p2001-specific entries in the device structure.
 */
struct net_device * __init p2001_eth_probe(int unit)
{
	struct net_device *dev;
	struct p2001_eth_private *priv;
	int i, err;

	dev = alloc_etherdev(sizeof(struct p2001_eth_private));
	if (!dev)
		return ERR_PTR(-ENOMEM);
	SET_MODULE_OWNER(dev);

	/* Configure unit specific variables */
	priv = dev->priv;
	dev->base_addr = p2001_eth_dev_list[unit].base_addr;
	dev->irq       = p2001_eth_dev_list[unit].irq;
	priv->nr       = p2001_eth_dev_list[unit].nr;
	sprintf(priv->adapter_name, "%s%i", p2001_eth_name, unit);
	request_mem_region(dev->base_addr, 0x1000, priv->adapter_name);
	spin_lock_init(&priv->lock);

	/* The p2001_eth-specific entries in the device structure. */
	// init
	dev->get_stats		= &p2001_eth_get_stats;
	// get_wireless_stats
	dev->ethtool_ops	= &p2001_eth_ethtool_ops;
	// uninit
	// destructor
	dev->open		= &p2001_eth_open;
	dev->stop		= &p2001_eth_stop;
	dev->hard_start_xmit	= &p2001_eth_hard_start_xmit;
	// poll
	// hard_header
	// rebuild_header
	// set_multicast_list
	// set_mac_address
	dev->do_ioctl		= &p2001_eth_do_ioctl;
	// set_config
	// hard_header_cache
	// header_cache_update
	// change_mtu
	dev->tx_timeout		= &p2001_eth_tx_timeout;
	// vlan_rx_register
	// vlan_rx_add_vid
	// vlan_rx_kill_vid
	// hard_header_parse
	// neigh_setup
	// accept_fastpath
	// poll_controller
	// last_stats
	ether_setup(dev);

	err = register_netdev(dev);
	if (err)
		goto err_out;
//	printk("%s: p2001_eth_probe\n", dev->name);

	/* Set MAC filter */
	memcpy(dev->dev_addr, p2001_eth_dev_list[unit].mac_hw_addr, ETH_ALEN);
	//random_ether_addr(dev->dev_addr);

	/* MII setup */
	priv->mii.phy_id = p2001_eth_dev_list[unit].phy_id;
	priv->mii.phy_id_mask = 0x1F;
	priv->mii.reg_num_mask = 0x1F;
	priv->mii.dev = dev;
	priv->mii.mdio_read = mdio_read;
	priv->mii.mdio_write = mdio_write;

	/* print some information about our NIC */
	printk(KERN_INFO "%s: ADDR %#lx, IRQ %d/%d, MAC ", dev->name, dev->base_addr, dev->irq, dev->irq+1);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", (u8)dev->dev_addr[i]);
	printk("%2.2x.\n", dev->dev_addr[i]);

	printk(KERN_INFO "%s: phy_addr = %d\n", dev->name, priv->mii.phy_id);
	printk(KERN_INFO "%s: phy ID = 0x%08x\n", dev->name,
		(mdio_read(dev, priv->mii.phy_id, MII_PHYSID2) << 16) |
		 mdio_read(dev, priv->mii.phy_id, MII_PHYSID1));

	netif_carrier_on(dev);

	return dev;

//err_out_unregister:
//	unregister_netdev(dev);
err_out:
	release_mem_region(dev->base_addr, 0x1000);
	free_netdev(dev);
	return ERR_PTR(err);
}



/**************************************************************************
 * REMOVE - Remove an adapter, this routine's visible to the outside
 **************************************************************************/

static void __devexit p2001_eth_remove(struct net_device *dev)
{
//	printk("%s: p2001_eth_remove\n", dev->name);
}



/**************************************************************************
 * GET_DRVINFO - Return information about driver
 **************************************************************************/

/**
 *	p2001_eth_get_drvinfo - Return information about driver
 *	@dev: the net device to probe
 *	@info: container for info returned
 *
 *	Process ethtool command such as "ethtool -i" to show information
 */
static void p2001_eth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, p2001_eth_name);
	strcpy(info->version, version);
	sprintf(info->bus_info, "ADDR 0x%lx", dev->base_addr);
}

static struct ethtool_ops p2001_eth_ethtool_ops = {
	.get_drvinfo =		p2001_eth_get_drvinfo,
};



/**************************************************************************
 * Module functions
 **************************************************************************/
static struct net_device *p2001_eth_dev[4];

/**
 * init_module:
 *
 * When the driver is loaded as a module this function is called. We fake up
 * a device structure with the base I/O and interrupt set as if it were being
 * called from Space.c. This minimises the extra code that would otherwise
 * be required.
 *
 * Returns 0 for success or -EIO if a card is not found. Returning an error
 * here also causes the module to be unloaded
 */
static int __init p2001_eth_init_module(void)
{
	int i;

//	printk("p2001_eth_init_module\n");
	printk(version);
	mdio_hard_reset();

//	for (i = 0; i < 4; i++) {
	for (i = 0; i < 2; i++) {
		p2001_eth_dev[i] = p2001_eth_probe(i);
		if (!p2001_eth_dev[i])
			return (unsigned int)p2001_eth_dev[i];
	}

	return 0;
}


/**
 * cleanup_module:
 * 
 * The module is being unloaded. We unhook our network device from the system
 * and then free up the resources we took when the card was found.
 */
static void __exit p2001_eth_cleanup_module(void)
{
//	printk("p2001_eth_cleanup_module\n");
}

module_init(p2001_eth_init_module);
module_exit(p2001_eth_cleanup_module);

MODULE_AUTHOR("Tobias Lorenz");
MODULE_DESCRIPTION("P2001 ethernet unit driver");
MODULE_LICENSE("GPL");
