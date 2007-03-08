/*
 * port to 16bit/8bit remote dma mode lq@cdgwbn.com.cn 
 * linux/deriver/net/Rtl8019as.c
 * Ethernet driver for Samsung 44B0
 * Copyright (C) 2003 antiscle <hzh12@163.net>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>		// kmalloc()
#include <linux/errno.h>	// error codes
#include <linux/types.h>	// size_t
#include <linux/interrupt.h>	// mark_bh
#include <linux/in.h>
#include <linux/netdevice.h>    // net_device
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <asm/irq.h>
#include "rtl8019.h"

#define RTL8019_OP_16   1

#undef  DEBUG
#define DEBUG	1
#ifdef	DEBUG
#define TRACE(str, args...)	printk(str, ## args)
#else
#define TRACE(str, args...)
#endif


#define	outportb(port, data)	*((volatile u8 *)(port)) = (u8)(data)
#define	inportb(port)		*((volatile u8 *)(port))


#define	outportw(port, data)	*((volatile u16 *)(port)) = (u16)(data)
#define	inportw(port)		*((volatile u16 *)(port))

#define	ETH_FRAME_LEN		1514

#define	RPSTART			0x4c
#define	RPSTOP			0x80
#define	SPSTART			0x40

static int timeout = 100;	// tx watchdog ticks 100 = 1s
static char *version = "Samsung S3C44B0 Rtl8019as driver version 0.1 (2002-02-20) <hzh12@163.net>\n";

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */
struct nic_8019_priv {
	struct net_device_stats stats;	
	spinlock_t lock;
	struct sk_buff *skb;
};

/*****************************************************************************/
static u8 rBNRY;
static u8 SrcMacID[ETH_ALEN] = {0x12,0x34,0x56,0x78,0x90,0xAB,};

static void SetRegPage( u8 PageIdx)
{
	u8 temp;

	temp = inportb(BaseAddr);	
	temp = (temp&0x3b)|(PageIdx<<6);						
	outportb(BaseAddr, temp);
}


irqreturn_t nic_8019_rx(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 RxPageBeg, RxPageEnd;
	u8 RxNextPage;
	u8 RxStatus;
	u16 *data,temp;
	u16 i, RxLength,RxLen;

	struct sk_buff *skb;	
	struct net_device *dev = (struct net_device *) dev_id;
	struct nic_8019_priv *priv = (struct nic_8019_priv *) dev->priv;

	TRACE("TX/RX Interupt!\n");
	spin_lock(&priv->lock);
	SetRegPage(0);
	outportb(BNRY, rBNRY);		//???
	RxStatus = inportb(ISR);
	if (RxStatus & 2) {
		outportb(ISR, 0x2);		//clr TX interupt
		priv->stats.tx_packets++;	
		TRACE("transmit one packet complete!\n");
	}
	
	if (RxStatus & 1) {
		TRACE("Receivex packet....\n");		
		outportb(ISR, 0x1);	         //clr Rx interupt	
		SetRegPage(1);
		RxPageEnd = inportb(CURR);

		SetRegPage(0);	
		RxPageBeg = rBNRY+1;
		if(RxPageBeg>=RPSTOP)
			RxPageBeg = RPSTART;		
		outportb(BaseAddr, 0x22);	// stop	remote dma

		//outport(RSAR0, RxPageBeg<<8);
		//outport(RBCR0, 256);		
		outportb(RSAR0, 0);
		outportb(RSAR1, RxPageBeg);
		outportb(RBCR0, 4);
		outportb(RBCR1, 0);	
		outportb(BaseAddr, 0xa);

#ifdef RTL8019_OP_16
		temp       = inportw(RWPORT);
		RxNextPage = temp>>8;
		RxStatus   = temp&0xff;
		RxLength   = inportw(RWPORT);
#else
		RxStatus   = inportb(RWPORT);
		RxNextPage = inportb(RWPORT);	
		RxLength   = inportb(RWPORT);
		RxLength  |= inportb(RWPORT)<<8;
#endif		
		TRACE("\nRxBeg = %x, RxEnd = %x,  nextpage = %x,  size = %i\n", RxPageBeg, RxPageEnd, RxNextPage, RxLength);		
		RxLength -= 4;
		if (RxLength>ETH_FRAME_LEN) {
			if (RxPageEnd==RPSTART)
				rBNRY = RPSTOP-1;
			else
				rBNRY = RxPageEnd-1;
				
			outportb(BNRY, rBNRY);
			TRACE("RxLength more long than %x\n", ETH_FRAME_LEN);
			return IRQ_HANDLED;
		}

		skb = dev_alloc_skb(RxLength+2);
		if (!skb) {
			TRACE("Rtl8019as eth: low on mem - packet dropped\n");
			priv->stats.rx_dropped++;
			return IRQ_HANDLED;
		}

		skb->dev = dev;		
		skb_reserve(skb, 2);
		skb_put(skb, RxLength);
		data = ( u16 *)skb->data;

		//		eth_copy_and_sum(skb, data, len, 0);
		outportb(RSAR0, 4);
		outportb(RSAR1, RxPageBeg);
		outportb(RBCR0, RxLength);
		outportb(RBCR1, RxLength>>8);	
		outportb(BaseAddr, 0xa);
#ifdef RTL8019_OP_16
		i = 2;
		data -= 2;
		RxLen=(RxLength+1)/2;	
#else 
		i = 4;
		data -= 4;
		RxLen=RxLength;
#endif
		for(; RxLen--;) {
#ifdef RTL8019_OP_16
			static const int cmp_val = 0x7f;
#else
			static const int cmp_val = 0xff;
#endif
			if (!(i & cmp_val)) {
				outportb(BNRY, RxPageBeg);				
				RxPageBeg++;
				if(RxPageBeg>=RPSTOP)
					RxPageBeg = RPSTART;					
			}
#ifdef RTL8019_OP_16
			data[i++] = inportw(RWPORT);		
			TRACE("%2X,%2X,", data[i-1]&0xff,data[i-1]>>8);
#else
			data[i++] = inportb(RWPORT);		
			TRACE("%2X,", data[i-1]);
#endif
		}

		TRACE("\n");
		outportb(BNRY, RxPageBeg);	
		rBNRY = RxPageBeg;

		skb->protocol = eth_type_trans(skb, dev);
		TRACE("\nprotocol=%x\n", skb->protocol);
		priv->stats.rx_packets++;
		priv->stats.rx_bytes +=RxLength;
		netif_rx(skb);
	} else {
		outportb(ISR, 0xfe);	
	}

	spin_unlock(&priv->lock);
	return IRQ_HANDLED;
}


/*
 * Open and Close
 */
static int nic_8019_open(struct net_device *dev)
{
	int i,j;

	MOD_INC_USE_COUNT;
	TRACE("open\n");
	// Disable irqs
	disable_irq(dev->irq);
	// register rx isr
	if (request_irq(dev->irq, &nic_8019_rx, SA_INTERRUPT, "eth rx isr", dev)) {
		printk(KERN_ERR "Rtl8019: Can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}

	// wake up Rtl8019as
	SetRegPage(3);	
	outportb(CR9346, 0xcf);		//set eem1-0, 11 ,enable write config register
	outportb(CONFIG3, 0x60);	//clear pwrdn, sleep mode, set led0 as led_col, led1 as led_crs	
	outportb(CR9346, 0x3f); 	//disable write config register
	
	// initialize
	outportb(RstAddr, 0x5a);
	i = 20000;
	while(i--);


	SetRegPage(0);
	inportb(ISR);				
	outportb(BaseAddr, 0x21);	/* set page 0 and stop */
	outportb(Pstart, RPSTART);	/* set Pstart 0x4c */
	outportb(Pstop, RPSTOP);	/* set Pstop 0x80 */
	outportb(BNRY, RPSTART);	/* BNRY-> the last page has been read */	
	outportb(TPSR, SPSTART);	/* SPSTART page start register, 0x40 */
	outportb(RCR, 0xcc);		/* set RCR 0xcc */	
	outportb(TCR, 0xe0);		/* set TCR 0xe0 */
	outportb(DCR, 0xc9);		/* set DCR 0xc9, 16bit DMA */	

	outportb(IMR, 0x03);		/* set IMR 0x03, enable tx rx int */
	outportb(ISR, 0xff);		/* clear ISR */

	SetRegPage(1);
	for(i=0; i<6; i++)
		outportb(BaseAddr+(1+i)*2, dev->dev_addr[i]);	// set mac id
		
	outportb(CURR, RPSTART+1);	
	outportb(MAR0, 0x00);
	outportb(MAR1, 0x41);
	outportb(MAR2, 0x00);
	outportb(MAR3, 0x80);
	outportb(MAR4, 0x00);
	outportb(MAR5, 0x00);
	outportb(MAR6, 0x00);
	outportb(MAR7, 0x00);
	outportb(BaseAddr, 0x22);		/* set page 0 and start */	
	rBNRY = RPSTART;
	enable_irq(dev->irq);		
	// Start the transmit queue
	netif_start_queue(dev);

	return 0;
}

static int nic_8019_stop(struct net_device *dev)
{
	TRACE("stop\n");
	SetRegPage(3);	
	outportb(CR9346, 0xcf);		// set eem1-0, 11 ,enable write config register
	outportb(CONFIG3, 0x66);	// enter pwrdn, sleep mode, set led0 as led_col, led1 as led_crs	
	outportb(CR9346, 0x3f); 	// disable write config register

	free_irq(dev->irq, dev);	
	netif_stop_queue(dev);
	MOD_DEC_USE_COUNT;

	return 0;
}

static int nic_8019_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int i;
	u16 len,TxLen;
	u16 *data;
	struct nic_8019_priv *priv = (struct nic_8019_priv *) dev->priv;

	TRACE("start_xmit\n");

	len = skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len;
	TRACE("\nTx Length = %i,%x,%x\n", len, skb->data[12], skb->data[13]);
	data =(u16*) skb->data;

	outportb(BaseAddr,0x22);  	//switch to page 0 and stop remote dma
	if (inportb(BaseAddr)&4)	// last remote dma not complete,return 1 echo busy(error),retransmit next
		return 1;
#ifdef bug_fix_for_write
	//read page 42,0,42,0 before write if you have problem
#endif
	outportb(RSAR0, 0);
	outportb(RSAR1, SPSTART);
	outportb(RBCR0, len&0xff);	
	outportb(RBCR1, len>>8);			
	outportb(BaseAddr, 0x12);	//begin remote write
	dev->trans_start = jiffies;	
#ifdef RTL8019_OP_16
	TxLen=(len+1)/2;
#else
	TxLen=len;
#endif
	for(i=0; i<TxLen; i++) {				
#ifdef RTL8019_OP_16		
		outportw(RWPORT, data[i]);		// copy data to nic ram
		TRACE("%2X,%2X,",data[i]&0xff,data[i]>>8);
#else
		outportb(RWPORT, data[i]);		// copy data to nic ram
		TRACE("%2X,",skb->data[i]);
#endif
	}	

	TRACE("\n");
	outportb(TPSR,  SPSTART);       // transmit begin page 0x40
	outportb(TBCR0, len&0xff);	
	outportb(TBCR1, len>>8);				
	outportb(BaseAddr, 0x1e);	// begin to send packet	
	dev_kfree_skb(skb);
	return 0;
}

static struct net_device_stats *nic_8019_get_stats(struct net_device *dev)
{
	struct nic_8019_priv *priv = (struct nic_8019_priv *) dev->priv;
	TRACE("get_stats\n");
	return &priv->stats;
}

/******************************************************************************/
static int nic_8019_init(struct net_device *dev)
{
	int i;
	TRACE("init\n");
	ether_setup(dev);	// Assign some of the fields

	// set net_device methods
	dev->open = nic_8019_open;
	dev->stop = nic_8019_stop;
	dev->get_stats = nic_8019_get_stats;
	dev->hard_start_xmit = nic_8019_start_xmit;

	// set net_device data members
	dev->watchdog_timeo = timeout;
	dev->irq = 22;
	dev->dma = 0;

	// set MAC address manually
	printk(KERN_INFO "%s: ", dev->name);
	for(i=0; i<6; i++) {
		dev->dev_addr[i] = SrcMacID[i];		
		printk("%2.2x%c", dev->dev_addr[i], (i==5) ? ' ' : ':');
	}
	printk("\n");

	SET_MODULE_OWNER(dev);

	dev->priv = kmalloc(sizeof(struct nic_8019_priv), GFP_KERNEL);
	if(dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct nic_8019_priv));
	spin_lock_init(&((struct nic_8019_priv *) dev->priv)->lock);
	return 0;
}

static struct net_device nic_8019_netdevs = {
	init: nic_8019_init,
};

/*
 * Finally, the module stuff
 */
int __init nic_8019_init_module(void)
{
	int result;
	TRACE("init_module\n");

	//Print version information
	printk(KERN_INFO "%s", version);

	//register_netdev will call nic_8019_init()
	if((result = register_netdev(&nic_8019_netdevs)))
		printk("Rtl8019as eth: Error %i registering device \"%s\"\n", result, nic_8019_netdevs.name);
		
	return result ? 0 : -ENODEV;
}

void __exit nic_8019_cleanup(void)
{
	TRACE("cleanup\n");
	kfree(nic_8019_netdevs.priv);
	unregister_netdev(&nic_8019_netdevs);
	return;
}

module_init(nic_8019_init_module);
module_exit(nic_8019_cleanup);

MODULE_DESCRIPTION("Rtl8019as ethernet driver");
MODULE_AUTHOR("antiscle <hzh12@163.net>");
MODULE_LICENSE("GPL");
