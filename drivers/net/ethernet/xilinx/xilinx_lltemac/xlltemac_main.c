/*
 * Xilinx Ethernet: Linux driver for the XPS_LLTEMAC core.
 *
 * Author: Xilinx, Inc.
 *
 * 2006-2007 (c) Xilinx, Inc. This file is licensed uner the terms of the GNU
 * General Public License version 2.1. This program is licensed "as is" without
 * any warranty of any kind, whether express or implied.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a jvb  05/08/05 First release
 * </pre>
 *
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

#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_gpio.h>

#include <linux/kthread.h>

#include "xbasic_types.h"
#include "xlltemac.h"
#include "xllfifo.h"
#include "xlldma.h"
#include "xlldma_bdring.h"
#include "xlldma_hw.h"

#define LOCAL_FEATURE_RX_CSUM   0x01

/*
 * Default SEND and RECV buffer descriptors (BD) numbers.
 * BD Space needed is (XTE_SEND_BD_CNT+XTE_RECV_BD_CNT)*Sizeof(XLlDma_Bd).
 * Each XLlDma_Bd instance currently takes 40 bytes.
 */
#define XTE_SEND_BD_CNT 256
#define XTE_RECV_BD_CNT 256

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME         "xilinx_lltemac"
#define DRIVER_DESCRIPTION  "Xilinx Tri-Mode Ethernet MAC driver"
#define DRIVER_VERSION      "1.00a"

#define TX_TIMEOUT   (3*HZ)	/* Transmission timeout is 3 seconds. */


/*
 * This version of the Xilinx TEMAC uses external DMA or FIFO cores.
 * Currently neither the DMA or FIFO cores used require any memory alignment
 * restrictions.
 */
/*
 * ALIGNMENT_RECV = the alignement required to receive
 * ALIGNMENT_SEND = the alignement required to send
 * ALIGNMENT_SEND_PERF = tx alignment for better performance
 *
 * ALIGNMENT_SEND is used to see if we *need* to copy the data to re-align.
 * ALIGNMENT_SEND_PERF is used if we've decided we need to copy anyway, we just
 * copy to this alignment for better performance.
 */

#define ALIGNMENT_RECV          34
#define ALIGNMENT_SEND          8
#define ALIGNMENT_SEND_PERF     32

#define XTE_SEND  1
#define XTE_RECV  2

/* FiFo Alignment macros */
#define FIFO_ALIGNMENT       4
#define FIFO_BUFFER_ALIGN(adr) ((FIFO_ALIGNMENT - ((u32) adr)) % FIFO_ALIGNMENT)




/* SGDMA buffer descriptors must be aligned on a 8-byte boundary. */
#define ALIGNMENT_BD            XLLDMA_BD_MINIMUM_ALIGNMENT

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGNSEND(adr) ((ALIGNMENT_SEND - ((u32) adr)) % ALIGNMENT_SEND)
#define BUFFER_ALIGNSEND_PERF(adr) ((ALIGNMENT_SEND_PERF - ((u32) adr)) % 32)
#define BUFFER_ALIGNRECV(adr) ((ALIGNMENT_RECV - ((u32) adr)) % 32)
/* Default TX/RX Threshold and waitbound values for SGDMA mode */
#define DFT_TX_THRESHOLD  24
#define DFT_TX_WAITBOUND  254
#define DFT_RX_THRESHOLD  4
#define DFT_RX_WAITBOUND  254

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
 * Checksum offload macros
 */
#define BdCsumEnable(BdPtr) \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET,             \
		(XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)) | 1 )

/* Used for debugging */
#define BdCsumEnabled(BdPtr) \
	((XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)) & 1)

#define BdCsumDisable(BdPtr) \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET,             \
		(XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)) & 0xFFFFFFFE )

#define BdCsumSetup(BdPtr, Start, Insert) \
    XLlDma_mBdWrite((BdPtr), XLLDMA_BD_USR1_OFFSET, ((Start) << 16) | (Insert))

/* Used for debugging */
#define BdCsumInsert(BdPtr) \
    (XLlDma_mBdRead((BdPtr), XLLDMA_BD_USR1_OFFSET) & 0xffff)

#define BdCsumSeed(BdPtr, Seed) \
    XLlDma_mBdWrite((BdPtr), XLLDMA_BD_USR2_OFFSET, 0)

#define BdCsumGet(BdPtr) \
    (XLlDma_mBdRead((BdPtr), XLLDMA_BD_USR3_OFFSET) & 0xffff)

#define BdGetRxLen(BdPtr) \
    (XLlDma_mBdRead((BdPtr), XLLDMA_BD_USR4_OFFSET) & 0x3fff)

/* ZDS: modification for BRAM access */
extern int bram_kernel_access(void **bram_area);

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
#if 0
	XInterruptHandler Isr;	/* Pointer to the XLlTemac ISR routine */
#endif
	u8 gmii_addr;		/* The GMII address of the PHY */
	u32 virt_dma_addr;	/* Virtual address to mapped dma */

	/* The underlying OS independent code needs space as well.  A
	 * pointer to the following XLlTemac structure will be passed to
	 * any XLlTemac_ function that requires it.  However, we treat the
	 * data as an opaque object in this file (meaning that we never
	 * reference any of the fields inside of the structure). */
	XLlFifo Fifo;
	XLlDma Dma;
	XLlTemac Emac;

	unsigned int fifo_irq;	/* fifo irq */
	unsigned int dma_irq_s;	/* send irq */
	unsigned int dma_irq_r;	/* recv irq */
	unsigned int frame_size; /* actual frame size = mtu + padding */

	int cur_speed;
	int cur_autoneg;
	int cur_state;
	int cur_pause;

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

u32 dma_rx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK;
u32 dma_tx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK;

/* for exclusion of all program flows (processes, ISRs and BHs) */
spinlock_t XTE_spinlock = __SPIN_LOCK_UNLOCKED(old_style_spin_init);
spinlock_t XTE_tx_spinlock = __SPIN_LOCK_UNLOCKED(old_style_spin_init);
spinlock_t XTE_rx_spinlock = __SPIN_LOCK_UNLOCKED(old_style_spin_init);

/*
 * ethtool has a status reporting feature where we can report any sort of
 * status information we'd like. This is the list of strings used for that
 * status reporting. ETH_GSTRING_LEN is defined in ethtool.h
 */
static char xenet_ethtool_gstrings_stats[][ETH_GSTRING_LEN] = {
	"txpkts", "txdropped", "txerr", "txfifoerr",
	"rxpkts", "rxdropped", "rxerr", "rxfifoerr",
	"rxrejerr", "max_frags", "tx_hw_csums", "rx_hw_csums",
};

#define XENET_STATS_LEN sizeof(xenet_ethtool_gstrings_stats) / ETH_GSTRING_LEN

/* Helper function to determine if a given XLlTemac error warrants a reset. */
extern inline int status_requires_reset(int s)
{
	return (s == XST_FIFO_ERROR ||
		s == XST_PFIFO_DEADLOCK ||
		s == XST_DMA_ERROR || s == XST_IPIF_ERROR);
}

/* Queues with locks */
static LIST_HEAD(receivedQueue);
static spinlock_t receivedQueueSpin = __SPIN_LOCK_UNLOCKED(old_style_spin_init);

static LIST_HEAD(sentQueue);
static spinlock_t sentQueueSpin = __SPIN_LOCK_UNLOCKED(old_style_spin_init);


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

/*
 * ZDS-specific (extracted from epl_regs.h.
 * Not even documented in xps_ll_temac official xilinx doc from 2010)
 */
#define MII_PAGESEL 0x13

/*
 * Wrap certain temac routines with a lock, so access to the shared hard temac
 * interface is accessed mutually exclusive for dual channel temac support.
 */

static inline void _XLlTemac_Start(XLlTemac *InstancePtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Start(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}


static inline int _XLlTemac_ReadReg(int BaseAddress, int Offset)
{
   int result;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	result  = XLlTemac_ReadReg(BaseAddress, Offset);
   spin_unlock_irqrestore(&XTE_spinlock, flags);

   return result;
}

static inline void _XLlTemac_Stop(XLlTemac *InstancePtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Stop(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_Reset(XLlTemac *InstancePtr, int HardCoreAction)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Reset(InstancePtr, HardCoreAction);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_SetMacAddress(XLlTemac *InstancePtr,
					  void *AddressPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetMacAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline void _XLlTemac_GetMacAddress(XLlTemac *InstancePtr,
					   void *AddressPtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_GetMacAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_SetOptions(XLlTemac *InstancePtr, u32 Options)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetOptions(InstancePtr, Options);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_ClearOptions(XLlTemac *InstancePtr, u32 Options)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_ClearOptions(InstancePtr, Options);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline u16 _XLlTemac_GetOperatingSpeed(XLlTemac *InstancePtr)
{
	u16 speed;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	speed = XLlTemac_GetOperatingSpeed(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return speed;
}

static inline void _XLlTemac_SetOperatingSpeed(XLlTemac *InstancePtr, u16 Speed)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_SetOperatingSpeed(InstancePtr, Speed);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	// Temac can change the speed only during InterFrameGap.
   // Worst frame duration : 1500 * 8 * 0.1 us (10Mbit/s) =  1200 us
   udelay(3000); 
}

static inline void _XLlTemac_PhySetMdioDivisor(XLlTemac *InstancePtr, u8 Divisor)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_PhySetMdioDivisor(InstancePtr, Divisor);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

/* ZDS-specific register-access code by JNA */

static int last_page = 0;

static inline void _XLlTemac_PhyRead(XLlTemac *InstancePtr, u32 PhyAddress,
				     u32 RegisterNum, u16 *PhyDataPtr){
  
  unsigned long flags;
  
  u16 page = 0x7 & (RegisterNum >> 5);
  u32 reg = 0x1f & RegisterNum;
  
  spin_lock_irqsave(&XTE_spinlock, flags);
  if(page != last_page){
    XLlTemac_PhyWrite(InstancePtr, PhyAddress, MII_PAGESEL, page);
    last_page = page;
  }
  
  XLlTemac_PhyRead(InstancePtr, PhyAddress, reg, PhyDataPtr);
  spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_PhyWrite(XLlTemac *InstancePtr, u32 PhyAddress,
				      u32 RegisterNum, u16 PhyData) {
  
  unsigned long flags;

  u16 page = 0x7 & (RegisterNum >> 5);
  u32 reg = 0x1f & RegisterNum;
  
  spin_lock_irqsave(&XTE_spinlock, flags);
  if(page != last_page){
    XLlTemac_PhyWrite(InstancePtr, PhyAddress, MII_PAGESEL, page);
    last_page = page;
  }
  
  XLlTemac_PhyWrite(InstancePtr, PhyAddress, reg, PhyData);
  spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_MulticastClear(XLlTemac *InstancePtr, int Entry)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_MulticastClear(InstancePtr, Entry);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_SetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetMacPauseAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline void _XLlTemac_GetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_GetMacPauseAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_GetSgmiiStatus(XLlTemac *InstancePtr, u16 *SpeedPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_GetSgmiiStatus(InstancePtr, SpeedPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_GetRgmiiStatus(XLlTemac *InstancePtr,
					   u16 *SpeedPtr,
					   int *IsFullDuplexPtr,
					   int *IsLinkUpPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_GetRgmiiStatus(InstancePtr, SpeedPtr, IsFullDuplexPtr, IsLinkUpPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}


#ifdef CONFIG_XILINX_LLTEMAC_MARVELL_88E1111_RGMII
#define MARVELL_88E1111_EXTENDED_PHY_CTL_REG_OFFSET  20
#define MARVELL_88E1111_EXTENDED_PHY_STATUS_REG_OFFSET  27
#endif

#define DEBUG_ERROR KERN_ERR
#define DEBUG_LOG(level, ...) printk(level __VA_ARGS__)

#define NATIONAL_DP83865_CONTROL_INIT       0x9200
#define NATIONAL_DP83865_CONTROL            0
#define NATIONAL_DP83865_STATUS             1
#define NATIONAL_DP83865_STATUS_LINK        0x04
#define NATIONAL_DP83865_STATUS_AUTONEGEND  0x20
#define NATIONAL_DP83865_STATUS_AUTONEG     0x11
#define NATIONAL_DP83865_LINKSPEED_1000M    0x10
#define NATIONAL_DP83865_LINKSPEED_100M     0x8
#define NATIONAL_DP83865_LINKSPEED_MASK     0x18
#define NATIONAL_DP83865_RETRIES            5

/* 160 base du registre GPIO de control, 9 bit du gpio */
#define AUTONEG_COMPLETE_GPIO               (160+9)

void zds_autoneg_complete(int flag)
{
#ifdef CONFIG_XILINX_LLTEMAC_AUTO_NEG_GPIO
    static int initialized = 0;

    if(!initialized)
    {
        //initialize autoneg complete dedicated GPIO
        if(gpio_request(AUTONEG_COMPLETE_GPIO, "autoneg_complete"))
		{
			printk(KERN_ERR "Cannot allocate gpio %d\n",AUTONEG_COMPLETE_GPIO);
            return;
		}
        switch(flag)
        {
            case 0:
                //write 0 to GPIO
                gpio_direction_output(AUTONEG_COMPLETE_GPIO, 0);
                break;

            case 1:
                //write 1 to GPIO
                gpio_direction_output(AUTONEG_COMPLETE_GPIO, 1);
                break;

            default:
                gpio_direction_output(AUTONEG_COMPLETE_GPIO, 0);
                break;
        }
        initialized = 1;
		gpio_export(AUTONEG_COMPLETE_GPIO,0);
    }
    else
    {
        switch(flag)
        {
            case 0:
                //write 0 to GPIO
                gpio_set_value(AUTONEG_COMPLETE_GPIO, 0);
                break;

            case 1:
                //write 1 to GPIO
                gpio_set_value(AUTONEG_COMPLETE_GPIO, 1);
                break;

            default:
                // we don't manage values other than 0 and 1
                break;
        }
    }
#endif
}

int zds_autoneg_check(struct net_local *lp)
{
    int result = 0;
#ifdef CONFIG_XILINX_LLTEMAC_AUTO_NEG_GPIO
    u16 phy_reg1;

    if(lp->cur_autoneg)
    {
        _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR , &phy_reg1 );
        if ((phy_reg1 & BMSR_LSTATUS) && (phy_reg1 & BMSR_ANEGCOMPLETE))
        {
            result = 1;
        }
    }

#endif
    return result;
}


typedef enum DUPLEX { UNKNOWN_DUPLEX, HALF_DUPLEX, FULL_DUPLEX } DUPLEX;

int set_phy_speed(struct net_device *dev, int speed, DUPLEX duplex, int autoneg) // Use TEMAC set operating speed after !
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	int retries = 2;
	int wait_count ;
	/* int poll_reset = 20; */
	u16 phy_reg0; 
	/* u16 phy_reg0_poll; */
	u16 phy_reg1;
	u16 phy_reg4;
	u16 phy_reg9 = 0;
	u16 phy_reg16;

   printk(KERN_INFO "%s cur_speed   : %d\n", __func__, lp->cur_speed);
   printk(KERN_INFO "%s cur_autoneg : %d\n", __func__, lp->cur_autoneg);
   printk(KERN_INFO "%s cur_pause   : %d\n", __func__, lp->cur_pause);
     
   // 1. Speed activation
   if (autoneg)
      phy_reg0 = BMCR_ANENABLE | BMCR_ANRESTART;
   else
   {
      phy_reg0 = 0;
      if (speed == 100 )         phy_reg0 |= BMCR_SPEED100;                             
      /*if (duplex == FULL_DUPLEX) */ phy_reg0 |= BMCR_FULLDPLX;  //Temac is FULL duplex only
   }
   lp->cur_autoneg = autoneg;


   // 2. Advertise speed   
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
		       "%s: XLlTemac: unsupported speed requested: %d\n",
		       dev->name, speed);
		return -1;
	}

   // Apply pause (flow control) settings
   if (lp->cur_pause)
   {
//      printk(KERN_WARNING "pause to_set : on\n");
      phy_reg4 |= ADVERTISE_PAUSE_CAP;  
      phy_reg4 |= ADVERTISE_PAUSE_ASYM;
   }
   else
   {
//      printk(KERN_WARNING "pause to_set : off\n");
      phy_reg4 &= ~ADVERTISE_PAUSE_CAP;  
      phy_reg4 &= ~ADVERTISE_PAUSE_ASYM;
   }
   
	_XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_ADVERTISE  ,  phy_reg4);
	_XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_EXADVERTISE,  phy_reg9);


   // 3. RESET + set speed 
   // remove reset for autonegociation
   _XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_BMCR,  phy_reg0); 
    mdelay(100);
/*   _XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_BMCR,  phy_reg0 | BMCR_RESET); 
   while (poll_reset--)
   {
      mdelay(100);
      _XLlTemac_PhyRead( &lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg0_poll);
      if (phy_reg0_poll & BMCR_RESET)
         break;
   }
   if(poll_reset==0)
      printk(KERN_ERR "%s: XLlTemac PhySetup: RESET failed\n", dev->name);               
  */
    
   lp->cur_state = 0;

   zds_autoneg_complete(0);

   // 4. Test
   while (retries--)
   {
      //setting speed
      _XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_BMCR,  phy_reg0);
      
      wait_count = 5;
      while (wait_count--)
      {
         mdelay(100);
         //check status 
         _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg1);

         if (autoneg)
         {
            if ((phy_reg1 & BMSR_LSTATUS) && (phy_reg1 & BMSR_ANEGCOMPLETE))
            {
               _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, 0x10, &phy_reg16);
               if (phy_reg16 & 0x0002)
               {
                  printk(KERN_INFO "%s: XLlTemac PhySetup: Autoneged to: 10 FULL\n", dev->name);
                  lp->cur_speed = 10;
                  lp->cur_state = 1;
                  zds_autoneg_complete(1);
                  return 0;
               }
               else
               {
                  printk(KERN_INFO "%s: XLlTemac PhySetup: Autoneged to: 100  FULL\n", dev->name);               
                  lp->cur_speed = 100;
                  lp->cur_state = 1;
                  zds_autoneg_complete(1);
                  return 0;
               }
            }
            //more delay for autonegotiation
            mdelay(900);
            //printk(KERN_INFO "%s: XLlTemac PhySetup: waiting autoneg (%d, 0x%04x)\n", dev->name, wait_count, phy_reg0);               

         }
         else //no autoneg
         {
            if (phy_reg1 & BMSR_LSTATUS)
            {
               printk(KERN_INFO "%s: XLlTemac PhySetup: Speed set : %d\n", dev->name, speed );
               lp->cur_state = 1;
               return 0;
            }
            //printk(KERN_INFO "%s: XLlTemac PhySetup: waiting (%d, 0x%04x)\n", dev->name, wait_count, phy_reg0);               
         }
      }

      printk(KERN_INFO "%s: XLlTemac PhySetup: retrying (%d, 0x%04x)\n", dev->name, retries, phy_reg0);               

   }
      
   if (autoneg)   
      printk(KERN_ERR "%s: XLlTemac PhySetup: Autoneg failed (status: 0x%0x)\n", dev->name, phy_reg1);
   else
      printk(KERN_ERR "%s: XLlTemac PhySetup: Not able to set the speed to %d (status: 0x%0x)\n", dev->name, speed, phy_reg1);

      
   return -1;
}



/*
 * Helper function to reset the underlying hardware.  This is called
 * when we get into such deep trouble that we don't know how to handle
 * otherwise.
 */
static void reset(struct net_device *dev, u32 line_num)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	u32 TxThreshold, TxWaitBound, RxThreshold, RxWaitBound;
	u32 Options;
	static u32 reset_cnt = 0;
	int status;
	int ready;
	int poll_reset=20;

	printk(KERN_INFO "%s: XLlTemac: resets (#%u) from adapter code line %d\n",
	       dev->name, ++reset_cnt, line_num);

	/* Shouldn't really be necessary, but shouldn't hurt. */
	netif_stop_queue(dev);

	/* Stop device */
	_XLlTemac_Stop(&lp->Emac);

	/*
	 * XLlTemac_Reset puts the device back to the default state.  We need
	 * to save all the settings we don't already know, reset, restore
	 * the settings, and then restart the TEMAC.
	 */
	Options = XLlTemac_GetOptions(&lp->Emac);

	/*
	 * Capture the dma coalesce settings (if needed) and reset the
	 * connected core, dma or fifo
	 */
	if (XLlTemac_IsDma(&lp->Emac)) {
		XLlDma_BdRingGetCoalesce(&XLlDma_mGetRxRing(&lp->Dma),
					 &RxThreshold, &RxWaitBound);
		XLlDma_BdRingGetCoalesce(&XLlDma_mGetTxRing(&lp->Dma),
					 &TxThreshold, &TxWaitBound);

		XLlDma_Reset(&lp->Dma);
	} else {
		XLlFifo_Reset(&lp->Fifo);
	}

	/* now we can reset the device */
	_XLlTemac_Reset(&lp->Emac, XTE_NORESET_HARD);

	/* Reset on TEMAC also resets PHY. Give it some time to finish negotiation
	 * before we move on */
	//mdelay(2000);
   while (poll_reset--)
   {
      mdelay(100);
      ready = _XLlTemac_ReadReg(lp->Emac.Config.BaseAddress,XTE_RDY_OFFSET);
      if (ready & XTE_RDY_HARD_ACS_RDY_MASK)
         break;
   }
   if (poll_reset==0)
      printk(KERN_ERR "%s: XLlTemac TEMAC RESET failed\n", dev->name);              
//   else
//      printk(KERN_WARNING "%s: XLlTemac TEMAC RESET ok\n", dev->name);              
      
	
	/*
	 * The following four functions will return an error if the
	 * EMAC is already started.  We just stopped it by calling
	 * _XLlTemac_Reset() so we can safely ignore the return values.
	 */
	(int) _XLlTemac_SetMacAddress(&lp->Emac, dev->dev_addr);
	(int) _XLlTemac_SetOptions(&lp->Emac, Options);
	(int) _XLlTemac_ClearOptions(&lp->Emac, ~Options);
	Options = XLlTemac_GetOptions(&lp->Emac);
	printk(KERN_INFO "%s: XLlTemac: Options: 0x%x\n", dev->name, Options);

   set_phy_speed(lp->ndev, lp->cur_speed, FULL_DUPLEX, lp->cur_autoneg); 
   _XLlTemac_SetOperatingSpeed(&lp->Emac, lp->cur_speed);


	if (XLlTemac_IsDma(&lp->Emac)) {	/* SG DMA mode */
		status = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
						  RxThreshold, RxWaitBound);
		status |= XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
						   TxThreshold, TxWaitBound);
		if (status != XST_SUCCESS) {
			/* Print the error, but keep on going as it's not a fatal error. */
			printk(KERN_ERR "%s: XLlTemac: error setting coalesce values (probably out of range). status: %d\n",
			       dev->name, status);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);
	} else {			/* FIFO interrupt mode */
		XLlFifo_IntEnable(&lp->Fifo, XLLF_INT_TC_MASK |
				XLLF_INT_RC_MASK | XLLF_INT_RXERROR_MASK |
				XLLF_INT_TXERROR_MASK);
	}
	XLlTemac_IntDisable(&lp->Emac, XTE_INT_ALL_MASK);

	if (lp->deferred_skb) {
		dev_kfree_skb_any(lp->deferred_skb);
		lp->deferred_skb = NULL;
		lp->stats.tx_errors++;
	}

	/*
	 * XLlTemac_Start returns an error when: if configured for
	 * scatter-gather DMA and a descriptor list has not yet been created
	 * for the send or receive channel, or if no receive buffer descriptors
	 * have been initialized. Those are not happening. so ignore the returned
	 * result checking.
	 */
	_XLlTemac_Start(&lp->Emac);

	/* We're all ready to go.  Start the queue in case it was stopped. */
	netif_wake_queue(dev);
}

/*
 * This routine is used for two purposes.  The first is to keep the
 * EMAC's duplex setting in sync with the PHY's.  The second is to keep
 * the system apprised of the state of the link.  Note that this driver
 * does not configure the PHY.  Either the PHY should be configured for
 * auto-negotiation or it should be handled by something like mii-tool. */
// Not true anymore

struct task_struct *poll_gmii_thread;

#ifdef USE_TIMER
static void poll_gmii(unsigned long data)
#else
static int poll_gmii(void *data)
#endif

{
	struct net_device *dev;
	struct net_local *lp;
   u16 phy_reg1;
   int new_state;
   
	dev = (struct net_device *) data;
	lp = (struct net_local *) netdev_priv(dev);

#ifdef USE_TIMER
	//very first : disable timer
   del_timer_sync(&lp->phy_timer);
#else
    while(!kthread_should_stop())
    {
#endif

   _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR , &phy_reg1 );

   //then check link
   if (phy_reg1 & BMSR_LSTATUS)
      new_state = 1; //up
   else
      new_state = 0; //down

   if (new_state != lp->cur_state)
   {
      if (new_state)  
      {
			printk(KERN_INFO "%s: XLlTemac: PHY Link carrier restored.\n", dev->name);
         set_phy_speed(lp->ndev, lp->cur_speed, FULL_DUPLEX, lp->cur_autoneg); 
         _XLlTemac_SetOperatingSpeed(&lp->Emac, lp->cur_speed); 
         netif_carrier_on(dev);
         zds_autoneg_complete(zds_autoneg_check(lp));

      }
      else
      {
			printk(KERN_INFO "%s: XLlTemac: PHY Link carrier down.\n", dev->name);
         netif_carrier_off(dev);
         zds_autoneg_complete(0);
      }

      lp->cur_state = new_state;
   }


#ifdef USE_TIMER
   /* Set up the timer so we'll get called again in 2 seconds. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	add_timer(&lp->phy_timer);
#else
    set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(2 * HZ);
    }
    return 0;
#endif

}

static irqreturn_t xenet_temac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
   unsigned isr=0;

	/*
	 * All we care about here is the RxRject interrupts. Explanation below:
	 *
	 * Interrupt     Usage Description
	 * ---------     -----------------
	 * TxCmplt:      Fifo or DMA will have completion interrupts. We'll use
	 *               those and not the TEMAC ones.
	 * RxFifoOvr:    if the RX fifo is overflowing, the last thing we need
	 *               is more interrupts to handle.
	 * RxRJect:      We're keeping stats on rejected packets (we could
	 *               choose not to).
	 * RxCmplt:      Fifo or DMA will have completion interrupts. We'll use
	 *               those and not the TEMAC ones.
	 * AutoNeg:      This driver doesn't make use of the autonegotation
	 *               completion interrupt.
	 * HardAcsCmplt: This driver just polls the RDY register for this
	 *               information instead of using an interrupt handler.
	 * CfgWst, CfgRst,
	 * AfWst, AfRst,
	 * MiimWst, MiimRst,
	 * FabrRst:      All of these registers indicate when access (read or
	 *               write) to one or other of the Hard Temac Core
	 *               registers is complete. Instead of relying on an
	 *               interrupt context switch to be notified that the
	 *               access is complete, this driver instead polls for the
	 *               status, which, in most cases, should be faster.
	 */
   
   
   isr = XLlTemac_Status(&lp->Emac); 
   printk(KERN_WARNING "IRQ: %08X\n",isr );
	
   
   XLlTemac_IntClear(&lp->Emac, XTE_INT_ALL_MASK);

	lp->stats.rx_errors++;
	lp->stats.rx_crc_errors++;



	return IRQ_HANDLED;
}

static void FifoSendHandler(struct net_device *dev);
static void FifoRecvHandler(unsigned long p /*struct net_device *dev*/);

DECLARE_TASKLET(FifoRecvBH, FifoRecvHandler, 0);

static irqreturn_t xenet_fifo_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	u32 irq_status;

	unsigned long flags;

	/*
	 * Need to:
	 * 1) Read the FIFO IS register
	 * 2) clear all bits in the FIFO IS register
	 * 3) loop on each bit in the IS register, and handle each interrupt event
	 *
	 */
	irq_status = XLlFifo_IntPending(&lp->Fifo);
	XLlFifo_IntClear(&lp->Fifo, irq_status);
	while (irq_status) {
		if (irq_status & XLLF_INT_RC_MASK) {
			/* handle the receive completion */
			struct list_head *cur_lp;
			spin_lock_irqsave(&receivedQueueSpin, flags);
			list_for_each(cur_lp, &receivedQueue) {
				if (cur_lp == &(lp->rcv)) {
					break;
				}
			}
			if (cur_lp != &(lp->rcv)) {
				list_add_tail(&lp->rcv, &receivedQueue);
				XLlFifo_IntDisable(&lp->Fifo, XLLF_INT_ALL_MASK);
				tasklet_schedule(&FifoRecvBH);
			}
			spin_unlock_irqrestore(&receivedQueueSpin, flags);
			irq_status &= ~XLLF_INT_RC_MASK;
		} else if (irq_status & XLLF_INT_TC_MASK) {
			/* handle the transmit completion */
			FifoSendHandler(dev);
			irq_status &= ~XLLF_INT_TC_MASK;
		} else if (irq_status & XLLF_INT_TXERROR_MASK) {
			lp->stats.tx_errors++;
			lp->stats.tx_fifo_errors++;
			XLlFifo_Reset(&lp->Fifo);
			irq_status &= ~XLLF_INT_TXERROR_MASK;
		} else if (irq_status & XLLF_INT_RXERROR_MASK) {
			lp->stats.rx_errors++;
			XLlFifo_Reset(&lp->Fifo);
			irq_status &= ~XLLF_INT_RXERROR_MASK;
		} else {
			/* debug
			 * if (irq_status == 0) printk("Temac: spurious fifo int\n");
			 */
		}
	}

	return IRQ_HANDLED;
}

/* The callback function for completed frames sent in SGDMA mode. */
static void DmaSendHandlerBH(unsigned long p);
static void DmaRecvHandlerBH(unsigned long p);

DECLARE_TASKLET(DmaSendBH, DmaSendHandlerBH, 0);
DECLARE_TASKLET(DmaRecvBH, DmaRecvHandlerBH, 0);

static irqreturn_t xenet_dma_rx_interrupt(int irq, void *dev_id)
{
	u32 irq_status;
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	struct list_head *cur_lp;

        unsigned long flags;

	/* Read pending interrupts */
	irq_status = XLlDma_mBdRingGetIrq(&lp->Dma.RxBdRing);

	XLlDma_mBdRingAckIrq(&lp->Dma.RxBdRing, irq_status);

	if ((irq_status & XLLDMA_IRQ_ALL_ERR_MASK)) {
		XLlDma_Reset(&lp->Dma);
		return IRQ_HANDLED;
	}
	if ((irq_status & (XLLDMA_IRQ_DELAY_MASK | XLLDMA_IRQ_COALESCE_MASK))) {
		spin_lock_irqsave(&receivedQueueSpin, flags);
		list_for_each(cur_lp, &receivedQueue) {
			if (cur_lp == &(lp->rcv)) {
				break;
			}
		}
		if (cur_lp != &(lp->rcv)) {
			list_add_tail(&lp->rcv, &receivedQueue);
			XLlDma_mBdRingIntDisable(&lp->Dma.RxBdRing,
						 XLLDMA_CR_IRQ_ALL_EN_MASK);
			tasklet_schedule(&DmaRecvBH);
		}
		spin_unlock_irqrestore(&receivedQueueSpin, flags);
	}
	return IRQ_HANDLED;
}

static irqreturn_t xenet_dma_tx_interrupt(int irq, void *dev_id)
{
	u32 irq_status;
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	struct list_head *cur_lp;

	unsigned long flags;

	/* Read pending interrupts */
	irq_status = XLlDma_mBdRingGetIrq(&(lp->Dma.TxBdRing));

	XLlDma_mBdRingAckIrq(&(lp->Dma.TxBdRing), irq_status);

	if ((irq_status & XLLDMA_IRQ_ALL_ERR_MASK)) {
		XLlDma_Reset(&lp->Dma);
		return IRQ_HANDLED;
	}

	if ((irq_status & (XLLDMA_IRQ_DELAY_MASK | XLLDMA_IRQ_COALESCE_MASK))) {
		spin_lock_irqsave(&sentQueueSpin, flags);
		list_for_each(cur_lp, &sentQueue) {
			if (cur_lp == &(lp->xmit)) {
 				break;
			}
		}
		if (cur_lp != &(lp->xmit)) {
			list_add_tail(&lp->xmit, &sentQueue);
			XLlDma_mBdRingIntDisable(&lp->Dma.TxBdRing,
						 XLLDMA_CR_IRQ_ALL_EN_MASK);
			tasklet_schedule(&DmaSendBH);
		}
		spin_unlock_irqrestore(&sentQueueSpin, flags);
	}
	return IRQ_HANDLED;
}

/*
 * Q:
 * Why doesn't this linux driver use an interrupt handler for the TEMAC itself?
 *
 * A:
 * Let's take a look at all the possible events that could be signaled by the
 * TEMAC core.
 *
 * possible events:
 *    Transmit Complete (TxCmplt) [not handled by this driver]
 *        The TEMAC TxCmplt interrupt status is ignored by software in favor of
 *        paying attention to the transmit complete status in the connected DMA
 *        or FIFO core.
 *    Receive Fifo Overflow (RxFifoOver) [not handled by this driver]
 *        We have discovered that the overhead of an interrupt context switch
 *        to attempt to handle this sort of event actually worsens the
 *        condition, and cuases further dropped packets further increasing the
 *        time spent in this interrupt handler.
 *    Receive Frame Rejected (RxRject) [not handled by this driver]
 *        We could possibly handle this interrupt and gather statistics
 *        information based on these events that occur. However it is not that
 *        critical.
 *    Receive Complete (RxCmplt) [not handled by this driver]
 *        The TEMAC RxCmplt interrupt status is ignored by software in favor of
 *        paying attention to the receive complete status in the connected DMA
 *        or FIFO core.
 *    Autonegotiaion Complete (AutoNeg) [not handled by this driver]
 *        Autonegotiation on the TEMAC is a bit complicated, and is handled in
 *        a way that does not require the use of this interrupt event.
 *    Hard Temac Core Access Complete (HardAcsCmplt) [not handled by this driver]
 *        This event really just indicates if there are any events in the TIS
 *        register. As can be seen below, none of the events from the TIS
 *        register are handled, so there is no need to handle this event
 *        either.
 *    Configuration Write Complete (CfgWst) [not handled by this driver]
 *    Configuration Read Complete (CfgRst) [not handled by this driver]
 *    Address Filter Write Complete (AfWst) [not handled by this driver]
 *    Address Filter Read Complete (AfRst) [not handled by this driver]
 *    MII Management Write Complete (MiimWst) [not handled by this driver]
 *    MII Management Read Complete (MiimRst) [not handled by this driver]
 *    Fabric Read Complete (FabrRst) [not handled by this driver]
 *        All of the above registers indicate when access (read or write) to
 *        one or other of the Hard Temac Core registers is complete. Instead of
 *        relying on an interrupt context switch to be notified that the access
 *        is complete, this driver instead polls for the status, which, in most
 *        cases, should be faster.
 */

static int xenet_open(struct net_device *dev)
{
	struct net_local *lp;
	u32 Options;
	u16 phy_reg1;
	int irqval = 0;

	/*
	 * Just to be safe, stop TX queue and the device first.  If the device is
	 * already stopped, an error will be returned.  In this case, we don't
	 * really care.
	 */
	netif_stop_queue(dev);
	lp = (struct net_local *) netdev_priv(dev);
	_XLlTemac_Stop(&lp->Emac);

	INIT_LIST_HEAD(&(lp->rcv));
	INIT_LIST_HEAD(&(lp->xmit));

	/* Set the MAC address each time opened. */
	if (_XLlTemac_SetMacAddress(&lp->Emac, dev->dev_addr) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XLlTemac: could not set MAC address.\n",
		       dev->name);
		return -EIO;
	}

	/*
	 * If the device is not configured for polled mode, connect to the
	 * interrupt controller and enable interrupts.  Currently, there
	 * isn't any code to set polled mode, so this check is probably
	 * superfluous.
	 */
	Options = XLlTemac_GetOptions(&lp->Emac);
	Options |= XTE_FLOW_CONTROL_OPTION;
	/* Enabling jumbo packets shouldn't be a problem if MTU is smaller */
	Options |= XTE_JUMBO_OPTION;
	Options |= XTE_TRANSMITTER_ENABLE_OPTION;
	Options |= XTE_RECEIVER_ENABLE_OPTION;
#if XTE_AUTOSTRIPPING
	Options |= XTE_FCS_STRIP_OPTION;
#endif
	(int) _XLlTemac_SetOptions(&lp->Emac, Options);
	(int) _XLlTemac_ClearOptions(&lp->Emac, ~Options);
	Options = XLlTemac_GetOptions(&lp->Emac);
	printk(KERN_INFO "%s: XLlTemac: Options: 0x%x\n", dev->name, Options);

	/* Just use interrupt driven methods - no polled mode */

	irqval = request_irq(dev->irq, &xenet_temac_interrupt, IRQF_DISABLED, dev->name, dev);
	if (irqval) {
		printk(KERN_ERR
		       "%s: XLlTemac: could not allocate interrupt %d.\n",
		       dev->name, dev->irq);
		return irqval;
	}
	if (XLlTemac_IsDma(&lp->Emac)) {
		printk(KERN_INFO
		       "%s: XLlTemac: allocating interrupt %d for dma mode tx.\n",
		       dev->name, lp->dma_irq_s);
		irqval = request_irq(lp->dma_irq_s,
			&xenet_dma_tx_interrupt, 0, "xilinx_dma_tx_int", dev);
		if (irqval) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not allocate interrupt %d.\n",
			       dev->name, lp->dma_irq_s);
			return irqval;
		}
		printk(KERN_INFO
		       "%s: XLlTemac: allocating interrupt %d for dma mode rx.\n",
		       dev->name, lp->dma_irq_r);
		irqval = request_irq(lp->dma_irq_r,
			&xenet_dma_rx_interrupt, 0, "xilinx_dma_rx_int", dev);
		if (irqval) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not allocate interrupt %d.\n",
			       dev->name, lp->dma_irq_r);
			return irqval;
		}
	} else {
		printk(KERN_INFO
		       "%s: XLlTemac: allocating interrupt %d for fifo mode.\n",
		       dev->name, lp->fifo_irq);
		/* With the way interrupts are issued on the fifo core, this needs to be
		 * fast interrupt handler.
		 */
		irqval = request_irq(lp->fifo_irq,
			&xenet_fifo_interrupt, IRQF_DISABLED, "xilinx_fifo_int", dev);
		if (irqval) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not allocate interrupt %d.\n",
			       dev->name, lp->fifo_irq);
			return irqval;
		}
	}

	/* We're ready to go. */
	netif_start_queue(dev);

#if 0
	/* give the system enough time to establish a link */
   //mdelay(2000);
   while (poll_reset--)
   {
      mdelay(100);
      ready = _XLlTemac_ReadReg(lp->Emac.Config.BaseAddress,XTE_RDY_OFFSET);
      if (ready & XTE_RDY_HARD_ACS_RDY_MASK)
         break;
   }
   if (poll_reset==0)
      printk(KERN_ERR "%s: XLlTemac TEMAC RESET failed\n", dev->name);               
//   else
//      printk(KERN_WARNING "%s: XLlTemac TEMAC RESET ok\n", dev->name);              
	
#endif   
   
   //first open:
   if (lp->cur_speed < 0)
      lp->cur_speed   = _XLlTemac_GetOperatingSpeed(&lp->Emac);                        //set by vhdl before linux starts 
   if (lp->cur_autoneg < 0)
   {
      u16 bmcr=0;      
	   _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &bmcr);                    //set by vhdl before linux starts 
      lp->cur_autoneg = (bmcr & BMCR_ANENABLE)?1:0;
   }
   if (lp->cur_pause < 0)
      lp->cur_pause = (XLlTemac_GetOptions(&lp->Emac) & XTE_FLOW_CONTROL_OPTION)?1:0; //set by vhdl before linux starts
   _XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR , &phy_reg1 );

   //then check link
   if (phy_reg1 & BMSR_LSTATUS)
   {
      lp->cur_state = 1; //up
      zds_autoneg_complete(zds_autoneg_check(lp));
   }
   else
   {
      lp->cur_state = 0; //down
      zds_autoneg_complete(0);
   }

   printk(KERN_INFO "%s Phy configuration\n", __func__);
   printk(KERN_INFO "%s cur_speed   : %d\n", __func__, lp->cur_speed);
   printk(KERN_INFO "%s cur_autoneg : %d\n", __func__, lp->cur_autoneg);
   printk(KERN_INFO "%s cur_pause   : %d\n", __func__, lp->cur_pause);

   //set_phy_speed(lp->ndev, lp->cur_speed, FULL_DUPLEX, lp->cur_autoneg);           //done by vhdl before linux starts  
   //_XLlTemac_SetOperatingSpeed(&lp->Emac, lp->cur_speed);

	/* Enable interrupts  - no polled mode */
	if (XLlTemac_IsFifo(&lp->Emac)) { /* fifo direct interrupt driver mode */
		XLlFifo_IntEnable(&lp->Fifo, XLLF_INT_TC_MASK |
			XLLF_INT_RC_MASK | XLLF_INT_RXERROR_MASK |
			XLLF_INT_TXERROR_MASK);
	} else {		/* SG DMA mode */
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);
	}
	/*
	 * Make sure all temac interrupts are disabled. These
	 * interrupts are not data flow releated.
	 */
	XLlTemac_IntDisable(&lp->Emac, XTE_INT_ALL_MASK);

	/* Start TEMAC device */
	_XLlTemac_Start(&lp->Emac);
	if (XLlTemac_IsDma(&lp->Emac)) {
		u32 threshold_s, timer_s, threshold_r, timer_r;

		XLlDma_BdRingGetCoalesce(&lp->Dma.TxBdRing, &threshold_s, &timer_s);
		XLlDma_BdRingGetCoalesce(&lp->Dma.RxBdRing, &threshold_r, &timer_r);
		printk(KERN_INFO
		       "%s: XLlTemac: Send Threshold = %d, Receive Threshold = %d\n",
		       dev->name, threshold_s, threshold_r);
		printk(KERN_INFO
		       "%s: XLlTemac: Send Wait bound = %d, Receive Wait bound = %d\n",
		       dev->name, timer_s, timer_r);
		if (XLlDma_BdRingStart(&lp->Dma.TxBdRing) == XST_FAILURE) {
			printk(KERN_ERR "%s: XLlTemac: could not start dma tx channel\n", dev->name);
			return -EIO;
		}
		if (XLlDma_BdRingStart(&lp->Dma.RxBdRing) == XST_FAILURE) {
			printk(KERN_ERR "%s: XLlTemac: could not start dma rx channel\n", dev->name);
			return -EIO;
		}
	}

#ifdef USE_TIMER
	/* Set up the PHY monitoring timer. */
	lp->phy_timer.expires = jiffies + 4 * HZ;
	lp->phy_timer.data = (unsigned long) dev;
	lp->phy_timer.function = &poll_gmii;
	init_timer(&lp->phy_timer);
	add_timer(&lp->phy_timer);
#else
    poll_gmii_thread = kthread_run(&poll_gmii, (void*)dev, "lltemac_poll_gmii");
    printk(KERN_INFO "%s: XLlTemac: %s started\n", dev->name, poll_gmii_thread->comm);
#endif

	return 0;
}

static int xenet_close(struct net_device *dev)
{
	struct net_local *lp;
	unsigned long flags;

	lp = (struct net_local *) netdev_priv(dev);

#ifdef USE_TIMER
	/* Shut down the PHY monitoring timer. */
	del_timer_sync(&lp->phy_timer);
#else
    kthread_stop(poll_gmii_thread);
#endif

	/* Stop Send queue */
	netif_stop_queue(dev);

	/* Now we could stop the device */
	_XLlTemac_Stop(&lp->Emac);

	/*
	 * Free the interrupt - not polled mode.
	 */
	free_irq(dev->irq, dev);
	if (XLlTemac_IsDma(&lp->Emac)) {
		free_irq(lp->dma_irq_s, dev);
		free_irq(lp->dma_irq_r, dev);
	} else {
		free_irq(lp->fifo_irq, dev);
	}

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
	struct net_local *lp = (struct net_local *) netdev_priv(dev);

	return &lp->stats;
}

static int descriptor_init(struct net_device *dev);
static void free_descriptor_skb(struct net_device *dev);


void xenet_set_multicast_list(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	int i;
	u32 Options;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Stop(&lp->Emac);

	Options = XLlTemac_GetOptions(&lp->Emac);
	Options &= ~XTE_MULTICAST_OPTION;
	Options &= ~XTE_PROMISC_OPTION;

#ifndef CONFIG_XILINX_LL_TEMAC_EXT
	for (i = 0; i < XTE_MULTI_MAT_ENTRIES; i++)
	  XLlTemac_MulticastClear(&lp->Emac, i);
	
	if(netdev_mc_count(dev) > XTE_MULTI_MAT_ENTRIES){
	  Options |= XTE_PROMISC_OPTION;
	  goto done;
	}
#else
	for (i = 0; i < XTE_MULTI_MAT_ENTRIES; i++)
	  XLlTemac_MulticastClear(&lp->Emac, i);
#endif

	/* If promisc, don't care about mc */
	if ((dev->flags & IFF_PROMISC) ||
	    (dev->flags & IFF_ALLMULTI)){
	  Options |= XTE_PROMISC_OPTION;
	  goto done;
	}
	
	if (dev->flags & IFF_MULTICAST) {
		struct netdev_hw_addr *ha;
		i = 0;
		netdev_for_each_mc_addr(ha, dev) {
		  XLlTemac_MulticastAdd(&lp->Emac, ha->addr, i++);
		}
		Options |= XTE_MULTICAST_OPTION;
	}

done:
	XLlTemac_SetOptions(&lp->Emac, Options);

	XLlTemac_Start(&lp->Emac);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}


static int xenet_change_mtu(struct net_device *dev, int new_mtu)
{
	int result;
	int device_enable = 0;
#ifdef CONFIG_XILINX_GIGE_VLAN
	int head_size = XTE_HDR_VLAN_SIZE;
#else
	int head_size = XTE_HDR_SIZE;
#endif
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	int max_frame = new_mtu + head_size + XTE_TRL_SIZE;
	int min_frame = 1 + head_size + XTE_TRL_SIZE;

	if (max_frame < min_frame)
		return -EINVAL;

	if (max_frame > XTE_MAX_JUMBO_FRAME_SIZE) {
		printk(KERN_INFO "Wrong MTU packet size. Use %d size\n",
							XTE_JUMBO_MTU);
		new_mtu = XTE_JUMBO_MTU;
	}

	dev->mtu = new_mtu;	/* change mtu in net_device structure */

	/* stop driver */
	if (netif_running(dev)) {
		device_enable = 1;
		xenet_close(dev);
	}
	/* free all created descriptors for previous size */
	free_descriptor_skb(dev);
	/* setup new frame size */
	lp->frame_size = dev->mtu + XTE_HDR_SIZE + XTE_TRL_SIZE;
	XLlDma_Initialize(&lp->Dma, lp->virt_dma_addr); /* initialize dma */

	result = descriptor_init(dev); /* create new skb with new size */
	if (result) {
		printk(KERN_ERR "Descriptor initialization failed.\n");
		return -EINVAL;
	}

	if (device_enable)
		xenet_open(dev); /* open the device */
	return 0;
}

static int xenet_FifoSend(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp;
	unsigned long flags, fifo_free_bytes;
	int total_frags = skb_shinfo(skb)->nr_frags + 1;
	unsigned int total_len;
	skb_frag_t *frag;
	int i;
	void *virt_addr;

	total_len = skb_headlen(skb);

	frag = &skb_shinfo(skb)->frags[0];
	for (i = 1; i < total_frags; i++, frag++) {
		total_len += frag->size;
	}

	/* The following lock is used to protect TxVacancy, Write
	 * and TxSetLen sequence which could happen from FifoSendHandler
	 * or other processor in SMP case.
	 */
	spin_lock_irqsave(&XTE_tx_spinlock, flags);
	lp = (struct net_local *) netdev_priv(dev);

	fifo_free_bytes = XLlFifo_TxVacancy(&lp->Fifo) * 4;
	if (fifo_free_bytes < total_len) {
		netif_stop_queue(dev);	/* stop send queue */
		lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
					   it in interrupt context */
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
		return 0;
	}

	/* Write frame data to FIFO */
	XLlFifo_Write(&lp->Fifo, (void *) skb->data, skb_headlen(skb));

	frag = &skb_shinfo(skb)->frags[0];
	for (i = 1; i < total_frags; i++, frag++) {
		virt_addr =
			(void *) page_address(frag->page.p) + frag->page_offset;
		XLlFifo_Write(&lp->Fifo, virt_addr, frag->size);
	}

	/* Initiate transmit */
	XLlFifo_TxSetLen(&lp->Fifo, total_len);
	lp->stats.tx_bytes += total_len;
	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);

	dev_kfree_skb(skb);	/* free skb */
	dev->trans_start = jiffies;
	return 0;
}

/* Callback function for completed frames sent in FIFO interrupt driven mode */
static void FifoSendHandler(struct net_device *dev)
{
	struct net_local *lp;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&XTE_tx_spinlock, flags);
	lp = (struct net_local *) netdev_priv(dev);
	lp->stats.tx_packets++;

	/*Send out the deferred skb and wake up send queue if a deferred skb exists */
	if (lp->deferred_skb) {
		int total_frags;
		unsigned int total_len;
		unsigned long fifo_free_bytes;
		skb_frag_t *frag;
		int i;
		void *virt_addr;

		skb = lp->deferred_skb;
		total_frags = skb_shinfo(skb)->nr_frags + 1;
		total_len = skb_headlen(skb);

		frag = &skb_shinfo(skb)->frags[0];
		for (i = 1; i < total_frags; i++, frag++) {
			total_len += frag->size;
		}

		fifo_free_bytes = XLlFifo_TxVacancy(&lp->Fifo) * 4;
		if (fifo_free_bytes < total_len) {
			/* If still no room for the deferred packet, return */
			spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
			return;
		}

		/* Write frame data to FIFO */
		XLlFifo_Write(&lp->Fifo, (void *) skb->data, skb_headlen(skb));

		frag = &skb_shinfo(skb)->frags[0];
		for (i = 1; i < total_frags; i++, frag++) {
			virt_addr =
				(void *) page_address(frag->page.p) + frag->page_offset;
			XLlFifo_Write(&lp->Fifo, virt_addr, frag->size);
		}

		/* Initiate transmit */
		XLlFifo_TxSetLen(&lp->Fifo, total_len);

		dev_kfree_skb(skb);	/* free skb */
		lp->deferred_skb = NULL;
		lp->stats.tx_packets++;
		lp->stats.tx_bytes += total_len;
		dev->trans_start = jiffies;
		netif_wake_queue(dev);	/* wake up send queue */
	}
	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
}

#if 0
/*
 * These are used for debugging purposes, left here in case they are useful
 * for further debugging
 */
static unsigned int _xenet_tx_csum(struct sk_buff *skb)
{
	unsigned int csum = 0;
	long csstart = skb_transport_header(skb) - skb->data;

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
 * xenet_DmaSend_internal is an internal use, send routine.
 * Any locks that need to be acquired, should be acquired
 * prior to calling this routine.
 */
static int xenet_DmaSend_internal(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp;
	XLlDma_Bd *bd_ptr;
	int result;
	int total_frags;
	int i;
	void *virt_addr;
	size_t len;
	dma_addr_t phy_addr;
	XLlDma_Bd *first_bd_ptr;
	XLlDma_Bd *last_bd_ptr;
	skb_frag_t *frag;

	lp = (struct net_local *) netdev_priv(dev);

	/* get skb_shinfo(skb)->nr_frags + 1 buffer descriptors */
	total_frags = skb_shinfo(skb)->nr_frags + 1;

	/* stats */
	if (lp->max_frags_in_a_packet < total_frags) {
		lp->max_frags_in_a_packet = total_frags;
	}

	if (total_frags < XTE_SEND_BD_CNT) {
		result = XLlDma_BdRingAlloc(&lp->Dma.TxBdRing, total_frags,
					    &bd_ptr);

		if (result != XST_SUCCESS) {
			netif_stop_queue(dev);	/* stop send queue */
			lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
						   it in interrupt context */
			return result;
		}
	} else {
		dev_kfree_skb(skb);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XLlTemac: could not send TX socket buffers (too many fragments).\n",
		       dev->name);
		return XST_FAILURE;
	}

	len = skb_headlen(skb);

	/* get the physical address of the header */
	phy_addr = (u32) dma_map_single(dev->dev.parent, skb->data, len, DMA_TO_DEVICE);

	/* get the header fragment, it's in the skb differently */
	XLlDma_mBdSetBufAddr(bd_ptr, phy_addr);
	XLlDma_mBdSetLength(bd_ptr, len);
	XLlDma_mBdSetId(bd_ptr, skb);

	/*
	 * if tx checksum offloading is enabled, when the ethernet stack
	 * wants us to perform the checksum in hardware,
	 * skb->ip_summed is CHECKSUM_PARTIAL. Otherwise skb->ip_summed is
	 * CHECKSUM_NONE, meaning the checksum is already done, or
	 * CHECKSUM_UNNECESSARY, meaning checksumming is turned off (e.g.
	 * loopback interface)
	 *
	 * skb->csum is an overloaded value. On send, skb->csum is the offset
	 * into the buffer (skb_transport_header(skb)) to place the csum value.
	 * On receive this feild gets set to the actual csum value, before it's
	 * passed up the stack.
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
	 * skb_transport_header(skb) points to the beginning of the ip header
	 *
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {

		unsigned int csum_start_off = skb_transport_offset(skb);
		unsigned int csum_index_off = csum_start_off + skb->csum_offset;

#if 0
		{
			unsigned int csum = _xenet_tx_csum(skb);

			*((unsigned short *) (raw + skb->csum)) =
				csum_fold(csum);
			BdCsumDisable(bd_ptr);
		}
#else
		BdCsumEnable(bd_ptr);
		BdCsumSetup(bd_ptr, csum_start_off, csum_index_off);

#endif
		lp->tx_hw_csums++;
	}
	else {
		/*
		 * This routine will do no harm even if hardware checksum capability is
		 * off.
		 */
		BdCsumDisable(bd_ptr);
	}

	first_bd_ptr = bd_ptr;
	last_bd_ptr = bd_ptr;

	frag = &skb_shinfo(skb)->frags[0];

	for (i = 1; i < total_frags; i++, frag++) {
		bd_ptr = XLlDma_mBdRingNext(&lp->Dma.TxBdRing, bd_ptr);
		last_bd_ptr = bd_ptr;

		virt_addr =
			(void *) page_address(frag->page.p) + frag->page_offset;
		phy_addr =
			(u32) dma_map_single(dev->dev.parent, virt_addr, frag->size,
					     DMA_TO_DEVICE);

		XLlDma_mBdSetBufAddr(bd_ptr, phy_addr);
		XLlDma_mBdSetLength(bd_ptr, frag->size);
		XLlDma_mBdSetId(bd_ptr, NULL);
		BdCsumDisable(bd_ptr);
		XLlDma_mBdSetStsCtrl(bd_ptr, 0);
	}

	if (first_bd_ptr == last_bd_ptr) {
		XLlDma_mBdSetStsCtrl(last_bd_ptr,
				     XLLDMA_BD_STSCTRL_SOP_MASK |
				     XLLDMA_BD_STSCTRL_EOP_MASK);
	} else {
		XLlDma_mBdSetStsCtrl(first_bd_ptr, XLLDMA_BD_STSCTRL_SOP_MASK);
		XLlDma_mBdSetStsCtrl(last_bd_ptr, XLLDMA_BD_STSCTRL_EOP_MASK);
	}


	/* Enqueue to HW */
	result = XLlDma_BdRingToHw(&lp->Dma.TxBdRing, total_frags,
				   first_bd_ptr);
	if (result != XST_SUCCESS) {
		netif_stop_queue(dev);	/* stop send queue */
		dev_kfree_skb(skb);
		XLlDma_mBdSetId(first_bd_ptr, NULL);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XLlTemac: could not send commit TX buffer descriptor (%d).\n",
		       dev->name, result);
		reset(dev, __LINE__);

		return XST_FAILURE;
	}

	dev->trans_start = jiffies;

	return XST_SUCCESS;
}

/* The send function for frames sent in DMA mode */
static int xenet_DmaSend(struct sk_buff *skb, struct net_device *dev)
{
	/* The following spin_lock protects
	 * SgAlloc, SgCommit sequence, which also exists in DmaSendHandlerBH Bottom
	 * Half, or triggered by other processor in SMP case.
	 */
	spin_lock_bh(&XTE_tx_spinlock);

	xenet_DmaSend_internal(skb, dev);

	spin_unlock_bh(&XTE_tx_spinlock);

	return 0;
}


static void DmaSendHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	XLlDma_Bd *BdPtr, *BdCurPtr;
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

		spin_lock_irqsave(&XTE_tx_spinlock, flags);
		dev = lp->ndev;
		bd_processed_save = 0;
		while ((bd_processed =
			XLlDma_BdRingFromHw(&lp->Dma.TxBdRing, XTE_SEND_BD_CNT,
					    &BdPtr)) > 0) {

			bd_processed_save = bd_processed;
			BdCurPtr = BdPtr;
			do {
				len = XLlDma_mBdGetLength(BdCurPtr);
				skb_dma_addr = (dma_addr_t) XLlDma_mBdGetBufAddr(BdCurPtr);
				dma_unmap_single(dev->dev.parent, skb_dma_addr, len,
						 DMA_TO_DEVICE);

				/* get ptr to skb */
				skb = (struct sk_buff *)
					XLlDma_mBdGetId(BdCurPtr);
				if (skb)
					dev_kfree_skb(skb);

				/* reset BD id */
				XLlDma_mBdSetId(BdCurPtr, NULL);

				lp->stats.tx_bytes += len;
				if (XLlDma_mBdGetStsCtrl(BdCurPtr) & XLLDMA_BD_STSCTRL_EOP_MASK) {
					lp->stats.tx_packets++;
				}

				BdCurPtr = XLlDma_mBdRingNext(&lp->Dma.TxBdRing, BdCurPtr);
				bd_processed--;
			} while (bd_processed > 0);

			result = XLlDma_BdRingFree(&lp->Dma.TxBdRing,
						   bd_processed_save, BdPtr);
			if (result != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XLlDma: BdRingFree() error %d.\n",
				       dev->name, result);
				reset(dev, __LINE__);
				spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
				return;
			}
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);

		/* Send out the deferred skb if it exists */
		if ((lp->deferred_skb) && bd_processed_save) {
			skb = lp->deferred_skb;
			lp->deferred_skb = NULL;

			result = xenet_DmaSend_internal(skb, dev);
		}

		if (result == XST_SUCCESS) {
			netif_wake_queue(dev);	/* wake up send queue */
		}
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
	}
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

	lp = (struct net_local *) netdev_priv(dev);
	printk(KERN_ERR
	       "%s: XLlTemac: exceeded transmit timeout of %lu ms.  Resetting emac.\n",
	       dev->name, TX_TIMEOUT * 1000UL / HZ);
	lp->stats.tx_errors++;

	reset(dev, __LINE__);

	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
}

/* The callback function for frames received when in FIFO mode. */
static void FifoRecvHandler(unsigned long p)
{
  unsigned int align;
  struct sk_buff *skb;
  unsigned long flags;
  struct net_local *lp;
  struct net_device *dev;
  
  spin_lock_irqsave(&receivedQueueSpin, flags);
  if (list_empty(&receivedQueue)) {
    spin_unlock_irqrestore(&receivedQueueSpin, flags);
    return;
  }

  lp = list_entry(receivedQueue.next, struct net_local, rcv);
  list_del_init(&(lp->rcv));
  spin_unlock_irqrestore(&receivedQueueSpin, flags);
  dev = lp->ndev;
  
  while (XLlFifo_RxOccupancy(&lp->Fifo) != 0) {
    
    u32 len = ETH_FRAME_LEN + ETH_FCS_LEN;
    u32 fifo_len = XLlFifo_RxGetLen(&lp->Fifo);
    /* Correct len */
    if(!(len = ((fifo_len < len) ? fifo_len : len)))
      break;
    /*
     * TODO: Hm this is odd, if we can't allocate the skb, we throw away the next packet. Why?
     */
    if(!(skb = alloc_skb(len + ALIGNMENT_RECV, GFP_ATOMIC))) {
#define XTE_RX_SINK_BUFFER_SIZE_u8  1024
#define XTE_RX_SINK_BUFFER_SIZE_u32 (XTE_RX_SINK_BUFFER_SIZE_u8 / sizeof(u32))
      static u32 rx_buffer_sink[XTE_RX_SINK_BUFFER_SIZE_u32];
      
      /* Couldn't get memory. */
      lp->stats.rx_dropped++;
      printk(KERN_ERR "%s: XLlTemac: could not allocate receive buffer.\n",
	     dev->name);
      
      /* consume data in Xilinx TEMAC RX data fifo so it is sync with RX length fifo */
      for (; len > XTE_RX_SINK_BUFFER_SIZE_u8;
	   len -= XTE_RX_SINK_BUFFER_SIZE_u8)
	XLlFifo_Read(&lp->Fifo, rx_buffer_sink, XTE_RX_SINK_BUFFER_SIZE_u8);
      /* Last bytes to read before breaking */
      XLlFifo_Read(&lp->Fifo, rx_buffer_sink, len);
      break;
    }
    
    /* align to %4 addresses */
    align = FIFO_BUFFER_ALIGN(skb->data);
    if(align) skb_reserve(skb, align);
    
    /* printk(KERN_WARNING "driver skb->data after mod %p\n", skb->data); */
    skb_reserve(skb, 2);
    
    /* Read the packet data */
    XLlFifo_Read(&lp->Fifo, skb->data, len);
    lp->stats.rx_packets++;
    lp->stats.rx_bytes += len;
    
    skb_put(skb, len); /* Tell the skb how much data we got. */
    skb->dev = dev; /* Fill out required meta-data. */
    skb->protocol = eth_type_trans(skb, dev);
    skb->ip_summed = CHECKSUM_NONE;
    netif_rx(skb); /* Send the packet upstream. */
  }
  
  XLlFifo_IntEnable(&lp->Fifo, XLLF_INT_TC_MASK | XLLF_INT_RC_MASK |
		    XLLF_INT_RXERROR_MASK | XLLF_INT_TXERROR_MASK);
}


/*
 * _xenet_DmaSetupRecvBuffers allocates as many socket buffers (sk_buff's) as it
 * can up to the number of free RX buffer descriptors. Then it sets up the RX
 * buffer descriptors to DMA into the socket_buffers.
 *
 * The net_device, dev, indcates on which device to operate for buffer
 * descriptor allocation.
 */
static void _xenet_DmaSetupRecvBuffers(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);

	int free_bd_count = XLlDma_mBdRingGetFreeCnt(&lp->Dma.RxBdRing);
	int num_sk_buffs;
	struct sk_buff_head sk_buff_list;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	XLlDma_Bd *BdPtr, *BdCurPtr;
	u32 align;
	int result;


	skb_queue_head_init(&sk_buff_list);
	for (num_sk_buffs = 0; num_sk_buffs < free_bd_count; num_sk_buffs++) {
		new_skb = netdev_alloc_skb_ip_align(dev, lp->frame_size);
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
		printk(KERN_ERR "%s: XLlTemac: alloc_skb unsuccessful\n",
		       dev->name);
		return;
	}

	/* now we got a bunch o' sk_buffs */
	result = XLlDma_BdRingAlloc(&lp->Dma.RxBdRing, num_sk_buffs, &BdPtr);
	if (result != XST_SUCCESS) {
		/* we really shouldn't get this */
		skb_queue_purge(&sk_buff_list);
		printk(KERN_ERR "%s: XLlDma: BdRingAlloc unsuccessful (%d)\n",
		       dev->name, result);
		reset(dev, __LINE__);
		return;
	}

	BdCurPtr = BdPtr;

	new_skb = skb_dequeue(&sk_buff_list);
	while (new_skb) {
		/* make sure we're long-word aligned */
		align = BUFFER_ALIGNRECV(new_skb->data);
		if (align) {
			skb_reserve(new_skb, align);
		}

		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(dev->dev.parent,
					new_skb->data, lp->frame_size,
						     DMA_FROM_DEVICE);

		XLlDma_mBdSetBufAddr(BdCurPtr, new_skb_baddr);
		XLlDma_mBdSetLength(BdCurPtr, lp->frame_size);
		XLlDma_mBdSetId(BdCurPtr, new_skb);
		XLlDma_mBdSetStsCtrl(BdCurPtr,
				     XLLDMA_BD_STSCTRL_SOP_MASK |
				     XLLDMA_BD_STSCTRL_EOP_MASK);

		BdCurPtr = XLlDma_mBdRingNext(&lp->Dma.RxBdRing, BdCurPtr);

		new_skb = skb_dequeue(&sk_buff_list);
	}

	/* enqueue RxBD with the attached skb buffers such that it is
	 * ready for frame reception */
	result = XLlDma_BdRingToHw(&lp->Dma.RxBdRing, num_sk_buffs, BdPtr);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR
		       "%s: XLlDma: (DmaSetupRecvBuffers) BdRingToHw unsuccessful (%d)\n",
		       dev->name, result);
		skb_queue_purge(&sk_buff_list);
		BdCurPtr = BdPtr;
		while (num_sk_buffs > 0) {
			XLlDma_mBdSetId(BdCurPtr, NULL);
			BdCurPtr = XLlDma_mBdRingNext(&lp->Dma.RxBdRing,
						      BdCurPtr);
			num_sk_buffs--;
		}
		reset(dev, __LINE__);
		return;
	}
}

static void DmaRecvHandlerBH(unsigned long p)
{
	struct net_device *dev;
	struct net_local *lp;
	struct sk_buff *skb;
	u32 len, skb_baddr;
	int result;
	unsigned long flags;
	XLlDma_Bd *BdPtr, *BdCurPtr;
	unsigned int bd_processed, bd_processed_saved;

	while (1) {
		spin_lock_irqsave(&receivedQueueSpin, flags);
		if (list_empty(&receivedQueue)) {
			spin_unlock_irqrestore(&receivedQueueSpin, flags);
			break;
		}
		lp = list_entry(receivedQueue.next, struct net_local, rcv);

		list_del_init(&(lp->rcv));
		spin_unlock_irqrestore(&receivedQueueSpin, flags);
		dev = lp->ndev;

		spin_lock_irqsave(&XTE_rx_spinlock, flags);
		if ((bd_processed =
		     XLlDma_BdRingFromHw(&lp->Dma.RxBdRing, XTE_RECV_BD_CNT, &BdPtr)) > 0) {

			bd_processed_saved = bd_processed;
			BdCurPtr = BdPtr;
			do {
				/*
				 * Regular length field not updated on rx,
				 * USR4 updated instead.
				 */
				len = BdGetRxLen(BdCurPtr);

				/* get ptr to skb */
				skb = (struct sk_buff *)
					XLlDma_mBdGetId(BdCurPtr);

				/* get and free up dma handle used by skb->data */
				skb_baddr = (dma_addr_t) XLlDma_mBdGetBufAddr(BdCurPtr);
				dma_unmap_single(dev->dev.parent, skb_baddr,
						 lp->frame_size,
						 DMA_FROM_DEVICE);

				/* reset ID */
				XLlDma_mBdSetId(BdCurPtr, NULL);

				/* setup received skb and send it upstream */
				skb_put(skb, len);	/* Tell the skb how much data we got. */
				skb->dev = dev;

				/* this routine adjusts skb->data to skip the header */
				skb->protocol = eth_type_trans(skb, dev);

				/* default the ip_summed value */
				skb->ip_summed = CHECKSUM_NONE;

				/* if we're doing rx csum offload, set it up */
				if (((lp->local_features & LOCAL_FEATURE_RX_CSUM) != 0) &&
				    (skb->protocol == __constant_htons(ETH_P_IP)) &&
				    (skb->len > 64)) {
					unsigned int csum;

					/*
					 * This hardware only supports proper checksum calculations
					 * on TCP/UDP packets.
					 *
					 * skb->csum is an overloaded value. On send, skb->csum is
					 * the offset into the buffer (skb_transport_header(skb))
					 * to place the csum value. On receive this feild gets set
					 * to the actual csum value, before it's passed up the stack.
					 *
					 * If we set skb->ip_summed to CHECKSUM_COMPLETE, the ethernet
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
					 * 3) skb->ip_summed was set to CHECKSUM_COMPLETE, skb->csum was
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
					csum = BdCsumGet(BdCurPtr);

#if ! XTE_AUTOSTRIPPING
					if (!lp->stripping) {
						/* take off the FCS */
						u16 *data;

						/* FCS is 4 bytes */
						skb_put(skb, -4);

						data = (u16 *) (&skb->
								data[skb->len]);

						/* subtract out the FCS from the csum value */
						csum = csum_sub(csum, *data /* & 0xffff */);
						data++;
						csum = csum_sub(csum, *data /* & 0xffff */);
					}
#endif
					skb->csum = csum;
					skb->ip_summed = CHECKSUM_COMPLETE;

					lp->rx_hw_csums++;
				}

				lp->stats.rx_packets++;
				lp->stats.rx_bytes += len;
				netif_rx(skb);	/* Send the packet upstream. */

				BdCurPtr =
					XLlDma_mBdRingNext(&lp->Dma.RxBdRing,
							   BdCurPtr);
				bd_processed--;
			} while (bd_processed > 0);

			/* give the descriptor back to the driver */
			result = XLlDma_BdRingFree(&lp->Dma.RxBdRing,
						   bd_processed_saved, BdPtr);
			if (result != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XLlDma: BdRingFree unsuccessful (%d)\n",
				       dev->name, result);
				reset(dev, __LINE__);
				spin_unlock_irqrestore(&XTE_rx_spinlock, flags);
				return;
			}

			_xenet_DmaSetupRecvBuffers(dev);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
		spin_unlock_irqrestore(&XTE_rx_spinlock, flags);
	}
}

static int descriptor_init(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	int recvsize, sendsize;
	int dftsize;
	u32 *recvpoolptr, *sendpoolptr;
	void *recvpoolphy, *sendpoolphy;
	int result;

/*
 * Buffer Descriptr
 * word	byte	description
 * 0	0h	next ptr
 * 1	4h	buffer addr
 * 2	8h	buffer len
 * 3	ch	sts/ctrl | app data (0) [tx csum enable (bit 31 LSB)]
 * 4	10h	app data (1) [tx csum begin (bits 0-15 MSB) | csum insert (bits 16-31 LSB)]
 * 5	14h	app data (2) [tx csum seed (bits 16-31 LSB)]
 * 6	18h	app data (3) [rx raw csum (bits 16-31 LSB)]
 * 7	1ch	app data (4) [rx recv length (bits 18-31 LSB)]
 */
#if 0
	int XferType = XDMAV3_DMACR_TYPE_BFBURST_MASK;
	int XferWidth = XDMAV3_DMACR_DSIZE_64_MASK;
#endif

	/* calc size of descriptor space pool; alloc from non-cached memory */
	dftsize = XLlDma_mBdRingMemCalc(ALIGNMENT_BD,
					XTE_RECV_BD_CNT + XTE_SEND_BD_CNT);
	printk(KERN_INFO "XLlTemac: buffer descriptor size: %d (0x%0x)\n",
	       dftsize, dftsize);

#if BD_IN_BRAM == 0
	/*
	 * Allow buffer descriptors to be cached.
	 * Old method w/cache on buffer descriptors disabled:
	 *     lp->desc_space = dma_alloc_coherent(NULL, dftsize,
	 *         &lp->desc_space_handle, GFP_KERNEL);
	 * (note if going back to dma_alloc_coherent() the CACHE macros in
	 * xenv_linux.h need to be disabled.
	 */

        printk(KERN_INFO "XLlTemac: Allocating DMA descriptors with kmalloc");
        lp->desc_space = kmalloc(dftsize, GFP_KERNEL);
	lp->desc_space_handle = (dma_addr_t) page_to_phys(virt_to_page(lp->desc_space));
#else
        printk(KERN_INFO "XLlTemac: Allocating DMA descriptors in Block Ram");
	lp->desc_space_handle = BRAM_BASEADDR;
	lp->desc_space = ioremap(lp->desc_space_handle, dftsize);
#endif
	if (lp->desc_space == 0) {
		return -1;
	}

	lp->desc_space_size = dftsize;

	printk(KERN_INFO
	       "XLlTemac: (buffer_descriptor_init) phy: 0x%x, virt: 0x%x, size: 0x%x\n",
	       (unsigned int)lp->desc_space_handle, (unsigned int) lp->desc_space,
	       lp->desc_space_size);

	/* calc size of send and recv descriptor space */
	recvsize = XLlDma_mBdRingMemCalc(ALIGNMENT_BD, XTE_RECV_BD_CNT);
	sendsize = XLlDma_mBdRingMemCalc(ALIGNMENT_BD, XTE_SEND_BD_CNT);

	recvpoolptr = lp->desc_space;
	sendpoolptr = (void *) ((u32) lp->desc_space + recvsize);

	/* cast the handle to a u32 1st just to keep the compiler happy */
	recvpoolphy = (void *) (u32)lp->desc_space_handle;
	sendpoolphy = (void *) ((u32) lp->desc_space_handle + recvsize);

	result = XLlDma_BdRingCreate(&lp->Dma.RxBdRing, (u32) recvpoolphy,
				     (u32) recvpoolptr, ALIGNMENT_BD,
				     XTE_RECV_BD_CNT);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR "XLlTemac: DMA Ring Create (RECV). Error: %d\n", result);
		return -EIO;
	}

	result = XLlDma_BdRingCreate(&lp->Dma.TxBdRing, (u32) sendpoolphy,
				     (u32) sendpoolptr, ALIGNMENT_BD,
				     XTE_SEND_BD_CNT);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR "XLlTemac: DMA Ring Create (SEND). Error: %d\n", result);
		return -EIO;
	}

	_xenet_DmaSetupRecvBuffers(dev);
	return 0;
}

static void free_descriptor_skb(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	XLlDma_Bd *BdPtr;
	struct sk_buff *skb;
	dma_addr_t skb_dma_addr;
	u32 len, i;

	/* Unmap and free skb's allocated and mapped in descriptor_init() */

	/* Get the virtual address of the 1st BD in the DMA RX BD ring */
	BdPtr = (XLlDma_Bd *) lp->Dma.RxBdRing.FirstBdAddr;

	for (i = 0; i < XTE_RECV_BD_CNT; i++) {
		skb = (struct sk_buff *) XLlDma_mBdGetId(BdPtr);
		if (skb) {
			skb_dma_addr = (dma_addr_t) XLlDma_mBdGetBufAddr(BdPtr);
			dma_unmap_single(dev->dev.parent, skb_dma_addr,
					lp->frame_size, DMA_FROM_DEVICE);
			dev_kfree_skb(skb);
		}
		/* find the next BD in the DMA RX BD ring */
		BdPtr = XLlDma_mBdRingNext(&lp->Dma.RxBdRing, BdPtr);
	}

	/* Unmap and free TX skb's that have not had a chance to be freed
	 * in DmaSendHandlerBH(). This could happen when TX Threshold is larger
	 * than 1 and TX waitbound is 0
	 */

	/* Get the virtual address of the 1st BD in the DMA TX BD ring */
	BdPtr = (XLlDma_Bd *) lp->Dma.TxBdRing.FirstBdAddr;

	for (i = 0; i < XTE_SEND_BD_CNT; i++) {
		skb = (struct sk_buff *) XLlDma_mBdGetId(BdPtr);
		if (skb) {
			skb_dma_addr = (dma_addr_t) XLlDma_mBdGetBufAddr(BdPtr);
			len = XLlDma_mBdGetLength(BdPtr);
			dma_unmap_single(dev->dev.parent, skb_dma_addr, len,
					 DMA_TO_DEVICE);
			dev_kfree_skb(skb);
		}
		/* find the next BD in the DMA TX BD ring */
		BdPtr = XLlDma_mBdRingNext(&lp->Dma.TxBdRing, BdPtr);
	}

#if BD_IN_BRAM == 0
	kfree(lp->desc_space);
/* this is old approach which was removed */
/*	dma_free_coherent(NULL,
			  lp->desc_space_size,
			  lp->desc_space, lp->desc_space_handle); */
#else
	iounmap(lp->desc_space);
#endif
}

static int
xenet_ethtool_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	u32 mac_options;
	u32 threshold, timer;
	u16 gmii_cmd, gmii_status, gmii_advControl;

	memset(ecmd, 0, sizeof(struct ethtool_cmd));

	mac_options = XLlTemac_GetOptions(&(lp->Emac));
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &gmii_cmd);
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &gmii_status);

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_ADVERTISE, &gmii_advControl);

	ecmd->duplex = DUPLEX_FULL;

	ecmd->supported |= SUPPORTED_MII;

	ecmd->port = PORT_MII;

	ecmd->speed = lp->cur_speed;

	if (gmii_status & BMSR_ANEGCAPABLE) {
		ecmd->supported |= SUPPORTED_Autoneg;
	}
   
   ecmd->autoneg = lp->cur_autoneg;
	if (gmii_status & BMSR_ANEGCOMPLETE) {
		//ecmd->autoneg = AUTONEG_ENABLE;
		ecmd->advertising |= ADVERTISED_Autoneg;
	}
	else {
		//ecmd->autoneg = AUTONEG_DISABLE;
	}
	ecmd->phy_address = lp->Emac.Config.BaseAddress;
	ecmd->transceiver = XCVR_INTERNAL;
	if (XLlTemac_IsDma(&lp->Emac)) {
		/* get TX threshold */

		XLlDma_BdRingGetCoalesce(&lp->Dma.TxBdRing, &threshold, &timer);
		ecmd->maxtxpkt = threshold;

		/* get RX threshold */
		XLlDma_BdRingGetCoalesce(&lp->Dma.RxBdRing, &threshold, &timer);
		ecmd->maxrxpkt = threshold;
	}

	ecmd->supported |= SUPPORTED_10baseT_Full | SUPPORTED_100baseT_Full |
		SUPPORTED_1000baseT_Full ; //| SUPPORTED_Autoneg;

	return 0;
}

static int
xenet_ethtool_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);

	if ((ecmd->duplex != DUPLEX_FULL) ||
	    (ecmd->transceiver != XCVR_INTERNAL) ||
	    (ecmd->phy_address &&
	     (ecmd->phy_address != lp->Emac.Config.BaseAddress))) {
		return -EOPNOTSUPP;
	}

	if ((ecmd->speed != 1000) && (ecmd->speed != 100) &&
	    (ecmd->speed != 10)) {
		printk(KERN_ERR
		       "%s: XLlTemac: xenet_ethtool_set_settings speed not supported: %d\n",
		       dev->name, ecmd->speed);
		return -EOPNOTSUPP;
	}

	if ((ecmd->speed != lp->cur_speed) || ( ecmd->autoneg != lp->cur_autoneg)) {
		set_phy_speed(dev, ecmd->speed, FULL_DUPLEX, ecmd->autoneg);
		_XLlTemac_SetOperatingSpeed(&lp->Emac, ecmd->speed);
	}
	return 0;
}

static int
xenet_ethtool_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	u32 threshold, waitbound;

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	if (!(XLlTemac_IsDma(&lp->Emac))) {
		return -EIO;
	}

	XLlDma_BdRingGetCoalesce(&lp->Dma.RxBdRing, &threshold, &waitbound);
	ec->rx_max_coalesced_frames = threshold;
	ec->rx_coalesce_usecs = waitbound;

	XLlDma_BdRingGetCoalesce(&lp->Dma.TxBdRing, &threshold, &waitbound);
	ec->tx_max_coalesced_frames = threshold;
	ec->tx_coalesce_usecs = waitbound;

	return 0;
}

void disp_bd_ring(XLlDma_BdRing *bd_ring)
{
	int num_bds = bd_ring->AllCnt;
	u32 *cur_bd_ptr = (u32 *) bd_ring->FirstBdAddr;
	int idx;

	printk("ChanBase: %p\n", (void *) bd_ring->ChanBase);
	printk("FirstBdPhysAddr: %p\n", (void *) bd_ring->FirstBdPhysAddr);
	printk("FirstBdAddr: %p\n", (void *) bd_ring->FirstBdAddr);
	printk("LastBdAddr: %p\n", (void *) bd_ring->LastBdAddr);
	printk("Length: %d (0x%0x)\n", bd_ring->Length, bd_ring->Length);
	printk("RunState: %d (0x%0x)\n", bd_ring->RunState, bd_ring->RunState);
	printk("Separation: %d (0x%0x)\n", bd_ring->Separation,
	       bd_ring->Separation);
	printk("BD Count: %d\n", bd_ring->AllCnt);

	printk("\n");

	printk("FreeHead: %p\n", (void *) bd_ring->FreeHead);
	printk("PreHead: %p\n", (void *) bd_ring->PreHead);
	printk("HwHead: %p\n", (void *) bd_ring->HwHead);
	printk("HwTail: %p\n", (void *) bd_ring->HwTail);
	printk("PostHead: %p\n", (void *) bd_ring->PostHead);
	printk("BdaRestart: %p\n", (void *) bd_ring->BdaRestart);

	printk("Ring Contents:\n");
/*
 * Buffer Descriptr
 * word	byte	description
 * 0	0h	next ptr
 * 1	4h	buffer addr
 * 2	8h	buffer len
 * 3	ch	sts/ctrl | app data (0) [tx csum enable (bit 31 LSB)]
 * 4	10h	app data (1) [tx csum begin (bits 0-15 MSB) | csum insert (bits 16-31 LSB)]
 * 5	14h	app data (2) [tx csum seed (bits 16-31 LSB)]
 * 6	18h	app data (3) [rx raw csum (bits 16-31 LSB)]
 * 7	1ch	app data (4) [rx recv length (bits 18-31 LSB)]
 * 8	20h	sw app data (0) [id]
 */
	printk("Idx   NextBD BuffAddr   Length  CTL/CSE CSUM B/I CSUMSeed Raw CSUM  RecvLen       ID\n");
	printk("--- -------- -------- -------- -------- -------- -------- -------- -------- --------\n");

	for (idx = 0; idx < num_bds; idx++) {
		printk("%3d %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		       idx,
		       cur_bd_ptr[XLLDMA_BD_NDESC_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_BUFA_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_BUFL_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_STSCTRL_USR0_OFFSET /
				  sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_USR1_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_USR2_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_USR3_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_USR4_OFFSET / sizeof(*cur_bd_ptr)],
		       cur_bd_ptr[XLLDMA_BD_ID_OFFSET / sizeof(*cur_bd_ptr)]);

		cur_bd_ptr += bd_ring->Separation / sizeof(int);
	}
	printk("--------------------------------------- Done ---------------------------------------\n");
}

static int
xenet_ethtool_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	int ret;
	struct net_local *lp;

	lp = (struct net_local *) netdev_priv(dev);

	if (!(XLlTemac_IsDma(&lp->Emac))) {
		return -EIO;
	}

	if (ec->rx_coalesce_usecs == 0) {
		ec->rx_coalesce_usecs = 1;
		dma_rx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK & ~XLLDMA_CR_IRQ_DELAY_EN_MASK;
	}
	if ((ret = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
			(u16) (ec->rx_max_coalesced_frames),
			(u16) (ec->rx_coalesce_usecs))) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XLlDma: BdRingSetCoalesce error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);

	if (ec->tx_coalesce_usecs == 0) {
		ec->tx_coalesce_usecs = 1;
		dma_tx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK & ~XLLDMA_CR_IRQ_DELAY_EN_MASK;
	}
	if ((ret = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
			(u16) (ec->tx_max_coalesced_frames),
			(u16) (ec->tx_coalesce_usecs))) != XST_SUCCESS) {
		printk(KERN_ERR "%s: XLlDma: BdRingSetCoalesce error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);

	return 0;
}

static void
xenet_ethtool_get_ringparam(struct net_device *dev,
			    struct ethtool_ringparam *erp)
{
	memset(erp, 0, sizeof(struct ethtool_ringparam));

	erp->rx_max_pending = XTE_RECV_BD_CNT;
	erp->tx_max_pending = XTE_SEND_BD_CNT;
	erp->rx_pending = XTE_RECV_BD_CNT;
	erp->tx_pending = XTE_SEND_BD_CNT;
}

static void
xenet_ethtool_get_pauseparam(struct net_device *dev,
			     struct ethtool_pauseparam *epp)
{
	u32 Options;
	u16 gmii_status;
	struct net_local *lp = netdev_priv(dev);

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_ADVERTISE, &gmii_status);

   if ((gmii_status & ADVERTISE_PAUSE_CAP) || (gmii_status & ADVERTISE_PAUSE_ASYM)) 
   {
      epp->autoneg = AUTONEG_ENABLE;
//      printk(KERN_WARNING "pause get: on\n");
   }
   else 
   {
      epp->autoneg = AUTONEG_DISABLE;
//      printk(KERN_WARNING "pause get: off\n");
   }

	Options = XLlTemac_GetOptions(&lp->Emac);
	if (Options & XTE_FLOW_CONTROL_OPTION) {
		epp->rx_pause = 1;
		epp->tx_pause = 1;
	}
	else {
		epp->rx_pause = 0;
		epp->tx_pause = 0;
	}
}

static int
xenet_ethtool_set_pauseparam(struct net_device *dev,
			     struct ethtool_pauseparam *epp)
{
	u32 Options;
	u16 gmii_status=0;
	struct net_local *lp = netdev_priv(dev);

	
//   printk(KERN_WARNING "%s\n",__func__);
   
   Options = XLlTemac_GetOptions(&lp->Emac);
	if (!(Options & XTE_FLOW_CONTROL_OPTION)) 
   {
      return -EOPNOTSUPP;
   }

	
   _XLlTemac_PhyRead (&lp->Emac, lp->gmii_addr, MII_ADVERTISE, &gmii_status);
   
//   printk(KERN_WARNING "mii_ad: %04X\n",gmii_status);
   
	if (epp->autoneg)
   {
//      printk(KERN_WARNING "pause to_set : on\n");
      gmii_status |= ADVERTISE_PAUSE_CAP;
      gmii_status |= ADVERTISE_PAUSE_ASYM;
      lp->cur_pause = 1;
   }
   else
   {
//      printk(KERN_WARNING "pause to_set : off\n");
      gmii_status &= ~ADVERTISE_PAUSE_CAP;
      gmii_status &= ~ADVERTISE_PAUSE_ASYM;
      lp->cur_pause = 0;
   }
   _XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_ADVERTISE, gmii_status);
   

   return 0;
}

#if 0
static u32
xenet_ethtool_get_rx_csum(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	u32 retval;
   
 	retval = (lp->local_features & LOCAL_FEATURE_RX_CSUM) != 0;

 	return retval;
}

static int
xenet_ethtool_set_rx_csum(struct net_device *dev, u32 onoff)
{
	struct net_local *lp = netdev_priv(dev);

	if (onoff) {
		if (XLlTemac_IsRxCsum(&lp->Emac) == TRUE) {
			lp->local_features |=
				LOCAL_FEATURE_RX_CSUM;
		}
	}
	else {
		lp->local_features &= ~LOCAL_FEATURE_RX_CSUM;
	}

	return 0;
}

static u32
xenet_ethtool_get_tx_csum(struct net_device *dev)
{
	u32 retval;

	retval = (dev->features & NETIF_F_IP_CSUM) != 0;
	return retval;
}

static int
xenet_ethtool_set_tx_csum(struct net_device *dev, u32 onoff)
{
	struct net_local *lp = netdev_priv(dev);

	if (onoff) {
		if (XLlTemac_IsTxCsum(&lp->Emac) == TRUE) {
			dev->features |= NETIF_F_IP_CSUM;
		}
	}
	else {
		dev->features &= ~NETIF_F_IP_CSUM;
	}

	return 0;
}

static u32
xenet_ethtool_get_sg(struct net_device *dev)
{
	u32 retval;

	retval = (dev->features & NETIF_F_SG) != 0;

	return retval;
}

static int
xenet_ethtool_set_sg(struct net_device *dev, u32 onoff)
{
	struct net_local *lp = netdev_priv(dev);

	if (onoff) {
		if (XLlTemac_IsDma(&lp->Emac)) {
			dev->features |=
				NETIF_F_SG | NETIF_F_FRAGLIST;
		}
	}
	else {
		dev->features &=
			~(NETIF_F_SG | NETIF_F_FRAGLIST);
	}

	return 0;
}
#endif
static void
xenet_ethtool_get_strings(struct net_device *dev, u32 stringset, u8 *strings)
{
	*strings = 0;

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(strings,
			&xenet_ethtool_gstrings_stats,
			sizeof(xenet_ethtool_gstrings_stats));

		break;

	default:
		break;
	}
}

static void
xenet_ethtool_get_ethtool_stats(struct net_device *dev,
	struct ethtool_stats *stats, u64 *data)
{
	struct net_local *lp = netdev_priv(dev);

	data[0] = lp->stats.tx_packets;
	data[1] = lp->stats.tx_dropped;
	data[2] = lp->stats.tx_errors;
	data[3] = lp->stats.tx_fifo_errors;
	data[4] = lp->stats.rx_packets;
	data[5] = lp->stats.rx_dropped;
	data[6] = lp->stats.rx_errors;
	data[7] = lp->stats.rx_fifo_errors;
	data[8] = lp->stats.rx_crc_errors;
	data[9] = lp->max_frags_in_a_packet;
	data[10] = lp->tx_hw_csums;
	data[11] = lp->rx_hw_csums;
}

static int
xenet_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return XENET_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}


#define EMAC_REGS_N 32
struct mac_regsDump {
	struct ethtool_regs hd;
	u16 data[EMAC_REGS_N];
};

int
xenet_ethtool_get_regs_len(struct net_device *dev)
{
	return (sizeof(u16) * EMAC_REGS_N);
}

static void
xenet_ethtool_get_regs(struct net_device *dev, struct ethtool_regs *regs,
		       void *ret)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	struct mac_regsDump *dump = (struct mac_regsDump *) regs;
	int i;

	dump->hd.version = 0;
	dump->hd.len = sizeof(dump->data);
	memset(dump->data, 0, sizeof(dump->data));

	for (i = 0; i < EMAC_REGS_N; i++) {
		_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, i, &(dump->data[i]));
	}

	*(int *) ret = 0;
}

static void
xenet_ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *ed)
{
	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strncpy(ed->driver, DRIVER_NAME, sizeof(ed->driver) - 1);
	strncpy(ed->version, DRIVER_VERSION, sizeof(ed->version) - 1);
	/* Also tell how much memory is needed for dumping register values */
	ed->regdump_len = sizeof(u16) * EMAC_REGS_N;
	ed->n_stats = XENET_STATS_LEN;
}

/*
 * xenet_do_ethtool_ioctl:
 * DEPRECATED
 */
static int xenet_do_ethtool_ioctl(struct net_device *dev, struct ifreq *rq)
{
	struct net_local *lp = (struct net_local *) netdev_priv(dev);
	struct ethtool_cmd ecmd;
	struct ethtool_coalesce eco;
	struct ethtool_drvinfo edrv;
	struct ethtool_ringparam erp;
	struct ethtool_pauseparam *epp_ptr = (struct ethtool_pauseparam*) &ecmd;
	struct mac_regsDump regs;
	int ret = -EOPNOTSUPP;

	if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd)))
      return -EFAULT;
	
   switch (ecmd.cmd) {
	case ETHTOOL_GSET:	/* Get setting. No command option needed w/ ethtool */
		ret = xenet_ethtool_get_settings(dev, &ecmd);
		if (ret < 0)
			return ret;
		if (copy_to_user(rq->ifr_data, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		ret = 0;
		break;
	case ETHTOOL_SSET:	/* Change setting. Use "-s" command option w/ ethtool */
		ret = xenet_ethtool_set_settings(dev, &ecmd);
		break;
	case ETHTOOL_GPAUSEPARAM:	/* Get pause parameter information. Use "-a" w/ ethtool */
		xenet_ethtool_get_pauseparam (dev, epp_ptr);
		if (copy_to_user(rq->ifr_data, epp_ptr, sizeof(struct ethtool_pauseparam)))
			return -EFAULT;
		ret = 0;
		break;
	case ETHTOOL_SPAUSEPARAM:	/* Set pause parameter. Use "-A" w/ ethtool */
      ret = xenet_ethtool_set_pauseparam (dev, epp_ptr);
		break;
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

			if (edata.data) {
				if (XLlTemac_IsRxCsum(&lp->Emac) == TRUE) {
					lp->local_features |=
						LOCAL_FEATURE_RX_CSUM;
				}
			}
			else {
				lp->local_features &= ~LOCAL_FEATURE_RX_CSUM;
			}

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
				if (XLlTemac_IsTxCsum(&lp->Emac) == TRUE) {
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
				if (XLlTemac_IsDma(&lp->Emac)) {
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
		if (!(XLlTemac_IsDma(&lp->Emac)))
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
		if (!(XLlTemac_IsDma(&lp->Emac)))
			break;
		if (copy_from_user
		    (&eco, rq->ifr_data, sizeof(struct ethtool_coalesce)))
			return -EFAULT;
		ret = xenet_ethtool_set_coalesce(dev, &eco);
		break;
	case ETHTOOL_GDRVINFO:	/* Get driver information. Use "-i" w/ ethtool */
		edrv.cmd = edrv.cmd;
		xenet_ethtool_get_drvinfo(dev, &edrv);
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
		xenet_ethtool_get_ringparam(dev, &(erp));
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

			stats.data[0] = lp->stats.tx_packets;
			stats.data[1] = lp->stats.tx_dropped;
			stats.data[2] = lp->stats.tx_errors;
			stats.data[3] = lp->stats.tx_fifo_errors;
			stats.data[4] = lp->stats.rx_packets;
			stats.data[5] = lp->stats.rx_dropped;
			stats.data[6] = lp->stats.rx_errors;
			stats.data[7] = lp->stats.rx_fifo_errors;
			stats.data[8] = lp->stats.rx_crc_errors;
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
	struct net_local *lp = (struct net_local *) netdev_priv(dev);

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
	u32 threshold, timer;
	XLlDma_BdRing *RingPtr;
	u32 *dma_int_mask_ptr;

	switch (cmd) {
	case SIOCETHTOOL:
#ifdef USE_TIMER
		/* DEPRECATED */
		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);
#endif

      ret = xenet_do_ethtool_ioctl(dev, rq);
		
#ifdef USE_TIMER
		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
#endif

      return ret;

	case SIOCGMIIPHY:		/* Get address of GMII PHY in use. */
	case SIOCDEVPRIVATE:	/* for binary compat, remove in 2.5 */
		data->phy_id = lp->gmii_addr;
		/* Fall Through */

	case SIOCGMIIREG:			/* Read GMII PHY register. */
	case SIOCDEVPRIVATE + 1:	/* for binary compat, remove in 2.5 */
	  if (data->phy_id > 31)
			return -ENXIO;

#ifdef USE_TIMER
		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);
#endif

		_XLlTemac_PhyRead(&lp->Emac, data->phy_id, data->reg_num,
				  &data->val_out);

#ifdef USE_TIMER
		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
#endif
		return 0;

	case SIOCSMIIREG:	/* Write GMII PHY register. */
	case SIOCDEVPRIVATE + 2:	/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (data->phy_id > 31)
			return -ENXIO;

#ifdef USE_TIMER
		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);
#endif
		_XLlTemac_PhyWrite(&lp->Emac, data->phy_id, data->reg_num,
				   data->val_in);

#ifdef USE_TIMER
		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
#endif
		return 0;

	case SIOCDEVPRIVATE + 3:	/* set THRESHOLD */
		if (XLlTemac_IsFifo(&lp->Emac))
			return -EFAULT;

		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg)))
			return -EFAULT;

		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr, &threshold, &timer);
		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		if ((ret = XLlDma_BdRingSetCoalesce(RingPtr, thr_arg.threshold,
						    timer)) != XST_SUCCESS) {
			return -EIO;
		}
		return 0;

	case SIOCDEVPRIVATE + 4:	/* set WAITBOUND */
		if (!(XLlTemac_IsDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg)))
			return -EFAULT;

		if (wbnd_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr, &threshold, &timer);
		if (wbnd_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
			dma_int_mask_ptr = &dma_tx_int_mask;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
			dma_int_mask_ptr = &dma_rx_int_mask;
		}
		if (wbnd_arg.waitbound == 0) {
			wbnd_arg.waitbound = 1;
			*dma_int_mask_ptr = XLLDMA_CR_IRQ_ALL_EN_MASK & ~XLLDMA_CR_IRQ_DELAY_EN_MASK;
		}
		if ((ret = XLlDma_BdRingSetCoalesce(RingPtr, threshold,
					wbnd_arg.waitbound)) != XST_SUCCESS) {
			return -EIO;
		}
		XLlDma_mBdRingIntEnable(RingPtr, *dma_int_mask_ptr);

		return 0;

	case SIOCDEVPRIVATE + 5:	/* get THRESHOLD */
		if (!(XLlTemac_IsDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg)))
			return -EFAULT;

		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr,
				(u32 *) &(thr_arg.threshold), &timer);
		if (copy_to_user(rq->ifr_data, &thr_arg, sizeof(thr_arg))) {
			return -EFAULT;
		}
		return 0;

	case SIOCDEVPRIVATE + 6:	/* get WAITBOUND */
		if (!(XLlTemac_IsDma(&lp->Emac)))
			return -EFAULT;

		if (copy_from_user(&wbnd_arg, rq->ifr_data, sizeof(wbnd_arg))) {
			return -EFAULT;
		}
		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		} else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr, &threshold,
					 (u32 *) &(wbnd_arg.waitbound));
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

		if (XLlTemac_IsDma(&lp->Emac) && (lp->desc_space))
			free_descriptor_skb(ndev);

		iounmap((void *) (lp->Emac.Config.BaseAddress));
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
	u16 phy_reg;
	u32 phy_addr;

	for (phy_addr = 31; phy_addr > 0; phy_addr--) {
		_XLlTemac_PhyRead(&lp->Emac, phy_addr, PHY_DETECT_REG, &phy_reg);

		if ((phy_reg != 0xFFFF) &&
		    ((phy_reg & PHY_DETECT_MASK) == PHY_DETECT_MASK)) {
			/* Found a valid PHY address */
			printk(KERN_INFO "XTemac: PHY detected at address %d.\n", phy_addr);
			return phy_addr;
		}
	}

	printk(KERN_WARNING "XTemac: No PHY detected.  Assuming a PHY at address 0\n");
	return 0;		/* default to zero */
}

static struct net_device_ops xilinx_netdev_ops;

/* From include/linux/ethtool.h */
static struct ethtool_ops ethtool_ops = {
	.get_settings = xenet_ethtool_get_settings,
	.set_settings = xenet_ethtool_set_settings,
	.get_drvinfo  = xenet_ethtool_get_drvinfo,
	.get_regs_len = xenet_ethtool_get_regs_len,
	.get_regs     = xenet_ethtool_get_regs,
	.get_coalesce = xenet_ethtool_get_coalesce,
	.set_coalesce = xenet_ethtool_set_coalesce,
	.get_ringparam  = xenet_ethtool_get_ringparam,
	.get_pauseparam = xenet_ethtool_get_pauseparam,
	.set_pauseparam = xenet_ethtool_set_pauseparam,
#if 0
	.get_rx_csum  = xenet_ethtool_get_rx_csum,
	.set_rx_csum  = xenet_ethtool_set_rx_csum,
	.get_tx_csum  = xenet_ethtool_get_tx_csum,
	.set_tx_csum  = xenet_ethtool_set_tx_csum,
	.get_sg       = xenet_ethtool_get_sg,
	.set_sg       = xenet_ethtool_set_sg,
#endif
	.get_strings  = xenet_ethtool_get_strings,
	.get_ethtool_stats = xenet_ethtool_get_ethtool_stats,
	.get_sset_count    = xenet_ethtool_get_sset_count,
};


/** Shared device initialization code */
static int xtenet_setup(
		struct device *dev,
		struct resource *r_mem,
		struct resource *r_irq,
		struct xlltemac_platform_data *pdata) {

	int xs;
	u32 virt_baddr;		/* virtual base address of TEMAC */

	XLlTemac_Config Temac_Config;

	struct net_device *ndev = NULL;
	struct net_local *lp = NULL;

	int rc = 0;

#ifndef CONFIG_XILINX_LL_TEMAC_EXT
	char *ext_mode = "";
#else
	char *ext_mode = "(extended multicast)";
#endif

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev) {
		dev_err(dev, "Could not allocate net device.\n");
		rc = -ENOMEM;
		goto error;
	}
	dev_set_drvdata(dev, ndev);

	/* the following is needed starting in 2.6.30 as the dma_ops now require
	   the device to be used in the dma calls 
	*/
	SET_NETDEV_DEV(ndev, dev);

	ndev->irq = r_irq->start;

	/* Initialize the private data used by XEmac_LookupConfig().
	 * The private data are zeroed out by alloc_etherdev() already.
	 */
	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dma_irq_r = pdata->ll_dev_dma_rx_irq;
	lp->dma_irq_s = pdata->ll_dev_dma_tx_irq;
	lp->fifo_irq = pdata->ll_dev_fifo_irq;

	/* Setup the Config structure for the XLlTemac_CfgInitialize() call. */
	Temac_Config.BaseAddress = r_mem->start;
#if 0
	Config.RxPktFifoDepth = pdata->rx_pkt_fifo_depth;
	Config.TxPktFifoDepth = pdata->tx_pkt_fifo_depth;
	Config.MacFifoDepth = pdata->mac_fifo_depth;
	Config.IpIfDmaConfig = pdata->dma_mode;
#endif
	Temac_Config.TxCsum = pdata->tx_csum;
	Temac_Config.RxCsum = pdata->rx_csum;
	Temac_Config.LLDevType = pdata->ll_dev_type;
	Temac_Config.LLDevBaseAddress = pdata->ll_dev_baseaddress;
	Temac_Config.PhyType = pdata->phy_type;

	/* Get the virtual base address for the device */
	virt_baddr = (u32) ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (0 == virt_baddr) {
		dev_err(dev, "XLlTemac: Could not allocate iomem.\n");
		rc = -EIO;
		goto error;
	}

	if (XLlTemac_CfgInitialize(&lp->Emac, &Temac_Config, virt_baddr) !=
	    XST_SUCCESS) {
		dev_err(dev, "XLlTemac: Could not initialize device.\n");

		rc = -ENODEV;
		goto error;
	}

	/* Set the MAC address from platform data */
        memcpy(ndev->dev_addr, pdata->mac_addr, 6);

	if (_XLlTemac_SetMacAddress(&lp->Emac, ndev->dev_addr) != XST_SUCCESS) {
		/* should not fail right after an initialize */
		dev_err(dev, "XLlTemac: could not set MAC address.\n");
		rc = -EIO;
		goto error;
	}

	dev_info(dev,
			"MAC address is now %2x:%2x:%2x:%2x:%2x:%2x\n",
			pdata->mac_addr[0], pdata->mac_addr[1],
			pdata->mac_addr[2], pdata->mac_addr[3],
			pdata->mac_addr[4], pdata->mac_addr[5]);

	if (ndev->mtu > XTE_JUMBO_MTU)
		ndev->mtu = XTE_JUMBO_MTU;

	lp->frame_size = ndev->mtu + XTE_HDR_SIZE + XTE_TRL_SIZE;

	if (XLlTemac_IsDma(&lp->Emac)) {
		int result;

		dev_err(dev, "XLlTemac: using DMA mode.\n");

		if (pdata->dcr_host) {
			printk("XLlTemac: DCR address: 0x%0x\n", pdata->ll_dev_baseaddress);
			XLlDma_Initialize(&lp->Dma, pdata->ll_dev_baseaddress);
		} else {
		        virt_baddr = (u32) ioremap(pdata->ll_dev_baseaddress, 4096);
			lp->virt_dma_addr = virt_baddr;
			if (0 == virt_baddr) {
			        dev_err(dev,
					"XLlTemac: Could not allocate iomem for local link connected device.\n");
				rc = -EIO;
				goto error;
			}
			printk("XLlTemac: Dma base address: phy: 0x%x, virt: 0x%x\n", pdata->ll_dev_baseaddress, virt_baddr);
			XLlDma_Initialize(&lp->Dma, virt_baddr);
		}

		xilinx_netdev_ops.ndo_start_xmit = xenet_DmaSend;

		result = descriptor_init(ndev);
		if (result) {
			rc = -EIO;
			goto error;
		}

		/* set the packet threshold and wait bound for both TX/RX directions */
		if (DFT_TX_WAITBOUND == 0) {
			dma_tx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK & ~XLLDMA_CR_IRQ_DELAY_EN_MASK;
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing, DFT_TX_THRESHOLD, 1);
		} else {
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing, DFT_TX_THRESHOLD, DFT_TX_WAITBOUND);
		}
		if (xs != XST_SUCCESS) {
			dev_err(dev,
			       "XLlTemac: could not set SEND pkt threshold/waitbound, ERROR %d",
			       xs);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);

		if (DFT_RX_WAITBOUND == 0) {
			dma_rx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK & ~XLLDMA_CR_IRQ_DELAY_EN_MASK;
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing, DFT_RX_THRESHOLD, 1);
		} else {
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing, DFT_RX_THRESHOLD, DFT_RX_WAITBOUND);
		}
		if (xs != XST_SUCCESS) {
			dev_err(dev,
			       "XLlTemac: Could not set RECV pkt threshold/waitbound ERROR %d",
			       xs);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
	}
	else {
		printk(KERN_INFO
		       "XLlTemac: using FIFO direct interrupt driven mode.\n");

		virt_baddr = (u32) ioremap(pdata->ll_dev_baseaddress, 4096);
		if (0 == virt_baddr) {
			dev_err(dev,
			       "XLlTemac: Could not allocate iomem for local link connected device.\n");
			rc = -EIO;
			goto error;
		}
		printk("XLlTemac: Fifo base address: 0x%0x\n", virt_baddr);
		XLlFifo_Initialize(&lp->Fifo, virt_baddr);

		xilinx_netdev_ops.ndo_start_xmit = xenet_FifoSend;
	}

	/** Scan to find the PHY */
	lp->gmii_addr = detect_phy(lp, ndev->name);


	/* initialize the netdev structure */

	lp->cur_speed   = -1; 
	lp->cur_autoneg = -1;
	lp->cur_pause   = -1;


	ndev->netdev_ops = &xilinx_netdev_ops;

	if (XLlTemac_IsDma(&lp->Emac)) {
		ndev->features = NETIF_F_SG | NETIF_F_FRAGLIST;

		if (XLlTemac_IsTxCsum(&lp->Emac) == TRUE) {
			/*
			 * This hardware only supports proper checksum calculations
			 * on TCP/UDP packets.
			 */
			ndev->features |= NETIF_F_IP_CSUM;
		}
		if (XLlTemac_IsRxCsum(&lp->Emac) == TRUE) {
			lp->local_features |= LOCAL_FEATURE_RX_CSUM;
		}
	}

	ndev->watchdog_timeo = TX_TIMEOUT;

	/* init the stats */
	lp->max_frags_in_a_packet = 0;
	lp->tx_hw_csums = 0;
	lp->rx_hw_csums = 0;

#if ! XTE_AUTOSTRIPPING
	lp->stripping =
		(XLlTemac_GetOptions(&(lp->Emac)) & XTE_FCS_STRIP_OPTION) != 0;
#endif
	/* Set ethtool IOCTL handler vectors.
	 * xenet_do_ethtool_ioctl() is deprecated.
	 */
	netdev_set_default_ethtool_ops(ndev, &ethtool_ops);

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(dev,
		       "%s: Cannot register net device, aborting.\n",
		       ndev->name);
		goto error;	/* rc is already set here... */
	}

	dev_info(dev,
		"%s: Xilinx TEMAC at 0x%08X mapped to 0x%08X, irq=%d %s\n",
		ndev->name,
		(unsigned int)r_mem->start,
		lp->Emac.Config.BaseAddress,
		 ndev->irq, ext_mode);

	return 0;

error:
	if (ndev) {
		xtenet_remove_ndev(ndev);
	}
	return rc;
}

int xenet_set_mac_address(struct net_device *ndev, void* address) { 
	struct net_local *lp; 
	struct sockaddr *macaddr; 

	if (ndev->flags & IFF_UP) 
		return -EBUSY; 

	lp = netdev_priv(ndev); 

	macaddr = (struct sockaddr*)address; 
 
	if (!is_valid_ether_addr(macaddr->sa_data)) 
		return -EADDRNOTAVAIL; 
 
	/* synchronized against open : rtnl_lock() held by caller */ 
	memcpy(ndev->dev_addr, macaddr->sa_data, ETH_ALEN); 
 
	if (!is_valid_ether_addr(ndev->dev_addr)) 
		return -EADDRNOTAVAIL; 
 
	if (_XLlTemac_SetMacAddress(&lp->Emac, ndev->dev_addr) != XST_SUCCESS) { 
		/* should not fail right after an initialize */ 
		dev_err(&ndev->dev, "XLlTemac: could not set MAC address.\n"); 
		return -EIO; 
	} 
	dev_info(&ndev->dev, 
		"MAC address is now %02x:%02x:%02x:%02x:%02x:%02x\n", 
		ndev->dev_addr[0], ndev->dev_addr[1], 
		ndev->dev_addr[2], ndev->dev_addr[3], 
		ndev->dev_addr[4], ndev->dev_addr[5]); 

	return 0; 
} 

static u32 get_u32(struct platform_device *ofdev, const char *s) {
	u32 *p = (u32 *)of_get_property(ofdev->dev.of_node, s, NULL);
	if(p) {
		return *p;
	} else {
		dev_warn(&ofdev->dev, "Parameter %s not found, defaulting to false.\n", s);
		return FALSE;
	}
}


static struct net_device_ops xilinx_netdev_ops = {
	.ndo_open 	= xenet_open,
	.ndo_stop	= xenet_close,
	.ndo_start_xmit	= 0,
	.ndo_set_rx_mode= 0,
//	.ndo_set_multicast_list= 0,
	.ndo_do_ioctl	= xenet_ioctl,
	.ndo_change_mtu	= xenet_change_mtu,
	.ndo_tx_timeout	= xenet_tx_timeout,
	.ndo_get_stats	= xenet_get_stats,
	.ndo_set_mac_address = xenet_set_mac_address,
	.ndo_set_rx_mode= xenet_set_multicast_list,
//	.ndo_set_multicast_list	= xenet_set_multicast_list,
};


static struct of_device_id xtenet_fifo_of_match[] = {
	{ .compatible = "xlnx,xps-ll-fifo-1.00.a", },
	{ .compatible = "xlnx,xps-ll-fifo-1.00.b", },
	{ .compatible = "xlnx,xps-ll-fifo-1.01.a", },
	{ .compatible = "xlnx,xps-ll-fifo-1.02.a", },
	{ /* end of list */ },
};

static struct of_device_id xtenet_sdma_of_match[] = {
	{ .compatible = "xlnx,ll-dma-1.00.a", },
	{ /* end of list */ },
};


static int xtenet_of_probe(struct platform_device *ofdev)
{
	struct resource r_irq_struct;
	struct resource r_mem_struct;
	struct resource r_connected_mem_struct;
	struct resource r_connected_irq_struct;
	struct xlltemac_platform_data pdata_struct;

	struct resource *r_irq = &r_irq_struct;	/* Interrupt resources */
	struct resource *r_mem = &r_mem_struct;	/* IO mem resources */
	struct xlltemac_platform_data *pdata = &pdata_struct;
	const void *mac_address;
	int rc = 0;
	const phandle *llink_connected_handle;
	struct device_node *llink_connected_node;
	u32 *dcrreg_property;
	void *bram_area = NULL;

	printk(KERN_INFO "Device Tree Probing \'%s\'\n",ofdev->dev.of_node->name); 

	/* Get iospace for the device */
	rc = of_address_to_resource(ofdev->dev.of_node, 0, r_mem);
	if(rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		return rc;
	}

	/* Get IRQ for the device */
	rc = of_irq_to_resource(ofdev->dev.of_node, 0, r_irq);
	if(rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found.\n");
		return rc;
	}

	pdata_struct.tx_csum	= get_u32(ofdev, "xlnx,txcsum");
	pdata_struct.rx_csum	= get_u32(ofdev, "xlnx,rxcsum");
	pdata_struct.phy_type	= get_u32(ofdev, "xlnx,phy-type");
	llink_connected_handle 	= of_get_property(ofdev->dev.of_node, "llink-connected", NULL);
	if(!llink_connected_handle) {
		dev_warn(&ofdev->dev, "no Locallink connection found.\n");
		return rc;
	}

	llink_connected_node = of_find_node_by_phandle(*llink_connected_handle);
	rc = of_address_to_resource(
			llink_connected_node,
			0,
			&r_connected_mem_struct);

        /** Get the right information from whatever the locallink is
	    connected to. */
	if(of_match_node(xtenet_fifo_of_match, llink_connected_node)) {

		/** Connected to a fifo. */
		if(rc) {
			dev_warn(&ofdev->dev, "invalid address\n");
			return rc;
		}

		pdata_struct.ll_dev_baseaddress	= r_connected_mem_struct.start;
		pdata_struct.ll_dev_type = XPAR_LL_FIFO;
		pdata_struct.ll_dev_dma_rx_irq	= NO_IRQ;
		pdata_struct.ll_dev_dma_tx_irq	= NO_IRQ;

		rc = of_irq_to_resource(
				llink_connected_node,
				0,
				&r_connected_irq_struct);
		if(rc == NO_IRQ) {
			dev_warn(&ofdev->dev, "no IRQ found.\n");
			return rc;
		}
		pdata_struct.ll_dev_fifo_irq	= r_connected_irq_struct.start;
		pdata_struct.dcr_host = 0x0;
        } else if(of_match_node(xtenet_sdma_of_match, llink_connected_node)) {

		/** Connected to a dma port, default to 405 type dma */
		pdata->dcr_host = 0;
		if(rc) {
			/* no address was found, might be 440, check for dcr reg */

			dcrreg_property = (u32 *)of_get_property(llink_connected_node, "dcr-reg", 									NULL);
			if(dcrreg_property) {
			        r_connected_mem_struct.start = *dcrreg_property;
				pdata->dcr_host = 0xFF;
			} else {
				dev_warn(&ofdev->dev, "invalid address\n");
				return rc;
			}			
		}

        	pdata_struct.ll_dev_baseaddress	= r_connected_mem_struct.start;
		pdata_struct.ll_dev_type = XPAR_LL_DMA;

		rc = of_irq_to_resource(
				llink_connected_node,
				0,
				&r_connected_irq_struct);
		if(rc == NO_IRQ) {
			dev_warn(&ofdev->dev, "First IRQ not found.\n");
			return rc;
		}
		pdata_struct.ll_dev_dma_rx_irq	= r_connected_irq_struct.start;

		rc = of_irq_to_resource(
				llink_connected_node,
				1,
				&r_connected_irq_struct);

		if(rc == NO_IRQ) {
			dev_warn(&ofdev->dev, "Second IRQ not found.\n");
			return rc;
		}

		pdata_struct.ll_dev_dma_tx_irq	= r_connected_irq_struct.start;

		pdata_struct.ll_dev_fifo_irq	= NO_IRQ;
        } else {
		dev_warn(&ofdev->dev, "Locallink connection not matched.\n");
		return rc;
        }

    /* ZDS: modification for BRAM access */
#ifdef CONFIG_XILINX_LLTEMAC_AUTO_NEG_GPIO
	    dev_info(&ofdev->dev, "Using internal GPIO to report autonegotiation status\n"); 
#else
	    dev_info(&ofdev->dev, "Not using internal GPIO to report autonegotiation status\n"); 
#endif

#if 0
	of_node_put(llink_connected_node);
        mac_address = of_get_mac_address(ofdev->dev.of_node);
        if(mac_address) {
            memcpy(pdata_struct.mac_addr, mac_address, 6);
        } else {
            dev_warn(&ofdev->dev, "No MAC address found.\n");
        }
#else
	mac_address = kmalloc(6*sizeof(unsigned char),GFP_KERNEL);
	if(!mac_address)
	{
		printk(KERN_INFO "Error allocation memory : mac_address\n"); 
		return -1;
	}

        bram_kernel_access(&bram_area);
        if(bram_area != NULL)
        {
            ((unsigned char *)mac_address)[0] = (unsigned char) ((in_be32(bram_area    ) & 0x00FF0000) >> 16);  
            ((unsigned char *)mac_address)[1] = (unsigned char) ((in_be32(bram_area    ) & 0xFF000000) >> 24);  
            ((unsigned char *)mac_address)[2] = (unsigned char) ((in_be32(bram_area + 1) & 0x000000FF) >> 0 );  
            ((unsigned char *)mac_address)[3] = (unsigned char) ((in_be32(bram_area + 1) & 0x0000FF00) >> 8 );  
            ((unsigned char *)mac_address)[4] = (unsigned char) ((in_be32(bram_area + 1) & 0x00FF0000) >> 16);  
            ((unsigned char *)mac_address)[5] = (unsigned char) ((in_be32(bram_area + 1) & 0xFF000000) >> 24);  

	memcpy(pdata_struct.mac_addr, mac_address, 6);
        dev_info(&ofdev->dev, 
		"MAC address retrieved through BRAM is %02x:%02x:%02x:%02x:%02x:%02x\n", 
		((unsigned char *)mac_address)[0], ((unsigned char *)mac_address)[1], 
		((unsigned char *)mac_address)[2], ((unsigned char *)mac_address)[3], 
		((unsigned char *)mac_address)[4], ((unsigned char *)mac_address)[5]); 
        }
        else
        {
	        dev_warn(&ofdev->dev, "Unable to access BRAM! No MAC address found.\n");
        }

#endif

	kfree(mac_address);

        return xtenet_setup(&ofdev->dev, r_mem, r_irq, pdata);
}


static int xtenet_of_remove(struct platform_device *dev)
{
	return xtenet_remove(&dev->dev);
}

static struct of_device_id xtenet_of_match[] = {
	{ .compatible = "xlnx,xps-ll-temac-1.00.a", },
	{ .compatible = "xlnx,xps-ll-temac-1.00.b", },
	{ .compatible = "xlnx,xps-ll-temac-1.01.a", },
	{ .compatible = "xlnx,xps-ll-temac-1.01.b", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xtenet_of_match);


static struct platform_driver xtenet_of_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = xtenet_of_match,
	},
	.probe		= xtenet_of_probe,
	.remove		= xtenet_of_remove,
};

static int __init xtenet_init(void)
{
	/*
	 * Make sure the locks are initialized
	 */
	spin_lock_init(&XTE_spinlock);
	spin_lock_init(&XTE_tx_spinlock);
	spin_lock_init(&XTE_rx_spinlock);

	INIT_LIST_HEAD(&sentQueue);
	INIT_LIST_HEAD(&receivedQueue);

	spin_lock_init(&sentQueueSpin);
	spin_lock_init(&receivedQueueSpin);

	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	return platform_driver_register(&xtenet_of_driver);
}

static void __exit xtenet_cleanup(void)
{
	platform_driver_unregister(&xtenet_of_driver);
}

module_init(xtenet_init);
module_exit(xtenet_cleanup);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
