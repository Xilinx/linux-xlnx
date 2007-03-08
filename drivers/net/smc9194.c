/*------------------------------------------------------------------------
 . smc9194.c
 . This is a driver for SMC's 9000 series of Ethernet cards.
 .
 . Copyright (C) 1996 by Erik Stahlman
 . This software may be used and distributed according to the terms
 . of the GNU General Public License, incorporated herein by reference.
 .
 . "Features" of the SMC chip:
 .   4608 byte packet memory. ( for the 91C92.  Others have more )
 .   EEPROM for configuration
 .   AUI/TP selection  ( mine has 10Base2/10BaseT select )
 .
 . Arguments:
 . 	io		 = for the base address
 .	irq	 = for the IRQ
 .	ifport = 0 for autodetect, 1 for TP, 2 for AUI ( or 10base2 )
 .
 . author:
 . 	Erik Stahlman				( erik@vt.edu )
 . contributors:
 .      Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 .
 . Hardware multicast code from Peter Cammaert ( pc@denkart.be )
 .
 . Sources:
 .    o   SMC databook
 .    o   skeleton.c by Donald Becker ( becker@scyld.com )
 .    o   ( a LOT of advice from Becker as well )
 .
 . History:
 .	12/07/95  Erik Stahlman  written, got receive/xmit handled
 . 	01/03/96  Erik Stahlman  worked out some bugs, actually usable!!! :-)
 .	01/06/96  Erik Stahlman	 cleaned up some, better testing, etc
 .	01/29/96  Erik Stahlman	 fixed autoirq, added multicast
 . 	02/01/96  Erik Stahlman	 1. disabled all interrupts in smc_reset
 .		   		 2. got rid of post-decrementing bug -- UGH.
 .	02/13/96  Erik Stahlman  Tried to fix autoirq failure.  Added more
 .				 descriptive error messages.
 .	02/15/96  Erik Stahlman  Fixed typo that caused detection failure
 . 	02/23/96  Erik Stahlman	 Modified it to fit into kernel tree
 .				 Added support to change hardware address
 .				 Cleared stats on opens
 .	02/26/96  Erik Stahlman	 Trial support for Kernel 1.2.13
 .				 Kludge for automatic IRQ detection
 .	03/04/96  Erik Stahlman	 Fixed kernel 1.3.70 +
 .				 Fixed bug reported by Gardner Buchanan in
 .				   smc_enable, with outw instead of outb
 .	03/06/96  Erik Stahlman  Added hardware multicast from Peter Cammaert
 .	04/14/00  Heiko Pruessing (SMA Regelsysteme)  Fixed bug in chip memory
 .				 allocation
 .      08/20/00  Arnaldo Melo   fix kfree(skb) in smc_hardware_send_packet
 .      12/15/00  Christian Jullien fix "Warning: kfree_skb on hard IRQ"
 .      11/08/01 Matt Domsch     Use common crc32 function
 ----------------------------------------------------------------------------*/

static const char version[] =
	"smc9194.c:v0.14 12/15/00 by Erik Stahlman (erik@vt.edu)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/io.h>

#include "smc9194.h"

#ifdef CONFIG_M68EZ328
#include <asm/MC68EZ328.h>
#include <asm/irq.h>
#include <asm/mcfsmc.h>
unsigned char	smc_defethaddr[] = { 0x00, 0x10, 0x8b, 0xf1, 0xda, 0x01 };
#define NO_AUTOPROBE
#endif

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfsmc.h>

unsigned char	smc_defethaddr[] = { 0x00, 0xd0, 0xcf, 0x00, 0x00, 0x01 };

#define NO_AUTOPROBE
#endif

#ifdef CONFIG_SH_KEYWEST
#include <asm/keywest.h>
#define NO_AUTOPROBE
#define PHY_SETUP
#endif

#ifdef CONFIG_LEDMAN
#include <linux/ledman.h>
#endif

#if defined(CONFIG_CPU_H8300H) || defined(CONFIG_CPU_H8S)
#include <asm/h8300_smsc.h>
#define NO_AUTOPROBE
#endif

#define DRV_NAME "smc9194"

/*------------------------------------------------------------------------
 .
 . Configuration options, for the experienced user to change.
 .
 -------------------------------------------------------------------------*/

/*
 . Do you want to use 32 bit xfers?  This should work on all chips, as
 . the chipset is designed to accommodate them.
*/
#if (defined(__sh__) && !defined(CONFIG_SH_KEYWEST)) || \
    defined(__H8300H__) || defined(__H8300S__)
#undef USE_32_BIT
#else
#define USE_32_BIT 1
#endif

#if defined(__H8300H__) || defined(__H8300S__)
#define NO_AUTOPROBE
#undef insl
#undef outsl
#define insl(a,b,l)  io_insl_noswap(a,b,l)
#define outsl(a,b,l) io_outsl_noswap(a,b,l)
#endif

/*
 .A typedef so we can change what IO looks like easily
*/
typedef unsigned int smcio_t;

/*
 .the SMC9194 can be at any of the following port addresses.  To change,
 .for a slightly different card, you can add it to the array.  Keep in
 .mind that the array must end in zero.
*/
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328) || \
	defined(CONFIG_SH_KEYWEST)

#ifdef CONFIG_NETtel
static smcio_t smc_portlist[]      = { 0x30600300, 0x30600000, 0 };
static unsigned int smc_irqlist[]  = {         29,         27, 0 };
#elif defined(CONFIG_SH_KEYWEST)
static smcio_t smc_portlist[]      = { KEYWEST_ETHR, 0 };
static unsigned int smc_irqlist[]  = { IRQ4_IRQ,     0 };
#elif defined(CONFIG_M68EZ328)
/* make sure that you program Port D selects to allow the interrupts! */
static smcio_t smc_portlist[]      = { 0x2000300,    0x2000320,    0 };
static unsigned int smc_irqlist[]  = { IRQ1_IRQ_NUM, IRQ2_IRQ_NUM, 0 };
#elif defined(CONFIG_CLEOPATRA)
static unsigned int smc_portlist[] = { 0x30600300, 0 };
static unsigned int smc_irqlist[]  = {         29, 0 };
#else
static smcio_t smc_portlist[]      = { 0x30600300, 0 };
static unsigned int smc_irqlist[]  = {         27, 0 };
#endif

#elif defined(CONFIG_H8S_EDOSK2674)
static struct devlist smc_devlist[] __initdata = {
	{.port = 0xf80000, .irq = 16},
	{.port = 0,        .irq = 0 },
};
#else
static struct devlist smc_devlist[] __initdata = {
	{.port = 0x200, .irq = 0},
	{.port = 0x220, .irq = 0},
	{.port = 0x240, .irq = 0},
	{.port = 0x260, .irq = 0},
	{.port = 0x280, .irq = 0},
	{.port = 0x2A0, .irq = 0},
	{.port = 0x2C0, .irq = 0},
	{.port = 0x2E0, .irq = 0},
	{.port = 0x300, .irq = 0},
	{.port = 0x320, .irq = 0},
	{.port = 0x340, .irq = 0},
	{.port = 0x360, .irq = 0},
	{.port = 0x380, .irq = 0},
	{.port = 0x3A0, .irq = 0},
	{.port = 0x3C0, .irq = 0},
	{.port = 0x3E0, .irq = 0},
	{.port = 0,     .irq = 0},
};
#endif
/*
 . Wait time for memory to be free.  This probably shouldn't be
 . tuned that much, as waiting for this means nothing else happens
 . in the system
*/
#define MEMORY_WAIT_TIME 16

/*
 . DEBUGGING LEVELS
 .
 . 0 for normal operation
 . 1 for slightly more details
 . >2 for various levels of increasingly useless information
 .    2 for interrupt tracking, status flags
 .    3 for packet dumps, etc.
*/
#define SMC_DEBUG 0

#if (SMC_DEBUG > 2 )
#define PRINTK3(x) printk x
#else
#define PRINTK3(x)
#endif

#if SMC_DEBUG > 1
#define PRINTK2(x) printk x
#else
#define PRINTK2(x)
#endif

#ifdef SMC_DEBUG
#define PRINTK(x) printk x
#else
#define PRINTK(x)
#endif


/*------------------------------------------------------------------------
 .
 . The internal workings of the driver.  If you are changing anything
 . here with the SMC stuff, you should have the datasheet and known
 . what you are doing.
 .
 -------------------------------------------------------------------------*/
#define CARDNAME "SMC9194"


/* store this information for the driver.. */
struct smc_local {
	/*
 	   these are things that the kernel wants me to keep, so users
	   can find out semi-useless statistics of how well the card is
	   performing
 	*/
	struct net_device_stats stats;

	/*
	   If I have to wait until memory is available to send
	   a packet, I will store the skbuff here, until I get the
	   desired memory.  Then, I'll send it out and free it.
	*/
	struct sk_buff * saved_skb;

	/*
 	 . This keeps track of how many packets that I have
 	 . sent out.  When an TX_EMPTY interrupt comes, I know
	 . that all of these have been sent.
	*/
	int	packets_waiting;
};


/*-----------------------------------------------------------------
 .
 .  The driver can be entered at any of the following entry points.
 .
 .------------------------------------------------------------------  */

/*
 . This is called by  register_netdev().  It is responsible for
 . checking the portlist for the SMC9000 series chipset.  If it finds
 . one, then it will initialize the device, find the hardware information,
 . and sets up the appropriate device parameters.
 . NOTE: Interrupts are *OFF* when this procedure is called.
 .
 . NB:This shouldn't be static since it is referred to externally.
*/
struct net_device *smc_init(int unit);

/*
 . The kernel calls this function when someone wants to use the device,
 . typically 'ifconfig ethX up'.
*/
static int smc_open(struct net_device *dev);

/*
 . Our watchdog timed out. Called by the networking layer
*/
static void smc_timeout(struct net_device *dev);

/*
 . This is called by the kernel in response to 'ifconfig ethX down'.  It
 . is responsible for cleaning up everything that the open routine
 . does, and maybe putting the card into a powerdown state.
*/
static int smc_close(struct net_device *dev);

/*
 . This routine allows the proc file system to query the driver's
 . statistics.
*/
static struct net_device_stats * smc_query_statistics( struct net_device *dev);

/*
 . Finally, a call to set promiscuous mode ( for TCPDUMP and related
 . programs ) and multicast modes.
*/
static void smc_set_multicast_list(struct net_device *dev);


/*---------------------------------------------------------------
 .
 . Interrupt level calls..
 .
 ----------------------------------------------------------------*/

/*
 . Handles the actual interrupt
*/
static irqreturn_t smc_interrupt(int irq, void *);
/*
 . This is a separate procedure to handle the receipt of a packet, to
 . leave the interrupt code looking slightly cleaner
*/
static inline void smc_rcv( struct net_device *dev );
/*
 . This handles a TX interrupt, which is only called when an error
 . relating to a packet is sent.
*/
static inline void smc_tx( struct net_device * dev );

/*
 ------------------------------------------------------------
 .
 . Internal routines
 .
 ------------------------------------------------------------
*/

/*
 . Test if a given location contains a chip, trying to cause as
 . little damage as possible if it's not a SMC chip.
*/
static int smc_probe(struct net_device *dev, smcio_t ioaddr);

/*
 . A rather simple routine to print out a packet for debugging purposes.
*/
#if SMC_DEBUG > 2
static void print_packet( byte *, int );
#endif

#define tx_done(dev) 1

/* this is called to actually send the packet to the chip */
static void smc_hardware_send_packet( struct net_device * dev );

/* Since I am not sure if I will have enough room in the chip's ram
 . to store the packet, I call this routine, which either sends it
 . now, or generates an interrupt when the card is ready for the
 . packet */
static int  smc_wait_to_send_packet( struct sk_buff * skb, struct net_device *dev );

/* this does a soft reset on the device */
static void smc_reset( smcio_t ioaddr );

/* Enable Interrupts, Receive, and Transmit */
static void smc_enable( smcio_t ioaddr );

/* this puts the device in an inactive state */
static void smc_shutdown( smcio_t ioaddr );

#ifndef NO_AUTOPROBE
/* This routine will find the IRQ of the driver if one is not
 . specified in the input to the device.  */
static int smc_findirq( smcio_t ioaddr );
#endif

#ifdef PHY_SETUP
static void clkmdio(smcio_t ioaddr, unsigned int MGMTData);
static unsigned PHYAccess(smcio_t ioaddr, unsigned char PHYAdd,
				unsigned char RegAdd, unsigned char OPCode, unsigned wData);
static unsigned char DetectPHY(smcio_t ioaddr, unsigned long *OUI,
						unsigned char *Model, unsigned char *Revision);
static int setup_phy(smcio_t ioaddr);
#endif

/*
 . Function: smc_reset( smcio_t ioaddr )
 . Purpose:
 .  	This sets the SMC91xx chip to its normal state, hopefully from whatever
 . 	mess that any other DOS driver has put it in.
 .
 . Maybe I should reset more registers to defaults in here?  SOFTRESET  should
 . do that for me.
 .
 . Method:
 .	1.  send a SOFT RESET
 .	2.  wait for it to finish
 .	3.  enable autorelease mode
 .	4.  reset the memory management unit
 .	5.  clear all interrupts
 .
*/
static void smc_reset( smcio_t ioaddr )
{
	/* This resets the registers mostly to defaults, but doesn't
	   affect EEPROM.  That seems unnecessary */
	SMC_SELECT_BANK( 0 );
	outw( RCR_SOFTRESET, ioaddr + RCR );

	/* this should pause enough for the chip to be happy */
	SMC_DELAY( );

	/* Set the transmit and receive configuration registers to
	   default values */
	outw( RCR_CLEAR, ioaddr + RCR );
	outw( TCR_CLEAR, ioaddr + TCR );

	/* set the control register to automatically
	   release successfully transmitted packets, to make the best
	   use out of our limited memory */
	SMC_SELECT_BANK( 1 );
	outw( inw( ioaddr + CONTROL ) | CTL_AUTO_RELEASE , ioaddr + CONTROL );

#if defined(CONFIG_LEDMAN) && defined(CONFIG_SNAPGEAR)
	outw( inw( ioaddr + CONTROL ) | CTL_LE_ENABLE , ioaddr + CONTROL );	
#endif

	/* Reset the MMU */
	SMC_SELECT_BANK( 2 );
	outw( MC_RESET, ioaddr + MMU_CMD );

	/* Note:  It doesn't seem that waiting for the MMU busy is needed here,
	   but this is a place where future chipsets _COULD_ break.  Be wary
 	   of issuing another MMU command right after this */

	SMC_SET_INT( 0 );
}

/*
 . Function: smc_enable
 . Purpose: let the chip talk to the outside work
 . Method:
 .	1.  Enable the transmitter
 .	2.  Enable the receiver
 .	3.  Enable interrupts
*/
static void smc_enable( smcio_t ioaddr )
{
	SMC_SELECT_BANK( 0 );
	/* see the header file for options in TCR/RCR NORMAL*/
	outw( TCR_NORMAL, ioaddr + TCR );
	outw( RCR_NORMAL, ioaddr + RCR );

	/* now, enable interrupts */
	SMC_SELECT_BANK( 2 );
	SMC_SET_INT( SMC_INTERRUPT_MASK );
}

/*
 . Function: smc_shutdown
 . Purpose:  closes down the SMC91xxx chip.
 . Method:
 .	1. zero the interrupt mask
 .	2. clear the enable receive flag
 .	3. clear the enable xmit flags
 .
 . TODO:
 .   (1) maybe utilize power down mode.
 .	Why not yet?  Because while the chip will go into power down mode,
 .	the manual says that it will wake up in response to any I/O requests
 .	in the register space.   Empirical results do not show this working.
*/
static void smc_shutdown( smcio_t ioaddr )
{
	/* no more interrupts for me */
	SMC_SELECT_BANK( 2 );
	SMC_SET_INT( 0 );

	/* and tell the card to stay away from that nasty outside world */
	SMC_SELECT_BANK( 0 );
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	outw( RCR_CLEAR, ioaddr + RCR );
	outw( TCR_CLEAR, ioaddr + TCR );
#else
	outb( RCR_CLEAR, ioaddr + RCR );
	outb( TCR_CLEAR, ioaddr + TCR );
#endif /* CONFIG_COLDFIRE */
#if 0
	/* finally, shut the chip down */
	SMC_SELECT_BANK( 1 );
	outw( inw( ioaddr + CONTROL ), CTL_POWERDOWN, ioaddr + CONTROL  );
#endif
}


/*
 . Function: smc_setmulticast( smcio_t ioaddr, int count, dev_mc_list * adds )
 . Purpose:
 .    This sets the internal hardware table to filter out unwanted multicast
 .    packets before they take up memory.
 .
 .    The SMC chip uses a hash table where the high 6 bits of the CRC of
 .    address are the offset into the table.  If that bit is 1, then the
 .    multicast packet is accepted.  Otherwise, it's dropped silently.
 .
 .    To use the 6 bits as an offset into the table, the high 3 bits are the
 .    number of the 8 bit register, while the low 3 bits are the bit within
 .    that register.
 .
 . This routine is based very heavily on the one provided by Peter Cammaert.
*/


static void smc_setmulticast( smcio_t ioaddr, int count, struct dev_mc_list * addrs ) {
	int			i;
	unsigned char		multicast_table[ 8 ];
	struct dev_mc_list	* cur_addr;
	/* table for flipping the order of 3 bits */
	unsigned char invert3[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

	/* start with a table of all zeros: reject all */
	memset( multicast_table, 0, sizeof( multicast_table ) );

	cur_addr = addrs;
	for ( i = 0; i < count ; i ++, cur_addr = cur_addr->next  ) {
		int position;

		/* do we have a pointer here? */
		if ( !cur_addr )
			break;
		/* make sure this is a multicast address - shouldn't this
		   be a given if we have it here ? */
		if ( !( *cur_addr->dmi_addr & 1 ) )
			continue;

		/* only use the low order bits */
		position = ether_crc_le(6, cur_addr->dmi_addr) & 0x3f;

		/* do some messy swapping to put the bit in the right spot */
		multicast_table[invert3[position&7]] |=
					(1<<invert3[(position>>3)&7]);

	}
	/* now, the table can be loaded into the chipset */
	SMC_SELECT_BANK( 3 );

#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	for ( i = 0; i < 8 ; i += 2 ) {
		outw(((multicast_table[i+1]<<8)+(multicast_table[i])), ioaddr+MULTICAST1+i );
	}
#else
	for ( i = 0; i < 8 ; i++ ) {
		outb( multicast_table[i], ioaddr + MULTICAST1 + i );
	}
#endif
}


/*
 . Function: smc_wait_to_send_packet( struct sk_buff * skb, struct net_device * )
 . Purpose:
 .    Attempt to allocate memory for a packet, if chip-memory is not
 .    available, then tell the card to generate an interrupt when it
 .    is available.
 .
 . Algorithm:
 .
 . o	if the saved_skb is not currently null, then drop this packet
 .	on the floor.  This should never happen, because of TBUSY.
 . o	if the saved_skb is null, then replace it with the current packet,
 . o	See if I can sending it now.
 . o 	(NO): Enable interrupts and let the interrupt handler deal with it.
 . o	(YES):Send it now.
*/
static int smc_wait_to_send_packet( struct sk_buff * skb, struct net_device * dev )
{
	struct smc_local *lp = netdev_priv(dev);
	unsigned int ioaddr 	= dev->base_addr;
	word 			length;
	unsigned short 		numPages;
	word			time_out;

	netif_stop_queue(dev);
	/* Well, I want to send the packet.. but I don't know
	   if I can send it right now...  */

	if ( lp->saved_skb) {
		/* THIS SHOULD NEVER HAPPEN. */
		lp->stats.tx_aborted_errors++;
		printk(CARDNAME": Bad Craziness - sent packet while busy.\n" );
		return 1;
	}
	lp->saved_skb = skb;

	length = skb->len;

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN)) {
			netif_wake_queue(dev);
			return 0;
		}
		length = ETH_ZLEN;
	}

	/*
	** The MMU wants the number of pages to be the number of 256 bytes
	** 'pages', minus 1 ( since a packet can't ever have 0 pages :) )
	**
	** Pkt size for allocating is data length +6 (for additional status words,
	** length and ctl!) If odd size last byte is included in this header.
	*/
	numPages =  ((length & 0xfffe) + 6) / 256;

	if (numPages > 7 ) {
		printk(CARDNAME": Far too big packet error. \n");
		/* freeing the packet is a good thing here... but should
		 . any packets of this size get down here?   */
		dev_kfree_skb (skb);
		lp->saved_skb = NULL;
		/* this IS an error, but, i don't want the skb saved */
		netif_wake_queue(dev);
		return 0;
	}
	/* either way, a packet is waiting now */
	lp->packets_waiting++;

	/* now, try to allocate the memory */
	SMC_SELECT_BANK( 2 );
	outw( MC_ALLOC | numPages, ioaddr + MMU_CMD );
	/*
 	. Performance Hack
	.
 	. wait a short amount of time.. if I can send a packet now, I send
	. it now.  Otherwise, I enable an interrupt and wait for one to be
	. available.
	.
	. I could have handled this a slightly different way, by checking to
	. see if any memory was available in the FREE MEMORY register.  However,
	. either way, I need to generate an allocation, and the allocation works
	. no matter what, so I saw no point in checking free memory.
	*/
	time_out = MEMORY_WAIT_TIME;
	do {
		word	status;

		status = inb( ioaddr + INTERRUPT );
		if ( status & IM_ALLOC_INT ) {
			/* acknowledge the interrupt */
			SMC_ACK_INT( IM_ALLOC_INT );
  			break;
		}
   	} while ( -- time_out );

   	if ( !time_out ) {
		/* oh well, wait until the chip finds memory later */
		SMC_ENABLE_INT( IM_ALLOC_INT );
      		PRINTK2((CARDNAME": memory allocation deferred. \n"));
		/* it's deferred, but I'll handle it later */
      		return 0;
   	}
	/* or YES! I can send the packet now.. */
	smc_hardware_send_packet(dev);
	netif_wake_queue(dev);
	return 0;
}

/*
 . Function:  smc_hardware_send_packet(struct net_device * )
 . Purpose:
 .	This sends the actual packet to the SMC9xxx chip.
 .
 . Algorithm:
 . 	First, see if a saved_skb is available.
 .		( this should NOT be called if there is no 'saved_skb'
 .	Now, find the packet number that the chip allocated
 .	Point the data pointers at it in memory
 .	Set the length word in the chip's memory
 .	Dump the packet to chip memory
 .	Check if a last byte is needed ( odd length packet )
 .		if so, set the control flag right
 . 	Tell the card to send it
 .	Enable the transmit interrupt, so I know if it failed
 . 	Free the kernel data if I actually sent it.
*/
static void smc_hardware_send_packet( struct net_device * dev )
{
	struct smc_local *lp = netdev_priv(dev);
	byte	 		packet_no;
	struct sk_buff * 	skb = lp->saved_skb;
	word			length;
	smcio_t			ioaddr;
	byte			* buf;

	ioaddr = dev->base_addr;

	if ( !skb ) {
		PRINTK((CARDNAME": In XMIT with no packet to send \n"));
		return;
	}
	length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	buf = skb->data;

	/* If I get here, I _know_ there is a packet slot waiting for me */
	packet_no = inb( ioaddr + PNR_ARR + 1 );
	if ( packet_no & 0x80 ) {
		/* or isn't there?  BAD CHIP! */
		printk(KERN_DEBUG CARDNAME": Memory allocation failed. \n");
		dev_kfree_skb_any(skb);
		lp->saved_skb = NULL;
		netif_wake_queue(dev);
		return;
	}

	/* we have a packet address, so tell the card to use it */
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	outw( packet_no, ioaddr + PNR_ARR );
#else
	outb( packet_no, ioaddr + PNR_ARR );
#endif

	/* point to the beginning of the packet */
	outw( PTR_AUTOINC , ioaddr + POINTER );

   	PRINTK3((CARDNAME": Trying to xmit packet of length %x\n", length ));
#if SMC_DEBUG > 2
	print_packet( buf, length );
#endif

	/* send the packet length ( +6 for status, length and ctl byte )
 	   and the status word ( set to zeros ) */

#ifdef USE_32_BIT
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	outl(  (length +6 ) , ioaddr + DATA_1 );
#else
	outl(  (length +6 ) << 16 , ioaddr + DATA_1 );
#endif
#else
	outw( 0, ioaddr + DATA_1 );
	/* send the packet length ( +6 for status words, length, and ctl*/
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328) || defined(CONFIG_CPU_H8S)
	outw( (length+6) & 0xFFFF, ioaddr + DATA_1 );
#else
	outb( (length+6) & 0xFF,ioaddr + DATA_1 );
	outb( (length+6) >> 8 , ioaddr + DATA_1 );
#endif
#endif

	/* send the actual data
	 . I _think_ it's faster to send the longs first, and then
	 . mop up by sending the last word.  It depends heavily
 	 . on alignment, at least on the 486.  Maybe it would be
 	 . a good idea to check which is optimal?  But that could take
	 . almost as much time as is saved?
	*/
#ifdef USE_32_BIT
	if ( length & 0x2  ) {
		outsl(ioaddr + DATA_1, buf,  length >> 2 );
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
		outwd( *((word *)(buf + (length & 0xFFFFFFFC))),ioaddr +DATA_1);
#elif !defined(__H8300H__) && !defined(__H8300S__)
		outw( *((word *)(buf + (length & 0xFFFFFFFC))),ioaddr +DATA_1);
#else
		ctrl_outw( *((word *)(buf + (length & 0xFFFFFFFC))),ioaddr +DATA_1);
#endif
	}
	else
		outsl(ioaddr + DATA_1, buf,  length >> 2 );
#else
	outsw(ioaddr + DATA_1 , buf, (length ) >> 1);
#endif
	/* Send the last byte, if there is one.   */

	if ( (length & 1) == 0 ) {
		outw( 0, ioaddr + DATA_1 );
	} else {
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
		outw( buf[length -1 ] | (0x20 << 8), ioaddr + DATA_1);
#else
		outb( buf[length -1 ], ioaddr + DATA_1 );
		outb( 0x20, ioaddr + DATA_1);
#endif
	}

	/* enable the interrupts */
	SMC_ENABLE_INT( (IM_TX_INT | IM_TX_EMPTY_INT) );

	/* and let the chipset deal with it */
	outw( MC_ENQUEUE , ioaddr + MMU_CMD );

	PRINTK2((CARDNAME": Sent packet of length %d \n",length));

	lp->saved_skb = NULL;
	dev_kfree_skb_any (skb);

	dev->trans_start = jiffies;

	/* we can send another packet */
	netif_wake_queue(dev);

	return;
}

/*-------------------------------------------------------------------------
 |
 | smc_init(int unit)
 |   Input parameters:
 |	dev->base_addr == 0, try to find all possible locations
 |	dev->base_addr == 1, return failure code
 |	dev->base_addr == 2, always allocate space,  and return success
 |	dev->base_addr == <anything else>   this is the address to check
 |
 |   Output:
 |	pointer to net_device or ERR_PTR(error)
 |
 ---------------------------------------------------------------------------
*/
static int io;
static int irq;
static int ifport;

struct net_device * __init smc_init(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct smc_local));
	struct devlist *smcdev = smc_devlist;
	int err = 0;

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
		irq = dev->irq;
	}

	SET_MODULE_OWNER(dev);

	if (io > 0x1ff) {	/* Check a single specified location. */
		err = smc_probe(dev, io);
	} else if (io != 0) {	/* Don't probe at all. */
		err = -ENXIO;
	} else {
		for (port = smc_portlist; *port; port++) {
#ifdef CONFIG_NETtel
			smc_remap(port);
#endif
			if (smc_probe(dev, *port) == 0)
				break;
		}
		if (!smcdev->port)
			err = -ENODEV;
	}
	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, SMC_IO_EXTENT);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

/*----------------------------------------------------------------------
 . smc_findirq
 .
 . This routine has a simple purpose -- make the SMC chip generate an
 . interrupt, so an auto-detect routine can detect it, and find the IRQ,
 ------------------------------------------------------------------------
*/
#ifndef NO_AUTOPROBE
int __init smc_findirq( smcio_t ioaddr )
{
#ifndef NO_AUTOPROBE
	int	timeout = 20;
	unsigned long cookie;


#if 0
	/* I have to do a STI() here, because this is called from
	   a routine that does an CLI during this process, making it
	   rather difficult to get interrupts for auto detection */
	sti();
#endif

	cookie = probe_irq_on();

	/*
	 * What I try to do here is trigger an ALLOC_INT. This is done
	 * by allocating a small chunk of memory, which will give an interrupt
	 * when done.
	 */


	SMC_SELECT_BANK(2);
	/* enable ALLOCation interrupts ONLY */
	SMC_SET_INT( IM_ALLOC_INT );

	/*
 	 . Allocate 512 bytes of memory.  Note that the chip was just
	 . reset so all the memory is available
	*/
	outw( MC_ALLOC | 1, ioaddr + MMU_CMD );

	/*
	 . Wait until positive that the interrupt has been generated
	*/
	while ( timeout ) {
		byte	int_status;

		int_status = inb( ioaddr + INTERRUPT );

		if ( int_status & IM_ALLOC_INT )
			break;		/* got the interrupt */
		timeout--;
	}
	/* there is really nothing that I can do here if timeout fails,
	   as probe_irq_off will return a 0 anyway, which is what I
	   want in this case.   Plus, the clean up is needed in both
	   cases.  */

	/* DELAY HERE!
	   On a fast machine, the status might change before the interrupt
	   is given to the processor.  This means that the interrupt was
	   never detected, and probe_irq_off fails to report anything.
	   This should fix probe_irq_* problems.
	*/
	SMC_DELAY();
	SMC_DELAY();

	/* and disable all interrupts again */
	SMC_SET_INT( 0 );

#if 0
	/* clear hardware interrupts again, because that's how it
	   was when I was called... */
	cli();
#endif

	/* and return what I found */
	return probe_irq_off(cookie);
#else /* NO_AUTOPROBE */
	struct devlist *smcdev;
	for (smcdev = smc_devlist; smcdev->port; smcdev++) {
		if (smcdev->port == ioaddr)
			return smcdev->irq;
	}
	return 0;
#endif
}
#endif /* NO_AUTOPROBE */

/*----------------------------------------------------------------------
 . Function: smc_probe( smcio_t ioaddr )
 .
 . Purpose:
 .	Tests to see if a given ioaddr points to an SMC9xxx chip.
 .	Returns a 0 on success
 .
 . Algorithm:
 .	(1) see if the high byte of BANK_SELECT is 0x33
 . 	(2) compare the ioaddr with the base register's address
 .	(3) see if I recognize the chip ID in the appropriate register
 .
 .---------------------------------------------------------------------
 */

/*---------------------------------------------------------------
 . Here I do typical initialization tasks.
 .
 . o  Initialize the structure if needed
 . o  print out my vanity message if not done so already
 . o  print out what type of hardware is detected
 . o  print out the ethernet address
 . o  find the IRQ
 . o  set up my private data
 . o  configure the dev structure with my subroutines
 . o  actually GRAB the irq.
 . o  GRAB the region
 .-----------------------------------------------------------------
*/
static int __init smc_probe(struct net_device *dev, smcio_t ioaddr)
{
	int i, memory, retval;
	static unsigned version_printed;
	unsigned int bank;
#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || defined(CONFIG_DISKtel) || defined(CONFIG_CLEOPATRA)
	static int nr = 0;
#endif
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	unsigned char *ep;
#endif

	const char *version_string;
	const char *if_string;

	/* registers */
	word revision_register;
	word base_address_register;
	word configuration_register;
	word memory_info_register;
	word memory_cfg_register;

#if !defined(CONFIG_COLDFIRE) && !defined(CONFIG_M68EZ328) && \
    !defined(CONFIG_CPU_H8300H) && !defined(CONFIG_CPU_H8S)
	/* Grab the region so that no one else tries to probe our ioports. */
	if (!request_region(ioaddr, SMC_IO_EXTENT, DRV_NAME))
		return -EBUSY;
#elif defined(CONFIG_COLDFIRE)
	/*
	 *	We need to put the SMC into 68k mode.
	 *	Do a write before anything else.
	 */
	outw(0, ioaddr + BANK_SELECT);
#endif

	dev->irq = irq;
	dev->if_port = ifport;

	/* First, see if the high byte is 0x33 */
	bank = inw( ioaddr + BANK_SELECT );
	if ( (bank & 0xFF00) != 0x3300 ) {
		retval = -ENODEV;
		goto err_out;
	}
	/* The above MIGHT indicate a device, but I need to write to further
 	 	test this.  */
	outw( 0x0, ioaddr + BANK_SELECT );
	bank = inw( ioaddr + BANK_SELECT );
	if ( (bank & 0xFF00 ) != 0x3300 ) {
		retval = -ENODEV;
		goto err_out;
	}
	/* well, we've already written once, so hopefully another time won't
 	   hurt.  This time, I need to switch the bank register to bank 1,
	   so I can access the base address register */
#if !defined(CONFIG_CPU_H8300H) && !defined(CONFIG_CPU_H8S)
	SMC_SELECT_BANK(1);
	base_address_register = inw( ioaddr + BASE );
	if ( (ioaddr & 0x3E0) != ( base_address_register >> 3 & 0x3E0 ) )  {
		printk(CARDNAME ": IOADDR %x doesn't match configuration (%x)."
			"Probably not a SMC chip\n",
			ioaddr, base_address_register >> 3 & 0x3E0 );
		/* well, the base address register didn't match.  Must not have
		   been a SMC chip after all. */
		retval = -ENODEV;
		goto err_out;
	}
#else
	(void)base_address_register; /* Warning suppression */
#endif

	/*  check if the revision register is something that I recognize.
	    These might need to be added to later, as future revisions
	    could be added.  */
	SMC_SELECT_BANK(3);
	revision_register  = inw( ioaddr + REVISION );
	if ( !chip_ids[ ( revision_register  >> 4 ) & 0xF  ] ) {
		/* I don't recognize this chip, so... */
		printk(CARDNAME ": IO %x: Unrecognized revision register:"
			" %x, Contact author. \n", ioaddr, revision_register );

		retval = -ENODEV;
		goto err_out;
	}

	/* at this point I'll assume that the chip is an SMC9xxx.
	   It might be prudent to check a listing of MAC addresses
	   against the hardware address, or do some other tests. */
	if (version_printed++ == 0)
		printk("%s", version);

	/* fill in some of the fields */
	dev->base_addr = ioaddr;

#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || defined(CONFIG_DISKtel) || defined(CONFIG_CLEOPATRA)
	/*
	 . MAC address should be in FLASH, check that it is valid.
	 . If good use it, otherwise use the default.
	*/
	ep = (unsigned char *) (0xf0006000 + (nr++ * 6));
	if ((ep[0] == 0xff) && (ep[1] == 0xff) && (ep[2] == 0xff) &&
	    (ep[3] == 0xff) && (ep[4] == 0xff) && (ep[5] == 0xff))
		ep = (unsigned char *) &smc_defethaddr[0];
	else if ((ep[0] == 0) && (ep[1] == 0) && (ep[2] == 0) &&
	    (ep[3] == 0) && (ep[4] == 0) && (ep[5] == 0))
		ep = (unsigned char *) &smc_defethaddr[0];
#else
	ep = (unsigned char *) &smc_defethaddr[0];
#endif
#endif

	/*
 	 . Get the MAC address ( bank 1, regs 4 - 9 )
	*/
	SMC_SELECT_BANK( 1 );
	for ( i = 0; i < 6; i += 2 ) {
		word	address;

#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
		dev->dev_addr[ i ] = ep[ i ];
		dev->dev_addr[ i + 1 ] = ep[ i + 1 ];
		address = (((word) ep[ i ]) << 8) | ep[ i + 1 ];
		outw( address, ioaddr + ADDR0 + i);
#else
		address = inw( ioaddr + ADDR0 + i  );
		dev->dev_addr[ i + 1] = address >> 8;
		dev->dev_addr[ i ] = address & 0xFF;	
#endif
	}

#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	/* HACK: to support 2 ethernets when using default address! */
	smc_defethaddr[5]++;
#endif

	/* get the memory information */

	SMC_SELECT_BANK( 0 );
	memory_info_register = inw( ioaddr + MIR );
	memory_cfg_register  = inw( ioaddr + MCR );
	memory = ( memory_cfg_register >> 9 )  & 0x7;  /* multiplier */
	memory *= 256 * ( memory_info_register & 0xFF );

	/*
	 Now, I want to find out more about the chip.  This is sort of
 	 redundant, but it's cleaner to have it in both, rather than having
 	 one VERY long probe procedure.
	*/
	SMC_SELECT_BANK(3);
	revision_register  = inw( ioaddr + REVISION );
	version_string = chip_ids[ ( revision_register  >> 4 ) & 0xF  ];
	if ( !version_string ) {
		/* I shouldn't get here because this call was done before.... */
		retval = -ENODEV;
		goto err_out;
	}

	/* is it using AUI or 10BaseT ? */
	if ( dev->if_port == 0 ) {
		SMC_SELECT_BANK(1);
		configuration_register = inw( ioaddr + CONFIG );
		if ( configuration_register & CFG_AUI_SELECT )
			dev->if_port = 2;
		else
			dev->if_port = 1;
	}
	if_string = interfaces[ dev->if_port - 1 ];

	/* now, reset the chip, and put it into a known state */
	smc_reset( ioaddr );

	/*
	 . If dev->irq is 0, then the device has to be banged on to see
	 . what the IRQ is.
 	 .
	 . This banging doesn't always detect the IRQ, for unknown reasons.
	 . a workaround is to reset the chip and try again.
	 .
	 . Interestingly, the DOS packet driver *SETS* the IRQ on the card to
	 . be what is requested on the command line.   I don't do that, mostly
	 . because the card that I have uses a non-standard method of accessing
	 . the IRQs, and because this _should_ work in most configurations.
	 .
	 . Specifying an IRQ is done with the assumption that the user knows
	 . what (s)he is doing.  No checking is done!!!!
 	 .
	*/
#ifndef NO_AUTOPROBE
	if ( dev->irq < 2 ) {
		int	trials;

		trials = 3;
		while ( trials-- ) {
			dev->irq = smc_findirq( ioaddr );
			if ( dev->irq )
				break;
			/* kick the card and try again */
			smc_reset( ioaddr );
		}
	}
	if (dev->irq == 0 ) {
		printk(CARDNAME": Couldn't autodetect your IRQ. Use irq=xx.\n");
		retval = -ENODEV;
		goto err_out;
	}
#else
	if (dev->irq == 0 ) {
		printk(CARDNAME
		": Autoprobing IRQs is not supported for this configuration.\n");
		return -ENODEV;
	}
#endif

	/* now, print out the card info, in a short format.. */

	printk("%s: %s(r:%d) at %#3x IRQ:%d INTF:%s MEM:%db ", dev->name,
		version_string, revision_register & 0xF, ioaddr, dev->irq,
		if_string, memory );
	/*
	 . Print the Ethernet address
	*/
	printk("ADDR: ");
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i] );
	printk("%2.2x \n", dev->dev_addr[5] );

	/* set the private data to zero by default */
	memset(dev->priv, 0, sizeof(struct smc_local));

	/* Grab the IRQ */
#ifdef CONFIG_COLDFIRE
	mcf_autovector(dev->irq);
    retval = request_irq(dev->irq, &smc_interrupt, 0, dev->name, dev);
#elif defined(CONFIG_M68EZ328) && !defined(CONFIG_CWEZ328) && !defined(CONFIG_CWVZ328)
	retval = request_irq(IRQ_MACHSPEC | dev->irq, &smc_interrupt,
			IRQ_FLG_STD, dev->name, dev);
	if (retval) panic("Unable to attach Lan91C96 intr\n");
#else
	retval = request_irq(dev->irq, &smc_interrupt, 0, DRV_NAME, dev);
#endif
	if (retval) {
		printk("%s: unable to get IRQ %d (irqval=%d).\n", dev->name,
			dev->irq, retval);
  	  	goto err_out;
	}

	dev->open		        = smc_open;
	dev->stop		        = smc_close;
	dev->hard_start_xmit    	= smc_wait_to_send_packet;
	dev->tx_timeout		    	= smc_timeout;
	dev->watchdog_timeo		= HZ/20;
	dev->get_stats			= smc_query_statistics;
	dev->set_multicast_list 	= smc_set_multicast_list;

#ifdef PHY_SETUP
	setup_phy( ioaddr );
#endif
	return 0;

err_out:
	release_region(ioaddr, SMC_IO_EXTENT);
	return retval;
}

#if SMC_DEBUG > 2
static void print_packet( byte * buf, int length )
{
#if 0
	int i;
	int remainder;
	int lines;

	printk("Packet of length %d \n", length );
	lines = length / 16;
	remainder = length % 16;

	for ( i = 0; i < lines ; i ++ ) {
		int cur;

		for ( cur = 0; cur < 8; cur ++ ) {
			byte a, b;

			a = *(buf ++ );
			b = *(buf ++ );
			printk("%02x%02x ", a, b );
		}
		printk("\n");
	}
	for ( i = 0; i < remainder/2 ; i++ ) {
		byte a, b;

		a = *(buf ++ );
		b = *(buf ++ );
		printk("%02x%02x ", a, b );
	}
	printk("\n");
#endif
}
#endif


/*
 * Open and Initialize the board
 *
 * Set up everything, reset the card, etc ..
 *
 */
static int smc_open(struct net_device *dev)
{
	smcio_t	ioaddr = dev->base_addr;

	int	i;	/* used to set hw ethernet address */

	/* clear out all the junk that was put here before... */
	memset(dev->priv, 0, sizeof(struct smc_local));

	/* reset the hardware */

	smc_reset( ioaddr );
	smc_enable( ioaddr );

	/* Select which interface to use */

	SMC_SELECT_BANK( 1 );
#if defined(CONFIG_DISKtel) || defined(CONFIG_SH_KEYWEST)
	/* Setup to use external PHY on smc91c110 */
	outw( inw( ioaddr + CONFIG ) | CFG_NO_WAIT | CFG_MII_SELECT,
		(ioaddr + CONFIG ));
#else
	if ( dev->if_port == 1 ) {
		outw( inw( ioaddr + CONFIG ) & ~CFG_AUI_SELECT,
			ioaddr + CONFIG );
	}
	else if ( dev->if_port == 2 ) {
		outw( inw( ioaddr + CONFIG ) | CFG_AUI_SELECT,
			ioaddr + CONFIG );
	}
#endif

	/*
  		According to Becker, I have to set the hardware address
		at this point, because the (l)user can set it with an
		ioctl.  Easily done...
	*/
	SMC_SELECT_BANK( 1 );
	for ( i = 0; i < 6; i += 2 ) {
		word	address;

		address = dev->dev_addr[ i + 1 ] << 8 ;
		address  |= dev->dev_addr[ i ];
		outw( address, ioaddr + ADDR0 + i );
	}

	netif_start_queue(dev);

#if defined(CONFIG_LEDMAN) && defined(CONFIG_SNAPGEAR)
	/*
	 *	fix the link status LED's
	 */
	SMC_SELECT_BANK( 0 );
	ledman_cmd((inw(ioaddr + EPH_STATUS) & ES_LINK_OK) == ES_LINK_OK ?
			LEDMAN_CMD_ON : LEDMAN_CMD_OFF,
			strcmp(dev->name, "eth0") ?
					LEDMAN_LAN2_LINK : LEDMAN_LAN1_LINK);
#endif

	return 0;
}

/*--------------------------------------------------------
 . Called by the kernel to send a packet out into the void
 . of the net.  This routine is largely based on
 . skeleton.c, from Becker.
 .--------------------------------------------------------
*/

static void smc_timeout(struct net_device *dev)
{
	/* If we get here, some higher level has decided we are broken.
	   There should really be a "kick me" function call instead. */
	printk(KERN_WARNING CARDNAME": transmit timed out, %s?\n",
		tx_done(dev) ? "IRQ conflict" :
		"network cable problem");
	/* "kick" the adaptor */
	smc_reset( dev->base_addr );
	smc_enable( dev->base_addr );
	dev->trans_start = jiffies;
	/* clear anything saved */
	((struct smc_local *)dev->priv)->saved_skb = NULL;
	netif_wake_queue(dev);
}

/*-------------------------------------------------------------
 .
 . smc_rcv -  receive a packet from the card
 .
 . There is ( at least ) a packet waiting to be read from
 . chip-memory.
 .
 . o Read the status
 . o If an error, record it
 . o otherwise, read in the packet
 --------------------------------------------------------------
*/
static void smc_rcv(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int packet_number;
	word status, packet_length;

	/* assume bank 2 */

	packet_number = inw( ioaddr + FIFO_PORTS );

	if ( packet_number & FP_RXEMPTY ) {
		/* we got called , but nothing was on the FIFO */
		PRINTK((CARDNAME ": WARNING: smc_rcv with nothing on FIFO. \n"));
		/* don't need to restore anything */
		return;
	}

	/*  start reading from the start of the packet */
	outw( PTR_READ | PTR_RCV | PTR_AUTOINC, ioaddr + POINTER );

	/* First two words are status and packet_length */
#ifndef CONFIG_SH_KEYWEST
	status 		= inw( ioaddr + DATA_1 );
	packet_length 	= inw( ioaddr + DATA_1 );
#else
	{
		unsigned int l = inl( ioaddr + DATA_1 );
		status         = l & 0xffff;
		packet_length  = l >> 16;
	}
#endif

	packet_length &= 0x07ff;  /* mask off top bits */

	PRINTK2(("RCV: STATUS %4x LENGTH %4x\n", status, packet_length ));
	/*
	 . the packet length contains 3 extra words :
	 . status, length, and an extra word with an odd byte .
	*/
	packet_length -= 6;

	if ( !(status & RS_ERRORS ) ){
		/* do stuff to make a new packet */
		struct sk_buff  * skb;
		byte		* data;

		/* read one extra byte */
		if ( status & RS_ODDFRAME )
			packet_length++;

		/* set multicast stats */
		if ( status & RS_MULTICAST )
			lp->stats.multicast++;

		skb = dev_alloc_skb( packet_length + 5);

		if ( skb == NULL ) {
			printk(KERN_NOTICE CARDNAME ": Low memory, packet dropped.\n");
			lp->stats.rx_dropped++;
			goto done;
		}

		/*
		 ! This should work without alignment, but it could be
		 ! in the worse case
		*/

		skb_reserve( skb, 2 );   /* 16 bit alignment */

		skb->dev = dev;
		data = skb_put( skb, packet_length);

#ifdef USE_32_BIT
		/* QUESTION:  Like in the TX routine, do I want
		   to send the DWORDs or the bytes first, or some
		   mixture.  A mixture might improve already slow PIO
		   performance  */
		PRINTK3((" Reading %d dwords (and %d bytes) \n",
			packet_length >> 2, packet_length & 3 ));
		insl(ioaddr + DATA_1 , data, packet_length >> 2 );
		/* read the left over bytes */
#ifndef CONFIG_SH_KEYWEST
		insb( ioaddr + DATA_1, data + (packet_length & 0xFFFFFC),
			packet_length & 0x3  );
#else
		if (packet_length & 3) {
			union { unsigned int l; char data[4]; } l;
			l.l = inl(ioaddr + DATA_1);
			memcpy(data + (packet_length & ~0x3), l.data, packet_length & 0x3);
		}
#endif
#else
		PRINTK3((" Reading %d words and %d byte(s) \n",
			(packet_length >> 1 ), packet_length & 1 ));
		insw(ioaddr + DATA_1 , data, packet_length >> 1);
		if ( packet_length & 1 ) {
			data += packet_length & ~1;
			*(data++) = inb( ioaddr + DATA_1 );
		}
#endif
#if	SMC_DEBUG > 2
			print_packet( data, packet_length );
#endif

		skb->protocol = eth_type_trans(skb, dev );
		netif_rx(skb);
		dev->last_rx = jiffies;
		lp->stats.rx_packets++;
		lp->stats.rx_bytes += packet_length;
	} else {
		/* error ... */
		lp->stats.rx_errors++;

		if ( status & RS_ALGNERR )  lp->stats.rx_frame_errors++;
		if ( status & (RS_TOOSHORT | RS_TOOLONG ) )
			lp->stats.rx_length_errors++;
		if ( status & RS_BADCRC)	lp->stats.rx_crc_errors++;
	}

done:
	/*  error or good, tell the card to get rid of this packet */
	outw( MC_RELEASE, ioaddr + MMU_CMD );
}


/*************************************************************************
 . smc_tx
 .
 . Purpose:  Handle a transmit error message.   This will only be called
 .   when an error, because of the AUTO_RELEASE mode.
 .
 . Algorithm:
 .	Save pointer and packet no
 .	Get the packet no from the top of the queue
 .	check if it's valid ( if not, is this an error??? )
 .	read the status word
 .	record the error
 .	( resend?  Not really, since we don't want old packets around )
 .	Restore saved values
 ************************************************************************/
static void smc_tx( struct net_device * dev )
{
	int ioaddr = dev->base_addr;
	struct smc_local *lp = netdev_priv(dev);
	byte saved_packet;
	byte packet_no;
	word tx_status;


	/* assume bank 2  */

	saved_packet = inb( ioaddr + PNR_ARR );
	packet_no = inw( ioaddr + FIFO_PORTS );
	packet_no &= 0x7F;

	/* select this as the packet to read from */
#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	outw( packet_no, ioaddr + PNR_ARR ); 
#else
	outb( packet_no, ioaddr + PNR_ARR ); 
#endif
	

	/* read the first word from this packet */
	outw( PTR_AUTOINC | PTR_READ, ioaddr + POINTER );

	tx_status = inw( ioaddr + DATA_1 );
	PRINTK3((CARDNAME": TX DONE STATUS: %4x \n", tx_status ));

	lp->stats.tx_errors++;
	if ( tx_status & TS_LOSTCAR ) lp->stats.tx_carrier_errors++;
	if ( tx_status & TS_LATCOL  ) {
#if 0
		printk(KERN_DEBUG CARDNAME
			": Late collision occurred on last xmit.\n");
#endif
		lp->stats.tx_window_errors++;
	}
#if 0
		if ( tx_status & TS_16COL ) { ... }
#endif

	if ( tx_status & TS_SUCCESS ) {
		printk(CARDNAME": Successful packet caused interrupt \n");
	}
	/* re-enable transmit */
	SMC_SELECT_BANK( 0 );
	outw( inw( ioaddr + TCR ) | TCR_ENABLE, ioaddr + TCR );

	/* kill the packet */
	SMC_SELECT_BANK( 2 );
	outw( MC_FREEPKT, ioaddr + MMU_CMD );

	/* one less packet waiting for me */
	lp->packets_waiting--;

#if defined(CONFIG_COLDFIRE) || defined(CONFIG_M68EZ328)
	outw( saved_packet, ioaddr + PNR_ARR );
#else
	outb( saved_packet, ioaddr + PNR_ARR );
#endif
	return;
}

/*--------------------------------------------------------------------
 .
 . This is the main routine of the driver, to handle the device when
 . it needs some attention.
 .
 . So:
 .   first, save state of the chipset
 .   branch off into routines to handle each case, and acknowledge
 .	    each to the interrupt register
 .   and finally restore state.
 .
 ---------------------------------------------------------------------*/

static irqreturn_t smc_interrupt(int irq, void * dev_id)
{
	struct net_device *dev 	= dev_id;
	int ioaddr 		= dev->base_addr;
	struct smc_local *lp = netdev_priv(dev);

	byte	status;
	word	card_stats;
	byte	mask;
	int	timeout;
	/* state registers */
	word	saved_bank;
	word	saved_pointer;
	int handled = 0;


	PRINTK3((CARDNAME": SMC interrupt started \n"));

	saved_bank = inw( ioaddr + BANK_SELECT );

	SMC_SELECT_BANK(2);
	saved_pointer = inw( ioaddr + POINTER );

	mask = inb( ioaddr + INT_MASK );
	/* clear all interrupts */
	outb( 0, ioaddr + INT_MASK );


	/* set a timeout value, so I don't stay here forever */
	timeout = 4;

	PRINTK2((KERN_WARNING CARDNAME ": MASK IS %x \n", mask ));
	do {
		/* read the status flag, and mask it */
		status = inb( ioaddr + INTERRUPT ) & mask;
		if (!status )
			break;

		handled = 1;

		PRINTK3((KERN_WARNING CARDNAME
			": Handling interrupt status %x \n", status ));

		if (status & IM_RCV_INT) {
			/* Got a packet(s). */
			PRINTK2((KERN_WARNING CARDNAME
				": Receive Interrupt\n"));
			smc_rcv(dev);
		} else if (status & IM_TX_INT ) {
			PRINTK2((KERN_WARNING CARDNAME
				": TX ERROR handled\n"));
			smc_tx(dev);
			outb(IM_TX_INT, ioaddr + INTERRUPT );
		} else if (status & IM_TX_EMPTY_INT ) {
			/* update stats */
			SMC_SELECT_BANK( 0 );
			card_stats = inw( ioaddr + COUNTER );
			/* single collisions */
			lp->stats.collisions += card_stats & 0xF;
			card_stats >>= 4;
			/* multiple collisions */
			lp->stats.collisions += card_stats & 0xF;

			/* these are for when linux supports these statistics */

			SMC_SELECT_BANK( 2 );
			PRINTK2((KERN_WARNING CARDNAME
				": TX_BUFFER_EMPTY handled\n"));
			outb( IM_TX_EMPTY_INT, ioaddr + INTERRUPT );
			mask &= ~IM_TX_EMPTY_INT;
			lp->stats.tx_packets += lp->packets_waiting;
			lp->packets_waiting = 0;

		} else if (status & IM_ALLOC_INT ) {
			PRINTK2((KERN_DEBUG CARDNAME
				": Allocation interrupt \n"));
			/* clear this interrupt so it doesn't happen again */
			mask &= ~IM_ALLOC_INT;

			smc_hardware_send_packet( dev );

			/* enable xmit interrupts based on this */
			mask |= ( IM_TX_EMPTY_INT | IM_TX_INT );

			/* and let the card send more packets to me */
			netif_wake_queue(dev);

			PRINTK2((CARDNAME": Handoff done successfully.\n"));
		} else if (status & IM_RX_OVRN_INT ) {
			lp->stats.rx_errors++;
			lp->stats.rx_fifo_errors++;
			outb( IM_RX_OVRN_INT, ioaddr + INTERRUPT );
		} else if (status & IM_EPH_INT ) {
			PRINTK((CARDNAME ": UNSUPPORTED: EPH INTERRUPT \n"));
		} else if (status & IM_ERCV_INT ) {
			PRINTK((CARDNAME ": UNSUPPORTED: ERCV INTERRUPT \n"));
			outb( IM_ERCV_INT, ioaddr + INTERRUPT );
		}
	} while ( timeout -- );


	/* restore state register */
	SMC_SELECT_BANK( 2 );
	outb( mask, ioaddr + INT_MASK );

	PRINTK3(( KERN_WARNING CARDNAME ": MASK is now %x \n", mask ));
	outw( saved_pointer, ioaddr + POINTER );

	SMC_SELECT_BANK( saved_bank );

	PRINTK3((CARDNAME ": Interrupt done\n"));
	return IRQ_RETVAL(handled);
}


/*----------------------------------------------------
 . smc_close
 .
 . this makes the board clean up everything that it can
 . and not talk to the outside world.   Caused by
 . an 'ifconfig ethX down'
 .
 -----------------------------------------------------*/
static int smc_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	/* clear everything */
	smc_shutdown( dev->base_addr );

#if defined(CONFIG_LEDMAN) && defined(CONFIG_SNAPGEAR)
	ledman_cmd(LEDMAN_CMD_OFF,
		strcmp(dev->name, "eth0")?LEDMAN_LAN2_LINK : LEDMAN_LAN1_LINK);
#endif

	/* Update the statistics here. */
	return 0;
}

/*------------------------------------------------------------
 . Get the current statistics.
 . This may be called with the card open or closed.
 .-------------------------------------------------------------*/
static struct net_device_stats* smc_query_statistics(struct net_device *dev) {
	struct smc_local *lp = netdev_priv(dev);

	return &lp->stats;
}

/*-----------------------------------------------------------
 . smc_set_multicast_list
 .
 . This routine will, depending on the values passed to it,
 . either make it accept multicast packets, go into
 . promiscuous mode ( for TCPDUMP and cousins ) or accept
 . a select set of multicast packets
*/
static void smc_set_multicast_list(struct net_device *dev)
{
	smcio_t ioaddr = dev->base_addr;

	SMC_SELECT_BANK(0);
	if ( dev->flags & IFF_PROMISC )
		outw( inw(ioaddr + RCR ) | RCR_PROMISC, ioaddr + RCR );

/* BUG?  I never disable promiscuous mode if multicasting was turned on.
   Now, I turn off promiscuous mode, but I don't do anything to multicasting
   when promiscuous mode is turned on.
*/

	/* Here, I am setting this to accept all multicast packets.
	   I don't need to zero the multicast table, because the flag is
	   checked before the table is
	*/
	else if (dev->flags & IFF_ALLMULTI)
		outw( inw(ioaddr + RCR ) | RCR_ALMUL, ioaddr + RCR );

	/* We just get all multicast packets even if we only want them
	 . from one source.  This will be changed at some future
	 . point. */
	else if (dev->mc_count )  {
		/* support hardware multicasting */

		/* be sure I get rid of flags I might have set */
		outw( inw( ioaddr + RCR ) & ~(RCR_PROMISC | RCR_ALMUL),
			ioaddr + RCR );
		/* NOTE: this has to set the bank, so make sure it is the
		   last thing called.  The bank is set to zero at the top */
		smc_setmulticast( ioaddr, dev->mc_count, dev->mc_list );
	}
	else  {
		outw( inw( ioaddr + RCR ) & ~(RCR_PROMISC | RCR_ALMUL),
			ioaddr + RCR );

		/*
		  since I'm disabling all multicast entirely, I need to
		  clear the multicast list
		*/
		SMC_SELECT_BANK( 3 );
		outw( 0, ioaddr + MULTICAST1 );
		outw( 0, ioaddr + MULTICAST2 );
		outw( 0, ioaddr + MULTICAST3 );
		outw( 0, ioaddr + MULTICAST4 );
	}
}

#ifdef PHY_SETUP
static int phy_delay1 = 4;
static int phy_delay2 = 1;
static int phy_delay3 = 100;
#endif

#ifdef MODULE

static struct net_device *devSMC9194;
MODULE_LICENSE("GPL");

module_param(io, int, 0);
module_param(irq, int, 0);
module_param(ifport, int, 0);
MODULE_PARM_DESC(io, "SMC 99194 I/O base address");
MODULE_PARM_DESC(irq, "SMC 99194 IRQ number");
MODULE_PARM_DESC(ifport, "SMC 99194 interface port (0-default, 1-TP, 2-AUI)");

#ifdef PHY_SETUP
MODULE_PARM(phy_delay1, "i");
MODULE_PARM(phy_delay2, "i");
MODULE_PARM(phy_delay3, "i");
MODULE_PARM_DESC(phy_delay1, "Per MII clock delay [4]");
MODULE_PARM_DESC(phy_delay2, "General delay [1]");
MODULE_PARM_DESC(phy_delay3, "pre probe delay [100]");
#endif

int __init init_module(void)
{
	if (io == 0)
		printk(KERN_WARNING
		CARDNAME": You shouldn't use auto-probing with insmod!\n" );

#ifdef PHY_SETUP
	printk(CARDNAME ": phy_delays %d %d %d\n", phy_delay1, phy_delay2,
			phy_delay3);
#endif
	/* copy the parameters from insmod into the device structure */
	devSMC9194 = smc_init(-1);
	if (IS_ERR(devSMC9194))
		return PTR_ERR(devSMC9194);
	return 0;
}

void __exit cleanup_module(void)
{
	unregister_netdev(devSMC9194);
	free_irq(devSMC9194->irq, devSMC9194);
	release_region(devSMC9194->base_addr, SMC_IO_EXTENT);
	free_netdev(devSMC9194);
}

#endif /* MODULE */


#ifdef PHY_SETUP
/*-----------------------------------------------------------
 . PHY/MII setup routines
 .
*/

#define phy_delay(x) ({ int d; for (d = 0; d < 100; d++) udelay((x) * 10); })

/*
 *	Ports for talking to the PHY/MII
 */

#define NV_CONTROL	0x10
#define MIICTRL		0x30
#define MIIDATA		0x34
#define MIICFG		0x38

#define MIIREAD		0x0001
#define MIIWRITE	0x0002

#define	MDO			0x01		/* MII Register bits */
#define	MDI			0x02
#define	MCLK		0x04
#define	MDOE		0x08
#define	MALL 		0x0F
#define	OPWrite		0x01
#define	OPRead		0x02


#define PHY_CR			0		/* PHY Registers and bits */
#define PHY_CR_Reset	0x8000
#define PHY_CR_Speed	0x2000
#define PHY_CR_Duplex	0x0100

#define PHY_SR	1
#define PHY_ID1	2
#define PHY_ID2	3

/*
 *	PHY propietary registers
 */

#define PHY_NATIONAL_PAR			0x19
#define PHY_NATIONAL_PAR_DUPLEX		0x0080
#define PHY_NATIONAL_PAR_SPEED_10	0x0040

#define PHY_TDK_DIAG				0x12
#define PHY_TDK_DIAG_DUPLEX			0x0800
#define PHY_TDK_DIAG_RATE			0x0400

#define PHY_QSI_BASETX				0x1F
#define PHY_QSI_BASETX_OPMODE_MASK	0x001c
#define PHY_QSI_BASETX_OPMODE_10HD	(2<<0x1)
#define PHY_QSI_BASETX_OPMODE_100HD	(2<<0x2)
#define PHY_QSI_BASETX_OPMODE_10FD	(2<<0x5)
#define PHY_QSI_BASETX_OPMODE_100FD	(2<<0x6)

#define PHY_SEEQ_STATUS_OUTPUT		0x12
#define PHY_SEEQ_SPD_DET			0x80
#define PHY_SEEQ_DPLX_DET			0x40

#define PHY_OUI_QSI			0x006051
#define PHY_OUI_TDK			0x00C039
#define PHY_OUI_MITELSMSC	0x00A087
#define PHY_OUI_NATIONAL	0x080017
#define PHY_OUI_SEEQSMSC	0x0005BE

#define NWAY_TIMEOUT	10

#define MAC_IS_FEAST()	(1)
#define MAC_IS_EPIC()	(0)

static void
clkmdio(smcio_t ioaddr, unsigned int MGMTData)
{
	outw(MGMTData, ioaddr + MGMT);
	udelay(phy_delay1);
	outw(MGMTData | MCLK, ioaddr + MGMT);
	udelay(phy_delay1);
}


static unsigned
PHYAccess(
	smcio_t ioaddr,
	unsigned char PHYAdd,
	unsigned char RegAdd,
	unsigned char OPCode,
	unsigned wData)
{
	int i;
	unsigned MGMTval;

	// Filter unused bits from input variables.

	PHYAdd &= 0x1F;
	RegAdd &= 0x1F;
	OPCode &= 0x03;

	if (MAC_IS_FEAST()) {
		MGMTval = inw(ioaddr + MGMT) & (MALL ^ 0xFFFF);

		// Output Preamble (32 '1's)

		for (i = 0; i < 32; i++)
			clkmdio(ioaddr, MGMTval | MDOE | MDO);

		// Output Start of Frame ('01')

		for (i = 0; i < 2; i++)
			clkmdio(ioaddr, MGMTval | MDOE | i);

		// Output OPCode ('01' for write or '10' for Read)

		for (i = 1; i >= 0; i--)
			clkmdio(ioaddr, MGMTval | MDOE | ((OPCode>>i) & 0x01) );

		// Output PHY Address

		for (i = 4; i >= 0; i--)
			clkmdio(ioaddr, MGMTval | MDOE | ((PHYAdd>>i) & 0x01) );

		// Output Register Address

		for (i = 4; i >= 0; i--)
			clkmdio(ioaddr, MGMTval | MDOE | ((RegAdd>>i) & 0x01) );

		if (OPCode == OPRead) {
			// Read Operation

			// Implement Turnaround ('Z0')

			clkmdio(ioaddr, MGMTval);
			// clkmdio(ioaddr, MGMTval | MDOE);

			// Read Data

			wData = 0;

			for (i = 15; i >= 0; i--) {
				clkmdio(ioaddr, MGMTval);
				wData |= (((inw(ioaddr + MGMT) & MDI) >> 1) << i);
			}

			// Add Idle state

			clkmdio(ioaddr, MGMTval);

			return (wData);
		} else {
			// Write Operation

			// Implement Turnaround ('10')

			for (i = 1; i >= 0; i--)
				clkmdio(ioaddr, MGMTval | MDOE | ((2>>i) & 0x01));

			// Write Data

			for (i = 15; i >= 0; i--)
				clkmdio(ioaddr, MGMTval | MDOE | ((wData>>i) & 0x01));

			// Add Idle state

			clkmdio(ioaddr, MGMTval);

			return (1);
		}
	}

	if (MAC_IS_EPIC()) {
		if (OPCode == OPRead) {
			// Read Operation
			outw((((unsigned)PHYAdd)<<9) | (((unsigned)RegAdd)<<4) | MIIREAD,
					ioaddr + MIICTRL);
			phy_delay(phy_delay2);
			wData = inw(MIIDATA);
			return(wData);
		} else {
			// Write Operation
			outw(wData, ioaddr + MIIDATA);
			outw((((unsigned)PHYAdd)<<9) | (((unsigned)RegAdd)<<4) | MIIWRITE,
					ioaddr + MIICTRL);
			phy_delay(phy_delay2);
			return(1);
		}
	}

	return(1);

}


static unsigned char
DetectPHY(
	smcio_t ioaddr,
	unsigned long *OUI,
	unsigned char *Model,
	unsigned char *Revision)
{
    unsigned int PhyId1, PhyId2;
    unsigned char PhyAdd=0xff;
    int Count;

    for (Count=31; Count >= 0; Count--) {
		PhyId1 = PHYAccess(ioaddr, Count, PHY_ID1, OPRead, 0);
		PhyId1 = PHYAccess(ioaddr, Count, PHY_ID1, OPRead, 0);
		PhyId2 = PHYAccess(ioaddr, Count, PHY_ID2, OPRead, 0);
		PhyId2 = PHYAccess(ioaddr, Count, PHY_ID2, OPRead, 0);

		if (PhyId1 > 0x0000 && PhyId1 < 0xffff && PhyId2 > 0x0000 &&
			PhyId2 < 0xffff && PhyId1 != 0x8000 && PhyId2 != 0x8000) {
			PhyAdd = (unsigned char) Count;
			break;
		}
		phy_delay(phy_delay2);
    }

    *OUI =		(((unsigned long) PhyId1) << 6) | ((PhyId2 & 0xfc00) >> 10);
    *Model =	(unsigned char) ((PhyId2 & 0x03f0) >> 4);
    *Revision =	(unsigned char) (PhyId2 & 0x000f);

    return(PhyAdd);
}


static int
setup_phy(smcio_t ioaddr)
{
    int duplex = 0; /* 0 = Half,   !0 = Full */
    int speed = 0;  /* 0 = 10Mbps, !0 = 100Mbps */
    char *report = "";
	unsigned long OUI;
	unsigned char Model, Revision;

    unsigned int i, PHYConfig, PHYConfig2, data;
    unsigned char PHYAdd, ositech = 0;

	printk("SMCPHY: ");
#if 0
	ositech = 1;
#endif

	//Setting the AUI Select Bit for 91C110 PCMCIA Design. 11/23/99 PG
	if (ositech) {
		SMC_SELECT_BANK( 1 );
		data = inw(ioaddr + BANK_SELECT);
		outw(data | 0x0100, ioaddr);
	}

    if (MAC_IS_FEAST())
		SMC_SELECT_BANK ( 3 );

	PHYAdd = DetectPHY(ioaddr, &OUI, &Model, &Revision);

	if (PHYAdd > 31) {
	    printk("UNRECOVERABLE ERROR: PHY is not present or not supported\n");
	    return(-1);
	}

	//Setup NV_CONTROL for the cardbus card.
	if (OUI == PHY_OUI_TDK)
		outw(0x7c03, ioaddr + NV_CONTROL);

	// Save Register 0.

	if (OUI == PHY_OUI_TDK)
		PHYAccess(ioaddr, PHYAdd, PHY_CR, OPRead, 0);
	PHYConfig = PHYAccess(ioaddr, PHYAdd,PHY_CR,OPRead,0);

	if (OUI == PHY_OUI_TDK) {
		outw(0x0012, ioaddr + MIICFG);	/* Set ENABLE_694 */
		/* if using EPIC, Hardware Reset the PHY from the MAC */
		outw(inw(ioaddr + CONTROL) | 0x4000, ioaddr + CONTROL);
		phy_delay(phy_delay2);
		outw(inw(ioaddr + CONTROL) & (~0x4000), ioaddr + CONTROL);
		phy_delay(phy_delay2);
	}

	/* Reset PHY */
	PHYAccess(ioaddr, PHYAdd, PHY_CR, OPWrite, PHY_CR_Reset);
	if (OUI == PHY_OUI_TDK)
		PHYAccess(ioaddr, PHYAdd, PHY_CR, OPWrite, PHY_CR_Reset);

	for (i = 0; i < 500; i++) {
		if (OUI == PHY_OUI_TDK)
			PHYAccess(ioaddr, PHYAdd, PHY_CR, OPRead, 0);

		if (PHYAccess(ioaddr, PHYAdd, PHY_CR, OPRead, 0) & PHY_CR_Reset)
			phy_delay(phy_delay2);
		else
			break;
	}

	if (i == 500) {
		printk("UNRECOVERABLE ERROR: Could not reset PHY\n");
		return(-1);
	}

	/* Write selected configuration to the PHY and verify it by reading back */
	/* Set Advertising Register for all 10/100 and Half/Full combinations */

	if (OUI == PHY_OUI_TDK)
		PHYConfig = PHYAccess(ioaddr, PHYAdd, 4, OPRead, 0);
	PHYConfig = PHYAccess(ioaddr, PHYAdd, 4, OPRead, 0);
	PHYConfig |= 0x01e0;
	PHYAccess(ioaddr, PHYAdd, 4, OPWrite, PHYConfig);
	if (OUI == PHY_OUI_TDK)
		PHYAccess(ioaddr, PHYAdd, 4, OPWrite, PHYConfig);

	/* Start 1 */

	/* National PHY requires clear before set 1 enable. */
	PHYAccess(ioaddr, PHYAdd, 0, OPWrite, 0x0000);
	PHYAccess(ioaddr, PHYAdd, 0, OPWrite, 0x1200);
	if (OUI == PHY_OUI_TDK)
		PHYAccess(ioaddr, PHYAdd, 0, OPWrite, 0x1200);

	/* Wait for completion */
	for (i = 0; i < NWAY_TIMEOUT * 10; i++) {
		printk("%c\b", "|/-\\"[i&3]);

		phy_delay(phy_delay3);

		PHYConfig = PHYAccess(ioaddr, PHYAdd, 1, OPRead, 0);
		PHYConfig2 = PHYAccess(ioaddr, PHYAdd, 1, OPRead, 0);

		if (PHYConfig != PHYConfig2) /* Value is not stable */
			continue;
		if (PHYConfig & 0x0010) /* Remote Fault */
			continue;
		if ((PHYConfig == 0x0000) || (PHYConfig == 0xffff)) /* invalid value */
			continue;
		if (PHYConfig & 0x0020)
			break;
	}

	/* Now read the results of the NWAY. */

	if (OUI == PHY_OUI_TDK)
		PHYConfig = PHYAccess(ioaddr, PHYAdd, 5, OPRead, 0);
	PHYConfig = PHYAccess(ioaddr, PHYAdd, 5, OPRead, 0);

	if (PHYConfig != 0) {
		/* Got real NWAY information here */
		report = "ANLPA";
		speed = (PHYConfig & 0x0180);
		duplex = (PHYConfig & 0x0140);
	} else {
		/*
		 * ANLPA was 0 so NWAY did not complete or is not reported fine.
		 * Get the info from propietary regs or from the control register.
		 */
		report = "Prop."; /* Proprietary Status */

		switch (OUI) {
		case PHY_OUI_NATIONAL:
			PHYConfig = PHYAccess(ioaddr, PHYAdd, PHY_NATIONAL_PAR, OPRead, 0);
			duplex = (PHYConfig & PHY_NATIONAL_PAR_DUPLEX);
			speed = ! (PHYConfig & PHY_NATIONAL_PAR_SPEED_10);
			break;

		case PHY_OUI_TDK:
			PHYConfig = PHYAccess(ioaddr, PHYAdd, PHY_TDK_DIAG, OPRead, 0);
			PHYConfig = PHYAccess(ioaddr, PHYAdd, PHY_TDK_DIAG, OPRead, 0);
			speed = ((Revision < 7) && ((PHYConfig & 0x300) == 0x300)) ||
					((Revision >= 7) && (PHYConfig & PHY_TDK_DIAG_RATE));
			duplex = ((Revision >= 7) && (PHYConfig & PHY_TDK_DIAG_DUPLEX));
			break;

		case PHY_OUI_QSI:
			PHYConfig = PHYAccess(ioaddr, PHYAdd, PHY_QSI_BASETX, OPRead, 0);
			PHYConfig &= PHY_QSI_BASETX_OPMODE_MASK;
			duplex = (PHYConfig & PHY_QSI_BASETX_OPMODE_10FD) ||
				(PHYConfig & PHY_QSI_BASETX_OPMODE_100FD);
			speed = (PHYConfig & PHY_QSI_BASETX_OPMODE_100HD) ||
				(PHYConfig & PHY_QSI_BASETX_OPMODE_100FD);
			break;

		case PHY_OUI_SEEQSMSC:
			PHYConfig=PHYAccess(ioaddr,PHYAdd,PHY_SEEQ_STATUS_OUTPUT,OPRead,0);
			duplex = (PHYConfig & PHY_SEEQ_DPLX_DET);
			speed = (PHYConfig & PHY_SEEQ_SPD_DET);
			break;

		default:
			report = "Command";
			PHYConfig = PHYAccess(ioaddr, PHYAdd, 0, OPRead, 0);
			speed = (PHYConfig & 0x2000);
			duplex = (PHYConfig & 0x0100);
			break;
		}
	}

	/* Do we need to adjust the Carrier sense on full duplex FEAST issue ?  */

	if (duplex && MAC_IS_FEAST() && (OUI == PHY_OUI_MITELSMSC))
		PHYAccess(ioaddr, PHYAdd, 0x18, OPWrite,
				0x0020 | PHYAccess(ioaddr, PHYAdd, 0x18, OPRead, 0));

	/* Display what we learned */

    printk(" %s-duplex %d Mbps ", duplex ? "Full" : "Half", speed ? 100 : 10);

	if (MAC_IS_FEAST())
		printk("FEAST ");
	if (MAC_IS_EPIC())
		printk("EPIC ");

	switch (OUI) {
	case PHY_OUI_QSI:       printk("QSI");                break;
	case PHY_OUI_TDK:       printk("TDK");                break;
	case PHY_OUI_MITELSMSC: printk("MITEL/SMSC180");      break;
	case PHY_OUI_NATIONAL:  printk("NATIONAL");           break;
	case PHY_OUI_SEEQSMSC:  printk("SEEQ/SMSC183");       break;
	default:                printk("%06lX(UNKNOWN)",OUI); break;
	}

	printk(" Model=%02X Rev=%02X ", Model, Revision);
#if DEBUG
	printk("Addr=%02X ", PHYAdd);
	printk("Conf=%s ", report);
#endif
	if (i == NWAY_TIMEOUT)
		printk("TIMEOUT!\n");
	else
		printk("Done.\n");
	return(0);
}

/*----------------------------------------------------------- */
#endif /* PHY_SETUP */
