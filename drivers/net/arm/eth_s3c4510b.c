/*
 * linux/drivers/net/arm/eth_s3c4510b.c
 *
 * Copyright (c) 2004	Cucy Systems (http://www.cucy.com)
 * Curt Brune <curt@cucy.com>
 *
 * Re-written from scratch for 2.6.x after studying the original 2.4.x
 * driver by Mac Wang.
 *
 * Copyright (C) 2002 Mac Wang <mac@os.nctu.edu.tw>
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/irq.h>
#include <asm/arch/hardware.h>
#include "eth_s3c4510b.h"

#define __DRIVER_NAME    "Samsung S3C4510B Ethernet Driver version 0.2 (2004-06-13) <curt@cucy.com>"

#define _SDEBUG
#ifdef _SDEBUG
#  define _DPRINTK(format, args...)  \
          printk (KERN_INFO "%s():%05d "format".\n" , __FUNCTION__ , __LINE__ , ## args);
#else
#  define _DPRINTK(format, args...)
#endif

#define _EPRINTK(format, args...)  \
          printk (KERN_ERR "%s():%05d "format".\n" , __FUNCTION__ , __LINE__ , ## args);

struct eth_priv {

	/* Frame Descriptors */
	TX_FrameDesc m_txFDbase[ETH_NTxFrames];  /* array of TX frame descriptors */
	RX_FrameDesc m_rxFDbase[ETH_NRxFrames];  /* array of RX frame descriptors */
	volatile TX_FrameDesc *m_curTX_FD;   /* current TX FD to queue */
	volatile TX_FrameDesc *m_oldTX_FD;   /* oldest  TX FD queued, but not transmitted  */
	volatile RX_FrameDesc *m_curRX_FD;   /* current RX FD to receive */
	
	struct net_device_stats stats;
	spinlock_t lock;
};

/* This struct must be 16 byte aligned */
struct skb_priv {
	volatile RX_FrameDesc *m_RxFD;
	struct net_device      *m_dev;
	u32                  m_pad[2];
};

static s32 __skb_head_offset;

/**
 ** Avoid memcpy in RX handler by pre-allocating the socket buffers
 **/

// static void __skb_destruct( struct sk_buff *skb);
static void __skb_prepare( struct net_device *dev, volatile RX_FrameDesc *pRxFD)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb( sizeof(ETHFrame) + 16 + 2);
	if ( unlikely(!skb)) {
		_EPRINTK(" unable to allocate skb...");
	}

//	_DPRINTK("allocate skb: 0x%08x", (u32)skb);

	skb->dev = dev;

	/* attach skb to FD */
	pRxFD->skb = skb;
	pRxFD->m_frameDataPtr.bf.dataPtr = (u32)skb->data | CACHE_DISABLE_MASK;
	pRxFD->m_frameDataPtr.bf.owner   = 0x1; /* BDMA owner */

}

static s32 RxFDinit( struct net_device *dev) {

	struct eth_priv *priv = (struct eth_priv *) dev->priv;
	s32 i;
	volatile RX_FrameDesc *rxFDbase;
	struct sk_buff *skb;
	
	/* determine skb initial headroom for later use in the skb destructor */
	skb = dev_alloc_skb(256);
	__skb_head_offset = skb_headroom( skb);
	dev_kfree_skb( skb);

	/* store start of Rx descriptors and set current */
	rxFDbase = priv->m_curRX_FD = 
		(RX_FrameDesc *)((u32)priv->m_rxFDbase | CACHE_DISABLE_MASK);
	for ( i = 0; i < ETH_NRxFrames; i++) {
		__skb_prepare( dev, &rxFDbase[i]);
		priv->m_rxFDbase[i].m_reserved                = 0x0;
		priv->m_rxFDbase[i].m_status.ui               = 0x0;
		priv->m_rxFDbase[i].m_nextFD                  = &rxFDbase[i+1];
//		_DPRINTK("rxFDbase[%d]: 0x%08x", i, (u32)&rxFDbase[i]);
	}
	
	/* make the list circular */
	priv->m_rxFDbase[i-1].m_nextFD                  = &rxFDbase[0];
	
	outl( (unsigned int)rxFDbase, REG_BDMARXPTR);
	
	return 0;
}

static s32 TxFDinit( struct net_device *dev) {

	struct eth_priv *priv = (struct eth_priv *) dev->priv;
	s32 i;
	volatile TX_FrameDesc   *txFDbase;
	
	/* store start of Tx descriptors and set current */
	txFDbase = priv->m_curTX_FD  =  priv->m_oldTX_FD =
		(TX_FrameDesc *) ((u32)priv->m_txFDbase | CACHE_DISABLE_MASK);
	
	for ( i = 0; i < ETH_NTxFrames; i++) {
		priv->m_txFDbase[i].m_frameDataPtr.ui         = 0x0; /* CPU owner */
		priv->m_txFDbase[i].m_opt.ui                  = 0x0;
		priv->m_txFDbase[i].m_status.ui               = 0x0;
		priv->m_txFDbase[i].m_nextFD                  = &txFDbase[i+1];
//		_DPRINTK("txFDbase[%d]: 0x%08x", i, (u32)&txFDbase[i]);
	}
	
	/* make the list circular */
	priv->m_txFDbase[i-1].m_nextFD          = &txFDbase[0];
	
	outl( (unsigned int)txFDbase, REG_BDMATXPTR);

	return 0;
}

static irqreturn_t __s3c4510b_rx_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sk_buff          *skb;
	struct net_device       *dev = (struct net_device *) dev_id;
	struct eth_priv        *priv = (struct eth_priv *) dev->priv;
	volatile RX_FrameDesc *pRxFD;
	volatile RX_FrameDesc *cRxFD;

	spin_lock(&priv->lock);

	LED_SET(4);

	pRxFD = priv->m_curRX_FD;
	cRxFD = (RX_FrameDesc *)inl(REG_BDMARXPTR);

	/* clear received frame bit */
	outl( ETH_S_BRxRDF, REG_BDMASTAT);

	do {
		if ( likely( pRxFD->m_status.bf.good)) {
			skb = pRxFD->skb;

			__skb_prepare( dev, pRxFD);

			/* reserve two words used by protocol layers */
			skb_reserve(skb, 2);
			skb_put(skb, pRxFD->m_status.bf.len);
			skb->protocol = eth_type_trans(skb, dev);
			priv->stats.rx_packets++;
			priv->stats.rx_bytes += pRxFD->m_status.bf.len;
			netif_rx(skb);
		}
		else {
			priv->stats.rx_errors++;
			if( pRxFD->m_status.bf.overFlow)
				priv->stats.rx_fifo_errors++;
			if( pRxFD->m_status.bf.overMax)
				priv->stats.rx_length_errors++;
			if( pRxFD->m_status.bf.crcErr)
				priv->stats.rx_crc_errors++;
			if( pRxFD->m_status.bf.longErr)
				priv->stats.rx_length_errors++;
			if( pRxFD->m_status.bf.alignErr)
				priv->stats.rx_frame_errors++;
			/**
			 ** No good category for these errors
			if( pRxFD->m_status.bf.parityErr)
			**/			

		}

		/* set owner back to CPU */
		pRxFD->m_frameDataPtr.bf.owner = 1;
		/* clear status */
		pRxFD->m_status.ui = 0x0;
		/* advance to next descriptor */
		pRxFD = pRxFD->m_nextFD;

	} while ( pRxFD != cRxFD);

	priv->m_curRX_FD = pRxFD;

	LED_CLR(4);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;

}

static irqreturn_t __s3c4510b_tx_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct eth_priv *priv = (struct eth_priv *) dev->priv;
	volatile TX_FrameDesc *pTxFD;
	volatile TX_FrameDesc *cTxFD;

	spin_lock(&priv->lock);

	pTxFD = priv->m_oldTX_FD;
	cTxFD = (TX_FrameDesc *)inl(REG_BDMATXPTR);

	while ( pTxFD != cTxFD) {

		if ( likely(pTxFD->m_status.bf.complete)) {
			priv->stats.tx_packets++;
		}
		if( pTxFD->m_status.bf.exColl) {
			_EPRINTK("TX collision detected");
			priv->stats.tx_errors++;
			priv->stats.collisions++;
		}
		if( pTxFD->m_status.bf.underRun) {
			_EPRINTK("TX Underrun detected");
			priv->stats.tx_errors++;
			priv->stats.tx_fifo_errors++;
		}
		if( pTxFD->m_status.bf.noCarrier) {
			_EPRINTK("TX no carrier detected");
			priv->stats.tx_errors++;
			priv->stats.tx_carrier_errors++;
		}
		if(  pTxFD->m_status.bf.lateColl) {
			_EPRINTK("TX late collision detected");
			priv->stats.tx_errors++;
			priv->stats.tx_window_errors++;
		}
		if(  pTxFD->m_status.bf.parityErr) {
			_EPRINTK("TX parity error detected");
			priv->stats.tx_errors++;
			priv->stats.tx_aborted_errors++;
		}

		dev_kfree_skb_irq( pTxFD->skb);
		pTxFD = pTxFD->m_nextFD;
	}

	priv->m_oldTX_FD = pTxFD;

	LED_CLR(3);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;

}

static int __s3c4510b_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	u32 addr;
	struct eth_priv *priv = (struct eth_priv *) dev->priv;

//	_DPRINTK("entered with dev = 0x%08x", (unsigned int)dev);

	len = skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len;
	dev->trans_start = jiffies;
	
	if ( unlikely( priv->m_curTX_FD->m_frameDataPtr.bf.owner)) {
		_EPRINTK("Ethernet TX Frame.  CPU not owner");
		return -EBUSY;
	}

	/* this needs to be word aligned for the BDMA -- round down */
	addr = ((u32)skb->data & ~0x3) | CACHE_DISABLE_MASK;
	priv->m_curTX_FD->m_frameDataPtr.bf.dataPtr = addr;

	/* Set TX Frame flags */
	priv->m_curTX_FD->m_opt.bf.widgetAlign  = (u32)skb->data - addr; /* compenstate for alignment */
	priv->m_curTX_FD->m_opt.bf.frameDataDir = 1;
	priv->m_curTX_FD->m_opt.bf.littleEndian = 1;
	priv->m_curTX_FD->m_opt.bf.macTxIrqEnbl = 1;
	priv->m_curTX_FD->m_opt.bf.no_crc       = 0;
	priv->m_curTX_FD->m_opt.bf.no_padding   = 0;
	
	/* Set TX Frame length */
	priv->m_curTX_FD->m_status.bf.len       = len;
	
	priv->m_curTX_FD->skb = skb;

	/* Change ownership to BDMA */
	priv->m_curTX_FD->m_frameDataPtr.bf.owner = 1;
	
	/* Change the Tx frame descriptor for next use */
	priv->m_curTX_FD = priv->m_curTX_FD->m_nextFD;

	LED_SET(3);

	/* Enable MAC and BDMA Tx control register */
	outl( ETH_BTxBRST   |	/* BDMA Tx burst size 16 words  */
	      ETH_BTxMSL110 |	/* BDMA Tx wait to fill 6/8 of the BDMA */
	      ETH_BTxSTSKO  |	/* BDMA Tx interrupt(Stop) on non-owner TX FD */
	      ETH_BTxEn,	/* BDMA Tx Enable  */
	      REG_BDMATXCON);

	outl( ETH_EnComp     | 	/* interrupt when the MAC transmits or discards packet */
	      ETH_TxEn	     |	/* MAC transmit enable */
	      ETH_EnUnder    |	/*  interrupt on Underrun */
	      ETH_EnNCarr    |	/*  interrupt on No Carrier  */
	      ETH_EnExColl   |	/*  interrupt if 16 collision occur  */
	      ETH_EnLateColl |	/*  interrupt if collision occurs after 512 bit times(64 bytes times) */
	      ETH_EnTxPar,	/*  interrupt if the MAC transmit FIFO has a parity error  */
	      REG_MACTXCON);

	return 0;
	
}

static struct irqaction __rx_irqaction = {
	name:	  "eth_rx",
	flags:	  SA_INTERRUPT,
	handler:  __s3c4510b_rx_int,
};

static struct irqaction __tx_irqaction = {
	name:	  "eth_tx",
	flags:	  SA_INTERRUPT,
	handler:  __s3c4510b_tx_int,
};

static int __s3c4510b_open(struct net_device *dev)
{
	unsigned long status;

	/* Disable interrupts */
	INT_DISABLE(INT_BDMARX);
	INT_DISABLE(INT_MACTX);

	/**
	 ** install RX ISR
	 **/
	__rx_irqaction.dev_id = (void *)dev;
	status = setup_irq( INT_BDMARX, &__rx_irqaction);
	if ( unlikely(status)) {
		printk( KERN_ERR "Unabled to hook irq %d for ethernet RX\n", INT_BDMARX);
		return status;
	}

	/**
	 ** install TX ISR
	 **/
	__tx_irqaction.dev_id = (void *)dev;
	status = setup_irq( INT_MACTX, &__tx_irqaction);
	if ( unlikely(status)) {
		printk( KERN_ERR "Unabled to hook irq %d for ethernet TX\n", INT_MACTX);
		return status;
	}

	/* setup DBMA and MAC */
	outl( ETH_BRxRS, REG_BDMARXCON);	/* reset BDMA RX machine */
	outl( ETH_BTxRS, REG_BDMATXCON);	/* reset BDMA TX machine */
	outl( ETH_SwReset, REG_MACCON);		/* reset MAC machine */
	outl( sizeof( ETHFrame), REG_BDMARXLSZ);
	outl( ETH_FullDup, REG_MACCON);		/* enable full duplex */

	/* init frame descriptors */
	TxFDinit( dev);
	RxFDinit( dev);

	outl( (dev->dev_addr[0] << 24) |
	      (dev->dev_addr[1] << 16) |
	      (dev->dev_addr[2] <<  8) |
	      (dev->dev_addr[3])       , REG_CAM_BASE);
	outl( (dev->dev_addr[4] << 24) |
	      (dev->dev_addr[5] << 16) , REG_CAM_BASE + 4);

	outl(  0x0001, REG_CAMEN);
	outl( ETH_CompEn | 	/* enable compare mode (check against the CAM) */
	      ETH_BroadAcc, 	/* accept broadcast packetes */
	      REG_CAMCON);

	INT_ENABLE(INT_BDMARX);
	INT_ENABLE(INT_MACTX);

	/* enable RX machinery */
	outl( ETH_BRxBRST   |	/* BDMA Rx Burst Size 16 words */
	      ETH_BRxSTSKO  |	/* BDMA Rx interrupt(Stop) on non-owner RX FD */
	      ETH_BRxMAINC  |	/* BDMA Rx Memory Address increment */
	      ETH_BRxDIE    |	/* BDMA Rx Every Received Frame Interrupt Enable */
	      ETH_BRxNLIE   |	/* BDMA Rx NULL List Interrupt Enable */
	      ETH_BRxNOIE   |	/* BDMA Rx Not Owner Interrupt Enable */
	      ETH_BRxLittle |	/* BDMA Rx Little endian */
	      ETH_BRxWA10   |	/* BDMA Rx Word Alignment- two invalid bytes */
	      ETH_BRxEn,	/* BDMA Rx Enable */
	      REG_BDMARXCON);

	outl( ETH_RxEn	    |	/* enable MAC RX */
	      ETH_StripCRC  |	/* check and strip CRC */
	      ETH_EnCRCErr  |	/* interrupt on CRC error */
	      ETH_EnOver    |	/* interrupt on overflow error */
	      ETH_EnLongErr |	/* interrupt on long frame error */
	      ETH_EnRxPar,   	/* interrupt on MAC FIFO parity error */
	      REG_MACRXCON);

	netif_start_queue(dev);

	return 0;
}

static int __s3c4510b_stop(struct net_device *dev)
{
	// Disable irqs
	INT_DISABLE(INT_BDMARX);
	INT_DISABLE(INT_MACTX);

	outl( 0, REG_BDMATXCON);
	outl( 0, REG_BDMARXCON);
	outl( 0, REG_MACTXCON);
	outl( 0, REG_MACRXCON);

	free_irq(INT_BDMARX, dev);
	free_irq(INT_MACTX, dev);

	netif_stop_queue(dev);
	
	return 0;
}

struct net_device_stats *__s3c4510b_get_stats(struct net_device *dev)
{
	return &((struct eth_priv *)dev->priv)->stats;
}

/*
 * The init function, invoked by register_netdev()
 */
static int __s3c4510b_init(struct net_device *dev)
{
	ether_setup(dev);

	/* assign net_device methods */
	dev->open = __s3c4510b_open;
	dev->stop = __s3c4510b_stop;
//	dev->ioctl = __s3c4510b_ioctl;
	dev->get_stats = __s3c4510b_get_stats;
//	dev->tx_timeout = __s3c4510b_tx_timeout;
	dev->hard_start_xmit = __s3c4510b_start_xmit;

	dev->irq = INT_BDMARX;
	dev->tx_queue_len = ETH_NTxFrames;
	dev->dma = 0;
	dev->watchdog_timeo = HZ;

	/* set MAC address */
	dev->dev_addr[0] = 0x00;
	dev->dev_addr[1] = 0x40;
	dev->dev_addr[2] = 0x95;
	dev->dev_addr[3] = 0x36;
	dev->dev_addr[4] = 0x35;
	dev->dev_addr[5] = 0x33;

	SET_MODULE_OWNER(dev);

	dev->priv = kmalloc(sizeof(struct eth_priv), GFP_KERNEL);
	if( dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct eth_priv));
	spin_lock_init(&((struct eth_priv *) dev->priv)->lock);
	return 0;
}

struct net_device __s3c4510b_netdevs = {
	init: __s3c4510b_init,
};

static int __init __s3c4510b_init_module(void)
{
	int status = 0;

	printk(KERN_INFO "%s\n", __DRIVER_NAME);

	if( (status = register_netdev( &__s3c4510b_netdevs)))
		printk("S3C4510 eth: Error %i registering interface %s\n", status, __s3c4510b_netdevs.name);

	return status;
}

static void __exit __s3c4510b_cleanup(void)
{
	kfree( __s3c4510b_netdevs.priv);
	unregister_netdev( &__s3c4510b_netdevs);
	return;
}

module_init(__s3c4510b_init_module);
module_exit(__s3c4510b_cleanup);

MODULE_DESCRIPTION("Samsung S3C4510B ethernet driver");
MODULE_AUTHOR("Curt Brune <curt@cucy.com>");
MODULE_LICENSE("GPL");
