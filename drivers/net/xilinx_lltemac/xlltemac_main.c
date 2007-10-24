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

/*
 * With the way the hardened Temac works, the driver needs to communicate
 * with the PHY controller. Since each board will have a different
 * type of PHY, the code that communicates with the MII type controller
 * is inside #ifdef XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT conditional
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

#ifdef CONFIG_OF
// For open firmware.
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif

#include "xbasic_types.h"
#include "xlltemac.h"
#include "xllfifo.h"
#include "xlldma.h"
#include "xlldma_bdring.h"

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

#define ALIGNMENT_RECV          32
#define ALIGNMENT_SEND          8
#define ALIGNMENT_SEND_PERF     32

#define XTE_SEND  1
#define XTE_RECV  2

/* SGDMA buffer descriptors must be aligned on a 8-byte boundary. */
#define ALIGNMENT_BD            XLLDMA_BD_MINIMUM_ALIGNMENT

/* BUFFER_ALIGN(adr) calculates the number of bytes to the next alignment. */
#define BUFFER_ALIGNSEND(adr) ((ALIGNMENT_SEND - ((u32) adr)) % ALIGNMENT_SEND)
#define BUFFER_ALIGNSEND_PERF(adr) ((ALIGNMENT_SEND_PERF - ((u32) adr)) % ALIGNMENT_SEND_PERF)
#define BUFFER_ALIGNRECV(adr) ((ALIGNMENT_RECV - ((u32) adr)) % ALIGNMENT_RECV)

/* Default TX/RX Threshold and waitbound values for SGDMA mode */
//#define DFT_TX_THRESHOLD  16
#define DFT_TX_THRESHOLD  1
#define DFT_TX_WAITBOUND  1
//#define DFT_RX_THRESHOLD  2
#define DFT_RX_THRESHOLD  1
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
 * Checksum offload macros
 */
#define BdCsumEnable(BdPtr) \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET,             \
		(XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)) | 1 )

#define BdCsumDisable(BdPtr) \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET,             \
		(XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)) & 0xFFFFFFFE )

#define BdCsumSetup(BdPtr, Start, Insert) \
    XLlDma_mBdWrite((BdPtr), XLLDMA_BD_USR1_OFFSET, (Start) << 16 | (Insert))

#define BdCsumSeed(BdPtr, Seed) \
    XLlDma_mBdWrite((BdPtr), XLLDMA_BD_USR2_OFFSET, 0)

#define BdCsumGet(BdPtr) \
    XLlDma_mBdRead((BdPtr), XLLDMA_BD_USR3_OFFSET)

#define BdGetRxLen(BdPtr) \
    XLlDma_mBdRead((BdPtr), XLLDMA_BD_USR4_OFFSET)

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

u32 dma_rx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK;
u32 dma_tx_int_mask = XLLDMA_CR_IRQ_ALL_EN_MASK;

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
static spinlock_t receivedQueueSpin = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(sentQueue);
static spinlock_t sentQueueSpin = SPIN_LOCK_UNLOCKED;


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
 * Wrap certain temac routines with a lock, so access to the shared hard temac
 * interface is accessed mutually exclusive for dual channel temac support.
 */

static inline void _XLlTemac_Start(XLlTemac * InstancePtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Start(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_Stop(XLlTemac * InstancePtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Stop(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_Reset(XLlTemac * InstancePtr, int HardCoreAction)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_Reset(InstancePtr, HardCoreAction);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_SetMacAddress(XLlTemac * InstancePtr,
					  void *AddressPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetMacAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline void _XLlTemac_GetMacAddress(XLlTemac * InstancePtr,
					   void *AddressPtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_GetMacAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_SetOptions(XLlTemac * InstancePtr, u32 Options)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetOptions(InstancePtr, Options);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_ClearOptions(XLlTemac * InstancePtr, u32 Options)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_ClearOptions(InstancePtr, Options);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline u16 _XLlTemac_GetOperatingSpeed(XLlTemac * InstancePtr)
{
	u16 speed;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	speed = XLlTemac_GetOperatingSpeed(InstancePtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return speed;
}

static inline void _XLlTemac_SetOperatingSpeed(XLlTemac * InstancePtr,
					       u16 Speed)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_SetOperatingSpeed(InstancePtr, Speed);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_PhySetMdioDivisor(XLlTemac * InstancePtr,
					       u8 Divisor)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_PhySetMdioDivisor(InstancePtr, Divisor);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_PhyRead(XLlTemac * InstancePtr, u32 PhyAddress,
				     u32 RegisterNum, u16 *PhyDataPtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_PhyRead(InstancePtr, PhyAddress, RegisterNum, PhyDataPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline void _XLlTemac_PhyWrite(XLlTemac * InstancePtr, u32 PhyAddress,
				      u32 RegisterNum, u16 PhyData)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_PhyWrite(InstancePtr, PhyAddress, RegisterNum, PhyData);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}


static inline int _XLlTemac_MulticastClear(XLlTemac * InstancePtr, int Entry)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_MulticastClear(InstancePtr, Entry);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_SetMacPauseAddress(XLlTemac * InstancePtr,
					       void *AddressPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_SetMacPauseAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline void _XLlTemac_GetMacPauseAddress(XLlTemac * InstancePtr,
						void *AddressPtr)
{
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	XLlTemac_GetMacPauseAddress(InstancePtr, AddressPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}

static inline int _XLlTemac_GetSgmiiStatus(XLlTemac * InstancePtr,
					   u16 *SpeedPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_GetSgmiiStatus(InstancePtr, SpeedPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}

static inline int _XLlTemac_GetRgmiiStatus(XLlTemac * InstancePtr,
					   u16 *SpeedPtr,
					   int *IsFullDuplexPtr,
					   int *IsLinkUpPtr)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&XTE_spinlock, flags);
	status = XLlTemac_GetRgmiiStatus(InstancePtr, SpeedPtr, IsFullDuplexPtr,
					 IsLinkUpPtr);
	spin_unlock_irqrestore(&XTE_spinlock, flags);

	return status;
}



#define PHYSETUP
#define ML405_PHY_MARVELL_88E1111_GMII_100
// #define ML410_PHY_MARVELL_88E1111_RGMII

#ifdef ML410_PHY_MARVELL_88E1111_RGMII

#  define C_RCW0_RD          0x00000200UL	/* hard TEMAC Read Config Wd 0 read */
#  define C_RCW0_WR          0x00008200UL	/* hard TEMAC Read Config Wd 0 write */
#  define C_RCW1_RD          0x00000240UL	/* hard TEMAC Read Config Wd 1 read */
#  define C_RCW1_WR          0x00008240UL	/* hard TEMAC Read Config Wd 1 write */
#  define C_TC_RD            0x00000280UL	/* hard TEMAC Transmit Config read */
#  define C_TC_WR            0x00008280UL	/* hard TEMAC Transmit Config write */
#  define C_FCC_RD           0x000002C0UL	/* hard TEMAC Flow Control Config read */
#  define C_FCC_WR           0x000082C0UL	/* hard TEMAC Flow Control Config write */
#  define C_EMMC_RD          0x00000300UL	/* hard TEMAC Ethernet MAC Mode Config read */
#  define C_EMMC_WR          0x00008300UL	/* hard TEMAC Ethernet MAC Mode Config write */
#  define C_PHYC_RD          0x00000320UL	/* hard TEMAC RGMII/SGMII Config read */
#  define C_MC_RD            0x00000340UL	/* hard TEMAC Management Config read */
#  define C_MC_WR            0x00008340UL	/* hard TEMAC Management Config write */
#  define C_UAW0_RD          0x00000380UL	/* hard TEMAC Unicast Addr Word 0 read */
#  define C_UAW0_WR          0x00008380UL	/* hard TEMAC Unicast Addr Word 0 write */
#  define C_UAW1_RD          0x00000384UL	/* hard TEMAC Unicast Addr Word 1 read */
#  define C_UAW1_WR          0x00008384UL	/* hard TEMAC Unicast Addr Word 1 write */
#  define C_MAW0_RD          0x00000388UL	/* hard TEMAC Multicast Addr Word 0 read */
#  define C_MAW0_WR          0x00008388UL	/* hard TEMAC Multicast Addr Word 0 write */
#  define C_MAW1_RD          0x0000038CUL	/* hard TEMAC Multicast Addr Word 1 read */
#  define C_MAW1_WR          0x0000838CUL	/* hard TEMAC Multicast Addr Word 1 write */
#  define C_AFM_RD           0x00000390UL	/* hard TEMAC Address Filter Mode read */
#  define C_AFM_WR           0x00008390UL	/* hard TEMAC Address Filter Mode write */
#  define C_IS_RD            0x000003A0UL	/* hard TEMAC Interrupt Status read */
#  define C_IS_WR            0x000083A0UL	/* hard TEMAC Interrupt Status write */
#  define C_IE_RD            0x000003A0UL	/* hard TEMAC Interrupt Enable read */
#  define C_IE_WR            0x000083A0UL	/* hard TEMAC Interrupt Enable write */
#  define C_MIIMWD_RD        0x000003B0UL	/* hard TEMAC Management Write Data Reg read */
#  define C_MIIMWD_WR        0x000083B0UL	/* hard TEMAC Management Write Data Reg write */
#  define C_MIIMAI_RD        0x000003B4UL	/* hard TEMAC Management Access Initiate Reg read */
#  define C_MIIMAI_WR        0x000083B4UL	/* hard TEMAC Management Access Initiate Reg write */

#  define C_MAW_RD_EN        0x00800000UL	/* Multicast Addr Table Read  Enable for MAW1 */
#  define C_MAW_WR_EN        0x00000000UL	/* Multicast Addr Table Write Enable for MAW1 */

#  define C_MAW_ADDR0        0x00000000UL	/* Multicast Addr Table entry 0 for MAW1 */
#  define C_MAW_ADDR1        0x00010000UL	/* Multicast Addr Table entry 1 for MAW1 */
#  define C_MAW_ADDR2        0x00020000UL	/* Multicast Addr Table entry 2 for MAW1 */
#  define C_MAW_ADDR3        0x00030000UL	/* Multicast Addr Table entry 3 for MAW1 */


#endif /* #ifdef ML410_PHY_MARVELL_88E1111_RGMII */

#define DEBUG_ERROR KERN_ERR
#define DEBUG_LOG(level, ...) printk(level __VA_ARGS__)

#ifdef PHYSETUP
static void PhySetup(XLlTemac * Mac, u32 Speed)
{
	u32 MacBaseAddr = Mac->Config.BaseAddress;
	unsigned long flags;

#ifdef ML410_PHY_MARVELL_88E1111_RGMII
	u32 Register;
	u32 PhyAddr, RegAddr;
#endif

	spin_lock_irqsave(&XTE_spinlock, flags);
	/* Validate the input argument(s) */
	if ((Speed != 10) && (Speed != 100) && (Speed != 1000)) {
		DEBUG_LOG(DEBUG_ERROR,
			  "PhySetup() received an invalid speed value.\n");
	}

#ifdef ML410_PHY_MARVELL_88E1111_RGMII

    /**************************************************************************
     * - Determine if ML410 board is a RevC (PhyAddr = 0) or RevD (PhyAddr = 7)
     * - ML405 is PhyAddr = 7 by trying to read PHY ID Register (Mdio_Reg = 2)
     *************************************************************************/

	/* PHY address 0 & Register address 2 */
	PhyAddr = 0;
	RegAddr = 2;

	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
			  ((PhyAddr << 5) | RegAddr));
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

	/* Loop until "Ready" signal is set */
	do {
		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
	} while (Register != 0x0001007F);

	/* Decide PHY address from Least significant word data register value */
	Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
	if (Register == 0x141) {
		PhyAddr = 0;
	}
	else {
		PhyAddr = 7;
	}

#ifdef XTEA_PRINT_BUF_INIT_DATA
	printf("lltemac: PHY address = %d\n", PhyAddr);
#endif

    /**************************************************************************
     * -- Set up MAC interface
     * -- Write to the mgtdr to disable line loopback, enable link pulses
     * -- Set up downshift counter, set mac interface to 100 Mbps, 25, 25 MHz
     * -- add delay to rx_clk but not tx_clk
     *************************************************************************/

	/* Register address 20 */
	RegAddr = 20;

	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET, 0x00000cc3);
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMWD_WR);
	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
			  ((PhyAddr << 5) | RegAddr));
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_WR);

	/* Loop until "Ready" signal is set */
	do {
		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
	} while (Register != 0x0001007F);

	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
			  ((PhyAddr << 5) | RegAddr));
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

	/* Loop until "Ready" signal is set */
	do {
		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
	} while (Register != 0x0001007F);

	Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
	if (Register != 0x00000cc3) {
		DEBUG_LOG(DEBUG_ERROR,
			  "PhySetup(): PHY register %d = 0x%x Expected = 0x00000cc3\n",
			  RegAddr, Register);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return;
	}

    /**************************************************************************
     * -- Set RGMII to copper with correct hysterisis and correct mode
     * -- Disable fiber/copper auto sel, choose copper
     * -- RGMII /Modified MII to copper mode
     *************************************************************************/

	/* Register address 27 */
	RegAddr = 27;

	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET, 0x0000848b);
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMWD_WR);
	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
			  ((PhyAddr << 5) | RegAddr));
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_WR);

	/* Loop until "Ready" signal is set */
	do {
		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
	} while (Register != 0x0001007F);

	XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
			  ((PhyAddr << 5) | RegAddr));
	XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

	/* Loop until "Ready" signal is set */
	do {
		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
	} while (Register != 0x0001007F);

	Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
	if (Register != 0x0000848b) {
		DEBUG_LOG(DEBUG_ERROR,
			  "PhySetup(): PHY register %d = 0x%x Expected = 0x0000848b\n",
			  RegAddr, Register);
		spin_unlock_irqrestore(&XTE_spinlock, flags);
		return;
	}

    /**************************************************************************
     * -- Reset the PHY: Turn off auto neg and force to given speed
     *************************************************************************/

	/* Register address 0 */
	RegAddr = 0;

	if (Speed == 10) {
		/* Write to force 10 Mbs full duplex  no autoneg and perform a reset */
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET, 0x00008100);
		/* Reset required after changing auto neg, speed or duplex */
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMWD_WR);
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_WR);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
		if (Register != 0x00000100) {
			DEBUG_LOG(DEBUG_ERROR,
				  "PhySetup(): LSW  = 0x%x Expected = 0x00000100\n",
				  Register);
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return;
		}
	}
	else if (Speed == 100) {
		/* Force 100 Mbs full duplex. no autoneg and perform a reset */
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET, 0x0000A100);
		/* reset required after changing auto neg, speed or duplex */
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMWD_WR);
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_WR);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
		if (Register != 0x00002100) {
			DEBUG_LOG(DEBUG_ERROR,
				  "PhySetup(): LSW  = 0x%x Expected = 0x00002100\n",
				  Register);
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return;
		}
	}
	else if (Speed == 1000) {
		/* Force 1000 Mbs full duplex. no autoneg and perform a reset */
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET, 0x00008140);
		/* reset required after changing auto neg, speed or duplex */
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMWD_WR);
		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_WR);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		XLlTemac_WriteReg(MacBaseAddr, XTE_LSW_OFFSET,
				  ((PhyAddr << 5) | RegAddr));
		XLlTemac_WriteReg(MacBaseAddr, XTE_CTL_OFFSET, C_MIIMAI_RD);

		/* Loop until "Ready" signal is set */
		do {
			Register =
				XLlTemac_ReadReg(MacBaseAddr, XTE_RDY_OFFSET);
		} while (Register != 0x0001007F);

		Register = XLlTemac_ReadReg(MacBaseAddr, XTE_LSW_OFFSET);
		if (Register != 0x00000140) {
			DEBUG_LOG(DEBUG_ERROR,
				  "PhySetup(): LSW  = 0x%x Expected = 0x00000140\n",
				  Register);
			spin_unlock_irqrestore(&XTE_spinlock, flags);
			return;
		}
	}

#endif /* #ifdef ML410_PHY_MARVELL_88E1111_RGMII */

#ifdef ML405_PHY_MARVELL_88E1111_GMII_100
	if (Speed == 100) {
		XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x0000a100);
		XLlTemac_WriteReg(MacBaseAddr, 0x28, 0x000083B0);

#ifdef XPAR_XLLTEMAC_0_DEVICE_ID
		if (Mac->Config.DeviceId == XPAR_XLLTEMAC_0_DEVICE_ID) {
			XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x000000E0);
		}
#endif
#ifdef XPAR_XLLTEMAC_1_DEVICE_ID
		if (Mac->Config.DeviceId == XPAR_XLLTEMAC_1_DEVICE_ID) {
			XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x00000000);
		}
#endif
		XLlTemac_WriteReg(MacBaseAddr, 0x28, 0x000083B4);
	}
	else if (Speed == 10) {
		XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x00008100);
		XLlTemac_WriteReg(MacBaseAddr, 0x28, 0x000083B0);

#ifdef XPAR_XLLTEMAC_0_DEVICE_ID
		if (Mac->Config.DeviceId == XPAR_XLLTEMAC_0_DEVICE_ID) {
			XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x000000E0);
		}
#endif
#ifdef XPAR_XLLTEMAC_1_DEVICE_ID
		if (Mac->Config.DeviceId == XPAR_XLLTEMAC_1_DEVICE_ID) {
			XLlTemac_WriteReg(MacBaseAddr, 0x24, 0x00000000);
		}
#endif
		XLlTemac_WriteReg(MacBaseAddr, 0x28, 0x000083B4);
	}

#endif /* ML405_PHY_MARVELL_88E1111_GMII_100 */
	spin_unlock_irqrestore(&XTE_spinlock, flags);
}
#endif /* PHYSETUP */


typedef enum DUPLEX { UNKNOWN_DUPLEX, HALF_DUPLEX, FULL_DUPLEX } DUPLEX;

int renegotiate_speed(struct net_device *dev, int speed, DUPLEX duplex)
{
	struct net_local *lp = (struct net_local *) dev->priv;
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
		       "%s: XLlTemac: unsupported speed requested: %d\n",
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
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg1);
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &phy_reg1);
	_XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_ADVERTISE, phy_reg4);
	_XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_EXADVERTISE, phy_reg9);

	while (retries--) {
		/* initiate an autonegotiation of the speed */
		_XLlTemac_PhyWrite(&lp->Emac, lp->gmii_addr, MII_BMCR,
				   phy_reg0);

		wait_count = 20;	/* so we don't loop forever */
		while (wait_count--) {
			/* wait a bit for the negotiation to complete */
			mdelay(500);
			_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR,
					  &phy_reg1);
			_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR,
					  &phy_reg1);
			if ((phy_reg1 & BMSR_LSTATUS) &&
			    (phy_reg1 & BMSR_ANEGCAPABLE))
				break;

		}

		if (phy_reg1 & BMSR_LSTATUS) {
			printk(KERN_INFO
			       "%s: XLlTemac: We renegotiated the speed to: %d\n",
			       dev->name, speed);
			return 0;
		}
		else {
			printk(KERN_ERR
			       "%s: XLlTemac: Not able to set the speed to %d (status: 0x%0x)\n",
			       dev->name, speed, phy_reg1);
			return -1;
		}
	}

	printk(KERN_ERR
	       "%s: XLlTemac: Not able to set the speed to %d\n", dev->name,
	       speed);
	return -1;
}

#define XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT
/*
 * This function sets up MAC's speed according to link speed of PHY
 * This function is specific to MARVELL 88E1111 PHY chip on Xilinx ML403
 * board and assumes GMII interface is being used by the TEMAC
 */
void set_mac_speed(struct net_local *lp)
{
	u16 phylinkspeed;
	struct net_device *dev = lp->ndev;
	int ret;

#ifndef XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT
	int retry_count = 1;
#endif

	/*
	 * See comments at top for an explanation of
	 * XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT
	 */
#ifdef XILINX_PLB_TEMAC_3_00A_ML403_PHY_SUPPORT
#define MARVELL_88E1111_PHY_SPECIFIC_STATUS_REG_OFFSET  17
#define MARVELL_88E1111_LINKSPEED_MARK                  0xC000
#define MARVELL_88E1111_LINKSPEED_SHIFT                 14
#define MARVELL_88E1111_LINKSPEED_1000M                 0x0002
#define MARVELL_88E1111_LINKSPEED_100M                  0x0001
#define MARVELL_88E1111_LINKSPEED_10M                   0x0000
	u16 RegValue;

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr,
			MARVELL_88E1111_PHY_SPECIFIC_STATUS_REG_OFFSET,
			&RegValue);
	/* Get current link speed */
	phylinkspeed = (RegValue & MARVELL_88E1111_LINKSPEED_MARK)
		>> MARVELL_88E1111_LINKSPEED_SHIFT;

	/* Update TEMAC speed accordingly */
	switch (phylinkspeed) {
	case (MARVELL_88E1111_LINKSPEED_1000M):
		_XLlTemac_SetOperatingSpeed(&lp->Emac, 1000);
		printk(KERN_INFO "%s: XLlTemac: speed set to 1000Mb/s\n",
		       dev->name);
		lp->cur_speed = 1000;
		break;
	case (MARVELL_88E1111_LINKSPEED_100M):
		_XLlTemac_SetOperatingSpeed(&lp->Emac, 100);
		printk(KERN_INFO "%s: XLlTemac: speed set to 100Mb/s\n",
		       dev->name);
		lp->cur_speed = 100;
		break;
	case (MARVELL_88E1111_LINKSPEED_10M):
		_XLlTemac_SetOperatingSpeed(&lp->Emac, 10);
		printk(KERN_INFO "%s: XLlTemac: speed set to 10Mb/s\n",
		       dev->name);
		lp->cur_speed = 10;
		break;
	default:
		_XLlTemac_SetOperatingSpeed(&lp->Emac, 1000);
		printk(KERN_INFO "%s: XLlTemac: speed set to 1000Mb/s\n",
		       dev->name);
		lp->cur_speed = 1000;
		break;
	}
#ifdef PHYSETUP
	PhySetup(&lp->Emac, lp->cur_speed);
#endif

#else
	if (XLlTemac_GetPhysicalInterface(&lp->Emac) == XTE_PHY_TYPE_MII) {
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
		printk(KERN_INFO "%s: XLlTemac: could not negotiate speed\n",
		       dev->name);
		lp->cur_speed = 0;

		return;
	}

	_XLlTemac_SetOperatingSpeed(&lp->Emac, phylinkspeed);
	printk(KERN_INFO "%s: XLlTemac: speed set to %dMb/s\n", dev->name,
	       phylinkspeed);
	lp->cur_speed = phylinkspeed;
#endif
#ifdef PHYSETUP
	PhySetup(&lp->Emac, lp->cur_speed);
#endif
}

/*
 * Helper function to reset the underlying hardware.  This is called
 * when we get into such deep trouble that we don't know how to handle
 * otherwise.
 */
static void reset(struct net_device *dev, u32 line_num)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 TxThreshold, TxWaitBound, RxThreshold, RxWaitBound;
	u32 Options;
	static u32 reset_cnt = 0;
	int status;

	printk(KERN_INFO
	       "%s: XLlTemac: resets (#%u) from adapter code line %d\n",
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
	}
	else {
		XLlFifo_Reset(&lp->Fifo);
	}

	/* now we can reset the device */
	_XLlTemac_Reset(&lp->Emac, XTE_NORESET_HARD);

	/* Reset on TEMAC also resets PHY. Give it some time to finish negotiation
	 * before we move on */
	mdelay(2000);

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

	set_mac_speed(lp);
#ifdef PHYSETUP
	PhySetup(&lp->Emac, lp->cur_speed);
#endif

	if (XLlTemac_IsDma(&lp->Emac)) {	/* SG DMA mode */
		status = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
						  RxThreshold, RxWaitBound);
		status |= XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
						   TxThreshold, TxWaitBound);
		if (status != XST_SUCCESS) {
			/* Print the error, but keep on going as it's not a fatal error. */
			printk(KERN_ERR
			       "%s: XLlTemac: error setting coalesce values (probably out of range). status: %d\n",
			       dev->name, status);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);
	}
	else {			/* FIFO interrupt mode */
		XLlFifo_IntEnable(&lp->Fifo,
				  XLLF_INT_TC_MASK | XLLF_INT_RC_MASK);
	}
	XLlTemac_IntEnable(&lp->Emac, XTE_INT_RXRJECT_MASK);

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
 * The PHY registers read here should be standard registers in all PHY chips
 */
static int get_phy_status(struct net_device *dev, DUPLEX * duplex, int *linkup)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u16 reg;

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &reg);
	*duplex = FULL_DUPLEX;

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &reg);
	*linkup = (reg & BMSR_LSTATUS) != 0;

	return 0;
}

/*
 * This routine is used for two purposes.  The first is to keep the
 * EMAC's duplex setting in sync with the PHY's.  The second is to keep
 * the system apprised of the state of the link.  Note that this driver
 * does not configure the PHY.  Either the PHY should be configured for
 * auto-negotiation or it should be handled by something like mii-tool. */
static void poll_gmii(unsigned long data)
{
	struct net_device *dev;
	struct net_local *lp;
	DUPLEX phy_duplex;
	int phy_carrier;
	int netif_carrier;

	dev = (struct net_device *) data;
	lp = (struct net_local *) dev->priv;

	/* First, find out what's going on with the PHY. */
	if (get_phy_status(dev, &phy_duplex, &phy_carrier)) {
		printk(KERN_ERR "%s: XLlTemac: terminating link monitoring.\n",
		       dev->name);
		return;
	}
	netif_carrier = netif_carrier_ok(dev) != 0;
	if (phy_carrier != netif_carrier) {
		if (phy_carrier) {
			printk(KERN_INFO
			       "%s: XLlTemac: PHY Link carrier restored.\n",
			       dev->name);
			netif_carrier_on(dev);
		}
		else {
			printk(KERN_INFO
			       "%s: XLlTemac: PHY Link carrier lost.\n",
			       dev->name);
			netif_carrier_off(dev);
		}
	}

	/* Set up the timer so we'll get called again in 2 seconds. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	add_timer(&lp->phy_timer);
}

static irqreturn_t xenet_temac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;

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
	XLlTemac_IntClear(&lp->Emac, XTE_INT_ALL_MASK);

	lp->stats.rx_errors++;
	lp->stats.rx_crc_errors++;


	return IRQ_HANDLED;
}

static void FifoSendHandler(struct net_device *dev);
static void FifoRecvHandler(struct net_device *dev);

static irqreturn_t xenet_fifo_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 irq_status;

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
			FifoRecvHandler(dev);
			irq_status &= ~XLLF_INT_RC_MASK;
		}
		else if (irq_status & XLLF_INT_TC_MASK) {
			/* handle the transmit completion */
			FifoSendHandler(dev);
			irq_status &= ~XLLF_INT_TC_MASK;
		}
		else if (irq_status & XLLF_INT_TXERROR_MASK) {
			lp->stats.tx_errors++;
			lp->stats.tx_fifo_errors++;
			XLlFifo_Reset(&lp->Fifo);
			irq_status &= ~XLLF_INT_TXERROR_MASK;
		}
		else if (irq_status & XLLF_INT_RXERROR_MASK) {
			lp->stats.rx_errors++;
			XLlFifo_Reset(&lp->Fifo);
			irq_status &= ~XLLF_INT_RXERROR_MASK;
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
	struct net_local *lp = (struct net_local *) dev->priv;
	struct list_head *cur_lp;

	/* Read pending interrupts */
	irq_status = XLlDma_mBdRingGetIrq(&lp->Dma.RxBdRing);

	XLlDma_mBdRingAckIrq(&lp->Dma.RxBdRing, irq_status);

	if ((irq_status & XLLDMA_IRQ_ALL_ERR_MASK)) {
		XLlDma_Reset(&lp->Dma);
		return IRQ_HANDLED;
	}

	if ((irq_status & (XLLDMA_IRQ_DELAY_MASK | XLLDMA_IRQ_COALESCE_MASK))) {
		spin_lock(&receivedQueueSpin);
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
		spin_unlock(&receivedQueueSpin);
	}
	return IRQ_HANDLED;
}

static irqreturn_t xenet_dma_tx_interrupt(int irq, void *dev_id)
{
	u32 irq_status;
	struct net_device *dev = dev_id;
	struct net_local *lp = (struct net_local *) dev->priv;
	struct list_head *cur_lp;

	/* Read pending interrupts */
	irq_status = XLlDma_mBdRingGetIrq(&(lp->Dma.TxBdRing));

	XLlDma_mBdRingAckIrq(&(lp->Dma.TxBdRing), irq_status);

	if ((irq_status & XLLDMA_IRQ_ALL_ERR_MASK)) {
		XLlDma_Reset(&lp->Dma);
		return IRQ_HANDLED;
	}

	if ((irq_status & (XLLDMA_IRQ_DELAY_MASK | XLLDMA_IRQ_COALESCE_MASK))) {
		spin_lock(&sentQueueSpin);
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
		spin_unlock(&sentQueueSpin);
	}
	return IRQ_HANDLED;
}

/*
 * Q:
 * Why doesn't this linux driver have an interrupt handler for the TEMAC itself?
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
 *        critical and also not wholy accurate either.
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
	int irqval = 0;

	/*
	 * Just to be safe, stop TX queue and the device first.  If the device is
	 * already stopped, an error will be returned.  In this case, we don't
	 * really care.
	 */
	netif_stop_queue(dev);
	lp = (struct net_local *) dev->priv;
	_XLlTemac_Stop(&lp->Emac);

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

	irqval = request_irq(dev->irq, &xenet_temac_interrupt, IRQF_DISABLED,
			     dev->name, dev);
	if (irqval) {
		printk(KERN_ERR
		       "%s: XLlTemac: could not allocate interrupt %d.\n",
		       dev->name, lp->dma_irq_s);
		return irqval;
	}
	if (XLlTemac_IsDma(&lp->Emac)) {
		printk(KERN_INFO
		       "%s: XLlTemac: allocating interrupt %d for dma mode tx.\n",
		       dev->name, lp->dma_irq_s);
		irqval = request_irq(lp->dma_irq_s,
				     &xenet_dma_tx_interrupt, 0,
				     "xilinx_dma_tx_int", dev);
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
				     &xenet_dma_rx_interrupt, 0,
				     "xilinx_dma_rx_int", dev);
		if (irqval) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not allocate interrupt %d.\n",
			       dev->name, lp->dma_irq_r);
			return irqval;
		}
	}
	else {
		printk(KERN_INFO
		       "%s: XLlTemac: allocating interrupt %d for fifo mode.\n",
		       dev->name, lp->fifo_irq);
		/* With the way interrupts are issued on the fifo core, this needs to be
		 * fast interrupt handler.
		 */
		irqval = request_irq(lp->fifo_irq,
				     &xenet_fifo_interrupt, IRQF_DISABLED,
				     "xilinx_fifo_int", dev);
		if (irqval) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not allocate interrupt %d.\n",
			       dev->name, lp->fifo_irq);
			return irqval;
		}
	}

	/* give the system enough time to establish a link */
	mdelay(2000);

	set_mac_speed(lp);
#ifdef PHYSETUP
	PhySetup(&lp->Emac, lp->cur_speed);
#endif

	INIT_LIST_HEAD(&(lp->rcv));
	INIT_LIST_HEAD(&(lp->xmit));

	/* Enable interrupts  - no polled mode */
	{
		if (XLlTemac_IsFifo(&lp->Emac)) {	/*fifo direct interrupt driver mode */
			XLlFifo_IntEnable(&lp->Fifo,
					  XLLF_INT_TC_MASK | XLLF_INT_RC_MASK);
		}
		else {		/* SG DMA mode */
			XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing,
						dma_rx_int_mask);
			XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing,
						dma_tx_int_mask);
		}
		XLlTemac_IntEnable(&lp->Emac, XTE_INT_RXRJECT_MASK);
	}

	/* Start TEMAC device */
	_XLlTemac_Start(&lp->Emac);
	if (XLlTemac_IsDma(&lp->Emac)) {
		u32 threshold_s, timer_s, threshold_r, timer_r;

		XLlDma_BdRingGetCoalesce(&lp->Dma.TxBdRing,
					 &threshold_s, &timer_s);
		XLlDma_BdRingGetCoalesce(&lp->Dma.RxBdRing,
					 &threshold_r, &timer_r);
		printk(KERN_INFO
		       "%s: XLlTemac: Send Threshold = %d, Receive Threshold = %d\n",
		       dev->name, threshold_s, threshold_r);
		printk(KERN_INFO
		       "%s: XLlTemac: Send Wait bound = %d, Receive Wait bound = %d\n",
		       dev->name, timer_s, timer_r);
		if (XLlDma_BdRingStart(&lp->Dma.TxBdRing) == XST_FAILURE) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not start dma tx channel\n",
			       dev->name);
			return -EIO;
		}
		if (XLlDma_BdRingStart(&lp->Dma.RxBdRing) == XST_FAILURE) {
			printk(KERN_ERR
			       "%s: XLlTemac: could not start dma rx channel\n",
			       dev->name);
			return -EIO;
		}
	}

	/* We're ready to go. */
	netif_start_queue(dev);

	/* Set up the PHY monitoring timer. */
	lp->phy_timer.expires = jiffies + 2 * HZ;
	lp->phy_timer.data = (unsigned long) dev;
	lp->phy_timer.function = &poll_gmii;
	init_timer(&lp->phy_timer);
	add_timer(&lp->phy_timer);

	INIT_LIST_HEAD(&sentQueue);
	INIT_LIST_HEAD(&receivedQueue);

	spin_lock_init(&sentQueueSpin);
	spin_lock_init(&receivedQueueSpin);
	return 0;
}

static int xenet_close(struct net_device *dev)
{
	struct net_local *lp;
	unsigned long flags;

	lp = (struct net_local *) dev->priv;

	/* Shut down the PHY monitoring timer. */
	del_timer_sync(&lp->phy_timer);

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
	}
	else {
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
	unsigned long flags, fifo_free_bytes;

	/* The following lock is used to protect GetFreeBytes, FifoWrite
	 * and FifoSend sequence which could happen from FifoSendHandler
	 * or other processor in SMP case.
	 */
	spin_lock_irqsave(&XTE_tx_spinlock, flags);
	lp = (struct net_local *) dev->priv;
	len = skb->len;

	fifo_free_bytes = XLlFifo_TxVacancy(&lp->Fifo) * 4;
	if (fifo_free_bytes < len) {
		netif_stop_queue(dev);	/* stop send queue */
		lp->deferred_skb = skb;	/* buffer the sk_buffer and will send
					   it in interrupt context */
		spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
		return 0;
	}

	/* Write frame data to FIFO */
	XLlFifo_Write(&lp->Fifo, (void *) skb->data, len);

	/* Initiate transmit */
	XLlFifo_TxSetLen(&lp->Fifo, len);
	lp->stats.tx_bytes += len;
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

	spin_lock(&XTE_tx_spinlock);
	lp = (struct net_local *) dev->priv;
	lp->stats.tx_packets++;

	/*Send out the deferred skb and wake up send queue if a deferred skb exists */
	if (lp->deferred_skb) {

		skb = lp->deferred_skb;
		/* If no room for the deferred packet, return */
		if ((XLlFifo_TxVacancy(&lp->Fifo) * 4) < skb->len) {
			spin_unlock(&XTE_tx_spinlock);
			return;
		}

		/* Write frame data to FIFO */
		XLlFifo_Write(&lp->Fifo, (void *) skb->data, skb->len);

		/* Initiate transmit */
		XLlFifo_TxSetLen(&lp->Fifo, skb->len);

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

	lp = (struct net_local *) dev->priv;

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
	}
	else {
		dev_kfree_skb(skb);
		lp->stats.tx_dropped++;
		printk(KERN_ERR
		       "%s: XLlTemac: could not send TX socket buffers (too many fragments).\n",
		       dev->name);
		return XST_FAILURE;
	}

	len = skb_headlen(skb);

	/* get the physical address of the header */
	phy_addr = (u32) dma_map_single(NULL, skb->data, len, DMA_TO_DEVICE);

	/* get the header fragment, it's in the skb differently */
	XLlDma_mBdSetBufAddr(bd_ptr, phy_addr);
	XLlDma_mBdSetLength(bd_ptr, len);
	XLlDma_mBdSetId(bd_ptr, skb);

	/* 
	 * if tx checksum offloading is enabled, when the ethernet stack
	 * wants us to perform the checksum in hardware,
	 * skb->ip_summed is CHECKSUM_COMPLETE. Otherwise skb->ip_summed is
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
	if (skb->ip_summed == CHECKSUM_COMPLETE) {

		unsigned char *raw = skb_transport_header(skb);
#if 0
		{
			unsigned int csum = _xenet_tx_csum(skb);

			*((unsigned short *) (raw + skb->csum)) =
				csum_fold(csum);
			BdCsumDisable(bd_ptr);
		}
#else
		BdCsumEnable(bd_ptr);
		BdCsumSetup(bd_ptr, raw - skb->data,
			    (raw - skb->data) + skb->csum);

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
			(void *) page_address(frag->page) + frag->page_offset;
		phy_addr =
			(u32) dma_map_single(NULL, virt_addr, frag->size,
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
	}
	else {
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

		spin_lock(&XTE_tx_spinlock);
		dev = lp->ndev;
		bd_processed_save = 0;
		while ((bd_processed =
			XLlDma_BdRingFromHw(&lp->Dma.TxBdRing, XTE_SEND_BD_CNT,
					    &BdPtr)) > 0) {

			bd_processed_save = bd_processed;
			BdCurPtr = BdPtr;
			do {
				len = XLlDma_mBdGetLength(BdCurPtr);
				skb_dma_addr = (dma_addr_t)
					XLlDma_mBdGetBufAddr(BdCurPtr);
				dma_unmap_single(NULL, skb_dma_addr, len,
						 DMA_TO_DEVICE);

				/* get ptr to skb */
				skb = (struct sk_buff *)
					XLlDma_mBdGetId(BdCurPtr);
				if (skb)
					dev_kfree_skb(skb);

				/* reset BD id */
				XLlDma_mBdSetId(BdCurPtr, NULL);

				lp->stats.tx_bytes += len;
				if (XLlDma_mBdGetStsCtrl(BdCurPtr) &
				    XLLDMA_BD_STSCTRL_EOP_MASK) {
					lp->stats.tx_packets++;
				}

				BdCurPtr = XLlDma_mBdRingNext(&lp->Dma.TxBdRing,
							      BdCurPtr);
				bd_processed--;
			} while (bd_processed > 0);

			result = XLlDma_BdRingFree(&lp->Dma.TxBdRing,
						   bd_processed_save, BdPtr);
			if (result != XST_SUCCESS) {
				printk(KERN_ERR
				       "%s: XLlDma: BdRingFree() error %d.\n",
				       dev->name, result);
				reset(dev, __LINE__);
				spin_unlock(&XTE_tx_spinlock);
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
		spin_unlock(&XTE_tx_spinlock);
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

	lp = (struct net_local *) dev->priv;
	printk(KERN_ERR
	       "%s: XLlTemac: exceeded transmit timeout of %lu ms.  Resetting emac.\n",
	       dev->name, TX_TIMEOUT * 1000UL / HZ);
	lp->stats.tx_errors++;

	reset(dev, __LINE__);

	spin_unlock_irqrestore(&XTE_tx_spinlock, flags);
}

/* The callback function for frames received when in FIFO mode. */
static void FifoRecvHandler(struct net_device *dev)
{
	struct net_local *lp;
	struct sk_buff *skb;
	u32 len;

#define XTE_RX_SINK_BUFFER_SIZE 1024
	static u32 rx_buffer_sink[XTE_RX_SINK_BUFFER_SIZE / sizeof(u32)];

	spin_lock(&XTE_rx_spinlock);
	lp = (struct net_local *) dev->priv;


	if (XLlFifo_RxOccupancy(&lp->Fifo) == 0) {
		spin_unlock(&XTE_rx_spinlock);
		return;
	}

	len = XLlFifo_RxGetLen(&lp->Fifo);

	/*
	 * TODO: Hm this is odd, if we can't allocate the skb, we throw away the next packet. Why?
	 */
	if (!(skb = /*dev_ */ alloc_skb(len + ALIGNMENT_RECV, GFP_ATOMIC))) {
		/* Couldn't get memory. */
		lp->stats.rx_dropped++;
		printk(KERN_ERR
		       "%s: XLlTemac: could not allocate receive buffer.\n",
		       dev->name);

		/* consume data in Xilinx TEMAC RX data fifo so it is sync with RX length fifo */
		for (; len > XTE_RX_SINK_BUFFER_SIZE;
		     len -= XTE_RX_SINK_BUFFER_SIZE) {
			XLlFifo_Read(&lp->Fifo, rx_buffer_sink,
				     XTE_RX_SINK_BUFFER_SIZE);
		}
		XLlFifo_Read(&lp->Fifo, rx_buffer_sink, len);

		spin_unlock(&XTE_rx_spinlock);
		return;
	}

	/* Read the packet data */
	XLlFifo_Read(&lp->Fifo, skb->data, len);
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
 * _xenet_DmaSetupRecvBuffers allocates as many socket buffers (sk_buff's) as it
 * can up to the number of free RX buffer descriptors. Then it sets up the RX
 * buffer descriptors to DMA into the socket_buffers.
 *
 * The net_device, dev, indcates on which device to operate for buffer
 * descriptor allocation.
 */
static void _xenet_DmaSetupRecvBuffers(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;

	int free_bd_count = XLlDma_mBdRingGetFreeCnt(&lp->Dma.RxBdRing);
	int num_sk_buffs;
	struct sk_buff_head sk_buff_list;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	XLlDma_Bd *BdPtr, *BdCurPtr;
	u32 align;
	int result;

#if 0
	int align_max = ALIGNMENT_RECV;
#else
	int align_max = 0;
#endif


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
		new_skb_baddr = (u32) dma_map_single(NULL, new_skb->data,
						     lp->max_frame_size,
						     DMA_FROM_DEVICE);

		XLlDma_mBdSetBufAddr(BdCurPtr, new_skb_baddr);
		XLlDma_mBdSetLength(BdCurPtr, lp->max_frame_size);
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
		dev = lp->ndev;
		spin_unlock_irqrestore(&receivedQueueSpin, flags);

		spin_lock(&XTE_rx_spinlock);
		if ((bd_processed =
		     XLlDma_BdRingFromHw(&lp->Dma.RxBdRing, XTE_RECV_BD_CNT,
					 &BdPtr)) > 0) {

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
				skb_baddr = (dma_addr_t)
					XLlDma_mBdGetBufAddr(BdCurPtr);
				dma_unmap_single(NULL, skb_baddr,
						 lp->max_frame_size,
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
						csum = csum_sub(csum, *data
								/* & 0xffff */
							);
						data++;
						csum = csum_sub(csum, *data
								/* & 0xffff */
							);
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
				spin_unlock(&XTE_rx_spinlock);
				return;
			}

			_xenet_DmaSetupRecvBuffers(dev);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
		spin_unlock(&XTE_rx_spinlock);
	}
}

static int descriptor_init(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
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

	lp->desc_space = kmalloc(dftsize, GFP_KERNEL);
	lp->desc_space_handle =
		(dma_addr_t) page_to_phys(virt_to_page(lp->desc_space));
#else
	lp->desc_space_handle = BRAM_BASEADDR;
	lp->desc_space = ioremap(lp->desc_space_handle, dftsize);
#endif
	if (lp->desc_space == 0) {
		return -1;
	}

	lp->desc_space_size = dftsize;

	printk(KERN_INFO
	       "XLlTemac: (buffer_descriptor_init) phy: 0x%x, virt: 0x%x, size: 0x%x\n",
	       lp->desc_space_handle, (unsigned int) lp->desc_space,
	       lp->desc_space_size);

	/* calc size of send and recv descriptor space */
	recvsize = XLlDma_mBdRingMemCalc(ALIGNMENT_BD, XTE_RECV_BD_CNT);
	sendsize = XLlDma_mBdRingMemCalc(ALIGNMENT_BD, XTE_SEND_BD_CNT);

	recvpoolptr = lp->desc_space;
	sendpoolptr = (void *) ((u32) lp->desc_space + recvsize);

	recvpoolphy = (void *) lp->desc_space_handle;
	sendpoolphy = (void *) ((u32) lp->desc_space_handle + recvsize);

	result = XLlDma_BdRingCreate(&lp->Dma.RxBdRing, (u32) recvpoolphy,
				     (u32) recvpoolptr, ALIGNMENT_BD,
				     XTE_RECV_BD_CNT);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR "XLlTemac: DMA Ring Create (RECV). Error: %d\n",
		       result);
		return -EIO;
	}

	result = XLlDma_BdRingCreate(&lp->Dma.TxBdRing, (u32) sendpoolphy,
				     (u32) sendpoolptr, ALIGNMENT_BD,
				     XTE_SEND_BD_CNT);
	if (result != XST_SUCCESS) {
		printk(KERN_ERR "XLlTemac: DMA Ring Create (SEND). Error: %d\n",
		       result);
		return -EIO;
	}

	_xenet_DmaSetupRecvBuffers(dev);
	return 0;
}

static void free_descriptor_skb(struct net_device *dev)
{
	struct net_local *lp = (struct net_local *) dev->priv;
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
			dma_unmap_single(NULL, skb_dma_addr, lp->max_frame_size,
					 DMA_FROM_DEVICE);
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
			dma_unmap_single(NULL, skb_dma_addr, len,
					 DMA_TO_DEVICE);
			dev_kfree_skb(skb);
		}
		/* find the next BD in the DMA TX BD ring */
		BdPtr = XLlDma_mBdRingNext(&lp->Dma.TxBdRing, BdPtr);
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
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 mac_options;
	u32 threshold, timer;
	u16 gmii_cmd, gmii_status, gmii_advControl;

	memset(ecmd, 0, sizeof(struct ethtool_cmd));

	mac_options = XLlTemac_GetOptions(&(lp->Emac));
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMCR, &gmii_cmd);
	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_BMSR, &gmii_status);

	_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, MII_ADVERTISE,
			  &gmii_advControl);

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

	if (ecmd->speed != lp->cur_speed) {
		renegotiate_speed(dev, ecmd->speed, FULL_DUPLEX);
		_XLlTemac_SetOperatingSpeed(&lp->Emac, ecmd->speed);
		lp->cur_speed = ecmd->speed;
#ifdef PHYSETUP
		PhySetup(&lp->Emac, lp->cur_speed);
#endif
	}
	return 0;
}

static int
xenet_ethtool_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct net_local *lp = (struct net_local *) dev->priv;
	u32 threshold, waitbound;

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	XLlDma_BdRingGetCoalesce(&lp->Dma.RxBdRing, &threshold, &waitbound);
	ec->rx_max_coalesced_frames = threshold;
	ec->rx_coalesce_usecs = waitbound;

	XLlDma_BdRingGetCoalesce(&lp->Dma.TxBdRing, &threshold, &waitbound);
	ec->tx_max_coalesced_frames = threshold;
	ec->tx_coalesce_usecs = waitbound;

	return 0;
}

void disp_bd_ring(XLlDma_BdRing * bd_ring)
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

	lp = (struct net_local *) dev->priv;

	if (ec->rx_coalesce_usecs == 0) {
		ec->rx_coalesce_usecs = 1;
		dma_rx_int_mask =
			XLLDMA_CR_IRQ_ALL_EN_MASK &
			~XLLDMA_IRQ_COALESCE_COUNTER_MASK;
	}
	if ((ret = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
					    (u16) (ec->rx_max_coalesced_frames),
					    (u16) (ec->rx_coalesce_usecs))) !=
	    XST_SUCCESS) {
		printk(KERN_ERR "%s: XLlDma: BdRingSetCoalesce error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);

	if (ec->tx_coalesce_usecs == 0) {
		ec->tx_coalesce_usecs = 1;
		dma_tx_int_mask =
			XLLDMA_CR_IRQ_ALL_EN_MASK &
			~XLLDMA_IRQ_COALESCE_COUNTER_MASK;
	}
	if ((ret = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
					    (u16) (ec->rx_max_coalesced_frames),
					    (u16) (ec->rx_coalesce_usecs))) !=
	    XST_SUCCESS) {
		printk(KERN_ERR "%s: XLlDma: BdRingSetCoalesce error %d\n",
		       dev->name, ret);
		return -EIO;
	}
	XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);

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

	dump->hd.version = 0;
	dump->hd.len = sizeof(dump->data);
	memset(dump->data, 0, sizeof(dump->data));

	for (i = 0; i < EMAC_REGS_N; i++) {
		_XLlTemac_PhyRead(&lp->Emac, lp->gmii_addr, i,
				  &(dump->data[i]));
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
	int ret = -EOPNOTSUPP;
	u32 Options;

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
		Options = XLlTemac_GetOptions(&lp->Emac);
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
	u32 threshold, timer;
	XLlDma_BdRing *RingPtr;
	u32 *dma_int_mask_ptr;

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
		del_timer_sync(&lp->phy_timer);

		_XLlTemac_PhyRead(&lp->Emac, data->phy_id, data->reg_num,
				  &data->val_out);

		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
		return 0;

	case SIOCSMIIREG:	/* Write GMII PHY register. */
	case SIOCDEVPRIVATE + 2:	/* for binary compat, remove in 2.5 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (data->phy_id > 31 || data->reg_num > 31)
			return -ENXIO;

		/* Stop the PHY timer to prevent reentrancy. */
		del_timer_sync(&lp->phy_timer);

		_XLlTemac_PhyWrite(&lp->Emac, data->phy_id, data->reg_num,
				   data->val_in);

		/* Start the PHY timer up again. */
		lp->phy_timer.expires = jiffies + 2 * HZ;
		add_timer(&lp->phy_timer);
		return 0;

	case SIOCDEVPRIVATE + 3:	/* set THRESHOLD */
		if (XLlTemac_IsFifo(&lp->Emac))
			return -EFAULT;

		if (copy_from_user(&thr_arg, rq->ifr_data, sizeof(thr_arg)))
			return -EFAULT;

		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		}
		else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr, &threshold, &timer);
		if (thr_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
		}
		else {
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
		}
		else {
			RingPtr = &lp->Dma.RxBdRing;
		}
		XLlDma_BdRingGetCoalesce(RingPtr, &threshold, &timer);
		if (wbnd_arg.direction == XTE_SEND) {
			RingPtr = &lp->Dma.TxBdRing;
			dma_int_mask_ptr = &dma_tx_int_mask;
		}
		else {
			RingPtr = &lp->Dma.RxBdRing;
			dma_int_mask_ptr = &dma_rx_int_mask;
		}
		if (wbnd_arg.waitbound == 0) {
			wbnd_arg.waitbound = 1;
			*dma_int_mask_ptr =
				XLLDMA_CR_IRQ_ALL_EN_MASK &
				~XLLDMA_IRQ_COALESCE_COUNTER_MASK;
		}
		if ((ret = XLlDma_BdRingSetCoalesce(RingPtr, threshold,
						    wbnd_arg.waitbound)) !=
		    XST_SUCCESS) {
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
		}
		else {
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
		}
		else {
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
		_XLlTemac_PhyRead(&lp->Emac, phy_addr, PHY_DETECT_REG,
				  &phy_reg);

		if ((phy_reg != 0xFFFF) &&
		    ((phy_reg & PHY_DETECT_MASK) == PHY_DETECT_MASK)) {
			/* Found a valid PHY address */
			printk(KERN_INFO
			       "XTemac: PHY detected at address %d.\n",
			       phy_addr);
			return phy_addr;
		}
	}

	printk(KERN_WARNING
	       "XTemac: No PHY detected.  Assuming a PHY at address 0\n");
	return 0;		/* default to zero */
}


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

	/* Create an ethernet device instance */
	ndev = alloc_etherdev(sizeof(struct net_local));
	if (!ndev) {
		dev_err(dev, "xlltemac: Could not allocate net device.\n");
		rc = -ENOMEM;
		goto error;
	}
	dev_set_drvdata(dev, ndev);

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

	/* Set the MAC address */
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

	lp->max_frame_size = XTE_MAX_JUMBO_FRAME_SIZE;
	if (ndev->mtu > XTE_JUMBO_MTU)
		ndev->mtu = XTE_JUMBO_MTU;


	if (XLlTemac_IsDma(&lp->Emac)) {
		int result;

		dev_err(dev, "XLlTemac: using DMA mode.\n");

		virt_baddr = (u32) ioremap(pdata->ll_dev_baseaddress, 4096);
		if (0 == virt_baddr) {
			dev_err(dev, 
			       "XLlTemac: Could not allocate iomem for local link connected device.\n");
			rc = -EIO;
			goto error;
		}
		XLlDma_Initialize(&lp->Dma, virt_baddr);


		ndev->hard_start_xmit = xenet_DmaSend;

		result = descriptor_init(ndev);
		if (result) {
			rc = -EIO;
			goto error;
		}

		/* set the packet threshold and wait bound for both TX/RX directions */
		if (DFT_TX_WAITBOUND == 0) {
			dma_tx_int_mask =
				XLLDMA_CR_IRQ_ALL_EN_MASK &
				~XLLDMA_IRQ_COALESCE_COUNTER_MASK;
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
						      DFT_TX_THRESHOLD, 1);
		}
		else {
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.TxBdRing,
						      DFT_TX_THRESHOLD,
						      DFT_TX_WAITBOUND);
		}
		if (xs != XST_SUCCESS) {
			dev_err(dev,
			       "XLlTemac: could not set SEND pkt threshold/waitbound, ERROR %d",
			       xs);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.TxBdRing, dma_tx_int_mask);

		if (DFT_RX_WAITBOUND == 0) {
			dma_rx_int_mask =
				XLLDMA_CR_IRQ_ALL_EN_MASK &
				~XLLDMA_IRQ_COALESCE_COUNTER_MASK;
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
						      DFT_RX_THRESHOLD, 1);
		}
		else {
			xs = XLlDma_BdRingSetCoalesce(&lp->Dma.RxBdRing,
						      DFT_RX_THRESHOLD,
						      DFT_RX_WAITBOUND);
		}
		if (xs != XST_SUCCESS) {
			dev_err(dev,
			       "XLlTemac: Could not set RECV pkt threshold/waitbound ERROR %d",
			       xs);
		}
		XLlDma_mBdRingIntEnable(&lp->Dma.RxBdRing, dma_rx_int_mask);
	}
	else {
		dev_err(dev,
		       "XLlTemac: using FIFO direct interrupt driven mode.\n");

		virt_baddr = (u32) ioremap(pdata->ll_dev_baseaddress, 4096);
		if (0 == virt_baddr) {
			dev_err(dev,
			       "XLlTemac: Could not allocate iomem for local link connected device.\n");
			rc = -EIO;
			goto error;
		}
		XLlFifo_Initialize(&lp->Fifo, virt_baddr);

		ndev->hard_start_xmit = xenet_FifoSend;
	}

	/** Scan to find the PHY */
	lp->gmii_addr = detect_phy(lp, ndev->name);


	/* initialize the netdev structure */
	ndev->open = xenet_open;
	ndev->stop = xenet_close;
	ndev->change_mtu = xenet_change_mtu;
	ndev->get_stats = xenet_get_stats;
	ndev->flags &= ~IFF_MULTICAST;

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

	ndev->do_ioctl = xenet_ioctl;
	ndev->tx_timeout = xenet_tx_timeout;
	ndev->watchdog_timeo = TX_TIMEOUT;

	/* init the stats */
	lp->max_frags_in_a_packet = 0;
	lp->tx_hw_csums = 0;
	lp->rx_hw_csums = 0;

#if ! XTE_AUTOSTRIPPING
	lp->stripping =
		(XLlTemac_GetOptions(&(lp->Emac)) & XTE_FCS_STRIP_OPTION) != 0;
#endif

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(dev,
		       "%s: Cannot register net device, aborting.\n",
		       ndev->name);
		goto error;	/* rc is already set here... */
	}

	dev_info(dev,
		"%s: Xilinx TEMAC at 0x%08X mapped to 0x%08X, irq=%d\n",
		ndev->name,
		r_mem->start,
		lp->Emac.Config.BaseAddress,
		ndev->irq);

	return 0;

      error:
	if (ndev) {
		xtenet_remove_ndev(ndev);
	}
	return rc;
}

static int xtenet_probe(struct device *dev)
{
	struct resource *r_irq = NULL;	/* Interrupt resources */
	struct resource *r_mem = NULL;	/* IO mem resources */
	struct xlltemac_platform_data *pdata;
	struct platform_device *pdev = to_platform_device(dev);

	/* param check */
	if (!pdev) {
		dev_err(dev, "XLlTemac: Internal error. Probe called with NULL param.\n");
		return -ENODEV;
	}

	pdata = (struct xlltemac_platform_data *) pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "xlltemac: Couldn't find platform data.\n");

		return -ENODEV;
	}

	/* Get iospace and an irq for the device */
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_irq || !r_mem) {
		dev_err(dev, "xlltemac: IO resource(s) not found.\n");
		return -ENODEV;
	}

        return xtenet_setup(dev, r_mem, r_irq, pdata);
}

static struct device_driver xtenet_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,

	.probe = xtenet_probe,
	.remove = xtenet_remove
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

static int __devinit xtenet_of_probe(struct of_device *ofdev, const struct of_device_id *match)
{
	struct resource r_irq_struct;
	struct resource r_mem_struct;
	struct xlltemac_platform_data pdata_struct;

	struct resource *r_irq = &r_irq_struct;	/* Interrupt resources */
	struct resource *r_mem = &r_mem_struct;	/* IO mem resources */
	struct xlltemac_platform_data *pdata = &pdata_struct;
	int rc = 0;

	printk(KERN_INFO "Device Tree Probing \'%s\'\n",
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

	pdata_struct.tx_csum		= get_u32(ofdev, "xlnx,txcsum");
	pdata_struct.rx_csum		= get_u32(ofdev, "xlnx,rxcsum");
	pdata_struct.phy_type           = get_u32(ofdev, "xlnx,phy-type");
	pdata_struct.ll_dev_type	= get_u32(ofdev, "xlnx,llink-connected-type");
	pdata_struct.ll_dev_baseaddress	= get_u32(ofdev, "xlnx,llink-connected-baseaddr");
	pdata_struct.ll_dev_dma_rx_irq	= get_u32(ofdev, "xlnx,llink-connected-dmarx-intr");
	pdata_struct.ll_dev_dma_tx_irq	= get_u32(ofdev, "xlnx,llink-connected-dmatx-intr");
	pdata_struct.ll_dev_fifo_irq	= get_u32(ofdev, "xlnx,llink-connected-fifo-intr");
	memcpy(pdata_struct.mac_addr, of_get_mac_address(ofdev->node), 6);

        return xtenet_setup(&ofdev->dev, r_mem, r_irq, pdata);
}

static int __devexit xtenet_of_remove(struct of_device *dev)
{
	return xtenet_remove(&dev->dev);
}

static struct of_device_id xtenet_of_match[] = {
	{ .compatible = "xlnx,xps-ll-temac", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xtenet_of_match);

static struct of_platform_driver xtenet_of_driver = {
	.name		= DRIVER_NAME,
	.match_table	= xtenet_of_match,
	.probe		= xtenet_of_probe,
	.remove		= __devexit_p(xtenet_of_remove),
};
#endif

static int __init xtenet_init(void)
{
	int status;

	/*
	 * Make sure the locks are initialized
	 */
	spin_lock_init(&XTE_spinlock);
	spin_lock_init(&XTE_tx_spinlock);
	spin_lock_init(&XTE_tx_spinlock);

	/*
	 * No kernel boot options used,
	 * so we just need to register the driver
	 */
	status = driver_register(&xtenet_driver);
#ifdef CONFIG_OF
	status |= of_register_platform_driver(&xtenet_of_driver);
#endif
        return status;

}

static void __exit xtenet_cleanup(void)
{
	driver_unregister(&xtenet_driver);
#ifdef CONFIG_OF
	of_unregister_platform_driver(&xtenet_of_driver);
#endif
}

module_init(xtenet_init);
module_exit(xtenet_cleanup);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
