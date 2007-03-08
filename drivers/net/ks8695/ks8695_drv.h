/*
	Copyright (c) 2002, Micrel Kendin Operations

	Written 2002 by LIQUN RUAN

	This software may be used and distributed according to the terms of 
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice. This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as lruan@kendin.com
	Micrel Kendin Operations
	486 Mercury Dr.
	Sunnyvale, CA 94085

	This driver is for Kendin's KS8695 SOHO Router Chipset as ethernet driver.

	Support and updates available at
	www.kendin.com/ks8695/

*/
#ifndef __KS8695DRV_H
#define __KS8695DRV_H

struct _ADAPTER_STRUCT;
typedef struct _ADAPTER_STRUCT ADAPTER_STRUCT, *PADAPTER_STRUCT;

#include "ks8695_kcompat.h"
//#include <linux/module.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include "ks8695_chipdef.h"

#include "ks8695_fxhw.h"

#define BAR_0						0

#define DRV_INFO(S, args...)		printk(KERN_INFO "eth info: " S "\n" , ##args)
#define DRV_DBG(S, args...)			printk(KERN_DEBUG "eth dbg: " S "\n" , ## args)
#define DRV_ERR(S, args...)			printk(KERN_ERR "eth err: " S "\n" , ## args)
#define DRV_WARN(S, args...)		printk(KERN_WARNING "eth warning: " S "\n" , ## args)

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/capability.h>

#define TRUE			1
#define FALSE			0

/* command line options defaults */
#define TXDESC_DEFAULT	128	
#define TXDESC_MAX		256
#define TXDESC_MIN		64

#define RXDESC_DEFAULT	128 
#define RXDESC_MAX		256
#define RXDESC_MIN		64

#define OPTION_UNSET    	-1
#define OPTION_DISABLED 	0
#define OPTION_ENABLED  	1

#if defined(CONFIG_MACH_LITE300) || defined(CONFIG_MACH_CM4002) || \
    defined(CONFIG_MACH_CM4008) || defined(CONFIG_MACH_CM41xx) || \
    defined(CONFIG_MACH_SE4200)
#define RXCHECKSUM_DEFAULT	OPTION_DISABLED
#define TXCHECKSUM_DEFAULT	OPTION_DISABLED
#else
#define RXCHECKSUM_DEFAULT	OPTION_ENABLED
#define TXCHECKSUM_DEFAULT	OPTION_ENABLED
#endif
#define FLOWCONTROL_DEFAULT	OPTION_ENABLED

#define PBL_DEFAULT		8	     /* 0 for unlimited, other value for (4 * x), our VxWorks shows that 8 is optimized */	

// Supported RX Buffer Sizes
#define BUFFER_1568		1568		/* 0x620 */
#define BUFFER_2048		2048
#define	BUFFER_4K		4096

// standard ethernet header
#define ENET_HEADER_SIZE                14
#define MAXIMUM_ETHERNET_PACKET_SIZE	1514
#define MINIMUM_ETHERNET_PACKET_SIZE	60
#define ETH_CRC_LENGTH			4
#define ETH_LENGTH_OF_ADDRESS		6

#define KS8695_ROUNDUP(size, unit) (unit * ((size + unit - 1) / unit))

// socket buffer
struct ks8695_buffer {
	struct sk_buff *skb;
	dma_addr_t     dma;
	unsigned long  length;
	int            direction;
};

// Adapter->flags definitions
#define KS8695_BOARD_OPEN	0

/* board specific private data structure */
struct _ADAPTER_STRUCT {
	struct _ADAPTER_STRUCT *next;
	struct _ADAPTER_STRUCT *prev;

	unsigned long flags;
	uint32_t bd_number;
	struct timer_list timer_id;

	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;

	DMA_INFO	stDMAInfo;					/* DMA information */
	u8	rev;		/* revision, for KS8695P */
};

#define	DI		(Adapter->stDMAInfo)		/* Dma Information */
#define	DPI		(Adapter->stDMAInfo.port)	/* Dma Port Inforamtion */

/* ks8695_main.c */
extern int ks8695_init_module(void);
extern void ks8695_exit_module(void);
extern int ks8695_probe(struct pci_dev *pdev,
                                 const struct pci_device_id *ent);
extern void ks8695_remove(struct pci_dev *pdev);
extern void ks8695_delete(PADAPTER_STRUCT Adapter);
extern int ks8695_open(struct net_device *netdev);
extern int ks8695_close(struct net_device *netdev);
extern void ks8695_set_multi(struct net_device *netdev);
extern int ks8695_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
extern struct net_device_stats *ks8695_get_stats(struct net_device *netdev);
extern int ks8695_change_mtu(struct net_device *netdev, int new_mtu);
extern int ks8695_set_mac(struct net_device *netdev, void *p);
extern irqreturn_t ks8695_isr(int irq, void *data);
/* for I-cache lockdown or FIQ purpose */
extern void ks8695_isre(void);	/* to fix compiler complain if integrated with kernel */
extern irqreturn_t ks8695_isr_link(int irq, void *data);
extern int ks8695_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
extern void ks8695_watchdog(unsigned long data);

/* ks8695_fxhw.c */
extern BOOLEAN ks8695_ChipInit(PADAPTER_STRUCT Adapter, BOOLEAN bResetPhy);
extern void macSetLoopback(PADAPTER_STRUCT Adapter, BOOLEAN bLoopback);
extern void macStopAll(PADAPTER_STRUCT Adapter);
extern void macStartRx(PADAPTER_STRUCT Adapter, BOOLEAN bStart);
extern void macStartTx(PADAPTER_STRUCT Adapter, BOOLEAN bStart);
extern int macSetStationEx(PADAPTER_STRUCT Adapter, UCHAR *pMac, UINT uIndex);
extern int macResetStationEx(PADAPTER_STRUCT Adapter, UCHAR *pMac);
extern void macGetStationAddress(PADAPTER_STRUCT Adapter, uint8_t *pMacAddress);
extern void macEnableInterrupt(PADAPTER_STRUCT Adapter, BOOLEAN bEnable);
extern void macSetStationAddress(PADAPTER_STRUCT Adapter, uint8_t *pMacAddress);
extern int macGetIndexStationEx(PADAPTER_STRUCT Adapter);

/* switch related */
extern void swSetLED(PADAPTER_STRUCT Adapter, BOOLEAN bLED1, LED_SELECTOR nSel);
extern int swGetPhyStatus(PADAPTER_STRUCT Adapter, UINT uPort);
extern UINT swReadSNMPReg(PADAPTER_STRUCT Adapter, UINT uIndex);
extern void swResetSNMPInfo(PADAPTER_STRUCT Adapter);
extern void swEnableSwitch(PADAPTER_STRUCT Adapter, UINT enable);
extern void swDetectPhyConnection(PADAPTER_STRUCT Adapter, UINT uPort);
extern BOOLEAN swPhyLoopback(PADAPTER_STRUCT Adapter, UINT uPort, BOOLEAN bLoopback);
extern BOOLEAN swGetWANLinkStatus(PADAPTER_STRUCT Adapter);
extern void swAutoNegoAdvertisement(PADAPTER_STRUCT Adapter, UINT uPort);
extern void swPhyReset(PADAPTER_STRUCT Adapter, UINT uPort);
extern void swConfigureMediaType(PADAPTER_STRUCT Adapter, UINT uPort, UINT uSpeed, UINT uDuplex);
extern void swConfigTagRemoval(PADAPTER_STRUCT Adapter, UINT uPort, UINT bRemoval);
extern void swConfigTagInsertion(PADAPTER_STRUCT Adapter, UINT uPort, UINT bInsert);
extern void swConfigurePort(PADAPTER_STRUCT Adapter, UINT uPort);

extern void gpioSet(PADAPTER_STRUCT Adapter, UINT uPort, UINT bSet);

#ifdef	CONFIG_ARCH_KS8695P
extern void enablePhyLoopback(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable);
extern void enableRemoteLoopback(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable);
extern void enablePhyIsolate(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable);
extern void forcePhyLink(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable);
extern void dumpDynamicMacTable(PADAPTER_STRUCT Adapter);
extern void dumpStaticMacTable(PADAPTER_STRUCT Adapter);
void enableTxRateControl(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable,
	UINT bEnableLow, UINT bEnableHigh);
void enableRxRateControl(PADAPTER_STRUCT Adapter, UINT uPort, UINT bEnable,
	UINT bEnableLow, UINT bEnableHigh);
void setTxRate(PADAPTER_STRUCT Adapter, UINT uPort, UINT lrate, UINT hrate);
void setRxRate(PADAPTER_STRUCT Adapter, UINT uPort, UINT lrate, UINT hrate);
#endif

#endif /* __KS8695DRV_H */
