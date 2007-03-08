/*
	Copyright (c) 2002-2003, Micrel Semiconductor

	Written 2002 by LIQUN RUAN

	This software may be used and distributed according to the terms of 
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice. This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as liqun.ruan@micrel.com
	Micrel Semiconductor
	1931 Fortune Dr.
	San Jose, CA 95131

	This driver is for Micrel's KS8695/KS8695P SOHO Router Chipset as ethernet driver.

	Support and updates available at
	www.micrel.com/ks8695/		not ready yet!!!

*/
#define __KS8695_MAIN__
#include <linux/mii.h>
#include "ks8695_drv.h"
#include "ks8695_ioctrl.h"
#include "ks8695_cache.h"
#include <asm-arm/unaligned.h>

#ifdef CONFIG_LEDMAN
#include <linux/ledman.h>
#endif

/* define which external ports do what... */
#if defined(CONFIG_MACH_CM4008) || defined(CONFIG_MACH_CM41xx) || \
    defined(CONFIG_MACH_LITE300) || defined(CONFIG_MACH_SE4200)
#define	LANPORT		0
#define	WANPORT		1
#define	HPNAPORT	2
#else
#define	WANPORT		0
#define	LANPORT		1
#define	HPNAPORT	2
#endif

#undef	  USE_TX_UNAVAIL
#undef	  USE_RX_UNAVAIL
#undef	  PACKET_DUMP

/* process recevied packet in task ReceiveProcessTask().*/
//RLQ, defined in Makfile
//#define	  RX_TASK
//#define	  TX_TASK

/* process recevied packet in ISR.*/
#undef    HANDLE_RXPACKET_BY_INTERRUPT
#ifdef    HANDLE_RXPACKET_BY_INTERRUPT
#undef	  RX_TASK
#undef	  TX_TASK
#endif

#define	USE_FIQ

static int offset = 2;		/* shift 2 bytes so that IP header can align to dword boundary */

/* update dcache by ourself only if no PCI subsystem is used */
#define KS8695_MAX_INTLOOP	1
#define	WATCHDOG_TICK		3

#ifndef	CONFIG_ARCH_KS8695P
#ifdef	KS8695X
char ks8695_driver_name[] = "ks8695X SOHO Router 10/100T Ethernet Dirver";
char ks8695_driver_string[]="Micrel KS8695X Ethernet Network Driver";
#else
char ks8695_driver_name[] = "ks8695 SOHO Router 10/100T Ethernet Dirver";
char ks8695_driver_string[]="Micrel KS8695 Ethernet Network Driver";
#endif	/*KS8695X*/
#else
char ks8695_driver_name[] = "ks8695P SOHO Router 10/100T Ethernet Dirver";
char ks8695_driver_string[]="Micrel KS8695P Ethernet Network Driver";
#endif //CONFIG_ARCH_KS8695P
char ks8695_driver_version[] = "1.0.0.20";
char ks8695_copyright[] = "Copyright (c) 2002-2004 Micrel Semiconductor Corp.";

PADAPTER_STRUCT ks8695_adapter_list = NULL;

#if defined(CONFIG_MACH_CM4002) || defined(CONFIG_MACH_CM4008) || \
    defined(CONFIG_MACH_CM41xx)
#define KS8695_MAX_NIC		1		/* Only 1 LAN port used */
#elif !defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
#define KS8695_MAX_NIC		3		/* 0 for WAN, 1 for LAN and 2 for HPHA */
#else
#define KS8695_MAX_NIC		2		/* 0 for WAN and 1 for LAN, KS8695X doesn't have HPNA port either */
#endif //CONFIG_ARCH_KS8695P

#define	STAT_NET(x)			(Adapter->net_stats.x)

static struct pci_dev pci_dev_mimic[KS8695_MAX_NIC];
static int	pci_dev_index = 0;		/* max dev probe allowed */

#define KS8695_OPTION_INIT	{ [0 ... KS8695_MAX_NIC - 1] = OPTION_UNSET }

static int TxDescriptors[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int RxDescriptors[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int Speed[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int Duplex[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int FlowControl[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int RxChecksum[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int TxChecksum[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int TxPBL[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
static int RxPBL[KS8695_MAX_NIC] = KS8695_OPTION_INIT;
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
static int HPNA = OPTION_UNSET;
#endif
#ifdef	__KS8695_CACHE_H
static int PowerSaving = 0;		/* default is off */
static int ICacheLockdown = 0;		/* default is off */
static int RoundRobin = 1;		/* default Icache is roundrobin */
#endif

/* For mii-tool support */
static struct mii_regs mii_regs_lan[] = { 
	{ {KS8695_SWITCH_PORT1, }, {KS8695_SWITCH_AUTO0, 16}, {KS8695_LAN12_POWERMAGR, 16} },
	{ {KS8695_SWITCH_PORT2, }, {KS8695_SWITCH_AUTO0,  0}, {KS8695_LAN12_POWERMAGR,  0} },
	{ {KS8695_SWITCH_PORT3, }, {KS8695_SWITCH_AUTO1, 16}, {KS8695_LAN34_POWERMAGR, 16} },
	{ {KS8695_SWITCH_PORT4, }, {KS8695_SWITCH_AUTO1,  0}, {KS8695_LAN34_POWERMAGR,  0} }
};
static struct mii_regs mii_regs_wan[] = {
	{ {KS8695_WAN_CONTROL, }, {KS8695_WAN_CONTROL, 16}, {KS8695_WAN_CONTROL, 16} }
};
static int skipcmd = 0;
static uint16_t ctype = SW_PHY_DEFAULT;

#ifndef	CONFIG_ARCH_KS8695P
#ifdef	KS8695X
MODULE_AUTHOR("Micrel Semiconductor, <liqun.ruan@micrel.com>");
MODULE_DESCRIPTION("Micrel KS8695X SOHO Router Ethernet Network Driver");
#else
/* for historical reason */
MODULE_AUTHOR("Micrel Kendin Operations, <lruan@kendin.com>");
MODULE_DESCRIPTION("Micrel Kendin KS8695 SOHO Router Ethernet Network Driver");
#endif
#else
MODULE_AUTHOR("Micrel Semiconductor, <liqun.ruan@micrel.com>");
MODULE_DESCRIPTION("Micrel KS8695P SOHO Router Ethernet Network Driver");
#endif
#ifdef	ARM_LINUX
MODULE_LICENSE("GPL");
#endif

module_param_array(TxDescriptors, int, NULL, 0);
module_param_array(RxDescriptors, int, NULL, 0);
module_param_array(Speed, int, NULL, 0);
module_param_array(Duplex, int, NULL, 0);
module_param_array(FlowControl, int, NULL, 0);
module_param_array(RxChecksum, int, NULL, 0);
module_param_array(TxChecksum, int, NULL, 0);
module_param_array(TxPBL, int, NULL, 0);
module_param_array(RxPBL, int, NULL, 0);
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
module_param(HPNA, int, 0);
#endif
module_param(PowerSaving, int, 0);
module_param(ICacheLockdown, int, 0);
module_param(RoundRobin, int, 0);

EXPORT_SYMBOL(ks8695_init_module);
EXPORT_SYMBOL(ks8695_exit_module);
EXPORT_SYMBOL(ks8695_probe);
EXPORT_SYMBOL(ks8695_remove);
EXPORT_SYMBOL(ks8695_open);
EXPORT_SYMBOL(ks8695_close);
EXPORT_SYMBOL(ks8695_xmit_frame);
EXPORT_SYMBOL(ks8695_isr);
EXPORT_SYMBOL(ks8695_isr_link);
EXPORT_SYMBOL(ks8695_set_multi);
EXPORT_SYMBOL(ks8695_change_mtu);
EXPORT_SYMBOL(ks8695_set_mac);
EXPORT_SYMBOL(ks8695_get_stats);
EXPORT_SYMBOL(ks8695_watchdog);
EXPORT_SYMBOL(ks8695_ioctl);

/* for I-cache lockdown or FIQ purpose */
EXPORT_SYMBOL(ks8695_isre);

int ks8695_module_probe(void);
EXPORT_SYMBOL(ks8695_module_probe);

/*********************************************************************
 * Fast timer poll support
 *********************************************************************/

#if defined(CONFIG_FAST_TIMER)
#define FAST_POLL 1
#include <linux/fast_timer.h>
static void ks8695_fast_poll(void *arg);
static int ks8695_poll_ready;
#endif

/*********************************************************************
 * Local Function Prototypes
 *********************************************************************/
static void CheckConfigurations(PADAPTER_STRUCT Adapter);
static int  SoftwareInit(PADAPTER_STRUCT Adapter);
static int  HardwareInit(PADAPTER_STRUCT Adapter);
static int  AllocateTxDescriptors(PADAPTER_STRUCT Adapter);
static int  AllocateRxDescriptors(PADAPTER_STRUCT Adapter);
static void FreeTxDescriptors(PADAPTER_STRUCT Adapter);
static void FreeRxDescriptors(PADAPTER_STRUCT Adapter);
static void UpdateStatsCounters(PADAPTER_STRUCT Adapter);
#if	0
static int	ProcessTxInterrupts(PADAPTER_STRUCT Adapter);
static int	ProcessRxInterrupts(PADAPTER_STRUCT Adapter);
#endif
static void ReceiveBufferFill(uintptr_t data);
static void CleanTxRing(PADAPTER_STRUCT Adapter);
static void CleanRxRing(PADAPTER_STRUCT Adapter);
static void InitTxRing(PADAPTER_STRUCT Adapter);
static void InitRxRing(PADAPTER_STRUCT Adapter);
/*static void ReceiveBufferFillEx(PADAPTER_STRUCT Adapter);*/
#ifdef	__KS8695_CACHE_H
static int ks8695_icache_lock2(void *icache_start, void *icache_end);
#endif

#ifdef	RX_TASK
static void ReceiveProcessTask(uintptr_t data);
#endif
#ifdef	TX_TASK
static void TransmitProcessTask(uintptr_t data);
#endif

/*
 * ResetDma
 *	This function is use to reset DMA in case Tx DMA was sucked due to 
 *	heavy traffic condition.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static __inline void ResetDma(PADAPTER_STRUCT Adapter)
{
	struct net_device *netdev;
	/*BOOLEAN	bTxStarted, bRxStarted;*/
	UINT uRxReg;

#ifdef	DEBUG_THIS 
	if (DMA_LAN == DI.usDMAId) {
		DRV_INFO("%s: LAN", __FUNCTION__);
	} else if (DMA_WAN == DI.usDMAId) {
		DRV_INFO("%s: WAN", __FUNCTION__);
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	} else {
		DRV_INFO("%s: HPNA", __FUNCTION__);
#endif
	}
#endif 

	if (!test_bit(KS8695_BOARD_OPEN, &Adapter->flags)) {
		DRV_INFO("%s: driver not opened yet", __FUNCTION__);
		return;
	}

	netdev = Adapter->netdev;

#ifdef	RX_TASK
	tasklet_disable(&DI.rx_tasklet);
#endif
#ifdef	TX_TASK
	tasklet_disable(&DI.tx_tasklet);
#endif
	netif_stop_queue(netdev);

	macStopAll(Adapter);

	CleanRxRing(Adapter);
	InitRxRing(Adapter);
	CleanTxRing(Adapter);
	InitTxRing(Adapter);

	ks8695_ChipInit(Adapter, FALSE);

	KS8695_WRITE_REG(KS8695_INT_STATUS, DI.uIntMask);

	/* read RX mode register */
	uRxReg = KS8695_READ_REG(REG_RXCTRL + DI.nOffset);
	if (netdev->flags & IFF_PROMISC) {
		uRxReg |= DMA_PROMISCUOUS;
	}
	if (netdev->flags & (IFF_ALLMULTI | IFF_MULTICAST)) {
		uRxReg |= DMA_MULTICAST;
	}
	uRxReg |= DMA_BROADCAST;

	/* write RX mode register */
	KS8695_WRITE_REG(REG_RXCTRL + DI.nOffset, uRxReg);

	KS8695_WRITE_REG(REG_RXBASE + DI.nOffset, cpu_to_le32(DI.RxDescDMA));
	KS8695_WRITE_REG(REG_TXBASE + DI.nOffset, cpu_to_le32(DI.TxDescDMA));
	macEnableInterrupt(Adapter, TRUE);

#ifdef	RX_TASK
	tasklet_enable( &DI.rx_tasklet );
	if (DI.rx_scheduled) {
		tasklet_hi_schedule(&DI.rx_tasklet);
	}
#endif
#ifdef	TX_TASK
	tasklet_enable( &DI.tx_tasklet );
	if (DI.tx_scheduled) {
		tasklet_hi_schedule(&DI.tx_tasklet);
	}
#endif
	netif_start_queue(netdev);

	/*if (bRxStarted)*/
		macStartRx(Adapter, TRUE);
	/*if (bTxStarted)*/
		macStartTx(Adapter, TRUE);
}

/*
 * ks8695_dump_packet
 *	This function is use to dump given packet for debugging.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *	data		pointer to the beginning of the packet to dump
 *	len			length of the packet
 *	flag		debug flag
 *
 * Return(s)
 *	NONE.
 */
#ifndef PING_READY
static __inline void ks8695_dump_packet(PADAPTER_STRUCT Adapter, unsigned char *data, int len, UINT flag)
{
	/* we may need to have locking mechamism to use this function, since Rx call it within INT context
	   and Tx call it in normal context */

	if (flag && len >= 18) {
		if (flag & DEBUG_PACKET_LEN) {
			printk("Pkt Len=%d\n", len);
		}
		if (flag & DEBUG_PACKET_CONTENT) {
			int	j = 0, k;

			do {
				printk("\n %08x   ", (int)(data+j));
				for (k = 0; (k < 16 && len); k++, data++, len--) {
					printk("%02x  ", *data);
				}
				j += 16;
			} while (len > 0);
			printk("\n");
		}
	}
}
#endif

#ifdef PING_READY
static __inline void ks8695_dump_packet(PADAPTER_STRUCT Adapter, unsigned char *data, int len, UINT flag)
{
	/* we may need to have locking mechamism to use this function, since Rx call it within INT context
	   and Tx call it in normal context */

	    DRV_INFO("%s", __FUNCTION__); 

	if (flag && len >= 18) {
		if (flag & DEBUG_PACKET_LEN) {
			printk("Pkt Len=%d\n", len);
		}
		if (flag & DEBUG_PACKET_HEADER) {
			printk("DA=%02x:%02x:%02x:%02x:%02x:%02x\n", 
				*data, *(data + 1), *(data + 2), *(data + 3), *(data + 4), *(data + 5));
			printk("SA=%02x:%02x:%02x:%02x:%02x:%02x\n", 
				*(data + 6), *(data + 7), *(data + 8), *(data + 9), *(data + 10), *(data + 11));
			printk("Type=%04x (%d)\n", ntohs(*(unsigned short *)(data + 12)), ntohs(*(unsigned short *)(data + 12)));
		}
		if (flag & DEBUG_PACKET_CONTENT) {
			int	j = 0, k;

			/* skip DA/SA/TYPE, ETH_HLEN is defined in if_ether.h under include/linux dir */
			data += ETH_HLEN;
			/*len -= (ETH_HLEN + ETH_CRC_LENGTH);*/
			len -= ETH_HLEN;
			do {
				printk("\n %04d   ", j);
				for (k = 0; (k < 16 && len); k++, data++, len--) {
					printk("%02x  ", *data);
				}
				j += 16;
			} while (len > 0);
			/* last dump crc field */
			/*printk("\nCRC=%04x\n", ntohl(*(unsigned int *)data));*/
		}
	}
}
#endif


/*
 * ks8695_relink
 *	This function is use to setup link in case some dynamic configuration
 *	is applied via ifconfig! if driver is opened!
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	NONE.
 */
static void ks8695_relink(PADAPTER_STRUCT Adapter)
{
	if (test_bit(KS8695_BOARD_OPEN, &Adapter->flags)) {
		/* reset the flag even if it is auto nego is in progress
		   to make sure we don't miss it!!! */
		if (DMA_LAN != DI.usDMAId) {
			swDetectPhyConnection(Adapter, 0);
		}
		else {
			int	i;

			for (i = 0; i < SW_MAX_LAN_PORTS; i++) {
				swDetectPhyConnection(Adapter, i);
			}
		}
	}
}

/*
 * ks8695_report_carrier
 *	This function is use to report carrier status to net device
 *
 * Argument(s)
 *	netdev		pointer to net_device structure.
 *	carrier		carrier status (0 off, non zero on)
 *
 * Return(s)
 *	NONE.
 */
static void ks8695_report_carrier(struct net_device *netdev, int carrier)
{
#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* if link is on */
	if (carrier) {
		netif_carrier_on(netdev);
		netif_carrier_ok(netdev);
	}
	else {
		netif_carrier_off(netdev);
	}
}

static void ks8695_tx_timeout(struct net_device *netdev)
{
	printk("%s(%d): ks8695_tx_timeout()\n", __FILE__, __LINE__);
}

/*
 * ks8695_module_probe
 *	This function is used to simulate pci's probe function.
 *
 * Argument(s)
 *	NONE.
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_module_probe(void)
{
#if 0
	spinlock_t	eth_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;
#endif
	int	nRet;
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	int nHPHA = 0;
#endif

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

#ifdef	__KS8695_CACHE_H
	if (RoundRobin) {
		ks8695_icache_change_policy(RoundRobin);
	}
	if (ICacheLockdown)
		ks8695_icache_lock2(ks8695_isr, ks8695_isre);
#endif
	
	if (pci_dev_index >= KS8695_MAX_NIC)
		return -EINVAL;

#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	/* if user enabled HPNA */
	if (HPNA != OPTION_UNSET) {
		nHPHA = HPNA ? 1 : 0;
	}
#endif

#ifdef	__KS8695_CACHE_H
	/* if allow power saving, default no power saving (wait for interrupt) */
	if (PowerSaving) {
		ks8695_enable_power_saving(PowerSaving);
	}
#endif

	nRet = 0;
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	/* default WAN and LAN, plus HPNA if enabled by the user */
	for (pci_dev_index = 0; pci_dev_index < (2 + nHPHA); pci_dev_index++) {
#else
	/* KS8695P and KS8695X has only WAN and LAN */
	for (pci_dev_index = 0; pci_dev_index < KS8695_MAX_NIC; pci_dev_index++) {
#endif
		if (0 == pci_dev_index) {
			//strcpy(pci_dev_mimic[pci_dev_index].name, "WAN Port");
			pci_dev_mimic[pci_dev_index].irq = 29;
		}
		else if (1 == pci_dev_index) {
			//strcpy(pci_dev_mimic[pci_dev_index].name, "LAN Port");
			pci_dev_mimic[pci_dev_index].irq = 22;
		}
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
		else {
			//strcpy(pci_dev_mimic[pci_dev_index].name, "HPNA Port");
			pci_dev_mimic[pci_dev_index].irq = 14;
		}
#endif
#ifdef	DEBUG_THIS
		DRV_INFO("%s: set ks8695_probe(%d)", __FUNCTION__, pci_dev_index);
#endif
#if 0
		/* We MUST not have interrupts off when calling through this */
		spin_lock_irqsave(&eth_lock, flags);
#endif
		/* we don't use pci id field, so set it to NULL */
		nRet = ks8695_probe(&pci_dev_mimic[pci_dev_index], NULL);
#if 0
		spin_unlock_irqrestore(&eth_lock, flags);
#endif
		/* if error happened */
		if (nRet) {
			DRV_ERR("%s: ks8695_probe(%d) failed, error code = 0x%08x", __FUNCTION__, pci_dev_index, nRet);
			break;
		}
	}

	return nRet;
}

/*
 * hook_irqs
 *	This function is used to hook irqs associated to given DMA type
 *
 * Argument(s)
 *	netdev	pointer to netdev structure.
 *	req		request or free interrupts
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static int hook_irqs(struct net_device *netdev, int req)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
#ifndef FAST_POLL
	int	i;
#endif

	switch (DI.usDMAId) {
	default:
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	case DMA_HPNA:
#endif
	case DMA_LAN:
		break;

	case DMA_WAN:
		if (DI.uLinkIntMask & INT_WAN_LINK) {
			if (req) {
#ifndef	USE_FIQ
				if (request_irq(31, ks8695_isr_link, SA_SHIRQ, "WAN eth", netdev)) {
#else
				if (request_irq(31, ks8695_isr_link, SA_SHIRQ | SA_INTERRUPT, "WAN eth", netdev)) {
#endif
					return -EBUSY;
				}
			}
			else {
				free_irq(31, netdev);
			}
		}
		break;
	}

#ifdef FAST_POLL
	if (req)
		fast_timer_add(ks8695_fast_poll, netdev);
	else
		fast_timer_remove(ks8695_fast_poll, netdev);
#else
	/* each DMA has 6 interrupt bits associated, except WAN which has one extra, INT_WAN_LINK */
	for (i = 0; i < 6; i++) {
		if (DI.uIntMask & (1L << (DI.uIntShift + i))) {
			if (req) {
#ifndef	USE_FIQ
				if (request_irq(i + DI.uIntShift, &ks8695_isr, SA_SHIRQ, "LAN eth", netdev)) {
#else
				if (request_irq(i + DI.uIntShift, &ks8695_isr, SA_SHIRQ | SA_INTERRUPT, "LAN eth", netdev)) {
#endif
					return -EBUSY;
				}
			}
			else {
				free_irq(i + DI.uIntShift, netdev);
			}
		}
	}
#endif /* FAST_POLL */

	return 0;
}

/*
 *	Determine MAC addresses for ethernet ports.
 */
#if defined(CONFIG_MACH_CM4002) || defined(CONFIG_MACH_CM4008) || \
    defined(CONFIG_MACH_CM41xx)
#define	MAC_OFFSET	0x1c000
#define MAC_DEFAULT	0x00, 0x13, 0xc6, 0x00, 0x00, 0x00
#elif defined(CONFIG_MACH_LITE300) || defined(CONFIG_MACH_SE4200)
#define	MAC_OFFSET	0x0c000
#define	MAC_DEFAULT	0x00, 0xd0, 0xcf, 0x00, 0x00, 0x00
#endif

#ifdef MAC_OFFSET
/*
 *	Ideally we want to use the MAC addresses stored in flash.
 *	But we do some sanity checks in case they are not present
 *	first.
 */
void ks8695_getmac(unsigned char *dst, int index)
{
	unsigned char dm[] = { MAC_DEFAULT };
	unsigned char *src, *mp, *ep;
	int i;

	/* Construct a default MAC address just in case */
	dm[ETH_LENGTH_OF_ADDRESS-1] = index;
	src = &dm[0];

	ep = ioremap(0x02000000, 0x20000);
	if (ep) {
		/* Check if flash MAC is valid */
		mp = ep + MAC_OFFSET + (index * ETH_LENGTH_OF_ADDRESS);
		for (i = 0; (i < ETH_LENGTH_OF_ADDRESS); i++) {
			if ((mp[i] != 0) && (mp[i] != 0xff)) {
				src = mp;
				break;
			}
		}
	}

	memcpy(dst, src, ETH_LENGTH_OF_ADDRESS);

	if (ep)
		iounmap(ep);
}
#else
void ks8695_getmac(unsigned char *dst, int index)
{
	static char macs[] = {
		0x00, 0x10, 0xa1, 0x00, 0x10, 0x01,
	};
	memcpy(dst, macs, ETH_LENGTH_OF_ADDRESS);
	macs[ETH_LENGTH_OF_ADDRESS-1]++;
}
#endif

/*
 * ks8695_init_module
 *	This function is the first routine called when the driver is loaded.
 *
 * Argument(s)
 *	NONE.
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_init_module(void)
{
	int	nRet;

	/* Print the driver ID string and copyright notice */
	DRV_INFO(" %s, version %s,  %s", 
		ks8695_driver_string, ks8695_driver_version, ks8695_copyright);

#ifdef	DEBUG_THIS
	DRV_INFO(" IO Address=0x%x", KS8695_IO_VIRT));
#endif

	nRet = ks8695_module_probe();

	return nRet;
}

module_init(ks8695_init_module);

/*
 * ks8695_exit_module
 *	This function is called just before the driver is removed from memory.
 *
 * Argument(s)
 *	NONE.
 *
 * Return(s)
 *	NONE.
 */
void ks8695_exit_module(void)
{
#ifdef	DEBUG_THIS
	DRV_INFO("%s: pci_dev_index=%d", __FUNCTION__, pci_dev_index);
#endif

	{	
		int i;

#ifdef	__KS8695_CACHE_H
		if (ICacheLockdown)
			ks8695_icache_unlock();
#endif
		for (i = pci_dev_index; i > 0; i--) {
			ks8695_remove(&pci_dev_mimic[i - 1]);
		}
		pci_dev_index = 0;
	}
}

module_exit(ks8695_exit_module);

/*
 * ks8695_probe
 *	This function initializes an adapter identified by a pci_dev
 *  structure. Note that KS8695 eval board doesn't have PCI bus at all,
 *	but the driver uses that since it was derived from a PCI based driver
 *	originally.
 *
 * Argument(s)
 *  pdev	pointer to PCI device information struct
 *  ent		pointer to PCI device ID structure (ks8695_pci_table)
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	PADAPTER_STRUCT Adapter;
	static int cards_found = 0;
	int	nRet;

	/* Register a new network interface and allocate private data
	   structure (ADAPTER_STRUCT) */
	netdev = alloc_etherdev(sizeof(ADAPTER_STRUCT));
	if (NULL == netdev) {
		DRV_ERR("alloc_etherdev failed");
		return -ENOMEM;
	}

	Adapter = (PADAPTER_STRUCT) netdev_priv(netdev);
	/*memset(Adapter, 0, sizeof(ADAPTER_STRUCT));*/
	Adapter->netdev = netdev;
	Adapter->pdev = pdev;

	/* chain the ADAPTER_STRUCT into the list */
	if (ks8695_adapter_list)
		ks8695_adapter_list->prev = Adapter;
	Adapter->next = ks8695_adapter_list;
	ks8695_adapter_list = Adapter;

	/* simply tell the network interface we are using this irq, but the driver
	   use more for each DMA, look for /proc/interrupts for details */
	netdev->irq = pdev->irq;

	Adapter->stDMAInfo.nBaseAddr = KS8695_IO_VIRT;
	netdev->mem_start = KS8695_IO_VIRT;
	netdev->mem_end = netdev->mem_start + 0xffff;

/* #ifdef	DEBUG_THIS */
	DRV_INFO("VA = 0x%08x, PA=0x%08x", Adapter->stDMAInfo.nBaseAddr, KS8695_IO_BASE);
/* #endif */

	/* set up function pointers to driver entry points */
	netdev->open               = &ks8695_open;
	netdev->stop               = &ks8695_close;
	netdev->hard_start_xmit    = &ks8695_xmit_frame;
	netdev->get_stats          = &ks8695_get_stats;
	netdev->set_multicast_list = &ks8695_set_multi;
	netdev->set_mac_address    = &ks8695_set_mac;
	netdev->change_mtu         = &ks8695_change_mtu;
	netdev->do_ioctl           = &ks8695_ioctl;
	netdev->tx_timeout         = &ks8695_tx_timeout;
	netdev->watchdog_timeo     = 10*HZ;
	if (DI.bTxChecksum) 
		netdev->features   |= NETIF_F_HW_CSUM;

#ifdef	CONFIG_ARCH_KS8695P
	Adapter->rev = (KS8695_READ_REG(KS8695_REVISION_ID) >> 0x4) & 0xf;
#else
#ifdef	KS8695X
	Adapter->rev = (KS8695_READ_REG(KS8695_REVISION_ID) >> 0x4) & 0xf;
#else
	Adapter->rev = 0;
#endif	/*KS8695X*/
#endif

	/* the card will tell which driver it will be */
	Adapter->bd_number = cards_found;

	if (WANPORT == cards_found) {
		/* for WAN */
		DI.usDMAId = DMA_WAN;
		DI.nOffset = DMA_WAN;
		DI.uIntMask = INT_WAN_MASK;
		DI.uLinkIntMask = INT_WAN_LINK;

#ifndef	USE_RX_UNAVAIL
		/* clear Rx buf unavail bit */
		DI.uIntMask &= ~BIT(27);
#endif

#ifndef	USE_TX_UNAVAIL
		/* clear Tx buf unavail bit */
		DI.uIntMask &= ~BIT(28);
#endif
		/* DMA's stop bit is a little bit different compared with KS9020, so disable them first */
		DI.uIntMask &= ~BIT(26);	
		DI.uIntMask &= ~BIT(25);
		DI.uIntShift = 25;

		/* set default mac address for WAN */
		ks8695_getmac(DI.stMacStation, cards_found);

	} else if (LANPORT == cards_found) {
		/* for LAN */
		DI.usDMAId = DMA_LAN;
		DI.nOffset = DMA_LAN;
		DI.uIntMask = INT_LAN_MASK;

#ifndef	USE_RX_UNAVAIL
		/* clear Rx buf unavail bit */
		DI.uIntMask &= ~BIT(14);
#endif

#ifndef	USE_TX_UNAVAIL
		/* clear Tx buf unavail bit */
		DI.uIntMask &= ~BIT(15);
#endif
		DI.uIntMask &= ~BIT(13);	
		DI.uIntMask &= ~BIT(12);
		DI.uIntShift = 12;

		ks8695_getmac(DI.stMacStation, cards_found);

#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	} else if (HPNAPORT == cards_found) {
		/* for HPNA */
		DI.usDMAId = DMA_HPNA;
		DI.nOffset = DMA_HPNA;
		DI.uIntMask = INT_HPNA_MASK;
#ifdef	RX_TASK
		/* clear Rx buf unavail bit */
		DI.uIntMask &= ~BIT(20);
#endif
#ifndef	USE_TX_UNAVAIL
		/* clear Tx buf unavail bit */
		/* if use Tx coalescing, don't disable Tx Complete bit */
		DI.uIntMask &= ~BIT(21);
#else
		/* clear Tx Completed bit */
		DI.uIntMask &= ~BIT(23);
#endif
		DI.uIntMask &= ~BIT(19);	
		DI.uIntMask &= ~BIT(18);

		DI.uIntShift = 18;
		ks8695_getmac(DI.stMacStation, cards_found);
#endif
	} else {
		DRV_ERR("%s: card id out of range (%d)", __FUNCTION__, cards_found);
		return -ENODEV;
	}

	nRet = SoftwareInit(Adapter);
	if (nRet) {
		DRV_ERR("%s: SoftwareInit failed", __FUNCTION__);
		ks8695_remove(pdev);
		return nRet;
	}
	CheckConfigurations(Adapter);

	/* reset spinlock */
	DI.lock = SPIN_LOCK_UNLOCKED;
	DI.lock_refill = SPIN_LOCK_UNLOCKED;

	/* finally, we get around to setting up the hardware */
	if (HardwareInit(Adapter) < 0) {
		DRV_ERR("%s: HardwareInit failed", __FUNCTION__);
		ks8695_remove(pdev);
		return -ENODEV;
	}
	cards_found++;

#if defined(CONFIG_MACH_LITE300)
	/* set LED 0 for link/activities */
	swSetLED(Adapter, FALSE, LED_LINK_ACTIVITY);
#else
	/* set LED 0 for speed */
	swSetLED(Adapter, FALSE, LED_SPEED);
#endif
	/* set LED 1 for link/activities */
	swSetLED(Adapter, TRUE, LED_LINK_ACTIVITY);

	if ((nRet = register_netdev(netdev))) {
		return -EIO;
	}

	return 0;
}

/*
 * ks8695_remove
 *	This function is called by the PCI subsystem to alert the driver
 *  that it should release a PCI device. It is called to clean up from
 *  a failure in ks8695_probe.
 *
 * Argument(s)
 *  pdev	pointer to PCI device information struct
 *
 * Return(s)
 *	NONE.
 */
void ks8695_remove(struct pci_dev *pdev)
{
	struct net_device *netdev;
	PADAPTER_STRUCT Adapter;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* find the Adapter struct that matches this PCI device */
	for (Adapter = ks8695_adapter_list; Adapter != NULL; Adapter = Adapter->next) {
		if (Adapter->pdev == pdev)
			break;
	}
	/* if no match is found */
	if (Adapter == NULL)
		return;

#ifdef	DEBUG_THIS
	DRV_INFO("%s: match found, bd_num = %d", __FUNCTION__, Adapter->bd_number);
#endif

	netdev = Adapter->netdev;

	if (test_bit(KS8695_BOARD_OPEN, &Adapter->flags))
		ks8695_close(netdev);

	/* remove from the adapter list */
	if (ks8695_adapter_list == Adapter)
		ks8695_adapter_list = Adapter->next;
	if (Adapter->next != NULL)
		Adapter->next->prev = Adapter->prev;
	if (Adapter->prev != NULL)
		Adapter->prev->next = Adapter->next;

	/* free the net_device _and_ ADAPTER_STRUCT memory */
	unregister_netdev(netdev);
	kfree(netdev);
}

/*
 * CheckConfigurations
 *	This function checks all command line paramters for valid user
 *  input. If an invalid value is given, or if no user specified
 *  value exists, a default value is used.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	NONE.
 */
static void CheckConfigurations(PADAPTER_STRUCT Adapter)
{
	int board = Adapter->bd_number, i;

#ifdef	DEBUG_THIS
	DRV_INFO("%s (board number = %d)", __FUNCTION__, board);
#endif

	/* Transmit Descriptor Count */
	if (TxDescriptors[board] == OPTION_UNSET) {
		DI.nTxDescTotal = TXDESC_DEFAULT;	/* 256 | 128 | 64(d) */
	} else if ((TxDescriptors[board] > TXDESC_MAX) &&
			   (TxDescriptors[board] < TXDESC_MIN)) {
		DRV_WARN("Invalid TxDescriptor specified (%d), using default %d",
			   TxDescriptors[board], TXDESC_DEFAULT);
		DI.nTxDescTotal = TXDESC_DEFAULT;
	} else {
		DRV_INFO("User specified TxDescriptors %d is used", TxDescriptors[board]);
		DI.nTxDescTotal = TxDescriptors[board];
	}
	/* Tx coalescing, currently can only be used if buffer unavailable bit is set */
	DI.nTransmitCoalescing = (DI.nTxDescTotal >> 3);

	/* Receive Descriptor Count */
	if (RxDescriptors[board] == OPTION_UNSET) {
		DI.nRxDescTotal = RXDESC_DEFAULT;		/* 256(d) | 128 | 64 */
	} else if ((RxDescriptors[board] > RXDESC_MAX) ||
			   (RxDescriptors[board] < RXDESC_MIN)) {
		DRV_WARN("Invalid RxDescriptor specified (%d), using default %d",
			   RxDescriptors[board], RXDESC_DEFAULT);
	} else {
		DRV_INFO("User specified RxDescriptors %d is used", RxDescriptors[board]);
		DI.nRxDescTotal = RxDescriptors[board];
	}

	/* Receive Checksum Offload Enable */
	if (RxChecksum[board] == OPTION_UNSET) {
		DI.bRxChecksum = RXCHECKSUM_DEFAULT;		/* enabled */
	} else if ((RxChecksum[board] != OPTION_ENABLED) && (RxChecksum[board] != OPTION_DISABLED)) {
		DRV_INFO("Invalid RxChecksum specified (%i), using default of %i",
			RxChecksum[board], RXCHECKSUM_DEFAULT);
		DI.bRxChecksum = RXCHECKSUM_DEFAULT;
	} else {
		DRV_INFO("Receive Checksum Offload %s",
			RxChecksum[board] == OPTION_ENABLED ? "Enabled" : "Disabled");
		DI.bRxChecksum = RxChecksum[board];
	}

	/* Transmit Checksum Offload Enable configuration */
	if (OPTION_UNSET == TxChecksum[board]) {
		DI.bTxChecksum = TXCHECKSUM_DEFAULT;		/* disabled */
	} else if ((OPTION_ENABLED != TxChecksum[board]) && (OPTION_DISABLED != TxChecksum[board])) {
		DRV_INFO("Invalid TxChecksum specified (%i), using default of %i",
			TxChecksum[board], TXCHECKSUM_DEFAULT);
		DI.bTxChecksum = TXCHECKSUM_DEFAULT;
	} else {
		DRV_INFO("Transmit Checksum Offload specified %s",
			TxChecksum[board] == OPTION_ENABLED ? "Enabled" : "Disabled");
		DI.bTxChecksum = TxChecksum[board];
	}

	/* Flow Control */
	if (FlowControl[board] == OPTION_UNSET) {
		DI.bRxFlowCtrl = FLOWCONTROL_DEFAULT;		/* enabled */
	} else if ((OPTION_ENABLED != FlowControl[board]) && (OPTION_DISABLED != FlowControl[board])) {
		DRV_INFO("Invalid FlowControl specified (%i), using default %i",
			   FlowControl[board], FLOWCONTROL_DEFAULT);
		DI.bRxFlowCtrl = FLOWCONTROL_DEFAULT;
	} else {
		DRV_INFO("Flow Control %s", FlowControl[board] == OPTION_ENABLED ?
			"Enabled" : "Disabled");
		DI.bRxFlowCtrl = FlowControl[board];
	}
	/* currently Tx control flow shares the setting of Rx control flow */
	DI.bTxFlowCtrl = DI.bRxFlowCtrl;

	/* Perform PHY PowerDown Reset instead of soft reset, the Option function can 
	   be overwritten by user later */
	DI.bPowerDownReset = TRUE;

	/* Programmable Burst Length */
	if (OPTION_UNSET == TxPBL[board]) {
		DI.byTxPBL = PBL_DEFAULT;		/* FIFO size */
	} else if ((0 != TxPBL[board]) && (1 != TxPBL[board]) && (2 != TxPBL[board]) && (4 != TxPBL[board]) &&
		(8 != TxPBL[board]) && (16 != TxPBL[board]) && (32 != TxPBL[board])) {
		DRV_INFO("Invalid TX Programmable Burst Length specified (%i), using default of %i",
			TxPBL[board], PBL_DEFAULT);
		DI.byTxPBL = PBL_DEFAULT;
	} else {
		DRV_INFO("Programmable Burst Length specified %d bytes", TxPBL[board]);
		DI.byTxPBL = TxPBL[board];
	}

	if (OPTION_UNSET == RxPBL[board]) {
		DI.byRxPBL = PBL_DEFAULT;		/* FIFO size */
	} else if ((0 != TxPBL[board]) && (1 != RxPBL[board]) && (2 != RxPBL[board]) && (4 != RxPBL[board]) &&
		(8 != RxPBL[board]) && (16 != RxPBL[board]) && (32 != RxPBL[board])) {
		DRV_INFO("Invalid TX Programmable Burst Length specified (%i), using default of %i",
			RxPBL[board], PBL_DEFAULT);
		DI.byRxPBL = PBL_DEFAULT;
	} else {
		DRV_INFO("Programmable Burst Length specified %d bytes", RxPBL[board]);
		DI.byRxPBL = RxPBL[board];
	}

	/* User speed and/or duplex options */
	if (Duplex[board] == OPTION_UNSET && Speed[board] == OPTION_UNSET) {
		DI.usCType[0] = SW_PHY_DEFAULT;
	}
	else {
		switch (Speed[board]) {
			case 10:
				if (Duplex[board])
					DI.usCType[0] = SW_PHY_10BASE_T_FD;		/* 10Base-TX Full Duplex */
				else {
					/* don't advertise flow control in half duplex case */
					if (DMA_WAN == DI.usDMAId) {
						DI.bRxFlowCtrl = FALSE;
						DI.bTxFlowCtrl = FALSE;
					}
					DI.usCType[0] = SW_PHY_10BASE_T;		/* 10Base-T Half Duplex */
				}
				break;

			case 100:
			default:
				if (Duplex[board])
					DI.usCType[0] = SW_PHY_100BASE_TX_FD;	/* 100Base-TX Full Duplex */
				else {
					/* don't advertise flow control in half duplex case */
					if (DMA_WAN == DI.usDMAId) {
						DI.bRxFlowCtrl = FALSE;
						DI.bTxFlowCtrl = FALSE;
					}
					DI.usCType[0] = SW_PHY_100BASE_TX;		/* 100Base-TX Half Duplex */
				}
				break;
		}
	}

	if (DMA_LAN == DI.usDMAId) {
		/*TEMP, currently assume all other ports share same configuration with
		  first one, will add more options for LAN ports later */
		for (i = 1; i < SW_MAX_LAN_PORTS; i++) {
			DI.usCType[i] = DI.usCType[0];
		}

		/* initialize some variables which do not have user configurable options */
		for (i = 0; i <= SW_MAX_LAN_PORTS; i++) {
			DPI[i].byCrossTalkMask = 0x1f;
			DPI[i].bySpanningTree = SW_SPANNINGTREE_ALL;
			DPI[i].byDisableSpanningTreeLearn = FALSE;
		}

		/* set default as direct mode for port 5, so no lookup table is checking */
		DI.bRxDirectMode = FALSE;
		DI.bTxRreTagMode = FALSE;

		DI.bPort5FlowCtrl = DI.bRxFlowCtrl;
		DI.bPortsFlowCtrl = DI.bRxFlowCtrl;
	}
}

/*
 * SoftwareInit
 *	This function initializes the Adapter private data structure.
 *
 * Argument(s)
 *  Adapter	pointer to ADAPTER_STRUCT structure
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static int SoftwareInit(PADAPTER_STRUCT Adapter)
{
	struct net_device *netdev = Adapter->netdev;

	/* Initial Receive Buffer Length */
	if ((netdev->mtu + ENET_HEADER_SIZE + ETH_CRC_LENGTH) <= BUFFER_1568) {
		DI.uRxBufferLen = BUFFER_1568;	/* 0x620 */
	}
	else {
		DI.uRxBufferLen = BUFFER_2048;	/* 0x800 */
	}

	/* please update link status within watchdog routine */
	DI.bLinkChanged[0] = TRUE;
	if (DMA_LAN == DI.usDMAId) {	/* if LAN driver, 3 more ports */
		DI.bLinkChanged[1] = TRUE;
		DI.bLinkChanged[2] = TRUE;
		DI.bLinkChanged[3] = TRUE;
	}

	return 0;
}

/*
 * HardwareInit
 *	This function initializes the hardware to a configuration as specified by the
 *  Adapter structure, including mac I/F, switch engine, IRQ and others.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT structure
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static int HardwareInit(PADAPTER_STRUCT Adapter)
{
#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* note that chip reset should only take once 
	   if three driver instances are used for WAN, LAN and HPNA respectively for KS8695
	   For KS8695P only two driver instances are used (WAN and LAN) */
	if (!ks8695_ChipInit(Adapter, TRUE)) {
		DRV_ERR("Hardware Initialization Failed");
		return -1;
	}

	return 0;
}

/*
 * ks8695_open
 *	This function is called when a network interface is made
 *  active by the system (IFF_UP). At this point all resources needed
 *  for transmit and receive operations are allocated, the interrupt
 *  handler is registered with the OS, the watchdog timer is started,
 *  and the stack is notified when the interface is ready.
 *
 * Argument(s)
 *  netdev		pointer to net_device struct
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_open(struct net_device *netdev)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* prevent multiple opens on same driver instance */
	if (test_and_set_bit(KS8695_BOARD_OPEN, &Adapter->flags)) {
		return -EBUSY;
	}
#ifdef FAST_POLL
	ks8695_poll_ready = 0;
#endif

	/* stop Tx/Rx and disable interrupt */
	macStopAll(Adapter);
	if (DMA_LAN == DI.usDMAId) {
		swEnableSwitch(Adapter, FALSE);
	}

	if (HardwareInit(Adapter) < 0) {
		clear_bit(KS8695_BOARD_OPEN, &Adapter->flags);
		return -EBUSY;
	}

	/* allocate transmit descriptors */
	if (AllocateTxDescriptors(Adapter) != 0) {
		clear_bit(KS8695_BOARD_OPEN, &Adapter->flags);
		return -ENOMEM;
	}
	/* set base address for Tx DMA */
	KS8695_WRITE_REG(REG_TXBASE + DI.nOffset, cpu_to_le32(DI.TxDescDMA));
	macStartTx(Adapter, TRUE);

	/* allocate receive descriptors and buffers */
	if (AllocateRxDescriptors(Adapter) != 0) {
		FreeTxDescriptors(Adapter);
		clear_bit(KS8695_BOARD_OPEN, &Adapter->flags);
		return -ENOMEM;
	}
	/* set base address for Rx DMA */
	KS8695_WRITE_REG(REG_RXBASE + DI.nOffset, cpu_to_le32(DI.RxDescDMA));
	macStartRx(Adapter, TRUE);

	/* hook the interrupt */
	if (hook_irqs(netdev, TRUE)) {
		DRV_ERR("%s: hook_irqs failed", __FUNCTION__);
		clear_bit(KS8695_BOARD_OPEN, &Adapter->flags);
		FreeTxDescriptors(Adapter);
		FreeRxDescriptors(Adapter);
		return -EBUSY;
	}

	/* fill Rx ring with sk_buffs */
	ReceiveBufferFill((unsigned long)Adapter);

#ifdef	RX_TASK
	/* if use task based rx process, initialize it */
	/* Initialize the tasklet again may crash the kernel. */
	if ( DI.rx_tasklet.func == ReceiveProcessTask ) {
		tasklet_enable( &DI.rx_tasklet );
	}
	else
		tasklet_init(&DI.rx_tasklet, ReceiveProcessTask, (unsigned long)Adapter);
#endif
#ifdef	TX_TASK
	/* if use task based tx process, initialize it */
	/* Initialize the tasklet again may crash the kernel. */
	if ( DI.tx_tasklet.func == TransmitProcessTask ) {
		tasklet_enable( &DI.tx_tasklet );
	}
	else
		tasklet_init(&DI.tx_tasklet, TransmitProcessTask, (unsigned long)Adapter);
#endif

	/* Set the watchdog timer for 2 seconds */
	init_timer(&Adapter->timer_id);

	Adapter->timer_id.function = &ks8695_watchdog;
	Adapter->timer_id.data = (unsigned long) netdev;
	mod_timer(&Adapter->timer_id, (jiffies + WATCHDOG_TICK * HZ));

	/* stats accumulated while down are dropped
	 * this does not clear the running total */
	swResetSNMPInfo(Adapter);

	if (DMA_LAN == DI.usDMAId) {
		swEnableSwitch(Adapter, TRUE);
	}
	macEnableInterrupt(Adapter, TRUE);

	/* clear tbusy bit */
	netif_start_queue(netdev);

#ifdef FAST_POLL
	ks8695_poll_ready++;
#endif
	return 0;
}

/*
 * ks8695_close
 *	This function is called when an interface is de-activated by the network 
 *	module (IFF_DOWN).
 *
 * Argument(s)
 *  netdev		pointer to net_device struct
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_close(struct net_device *netdev)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	if (!test_bit(KS8695_BOARD_OPEN, &Adapter->flags))
		return 0;

	/* stop all */
	macStopAll(Adapter);
	if (DMA_LAN == DI.usDMAId) {
		swEnableSwitch(Adapter, FALSE);
	}

	netif_stop_queue(netdev);
	hook_irqs(netdev, FALSE);
	del_timer(&Adapter->timer_id);

#ifdef	RX_TASK
	tasklet_disable(&DI.rx_tasklet);
	DI.rx_scheduled = FALSE;
#endif
#ifdef	TX_TASK
	tasklet_disable(&DI.tx_tasklet);
	DI.tx_scheduled = FALSE;
#endif
	FreeTxDescriptors(Adapter);
	FreeRxDescriptors(Adapter);

	clear_bit(KS8695_BOARD_OPEN, &Adapter->flags);

	return 0;
}

/*
 * InitTxRing
 *	This function is used to initialize Tx descriptor ring.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE
 */
void InitTxRing(PADAPTER_STRUCT Adapter)
{
	int i;
	TXDESC	*pTxDesc = DI.pTxDescriptors;
	UINT32	uPA = DI.TxDescDMA;

	for (i = 0; i < DI.nTxDescTotal - 1; i++, pTxDesc++) {
		uPA += sizeof(TXDESC);		/* pointer to next Tx Descriptor */
		pTxDesc->TxDMANextPtr = cpu_to_le32(uPA);
	}
	/* last descriptor should point back to the beginning */
	pTxDesc->TxDMANextPtr = cpu_to_le32(DI.TxDescDMA);
	pTxDesc->TxFrameControl |= cpu_to_le32(TFC_TER);
}


/*
 * KS8695's internal ethernet driver doesn't use PCI bus at all. As a resumt
 * call these set of functions instead
 */
static void *consistent_alloc_ex(int gfp, size_t size, dma_addr_t *dma_handle)
{
	struct page *page, *end, *free;
	unsigned long order;
	void *ret, *virt;

	if (in_interrupt())
		BUG();

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * We could do with a page_to_phys here
	 */
	virt = page_address(page);
	*dma_handle = virt_to_phys(virt);
	ret = __ioremap(virt_to_phys(virt), size, 0);
	if (!ret)
		goto no_remap;

	/*
	 * free wasted pages.  We skip the first page since we know
	 * that it will have count = 1 and won't require freeing.
	 * We also mark the pages in use as reserved so that
	 * remap_page_range works.
	 */
	page = virt_to_page(virt);
	free = page + (size >> PAGE_SHIFT);
	end  = page + (1 << order);

	for (; page < end; page++) {
		if (page >= free)
			__free_page(page);
		else
			SetPageReserved(page);
	}
	return ret;

no_remap:
	__free_pages(page, order);
no_page:
	return NULL;
}

/*
 * free a page as defined by the above mapping.  We expressly forbid
 * calling this from interrupt context.
 */
static void consistent_free_ex(void *vaddr, size_t size, dma_addr_t handle)
{
	struct page *page, *end;
	void *virt;

	if (in_interrupt())
		BUG();

	virt = phys_to_virt(handle);

	/*
	 * More messing around with the MM internals.  This is
	 * sick, but then so is remap_page_range().
	 */
	size = PAGE_ALIGN(size);
	page = virt_to_page(virt);
	end = page + (size >> PAGE_SHIFT);

	for (; page < end; page++)
		ClearPageReserved(page);

	__iounmap(vaddr);
}

/*
 * AllocateTxDescriptors
 *	This function is used to allocate Tx descriptors, including allocate memory,
 *	alignment adjustment, variable initialization and so on.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static int AllocateTxDescriptors(PADAPTER_STRUCT Adapter)
{
	int size;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* allocate data buffers for transmission */
	size = sizeof(struct ks8695_buffer) * DI.nTxDescTotal;
	DI.pTxSkb = kmalloc(size, GFP_KERNEL);
	if (DI.pTxSkb == NULL) {
		return -ENOMEM;
	}
	memset(DI.pTxSkb, 0, size);

	/* round up to nearest 4K */
	size = KS8695_ROUNDUP(DI.nTxDescTotal * sizeof(TXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	DI.pTxDescriptors = consistent_alloc_ex(GFP_KERNEL | GFP_DMA, size, &DI.TxDescDMA);
	if (NULL == DI.pTxDescriptors) {
		kfree(DI.pTxSkb);
		DI.pTxSkb = NULL;
		return -ENOMEM;
	}

#ifdef	DEBUG_THIS
	DRV_INFO("TXDESC> DataBuf=0x%08x, Descriptor=0x%08x, PA=0x%08x", (UINT)DI.pTxSkb, (UINT)DI.pTxDescriptors, (UINT)DI.TxDescDMA);
#endif
	memset(DI.pTxDescriptors, 0, size);

	atomic_set(&DI.nTxDescAvail, DI.nTxDescTotal);
	DI.nTxDescNextAvail = 0;
	DI.nTxDescUsed = 0;
	DI.nTransmitCount = 0;
	DI.nTxProcessedCount = 0;
	DI.bTxNoResource = 0;

	InitTxRing(Adapter);

    return 0;
}

/*
 * InitRxRing
 *	This function is used to initialize Rx descriptor ring.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE
 */
void InitRxRing(PADAPTER_STRUCT Adapter)
{
	int i;
	RXDESC	*pRxDesc = DI.pRxDescriptors;
	UINT32	uPA = DI.RxDescDMA;

	for (i = 0; i < DI.nRxDescTotal - 1; i++, pRxDesc++) {
		uPA += sizeof(RXDESC);		/* pointer to next Rx Descriptor */
		pRxDesc->RxDMANextPtr = cpu_to_le32(uPA);
	}
	/* last descriptor should point back to the beginning */
	pRxDesc->RxDMANextPtr = cpu_to_le32(DI.RxDescDMA);
	pRxDesc->RxDMAFragLen &= cpu_to_le32(~RFC_RBS_MASK);
}

/*
 * AllocateRxDescriptors
 *	This function is used to setup Rx descriptors, including allocate memory, receive SKBs
 *	alignment adjustment, variable initialization and so on.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
static int AllocateRxDescriptors(PADAPTER_STRUCT Adapter)
{
	int size;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	size = sizeof(struct ks8695_buffer) * DI.nRxDescTotal;
	DI.pRxSkb = kmalloc(size, GFP_KERNEL);
	if (DI.pRxSkb == NULL) {
		return -ENOMEM;
	}
	memset(DI.pRxSkb, 0, size);

	/* Round up to nearest 4K */
	size = KS8695_ROUNDUP(DI.nRxDescTotal * sizeof(RXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	DI.pRxDescriptors = consistent_alloc_ex(GFP_KERNEL | GFP_DMA, size, &DI.RxDescDMA);
	if (NULL == DI.pRxDescriptors) {
		kfree(DI.pRxSkb);
		DI.pRxSkb = NULL;
		return -ENOMEM;
	}

#ifdef	DEBUG_THIS
	DRV_INFO("RXDESC> DataBuf=0x%08x, Descriptor=0x%08x, PA=0x%08x", 
			(UINT)DI.pRxSkb, (UINT)DI.pRxDescriptors, (UINT)DI.RxDescDMA);
#endif

	memset(DI.pRxDescriptors, 0, size);

	DI.nRxDescNextAvail = 0;
	atomic_set(&DI.RxDescEmpty, DI.nRxDescTotal);
	DI.nRxDescNextToFill = 0;

	InitRxRing(Adapter);

	return 0;
}

/*
 * FreeTxDescriptors
 *	This function is used to free Tx resources.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE.
 */
static void FreeTxDescriptors(PADAPTER_STRUCT Adapter)
{
	int size;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	CleanTxRing(Adapter);

	kfree(DI.pTxSkb);
	DI.pTxSkb = NULL;

	size = KS8695_ROUNDUP(DI.nTxDescTotal * sizeof(TXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	consistent_free_ex((void *)DI.pTxDescriptors, size, DI.TxDescDMA);
	DI.pTxDescriptors = NULL;
	DI.TxDescDMA = 0;
}

/*
 * CleanTxRing
 *	This function is used to go through Tx descriptor list and clean up any pending resources.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE.
 */
static void CleanTxRing(PADAPTER_STRUCT Adapter)
{
	unsigned long size;
	TXDESC	*pTxDesc = DI.pTxDescriptors;
	int i;

	/* free pending sk_buffs if any */
	for (i = 0; i < DI.nTxDescTotal; i++, pTxDesc++) {
		if (NULL != DI.pTxSkb[i].skb) {
			dev_kfree_skb(DI.pTxSkb[i].skb);
			DI.pTxSkb[i].skb = NULL;

			/* reset corresponding Tx Descriptor structure as well */
			pTxDesc->TxDMAFragAddr = 0;
			pTxDesc->TxOwnBit = 0;
			pTxDesc->TxFrameControl = 0;
		}
	}
	DI.nTransmitCount = 0;
	DI.nTxProcessedCount = 0;

	size = sizeof(struct ks8695_buffer) * DI.nTxDescTotal;
	memset(DI.pTxSkb, 0, size);

	size = KS8695_ROUNDUP(DI.nTxDescTotal * sizeof(TXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	memset(DI.pTxDescriptors, 0, size);
	atomic_set(&DI.nTxDescAvail, DI.nTxDescTotal);
	DI.nTxDescNextAvail = 0;
	DI.nTxDescUsed = 0;

	/* for safety!!! */
	KS8695_WRITE_REG(REG_TXBASE + DI.nOffset, 0);
}

/*
 * FreeRxDescriptors
 *	This function is used to free Rx resources.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE.
 */
static void FreeRxDescriptors(PADAPTER_STRUCT Adapter)
{
	int size;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

#if 0
#ifdef	RX_TASK
	tasklet_disable(&DI.rx_tasklet);
	DI.rx_scheduled = FALSE;
#endif
#ifdef	TX_TASK
	tasklet_disable(&DI.tx_tasklet);
	DI.tx_scheduled = FALSE;
#endif
#endif

	CleanRxRing(Adapter);

	kfree(DI.pRxSkb);
	DI.pRxSkb = NULL;

	size = KS8695_ROUNDUP(DI.nRxDescTotal * sizeof(RXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	consistent_free_ex((void *)DI.pRxDescriptors, size, DI.RxDescDMA);
	DI.pRxDescriptors = NULL;
	DI.RxDescDMA = 0;
}

/*
 * CleanRxRing
 *	This function is used to go through Rx descriptor list and clean up any pending resources.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE.
 */
static void CleanRxRing(PADAPTER_STRUCT Adapter)
{
	unsigned long size;
	RXDESC	*pRxDesc = DI.pRxDescriptors;
	int i;

	/* Free pending sk_buffs if any */
	for (i = 0; i < DI.nRxDescTotal; i++, pRxDesc++) {
		if (DI.pRxSkb[i].skb != NULL) {
			dev_kfree_skb(DI.pRxSkb[i].skb);
			DI.pRxSkb[i].skb = NULL;

			/* reset corresponding Rx Descriptor structure as well */
			pRxDesc->RxFrameControl &= cpu_to_le32(~(RFC_FRAMECTRL_MASK | DESC_OWN_BIT));
			pRxDesc->RxDMAFragLen = 0;
			pRxDesc->RxDMAFragAddr = 0;
		}
	}

	size = sizeof(struct ks8695_buffer) * DI.nRxDescTotal;
	memset(DI.pRxSkb, 0, size);

	size = KS8695_ROUNDUP(DI.nRxDescTotal * sizeof(RXDESC) + DESC_ALIGNMENT, BUFFER_4K);
	memset(DI.pRxDescriptors, 0, size);
	atomic_set(&DI.RxDescEmpty, DI.nRxDescTotal);
	DI.nRxDescNextAvail = 0;
	DI.nRxDescNextToFill = 0;

	/* for safety!!! */
	KS8695_WRITE_REG(REG_RXBASE + DI.nOffset, 0);
}

/*
 * ks8695_set_multi
 *	This function is used to set Multicast and Promiscuous mode. It is 
 *	called whenever the multicast address list or the network interface
 *	flags are updated. This routine is resposible for configuring the 
 *	hardware for proper multicast, promiscuous mode, and all-multi behavior.
 *
 * Argument(s)
 *  Adapter		pointer to ADAPTER_STRUCT struct
 *
 * Return(s)
 *	NONE.
 */
void ks8695_set_multi(struct net_device *netdev)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	uint32_t uReg;
#if	0
	uint32_t HwLowAddress, HwHighAddress;
	uint16_t HashValue, HashReg, HashBit;
	struct dev_mc_list *mc_ptr;
#endif

	BOOLEAN	bRxStarted;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	bRxStarted = DI.bRxStarted;
	if (bRxStarted)
		macStartRx(Adapter, FALSE);

	/* read RX mode register in order to set hardware filter mode */
	uReg = KS8695_READ_REG(REG_RXCTRL + DI.nOffset);
	uReg |= DMA_UNICAST | DMA_BROADCAST;
	uReg &= ~(DMA_PROMISCUOUS | DMA_MULTICAST);

	if (netdev->flags & IFF_PROMISC) {
		uReg |= DMA_PROMISCUOUS;
	}
	if (netdev->flags & (IFF_ALLMULTI | IFF_MULTICAST)) {
		uReg |= DMA_MULTICAST;
	}

	KS8695_WRITE_REG(REG_RXCTRL + DI.nOffset, uReg);

	if (bRxStarted)
		macStartRx(Adapter, TRUE);

	ks8695_relink(Adapter);
}

/*
 * ks8695_watchdog
 *	This function is a timer callback routine for updating statistics infomration.
 *
 * Argument(s)
 *  data		pointer to net_device struct
 *
 * Return(s)
 *	NONE.
 */
void ks8695_watchdog(unsigned long data)
{
	struct net_device *netdev = (struct net_device *)data;
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	int	carrier;

	if (DMA_LAN == DI.usDMAId) {
		static int	nCheck = 0;

		if (nCheck++ > 6) {
			int	i;
			uint8_t	bLinkActive[SW_MAX_LAN_PORTS];

			nCheck = 0;
			for (i = 0; i < SW_MAX_LAN_PORTS; i++) {
				/* keep a track for previous link active state */
				bLinkActive[i] = DI.bLinkActive[i];

				carrier = swGetPhyStatus(Adapter, i);
				/* if current link state is not same as previous state, means link state changed */
				if (bLinkActive[i] != DI.bLinkActive[i]) {
					DI.bLinkChanged[i] = TRUE;
					ks8695_report_carrier(netdev, carrier);
				}
				/* note that since LAN doesn't have Interrupt bit for link status change */
				/* we have to check it to make sure if link is lost, restart it!!! */
				if (!DI.bLinkActive[i]) {
					swDetectPhyConnection(Adapter, i);
				}
			}
		}
	}
	else {
		if (!DI.bLinkActive[0]) {
			carrier = swGetPhyStatus(Adapter, 0);
			ks8695_report_carrier(netdev, carrier);
		}
#ifndef	TX_TASK
		/* handling WAN DMA sucked case if any */
		/*if (DMA_WAN == DI.usDMAId) {*/
		{	/* all driver ? */
			static int	nCount = 0;

			/* if no tx resource is reported */
			if (DI.bTxNoResource) {
				nCount++;
				/* if happened 5 times (WATCHDOG_TICK seconds * 5), means most likely, the WAN Tx DMA is died,
				   reset it again */
				if (nCount > 5) {
					DI.nResetCount++;
					ResetDma(Adapter);
					DI.bTxNoResource = FALSE;
					/* wake queue will call mark_bh(NET_BH) to resume tx */
					netif_wake_queue(netdev);
					nCount = 0;
				}
			}
		}
#endif
	}
	UpdateStatsCounters(Adapter);

	/* Reset the timer */
	mod_timer(&Adapter->timer_id, jiffies + WATCHDOG_TICK * HZ);
}

/*
 * ks8695_xmit_frame
 *	This function is used to called by the stack to initiate a transmit.
 *  The out of resource condition is checked after each successful Tx
 *  so that the stack can be notified, preventing the driver from
 *  ever needing to drop a frame.  The atomic operations on
 *  nTxDescAvail are used to syncronize with the transmit
 *  interrupt processing code without the need for a spinlock.
 *
 * Argument(s)
 *  skb			buffer with frame data to transmit
 *  netdev		pointer to network interface device structure
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	TXDESC *pTxDesc;
	int i, len;
	char *data;
	unsigned long flags;
	struct sk_buff* org_skb = skb;

#ifdef CONFIG_LEDMAN
	ledman_cmd(LEDMAN_CMD_SET,
		(netdev->name[3] == '0') ? LEDMAN_LAN1_TX : LEDMAN_LAN2_TX);
#endif

	/* Hardware has problem sending out short frames in which the first
	 * 4 bytes of MAC destination address are replaced with data at
	 * location 0x28 after sending out ICMP packets.
	 */
	if ( skb->len <= 48 ) {
		skb = dev_alloc_skb( 50 );
		if ( !skb ) {
			Adapter->net_stats.tx_aborted_errors++;
			return 1;
		}
		memcpy( skb->data, org_skb->data, org_skb->len );
		memset( &skb->data[ org_skb->len ], 0, 50 - org_skb->len );
		skb->len = 50;
		dev_kfree_skb( org_skb );
	}
	len = skb->len;
	data = skb->data;
#ifdef	DEBUG_THIS
	DRV_INFO("%s> len=%d", __FUNCTION__, len); 
#endif


	i = DI.nTxDescNextAvail;
	pTxDesc = &DI.pTxDescriptors[i];

	DI.pTxSkb[i].skb = skb;
	DI.pTxSkb[i].length = len;
	DI.pTxSkb[i].direction = PCI_DMA_TODEVICE;
	consistent_sync(data, DI.uRxBufferLen, PCI_DMA_TODEVICE);
	DI.pTxSkb[i].dma = virt_to_phys(data);

	/* set DMA buffer address */
	pTxDesc->TxDMAFragAddr = cpu_to_le32(DI.pTxSkb[i].dma);

#ifdef	PACKET_DUMP
	ks8695_dump_packet(Adapter, data, len, DI.uDebugDumpTxPkt);
#endif


#if	0
	if (DMA_LAN == DI.usDMAId) {
		/* may need to set SPN for IGCP for LAN driver, but do it later; */
	}
#endif

	local_irq_save(flags);
	/* note that since we have set the last Tx descriptor back to the first to form */
	/* a ring, there is no need to keep ring end flag for performance sake */
	/* clear some bits operation for optimization!!! */
#ifndef	USE_TX_UNAVAIL
	pTxDesc->TxFrameControl = cpu_to_le32((TFC_FS | TFC_LS | TFC_IC) | (len & TFC_TBS_MASK));
#else
	if ((DI.nTransmitCount + 1) % DI.nTransmitCoalescing) {
		pTxDesc->TxFrameControl = cpu_to_le32((TFC_FS | TFC_LS) | (len & TFC_TBS_MASK));
	}
	else {
		pTxDesc->TxFrameControl = cpu_to_le32((TFC_FS | TFC_LS | TFC_IC) | (len & TFC_TBS_MASK));
	}
#endif

	/* set own bit */
	pTxDesc->TxOwnBit = cpu_to_le32(DESC_OWN_BIT);

	/* eanble read transfer for the packet!!! */
	KS8695_WRITE_REG(REG_TXSTART + DI.nOffset, 1);

	/*atomic_dec(&DI.nTxDescAvail);*/
	/*__save_flags_cli(flags);*/
	DI.nTxDescAvail.counter--;
	/* update pending transimt packet count */
	DI.nTransmitCount++;
	local_irq_restore(flags);
	if (atomic_read(&DI.nTxDescAvail) <= 1) {
#ifdef	DEBUG_THIS
		if (DMA_WAN == DI.usDMAId)
			DRV_WARN("%s> no WAN tx descriptors available, tx suspended, nTransmitCount=%d", __FUNCTION__, DI.nTransmitCount);
		else if (DMA_LAN == DI.usDMAId)
			DRV_WARN("%s> no LAN tx descriptors available, tx suspended, nTransmitCount=%d", __FUNCTION__, DI.nTransmitCount);
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
		else
			DRV_WARN("%s> no HPNA tx descriptors available, tx suspended, nTransmitCount=%d", __FUNCTION__, DI.nTransmitCount);
#endif
#endif
		DI.bTxNoResource = TRUE;
		netif_stop_queue(netdev);
#ifdef	TX_TASK
		/* try eanble read transfer again */
		KS8695_WRITE_REG(REG_TXSTART + DI.nOffset, 1);
		if (FALSE == DI.tx_scheduled) {
			DI.tx_scheduled = TRUE;
			tasklet_hi_schedule(&DI.tx_tasklet);
		}
#endif
	}

	/* adv to next available descriptor */
	DI.nTxDescNextAvail = ++DI.nTxDescNextAvail % DI.nTxDescTotal;
	netdev->trans_start = jiffies;

	return 0;
}

/*
 * ks8695_get_stats
 *	This function is used to get NIC's SNMP staticstics.
 *
 * Argument(s)
 *  netdev		network interface device structure
 *
 * Return(s)
 *	pointer to net_device_stats structure
 */
struct net_device_stats *ks8695_get_stats(struct net_device *netdev)
{
    PADAPTER_STRUCT Adapter = netdev_priv(netdev);

#ifdef	DEBUG_THIS
    DRV_INFO("ks8695_get_stats");
#endif

    return &Adapter->net_stats;
}

/*
 * ks8695_change_mtu
 *	This function is use to change the Maximum Transfer Unit.
 *
 * Argument(s)
 *	netdev		pointer to net_device structure.
 *	new_mtu		new_mtu new value for maximum frame size
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_change_mtu(struct net_device *netdev, int new_mtu)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	uint32_t old_mtu = DI.uRxBufferLen;

	DRV_INFO("%s", __FUNCTION__);

	if (new_mtu <= DI.uRxBufferLen) {
		netdev->mtu = new_mtu;
		return 0;
	}

	if ((new_mtu < MINIMUM_ETHERNET_PACKET_SIZE - ENET_HEADER_SIZE) ||
		new_mtu > BUFFER_2048 - ENET_HEADER_SIZE) {
		DRV_ERR("%s> Invalid MTU setting", __FUNCTION__);
		return -EINVAL;
	}

	if (new_mtu <= BUFFER_1568 - ENET_HEADER_SIZE) {
		DI.uRxBufferLen = BUFFER_1568;
	} else {
		DI.uRxBufferLen = BUFFER_2048;
	}

	if (old_mtu != DI.uRxBufferLen) {
		/* put DEBUG_THIS after verification please */
		DRV_INFO("%s, old=%d, new=%d", __FUNCTION__, old_mtu, DI.uRxBufferLen);
		ResetDma(Adapter);
	}

	netdev->mtu = new_mtu;
	ks8695_relink(Adapter);

	return 0;
}

/*
 * ks8695_set_mac
 *	This function is use to change Ethernet Address of the NIC.
 *
 * Argument(s)
 *	netdev		pointer to net_device structure.
 *	p			pointer to sockaddr structure
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_set_mac(struct net_device *netdev, void *p)
{
	PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	struct sockaddr *addr = (struct sockaddr *)p;
	BOOLEAN	bRxStarted, bTxStarted;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	bRxStarted = DI.bRxStarted;
	bTxStarted = DI.bTxStarted;
	if (bRxStarted)
		macStartRx(Adapter, FALSE);
	if (bTxStarted)
		macStartTx(Adapter, FALSE);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(DI.stMacCurrent, addr->sa_data, netdev->addr_len);
	macSetStationAddress(Adapter, DI.stMacCurrent);

	if (bRxStarted)
		macStartRx(Adapter, TRUE);
	if (bTxStarted)
		macStartTx(Adapter, TRUE);

	ks8695_relink(Adapter);

	return 0;
}

/*
 * UpdateStatsCounters
 *	This function is used to update the board statistics counters.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure
 *
 * Return(s)
 *	NONE
 */
static void UpdateStatsCounters(PADAPTER_STRUCT Adapter)
{
	struct net_device_stats *stats;
	
	stats= &Adapter->net_stats;
}

/*
 * CheckState
 *	This function is used to handle error conditions if any.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure
 *	uISR		bit values of ISR register
 *
 * Return(s)
 *	NONE.
 */
static __inline void CheckState(PADAPTER_STRUCT Adapter, UINT32 uISR)
{
	BOOLEAN	bTxStopped = FALSE, bRxStopped = FALSE;

#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	/* clear all bits other than stop */
	uISR &= (DI.uIntMask & INT_DMA_STOP_MASK);
	switch (DI.usDMAId) {
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	case DMA_HPNA:
		if (uISR & INT_HPNA_TX_STOPPED)
			bTxStopped = TRUE;
		if (uISR & INT_HPNA_RX_STOPPED)
			bRxStopped = TRUE;
		break;
#endif

	case DMA_LAN:
		if (uISR & INT_LAN_TX_STOPPED)
			bTxStopped = TRUE;
		if (uISR & INT_LAN_RX_STOPPED)
			bRxStopped = TRUE;
		break;

	default:
	case DMA_WAN:
		if (uISR & INT_WAN_TX_STOPPED)
			bTxStopped = TRUE;
		if (uISR & INT_WAN_RX_STOPPED)
			bRxStopped = TRUE;
		break;
	}

	if (bRxStopped) {
		/* if Rx started already, then it is a problem! */
		if (DI.bRxStarted) {
			DRV_WARN("%s> RX stopped, ISR=0x%08x", __FUNCTION__, uISR);

			macStartRx(Adapter, FALSE);
			DelayInMilliseconds(2);
			macStartRx(Adapter, TRUE);
		}
		else {
			/* ACK and clear the bit */
			KS8695_WRITE_REG(KS8695_INT_STATUS, uISR);
		}
	}
	if (bTxStopped) {
		/* if Tx started already, then it is a problem! */
		if (DI.bTxStarted) {
			DRV_WARN("%s> TX stopped, ISR=0x%08x", __FUNCTION__, uISR);

			macStartTx(Adapter, FALSE);
			DelayInMilliseconds(2);
			macStartTx(Adapter, TRUE);
		}
		else {
			/* ACK and clear the bit */
			KS8695_WRITE_REG(KS8695_INT_STATUS, uISR);
		}
	}
}

/*
 * CheckLinkState
 *	This function is used to check link status to see whether link has changed or not.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *	uISR		ISR register (should be IMSR) to check
 *
 * Return(s)
 *	TRUE	if link change has detected
 *	FALSE	otherwise
 */
static __inline BOOLEAN CheckLinkState(PADAPTER_STRUCT Adapter, UINT uISR)
{
	BOOLEAN	bLinkChanged = FALSE;
	int	i;

	switch (DI.usDMAId) {
#if	!defined(CONFIG_ARCH_KS8695P) && !defined(KS8695X)
	case DMA_HPNA:
		/* what to do? */
		return FALSE;
#endif

	case DMA_WAN:
		if (uISR & INT_WAN_LINK) {
			bLinkChanged = TRUE;
			DI.bLinkChanged[0] = TRUE;
		}
		break;

	default:
	case DMA_LAN:
		for (i = 0; i < SW_MAX_LAN_PORTS; i++) {
			if (FALSE == DI.bLinkChanged[i]) {
				UINT	uReg = KS8695_READ_REG(KS8695_SWITCH_AUTO0 + (i >> 1));
				if (0 == (i % 2))
					uReg >>= 16;
				if (!(uReg & SW_AUTONEGO_STAT_LINK)) {
					bLinkChanged = TRUE;
					DI.bLinkChanged[i] = TRUE;
				}
			}
		}
		break;
	}

	return bLinkChanged;
}

/*
 * ProcessTxInterrupts
 *	This function is use to process Tx interrupt, reclaim resources after 
 *	transmit completes.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	how many number of Tx packets are not processed yet.
 */
static int __inline ProcessTxInterrupts(PADAPTER_STRUCT Adapter)
{
	int i;
	TXDESC *TransmitDescriptor;
	unsigned long flags;

#ifdef	DEBUG_THIS
	DRV_INFO("%s> )", __FUNCTION__); 
#endif

	i = DI.nTxDescUsed;
	TransmitDescriptor = &DI.pTxDescriptors[i];
	while (!(le32_to_cpu(TransmitDescriptor->TxOwnBit) & DESC_OWN_BIT) && DI.nTransmitCount > 0) {
		/* note that WAN DMA doesn't have statistics counters associated with,
		   therefore use local variables to track them instead */
		STAT_NET(tx_packets)++;
		STAT_NET(tx_bytes) += DI.pTxSkb[i].length;
		dev_kfree_skb_irq(DI.pTxSkb[i].skb);
		DI.pTxSkb[i].skb = NULL;

		local_irq_save(flags);
		DI.nTxDescAvail.counter++;
		DI.nTransmitCount--;
		local_irq_restore(flags);
		
		/* clear corresponding fields */
		TransmitDescriptor->TxDMAFragAddr = 0;

		/* clear all related bits, including len field, control bits and port bits */
		TransmitDescriptor->TxFrameControl = 0;

		/* to next Tx descriptor */
		i = (i + 1) % DI.nTxDescTotal;
		TransmitDescriptor = &DI.pTxDescriptors[i];
		DI.nTxProcessedCount++;
	}
	DI.nTxDescUsed = i;

	if (DI.bTxNoResource && netif_queue_stopped(Adapter->netdev) &&
	   (atomic_read(&DI.nTxDescAvail) > ((DI.nTxDescTotal * 3) >> 2))) {	/* 3/4 */
		DI.bTxNoResource = FALSE;
		netif_wake_queue(Adapter->netdev);
#ifdef	DEBUG_THIS
		DRV_INFO("%s> Tx process resumed", __FUNCTION__);
#endif
	}

	return DI.nTransmitCount;
}


/*
 * ProcessRxInterrupts
 *	This function is use to process Rx interrupt, send received data up
 *	the network stack. 
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	how many Rx packets are processed.
 */
static int __inline ProcessRxInterrupts(PADAPTER_STRUCT Adapter)
{
	RXDESC *CurrentDescriptor, *pBegin;
	int i, nProcessed = 0;
	uint32_t Length;
	uint32_t uFrameCtrl;
	struct sk_buff *skb;
	struct sk_buff *copy_skb;
	int cng_level = 0;

#ifdef	DEBUG_THIS
	DRV_INFO("%s> )", __FUNCTION__); 
#endif

#ifdef CONFIG_LEDMAN
	ledman_cmd(LEDMAN_CMD_SET, LEDMAN_LAN1_RX);
		/*(dev->name[3] == '0') ? LEDMAN_LAN1_RX : LEDMAN_LAN2_RX);*/
#endif
	i = DI.nRxDescNextAvail;
	pBegin = CurrentDescriptor = &DI.pRxDescriptors[i];

	while (!((uFrameCtrl = le32_to_cpu(CurrentDescriptor->RxFrameControl)) & DESC_OWN_BIT)) {
		skb = DI.pRxSkb[i].skb;
        	/* it should never goes here */
		if (NULL == skb) 
        	{
			if (!(0 == CurrentDescriptor->RxFrameControl && 0 == CurrentDescriptor->RxDMAFragLen
				&& 0 == CurrentDescriptor->RxDMAFragAddr)) {
				DRV_INFO("%s: inconsistency error, rx desc index=%d", __FUNCTION__, i);
			}
			break;
		}

		/* length with CRC bytes included */
		Length = (uFrameCtrl & RFC_FL_MASK);

		/* test both bits to make sure single packet */
		if ((uFrameCtrl & (RFC_LS | RFC_FS)) != (RFC_LS | RFC_FS)) {
			DRV_INFO("%s> spanning packet detected (framectrl=0x%08x, rx desc index=%d)", __FUNCTION__, uFrameCtrl, i);
			if (uFrameCtrl & RFC_FS) {
				/* first segment */
				Length = DI.uRxBufferLen;
				DRV_INFO(" first segment, len=%d", Length);
				/* compensite offset CRC */
				Length += ETH_CRC_LENGTH;
			}
			else if (uFrameCtrl & RFC_LS) {
				/* last segment */
				if (Length > DI.uRxBufferLen + ETH_CRC_LENGTH) {
					Length -= DI.uRxBufferLen;
					DRV_INFO(" last segment, len=%d", Length);
				}
				else {
					DRV_WARN("%s> under size packet (len=%d, buffer=%d)", __FUNCTION__, Length, DI.uRxBufferLen);
					STAT_NET(rx_errors)++;
					goto CLEAN_UP;
				}
			}
			else {
				if (0 == uFrameCtrl) {
					/* race condition ? */
					DRV_WARN("FragLen=0x%08x, FragAddr=0x%08x, RxNextPtr=0x%08x, RxDescEmpty=%d, pkt dropped", 
						CurrentDescriptor->RxDMAFragLen, CurrentDescriptor->RxDMAFragAddr, CurrentDescriptor->RxDMANextPtr, atomic_read(&DI.RxDescEmpty));
#ifdef	PACKET_DUMP
					ks8695_dump_packet(Adapter, skb->data, DI.uRxBufferLen, DEBUG_PACKET_LEN | DEBUG_PACKET_HEADER | DEBUG_PACKET_CONTENT);
#endif
				}
				else {
					DRV_WARN("%s> error spanning packet, dropped", __FUNCTION__);
				}
				STAT_NET(rx_errors)++;
				goto CLEAN_UP;
			}
		}

		/* if error happened!!! */
		if (uFrameCtrl & (RFC_ES | RFC_RE)) {
			DRV_WARN("%s> error found (framectrl=0x%08x)", __FUNCTION__, uFrameCtrl);
			STAT_NET(rx_errors)++;
			if (uFrameCtrl & RFC_TL) {
				STAT_NET(rx_length_errors)++;
			}
			if (uFrameCtrl & RFC_CRC) {
				STAT_NET(rx_crc_errors)++;
			}
			if (uFrameCtrl & RFC_RF) {
				STAT_NET(rx_length_errors)++;
			}
			/* if errors other than ES happened!!! */
			if (uFrameCtrl & RFC_RE) {
				DRV_WARN("%s> RFC_RE (MII) (framectrl=0x%08x)", __FUNCTION__, uFrameCtrl);
				STAT_NET(rx_errors)++;
			}
			/* RLQ, 11/07/2002, added more check to IP/TCP/UDP checksum errors */
			if (uFrameCtrl | (RFC_IPE | RFC_TCPE | RFC_UDPE)) {
				STAT_NET(rx_errors)++;
			}
			goto CLEAN_UP;
		}

#ifdef	MORE_ERROR_TRACKING
		/* for debug purpose */
		if (Length > 1518) {
			DI.uRx1518plus++;

			/* note that printout affects the performance figure quite lots, so don't display
			   it when conducting performance test, like Chariot */
			if (DI.uDebugDumpRxPkt & DEBUG_PACKET_OVSIZE) {
				DRV_INFO("%s> oversize pkt, size=%d, RxDesc=%d", __FUNCTION__, Length, i);
			}

			/* do early drop */
			STAT_NET(rx_errors)++;
			goto CLEAN_UP;
		}

		/* for debug purpose */
		if (Length < 64) {
			DI.uRxUnderSize++;
			/* note that printout affects the performance figure quite lots, so don't display
			   it when conducting performance test, like Chariot */
			if (DI.uDebugDumpRxPkt & DEBUG_PACKET_UNDERSIZE) {
				DRV_INFO("%s> under pkt, size=%d, RxDesc=%d", __FUNCTION__, Length, i);
			}
			/* do early drop */
			STAT_NET(rx_errors)++;
			goto CLEAN_UP;
		}
#endif /* #ifdef	MORE_ERROR_TRACKING */

		/* 
         	 * if we are here, means a valid packet received!!! Get length of the pacekt 
         	 */

		/* offset CRC bytes! */
		Length -= ETH_CRC_LENGTH;

		/* to do something ? */
		consistent_sync(skb->data, DI.uRxBufferLen, PCI_DMA_FROMDEVICE);

#ifdef	PACKET_DUMP
		/* build in debug mechanism */
		ks8695_dump_packet(Adapter, skb->data, Length, DI.uDebugDumpRxPkt);
#endif

		/* 
		 * copy recevied data to a new skb buffer in order to make IP header 32 bit alignment.
		 */
		copy_skb = dev_alloc_skb(Length + offset );
		if ( copy_skb == NULL) 
		{
		    STAT_NET(rx_dropped)++;
		    cng_level = NET_RX_DROP;
		    goto CLEAN_UP;
		}
		copy_skb->dev = Adapter->netdev;
		skb_reserve(copy_skb, offset); /* offset frame by 2 bytes */

		/* read pkt_len bytes into new skb buf */
		memcpy ( skb_put(copy_skb, Length), skb->data, Length ); 

		/* pass the copied skb with IP header alignment to uplayer */
		skb = copy_skb;

		/* check and set Rx Checksum Offload flag */
		if (DI.bRxChecksum) 
		    /* tell upper edge that the driver handled it already! */
		    skb->ip_summed = CHECKSUM_UNNECESSARY;
		else 
		    skb->ip_summed = CHECKSUM_NONE;

		skb->protocol = eth_type_trans(skb, Adapter->netdev);
		cng_level = netif_rx(skb);
		nProcessed++;

		/* note that WAN DMA doesn't have statistics counters associated with,
		   therefore use local variables to track them instead */
		STAT_NET(rx_packets)++;
		STAT_NET(rx_bytes) += Length;
		if (uFrameCtrl & RFC_MF)
		    STAT_NET(multicast)++;
		Adapter->netdev->last_rx = jiffies;

CLEAN_UP:
		/* 
		 * done with this descriptor, let ks8695 DMA own it again 
         	*/
		CurrentDescriptor->RxFrameControl &= cpu_to_le32(~(RFC_FRAMECTRL_MASK)); 
		if (pBegin != CurrentDescriptor)
			CurrentDescriptor->RxFrameControl |= cpu_to_le32(DESC_OWN_BIT);

        	/* go to next rx descriptor */
		i = (i + 1) % DI.nRxDescTotal;
		CurrentDescriptor = &DI.pRxDescriptors[i];
		if (pBegin == CurrentDescriptor)	/* one round already */
			break;
		if (cng_level == NET_RX_DROP || cng_level == NET_RX_CN_HIGH)
			break;
	} /* while (!((uFrameCtrl = le32_to_cpu(CurrentDescriptor->RxFrameControl)) & DESC_OWN_BIT)) { */
	if (nProcessed)
		pBegin->RxFrameControl |= cpu_to_le32(DESC_OWN_BIT);

	DI.nRxDescNextAvail = i;

	/* enable Rx engine!!! */
	KS8695_WRITE_REG(REG_RXSTART + DI.nOffset, 1);

	return nProcessed;
}

#ifdef FAST_POLL
static void ks8695_fast_poll(void *arg)
{
	PADAPTER_STRUCT Adapter = netdev_priv((struct net_device *)arg);
	int i, irq;

	if (ks8695_poll_ready) {
		for (i = 0; (i < 6); i++) {
			irq = DI.uIntShift + i;
			if (DI.uIntMask & (1L << irq))
				ks8695_isr(irq, arg);
		}
	}
}
#endif

#ifdef HANDLE_RXPACKET_BY_INTERRUPT
/*
 * ks8695_isr
 *	This function is the Interrupt Service Routine.
 *
 * Argument(s)
 *	irq		interrupt number
 *	data	pointer to net_device structure
 *	regs	pointer to pt_regs structure
 *
 * Return(s)
 *	NONE.
 */
irqreturn_t ks8695_isr(int irq, void *data)
{
	PADAPTER_STRUCT Adapter = netdev_priv((struct net_device *)data);
	uint32_t uISR, uISR1, uIER;

#ifdef	DEBUG_THIS
	DRV_INFO("%s> HANDLE_RXPACKET_BY_INTERRUPT.)", __FUNCTION__); 
#endif

	uISR1 = (1L << irq);
	uIER = KS8695_READ_REG(KS8695_INT_ENABLE);

	/* disable corresponding interrupt */
	KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER & ~uISR1);

	/* ACK */
	KS8695_WRITE_REG(KS8695_INT_STATUS, uISR1);

	uISR = uISR1 >> DI.uIntShift;

	/* handle Receive Interrupt */
	if (uISR & INT_RX_BIT)
		ProcessRxInterrupts(Adapter);

	/* handle Transmit Done Interrupt */
#ifndef	USE_TX_UNAVAIL
	if (uISR & INT_TX_BIT) {
#else
	if (DI.nTransmitCount) {
#endif
		ProcessTxInterrupts(Adapter);
	}

	/* Restore Previous Interrupt Settings */
	KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER );

	return IRQ_HANDLED;
}

#endif /* #ifdef HANDLE_RXPACKET_BY_INTERRUPT */


#ifdef RX_TASK
/*
 * ks8695_isr
 *	This function is the Interrupt Service Routine.
 *
 * Argument(s)
 *	irq		interrupt number
 *	data	pointer to net_device structure
 *	regs	pointer to pt_regs structure
 *
 * Return(s)
 *	NONE.
 */
irqreturn_t ks8695_isr(int irq, void *data)
{
	PADAPTER_STRUCT Adapter = netdev_priv((struct net_device *)data);
	uint32_t uISR, uISR1, uIER;

#ifdef	  PACKET_DUMP
	DRV_INFO("%s> RX_TASK ?)", __FUNCTION__); 
#endif

	uISR1 = (1L << irq);
	uIER = KS8695_READ_REG(KS8695_INT_ENABLE);

	/* disable corresponding interrupt */
	KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER & ~uISR1);

	/* ACK */
	KS8695_WRITE_REG(KS8695_INT_STATUS, uISR1);

	uISR = uISR1 >> DI.uIntShift;

	switch (uISR) {
	    /* handle Receive Interrupt */
		case INT_RX_BIT: 
			if (FALSE == DI.rx_scheduled) {
				DI.rx_scheduled = TRUE;
				tasklet_hi_schedule(&DI.rx_tasklet);
			}
			uISR1 = 0;
			break;

	    /* handle Transmit Done Interrupt */
		case INT_TX_BIT:
#ifdef	USE_TX_UNAVAIL
	    /* handle Transmit Buffer Unavailable Interrupt */
		case INT_TX_UNAVAIL_BIT:
		case (INT_TX_UNAVAIL_BIT | INT_TX_BIT):
#endif
#ifndef	TX_TASK
			ProcessTxInterrupts(Adapter);
#else
			if (FALSE == DI.tx_scheduled) {
				DI.tx_scheduled = TRUE;
				tasklet_hi_schedule(&DI.tx_tasklet);
			}
			uISR1 = 0;
#endif
			break;

#ifdef	USE_RX_UNAVAIL
	    /* handle Receive Buffer Unavailable Interrupt */
		case INT_RX_UNAVAIL_BIT:
			break;
#endif
	}

	/* Restore Previous Interrupt if not a scheduled task */
	if (uISR1) {
		KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER );
	}

	return IRQ_HANDLED;
}

#endif	/*RX_TASK*/

/*
 * ks8695_isre
 * for I-cache lockdown or FIQ purpose. Make sure this function is after ks8695_isr immediately.
 *
 * Argument(s)
 *	NONE.
 *
 */
void ks8695_isre(void)
{
	/* just for the end of ks8695_isr routine, unless we find a way to define end of function
	   within ks8695_isr itself */
}

/*
 * ks8695_isr_link
 *	This function is to process WAN link change interrupt as a special case
 *
 * Argument(s)
 *	irq		interrupt number
 *	data	pointer to net_device structure
 *	regs	pointer to pt_regs structure
 *
 * Return(s)
 *	NONE.
 */
irqreturn_t ks8695_isr_link(int irq, void *data)
{
	PADAPTER_STRUCT Adapter = netdev_priv((struct net_device *)data);
	UINT	uIER;

	spin_lock(&DI.lock);
	uIER = KS8695_READ_REG(KS8695_INT_ENABLE) & ~INT_WAN_LINK;
	KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER);
	spin_unlock(&DI.lock);

	DI.nLinkChangeCount++;
	DI.bLinkChanged[0] = TRUE;

	/* start auto nego only when link is down */
	if (!swGetWANLinkStatus(Adapter)) {
		swPhyReset(Adapter, 0);
		swAutoNegoAdvertisement(Adapter, 0);
		swDetectPhyConnection(Adapter, 0);
	}

	/* ACK */
	KS8695_WRITE_REG(KS8695_INT_STATUS, INT_WAN_LINK);
	spin_lock(&DI.lock);
	uIER = KS8695_READ_REG(KS8695_INT_ENABLE) | INT_WAN_LINK;
	KS8695_WRITE_REG(KS8695_INT_ENABLE, uIER);
	spin_unlock(&DI.lock);

	return IRQ_HANDLED;
}


/*
 * ReceiveBufferFill
 *	This function is use to replace used receive buffers with new SBBs
 *	to Rx descriptors.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	NONE.
 */
static void ReceiveBufferFill(uintptr_t data)
{
	PADAPTER_STRUCT Adapter = (PADAPTER_STRUCT) data;
	RXDESC *CurrentDescriptor, *pBegin;
	struct sk_buff *skb;
	unsigned long flags;
	int i;

	if (!test_bit(KS8695_BOARD_OPEN, &Adapter->flags)) {
		DI.rx_fill_scheduled = FALSE;
		/* enable Rx engine!!! */
		KS8695_WRITE_REG(REG_RXSTART + DI.nOffset, 1);
		return;
	}

	i = DI.nRxDescNextToFill;
	pBegin = CurrentDescriptor = &DI.pRxDescriptors[i];

	//__save_flags_cli(flags);
	while (NULL == DI.pRxSkb[i].skb) {
		skb = alloc_skb(DI.uRxBufferLen + offset, GFP_ATOMIC | GFP_DMA);
		if (NULL == skb) {
			/*DRV_WARN("%s> alloc_skb failed, refill rescheduled again", __FUNCTION__);*/
			break;
		}

		skb->dev = Adapter->netdev;
		DI.pRxSkb[i].length = DI.uRxBufferLen;
		DI.pRxSkb[i].direction = PCI_DMA_FROMDEVICE;
#ifndef	RX_TASK
		consistent_sync(skb->data, DI.uRxBufferLen, PCI_DMA_FROMDEVICE);
#endif
		DI.pRxSkb[i].dma = virt_to_phys(skb->data);

		/* to avoid possible race problem, make the change of these variables atomic */
		local_irq_save(flags);
		DI.pRxSkb[i].skb = skb;

		/* setup Rx descriptor!!! */
		CurrentDescriptor->RxDMAFragAddr = cpu_to_le32(DI.pRxSkb[i].dma);
		CurrentDescriptor->RxDMAFragLen = cpu_to_le32(DI.uRxBufferLen);
		CurrentDescriptor->RxFrameControl |= cpu_to_le32(DESC_OWN_BIT);

		DI.RxDescEmpty.counter--;
		local_irq_restore(flags);

		i = (i + 1) % DI.nRxDescTotal;
		CurrentDescriptor = &DI.pRxDescriptors[i];
		if (pBegin == CurrentDescriptor) /* one round already */
			break;
	}
	DI.nRxDescNextToFill = i;
	//__restore_flags(flags);
	
	DI.rx_fill_scheduled = FALSE;

	/* enable Rx engine!!! */
	KS8695_WRITE_REG(REG_RXSTART + DI.nOffset, 1);
}

static UINT mii_bmcr(PADAPTER_STRUCT Adapter, struct mii_regs *regs)
{
	UINT out = 0, reg;

	reg = KS8695_READ_REG(regs->config.reg);
	out |= reg & SW_PORT_FULLDUPLEX ? BMCR_FULLDPLX : 0;
	out |= reg & SW_PORT_DISABLE_AUTONEG ? 0 : BMCR_ANENABLE;
	out |= reg & SW_PORT_100BASE ? BMCR_SPEED100 : 0;

	reg = KS8695_READ_REG(regs->autonego.reg);
	out |= reg & (SW_AUTONEGO_RESTART << regs->autonego.shift) ? BMCR_ANRESTART : 0;

	reg = KS8695_READ_REG(regs->power.reg);
	out |= reg & (POWER_POWERDOWN << regs->power.shift) ? BMCR_PDOWN : 0;

	reg = KS8695_READ_REG(REG_TXCTRL + DI.nOffset);
	out |= reg & DMA_LOOPBACK ? BMCR_LOOPBACK : 0;

	return out;
}

static UINT mii_bmsr(PADAPTER_STRUCT Adapter, struct mii_regs *regs)
{
	UINT out = 0, reg;

	reg = KS8695_READ_REG(regs->autonego.reg);
	out |= reg & (SW_AUTONEGO_STAT_LINK << regs->autonego.shift) ? BMSR_LSTATUS : 0;
	out |= reg & (SW_AUTONEGO_COMPLETE << regs->autonego.shift) ? BMSR_ANEGCOMPLETE : 0;

	reg = KS8695_READ_REG(regs->config.reg);
	if (reg & SW_PORT_DISABLE_AUTONEG) {
		if (reg & SW_PORT_100BASE) {
			if (reg & SW_PORT_FULLDUPLEX)
				out |= BMSR_100FULL;
			else
				out |= BMSR_100HALF;
		} else {
			if (reg & SW_PORT_FULLDUPLEX)
				out |= BMSR_10FULL;
			else
				out |= BMSR_10HALF;
		}
	} else
		out |= BMSR_ANEGCAPABLE | BMSR_10HALF | BMSR_10FULL | BMSR_100HALF | BMSR_100FULL;

	return out;
}

static UINT mii_advertise(PADAPTER_STRUCT Adapter, struct mii_regs *regs)
{
	UINT out = 0, reg;

	reg = KS8695_READ_REG(regs->autonego.reg);
	out |= ADVERTISE_CSMA; /* Only mode supported */
	out |= reg & (SW_AUTONEGO_ADV_10HD << regs->autonego.shift) ? ADVERTISE_10HALF : 0;
	out |= reg & (SW_AUTONEGO_ADV_10FD << regs->autonego.shift) ? ADVERTISE_10FULL : 0;
	out |= reg & (SW_AUTONEGO_ADV_100HD << regs->autonego.shift) ? ADVERTISE_100HALF : 0;
	out |= reg & (SW_AUTONEGO_ADV_100FD << regs->autonego.shift) ? ADVERTISE_100FULL : 0;
	out |= reg & (SW_AUTONEGO_PART_10HD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_10FD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_100HD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_100FD << regs->autonego.shift) ? ADVERTISE_LPACK : 0;

	return out;
}

static UINT mii_lpa(PADAPTER_STRUCT Adapter, struct mii_regs *regs)
{
	UINT out = 0, reg;

	reg = KS8695_READ_REG(regs->autonego.reg);
	out |= ADVERTISE_CSMA; /* Only mode supported */
	out |= reg & (SW_AUTONEGO_PART_10HD << regs->autonego.shift) ? ADVERTISE_10HALF : 0;
	out |= reg & (SW_AUTONEGO_PART_10FD << regs->autonego.shift) ? ADVERTISE_10FULL : 0;
	out |= reg & (SW_AUTONEGO_PART_100HD << regs->autonego.shift) ? ADVERTISE_100HALF : 0;
	out |= reg & (SW_AUTONEGO_PART_100FD << regs->autonego.shift) ? ADVERTISE_100FULL : 0;
	out |= reg & (SW_AUTONEGO_PART_10HD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_10FD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_100HD << regs->autonego.shift) ||
		reg & (SW_AUTONEGO_PART_100FD << regs->autonego.shift) ? LPA_LPACK : 0;

	return out;
}

static int	ks8695_ioctl_switch(PADAPTER_STRUCT Adapter, PIOCTRL_SWITCH pIoCtrl);

/*
 * ks8695_ioctl
 *	This function is the ioctl entry point. It handles some driver specific IO
 *	functions.
 *
 * Argument(s)
 *	netdev		pointer to net_device structure.
 *	ifr			pointer to ifreq structure
 *	cmd			io cmd 
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
    PADAPTER_STRUCT Adapter = netdev_priv(netdev);
	PIOCTRL	pIoCtrl;
	struct mii_ioctl_data *data;
	struct mii_regs *regs;
	int	nRet = -1;

#ifdef DEBUG_THIS
    DRV_INFO("%s> cmd = 0x%x", __FUNCTION__, cmd);
#endif

	pIoCtrl = (PIOCTRL)ifr->ifr_data;
	data = (struct mii_ioctl_data *)&ifr->ifr_data;

    switch(cmd) {
		/*
		 *  mii-tool commands
		 */
		case SIOCGMIIPHY:
		case SIOCDEVPRIVATE:
			/* Get address of MII PHY */
			data->phy_id = 0;
			nRet = 0;
			break;

		case SIOCGMIIREG:
		case SIOCDEVPRIVATE + 1:
			/*
			 *  Read MII PHY register
			 */
			if (!strcmp(netdev->name, "eth0")) {
				if (data->phy_id >= SW_MAX_LAN_PORTS || DI.usDMAId != DMA_LAN)
					return -EOPNOTSUPP;
				regs = &mii_regs_lan[data->phy_id];
			} else if (!strcmp(netdev->name, "eth1")) {
				if (data->phy_id != 0 || DI.usDMAId != DMA_WAN)
					return -EOPNOTSUPP;
				regs = &mii_regs_wan[0];
			} else
				return -EOPNOTSUPP;

			data->val_out = 0;

			switch (data->reg_num)
			{
				case MII_BMCR:
					/* Basic mode control register */
					data->val_out = mii_bmcr(Adapter, regs);
					break;

				case MII_BMSR:
					/* Basic mode status register */
					data->val_out = mii_bmsr(Adapter, regs);
					break;

				case MII_ADVERTISE:
					/* Advertisement control register */
					data->val_out = mii_advertise(Adapter, regs);
					break;

				case MII_LPA:
					/* Link partner ability register */
					data->val_out = mii_lpa(Adapter, regs);
					break;
			}
			nRet = 0;
			break;
				
		case SIOCSMIIREG:
		case SIOCDEVPRIVATE + 2:
			/*
			 *  Write MII PHY register
			 */
			if (!strcmp(netdev->name, "eth0")) {
				if (data->phy_id >= SW_MAX_LAN_PORTS || DI.usDMAId != DMA_LAN)
					return -EOPNOTSUPP;
			} else if (!strcmp(netdev->name, "eth1")) {
				if (data->phy_id != 0 || DI.usDMAId != DMA_WAN)
					return -EOPNOTSUPP;
			} else
				return -EOPNOTSUPP;

			switch (data->reg_num)
			{
				case MII_BMCR:
					if (skipcmd) {
						skipcmd = 0;
						break;
					} else if (data->val_in & BMCR_ANRESTART) {
						/* Restart autonegotiation */
						if (DI.byDisableAutoNego[data->phy_id] == LINK_SELECTION_FORCED) {
							if ((DI.usCType[data->phy_id] = ctype) == SW_PHY_AUTO)
								DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_FULL_AUTO;
							else
								DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_PARTIAL_AUTO;
						}
					} else if (data->val_in & BMCR_RESET) {
						/* Reset to defaults */
						if ((DI.usCType[data->phy_id] = SW_PHY_DEFAULT) == SW_PHY_AUTO)
							DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_FULL_AUTO;
						else
							DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_PARTIAL_AUTO;
					} else {
						/* Force link media type */
						if (DI.byDisableAutoNego[data->phy_id] == LINK_SELECTION_PARTIAL_AUTO)
							ctype = DI.usCType[data->phy_id];	/* Remember advert media */
						DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_FORCED;

						if (data->val_in & BMCR_SPEED100) {
							if (data->val_in & BMCR_FULLDPLX)
								DI.usCType[data->phy_id] = SW_PHY_100BASE_TX_FD;
							else
								DI.usCType[data->phy_id] = SW_PHY_100BASE_TX;
						} else {
							if (data->val_in & BMCR_FULLDPLX)
								DI.usCType[data->phy_id] = SW_PHY_10BASE_T_FD;
							else
								DI.usCType[data->phy_id] = SW_PHY_10BASE_T;
						}
					}
				
					swConfigureMediaType(Adapter,
						data->phy_id,
						DI.usCType[data->phy_id] == SW_PHY_100BASE_TX ||
						DI.usCType[data->phy_id] == SW_PHY_100BASE_TX_FD ? 1 : 0,
						DI.usCType[data->phy_id] == SW_PHY_10BASE_T_FD ||
						DI.usCType[data->phy_id] == SW_PHY_100BASE_TX_FD ? 1 : 0);
					break;

				case MII_ADVERTISE:
					DI.byDisableAutoNego[data->phy_id] = LINK_SELECTION_PARTIAL_AUTO;
					/*  
					 *  mii-tool -A tries to disable then re-enable autonego,
					 *  the cmd to disable autonego looks just like the cmd
					 *  to force 10baseT-HD so set skipcmd to ignore it
					 */
					skipcmd = 1;

					if (data->val_in & ADVERTISE_10HALF)
						DI.usCType[data->phy_id] = SW_PHY_10BASE_T;
					if (data->val_in & ADVERTISE_10FULL)
						DI.usCType[data->phy_id] = SW_PHY_10BASE_T_FD;
					if (data->val_in & ADVERTISE_100HALF)
						DI.usCType[data->phy_id] = SW_PHY_100BASE_TX;
					if (data->val_in & ADVERTISE_100FULL)
						DI.usCType[data->phy_id] = SW_PHY_100BASE_TX_FD;
					break;

				default:
					break;
			}
			nRet = 0;
			break;

		/*
		 *  Debug commands
		 */
		case SIOC_KS8695_IOCTRL:
			if (ifr->ifr_data) {
				UINT32	*pReg, i;

				switch (pIoCtrl->byId)
				{
					case REG_DMA_DUMP:
						if (pIoCtrl->usLen >= (4 * (1 + REG_DMA_MAX) + 3)) { /* 1 + 2 + 4 + 8 * 4 */
							pReg = pIoCtrl->u.uData;
							/* tell debug application its offset */
							*pReg++ = DI.nOffset;
							for (i = REG_TXCTRL; i <= REG_STATION_HIGH ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i + DI.nOffset);
							}
							nRet = 0;
						}
						break;

					case REG_DMA_STATION_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_DMA_STATION_MAX + 3)) { /* 1 + 2 + 16 * 2 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = REG_MAC0_LOW; i <= REG_MAC15_HIGH ; i += 8) {
								*pReg++ = (UINT32)KS8695_READ_REG(i + DI.nOffset);
								*pReg++ = (UINT32)KS8695_READ_REG(i + DI.nOffset + 4);
							}
							nRet = 0;
						}
						break;

					case REG_UART_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_UART_MAX + 3)) { /* 1 + 2 + 9 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_UART_RX_BUFFER; i <= KS8695_UART_STATUS ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					case REG_INT_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_INT_MAX + 3)) { /* 1 + 2 + 14 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_INT_CONTL; i <= KS8695_IRQ_PEND_PRIORITY ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					/* Timer receive related */
					case REG_TIMER_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_TIMER_MAX + 3)) { /* 1 + 2 + 5 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_TIMER_CTRL; i <= KS8695_TIMER0_PCOUNT ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					/* GPIO receive related */
					case REG_GPIO_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_GPIO_MAX + 3)) { /* 1 + 2 + 3 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_GPIO_MODE; i <= KS8695_GPIO_DATA ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					case REG_SWITCH_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_SWITCH_MAX + 3)) { /* 1 + 2 + 21 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_SWITCH_CTRL0; i <= KS8695_LAN34_POWERMAGR ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					case REG_MISC_DUMP:
						if (pIoCtrl->usLen >= (4 * REG_MISC_MAX + 3)) { /* 1 + 2 + 7 * 4 */
							pReg = pIoCtrl->u.uData;
							for (i = KS8695_DEVICE_ID; i <= KS8695_WAN_PHY_STATUS ; i += 4, pReg++) {
								*pReg = (UINT32)KS8695_READ_REG(i);
							}
							nRet = 0;
						}
						break;

					case REG_SNMP_DUMP:
						/* do we need to restrict its read to LAN driver only? May be later! */
						if (pIoCtrl->usLen >= (4 * REG_SNMP_MAX + 3)) { /* 1 + 2 + 138 * 4 */
							/* each port (1-4) takes 32 counters, and last 10 counters for port */
							pReg = pIoCtrl->u.uData;
							for (i = 0; i <= REG_SNMP_MAX ; i++, pReg++) {
								*pReg = swReadSNMPReg(Adapter, i);
								DelayInMicroseconds(10);
							}
							nRet = 0;
						}
						break;

					case DRV_VERSION:
						if (pIoCtrl->usLen >= 19) { /* 1 + 2 + 16 */
							if (0 == Adapter->rev) 
								strncpy(pIoCtrl->u.byData, ks8695_driver_version, 15);
							else {
								if ((strlen(ks8695_driver_version) + 4) <= 15) {
									sprintf(pIoCtrl->u.byData, "%s, 95P.PING.01", ks8695_driver_version);
								}
								else
									strncpy(pIoCtrl->u.byData, ks8695_driver_version, 15);
							}
							nRet = 0;
						}
						break;

#ifdef	CONFIG_ARCH_KS8695P
					case DUMP_PCI_SPACE:
						if (pIoCtrl->usLen >= sizeof(IOCTRL)) { /* 1 + 2 + 1 */
							if (Adapter->rev) {
								i = 0;
								printk("----- PCI conf Space -----\n");
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_2000)); i += 4;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_2004)); i += 4;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_2008)); i += 4;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_200C)); i += 4;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_2010)); i += 4;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_2014)); i += 4;
								i = 0x2c;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_202C)); i += 4;
								i = 0x3c;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(KS8695_203C)); i += 4;
								nRet = 0;
							}	
						}
						break;

					case DUMP_BRIDGE_REG:
						if (pIoCtrl->usLen >= sizeof(IOCTRL)) { /* 1 + 2 + 1 + 1 (optional) */
							if (Adapter->rev) {
								printk("----- Bridge Conf Registers -----\n");
								i = KS8695_2100;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2104;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));

								i = KS8695_2200;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2204;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2208;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_220C;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2210;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2214;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2218;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_221C;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2220;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								i = KS8695_2224;
								printk("0x%04x          0x%08x\n", i, KS8695_READ_REG(i));
								nRet = 0;
							}
						}
						break;
#endif //CONFIG_ARCH_KS8695P
					case MEMORY_DUMP:
						/* dump 32 dwords each time */
						if (pIoCtrl->usLen >= (4 * 32 + 3)) { /* 1 + 2 + 1 + 1 (optional) */
							UINT32	*pReg1;

							/* note that the address is in the first dword, when returned will contain data */
							pReg = pIoCtrl->u.uData;
							pReg1 = (UINT32 *)(*pReg);

#ifdef	DEBUG_THIS
							DRV_INFO("addr=0x%08x, 0x%0x8", pReg1, *pReg1);
#endif
							/* if no null pointer */
							if (pReg1 && (0xc0000000 == ((UINT)pReg1 & 0xc0000000))) {
								for (i = 0; i <= 32 ; i++, pReg++, pReg1++) {
									*pReg = *pReg1;
								}
								nRet = 0;
							}
							else {
								DRV_INFO("%s> invalid address: 0x%08x", __FUNCTION__, *pReg1);
								nRet = -EINVAL;
							}
						}
						break;

					case MEMORY_SEARCH:
						/* dump 32 dwords each time */
						if (pIoCtrl->usLen > 3 && pIoCtrl->usLen < 128) { /* 1 + 2 + optional length */
							DRV_INFO("%s> not implemented yet", __FUNCTION__);
							nRet = 0;
						}
						break;

					case REG_WRITE:
						/* write control register */
						if (pIoCtrl->usLen >= 7) { /* 1 + 2 + 1 * 4 */
							UINT	uReg;

							uReg = pIoCtrl->u.uData[0];
							if (uReg >= 0xffff) {
								return -EINVAL;
							}
							if (pIoCtrl->usLen < 11) {
								/* if no parameter is given, display register value instead */
								printk("Reg(0x%04x) = 0x%08x", uReg, KS8695_READ_REG(uReg));
							}
							else {
								KS8695_WRITE_REG(uReg, pIoCtrl->u.uData[1]);
							}
							nRet = 0;
						}
						break;

					case DEBUG_DUMP_TX_PACKET:
						/* set dump tx packet flag */
						if (pIoCtrl->usLen >= 7) { /* 1 + 2 + 4 */
							DI.uDebugDumpTxPkt = pIoCtrl->u.uData[0];
#ifndef	PACKET_DUMP
							DRV_INFO("%s> DumpTxPkt was disabled", __FUNCTION__);
#endif
							nRet = 0;
						}
						break;

					case DEBUG_DUMP_RX_PACKET:
						/* set dump rx packet flag */
						if (pIoCtrl->usLen >= 7) { /* 1 + 2 + 4 */
							DI.uDebugDumpRxPkt = pIoCtrl->u.uData[0];
#ifndef	PACKET_DUMP
							DRV_INFO("%s> DumpRxPkt was disabled", __FUNCTION__);
#endif
							nRet = 0;
						}
						break;

					case DEBUG_RESET_DESC:
						/* set dump rx packet flag */
						if (pIoCtrl->usLen == 3) { /* 1 + 2 */
							ResetDma(Adapter);
							nRet = 0;
						}
						break;

					case DEBUG_STATISTICS:
						/* printout statistical counters */
						if (pIoCtrl->usLen == 3) { /* 1 + 2 */
							printk("------- statistics TX -------\n");
							printk("tx_packets      = %12u\n", (UINT)STAT_NET(tx_packets));
							printk("tx_bytes        = %12u\n", (UINT)STAT_NET(tx_bytes));
							printk("tx_dropped      = %12u\n", (UINT)STAT_NET(tx_dropped));
							printk("tx_errors       = %12u\n", (UINT)STAT_NET(tx_errors));

							printk("------- statistics RX -------\n");
							printk("rx_packets      = %12u\n", (UINT)STAT_NET(rx_packets));
							printk("rx_bytes        = %12u\n", (UINT)STAT_NET(rx_bytes));
							printk("rx_dropped      = %12u\n", (UINT)STAT_NET(rx_dropped));
							printk("rx_errors       = %12u\n", (UINT)STAT_NET(rx_errors));
							printk("rx_length_errors= %12u\n", (UINT)STAT_NET(rx_length_errors));
							printk("rx_crc_errors   = %12u\n", (UINT)STAT_NET(rx_crc_errors));
							printk("collisions      = %12u\n", (UINT)STAT_NET(collisions));
							printk("multicast       = %12u\n", (UINT)STAT_NET(multicast));
							printk("rx_missed_errors= %12u\n", (UINT)STAT_NET(rx_missed_errors));
							printk("rx_length_errors= %12u\n", (UINT)STAT_NET(rx_length_errors));
							printk("over size pkts  = %12u\n", (UINT)DI.uRx1518plus);
							printk("under size pkts = %12u\n", (UINT)DI.uRxUnderSize);
							printk("TransmitCount   = %12u\n", DI.nTransmitCount);

							printk("------- Misc -------\n");
							printk("DMA reset count = %12d\n", DI.nResetCount);
							printk("Link change cnt = %12d\n", DI.nLinkChangeCount);
							nRet = 0;
						}
						break;

					case DEBUG_DESCRIPTORS:
						/* printout descriptors info */
						if (pIoCtrl->usLen == 3) { /* 1 + 2 */
							printk("------ TX Descriptors ------\n");
							printk("descriptor VA   = 0x%08x\n", (UINT)DI.pTxDescriptors);
							printk("total           = %10d\n", DI.nTxDescTotal);
							printk("available       = %10d\n", atomic_read(&DI.nTxDescAvail));
							printk("next available  = %10d\n", DI.nTxDescNextAvail);
							printk("no resource     = %10d\n", DI.bTxNoResource);
							printk("------ RX Descriptors ------\n");
							printk("descriptor VA   = 0x%08x\n", (UINT)DI.pRxDescriptors);
							printk("total           = %10d\n", DI.nRxDescTotal);
							printk("next to fill    = %10d\n", DI.nRxDescNextToFill);
							printk("next available  = %10d\n", DI.nRxDescNextAvail);
							printk("empty           = %10d\n", atomic_read(&DI.RxDescEmpty));
							nRet = 0;
						}
						break;

					case DEBUG_LINK_STATUS:
						/* printout link status */
						if (pIoCtrl->usLen >= 3) { /* 1 + 2 */
							int	i;

							if (DMA_LAN != DI.usDMAId) {
								/* RLQ, 1.0.0.14, modified for UPNP query */
								if (pIoCtrl->usLen == 15) {	/* 3 + 3 * 4 */
									UINT32	*pReg;

									/* note that the address is in the first dword, when returned will contain data */
									pReg = pIoCtrl->u.uData;
									i = 0;
									*pReg++ = DI.bLinkActive[i];	/* link active */
									if (!DI.bLinkActive[i]) {
										*pReg++ = 0;				/* speed */
										*pReg = 0;					/* duplex */
									}
									else {
										*pReg++ = (SPEED_100 == DI.usLinkSpeed[i]) ? 100000000 : 10000000;
										*pReg = DI.bHalfDuplex[i];
									}
								}
								else {	/* for back compatible with ks8695_debug utility */
									i = 0;
									if (!DI.bLinkActive[i]) {
										printk("Link = Down, Speed=Unknown, Duplex=Unknown\n");
									}
									else {
										if (SW_PHY_AUTO == DI.usCType[i]) {
											printk("Link=Up, Speed=%s, Duplex (read)=%s, Duplex (detected)=%s\n",
												SPEED_100 == DI.usLinkSpeed[i] ? "100" : "10", 
												DI.bHalfDuplex[i] ? "Full Duplex" : "Half Duplex",
												DI.bHalfDuplexDetected[i] ? "Full Duplex" : "Half Duplex");
										}
										else {
											printk("Link=Up, Speed=%s, Duplex=%s\n",
												SPEED_100 == DI.usLinkSpeed[i] ? "100" : "10", 
												DI.bHalfDuplex[i] ? "Full Duplex" : "Half Duplex");
										}
									}
								}
							}
							else {
								/* RLQ, 1.0.0.14, modified for UPNP query */
								if (pIoCtrl->usLen == 3 + 3 * 4 * SW_MAX_LAN_PORTS) {	/* 3 + 3 * 4 * 4 = 51 */
									UINT32	*pReg;

									/* note that the address is in the first dword, when returned will contain data */
									pReg = pIoCtrl->u.uData;
									for (i = 0; i < SW_MAX_LAN_PORTS; i++) {
										*pReg++ = DI.bLinkActive[i];	/* link active */
										if (!DI.bLinkActive[i]) {
											*pReg++ = 0;				/* speed */
											*pReg++ = 0;				/* duplex */
										}
										else {
											*pReg++ = (SPEED_100 == DI.usLinkSpeed[i]) ? 100000000 : 10000000;
											*pReg++ = DI.bHalfDuplex[i];
										}
									}
								}
								else {
									for (i = 0; i < SW_MAX_LAN_PORTS; i++) {
										if (DI.bLinkActive[i]) {
											printk("Port[%d]  Speed=%s, Duplex (read)=%s, Duplex (detected)=%s\n", (i + 1),
												SPEED_100 == DI.usLinkSpeed[i] ? "100" : "10", 
												DI.bHalfDuplex[i] ? "Full Duplex" : "Half Duplex",
											DI.bHalfDuplexDetected[i] ? "Full Duplex" : "Half Duplex");
										}
										else {
											printk("Port[%d]  Speed = Unknown, Duplex = Unknown\n", (i + 1));
										}
									}
								}
							}
							nRet = 0;
						}
						break;

					case CONFIG_LINK_TYPE:
						/* configure link media type (forced mode without auto nego) */
						if (pIoCtrl->usLen == 19) { /* 1 + 2 + 4 * 4*/
							uint32_t	uPort;
							uint32_t	uSpeed;
							uint32_t	uDuplex;

							pReg = pIoCtrl->u.uData;
							if (DMA_LAN != DI.usDMAId) {
								uPort = 0;
								pReg++;
							} else {
								uPort = *pReg++;
								if (uPort >= SW_MAX_LAN_PORTS) {
									DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
									break;
								}
							}
							DI.byDisableAutoNego[uPort] = *pReg++;
							uSpeed = *pReg++;
							uDuplex = *pReg;
							/*DRV_INFO("%s> port=%d, disable autonego=%d, 100=%d, duplex=%d", __FUNCTION__, uPort, DI.byDisableAutoNego[uPort], uSpeed, uDuplex);*/
							swConfigureMediaType(Adapter, uPort, uSpeed, uDuplex);
							nRet = 0;
						}
						break;

					case CONFIG_STATION_EX:
						/* configure link media type (forced mode without auto nego) */
						if (pIoCtrl->usLen == 13) { /* 1 + 2 + 4 + 6 */
							pReg = pIoCtrl->u.uData;

							/* uData[0] = set, byData[4-9] = mac address */
							if (pIoCtrl->u.uData[0]) {
								int	i;

								i = macGetIndexStationEx(Adapter);
								if (i >= 0) {
									macSetStationEx(Adapter, &pIoCtrl->u.byData[4], i);
									nRet = 0;
								}
							}
							else {
								macResetStationEx(Adapter, &pIoCtrl->u.byData[4]);
								nRet = 0;
							}
						}
						break;

					case CONFIG_SWITCH_GET:
						/* we don't really need it since the OS always boots at super mode */
						/* if that is not the case, then enable following line, and add header file if missing ? */
						/*if (!capable(CAP_NET_ADMIN)) return -EPERM;*/
						/* !!! fall over !!! */

					case CONFIG_SWITCH_SET:
						/* for LAN driver only */
						if (DMA_LAN == DI.usDMAId) {
							return ks8695_ioctl_switch(Adapter, (PIOCTRL_SWITCH)ifr->ifr_data);
						}
						else {
							if (CONFIG_SW_SUBID_ADV_LINK_SELECTION == ((PIOCTRL_SWITCH)ifr->ifr_data)->bySubId) {
								return ks8695_ioctl_switch(Adapter, (PIOCTRL_SWITCH)ifr->ifr_data);
							}
							else {
								/* filter out the IF supported for WAN */
								return -EPERM;
							}
						}
						break;

					default:
						DRV_INFO("%s> unsupported parameters: id=%d, len=%d", __FUNCTION__, pIoCtrl->byId, pIoCtrl->usLen);
						return -EOPNOTSUPP;
				}
				break;
			}

		default:
			return -EOPNOTSUPP;
    }

    return nRet;
}

/*
 * ks8695_ioctl_switch
 *	This function is used to configure CONFIG_SWITCH_SUBID related functions, for web page based
 *	configuration or for ks8695_debug tool.
 *
 * Argument(s)
 *	Adapter		pointer to ADAPTER_STRUCT structure.
 *	pIoCtrl		pointer to IOCTRL_SWITCH structure.
 *
 * Return(s)
 *	0				if success
 *	negative value	if failed
 */
int ks8695_ioctl_switch(PADAPTER_STRUCT Adapter, PIOCTRL_SWITCH pIoCtrl)
{
	int	nRet = -1;
	uint32_t	uPort, uReg;

    switch(pIoCtrl->bySubId) {
		case CONFIG_SW_SUBID_ON:
			if (pIoCtrl->usLen == 8) { /* 1 + 2 + 1 + 4 */
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) 
					swEnableSwitch(Adapter, pIoCtrl->u.uData[0]);
				else {
					/* return current switch status */
					pIoCtrl->u.uData[0] = (KS8695_READ_REG(KS8695_SWITCH_CTRL0) & SW_CTRL0_SWITCH_ENABLE) ? TRUE : FALSE;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_PORT_VLAN:
			if (pIoCtrl->usLen == 20) { /* 1 + 2 + 1 + 4 * 4 */
				uPort = pIoCtrl->u.uData[0];
				if (uPort >= SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uint32_t	bInsertion;

					DPI[uPort].usTag = (uint16_t)pIoCtrl->u.uData[1];
					/* note that the driver doesn't use VLAN name, so the web page needs to remember it */
					bInsertion = pIoCtrl->u.uData[2];
					DPI[uPort].byCrossTalkMask = (uint8_t)(pIoCtrl->u.uData[3] & 0x1f);
#ifdef	DEBUG_THIS
					DRV_INFO("%s> port=%d, VID=%d, EgressMode=%s, crosstalk bit=0x%x", __FUNCTION__, uPort, DPI[uPort].usTag, bInsertion ? "tagged" : "untagged");
#endif
					swConfigurePort(Adapter, uPort);
					swConfigTagInsertion(Adapter, uPort, bInsertion);
				}
				else {
					pIoCtrl->u.uData[1] = (uint32_t)DPI[uPort].usTag;
					pIoCtrl->u.uData[2] = (KS8695_READ_REG(KS8695_SWITCH_ADVANCED) & (1L << (17 + uPort))) ? TRUE : FALSE;
					pIoCtrl->u.uData[3] = (uint32_t)DPI[uPort].byCrossTalkMask;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_PRIORITY:
			if (pIoCtrl->usLen == 32) { /* 1 + 2 + 1 + 4 * 7 */
				uPort = pIoCtrl->u.uData[0];
				if (uPort >= SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uint32_t	bRemoval;

					DPI[uPort].byIngressPriorityTOS		= (uint8_t)pIoCtrl->u.uData[1];
					DPI[uPort].byIngressPriority802_1P	= (uint8_t)pIoCtrl->u.uData[2];
					DPI[uPort].byIngressPriority		= (uint8_t)pIoCtrl->u.uData[3];
					DPI[uPort].byEgressPriority			= (uint8_t)pIoCtrl->u.uData[4];
					bRemoval = (uint8_t)pIoCtrl->u.uData[5];
					DPI[uPort].byStormProtection		= (uint8_t)pIoCtrl->u.uData[6];
#ifdef	DEBUG_THIS
					DRV_INFO("%s> port=%d, DSCPEnable=%d, IngressPriority802_1P=%d, IngressPriority=%d,"
						"EgressPriority=%d, IngressTagRemoval=%d, StormProtection=%d", __FUNCTION__, uPort, 
						DPI[uPort].byIngressPriorityTOS, DPI[uPort].byIngressPriority802_1P, DPI[uPort].byIngressPriority,
						DPI[uPort].byEgressPriority, bRemoval, DPI[uPort].byStormProtection);
#endif
					swConfigurePort(Adapter, uPort);
					swConfigTagRemoval(Adapter, uPort, bRemoval);
				}
				else {
					pIoCtrl->u.uData[1]	= (uint32_t)DPI[uPort].byIngressPriorityTOS;
					pIoCtrl->u.uData[2]	= (uint32_t)DPI[uPort].byIngressPriority802_1P;
					pIoCtrl->u.uData[3]	= (uint32_t)DPI[uPort].byIngressPriority;
					pIoCtrl->u.uData[4]	= (uint32_t)DPI[uPort].byEgressPriority;
					pIoCtrl->u.uData[6]	= (uint32_t)DPI[uPort].byStormProtection;
					uReg = KS8695_READ_REG(KS8695_SWITCH_ADVANCED);
					pIoCtrl->u.uData[5] = uReg & (1L << (22 + uPort)) ? TRUE : FALSE;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_ADV_LINK_SELECTION:
			if (pIoCtrl->usLen >= 16) { /* 1 + 2 + 1 + 4 * (3 | 4) */
				uPort = pIoCtrl->u.uData[0];
				if (uPort >= SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				if (DMA_LAN != DI.usDMAId) {
					uPort = 0;
				}
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uint32_t	uDuplex;

					if (pIoCtrl->usLen < 20) {
						uDuplex = 0;
					}
					else {
						uDuplex = pIoCtrl->u.uData[3];
					}
					/* auto nego or forced mode */
					DI.byDisableAutoNego[uPort] = pIoCtrl->u.uData[1];
					swConfigureMediaType(Adapter, uPort, pIoCtrl->u.uData[2], uDuplex);
				}
				else {
					pIoCtrl->u.uData[1] = DI.usCType[uPort];
					pIoCtrl->u.uData[2] = (uint32_t)DI.byDisableAutoNego[uPort];
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_ADV_CTRL:
			if (pIoCtrl->usLen == 24) { /* 1 + 2 + 1 + 4 * 5 */
				uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL0);
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					if (pIoCtrl->u.uData[0]) {
						uReg |= SW_CTRL0_FLOWCTRL_FAIR;
					}
					else {
						uReg &= ~SW_CTRL0_FLOWCTRL_FAIR;
					}
					if (pIoCtrl->u.uData[1]) {
						uReg |= SW_CTRL0_LEN_CHECKING;
					}
					else {
						uReg &= ~SW_CTRL0_LEN_CHECKING;
					}
					if (pIoCtrl->u.uData[2]) {
						uReg |= SW_CTRL0_AUTO_FAST_AGING;
					}
					else {
						uReg &= ~SW_CTRL0_AUTO_FAST_AGING;
					}
					if (pIoCtrl->u.uData[3]) {
						uReg |= SW_CTRL0_NO_BCAST_STORM_PROT;
					}
					else {
						uReg &= ~SW_CTRL0_NO_BCAST_STORM_PROT;
					}
					uReg &= 0xfffffff3;	/* clear priority scheme fields, 3:2 */
					uReg |= (pIoCtrl->u.uData[4] & 0x3) << 2;
					KS8695_WRITE_REG(KS8695_SWITCH_CTRL0, uReg);
					/* need 20 cpu clock delay for switch related registers */
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = uReg & SW_CTRL0_FLOWCTRL_FAIR ? TRUE : FALSE;
					pIoCtrl->u.uData[1] = uReg & SW_CTRL0_LEN_CHECKING ? TRUE : FALSE;
					pIoCtrl->u.uData[2] = uReg & SW_CTRL0_AUTO_FAST_AGING ? TRUE : FALSE;
					pIoCtrl->u.uData[3] = uReg & SW_CTRL0_NO_BCAST_STORM_PROT ? TRUE : FALSE;
					pIoCtrl->u.uData[4] = (uReg >> 2) & 0x3;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_ADV_MIRRORING:
			if (pIoCtrl->usLen == 24) { /* 1 + 2 + 1 + 4 * 5 */
				uReg = KS8695_READ_REG(KS8695_SWITCH_ADVANCED);
				/* note that the port start from 1 - 5 instead of 0 - 5 used internally in the driver */
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uReg &= 0xfffe0000;		/* currently, WEB page allows only on sniffer */
					/* sniffer port */
					if (pIoCtrl->u.uData[0] > 0 && pIoCtrl->u.uData[0] <= 5) {
						uReg |= (1L << (pIoCtrl->u.uData[0] - 1)) << 11;
					}
					/* Tx mirror port */
					if (pIoCtrl->u.uData[1] > 0 && pIoCtrl->u.uData[1] <= 5) {
						uReg |= (1L << (pIoCtrl->u.uData[1] - 1)) << 6;
					}
					/* Rx mirror port */
					if (pIoCtrl->u.uData[2] > 0 && pIoCtrl->u.uData[2] <= 5) {
						uReg |= (1L << (pIoCtrl->u.uData[2] - 1)) << 1;
					}
					/* sniffer mode, 1 for AND 0 for OR */
					if (pIoCtrl->u.uData[3]) {
						uReg |= 0x00010000;		/* bit 16 */
					}
					/* IGMP trap enable */
					if (pIoCtrl->u.uData[4]) {
						uReg |= 0x00000001;		/* bit 0 */
					}
					KS8695_WRITE_REG(KS8695_SWITCH_ADVANCED, uReg);
					/* need 20 cpu clock delay for switch related registers */
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = (uReg >> 11) & 0x1f;
					pIoCtrl->u.uData[1] = (uReg >> 6) & 0x1f;
					pIoCtrl->u.uData[2] = (uReg >> 1) & 0x1f;
					pIoCtrl->u.uData[3] = (uReg & 0x00010000) ? TRUE : FALSE;
					pIoCtrl->u.uData[4] = (uReg & 0x00000001) ? TRUE : FALSE;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_ADV_THRESHOLD:
			if (pIoCtrl->usLen == 12) { /* 1 + 2 + 1 + 4 * 2 */
				uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL1);	/* 0xE804 */
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uReg &= 0x00ffffff;	/* bit 31:24 */
					uReg |= (pIoCtrl->u.uData[0] & 0xff) << 24;
					KS8695_WRITE_REG(KS8695_SWITCH_CTRL1, uReg);
					DelayInMicroseconds(10);

					uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL0);
					uReg &= 0x8fffffff;	/* bit 30:28 */
					uReg |= (pIoCtrl->u.uData[1] & 0x07) << 28;
					KS8695_WRITE_REG(KS8695_SWITCH_CTRL0, uReg);
					/* need 20 cpu clock delay for switch related registers */
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = (uReg >> 24);
					uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL0);
					pIoCtrl->u.uData[1] = (uReg >> 28) & 0x07;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_ADV_DSCP:
			if (pIoCtrl->usLen == 12) { /* 1 + 2 + 1 + 4 * 2 */
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					/* DSCP high */
					KS8695_WRITE_REG(KS8695_DSCP_HIGH, pIoCtrl->u.uData[0]);
					DelayInMicroseconds(10);
					/* DSCP low */
					KS8695_WRITE_REG(KS8695_DSCP_LOW, pIoCtrl->u.uData[1]);
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = KS8695_READ_REG(KS8695_DSCP_HIGH);
					pIoCtrl->u.uData[1] = KS8695_READ_REG(KS8695_DSCP_LOW);
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_INTERNAL_LED:
			if (pIoCtrl->usLen == 12) { /* 1 + 2 + 1 + 4 * 2 */
				uint8_t	byLed0, byLed1;

				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					byLed0 = (uint8_t)pIoCtrl->u.uData[0];
					byLed1 = (uint8_t)pIoCtrl->u.uData[1];
					if (byLed0 <= LED_LINK_ACTIVITY && byLed1 <= LED_LINK_ACTIVITY) {
						swSetLED(Adapter, FALSE, byLed0);
						swSetLED(Adapter, TRUE, byLed1);

						/* can we set WAN here as well or separate it to WAN driver ? */
						{
							uReg = KS8695_READ_REG(KS8695_WAN_CONTROL);
							uReg &= 0xffffff88;		/* 6:4, 2:0 */
							uReg |= (uint32_t)byLed1 << 4;
							uReg |= (uint32_t)byLed0;
							KS8695_WRITE_REG(KS8695_WAN_CONTROL, uReg);
							/* need 20 cpu clock delay for switch related registers */
							DelayInMicroseconds(10);
						}
					}
					else {	/* out of range error */
						DRV_WARN("%s> LED setting out of range", __FUNCTION__);
						break;
					}
				}
				else {
					/* note that currently, all LED use same settings, so there is no
					   need to define port in the IF */
					/* LAN */
					uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL0);

					pIoCtrl->u.uData[0] = (uint32_t)((uReg >> 22) & 0x7);
					pIoCtrl->u.uData[1] = (uint32_t)((uReg >> 25) & 0x7);
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_INTERNAL_MISC:
			if (pIoCtrl->usLen == 44) { /* 1 + 2 + 1 + 4 * 10 */
				uReg = KS8695_READ_REG(KS8695_SWITCH_CTRL0);
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					if (pIoCtrl->u.uData[0]) {
						uReg |= SW_CTRL0_ERROR_PKT;
					}
					else {
						uReg &= ~SW_CTRL0_ERROR_PKT;
					}
					if (pIoCtrl->u.uData[1]) {
						uReg |= SW_CTRL0_BUFFER_SHARE;
					}
					else {
						uReg &= ~SW_CTRL0_BUFFER_SHARE;
					}
					if (pIoCtrl->u.uData[2]) {
						uReg |= SW_CTRL0_AGING_ENABLE;
					}
					else {
						uReg &= ~SW_CTRL0_AGING_ENABLE;
					}
					if (pIoCtrl->u.uData[3]) {
						uReg |= SW_CTRL0_FAST_AGING;
					}
					else {
						uReg &= ~SW_CTRL0_FAST_AGING;
					}
					if (pIoCtrl->u.uData[4]) {
						uReg |= SW_CTRL0_FAST_BACKOFF;
					}
					else {
						uReg &= ~SW_CTRL0_FAST_BACKOFF;
					}
					if (pIoCtrl->u.uData[5]) {
						uReg |= SW_CTRL0_6K_BUFFER;
					}
					else {
						uReg &= ~SW_CTRL0_6K_BUFFER;
					}
					if (pIoCtrl->u.uData[6]) {
						uReg |= SW_CTRL0_MISMATCH_DISCARD;
					}
					else {
						uReg &= ~SW_CTRL0_MISMATCH_DISCARD;
					}
					if (pIoCtrl->u.uData[7]) {
						uReg |= SW_CTRL0_COLLISION_DROP;
					}
					else {
						uReg &= ~SW_CTRL0_COLLISION_DROP;
					}
					if (pIoCtrl->u.uData[8]) {
						uReg |= SW_CTRL0_BACK_PRESSURE;
					}
					else {
						uReg &= ~SW_CTRL0_BACK_PRESSURE;
					}
					if (pIoCtrl->u.uData[9]) {
						uReg |= SW_CTRL0_PREAMBLE_MODE;
					}
					else {
						uReg &= ~SW_CTRL0_PREAMBLE_MODE;
					}
					KS8695_WRITE_REG(KS8695_SWITCH_CTRL0, uReg);
					/* need 20 cpu clock delay for switch related registers */
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = uReg & SW_CTRL0_ERROR_PKT ? TRUE : FALSE;
					pIoCtrl->u.uData[1] = uReg & SW_CTRL0_BUFFER_SHARE ? TRUE : FALSE;
					pIoCtrl->u.uData[2] = uReg & SW_CTRL0_AGING_ENABLE ? TRUE : FALSE;
					pIoCtrl->u.uData[3] = uReg & SW_CTRL0_FAST_AGING ? TRUE : FALSE;
					pIoCtrl->u.uData[4] = uReg & SW_CTRL0_FAST_BACKOFF ? TRUE : FALSE;
					pIoCtrl->u.uData[5] = uReg & SW_CTRL0_6K_BUFFER ? TRUE : FALSE;
					pIoCtrl->u.uData[6] = uReg & SW_CTRL0_MISMATCH_DISCARD ? TRUE : FALSE;
					pIoCtrl->u.uData[7] = uReg & SW_CTRL0_COLLISION_DROP ? TRUE : FALSE;
					pIoCtrl->u.uData[8] = uReg & SW_CTRL0_BACK_PRESSURE ? TRUE : FALSE;
					pIoCtrl->u.uData[9] = uReg & SW_CTRL0_PREAMBLE_MODE ? TRUE : FALSE;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_INTERNAL_SPANNINGTREE:
			if (pIoCtrl->usLen == 20) { /* 1 + 2 + 1 + 4 * 4 */
				uint32_t	uTxSpanning, uRxSpanning;

				uPort = pIoCtrl->u.uData[0];
				if (uPort >= SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					uTxSpanning = pIoCtrl->u.uData[1];
					uRxSpanning = pIoCtrl->u.uData[2];
					DPI[uPort].byDisableSpanningTreeLearn = pIoCtrl->u.uData[3];
					if (uTxSpanning) {
						if (uRxSpanning)
							DPI[uPort].bySpanningTree = SW_SPANNINGTREE_ALL;
						else
							DPI[uPort].bySpanningTree = SW_SPANNINGTREE_TX;
					}
					else {
						if (uRxSpanning)
							DPI[uPort].bySpanningTree = SW_SPANNINGTREE_RX;
						else
							DPI[uPort].bySpanningTree = SW_SPANNINGTREE_NONE;
					}
					swConfigurePort(Adapter, uPort);
				}
				else {
					uTxSpanning = uRxSpanning = FALSE;
					if (SW_SPANNINGTREE_ALL == DPI[uPort].bySpanningTree) {
						uTxSpanning = uRxSpanning = TRUE;
					}
					else if (SW_SPANNINGTREE_TX == DPI[uPort].bySpanningTree) {
						uTxSpanning = TRUE;
					}
					else if (SW_SPANNINGTREE_RX == DPI[uPort].bySpanningTree) {
						uRxSpanning = TRUE;
					}
					pIoCtrl->u.uData[1] = uTxSpanning;
					pIoCtrl->u.uData[2] = uRxSpanning;
					pIoCtrl->u.uData[3] = (uint32_t)DPI[uPort].byDisableSpanningTreeLearn;
				}
				nRet = 0;
			}
			break;
			
#ifdef	CONFIG_ARCH_KS8695P
		case CONFIG_SW_SUBID_PHY_IF:
			if (pIoCtrl->usLen == 24) { /* 1 + 2 + 1 + 4 + 4 * 4 */
				u32	off, shift = 0;

				uPort = pIoCtrl->u.uData[0];
				if (DMA_WAN == DI.usDMAId)
					uPort = 0;
				if (uPort > SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				if (uPort == SW_MAX_LAN_PORTS)
					off = KS8695_WAN_POWERMAGR;
				else {
					if (uPort < 2)
						off = KS8695_LPPM12;
					else
						off = KS8695_LPPM34;
				}
				if (!(uPort % 2))
					shift = 1;
				uReg = KS8695_READ_REG(off);
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					enablePhyLoopback(Adapter, uPort, pIoCtrl->u.uData[1]);
					enableRemoteLoopback(Adapter, uPort, pIoCtrl->u.uData[2]);
					enablePhyIsolate(Adapter, uPort, pIoCtrl->u.uData[3]);
					forcePhyLink(Adapter, uPort, pIoCtrl->u.uData[4]);
				}
				else {
					pIoCtrl->u.uData[1] = (uReg & (KS8695_LPPM_PHY_LOOPBACK << (shift * 16))) ? 1 : 0;
					pIoCtrl->u.uData[2] = (uReg & (KS8695_LPPM_RMT_LOOPBACK << (shift * 16))) ? 1 : 0;
					pIoCtrl->u.uData[3] = (uReg & (KS8695_LPPM_PHY_ISOLATE << (shift * 16))) ? 1 : 0;
					pIoCtrl->u.uData[4] = (uReg & (KS8695_LPPM_FORCE_LINK << (shift * 16))) ? 1 : 0;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_SEC1:
			if (pIoCtrl->usLen == 36) { /* 1 + 2 + 1 + 4 * 8 */
				uReg = KS8695_READ_REG(KS8695_SEC1);
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					if (pIoCtrl->u.uData[0])
						uReg |= KS8695_SEC1_NO_IEEE_AN;
					else
						uReg &= ~KS8695_SEC1_NO_IEEE_AN;
					if (pIoCtrl->u.uData[1])
						uReg |= KS8695_SEC1_TPID_MODE;
					else
						uReg &= ~KS8695_SEC1_TPID_MODE;
					if (pIoCtrl->u.uData[2])
						uReg |= KS8695_SEC1_NO_TX_8021X_FLOW_CTRL;
					else
						uReg &= ~KS8695_SEC1_NO_TX_8021X_FLOW_CTRL;
					if (pIoCtrl->u.uData[3])
						uReg |= KS8695_SEC1_NO_RX_8021X_FLOW_CTRL;
					else
						uReg &= ~KS8695_SEC1_NO_RX_8021X_FLOW_CTRL;
					if (pIoCtrl->u.uData[4])
						uReg |= KS8695_SEC1_HUGE_PACKET;
					else
						uReg &= ~KS8695_SEC1_HUGE_PACKET;
					if (pIoCtrl->u.uData[5])
						uReg |= KS8695_SEC1_8021Q_VLAN_EN;
					else
						uReg &= ~KS8695_SEC1_8021Q_VLAN_EN;
					if (pIoCtrl->u.uData[6])
						uReg |= KS8695_SEC1_MII_10BT;
					else
						uReg &= ~KS8695_SEC1_MII_10BT;
					if (pIoCtrl->u.uData[7])
						uReg |= KS8695_SEC1_NULL_VID;
					else
						uReg &= ~KS8695_SEC1_NULL_VID;
					KS8695_WRITE_REG(KS8695_SEC1, uReg);
					DelayInMicroseconds(10);
				}
				else {
					pIoCtrl->u.uData[0] = (uReg & KS8695_SEC1_NO_IEEE_AN) ? 1 : 0;
					pIoCtrl->u.uData[1] = (uReg & KS8695_SEC1_TPID_MODE) ? 1 : 0;
					pIoCtrl->u.uData[2] = (uReg & KS8695_SEC1_NO_TX_8021X_FLOW_CTRL) ? 1 : 0;
					pIoCtrl->u.uData[3] = (uReg & KS8695_SEC1_NO_RX_8021X_FLOW_CTRL) ? 1 : 0;
					pIoCtrl->u.uData[4] = (uReg & KS8695_SEC1_HUGE_PACKET) ? 1 : 0;
					pIoCtrl->u.uData[5] = (uReg & KS8695_SEC1_8021Q_VLAN_EN) ? 1 : 0;
					pIoCtrl->u.uData[6] = (uReg & KS8695_SEC1_MII_10BT) ? 1 : 0;
					pIoCtrl->u.uData[7] = (uReg & KS8695_SEC1_NULL_VID) ? 1 : 0;
				}
				nRet = 0;
			}
			break;

		case CONFIG_SW_SUBID_GENERIC_DUMP:
			if (pIoCtrl->usLen == 8) { /* 1 + 2 + 1 + 4 */
				int i;

				switch (pIoCtrl->u.uData[0]) {
					case GENERIC_DUMP_DYNAMIC:
						dumpDynamicMacTable(Adapter);
						nRet = 0;
						break;

					case GENERIC_DUMP_SWITCH_REGS:
						printk("--Reg--   ---Value--\n");
						for (i = KS8695_SEC0; i <= KS8695_LPPM34; i += 4) {
							printk(" 0x%04x   0x%08x\n", i, KS8695_READ_REG(i));
						}
						nRet = 0;
						break;

					case GENERIC_DUMP_STATIC:
						dumpStaticMacTable(Adapter);
						nRet = 0;
						break;

					default:
					case GENERIC_DUMP_VLAN:
						DRV_INFO("%s> not implemented yet", __FUNCTION__);
						break;
				};
			}
			break;

		case CONFIG_SW_SUBID_RATE_CTRL:
			if (pIoCtrl->usLen == 32) { /* 1 + 2 + 4 * 7 */
				u32	off, tx, v1 = 0;

				uPort = pIoCtrl->u.uData[0];
				if (DMA_WAN == DI.usDMAId)
					uPort = 0;
				if (uPort > SW_MAX_LAN_PORTS) {
					DRV_WARN("%s> LAN port out of range (%d)", __FUNCTION__, uPort);
					break;
				}
				tx = pIoCtrl->u.uData[1];
				if (uPort == SW_MAX_LAN_PORTS) {
					off = KS8695_SEP5C3;
					if (tx) v1 = KS8695_READ_REG(KS8695_SEP5C2);
				} else {
					off = KS8695_SEP1C3 + uPort * 0x0c;
					if (tx) v1 = KS8695_READ_REG(KS8695_SEP1C2 + uPort * 0x0c);
				}
				uReg = KS8695_READ_REG(off);
				if (CONFIG_SWITCH_SET == pIoCtrl->byId) {
					if (tx) {
						setTxRate(Adapter, uPort, pIoCtrl->u.uData[2], pIoCtrl->u.uData[3]);
						enableTxRateControl(Adapter, uPort, pIoCtrl->u.uData[4], pIoCtrl->u.uData[5], pIoCtrl->u.uData[6]);
					}
					else {
						setRxRate(Adapter, uPort, pIoCtrl->u.uData[2], pIoCtrl->u.uData[3]);
						enableRxRateControl(Adapter, uPort, pIoCtrl->u.uData[4], pIoCtrl->u.uData[5], pIoCtrl->u.uData[6]);
					}
				}
				else {
					if (tx) {
						pIoCtrl->u.uData[2] = (v1 & KS8695_SEPC2_TX_L_RATECTRL_MASK);
						pIoCtrl->u.uData[3] = ((v1 & KS8695_SEPC2_TX_H_RATECTRL_MASK) >> 12);
						pIoCtrl->u.uData[4] = (uReg & KS8695_SEPC3_TX_DIF_RATECTRL_EN) ? 1 : 0;
						pIoCtrl->u.uData[5] = (uReg & KS8695_SEPC3_TX_L_RATECTRL_EN) ? 1 : 0;
						pIoCtrl->u.uData[6] = (uReg & KS8695_SEPC3_TX_H_RATECTRL_EN) ? 1 : 0;
					}
					else {
						pIoCtrl->u.uData[2] = ((uReg & KS8695_SEPC3_RX_L_RATECTRL_MASK) >> 8);
						pIoCtrl->u.uData[3] = ((uReg & KS8695_SEPC3_RX_H_RATECTRL_MASK) >> 20);
						pIoCtrl->u.uData[4] = (uReg & KS8695_SEPC3_RX_DIF_RATECTRL_EN) ? 1 : 0;
						pIoCtrl->u.uData[5] = (uReg & KS8695_SEPC3_RX_L_RATECTRL_EN) ? 1 : 0;
						pIoCtrl->u.uData[6] = (uReg & KS8695_SEPC3_RX_H_RATECTRL_EN) ? 1 : 0;
					}
				}
				nRet = 0;
			}
			break;
#endif

		default:
			DRV_INFO("%s> unsupported parameters: id=%d, len=%d", __FUNCTION__, pIoCtrl->byId, pIoCtrl->usLen);
			return -EOPNOTSUPP;
	}

	return nRet;
}

#ifdef	__KS8695_CACHE_H
/*
 * ks8695_icache_lock
 *	This function is use to lock given icache
 *
 * Argument(s)
 *	icache_start	pointer to starting icache address
 *	icache_end		pointer to ending icache address
 *
 * Return(s)
 *	0		if success
 *	error	otherwise
 */
int ks8695_icache_lock2(void *icache_start, void *icache_end)
{
	uint32_t victim_base = ICACHE_VICTIM_BASE << ICACHE_VICTIM_INDEX;
	spinlock_t	lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;
#ifdef	DEBUG_THIS
	int	len;

	len = (int)(icache_end - icache_start);
	DRV_INFO("%s: start=%p, end=%p, len=%d", __FUNCTION__, icache_start, icache_end, len);
	/* if lockdown lines are more than half of max associtivity */
	if ((len / ICACHE_BYTES_PER_LINE) > (ICACHE_ASSOCITIVITY >> 1)) {
		DRV_WARN("%s: lockdown lines = %d is too many, (Assoc=%d)", __FUNCTION__, (len / ICACHE_BYTES_PER_LINE), ICACHE_ASSOCITIVITY);
		return -1;
	}
#endif

	spin_lock_irqsave(&lock, flags);
	
	__asm__(
		" \n\
		ADRL	r0, ks8695_isr		/* compile complains if icache_start is given instead */ \n\
		ADRL	r1, ks8695_isre		/* compile complains if icache_end is given instead */ \n\
		MOV	r2, %0 \n\
		MCR	p15, 0, r2, c9, c4, 1 \n\
 \n\
lock_loop: \n\
		MCR	p15, 0, r0, c7, c13, 1 \n\
		ADD	r0, r0, #32 \n\
 \n\
		AND	r3, r0, #0x60 \n\
		CMP	r3, #0x0 \n\
		ADDEQ	r2, r2, #0x1<<26		/* ICACHE_VICTIM_INDEX */ \n\
		MCREQ	p15, 0, r2, c9, c0, 1 \n\
		  \n\
		CMP	r0, r1 \n\
		BLE	lock_loop \n\
 \n\
		CMP	r3, #0x0 \n\
		ADDNE	r2, r2, #0x1<<26		/* ICACHE_VICTIM_INDEX */ \n\
		MCRNE	p15, 0, r2, c9, c0,	1 \n\
		"
		:
		: "r" (victim_base)
		: "r0", "r1", "r2", "r3"
	);

#ifdef	DEBUG_THIS
	ks8695_icache_read_c9();
#endif

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}
#endif /*__KS8695_CACHE_H*/

#ifdef	RX_TASK
/*
 * ReceiveProcessTask
 *	This function is use to process Rx interrupt, send received data up
 *	the network stack. 
 *
 * Argument(s)
 *	data		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	NONE
 */
static void ReceiveProcessTask(uintptr_t data)
{
	PADAPTER_STRUCT Adapter = (PADAPTER_STRUCT)data;
	UINT32 uFrameCtrl;
	RXDESC *CurrentDescriptor;

#ifdef	DEBUG_THIS
	DRV_INFO("%s> )", __FUNCTION__); 
#endif

	/* 
         * Process receive packet by call ProcessRxInterrupts() 
	*/ 
	ProcessRxInterrupts(Adapter);

	/* 
         * if there are pending rx interrupt, reschedule rx task again 
	*/
	CurrentDescriptor = &DI.pRxDescriptors[DI.nRxDescNextAvail];
	uFrameCtrl = CurrentDescriptor->RxFrameControl; 
	if  (!(uFrameCtrl & DESC_OWN_BIT)) 
    	{
#ifdef	TX_TASK
		if (DI.nTransmitCount > (DI.nTxDescTotal >> 1)) {
			/* try eanble read transfer again */
			KS8695_WRITE_REG(REG_TXSTART + DI.nOffset, 1);
			if (FALSE == DI.tx_scheduled) {
				DI.tx_scheduled = TRUE;
				tasklet_hi_schedule(&DI.tx_tasklet);
			}
		}
#endif
		tasklet_hi_schedule(&DI.rx_tasklet);
	}	
	else 
    	{
		DI.rx_scheduled = FALSE;
		/* enable corresponding Rx Interrupt again */
		KS8695_WRITE_REG(KS8695_INT_ENABLE, KS8695_READ_REG(KS8695_INT_ENABLE) | 
			((uint32_t)INT_RX_BIT << DI.uIntShift));
	}
}

#endif /*RX_TASK*/

#ifdef	TX_TASK
/*
 * TransmitProcessTask
 *	This function is use to process Tx interrupt in task level, reclaim resources after 
 *	transmit completed.
 *
 * Argument(s)
 *	data		pointer to ADAPTER_STRUCT structure.
 *
 * Return(s)
 *	NONE.
 */
static void TransmitProcessTask(uintptr_t data)
{
	PADAPTER_STRUCT Adapter = (PADAPTER_STRUCT)data;


#ifdef	DEBUG_THIS
	DRV_INFO("%s> )", __FUNCTION__); 
#endif

	/* 
     * Process free transmit data buffer by call ProcessTxInterrupts() 
     */ 
	ProcessTxInterrupts(Adapter);


	/* 
     * if there are pending tx interrupt, reschedule tx task again 
     */
#ifndef	USE_TX_UNAVAIL
	if (KS8695_READ_REG(KS8695_INT_STATUS) & ((uint32_t)INT_TX_BIT << DI.uIntShift)) {
#else
	if (KS8695_READ_REG(KS8695_INT_STATUS) & 
		((uint32_t)(INT_TX_BIT | INT_TX_UNAVAIL_BIT) << DI.uIntShift) & DI.uIntMask) {
#endif
		/* Acknowledge the transmit interrupt let this routine becomes
		 * an infinite loop.
		 */
		KS8695_WRITE_REG( KS8695_INT_STATUS,
			( uint32_t ) INT_TX_BIT << DI.uIntShift );
		tasklet_hi_schedule(&DI.tx_tasklet);
	}	
	else 
    {
		DI.tx_scheduled = FALSE;
		/* enable corresponding Tx Interrupt again */
#ifndef	USE_TX_UNAVAIL
		KS8695_WRITE_REG(KS8695_INT_ENABLE, KS8695_READ_REG(KS8695_INT_ENABLE) | 
			((uint32_t)INT_TX_BIT << DI.uIntShift));
#else
		KS8695_WRITE_REG(KS8695_INT_ENABLE, KS8695_READ_REG(KS8695_INT_ENABLE) | 
			((uint32_t)(INT_TX_BIT | INT_TX_UNAVAIL_BIT) << DI.uIntShift) & DI.uIntMask);
#endif
	}



}
#endif /*TX_TASK*/

/* ks8695_main.c */
