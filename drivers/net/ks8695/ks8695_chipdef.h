/*
 *  ks8695_chipdef.h
 *      This file defines the driver-independent constants, macro and
 *      structures used in the Linux driver.
 *
 *  Copyright (c) 1998-2002 by Micrel-Kendin Operations. All rights reserved.
 *
 *  Modification History
 *
 *  Name        Date        Ver     Brief
 *  ----------- ----------- ------- ------------------------------------------
 *	RLQ	05/13/2000  1.0.0.0 Modified based on KS8695 Linux driver
 *
 */
#ifndef __KS8695_CHIPDEF_H
#define __KS8695_CHIPDEF_H

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

/* should change to system wise platform.h instead of local copy */
//#include <asm/arch/platform.h>
//#include "platform.h"

#ifndef	BIT
#define BIT(x) (1 << (x))
#endif

#ifndef	REG_MISC_CONTROL

#define REG_MISC_CONTROL		0xEA08
#define REG_LAN12_POWERMAGR		0xE84C
#define REG_LAN34_POWERMAGR		0xE850

#endif

/* DMA related register offset */
#define	REG_TXCTRL				0x0000
#define	REG_RXCTRL				0x0004
#define	REG_TXSTART				0x0008
#define	REG_RXSTART				0x000c
#define	REG_TXBASE				0x0010
#define	REG_RXBASE				0x0014
#define	REG_STATION_LOW			0x0018
#define	REG_STATION_HIGH		0x001c

#define	REG_MAC0_LOW			0x0080
#define	REG_MAC0_HIGH			0x0084
#define	REG_MAC1_LOW			0x0088
#define	REG_MAC1_HIGH			0x008c
#define	REG_MAC2_LOW			0x0090
#define	REG_MAC2_HIGH			0x0094
#define	REG_MAC3_LOW			0x0098
#define	REG_MAC3_HIGH			0x009c
#define	REG_MAC4_LOW			0x00a0
#define	REG_MAC4_HIGH			0x00a4
#define	REG_MAC5_LOW			0x00a8
#define	REG_MAC5_HIGH			0x00ac
#define	REG_MAC6_LOW			0x00b0
#define	REG_MAC6_HIGH			0x00b4
#define	REG_MAC7_LOW			0x00b8
#define	REG_MAC7_HIGH			0x00bc
#define	REG_MAC8_LOW			0x00c0
#define	REG_MAC8_HIGH			0x00c4
#define	REG_MAC9_LOW			0x00c8
#define	REG_MAC9_HIGH			0x00cc
#define	REG_MAC10_LOW			0x00d0
#define	REG_MAC10_HIGH			0x00d4
#define	REG_MAC11_LOW			0x00d8
#define	REG_MAC11_HIGH			0x00dc
#define	REG_MAC12_LOW			0x00e0
#define	REG_MAC12_HIGH			0x00e4
#define	REG_MAC13_LOW			0x00e8
#define	REG_MAC13_HIGH			0x00ec
#define	REG_MAC14_LOW			0x00f0
#define	REG_MAC14_HIGH			0x00f4
#define	REG_MAC15_LOW			0x00f8
#define	REG_MAC15_HIGH			0x00fc

/* register Bit field defines for Tx Ctrl and (some are shared with RX) */
#define	DMA_SOFTRESET			0x80000000		/* DMA soft reset (shared with RX) */
#define	DMA_UDPCHECKSUM			0x00040000		/* bit 18 (shared with RX) */
#define	DMA_TCPCHECKSUM			0x00020000		/* bit 17 (shared with RX) */
#define	DMA_IPCHECKSUM			0x00010000		/* bit 16 (shared with RX) */
#define	DMA_FLOWCTRL			0x00000200		/* bit 9 (shared with RX) */
#define	DMA_LOOPBACK			0x00000100		/* bit 8 */
#define	DMA_ERRORFRAME			0x00000008		/* bit 3 */
#define	DMA_PADDING				0x00000004		/* bit 2 */
#define	DMA_CRC					0x00000002		/* bit 1 */
#define	DMA_START				0x00000001		/* bit 0 (shared with RX) */

#define	DMA_PBLTMASK			0x3f000000		/* DMA Burst Size bit mask (shared with RX) */
#define	DMA_PBLTSHIFT			24				/* DMA Burst Size bit shift */

/* some bits for RX Ctrl register */
#define	DMA_BROADCAST			0x00000040		/* bit 6 */
#define	DMA_MULTICAST			0x00000020		/* bit 5 */
#define	DMA_UNICAST				0x00000010		/* bit 4 */
#define	DMA_PROMISCUOUS			0x00000004		/* bit 2 */

/* Addition station registers */
#define	DMA_MACENABLE			0x80000000		/* enable/disable additional MAC station address */

enum DMAID {
	DMA_WAN = 0x6000,			/* WAN DMA */
	DMA_LAN = 0x8000,			/* LAN DMA */
#ifndef	CONFIG_ARCH_KS8695P
	DMA_HPNA= 0xA000			/* HPNA DMA */
#endif
};

/* DESC and Data buffer */
#define DESC_ALIGNMENT			16	/* two dwords */

/* Receive Descriptor */
typedef struct
{
    volatile uint32_t RxFrameControl;
    volatile uint32_t RxDMAFragLen;
    volatile uint32_t RxDMAFragAddr;
    volatile uint32_t RxDMANextPtr;
} RXDESC, *PRXDESC;

#define DESC_OWN_BIT			0x80000000		/* shared with Tx descriptor */

/* In Linux, we use all 32 bits definitions! */
/* Bits related to RxFrameControl */
#define	RFC_FS					0x40000000		/* First Descriptor of the received frame */
#define	RFC_LS					0x20000000		/* Last Descriptor of the received frame */
#define	RFC_IPE					0x10000000		/* IP checksum generation */
#define	RFC_TCPE				0x08000000		/* TCP checksum generation */
#define	RFC_UDPE				0x04000000		/* UDP checksum generation */
#define	RFC_ES					0x02000000		/* Error Summary */
#define	RFC_MF					0x01000000		/* Multicast Frame */

#define	RFC_RE					0x00080000		/* Report on MII/GMII error */
#define	RFC_TL					0x00040000		/* Frame Too Long */
#define	RFC_RF					0x00020000		/* Runt Frame */
#define	RFC_CRC					0x00010000		/* CRC error */
#define	RFC_FT					0x00008000		/* Frame Type */

#define	RFC_SPN_MASK			0x00f00000		/* Switch engine destination port map, 20:23 */
#define	RFC_FL_MASK				0x000007ff		/* Frame Length bit mask, 0:10 */
#define	RFC_FRAMECTRL_MASK		(RFC_FS	| RFC_LS | RFC_ES | RFC_MF | RFC_RE | RFC_TL | RFC_CRC | RFC_FT | RFC_FL_MASK)

/* Bits related to RxDMAFragLen */
#define	RFC_RER					0x02000000		/* Receive End of Ring */
#define	RFC_RBS_MASK			0x000007ff		/* Receive buffer Size bit mask, 0:10 */

/* Transmit descriptor */
typedef struct
{
    volatile uint32_t TxOwnBit;
    volatile uint32_t TxFrameControl;
    volatile uint32_t TxDMAFragAddr;
    volatile uint32_t TxDMANextPtr;
} TXDESC, *PTXDESC;

/* Bits related to TxFrameControl */
#define	TFC_IC					0x80000000		/* Interrupt on completion */
#define	TFC_FS					0x40000000		/* first segment */
#define	TFC_LS					0x20000000		/* last segment */
#define	TFC_IPCKG				0x10000000		/* IP checksum generation */
#define	TFC_TCPCKG				0x08000000		/* TCP checksum generation */
#define	TFC_UDPCKG				0x04000000		/* UDP checksum generation */
#define	TFC_TER					0x02000000		/* Transmit End of Ring */

#define	TFC_SPN_MASK			0x00f00000		/* Switch engine destination port map, 20:23 */
#define	TFC_TBS_MASK			0x000007ff		/* Transmit Buffer Size Mask (0:10) */
#define	TFC_FRAMECTRL_MASK		(TFC_IC	| TFC_FS | TFC_LS | TFC_SPN_MASK | TFC_TBS_MASK)

/* Interrupt related (shared among IMR, IER, ISR, IPR, and IQR) */
#define	INT_WAN_LINK			0x80000000		/* WAN link change interrupt */
#define	INT_WAN_TX				0x40000000		/* WAN Tx complete interrupt */
#define	INT_WAN_RX				0x20000000		/* WAN Rx complete interrupt */
#define	INT_WAN_TX_UNAVIAL		0x10000000		/* WAN Tx desc unavailable interrupt */
#define	INT_WAN_RX_UNAVIAL		0x08000000		/* WAN Rx desc unavailable interrupt */
#define	INT_WAN_TX_STOPPED		0x04000000		/* WAN Tx stopped interrupt */
#define	INT_WAN_RX_STOPPED		0x02000000		/* WAN Rx stopped interrupt */

#define	INT_WAN_MASK			0x7e000000		/* not include LINK interrupt bit */

#define	INT_AMBA_BUS_ERROR		0x01000000		/* AMBA bus error interrupt */

#define	INT_HPNA_TX				0x00800000		/* HPNA Tx complete interrupt */
#define	INT_HPNA_RX				0x00400000		/* HPNA Rx complete interrupt */
#define	INT_HPNA_TX_UNAVIAL		0x00200000		/* HPNA Tx desc unavailable interrupt */
#define	INT_HPNA_RX_UNAVIAL		0x00100000		/* HPNA Rx desc unavailable interrupt */
#define	INT_HPNA_TX_STOPPED		0x00080000		/* HPNA Tx stopped interrupt */
#define	INT_HPNA_RX_STOPPED		0x00040000		/* HPNA Rx stopped interrupt */

#define	INT_HPNA_MASK			0x00fc0000

#define	INT_LAN_TX				0x00020000		/* LAN Tx complete interrupt */
#define	INT_LAN_RX				0x00010000		/* LAN Rx complete interrupt */
#define	INT_LAN_TX_UNAVIAL		0x00008000		/* LAN Tx desc unavailable interrupt */
#define	INT_LAN_RX_UNAVIAL		0x00004000		/* LAN Rx desc unavailable interrupt */
#define	INT_LAN_TX_STOPPED		0x00002000		/* LAN Tx stopped interrupt */
#define	INT_LAN_RX_STOPPED		0x00001000		/* LAN Rx stopped interrupt */

#define	INT_LAN_MASK			0x0003f000

#define	INT_DMA_MASK			0xfefff000		/* interrupt bit mask for DMA (WAN, HPNA and LAN) */

#define	INT_DMA_STOP_MASK		(INT_WAN_TX_STOPPED | INT_WAN_RX_STOPPED | INT_HPNA_TX_STOPPED | INT_HPNA_RX_STOPPED | INT_LAN_TX_STOPPED | INT_LAN_RX_STOPPED)
#define	INT_TX_BIT				BIT(5)
#define	INT_RX_BIT				BIT(4)
#define	INT_TX_UNAVAIL_BIT		BIT(3)
#define	INT_RX_UNAVAIL_BIT		BIT(2)
#define	INT_TX_STOPPED_BIT		BIT(1)
#define	INT_RX_STOPPED_BIT		BIT(0)

/* MAC address */
#define	MAC_ADDRESS_LEN			6
#define	MAC_MAX_EXTRA			16

typedef enum {
	LED_SPEED,			/* 0 */
	LED_LINK,
	LED_FD,				/* full duplex */
	LED_COLLISION,
	LED_ACTIVITY,
	LED_FD_COLLISION,	/* full duplex/collision */
	LED_LINK_ACTIVITY,	/* link/activities */
} LED_SELECTOR;

/* register Bit field for Switch control 0 */
#define	SW_CTRL0_AUTO_FAST_AGING	0x00100000	/* automic fast aging when link changed detected */
#define	SW_CTRL0_ERROR_PKT		0x00080000		/* pass all error packets */
#define	SW_CTRL0_ENABLE_PORT5	0x00040000		/* enable port 5 flow control */
#define	SW_CTRL0_ENABLE_PORTS	0x00020000		/* enable flow control for port 1 - 4 */
#define	SW_CTRL0_BUFFER_SHARE	0x00010000		/* buffer share mode */
#define	SW_CTRL0_AGING_ENABLE	0x00008000		/* aging enable */
#define	SW_CTRL0_FAST_AGING		0x00004000		/* fast aging enable */
#define	SW_CTRL0_FAST_BACKOFF	0x00002000		/* fast back off */
#define	SW_CTRL0_MISMATCH_DISCARD	0x00001000	/* VLAN mismatch discard */
#define	SW_CTRL0_NO_BCAST_STORM_PROT	0x00000800		/* no broadcast storm proection tp ,cast pkts */
#define	SW_CTRL0_PREAMBLE_MODE	0x00000400		/* back pressure mode */
#define	SW_CTRL0_FLOWCTRL_FAIR	0x00000200		/* flow control fair mode */
#define	SW_CTRL0_COLLISION_DROP	0x00000100		/* no excessive collision drop */
#define	SW_CTRL0_LEN_CHECKING	0x00000080		/* enforced max length checking */
#define	SW_CTRL0_6K_BUFFER		0x00000040		/* 6K byte buffer per port reserved for high priority pkts */
#define	SW_CTRL0_BACK_PRESSURE	0x00000020		/* back pressure enable */
#define	SW_CTRL0_SWITCH_ENABLE	0x00000001		/* enable switch bit */

/* register Bit field for Auto Regotiation */
#define	SW_AUTONEGO_COMPLETE	0x00004000		/* auto nego completed */
#define	SW_AUTONEGO_RESTART		0x00002000		/* auto nego restart */
#define	SW_AUTONEGO_ADV_PUASE	0x00001000		/* auto nego advertise PAUSE */
#define	SW_AUTONEGO_ADV_100FD	0x00000800		/* auto nego advertise 100 FD */
#define	SW_AUTONEGO_ADV_100HD	0x00000400		/* auto nego advertise 100 HD */
#define	SW_AUTONEGO_ADV_10FD	0x00000200		/* auto nego advertise 10 FD */
#define	SW_AUTONEGO_ADV_10HD	0x00000100		/* auto nego advertise 10 HD */
#define	SW_AUTONEGO_STAT_LINK	0x00000080		/* auto nego link status */
#define	SW_AUTONEGO_STAT_DUPLEX	0x00000040		/* auto nego duplex status (solved) */
#define	SW_AUTONEGO_STAT_SPEED	0x00000020		/* auto nego speed status (solved) */
#define	SW_AUTONEGO_PART_PAUSE	0x00000010		/* auto nego parterner pause */
#define	SW_AUTONEGO_PART_100FD	0x00000008		/* auto nego parterner 100 FD */
#define	SW_AUTONEGO_PART_100HD	0x00000004		/* auto nego parterner 100 HD */
#define	SW_AUTONEGO_PART_10FD	0x00000002		/* auto nego parterner 10 FD */
#define	SW_AUTONEGO_PART_10HD	0x00000001		/* auto nego parterner 10 HD */

#define	SW_AUTONEGO_ADV_MASK	0x00001f00

#define	SW_MAX_LAN_PORTS		4				/* max LAN ports */

/* bits for SNMP data register (SEMCD) */
#ifndef	CONFIG_ARCH_KS8695P
#define	SW_SNMP_DATA_VALID	0x80000000		/* counter value is valid */
#define	SW_SNMP_DATA_OVERFLOW	0x40000000		/* counter is overflow */
#else
#define	SW_SNMP_DATA_OVERFLOW	0x80000000		/* counter is overflow */
#define	SW_SNMP_DATA_VALID	0x40000000		/* counter value is valid */
#endif

enum PORTS {
	SW_PORT_1 = 0,
	SW_PORT_2,
	SW_PORT_3,
	SW_PORT_4
};

/* bits related to power management */
#define	POWER_POWERDOWN			0x00000010		/* port power down */
#define	POWER_DMDX_DISABLE		0x00000008		/* disable auto MDI/MDIX */
#define	POWER_FORCE_MDIX		0x00000004		/* if auto MDI/MDIX is disabled, force PHY into MDIX mode */
#define	POWER_LOOPBACK			0x00000002		/* PHY loopback */

#define SW_PHY_AUTO				0   /* autosense */
#define SW_PHY_10BASE_T			1   /* 10Base-T */
#define SW_PHY_10BASE_T_FD		2   /* 10Base-T Full Duplex */
#define SW_PHY_100BASE_TX		3   /* 100Base-TX */
#define SW_PHY_100BASE_TX_FD	4   /* 100Base-TX Full Duplex */

#define SW_PHY_DEFAULT			SW_PHY_AUTO

enum SPANNINGTREE {
	SW_SPANNINGTREE_NONE,		/* no spanning tree */
	SW_SPANNINGTREE_RX,			/* spanning tree, RX only */
	SW_SPANNINGTREE_TX,			/* spanning tree, TX only */
	SW_SPANNINGTREE_ALL,		/* spanning tree, both TX/RX */
};

/* bits related to port configuration register */
#define	SW_PORT_DISABLE_AUTONEG	0x00008000		/* port disable auto nego */
#define	SW_PORT_100BASE			0x00004000		/* force 100 when auto nego disabled */
#define	SW_PORT_FULLDUPLEX		0x00002000		/* force full duplex when auto nego disabled */
#define	SW_PORT_TX_SPANNINGTREE	0x00000080		/* spanning tree transmit enable */
#define	SW_PORT_RX_SPANNINGTREE	0x00000040		/* spanning tree receive enable */
#define	SW_PORT_NO_SPANNINGTREE	0x00000020		/* spanning tree disable */
#define	SW_PORT_STORM_PROCTION	0x00000010		/* broadcast storm protection (ingress) */
#define	SW_PORT_HI_PRIORITY		0x00000008		/* high priority (ingress) */
#define	SW_PORT_TOS_ENABLE		0x00000004		/* Enable TOS based priotiry classification (ingress) */
#define	SW_PORT_8021Q_ENABLE	0x00000002		/* Enable 802.1Q based priotiry classification (ingress) */
#define	SW_PORT_PRIOTIRY_ENABLE	0x00000001		/* Enable priotiry function on the port (egress) */

/* port 5 only */
#define	SW_PORT_RX_DIRECT_MODE	0x00004000		/* receive direct mode for port 5 */
#define	SW_PORT_TX_PRETAG_MODE	0x00002000		/* transmit pre-tag mode for port 5 */

typedef struct _PORT_INFO {
    uint16_t usTag;					/* tag value for the port (ingress) */
	uint8_t byCrossTalkMask;		/* specify ports that this port can talk to */
	uint8_t byStormProtection;		/* broadcast storm protection */
	uint8_t bySpanningTree;			/* spanning tree */
	uint8_t byDisableSpanningTreeLearn;	/* disable spanning tree learn for the port */
	uint8_t byIngressPriority;		/* ingress priority */
	uint8_t byIngressPriorityTOS;	/* TOS based ingress priority */
	uint8_t byIngressPriority802_1P;/* 802.1p based ingress priority */
	uint8_t byEgressPriority;		/* egress priority */
} PORT_INFO, *PPORT_INFO;

typedef struct _DMA_INFO {
	unsigned short	usDMAId;	/* DMAID */
	int32_t	nBaseAddr;		/* base address */
	int32_t	nOffset;		/* DMA register offset */
	uint8_t	bUseFIQ;		/* use FIQ or not */
	uint32_t *pbaseVa;
	uint32_t nResetCount;		/* DMA reset counter */

	/* interrupt related */
	uint32_t uIntMask;		/* interrupt mask checked within ISR */
	uint32_t uLinkIntMask;		/* WAN link interrupt mask checked within ISR */
	uint32_t uIntShift;		/* interrupt bit shift */

	/* mac */
	uint8_t stMacStation[MAC_ADDRESS_LEN];
	uint8_t stMacCurrent[MAC_ADDRESS_LEN];
	uint8_t stSwitchMac[MAC_ADDRESS_LEN];

	/* spinlock */
	spinlock_t	lock;		/* spin lock */
	spinlock_t	lock_refill;	/* spin lock */

	/* Tx related */
	uint8_t	bTxStarted;		/* Tx DMA started or stopped! */
	uint8_t	bTxFlowCtrl;		/* flow control for Tx DMA */
	uint8_t	bTxOffLoad;		/* enable/disable Task offload for Tx DMA */
	uint8_t	byTxPBL;		/* Tx PBL */
	uint8_t bTxChecksum;		/* checksum enable/disable indicator */
	uint8_t	bTxNoResource;		/* flag indicates out of Tx resource */
	uint32_t uDebugDumpTxPkt;	/* flag to dump tx packet for debugging */

	/* Tx desc related */
	int32_t	nTxDesc;		/* number of Tx descriptors */
	int32_t nTxDescNextAvail;	/* next available Tx descriptor */
	int32_t nTxDescUsed;		/* used Tx descriptor */
	volatile int32_t nTransmitCount;	/* number of packets to transmit */
	int32_t nTxProcessedCount;	/* number of packets to transmitted */
	int32_t	nTxDescTotal;		/* total number fo Tx descriptors */
	int32_t nTransmitCoalescing;	/* Tx packets coalescing count */

	TXDESC *pTxDescriptors;
	dma_addr_t TxDescDMA;
	struct ks8695_buffer *pTxSkb;
	atomic_t nTxDescAvail;

	/* Rx related */
	uint8_t	bRxStarted;		/* Rx DMA started or stopped! */
	uint8_t	bRxFlowCtrl;		/* flow control for Rx DMA */
	uint8_t	bPort5FlowCtrl;		/* flow control for LAN port 5 */
	uint8_t	bPortsFlowCtrl;		/* flow control for LAN port 1 - 4 */
	uint8_t	byRxPBL;		/* Rx PBL */
	uint8_t bRxChecksum;		/* checksum enable/disable indicator */
	uint32_t uRxBufferLen;		/* rx buffer length */
	uint32_t uDebugDumpRxPkt;	/* flag to dump rx packet for debugging */
	uint32_t uRx1518plus;		/* rx packet counter for 1518 plus for debugging */
	uint32_t uRxUnderSize;		/* rx packet counter for under size packets (< 64) */
	uint32_t nMaxFilledCount;	/* max refilled count */
	uint32_t nMaxProcessedCount;	/* max rx pkt count in one process cycle */

	int32_t nRxDesc;		/* number of Rx descriptors */
	int32_t nRxDescNextAvail;	/* next available Rx descriptor */
	int32_t nRxDescNextToFill;	/* next Rx desc to fill new buffer to */
	RXDESC *pRxDescriptors;
	dma_addr_t RxDescDMA;
	struct ks8695_buffer *pRxSkb;
	atomic_t RxDescEmpty;		/* atomic flag for empty Rx descriptor  */
	struct tasklet_struct rx_fill_tasklet;
	int32_t	nRxDescTotal;		/* total number fo Rx descriptors */
	int32_t	rx_fill_scheduled;	/* flag for rx fill schedule routine */

#ifdef	RX_TASK
	struct tasklet_struct rx_tasklet;
	int32_t	rx_scheduled;		/* flag for rx receive task schedule routine */
#endif

#ifdef	TX_TASK
	struct tasklet_struct tx_tasklet;
	int32_t	tx_scheduled;		/* flag for tx receive task schedule routine */
#endif

	/* PHY related */
	uint8_t	bAutoNegoInProgress[SW_MAX_LAN_PORTS];/* if set, means that auto nego is in progress!!! */
	uint8_t	bLinkActive[SW_MAX_LAN_PORTS];		/* flag indicates whether link is active or not */
	uint8_t bLinkChanged[SW_MAX_LAN_PORTS];		/* link changed indicator */
	uint8_t bHalfDuplex[SW_MAX_LAN_PORTS];		/* HD/FD */
	uint16_t usCType[SW_MAX_LAN_PORTS];		/* Convert type */
	uint16_t usLinkSpeed[SW_MAX_LAN_PORTS];		/* link speed */
	PORT_INFO	port[SW_MAX_LAN_PORTS + 1];	/* port related */
	int32_t	nLinkChangeCount;			/* trace link change count */
	uint8_t	byDisableAutoNego[SW_MAX_LAN_PORTS];	/* auto nego/disable auto nego/partial auto nego */
	uint8_t bHalfDuplexDetected[SW_MAX_LAN_PORTS];	/* HD/FD detected based on partner's settings */

	uint8_t	bRxDirectMode;				/* for port 5 only */
	uint8_t	bTxRreTagMode;				/* for port 5 only */

	uint8_t bPowerDownReset;	/* perform powerdown reset instead of soft reset */
} DMA_INFO, *PDMA_INFO;

typedef struct _INTCFG {
	uint8_t	bFIQ;		/* use FIQ */
	uint8_t	byPriority;	/* priority level for IRQ */
} INTCFG, *PINTCFG;

#endif	/*__KS8695_CHIPDEF_H*/
