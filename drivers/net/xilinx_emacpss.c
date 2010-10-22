/*
 * Xilinx Ethernet: Linux driver for Ethernet.
 *
 * Author: Xilinx, Inc.
 *
 * 2010 (c) Xilinx, Inc. This file is licensed uner the terms of the GNU
 * General Public License version 2.1. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * This is a driver for xilinx processor sub-system (pss) ethernet device.
 * This driver is mainly used in Linux 2.6.30 and above and it does _not_
 * support Linux 2.4 kernel due to certain new features (e.g. NAPI) is
 * introduced in this driver.
 *
 * TODO:
 * 1. Current GEM hardware supports only 100Mbps, when it supprts multiple
 *    speed, please remove DEBUG_SPEED and xemacpss_phy_init function.
 *    It can also be done with ethtool, but it depends on if rootfile
 *    system has it or not. So just leave it as is for now.
 * 2. RGMII mode is not yet determined. Four modes are supported by open
 *    source linux Marvell driver found in drivers/net/phy directory.
 *    There might have hardware depenedent configurations and needs to be
 *    verified. The worst case is the subtle configuration does not apply,
 *    we have to modify xemacpss_phy_init and hard code it. Ugly...
 * 3. 1588 is not tested due to network setup limitation. If GEM does not
 *    update 1588 related timer counters. Driver need to be updated with
 *    calculations.
 * 4. Two instances are supported from EP3 but no second phy connection.
 *    We need to revisit/verify this when hardware is available.
 *
 * 6. NFS mounted root file system and performance test are not done until
 *    hardware is stable and processor speed is back to normal.
 * 7. JUMBO frame is not enabled per EPs spec. Please update it if this
 *    support is added in and set MAX_MTU to 9000.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <mach/board.h>

#include <linux/clocksource.h>
#include <linux/timecompare.h>
#include <linux/net_tstamp.h>

/************************** Constant Definitions *****************************/

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xemacpss"
#define DRIVER_DESCRIPTION	"Xilinx Tri-Mode Ethernet MAC driver"
#define DRIVER_VERSION		"1.00a"

/* Transmission timeout is 3 seconds. */
#define TX_TIMEOUT		(3*HZ)

/* for RX skb IP header word-aligned */
#define RX_IP_ALIGN_OFFSET	2

/* DMA buffer descriptors must be aligned on a 4-byte boundary. */
#define ALIGNMENT_BD		8

/* Maximum value for hash bits. 2**6 */
#define XEMACPSS_MAX_HASH_BITS	64

/* MDC clock division
 * currently supporting 8, 16, 32, 48, 64, 96, 128, 224.
 */
enum { MDC_DIV_8 = 0, MDC_DIV_16, MDC_DIV_32, MDC_DIV_48,
MDC_DIV_64, MDC_DIV_96, MDC_DIV_128, MDC_DIV_224 };

/* Specify the receive buffer size in bytes, 64, 128, 192, ... 10240 */
#define XEMACPSS_RX_BUF_SIZE	1600

/* Number of receive buffer bytes as a unit, this is HW setup */
#define XEMACPSS_RX_BUF_UNIT	64

/* Default SEND and RECV buffer descriptors (BD) numbers.
 * BD Space needed is (XEMACPSS_SEND_BD_CNT+XEMACPSS_RECV_BD_CNT)*8
 */
#undef  DEBUG
#define DEBUG
#define DEBUG_SPEED

#define XEMACPSS_SEND_BD_CNT	32
#define XEMACPSS_RECV_BD_CNT	32

#define XEMACPSS_NAPI_WEIGHT	64

/* Register offset definitions. Unless otherwise noted, register access is
 * 32 bit. Names are self explained here.
 */
#define XEMACPSS_NWCTRL_OFFSET        0x00000000 /* Network Control reg */
#define XEMACPSS_NWCFG_OFFSET         0x00000004 /* Network Config reg */
#define XEMACPSS_NWSR_OFFSET          0x00000008 /* Network Status reg */
#define XEMACPSS_USERIO_OFFSET        0x0000000C /* User IO reg */
#define XEMACPSS_DMACR_OFFSET         0x00000010 /* DMA Control reg */
#define XEMACPSS_TXSR_OFFSET          0x00000014 /* TX Status reg */
#define XEMACPSS_RXQBASE_OFFSET       0x00000018 /* RX Q Base address reg */
#define XEMACPSS_TXQBASE_OFFSET       0x0000001C /* TX Q Base address reg */
#define XEMACPSS_RXSR_OFFSET          0x00000020 /* RX Status reg */
#define XEMACPSS_ISR_OFFSET           0x00000024 /* Interrupt Status reg */
#define XEMACPSS_IER_OFFSET           0x00000028 /* Interrupt Enable reg */
#define XEMACPSS_IDR_OFFSET           0x0000002C /* Interrupt Disable reg */
#define XEMACPSS_IMR_OFFSET           0x00000030 /* Interrupt Mask reg */
#define XEMACPSS_PHYMNTNC_OFFSET      0x00000034 /* Phy Maintaince reg */
#define XEMACPSS_RXPAUSE_OFFSET       0x00000038 /* RX Pause Time reg */
#define XEMACPSS_TXPAUSE_OFFSET       0x0000003C /* TX Pause Time reg */
#define XEMACPSS_HASHL_OFFSET         0x00000080 /* Hash Low address reg */
#define XEMACPSS_HASHH_OFFSET         0x00000084 /* Hash High address reg */
#define XEMACPSS_LADDR1L_OFFSET       0x00000088 /* Specific1 addr low reg */
#define XEMACPSS_LADDR1H_OFFSET       0x0000008C /* Specific1 addr high reg */
#define XEMACPSS_LADDR2L_OFFSET       0x00000090 /* Specific2 addr low reg */
#define XEMACPSS_LADDR2H_OFFSET       0x00000094 /* Specific2 addr high reg */
#define XEMACPSS_LADDR3L_OFFSET       0x00000098 /* Specific3 addr low reg */
#define XEMACPSS_LADDR3H_OFFSET       0x0000009C /* Specific3 addr high reg */
#define XEMACPSS_LADDR4L_OFFSET       0x000000A0 /* Specific4 addr low reg */
#define XEMACPSS_LADDR4H_OFFSET       0x000000A4 /* Specific4 addr high reg */
#define XEMACPSS_MATCH1_OFFSET        0x000000A8 /* Type ID1 Match reg */
#define XEMACPSS_MATCH2_OFFSET        0x000000AC /* Type ID2 Match reg */
#define XEMACPSS_MATCH3_OFFSET        0x000000B0 /* Type ID3 Match reg */
#define XEMACPSS_MATCH4_OFFSET        0x000000B4 /* Type ID4 Match reg */
#define XEMACPSS_WOL_OFFSET           0x000000B8 /* Wake on LAN reg */
#define XEMACPSS_STRETCH_OFFSET       0x000000BC /* IPG Stretch reg */
#define XEMACPSS_SVLAN_OFFSET         0x000000C0 /* Stacked VLAN reg */
#define XEMACPSS_MODID_OFFSET         0x000000FC /* Module ID reg */
#define XEMACPSS_OCTTXL_OFFSET        0x00000100 /* Octects transmitted Low
						reg */
#define XEMACPSS_OCTTXH_OFFSET        0x00000104 /* Octects transmitted High
						reg */
#define XEMACPSS_TXCNT_OFFSET         0x00000108 /* Error-free Frmaes
						transmitted counter */
#define XEMACPSS_TXBCCNT_OFFSET       0x0000010C /* Error-free Broadcast
						Frames counter*/
#define XEMACPSS_TXMCCNT_OFFSET       0x00000110 /* Error-free Multicast
						Frame counter */
#define XEMACPSS_TXPAUSECNT_OFFSET    0x00000114 /* Pause Frames Transmitted
						Counter */
#define XEMACPSS_TX64CNT_OFFSET       0x00000118 /* Error-free 64 byte Frames
						Transmitted counter */
#define XEMACPSS_TX65CNT_OFFSET       0x0000011C /* Error-free 65-127 byte
						Frames Transmitted counter */
#define XEMACPSS_TX128CNT_OFFSET      0x00000120 /* Error-free 128-255 byte
						Frames Transmitted counter */
#define XEMACPSS_TX256CNT_OFFSET      0x00000124 /* Error-free 256-511 byte
						Frames transmitted counter */
#define XEMACPSS_TX512CNT_OFFSET      0x00000128 /* Error-free 512-1023 byte
						Frames transmitted counter */
#define XEMACPSS_TX1024CNT_OFFSET     0x0000012C /* Error-free 1024-1518 byte
						Frames transmitted counter */
#define XEMACPSS_TX1519CNT_OFFSET     0x00000130 /* Error-free larger than 1519
						byte Frames transmitted
						   counter */
#define XEMACPSS_TXURUNCNT_OFFSET     0x00000134 /* TX under run error
						    counter */
#define XEMACPSS_SNGLCOLLCNT_OFFSET   0x00000138 /* Single Collision Frame
						Counter */
#define XEMACPSS_MULTICOLLCNT_OFFSET  0x0000013C /* Multiple Collision Frame
						Counter */
#define XEMACPSS_EXCESSCOLLCNT_OFFSET 0x00000140 /* Excessive Collision Frame
						Counter */
#define XEMACPSS_LATECOLLCNT_OFFSET   0x00000144 /* Late Collision Frame
						Counter */
#define XEMACPSS_TXDEFERCNT_OFFSET    0x00000148 /* Deferred Transmission
						Frame Counter */
#define XEMACPSS_CSENSECNT_OFFSET     0x0000014C /* Carrier Sense Error
						Counter */
#define XEMACPSS_OCTRXL_OFFSET        0x00000150 /* Octects Received register
						Low */
#define XEMACPSS_OCTRXH_OFFSET        0x00000154 /* Octects Received register
						High */
#define XEMACPSS_RXCNT_OFFSET         0x00000158 /* Error-free Frames
						Received Counter */
#define XEMACPSS_RXBROADCNT_OFFSET    0x0000015C /* Error-free Broadcast
						Frames Received Counter */
#define XEMACPSS_RXMULTICNT_OFFSET    0x00000160 /* Error-free Multicast
						Frames Received Counter */
#define XEMACPSS_RXPAUSECNT_OFFSET    0x00000164 /* Pause Frames
						Received Counter */
#define XEMACPSS_RX64CNT_OFFSET       0x00000168 /* Error-free 64 byte Frames
						Received Counter */
#define XEMACPSS_RX65CNT_OFFSET       0x0000016C /* Error-free 65-127 byte
						Frames Received Counter */
#define XEMACPSS_RX128CNT_OFFSET      0x00000170 /* Error-free 128-255 byte
						Frames Received Counter */
#define XEMACPSS_RX256CNT_OFFSET      0x00000174 /* Error-free 256-512 byte
						Frames Received Counter */
#define XEMACPSS_RX512CNT_OFFSET      0x00000178 /* Error-free 512-1023 byte
						Frames Received Counter */
#define XEMACPSS_RX1024CNT_OFFSET     0x0000017C /* Error-free 1024-1518 byte
						Frames Received Counter */
#define XEMACPSS_RX1519CNT_OFFSET     0x00000180 /* Error-free 1519-max byte
						Frames Received Counter */
#define XEMACPSS_RXUNDRCNT_OFFSET     0x00000184 /* Undersize Frames Received
						Counter */
#define XEMACPSS_RXOVRCNT_OFFSET      0x00000188 /* Oversize Frames Received
						Counter */
#define XEMACPSS_RXJABCNT_OFFSET      0x0000018C /* Jabbers Received
						Counter */
#define XEMACPSS_RXFCSCNT_OFFSET      0x00000190 /* Frame Check Sequence
						Error Counter */
#define XEMACPSS_RXLENGTHCNT_OFFSET   0x00000194 /* Length Field Error
						Counter */
#define XEMACPSS_RXSYMBCNT_OFFSET     0x00000198 /* Symbol Error Counter */
#define XEMACPSS_RXALIGNCNT_OFFSET    0x0000019C /* Alignment Error Counter */
#define XEMACPSS_RXRESERRCNT_OFFSET   0x000001A0 /* Receive Resource Error
						Counter */
#define XEMACPSS_RXORCNT_OFFSET       0x000001A4 /* Receive Overrun Counter */
#define XEMACPSS_RXIPCCNT_OFFSET      0x000001A8 /* IP header Checksum Error
						Counter */
#define XEMACPSS_RXTCPCCNT_OFFSET     0x000001AC /* TCP Checksum Error
						Counter */
#define XEMACPSS_RXUDPCCNT_OFFSET     0x000001B0 /* UDP Checksum Error
						Counter */

#define XEMACPSS_1588S_OFFSET         0x000001D0 /* 1588 Timer Seconds */
#define XEMACPSS_1588NS_OFFSET        0x000001D4 /* 1588 Timer Nanoseconds */
#define XEMACPSS_1588ADJ_OFFSET       0x000001D8 /* 1588 Timer Adjust */
#define XEMACPSS_1588INC_OFFSET       0x000001DC /* 1588 Timer Increment */
#define XEMACPSS_PTPETXS_OFFSET       0x000001E0 /* PTP Event Frame
						Transmitted Seconds */
#define XEMACPSS_PTPETXNS_OFFSET      0x000001E4 /* PTP Event Frame
						Transmitted Nanoseconds */
#define XEMACPSS_PTPERXS_OFFSET       0x000001E8 /* PTP Event Frame Received
						Seconds */
#define XEMACPSS_PTPERXNS_OFFSET      0x000001EC /* PTP Event Frame Received
						Nanoseconds */
#define XEMACPSS_PTPPTXS_OFFSET       0x000001E0 /* PTP Peer Frame
						Transmitted Seconds */
#define XEMACPSS_PTPPTXNS_OFFSET      0x000001E4 /* PTP Peer Frame
						Transmitted Nanoseconds */
#define XEMACPSS_PTPPRXS_OFFSET       0x000001E8 /* PTP Peer Frame Received
						Seconds */
#define XEMACPSS_PTPPRXNS_OFFSET      0x000001EC /* PTP Peer Frame Received
						Nanoseconds */

/* network control register bit definitions */
#define XEMACPSS_NWCTRL_RXTSTAMP_MASK    0x00008000 /* RX Timestamp in CRC */
#define XEMACPSS_NWCTRL_ZEROPAUSETX_MASK 0x00001000 /* Transmit zero quantum
						pause frame */
#define XEMACPSS_NWCTRL_PAUSETX_MASK     0x00000800 /* Transmit pause frame */
#define XEMACPSS_NWCTRL_HALTTX_MASK      0x00000400 /* Halt transmission
						after current frame */
#define XEMACPSS_NWCTRL_STARTTX_MASK     0x00000200 /* Start tx (tx_go) */

#define XEMACPSS_NWCTRL_STATWEN_MASK     0x00000080 /* Enable writing to
						stat counters */
#define XEMACPSS_NWCTRL_STATINC_MASK     0x00000040 /* Increment statistic
						registers */
#define XEMACPSS_NWCTRL_STATCLR_MASK     0x00000020 /* Clear statistic
						registers */
#define XEMACPSS_NWCTRL_MDEN_MASK        0x00000010 /* Enable MDIO port */
#define XEMACPSS_NWCTRL_TXEN_MASK        0x00000008 /* Enable transmit */
#define XEMACPSS_NWCTRL_RXEN_MASK        0x00000004 /* Enable receive */
#define XEMACPSS_NWCTRL_LOOPEN_MASK      0x00000002 /* local loopback */

/* name network configuration register bit definitions */
#define XEMACPSS_NWCFG_BADPREAMBEN_MASK 0x20000000 /* disable rejection of
						non-standard preamble */
#define XEMACPSS_NWCFG_IPDSTRETCH_MASK  0x10000000 /* enable transmit IPG */
#define XEMACPSS_NWCFG_FCSIGNORE_MASK   0x04000000 /* disable rejection of
						FCS error */
#define XEMACPSS_NWCFG_HDRXEN_MASK      0x02000000 /* RX half duplex */
#define XEMACPSS_NWCFG_RXCHKSUMEN_MASK  0x01000000 /* enable RX checksum
						offload */
#define XEMACPSS_NWCFG_PAUSECOPYDI_MASK 0x00800000 /* Do not copy pause
						Frames to memory */
#define XEMACPSS_NWCFG_MDC_SHIFT_MASK   18         /* shift bits for MDC */
#define XEMACPSS_NWCFG_MDCCLKDIV_MASK   0x001C0000 /* MDC Mask PCLK divisor */
#define XEMACPSS_NWCFG_FCSREM_MASK      0x00020000 /* Discard FCS from
						received frames */
#define XEMACPSS_NWCFG_LENGTHERRDSCRD_MASK 0x00010000
/* RX length error discard */
#define XEMACPSS_NWCFG_RXOFFS_MASK      0x0000C000 /* RX buffer offset */
#define XEMACPSS_NWCFG_PAUSEEN_MASK     0x00002000 /* Enable pause TX */
#define XEMACPSS_NWCFG_RETRYTESTEN_MASK 0x00001000 /* Retry test */
#define XEMACPSS_NWCFG_1000_MASK        0x00000400 /* Gigbit mode */
#define XEMACPSS_NWCFG_EXTADDRMATCHEN_MASK 0x00000200
/* External address match enable */
#define XEMACPSS_NWCFG_1536RXEN_MASK    0x00000100 /* Enable 1536 byte
						frames reception */
#define XEMACPSS_NWCFG_UCASTHASHEN_MASK 0x00000080 /* Receive unicast hash
						frames */
#define XEMACPSS_NWCFG_MCASTHASHEN_MASK 0x00000040 /* Receive multicast hash
						frames */
#define XEMACPSS_NWCFG_BCASTDI_MASK     0x00000020 /* Do not receive
						broadcast frames */
#define XEMACPSS_NWCFG_COPYALLEN_MASK   0x00000010 /* Copy all frames */

#define XEMACPSS_NWCFG_NVLANDISC_MASK   0x00000004 /* Receive only VLAN
						frames */
#define XEMACPSS_NWCFG_FDEN_MASK        0x00000002 /* Full duplex */
#define XEMACPSS_NWCFG_100_MASK         0x00000001 /* 10 or 100 Mbs */

/* network status register bit definitaions */
#define XEMACPSS_NWSR_MDIOIDLE_MASK     0x00000004 /* PHY management idle */
#define XEMACPSS_NWSR_MDIO_MASK         0x00000002 /* Status of mdio_in */

/* MAC address register word 1 mask */
#define XEMACPSS_LADDR_MACH_MASK        0x0000FFFF /* Address bits[47:32]
						bit[31:0] are in BOTTOM */

/* DMA control register bit definitions */
#define XEMACPSS_DMACR_RXBUF_MASK     0x00FF0000 /* Mask bit for RX buffer
						size */
#define XEMACPSS_DMACR_RXBUF_SHIFT    16         /* Shift bit for RX buffer
						size */
#define XEMACPSS_DMACR_TCPCKSUM_MASK  0x00000800 /* enable/disable TX
						checksum offload */
#define XEMACPSS_DMACR_TXSIZE_MASK    0x00000400 /* TX buffer memory size */
#define XEMACPSS_DMACR_RXSIZE_MASK    0x00000300 /* RX buffer memory size */
#define XEMACPSS_DMACR_ENDIAN_MASK    0x00000080 /* Endian configuration */
#define XEMACPSS_DMACR_BLENGTH_MASK   0x0000001F /* Buffer burst length */

/* transmit status register bit definitions */
#define XEMACPSS_TXSR_HRESPNOK_MASK   0x00000100 /* Transmit hresp not OK */
#define XEMACPSS_TXSR_COL1000_MASK    0x00000080 /* Collision Gbs mode */
#define XEMACPSS_TXSR_URUN_MASK       0x00000040 /* Transmit underrun */
#define XEMACPSS_TXSR_TXCOMPL_MASK    0x00000020 /* Transmit completed OK */
#define XEMACPSS_TXSR_BUFEXH_MASK     0x00000010 /* Transmit buffs exhausted
						mid frame */
#define XEMACPSS_TXSR_TXGO_MASK       0x00000008 /* Status of go flag */
#define XEMACPSS_TXSR_RXOVR_MASK      0x00000004 /* Retry limit exceeded */
#define XEMACPSS_TXSR_COL100_MASK     0x00000002 /* Collision 10/100  mode */
#define XEMACPSS_TXSR_USEDREAD_MASK   0x00000001 /* TX buffer used bit set */

#define XEMACPSS_TXSR_ERROR_MASK	(XEMACPSS_TXSR_HRESPNOK_MASK | \
					XEMACPSS_TXSR_COL1000_MASK | \
					XEMACPSS_TXSR_URUN_MASK |   \
					XEMACPSS_TXSR_BUFEXH_MASK | \
					XEMACPSS_TXSR_RXOVR_MASK |  \
					XEMACPSS_TXSR_COL100_MASK | \
					XEMACPSS_TXSR_USEDREAD_MASK)

/* receive status register bit definitions */
#define XEMACPSS_RXSR_HRESPNOK_MASK   0x00000008 /* Receive hresp not OK */
#define XEMACPSS_RXSR_RXOVR_MASK      0x00000004 /* Receive overrun */
#define XEMACPSS_RXSR_FRAMERX_MASK    0x00000002 /* Frame received OK */
#define XEMACPSS_RXSR_BUFFNA_MASK     0x00000001 /* RX buffer used bit set */

#define XEMACPSS_RXSR_ERROR_MASK	(XEMACPSS_RXSR_HRESPNOK_MASK | \
					XEMACPSS_RXSR_RXOVR_MASK | \
					XEMACPSS_RXSR_BUFFNA_MASK)

/* interrupts bit definitions
 * Bits definitions are same in XEMACPSS_ISR_OFFSET,
 * XEMACPSS_IER_OFFSET, XEMACPSS_IDR_OFFSET, and XEMACPSS_IMR_OFFSET
 */
#define XEMACPSS_IXR_PTPPSTX_MASK    0x02000000	/* PTP Psync transmitted */
#define XEMACPSS_IXR_PTPPDRTX_MASK   0x01000000	/* PTP Pdelay_req transmitted */
#define XEMACPSS_IXR_PTPSTX_MASK     0x00800000	/* PTP Sync transmitted */
#define XEMACPSS_IXR_PTPDRTX_MASK    0x00400000	/* PTP Delay_req transmitted */
#define XEMACPSS_IXR_PTPPSRX_MASK    0x00200000	/* PTP Psync received */
#define XEMACPSS_IXR_PTPPDRRX_MASK   0x00100000	/* PTP Pdelay_req received */
#define XEMACPSS_IXR_PTPSRX_MASK     0x00080000	/* PTP Sync received */
#define XEMACPSS_IXR_PTPDRRX_MASK    0x00040000	/* PTP Delay_req received */
#define XEMACPSS_IXR_PAUSETX_MASK    0x00004000	/* Pause frame transmitted */
#define XEMACPSS_IXR_PAUSEZERO_MASK  0x00002000	/* Pause time has reached
						zero */
#define XEMACPSS_IXR_PAUSENZERO_MASK 0x00001000	/* Pause frame received */
#define XEMACPSS_IXR_HRESPNOK_MASK   0x00000800	/* hresp not ok */
#define XEMACPSS_IXR_RXOVR_MASK      0x00000400	/* Receive overrun occurred */
#define XEMACPSS_IXR_TXCOMPL_MASK    0x00000080	/* Frame transmitted ok */
#define XEMACPSS_IXR_TXEXH_MASK      0x00000040	/* Transmit err occurred or
						no buffers*/
#define XEMACPSS_IXR_RETRY_MASK      0x00000020	/* Retry limit exceeded */
#define XEMACPSS_IXR_URUN_MASK       0x00000010	/* Transmit underrun */
#define XEMACPSS_IXR_TXUSED_MASK     0x00000008	/* Tx buffer used bit read */
#define XEMACPSS_IXR_RXUSED_MASK     0x00000004	/* Rx buffer used bit read */
#define XEMACPSS_IXR_FRAMERX_MASK    0x00000002	/* Frame received ok */
#define XEMACPSS_IXR_MGMNT_MASK      0x00000001	/* PHY management complete */
#define XEMACPSS_IXR_ALL_MASK        0x03FC7FFF	/* Everything! */

#define XEMACPSS_IXR_TX_ERR_MASK	(XEMACPSS_IXR_TXEXH_MASK |    \
					XEMACPSS_IXR_RETRY_MASK |    \
					XEMACPSS_IXR_URUN_MASK  |    \
					XEMACPSS_IXR_TXUSED_MASK)

#define XEMACPSS_IXR_RX_ERR_MASK	(XEMACPSS_IXR_HRESPNOK_MASK | \
					XEMACPSS_IXR_RXUSED_MASK |  \
					XEMACPSS_IXR_RXOVR_MASK)
/* PHY Maintenance bit definitions */
#define XEMACPSS_PHYMNTNC_OP_MASK    0x40020000	/* operation mask bits */
#define XEMACPSS_PHYMNTNC_OP_R_MASK  0x20000000	/* read operation */
#define XEMACPSS_PHYMNTNC_OP_W_MASK  0x10000000	/* write operation */
#define XEMACPSS_PHYMNTNC_ADDR_MASK  0x0F800000	/* Address bits */
#define XEMACPSS_PHYMNTNC_REG_MASK   0x007C0000	/* register bits */
#define XEMACPSS_PHYMNTNC_DATA_MASK  0x0000FFFF	/* data bits */
#define XEMACPSS_PHYMNTNC_PHYAD_SHIFT_MASK   23	/* Shift bits for PHYAD */
#define XEMACPSS_PHYMNTNC_PHREG_SHIFT_MASK   18	/* Shift bits for PHREG */

/* Wake on LAN bit definition */
#define XEMACPSS_WOL_MCAST_MASK      0x00080000
#define XEMACPSS_WOL_SPEREG1_MASK    0x00040000
#define XEMACPSS_WOL_ARP_MASK        0x00020000
#define XEMACPSS_WOL_MAGIC_MASK      0x00010000
#define XEMACPSS_WOL_ARP_ADDR_MASK   0x0000FFFF

/* Buffer descriptor status words offset */
#define XEMACPSS_BD_ADDR_OFFSET     0x00000000 /**< word 0/addr of BDs */
#define XEMACPSS_BD_STAT_OFFSET     0x00000004 /**< word 1/status of BDs */

/* Transmit buffer descriptor status words bit positions.
 * Transmit buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit address pointing to the location of
 * the transmit data.
 * The following register - word1, consists of various information to
 * control transmit process.  After transmit, this is updated with status
 * information, whether the frame was transmitted OK or why it had failed.
 */
#define XEMACPSS_TXBUF_USED_MASK  0x80000000 /* Used bit. */
#define XEMACPSS_TXBUF_WRAP_MASK  0x40000000 /* Wrap bit, last descriptor */
#define XEMACPSS_TXBUF_RETRY_MASK 0x20000000 /* Retry limit exceeded */
#define XEMACPSS_TXBUF_URUN_MASK  0x10000000 /* Transmit underrun occurred */
#define XEMACPSS_TXBUF_EXH_MASK   0x08000000 /* Buffers exhausted */
#define XEMACPSS_TXBUF_LAC_MASK   0x04000000 /* Late collision. */
#define XEMACPSS_TXBUF_NOCRC_MASK 0x00010000 /* No CRC */
#define XEMACPSS_TXBUF_LAST_MASK  0x00008000 /* Last buffer */
#define XEMACPSS_TXBUF_LEN_MASK   0x00003FFF /* Mask for length field */

#define XEMACPSS_TXBUF_ERR_MASK   0x3C000000 /* Mask for length field */

/* Receive buffer descriptor status words bit positions.
 * Receive buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit word aligned address pointing to the
 * address of the buffer. The lower two bits make up the wrap bit indicating
 * the last descriptor and the ownership bit to indicate it has been used.
 * The following register - word1, contains status information regarding why
 * the frame was received (the filter match condition) as well as other
 * useful info.
 */
#define XEMACPSS_RXBUF_BCAST_MASK     0x80000000 /* Broadcast frame */
#define XEMACPSS_RXBUF_MULTIHASH_MASK 0x40000000 /* Multicast hashed frame */
#define XEMACPSS_RXBUF_UNIHASH_MASK   0x20000000 /* Unicast hashed frame */
#define XEMACPSS_RXBUF_EXH_MASK       0x08000000 /* buffer exhausted */
#define XEMACPSS_RXBUF_AMATCH_MASK    0x06000000 /* Specific address
						matched */
#define XEMACPSS_RXBUF_IDFOUND_MASK   0x01000000 /* Type ID matched */
#define XEMACPSS_RXBUF_IDMATCH_MASK   0x00C00000 /* ID matched mask */
#define XEMACPSS_RXBUF_VLAN_MASK      0x00200000 /* VLAN tagged */
#define XEMACPSS_RXBUF_PRI_MASK       0x00100000 /* Priority tagged */
#define XEMACPSS_RXBUF_VPRI_MASK      0x000E0000 /* Vlan priority */
#define XEMACPSS_RXBUF_CFI_MASK       0x00010000 /* CFI frame */
#define XEMACPSS_RXBUF_EOF_MASK       0x00008000 /* End of frame. */
#define XEMACPSS_RXBUF_SOF_MASK       0x00004000 /* Start of frame. */
#define XEMACPSS_RXBUF_LEN_MASK       0x00003FFF /* Mask for length field */

#define XEMACPSS_RXBUF_WRAP_MASK      0x00000002 /* Wrap bit, last BD */
#define XEMACPSS_RXBUF_NEW_MASK       0x00000001 /* Used bit.. */
#define XEMACPSS_RXBUF_ADD_MASK       0xFFFFFFFC /* Mask for address */

#define xemacpss_read(base, reg)	\
	__raw_readl((u32)(base) + (u32)(reg))
#define xemacpss_write(base, reg, val)	\
	__raw_writel((val), (u32)(base) + (u32)(reg))

#define XEMACPSS_RING_SEEKAHEAD(ringptr, bdptr, numbd)			\
{									\
	u32 addr = (u32)bdptr;						\
	addr += ((ringptr)->separation * numbd);			\
	if ((addr > (ringptr)->lastbdaddr) || ((u32)bdptr > addr)) {	\
		addr -= (ringptr)->length;				\
	}								\
	bdptr = (struct xemacpss_bd *)addr;				\
}

#define XEMACPSS_RING_SEEKBACK(ringptr, bdptr, numbd)			\
{									\
	u32 addr = (u32)bdptr;						\
	addr -= ((ringptr)->separation * numbd);			\
	if ((addr < (ringptr)->firstbdaddr) || ((u32)bdptr < addr)) {	\
		addr += (ringptr)->length;				\
	}								\
	bdptr = (struct xemacpss_bd *)addr;				\
}

#define XEMACPSS_BDRING_NEXT(ringptr, bdptr)				\
	(((u32)(bdptr) >= (ringptr)->lastbdaddr) ?			\
	(struct xemacpss_bd *)(ringptr)->firstbdaddr :			\
	(struct xemacpss_bd *)((u32)(bdptr) + (ringptr)->separation))

#define XEMACPSS_BDRING_PREV(ringptr, bdptr)				\
	(((u32)(bdptr) <= (ringptr)->firstbdaddr) ?			\
	(struct xemacpss_bd *)(ringptr)->lastbdaddr :			\
	(struct xemacpss_bd *)((u32)(bdptr) - (ringptr)->separation))

#define XEMACPSS_SET_BUFADDR_RX(bdptr, addr)				\
	xemacpss_write((bdptr), XEMACPSS_BD_ADDR_OFFSET,		\
	((xemacpss_read((bdptr), XEMACPSS_BD_ADDR_OFFSET) &		\
	~XEMACPSS_RXBUF_ADD_MASK) | (u32)(addr)))

#define XEMACPSS_BD_TO_INDEX(ringptr, bdptr)				\
	(((u32)bdptr - (u32)(ringptr)->firstbdaddr) / (ringptr)->separation)

struct ring_info {
	struct sk_buff *skb;
	dma_addr_t     mapping;
};

/* DMA buffer descriptor structure. Each BD is two words */
struct xemacpss_bd {
	u32 addr;
	u32 ctrl;
};

/* This is an internal structure used to maintain the DMA list */
struct xemacpss_bdring {
	u32 physbaseaddr;    /* Physical address of 1st BD in list */
	u32 firstbdaddr;     /* Virtual address of 1st BD in list */
	u32 lastbdaddr;      /* Virtual address of last BD in the list */
	u32 length;          /* size of ring in bytes */
	u32 separation;      /* Number of bytes between the starting
				address of adjacent BDs */
	struct xemacpss_bd *freehead; /* First BD in the free group */
	struct xemacpss_bd *prehead;  /* First BD in the pre-work group */
	struct xemacpss_bd *hwhead;   /* First BD in the work group */
	struct xemacpss_bd *hwtail;   /* Last BD in the work group */
	struct xemacpss_bd *posthead; /* First BD in the post-work group */
	unsigned freecnt;    /* Number of BDs in the free group */
	unsigned hwcnt;      /* Number of BDs in work group */
	unsigned precnt;     /* Number of BDs in pre-work group */
	unsigned postcnt;    /* Number of BDs in post-work group */
	unsigned allcnt;     /* Total Number of BDs for channel */

	int is_rx;           /* Is this an RX or a TX ring? */
};

/* Our private device data. */
struct net_local {
	void   __iomem         *baseaddr;
	struct xemacpss_bdring tx_ring;
	struct xemacpss_bdring rx_ring;

	struct ring_info       *tx_skb;
	struct ring_info       *rx_skb;

	void                   *rx_bd;        /* virtual address */
	void                   *tx_bd;        /* virtual address */

	dma_addr_t             rx_bd_dma;     /* physical address */
	dma_addr_t             tx_bd_dma;     /* physical address */

	spinlock_t             lock;

	struct platform_device *pdev;
	struct net_device      *ndev;   /* this device */

	struct napi_struct     napi;    /* napi information for device */
	struct net_device_stats stats;  /* Statistics for this device */

	/* Manage internal timer for packet timestamping */
	struct cyclecounter    cycles;
	struct timecounter     clock;
	struct timecompare     compare;
	struct hwtstamp_config hwtstamp_config;

	struct mii_bus         *mii_bus;
	struct phy_device      *phy_dev;
	unsigned int           link;
	unsigned int           speed;
	unsigned int           duplex;
	/* RX ip/tcp/udp checksum */
	unsigned               ip_summed;
};

static struct net_device_ops netdev_ops;

/**
 * xemacpss_mdio_read - Read current value of phy register indicated by
 * phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be read
 *
 * @return: value read from specified phy register.
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPSS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacpss_mdio_read(struct mii_bus *bus, int mii_id, int phyreg)
{
	struct net_local *lp = bus->priv;
	u32 regval;
	int value;

	regval  = XEMACPSS_PHYMNTNC_OP_MASK;
	regval |= XEMACPSS_PHYMNTNC_OP_R_MASK;
	regval |= (mii_id << XEMACPSS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPSS_PHYMNTNC_PHREG_SHIFT_MASK);

	xemacpss_write(lp->baseaddr, XEMACPSS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	while (!(xemacpss_read(lp->baseaddr, XEMACPSS_NWSR_OFFSET) &
			XEMACPSS_NWSR_MDIOIDLE_MASK))
		cpu_relax();

	value = xemacpss_read(lp->baseaddr, XEMACPSS_PHYMNTNC_OFFSET) &
			XEMACPSS_PHYMNTNC_DATA_MASK;

	return value;
}

/**
 * xemacpss_mdio_write - Write passed in value to phy register indicated
 * by phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be configured.
 * @value: value to be written to phy register.
 * return 0. This API requires to be int type or compile warning generated
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPSS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacpss_mdio_write(struct mii_bus *bus, int mii_id, int phyreg,
	u16 value)
{
	struct net_local *lp = bus->priv;
	u32 regval;

	regval  = XEMACPSS_PHYMNTNC_OP_MASK;
	regval |= XEMACPSS_PHYMNTNC_OP_W_MASK;
	regval |= (mii_id << XEMACPSS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPSS_PHYMNTNC_PHREG_SHIFT_MASK);
	regval |= value;

	xemacpss_write(lp->baseaddr, XEMACPSS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	while (!(xemacpss_read(lp->baseaddr, XEMACPSS_NWSR_OFFSET) &
		XEMACPSS_NWSR_MDIOIDLE_MASK))
		cpu_relax();

	return 0;
}


/**
 * xemacpss_mdio_reset - mdio reset. It seems to be required per open
 * source documentation phy.txt. But there is no reset in this device.
 * Provide function API for now.
 * @bus: mdio bus
 **/
static int xemacpss_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

#ifdef DEBUG_SPEED
static void xemacpss_phy_init(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u16 regval;
	int i = 0;

	/* set RX delay */
	regval = xemacpss_mdio_read(lp->mii_bus, lp->phy_dev->addr, 20);
	/* 0x0080 for 100Mbps, 0x0060 for 1Gbps. */
	regval |= 0x0080;
	xemacpss_mdio_write(lp->mii_bus, lp->phy_dev->addr, 20, regval);

	/* 0x2100 for 100Mbps, 0x0140 for 1Gbps. */
	xemacpss_mdio_write(lp->mii_bus, lp->phy_dev->addr, 0, 0x2100);

	regval = xemacpss_mdio_read(lp->mii_bus, lp->phy_dev->addr, 0);
	regval |= 0x8000;
	xemacpss_mdio_write(lp->mii_bus, lp->phy_dev->addr, 0, regval);
	for (i = 0; i < 10; i++)
		mdelay(500);
#ifdef DEBUG_VERBOSE
	printk(KERN_INFO "GEM: phy register dump, start from 0, four in a row.");
	for (i = 0; i <= 30; i++) {
		if (!(i%4))
			printk("\n %02d:  ", i);
		regval = xemacpss_mdio_read(lp->mii_bus, lp->phy_dev->addr, i);
		printk(" 0x%08x", regval);
	}
	printk("\n");
#endif
}
#endif

/**
 * xemacpss_adjust_link - handles link status changes, such as speed,
 * duplex, up/down, ...
 * @ndev: network device
 */
static void xemacpss_adjust_link(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;
	unsigned long flags;
	int status_change = 0;
	u32 regval;

	spin_lock_irqsave(&lp->lock, flags);

	if (phydev->link) {
		if ((lp->speed != phydev->speed) ||
		    (lp->duplex != phydev->duplex)) {
			regval = xemacpss_read(lp->baseaddr,
				XEMACPSS_NWCFG_OFFSET);
			if (phydev->duplex)
				regval |= XEMACPSS_NWCFG_FDEN_MASK;
			else
				regval &= ~XEMACPSS_NWCFG_FDEN_MASK;

			if (phydev->speed == SPEED_1000)
				regval |= XEMACPSS_NWCFG_1000_MASK;
			else
				regval &= ~XEMACPSS_NWCFG_1000_MASK;

			if (phydev->speed == SPEED_100)
				regval |= XEMACPSS_NWCFG_100_MASK;
			else
				regval &= ~XEMACPSS_NWCFG_100_MASK;

			xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET,
				regval);

			lp->speed = phydev->speed;
			lp->duplex = phydev->duplex;
			status_change = 1;
		}
	}

	if (phydev->link != lp->link) {
		lp->link = phydev->link;
		status_change = 1;
	}

	spin_unlock_irqrestore(&lp->lock, flags);

	if (status_change) {
		if (phydev->link) {
			printk(KERN_INFO "%s: link up (%d/%s)\n",
				ndev->name, phydev->speed,
				DUPLEX_FULL == phydev->duplex ?
				"FULL" : "HALF");
		} else {
			printk(KERN_INFO "%s: link down\n", ndev->name);
		}
	}
}

/**
 * xemacpss_mii_probe - probe mii bus, find the right bus_id to register
 * phy callback function.
 * @ndev: network interface device structure
 * return 0 on success, negative value if error
 **/
static int xemacpss_mii_probe(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = NULL;
	struct xemacpss_eth_data *pdata;
	int phy_addr;

	for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
		if (lp->mii_bus->phy_map[phy_addr]) {
			phydev = lp->mii_bus->phy_map[phy_addr];
			break;
		}
	}

	if (!phydev) {
		printk(KERN_ERR "%s: no PHY found\n", ndev->name);
		return -1;
	}

	pdata = lp->pdev->dev.platform_data;

	phydev = phy_connect(ndev, dev_name(&phydev->dev),
		&xemacpss_adjust_link, 0, PHY_INTERFACE_MODE_RGMII_ID);

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: can not connect phy\n", ndev->name);
		return -1;
	}

#ifdef DEBUG
	printk(KERN_INFO "GEM: phydev %p, phydev->phy_id 0x%x, phydev->addr 0x%x\n",
		phydev, phydev->phy_id, phydev->addr);
#endif
	phydev->supported &= PHY_GBIT_FEATURES;

	phydev->advertising = phydev->supported;

	lp->link    = 0;
	lp->speed   = 0;
	lp->duplex  = -1;
	lp->phy_dev = phydev;

	return 0;
}

/**
 * xemacpss_mii_init - Initialize and register mii bus to network device
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacpss_mii_init(struct net_local *lp)
{
	struct xemacpss_eth_data *pdata;
	int rc = -ENXIO, i;

	lp->mii_bus = mdiobus_alloc();
	if (lp->mii_bus == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	lp->mii_bus->name  = "XEMACPSS mii bus";
	lp->mii_bus->read  = &xemacpss_mdio_read;
	lp->mii_bus->write = &xemacpss_mdio_write;
	lp->mii_bus->reset = &xemacpss_mdio_reset;
	snprintf(lp->mii_bus->id, MII_BUS_ID_SIZE, "%x", lp->pdev->id);
	lp->mii_bus->priv = lp;
	lp->mii_bus->parent = &lp->ndev->dev;
	pdata = lp->pdev->dev.platform_data;

	if (pdata)
		lp->mii_bus->phy_mask = pdata->phy_mask;

	lp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!lp->mii_bus->irq) {
		rc = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		lp->mii_bus->irq[i] = PHY_POLL;

	platform_set_drvdata(lp->ndev, lp->mii_bus);

	if (mdiobus_register(lp->mii_bus))
		goto err_out_free_mdio_irq;

	if (xemacpss_mii_probe(lp->ndev) != 0) {
		printk(KERN_ERR "%s mii_probe fail.\n", lp->mii_bus->name);
		goto err_out_unregister_bus;
	}

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(lp->mii_bus);
err_out_free_mdio_irq:
	kfree(lp->mii_bus->irq);
err_out_free_mdiobus:
	mdiobus_free(lp->mii_bus);
err_out:
	return rc;
}

/**
 * xemacpss_update_hdaddr - Update device's MAC address when configured
 * MAC address is not valid, reconfigure with a good one.
 * @lp: local device instance pointer
 **/
static void __init xemacpss_update_hwaddr(struct net_local *lp)
{
	u32 regvall;
	u16 regvalh;
	u8  addr[6];

	regvall = xemacpss_read(lp->baseaddr, XEMACPSS_LADDR1L_OFFSET);
	regvalh = xemacpss_read(lp->baseaddr, XEMACPSS_LADDR1H_OFFSET);
	addr[0] = regvall & 0xFF;
	addr[1] = (regvall >> 8) & 0xFF;
	addr[2] = (regvall >> 16) & 0xFF;
	addr[3] = (regvall >> 24) & 0xFF;
	addr[4] = regvalh & 0xFF;
	addr[5] = (regvalh >> 8) & 0xFF;
#ifdef DEBUG
	printk(KERN_INFO "GEM: MAC addr %02x:%02x:%02x:%02x:%02x:%02x\n",
		addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
#endif
	if (is_valid_ether_addr(addr)) {
		memcpy(lp->ndev->dev_addr, addr, sizeof(addr));
	} else {
		dev_info(&lp->pdev->dev, "invalid address, use assigned\n");
		random_ether_addr(lp->ndev->dev_addr);
		printk(KERN_INFO "MAC updated %02x:%02x:%02x:%02x:%02x:%02x\n",
			lp->ndev->dev_addr[0], lp->ndev->dev_addr[1],
			lp->ndev->dev_addr[2], lp->ndev->dev_addr[3],
			lp->ndev->dev_addr[4], lp->ndev->dev_addr[5]);
	}
}

/**
 * xemacpss_set_hwaddr - Set device's MAC address from ndev->dev_addr
 * @lp: local device instance pointer
 **/
static void xemacpss_set_hwaddr(struct net_local *lp)
{
	u32 regvall = 0;
	u16 regvalh = 0;
#ifdef __LITTLE_ENDIAN
	regvall = cpu_to_le32(*((u32 *)lp->ndev->dev_addr));
	regvalh = cpu_to_le16(*((u16 *)(lp->ndev->dev_addr + 4)));
#endif
#ifdef __BIG_ENDIAN
	regvall = cpu_to_be32(*((u32 *)lp->ndev->dev_addr));
	regvalh = cpu_to_be16(*((u16 *)(lp->ndev->dev_addr + 4)));
#endif
	/* LADDRXH has to be wriiten latter than LADDRXL to enable
	 * this address even if these 16 bits are zeros. */
	xemacpss_write(lp->baseaddr, XEMACPSS_LADDR1L_OFFSET, regvall);
	xemacpss_write(lp->baseaddr, XEMACPSS_LADDR1H_OFFSET, regvalh);
#ifdef DEBUG
	regvall = xemacpss_read(lp->baseaddr, XEMACPSS_LADDR1L_OFFSET);
	regvalh = xemacpss_read(lp->baseaddr, XEMACPSS_LADDR1H_OFFSET);
	printk(KERN_INFO "GEM: MAC 0x%08x, 0x%08x, %02x:%02x:%02x:%02x:%02x:%02x\n",
		regvall, regvalh,
		(regvall & 0xff), ((regvall >> 8) & 0xff),
		((regvall >> 16) & 0xff), (regvall >> 24),
		(regvalh & 0xff), (regvalh >> 8));
#endif
}

/*
 * xemacpss_reset_hw - Helper function to reset the underlying hardware.
 * This is called when we get into such deep trouble that we don't know
 * how to handle otherwise.
 * @lp: local device instance pointer
 */
static void xemacpss_reset_hw(struct net_local *lp)
{
	/* make sure we have the buffer for ourselves */
	wmb();

	/* Have a clean start */
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET, 0);

	/* Clear statistic counters */
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET,
		XEMACPSS_NWCTRL_STATCLR_MASK);

	/* Clear TX and RX status */
	xemacpss_write(lp->baseaddr, XEMACPSS_TXSR_OFFSET, ~0UL);
	xemacpss_write(lp->baseaddr, XEMACPSS_RXSR_OFFSET, ~0UL);

	/* Disable all interrupts */
	xemacpss_write(lp->baseaddr, XEMACPSS_IDR_OFFSET, ~0UL);
	xemacpss_read(lp->baseaddr, XEMACPSS_ISR_OFFSET);
}

/**
 * xemacpss_bdringalloc - reserve locations in BD list.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to allocate.
 * @bdptr: output parameter points to the first BD available for
 *         modification.
 * return 0 on success, negative value if not enough BDs.
 **/
int xemacpss_bdringalloc(struct xemacpss_bdring *ringptr, unsigned numbd,
		struct xemacpss_bd **bdptr)
{
	/* Enough free BDs available for the request? */
	if (ringptr->freecnt < numbd)
		return NETDEV_TX_BUSY;

	/* Set the return argument and move FreeHead forward */
	*bdptr = ringptr->freehead;
	XEMACPSS_RING_SEEKAHEAD(ringptr, ringptr->freehead, numbd);
	ringptr->freecnt -= numbd;
	ringptr->precnt  += numbd;
	return 0;
}

/**
 * xemacpss_bdringunalloc - Fully or partially undo xemacpss_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to unallocate.
 * @bdptr: points to the first of BDs to be unallocated.
 * return 0 on success, negative value if error.
 **/
int xemacpss_bdringunalloc(struct xemacpss_bdring *ringptr, unsigned numbd,
		struct xemacpss_bd *bdptr)
{
	/* Enough BDs in the free state for the request? */
	if (ringptr->precnt < numbd)
		return -ENOSPC;

	/* Set the return argument and move FreeHead backward */
	XEMACPSS_RING_SEEKBACK(ringptr, ringptr->freehead, numbd);
	ringptr->freecnt += numbd;
	ringptr->precnt  -= numbd;
	return 0;
}

#ifdef DEBUG_VERBOSE
static void print_ring(struct xemacpss_bdring *ring)
{
	int i;
	unsigned regval;
	struct xemacpss_bd *bd;

	printk(KERN_INFO "freehead %p prehead %p hwhead %p hwtail %p posthead %p\n",
		ring->freehead, ring->prehead, ring->hwhead, ring->hwtail, ring->posthead);
	printk(KERN_INFO "freecnt %d hwcnt %d precnt %d postcnt %d allcnt %d\n",
		ring->freecnt, ring->hwcnt, ring->precnt, ring->postcnt, ring->allcnt);

	bd = (struct xemacpss_bd *)ring->firstbdaddr;
	for (i=0; i<XEMACPSS_RECV_BD_CNT; i++) {
		regval = xemacpss_read(bd, XEMACPSS_BD_ADDR_OFFSET);
		printk(KERN_INFO "BD %p: ADDR: 0x%08x\n", bd, regval);
		regval = xemacpss_read(bd, XEMACPSS_BD_STAT_OFFSET);
		printk(KERN_INFO "BD %p: STAT: 0x%08x\n", bd, regval);
		bd++;
	}
}
#endif

/**
 * xemacpss_bdringtohw - Enqueue a set of BDs to hardware that were
 * previously allocated by xemacpss_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to hardware.
 * @bdptr: points to the first of BDs to be processed.
 * return 0 on success, negative value if error.
 **/
int xemacpss_bdringtohw(struct xemacpss_bdring *ringptr, unsigned numbd,
		struct xemacpss_bd *bdptr)
{
	struct xemacpss_bd *curbdptr;
	unsigned int i;
	unsigned int regval;

	/* if no bds to process, simply return. */
	if (numbd == 0)
		return 0;

	/* Make sure we are in sync with xemacpss_bdringalloc() */
	if ((ringptr->precnt < numbd) || (ringptr->prehead != bdptr)) {
		return -ENOSPC;
	}

	curbdptr = bdptr;
	for (i = 0; i < numbd; i++) {
		/* Assign ownership back to hardware */
		if (ringptr->is_rx) {
			xemacpss_write(curbdptr, XEMACPSS_BD_STAT_OFFSET, 0);
			wmb();

			regval = xemacpss_read(curbdptr, XEMACPSS_BD_ADDR_OFFSET);
			regval &= ~XEMACPSS_RXBUF_NEW_MASK;
			xemacpss_write(curbdptr, XEMACPSS_BD_ADDR_OFFSET, regval);
		} else {
			regval = xemacpss_read(curbdptr, XEMACPSS_BD_STAT_OFFSET);
			/* clear used bit - hardware to own this descriptor */
			regval &= ~XEMACPSS_TXBUF_USED_MASK;
			xemacpss_write(curbdptr, XEMACPSS_BD_STAT_OFFSET, regval);
		}
		wmb();
		curbdptr = XEMACPSS_BDRING_NEXT(ringptr, curbdptr);
	}
	/* Adjust ring pointers & counters */
	XEMACPSS_RING_SEEKAHEAD(ringptr, ringptr->prehead, numbd);
	ringptr->hwtail  = curbdptr;
	ringptr->precnt -= numbd;
	ringptr->hwcnt  += numbd;

	return 0;
}

/**
 * xemacpss_bdringfromhwtx - returns a set of BD(s) that have been
 * processed by hardware in tx direction.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @bdlimit: maximum number of BDs to return in the set.
 * @bdptr: output parameter points to the first BD available for
 *         examination.
 * return number of BDs processed by hardware.
 **/
u32 xemacpss_bdringfromhwtx(struct xemacpss_bdring *ringptr, unsigned bdlimit,
		struct xemacpss_bd **bdptr)
{
	struct xemacpss_bd *curbdptr;
	u32 bdstr = 0;
	unsigned int bdcount = 0;
	unsigned int bdpartialcount = 0;
	unsigned int sop = 0;

	curbdptr = ringptr->hwhead;

	/* If no BDs in work group, then there's nothing to search */
	if (ringptr->hwcnt == 0) {
		*bdptr = NULL;
		return 0;
	}

	if (bdlimit > ringptr->hwcnt) {
		bdlimit = ringptr->hwcnt;
	}

	/* Starting at hwhead, keep moving forward in the list until:
	 *  - ringptr->hwtail is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (bdcount < bdlimit) {
		/* Read the status */
		bdstr = xemacpss_read(curbdptr, XEMACPSS_BD_STAT_OFFSET);

		if ((sop == 0) && (bdstr & XEMACPSS_TXBUF_USED_MASK)) {
			sop = 1;
		} else {
			break;
		}

		if (sop == 1) {
			bdcount++;
			bdpartialcount++;
		}
		/* hardware has processed this BD so check the "last" bit.
		 * If it is clear, then there are more BDs for the current
		 * packet. Keep a count of these partial packet BDs.
		 */
		if ((sop == 1) && (bdstr & XEMACPSS_TXBUF_LAST_MASK)) {
			sop = 0;
			bdpartialcount = 0;
		}

		/* Move on to next BD in work group */
		curbdptr = XEMACPSS_BDRING_NEXT(ringptr, curbdptr);
	}

	/* Subtract off any partial packet BDs found */
	bdcount -= bdpartialcount;

	/* If bdcount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return number of BDs
	 */
	if (bdcount > 0) {
		*bdptr = ringptr->hwhead;
		ringptr->hwcnt   -= bdcount;
		ringptr->postcnt += bdcount;
		XEMACPSS_RING_SEEKAHEAD(ringptr, ringptr->hwhead, bdcount);
		return bdcount;
	} else {
		*bdptr = NULL;
		return 0;
	}
}

/**
 * xemacpss_bdringfromhwrx - returns a set of BD(s) that have been
 * processed by hardware in rx direction.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @bdlimit: maximum number of BDs to return in the set.
 * @bdptr: output parameter points to the first BD available for
 *         examination.
 * return number of BDs processed by hardware.
 **/
u32 xemacpss_bdringfromhwrx(struct xemacpss_bdring *ringptr, int bdlimit,
		struct xemacpss_bd **bdptr)
{
	struct xemacpss_bd *curbdptr;
	u32 bdadd = 0;
	int bdcount = 0;
	curbdptr = ringptr->hwhead;

	/* If no BDs in work group, then there's nothing to search */
	if (ringptr->hwcnt == 0) {
		*bdptr = NULL;
		return 0;
	}

	if (bdlimit > ringptr->hwcnt) {
		bdlimit = ringptr->hwcnt;
	}

	/* Starting at hwhead, keep moving forward in the list until:
	 *  - A BD is encountered with its new/used bit set which means
	 *    hardware has not completed processing of that BD.
	 *  - ringptr->hwtail is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (bdcount < bdlimit) {
		/* Read the status word to see if BD has been processed. */
		bdadd = xemacpss_read(curbdptr, XEMACPSS_BD_ADDR_OFFSET);
		if (bdadd & XEMACPSS_RXBUF_NEW_MASK) {
			bdcount++;
		} else {
			break;
		}

		/* Move on to next BD in work group */
		curbdptr = XEMACPSS_BDRING_NEXT(ringptr, curbdptr);
	}

	/* If bdcount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return number of BDs
	 */
	if (bdcount > 0) {
		*bdptr = ringptr->hwhead;
		ringptr->hwcnt   -= bdcount;
		ringptr->postcnt += bdcount;
		XEMACPSS_RING_SEEKAHEAD(ringptr, ringptr->hwhead, bdcount);
		return bdcount;
	} else {
		*bdptr = NULL;
		return 0;
	}
}

/**
 * xemacpss_bdringfree - Free a set of BDs that has been retrieved with
 * xemacpss_bdringfromhw().
 * previously allocated by xemacpss_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to allocate.
 * @bdptr: the head of BD list returned by xemacpss_bdringfromhw().
 * return 0 on success, negative value if error.
 **/
int xemacpss_bdringfree(struct xemacpss_bdring *ringptr, unsigned numbd,
		struct xemacpss_bd *bdptr)
{
	/* if no bds to free, simply return. */
	if (0 == numbd)
		return 0;

	/* Make sure we are in sync with xemacpss_bdringfromhw() */
	if ((ringptr->postcnt < numbd) || (ringptr->posthead != bdptr)) {
		printk(KERN_ERR "GEM: Improper bdringfree()\n");
		return -ENOSPC;
	}

	/* Update pointers and counters */
	ringptr->freecnt += numbd;
	ringptr->postcnt -= numbd;
	XEMACPSS_RING_SEEKAHEAD(ringptr, ringptr->posthead, numbd);
	return 0;
}


/**
 * xemacpss_DmaSetupRecvBuffers - allocates socket buffers (sk_buff's) 
 * up to the number of free RX buffer descriptors. Then it sets up the RX
 * buffer descriptors to DMA into the socket_buffers.
 * @ndev: the net_device 
 **/
static void xemacpss_DmaSetupRecvBuffers(struct net_device *ndev)
{
	struct net_local *lp;
	struct xemacpss_bdring *rxringptr;
	struct xemacpss_bd *bdptr;
	struct sk_buff *new_skb;
	u32 new_skb_baddr;
	int free_bd_count;
	int num_sk_buffs;
	int bdidx;
	int result;

	lp = (struct net_local *) netdev_priv(ndev);
	rxringptr = &lp->rx_ring;
	free_bd_count = rxringptr->freecnt;

	for (num_sk_buffs = 0; num_sk_buffs < free_bd_count; num_sk_buffs++) {
		new_skb = netdev_alloc_skb(ndev, XEMACPSS_RX_BUF_SIZE);
		if (new_skb == NULL) {
			break;
		}

		result = xemacpss_bdringalloc(rxringptr, 1, &bdptr);
		if (result) {
			printk(KERN_ERR "%s RX bdringalloc() error.\n", lp->ndev->name);
			break;
		}

		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(ndev->dev.parent, new_skb->data,
			XEMACPSS_RX_BUF_SIZE, DMA_FROM_DEVICE);

		XEMACPSS_SET_BUFADDR_RX(bdptr, new_skb_baddr);
		bdidx = XEMACPSS_BD_TO_INDEX(rxringptr, bdptr);
		lp->rx_skb[bdidx].skb = new_skb;
		lp->rx_skb[bdidx].mapping = new_skb_baddr;
		wmb();

		/* enqueue RxBD with the attached skb buffers such that it is
		 * ready for frame reception
		 */
		result = xemacpss_bdringtohw(rxringptr, 1, bdptr);
		if (result) {
			printk(KERN_ERR "%s: bdringtohw unsuccessful (%d)\n",
				ndev->name, result);
			break;
		}
	}
}

#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP

/**
 * xemacpss_get_hwticks - get the current value of the GEM internal timer
 * @lp: local device instance pointer
 * return: nothing
 **/
static inline void
xemacpss_get_hwticks(struct net_local *lp, u64 *sec, u64 *nsec)
{
	do { 
		*nsec = xemacpss_read(lp->baseaddr, XEMACPSS_1588NS_OFFSET);
		*sec = xemacpss_read(lp->baseaddr, XEMACPSS_1588S_OFFSET);
	} while (*nsec > xemacpss_read(lp->baseaddr, XEMACPSS_1588NS_OFFSET) );
}

/**
 * xemacpss_read_clock - read raw cycle counter (to be used by time counter)
 */
static cycle_t xemacpss_read_clock(const struct cyclecounter *tc)
{
	struct net_local *lp =
                container_of(tc, struct net_local, cycles);
        u64 stamp;
	u64 sec, nsec;

	xemacpss_get_hwticks(lp, &sec, &nsec);
	stamp = (sec << 32) | nsec;

        return stamp;
}


/**
 * xemacpss_systim_to_hwtstamp - convert system time value to hw timestamp
 * @adapter: board private structure
 * @shhwtstamps: timestamp structure to update
 * @regval: unsigned 64bit system time value.
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions
 */
static void xemacpss_systim_to_hwtstamp(struct net_local *lp,
                                   struct skb_shared_hwtstamps *shhwtstamps,
                                   u64 regval)
{
    u64 ns;

    ns = timecounter_cyc2time(&lp->clock, regval);
    timecompare_update(&lp->compare, ns);
    memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
    shhwtstamps->hwtstamp = ns_to_ktime(ns);
    shhwtstamps->syststamp = timecompare_transform(&lp->compare, ns);
}

static void xemacpss_rx_hwtstamp(struct net_local *lp, struct sk_buff *skb)
{
	u64 time64, sec, nsec, packet_ns_stamp;
			
	/* Get the current hw timer value */
	xemacpss_get_hwticks(lp, &sec, &nsec);

	/* Get the receive timestamp value for this packet.
	 * The GEM has been configured to record this in the FCS field of the 
	 * packet.  Only the nanoseconds value was recorded (so the present
	 * timestamp is used to fill in the rest.)
	 * NOTE: this means there is a maximum of 1 second to process this
	 * packet to this point before overflow.
	 */
	packet_ns_stamp = *(skb->tail-1) << 24 |
					*(skb->tail-2) << 16 |
					*(skb->tail-3) <<  8 |
					*(skb->tail-4);

	/* ns wrap ? */
	if (nsec < packet_ns_stamp) {
		sec--;
	}

	time64 = (sec << 32) | packet_ns_stamp;
	xemacpss_systim_to_hwtstamp(lp, skb_hwtstamps(skb), time64);
}
#endif /* CONFIG_XILINX_PSS_EMAC_HWTSTAMP */

/**
 * xemacpss_rx - process received packets when napi called
 * @lp: local device instance pointer
 * @budget: NAPI budget
 * return: number of BDs processed
 **/
static int xemacpss_rx(struct net_local *lp, int budget)
{
	u32 regval, len = 0;
	struct sk_buff *skb = NULL;
	struct xemacpss_bd *bdptr, *bdptrfree;
	unsigned int numbdfree, numbd = 0, bdidx = 0, rc = 0;

	numbd = xemacpss_bdringfromhwrx(&lp->rx_ring, budget, &bdptr);

	numbdfree = numbd;
	bdptrfree = bdptr;

#ifdef DEBUG_VERBOSE
	printk(KERN_INFO "GEM: %s: numbd %d\n",
			__FUNCTION__, numbd);
#endif 

	while (numbd) {
		bdidx = XEMACPSS_BD_TO_INDEX(&lp->rx_ring, bdptr);
		regval = xemacpss_read(bdptr, XEMACPSS_BD_STAT_OFFSET);

#ifdef DEBUG_VERBOSE
		printk(KERN_INFO "GEM: %s: RX BD index %d, BDptr %p, BD_STAT 0x%08x\n",
			__FUNCTION__, bdidx, bdptr, regval);
#endif

		/* look for start of packet */
		if (!(regval & XEMACPSS_RXBUF_SOF_MASK) ||
		    !(regval & XEMACPSS_RXBUF_EOF_MASK)) {
			printk(KERN_INFO "GEM: %s: SOF and EOF not set (0x%08x) BD %p\n",
				__FUNCTION__, regval, bdptr);
			return 0;
		}

		/* the packet length */
		len = regval & XEMACPSS_RXBUF_LEN_MASK;

		skb = lp->rx_skb[bdidx].skb;
		dma_unmap_single(lp->ndev->dev.parent, lp->rx_skb[bdidx].mapping,
						 XEMACPSS_RX_BUF_SIZE,
						 DMA_FROM_DEVICE);

		lp->rx_skb[bdidx].skb = NULL;
		lp->rx_skb[bdidx].mapping = 0;

		/* setup received skb and send it upstream */
		skb_put(skb, len);  /* Tell the skb how much data we got. */
		skb->dev = lp->ndev;

		/* Why does this return the protocol in network bye order ? */
		skb->protocol = eth_type_trans(skb, lp->ndev);

		skb->ip_summed = lp->ip_summed;

#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP
		if ((lp->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) &&
		    (ntohs(skb->protocol) == 0x800)) {
			unsigned ip_proto, dest_port;

			/* While the GEM can timestamp PTP packets, it does not mark the
			 * RX descriptor to identify them.  This is entirely the wrong
			 * place to be parsing UDP headers, but some minimal effort must
			 * be made.
			 * NOTE: the below parsing of ip_proto and dest_port depend on
			 * the use of Ethernet_II encapsulation, IPv4 without any options.
			 */
	 		ip_proto = *((u8*)skb->mac_header + 14 + 9);
	 		dest_port = ntohs(*(((u16*)skb->mac_header) + ((14 + 20 + 2)/2) ));
			if ((ip_proto == IPPROTO_UDP) &&
			    (dest_port == 0x13F)) {

				/* Timestamp this packet */
				xemacpss_rx_hwtstamp(lp, skb);
			}
		}
#endif /* CONFIG_XILINX_PSS_EMAC_HWTSTAMP */

		lp->stats.rx_packets++;
		lp->stats.rx_bytes += len;
		netif_receive_skb(skb);

		bdptr = XEMACPSS_BDRING_NEXT(&lp->rx_ring, bdptr);
		numbd--;
	}

	/* Make used BDs available */
	rc = xemacpss_bdringfree(&lp->rx_ring, numbdfree, bdptrfree);
	if (rc)
		printk(KERN_ERR "%s RX bdringfree() error.\n", lp->ndev->name);

	/* Refill RX buffers */
	xemacpss_DmaSetupRecvBuffers(lp->ndev);

	return numbdfree;
}

/**
 * xemacpss_rx_poll - NAPI poll routine
 * napi: pointer to napi struct
 * budget:
 **/
static int xemacpss_rx_poll(struct napi_struct *napi, int budget)
{
	struct net_local *lp = container_of(napi, struct net_local, napi);
	int work_done = 0;
	u32 regval;

	regval = xemacpss_read(lp->baseaddr, XEMACPSS_RXSR_OFFSET);
	xemacpss_write(lp->baseaddr, XEMACPSS_RXSR_OFFSET, regval);

	while (work_done < budget) {

		dev_dbg(&lp->pdev->dev, "poll RX status 0x%x weight 0x%x\n",
			regval, budget);

		if (!(regval & XEMACPSS_RXSR_FRAMERX_MASK)) {
			dev_dbg(&lp->pdev->dev, "No RX complete status 0x%x\n",
				regval);
			napi_complete(napi);

			/* We disable RX interrupts in interrupt service routine, now
			 * it is time to enable it back.
			 */
			regval = (XEMACPSS_IXR_FRAMERX_MASK | XEMACPSS_IXR_RX_ERR_MASK);
			xemacpss_write(lp->baseaddr, XEMACPSS_IER_OFFSET, regval);
			break;
		}

		work_done += xemacpss_rx(lp, budget - work_done);

		regval = xemacpss_read(lp->baseaddr, XEMACPSS_RXSR_OFFSET);
		xemacpss_write(lp->baseaddr, XEMACPSS_RXSR_OFFSET, regval);
	}

	return work_done;
}

/**
 * xemacpss_tx_poll - tasklet poll routine
 * @data: pointer to network interface device structure
 **/
static void xemacpss_tx_poll(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval, len = 0;
	struct xemacpss_bd *bdptr, *bdptrfree;
	struct ring_info *rp;
	struct sk_buff *skb;
	unsigned int numbd, numbdfree, bdidx, rc;

	regval = xemacpss_read(lp->baseaddr, XEMACPSS_TXSR_OFFSET);
	xemacpss_write(lp->baseaddr, XEMACPSS_TXSR_OFFSET, regval);
	dev_dbg(&lp->pdev->dev, "TX status 0x%x\n", regval);

	/* If this error is seen, it is in deep trouble and nothing
	 * we can do to revive hardware other than reset hardware.
	 * Or try to close this interface and reopen it.
	 */
	if (regval & (XEMACPSS_TXSR_URUN_MASK | XEMACPSS_TXSR_RXOVR_MASK |
		XEMACPSS_TXSR_HRESPNOK_MASK | XEMACPSS_TXSR_COL1000_MASK |
		XEMACPSS_TXSR_BUFEXH_MASK | XEMACPSS_TXSR_COL100_MASK)) {
		printk(KERN_ERR "%s: TX error 0x%x, resetting buffers?\n",
			ndev->name, regval);
		lp->stats.tx_errors++;
	}

	/* This may happen when a buffer becomes complete
	 * between reading the ISR and scanning the descriptors.
	 * Nothing to worry about.
	 */
	if (!(regval & XEMACPSS_TXSR_TXCOMPL_MASK)) {
		goto tx_poll_out;
	}

	numbd = xemacpss_bdringfromhwtx(&lp->tx_ring, XEMACPSS_SEND_BD_CNT,
		&bdptr);
	numbdfree = numbd;
	bdptrfree = bdptr;

	while (numbd) {
		rmb();
		regval  = xemacpss_read(bdptr, XEMACPSS_BD_STAT_OFFSET);
		bdidx = XEMACPSS_BD_TO_INDEX(&lp->tx_ring, bdptr);
		rp = &lp->tx_skb[bdidx];
		skb = rp->skb;

		BUG_ON(skb == NULL);

		len += skb->len;
		rmb();
		dma_unmap_single(&lp->pdev->dev, rp->mapping, skb->len,
			DMA_TO_DEVICE);
		rp->skb = NULL;
		dev_kfree_skb_irq(skb);
#ifdef DEBUG_VERBOSE_TX
		printk(KERN_INFO "GEM: TX bd index %d BD_STAT 0x%08x after sent.\n",
			bdidx, regval);
#endif
		/* log tx completed packets and bytes, errors logs
		 * are in other error counters.
		 */
		if (regval & XEMACPSS_TXBUF_LAST_MASK) {
			if (!(regval & XEMACPSS_TXBUF_ERR_MASK)) {
				lp->stats.tx_packets++;
				lp->stats.tx_bytes += len;
			} else {
				lp->stats.tx_errors++;
			}
			len = 0;
		}

		/* Preserve used and wrap bits; clear everything else. */
		regval &= (XEMACPSS_TXBUF_USED_MASK | XEMACPSS_TXBUF_WRAP_MASK);
		xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET, regval);

		bdptr = XEMACPSS_BDRING_NEXT(&lp->tx_ring, bdptr);
		numbd--;
		wmb();
	}

	rc = xemacpss_bdringfree(&lp->tx_ring, numbdfree, bdptrfree);
	if (rc)
		printk(KERN_ERR "%s TX bdringfree() error.\n", ndev->name);

tx_poll_out:
	if (netif_queue_stopped(ndev))
		netif_start_queue(ndev);
}

/**
 * xemacpss_interrupt - interrupt main service routine
 * @irq: interrupt number
 * @dev_id: pointer to a network device structure
 * return IRQ_HANDLED or IRQ_NONE
 **/
static irqreturn_t xemacpss_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct net_local *lp = netdev_priv(ndev);
	u32 regisr;

	regisr = xemacpss_read(lp->baseaddr, XEMACPSS_ISR_OFFSET);

	if (unlikely(!regisr))
		return IRQ_NONE;

	spin_lock(&lp->lock);

	while (regisr) {
		/* acknowledge interrupt and clear it */
		xemacpss_write(lp->baseaddr, XEMACPSS_ISR_OFFSET, regisr);

		/* Log errors here. ISR status is cleared;
		 * this must be recorded here.
		 */
		if (regisr & XEMACPSS_IXR_RX_ERR_MASK)
			lp->stats.rx_errors++;

		/* RX interrupts */
		if (regisr &
		(XEMACPSS_IXR_FRAMERX_MASK | XEMACPSS_IXR_RX_ERR_MASK)) {

			if (napi_schedule_prep(&lp->napi)) {
				/* acknowledge RX interrupt and disable it,
				 * napi will be the one processing it.  */
				xemacpss_write(lp->baseaddr,
					XEMACPSS_IDR_OFFSET,
					(XEMACPSS_IXR_FRAMERX_MASK |
					 XEMACPSS_IXR_RX_ERR_MASK));
				dev_dbg(&lp->pdev->dev,
					"schedule RX softirq\n");
				__napi_schedule(&lp->napi);
			}
		}

		/* TX interrupts */
		if (regisr &
		(XEMACPSS_IXR_TXCOMPL_MASK | XEMACPSS_IXR_TX_ERR_MASK))
		{
			xemacpss_tx_poll(ndev);
		}

		regisr = xemacpss_read(lp->baseaddr, XEMACPSS_ISR_OFFSET);
	}
	spin_unlock(&lp->lock);

	return IRQ_HANDLED;
}

/*
 * Free all packets presently in the descriptor rings.
 */
static void xemacpss_clean_rings(struct net_local *lp)
{
	int i;

	for (i=0; i < XEMACPSS_RECV_BD_CNT; i++) {
		if (lp->rx_skb && lp->rx_skb[i].skb) {
			dma_unmap_single(lp->ndev->dev.parent,
			                 lp->rx_skb[i].mapping,
					 XEMACPSS_RX_BUF_SIZE,
					 DMA_FROM_DEVICE);

			dev_kfree_skb(lp->rx_skb[i].skb);
			lp->rx_skb[i].skb = NULL;
			lp->rx_skb[i].mapping = 0;
		}
	}

	for (i=0; i < XEMACPSS_SEND_BD_CNT; i++) {
		if (lp->tx_skb && lp->tx_skb[i].skb) {
			dma_unmap_single(lp->ndev->dev.parent,
			                 lp->tx_skb[i].mapping,
					 lp->tx_skb[i].skb->len,
					 DMA_TO_DEVICE);

			dev_kfree_skb(lp->tx_skb[i].skb);
			lp->tx_skb[i].skb = NULL;
			lp->tx_skb[i].mapping = 0;
		}
	}
}

/**
 * xemacpss_descriptor_free - Free allocated TX and RX BDs
 * @lp: local device instance pointer
 **/
static void xemacpss_descriptor_free(struct net_local *lp)
{
	int size;

	xemacpss_clean_rings(lp);

	/* kfree(NULL) is safe, no need to check here */
	kfree(lp->tx_skb);
	lp->tx_skb = NULL;
	kfree(lp->rx_skb);
	lp->rx_skb = NULL;

	size = XEMACPSS_RECV_BD_CNT * sizeof(struct xemacpss_bd);
	if (lp->rx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->rx_bd, lp->rx_bd_dma);
		lp->rx_bd = NULL;
	}

	size = XEMACPSS_SEND_BD_CNT * sizeof(struct xemacpss_bd);
	if (lp->tx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->tx_bd, lp->tx_bd_dma);
		lp->tx_bd = NULL;
	}
}

/**
 * xemacpss_descriptor_init - Allocate both TX and RX BDs
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacpss_descriptor_init(struct net_local *lp)
{
	int size;

	size = XEMACPSS_SEND_BD_CNT * sizeof(struct ring_info);
	lp->tx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->tx_skb)
		goto err_out;
	size = XEMACPSS_RECV_BD_CNT * sizeof(struct ring_info);
	lp->rx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->rx_skb)
		goto err_out;

	size = XEMACPSS_RECV_BD_CNT * sizeof(struct xemacpss_bd);
	lp->rx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->rx_bd_dma, GFP_KERNEL);
	if (!lp->rx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "RX ring %d bytes at 0x%x mapped %p\n",
			size, lp->rx_bd_dma, lp->rx_bd);

	size = XEMACPSS_SEND_BD_CNT * sizeof(struct xemacpss_bd);
	lp->tx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->tx_bd_dma, GFP_KERNEL);
	if (!lp->tx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "TX ring %d bytes at 0x%x mapped %p\n",
			size, lp->tx_bd_dma, lp->tx_bd);

#ifdef DEBUG
	printk(KERN_INFO "GEM: lp->tx_bd %p lp->tx_bd_dma %p lp->tx_skb %p\n",
		lp->tx_bd, (void*)lp->tx_bd_dma, lp->tx_skb);
	printk(KERN_INFO "GEM: lp->rx_bd %p lp->rx_bd_dma %p lp->rx_skb %p\n",
		lp->rx_bd, (void*)lp->rx_bd_dma, lp->rx_skb);
#endif
	return 0;

err_out:
	xemacpss_descriptor_free(lp);
	return -ENOMEM;
}

/**
 * xemacpss_setup_ring - Setup both TX and RX BD rings
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacpss_setup_ring(struct net_local *lp)
{
	int i;
	u32 regval;
	struct xemacpss_bd *bdptr;

	lp->rx_ring.separation   = (sizeof(struct xemacpss_bd) +
		(ALIGNMENT_BD - 1)) & ~(ALIGNMENT_BD - 1);
	lp->rx_ring.physbaseaddr = lp->rx_bd_dma;
	lp->rx_ring.firstbdaddr  = (u32)lp->rx_bd;
	lp->rx_ring.lastbdaddr   = (u32)(lp->rx_bd +
		(XEMACPSS_RECV_BD_CNT - 1) * sizeof(struct xemacpss_bd));
	lp->rx_ring.length       = lp->rx_ring.lastbdaddr -
		lp->rx_ring.firstbdaddr + lp->rx_ring.separation;
	lp->rx_ring.freehead     = (struct xemacpss_bd *)lp->rx_bd;
	lp->rx_ring.prehead      = (struct xemacpss_bd *)lp->rx_bd;
	lp->rx_ring.hwhead       = (struct xemacpss_bd *)lp->rx_bd;
	lp->rx_ring.hwtail       = (struct xemacpss_bd *)lp->rx_bd;
	lp->rx_ring.posthead     = (struct xemacpss_bd *)lp->rx_bd;
	lp->rx_ring.allcnt       = XEMACPSS_RECV_BD_CNT;
	lp->rx_ring.freecnt      = XEMACPSS_RECV_BD_CNT;
	lp->rx_ring.precnt       = 0;
	lp->rx_ring.hwcnt        = 0;
	lp->rx_ring.postcnt      = 0;
	lp->rx_ring.is_rx        = 1;

	bdptr = (struct xemacpss_bd *)lp->rx_ring.firstbdaddr;

	/* Setup RX BD ring structure and populate buffer address. */
	for (i = 0; i < (XEMACPSS_RECV_BD_CNT - 1); i++) {
		xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET, 0);
		xemacpss_write(bdptr, XEMACPSS_BD_ADDR_OFFSET, 0);
		bdptr = XEMACPSS_BDRING_NEXT(&lp->rx_ring, bdptr);
	}
	/* wrap bit set for last BD, bdptr is moved to last here */
	xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET, 0);
	xemacpss_write(bdptr, XEMACPSS_BD_ADDR_OFFSET, XEMACPSS_RXBUF_WRAP_MASK);

	/* Allocate RX skbuffs; set descriptor buffer addresses */
	xemacpss_DmaSetupRecvBuffers(lp->ndev);

	lp->tx_ring.separation   = (sizeof(struct xemacpss_bd) +
		(ALIGNMENT_BD - 1)) & ~(ALIGNMENT_BD - 1);
	lp->tx_ring.physbaseaddr = lp->tx_bd_dma;
	lp->tx_ring.firstbdaddr  = (u32)lp->tx_bd;
	lp->tx_ring.lastbdaddr   = (u32)(lp->tx_bd +
		(XEMACPSS_SEND_BD_CNT - 1) * sizeof(struct xemacpss_bd));
	lp->tx_ring.length       = lp->tx_ring.lastbdaddr -
		lp->tx_ring.firstbdaddr + lp->tx_ring.separation;
	lp->tx_ring.freehead     = (struct xemacpss_bd *)lp->tx_bd;
	lp->tx_ring.prehead      = (struct xemacpss_bd *)lp->tx_bd;
	lp->tx_ring.hwhead       = (struct xemacpss_bd *)lp->tx_bd;
	lp->tx_ring.hwtail       = (struct xemacpss_bd *)lp->tx_bd;
	lp->tx_ring.posthead     = (struct xemacpss_bd *)lp->tx_bd;
	lp->tx_ring.allcnt       = XEMACPSS_SEND_BD_CNT;
	lp->tx_ring.freecnt      = XEMACPSS_SEND_BD_CNT;
	lp->tx_ring.precnt       = 0;
	lp->tx_ring.hwcnt        = 0;
	lp->tx_ring.postcnt      = 0;
	lp->tx_ring.is_rx        = 0;

	bdptr = (struct xemacpss_bd *)lp->tx_ring.firstbdaddr;

	/* Setup TX BD ring structure and assert used bit initially. */
	for (i = 0; i < (XEMACPSS_SEND_BD_CNT - 1); i++) {
		xemacpss_write(bdptr, XEMACPSS_BD_ADDR_OFFSET, 0);
		xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET,
			XEMACPSS_TXBUF_USED_MASK);
		bdptr = XEMACPSS_BDRING_NEXT(&lp->tx_ring, bdptr);
	}
	/* wrap bit set for last BD, bdptr is moved to last here */
	xemacpss_write(bdptr, XEMACPSS_BD_ADDR_OFFSET, 0);
	regval = (XEMACPSS_TXBUF_WRAP_MASK | XEMACPSS_TXBUF_USED_MASK);
	xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET, regval);

	return 0;
}

#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP

#define NS_PER_SEC 1000000000ULL      /* Nanoseconds per second */
#define FP_MULT    100000000ULL       /* Defined Fixed Point */
#define FP_ROUNDUP (FP_MULT / 200000) /* Value used to round up fractionals */
#define FRAC_MIN   (FP_MULT / 1000)   /* Expect at lest four digits of '0' */

/*
 * Calculate clock configuration register values for indicated input clock
 */
static unsigned xemacpss_tsu_calc_clk(u32 freq)
{
    u64 period_ns_XFP;
    u64 nn;
    u64 acc;
    u64 iacc;
    u64 int1, int2;
    u64 frac_part;
    unsigned retval;

    retval = 0;
    period_ns_XFP = (NS_PER_SEC * FP_MULT)/freq;

    nn = 1;
    while (nn <= 256) {
        acc = (nn * period_ns_XFP) + FP_ROUNDUP;
        iacc = acc/FP_MULT;
        frac_part = acc - ((acc/FP_MULT) *FP_MULT);

        if (frac_part <= (FP_MULT/FRAC_MIN) ) {
            break;
        } else {
            nn += 1;
        }
    }

    if (nn > 256) {
        printk(KERN_ERR "GEM: failed to calculate TSU input clock config.\n");
    } else {
        int1 = period_ns_XFP / FP_MULT;
        int2 = iacc - (nn-1)*int1;
        retval =  ((nn - 1) << 16) | (int2 << 8) | int1;
#ifdef DEBUG
        printk(KERN_INFO "GEM: TSU: %lld x %lld = %lld.%08lld\n",
            int1, nn, iacc, frac_part);
        printk(KERN_INFO "GEM: TSU:  solution: %lld of %lld, then 1 of %lld\n",
            nn-1, int1, int2);
#endif
    }

    return retval;
}

/*
 * Initialize the GEM Time Stamp Unit
 */
static void xemacpss_init_tsu(struct net_local *lp, u32 tsu_clock_hz)
{
	struct timeval tv;
u32 regval;

	/* Stuff the timer with some (totally incorrect...) inital concept
	 * of the time.
	 */
	tv = ktime_to_timeval(ktime_get_real());
	xemacpss_write(lp->baseaddr, XEMACPSS_1588NS_OFFSET, tv.tv_usec * 1000);
	xemacpss_write(lp->baseaddr, XEMACPSS_1588S_OFFSET, tv.tv_sec);

	/* program the timer increment register with the numer of nanoseconds
	 * per clock tick.
	 */
	xemacpss_write(lp->baseaddr, XEMACPSS_1588INC_OFFSET, 
		xemacpss_tsu_calc_clk(tsu_clock_hz) );

	memset(&lp->cycles, 0, sizeof(lp->cycles));
	lp->cycles.read = xemacpss_read_clock;
	lp->cycles.mask = CLOCKSOURCE_MASK(64);
	lp->cycles.mult = 1;

	timecounter_init(&lp->clock,
	                 &lp->cycles,
	                 ktime_to_ns(ktime_get_real()));
	/*
	 * Synchronize our NIC clock against system wall clock. 
	 */
	memset(&lp->compare, 0, sizeof(lp->compare));
	lp->compare.source = &lp->clock;
	lp->compare.target = ktime_get_real;
	lp->compare.num_samples = 10;
	timecompare_update(&lp->compare, 0);

/* HACK FIXME BHILL -- remove this - perform in ioctl */
	/* Do not strip RX FCS */
	regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCFG_OFFSET);
	regval &= ~XEMACPSS_NWCFG_FCSREM_MASK;
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET, regval);

/* HACK FIXME BHILL -- remove this - perform in ioctl */
	/* replace RX FCS with present counter nanosecond snapshot */
	regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET);
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET,
			(regval | XEMACPSS_NWCTRL_RXTSTAMP_MASK));
}
#endif /* CONFIG_XILINX_PSS_EMAC_HWTSTAMP */

/**
 * xemacpss_init_hw - Initialize hardware to known good state
 * @lp: local device instance pointer
 **/
static void xemacpss_init_hw(struct net_local *lp)
{
	u32 regval;

	xemacpss_reset_hw(lp);
	xemacpss_set_hwaddr(lp);

	/* network configuration */
	regval  = 0;
	regval |= XEMACPSS_NWCFG_FDEN_MASK;
	regval |= XEMACPSS_NWCFG_RXCHKSUMEN_MASK;
	regval |= XEMACPSS_NWCFG_PAUSECOPYDI_MASK;
	regval |= XEMACPSS_NWCFG_FCSREM_MASK;
	regval |= XEMACPSS_NWCFG_PAUSEEN_MASK;
	regval |= XEMACPSS_NWCFG_100_MASK;
	regval |= XEMACPSS_NWCFG_1536RXEN_MASK;
	regval |= (MDC_DIV_32 << XEMACPSS_NWCFG_MDC_SHIFT_MASK);
	if (lp->ndev->flags & IFF_PROMISC)	/* copy all */
		regval |= XEMACPSS_NWCFG_COPYALLEN_MASK;
	if (!(lp->ndev->flags & IFF_BROADCAST))	/* No broadcast */
		regval |= XEMACPSS_NWCFG_BCASTDI_MASK;
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET, regval);

	/* Init TX and RX DMA Q address */
	xemacpss_write(lp->baseaddr, XEMACPSS_RXQBASE_OFFSET,
		lp->rx_ring.physbaseaddr);
	xemacpss_write(lp->baseaddr, XEMACPSS_TXQBASE_OFFSET,
		lp->tx_ring.physbaseaddr);

	/* DMACR configurations */
	regval  = (((XEMACPSS_RX_BUF_SIZE / XEMACPSS_RX_BUF_UNIT) +
		((XEMACPSS_RX_BUF_SIZE % XEMACPSS_RX_BUF_UNIT) ? 1 : 0)) <<
		XEMACPSS_DMACR_RXBUF_SHIFT);
	regval |= XEMACPSS_DMACR_RXSIZE_MASK;
	regval |= XEMACPSS_DMACR_TXSIZE_MASK;
	regval |= XEMACPSS_DMACR_TCPCKSUM_MASK;
#ifdef __LITTLE_ENDIAN
	regval &= ~XEMACPSS_DMACR_ENDIAN_MASK;
#endif
#ifdef __BIG_ENDIAN
	regval |= XEMACPSS_DMACR_ENDIAN_MASK;
#endif
	xemacpss_write(lp->baseaddr, XEMACPSS_DMACR_OFFSET, regval);

	/* Enable TX, RX and MDIO port */
	regval  = 0;
	regval |= XEMACPSS_NWCTRL_MDEN_MASK;
	regval |= XEMACPSS_NWCTRL_TXEN_MASK;
	regval |= XEMACPSS_NWCTRL_RXEN_MASK;
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET, regval);

#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP
	/* Initialize the Time Stamp Unit */
	xemacpss_init_tsu(lp, 50000000);
#endif

	/* Enable interrupts */
	regval  = XEMACPSS_IXR_ALL_MASK;
	xemacpss_write(lp->baseaddr, XEMACPSS_IER_OFFSET, regval);
}

/**
 * xemacpss_open - Called when a network device is made active
 * @ndev: network interface device structure
 * return 0 on success, negative value if error
 *
 * The open entry point is called when a network interface is made active
 * by the system (IFF_UP). At this point all resources needed for transmit
 * and receive operations are allocated, the interrupt handler is
 * registered with OS, the watchdog timer is started, and the stack is
 * notified that the interface is ready.
 *
 * note: if error(s), allocated resources before error require to be
 * released or system issues (such as memory) leak might happen.
 **/
static int xemacpss_open(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	int rc;

	dev_dbg(&lp->pdev->dev, "open\n");
	if (!is_valid_ether_addr(ndev->dev_addr))
		return  -EADDRNOTAVAIL;

	rc = xemacpss_descriptor_init(lp);
	if (rc) {
		printk(KERN_ERR "%s Unable to allocate DMA memory, rc %d \n",
		ndev->name, rc);
		return rc;
	}

	rc = xemacpss_setup_ring(lp);
	if (rc) {
		printk(KERN_ERR "%s Unable to setup BD rings, rc %d \n",
		ndev->name, rc);
		return rc;
	}
	xemacpss_init_hw(lp);
	napi_enable(&lp->napi);
#ifdef DEBUG_SPEED
	xemacpss_phy_init(ndev);
#else
	if (lp->phy_dev)
		phy_start(lp->phy_dev);
#endif
	netif_carrier_on(ndev);

	netif_start_queue(ndev);

	return 0;
}

/**
 * xemacpss_close - disable a network interface
 * @ndev: network interface device structure
 * return 0
 *
 * The close entry point is called when a network interface is de-activated
 * by OS. The hardware is still under the driver control, but needs to be
 * disabled. A global MAC reset is issued to stop the hardware, and all
 * transmit and receive resources are freed.
 **/
static int xemacpss_close(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;

	netif_stop_queue(ndev);
	napi_disable(&lp->napi);
	if (lp->phy_dev)
		phy_stop(lp->phy_dev);

	spin_lock_irqsave(&lp->lock, flags);
	xemacpss_reset_hw(lp);
	netif_carrier_off(ndev);
	spin_unlock_irqrestore(&lp->lock, flags);
	xemacpss_descriptor_free(lp);

	return 0;
}

/**
 * xemacpss_tx_timeout - callback uses when the transmitter has not made
 * any progress for dev->watchdog ticks.
 * @ndev: network interface device structure
 **/
static void xemacpss_tx_timeout(struct net_device *ndev)
{
	unsigned long flags;
	struct net_local *lp = netdev_priv(ndev);
	int rc;

	printk(KERN_ERR "%s transmit timeout %lu ms, reseting...\n",
		ndev->name, TX_TIMEOUT * 1000UL / HZ);
	lp->stats.tx_errors++;

	spin_lock_irqsave(&lp->lock, flags);

	netif_stop_queue(ndev);
	napi_disable(&lp->napi);
	xemacpss_reset_hw(lp);
	xemacpss_clean_rings(lp);
	rc  = xemacpss_setup_ring(lp);
	if (rc)
		printk(KERN_ERR "%s Unable to setup BD or rings, rc %d\n",
		ndev->name, rc);
	xemacpss_init_hw(lp);
	ndev->trans_start = jiffies;
	napi_enable(&lp->napi);
	netif_wake_queue(ndev);

	spin_unlock_irqrestore(&lp->lock, flags);
}

/**
 * xemacpss_set_mac_address - set network interface mac address
 * @ndev: network interface device structure
 * @addr: pointer to MAC address
 * return 0 on success, negative value if error
 **/
static int xemacpss_set_mac_address(struct net_device *ndev, void *addr)
{
	struct net_local *lp = netdev_priv(ndev);
	struct sockaddr *hwaddr = (struct sockaddr *)addr;

	if (netif_running(ndev))
		return -EBUSY;

	if (!is_valid_ether_addr(hwaddr->sa_data))
		return -EADDRNOTAVAIL;
#ifdef DEBUG
	printk(KERN_INFO "GEM: hwaddr 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		hwaddr->sa_data[0], hwaddr->sa_data[1], hwaddr->sa_data[2],
		hwaddr->sa_data[3], hwaddr->sa_data[4], hwaddr->sa_data[5]);
#endif
	memcpy(ndev->dev_addr, hwaddr->sa_data, ndev->addr_len);

	xemacpss_set_hwaddr(lp);
	return 0;
}

/**
 * xemacpss_start_xmit - transmit a packet (called by kernel)
 * @skb: socket buffer
 * @ndev: network interface device structure
 * return 0 on success, other value if error
 **/
static int xemacpss_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	dma_addr_t  mapping;
	unsigned int nr_frags, bdidx, len;
	int i, rc;
	u32 regval;
	struct xemacpss_bd *bdptr, *bdptrs;
	void       *virt_addr;
	skb_frag_t *frag;

#ifdef DEBUG_VERBOSE_TX
	printk(KERN_INFO "%s: TX data:", __FUNCTION__);
	for (i = 0; i < 48; i++) {
		if (!(i % 16))
			printk("\n");
		printk(" %02x", (unsigned int)skb->data[i]);
	}
	printk("\n");
#endif

	nr_frags = skb_shinfo(skb)->nr_frags + 1;
	spin_lock_irq(&lp->lock);

	if (nr_frags < lp->tx_ring.freecnt) {
		rc = xemacpss_bdringalloc(&lp->tx_ring, nr_frags, &bdptr);
		if (rc) {
			netif_stop_queue(ndev); /* stop send queue */
			spin_unlock_irq(&lp->lock);
			return rc;
		}
	} else {
		netif_stop_queue(ndev); /* stop send queue */
		spin_unlock_irq(&lp->lock);
		return NETDEV_TX_BUSY;
	}

	frag = &skb_shinfo(skb)->frags[0];
	bdptrs = bdptr;

#ifdef DEBUG_VERBOSE_TX
	printk(KERN_INFO "GEM: TX nr_frags %d, skb->len 0x%x, skb_headlen(skb) 0x%x\n",
		nr_frags, skb->len, skb_headlen(skb));
#endif

	for (i = 0; i < nr_frags; i++) {
		if (i == 0) {
			len = skb_headlen(skb);
			mapping = dma_map_single(&lp->pdev->dev, skb->data,
				len, DMA_TO_DEVICE);
		} else {
			len = frag->size;
			virt_addr = (void *)page_address(frag->page) +
				frag->page_offset;
			mapping = dma_map_single(&lp->pdev->dev, virt_addr,
				len, DMA_TO_DEVICE);
			frag++;
		}

		bdidx = XEMACPSS_BD_TO_INDEX(&lp->tx_ring, bdptr);

		lp->tx_skb[bdidx].skb = skb;
		lp->tx_skb[bdidx].mapping = mapping;
		wmb();

		xemacpss_write(bdptr, XEMACPSS_BD_ADDR_OFFSET, mapping);
		wmb();

		regval = xemacpss_read(bdptr, XEMACPSS_BD_STAT_OFFSET);
		/* Preserve only critical status bits.  Packet is NOT to be
		 * committed to hardware at this time.
		 */
		regval &= (XEMACPSS_TXBUF_USED_MASK | XEMACPSS_TXBUF_WRAP_MASK);
		/* update length field */
		regval |= ((regval & ~XEMACPSS_TXBUF_LEN_MASK) | len);
		/* last fragment of this packet? */
		if (i == (nr_frags - 1)) {
			regval |= XEMACPSS_TXBUF_LAST_MASK;
		}
		xemacpss_write(bdptr, XEMACPSS_BD_STAT_OFFSET, regval);

#ifdef DEBUG_VERBOSE_TX
		printk(KERN_INFO "GEM: TX BD index %d, BDptr %p, BD_STAT 0x%08x\n",
			bdidx, bdptr, regval);
#endif
		bdptr = XEMACPSS_BDRING_NEXT(&lp->tx_ring, bdptr);
	}
	wmb();

	rc = xemacpss_bdringtohw(&lp->tx_ring, nr_frags, bdptrs);

	if (rc) {
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		lp->stats.tx_dropped++;
		xemacpss_bdringunalloc(&lp->tx_ring, nr_frags, bdptrs);
		printk(KERN_ERR "%s can not send, commit TX buffer desc\n",
			ndev->name);
		spin_unlock_irq(&lp->lock);
		return rc;
	} else {
		regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET);
		xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET,
			(regval | XEMACPSS_NWCTRL_STARTTX_MASK));
	}

	spin_unlock_irq(&lp->lock);
	ndev->trans_start = jiffies;

	return rc;
}

/*
 * Get the MAC Address bit from the specified position
 */
static unsigned get_bit(u8 *mac, unsigned bit)
{
	unsigned byte;

	byte = mac[bit / 8];
	byte >>= (bit & 0x7);
	byte &= 1;

	return byte;
}

/*
 * Calculate a GEM MAC Address hash index
 */
static unsigned calc_mac_hash(u8 *mac)
{
	int index_bit, mac_bit;
	unsigned hash_index;

	hash_index = 0;
	mac_bit = 5;
	for (index_bit = 5; index_bit >= 0; index_bit--) {
		hash_index |= (get_bit(mac,  mac_bit) ^
					get_bit(mac, mac_bit + 6) ^
					get_bit(mac, mac_bit + 12) ^
					get_bit(mac, mac_bit + 18) ^
					get_bit(mac, mac_bit + 24) ^
					get_bit(mac, mac_bit + 30) ^
					get_bit(mac, mac_bit + 36) ^
					get_bit(mac, mac_bit + 42)) << index_bit;
		mac_bit--;
	}

	return hash_index;
}

/**
 * xemacpss_set_hashtable - Add multicast addresses to the internal
 * multicast-hash table. Called from xemac_set_rx_mode().
 * @ndev: network interface device structure
 *
 * The hash address register is 64 bits long and takes up two
 * locations in the memory map.  The least significant bits are stored
 * in EMAC_HSL and the most significant bits in EMAC_HSH.
 *
 * The unicast hash enable and the multicast hash enable bits in the
 * network configuration register enable the reception of hash matched
 * frames. The destination address is reduced to a 6 bit index into
 * the 64 bit hash register using the following hash function.  The
 * hash function is an exclusive or of every sixth bit of the
 * destination address.
 *
 * hi[5] = da[5] ^ da[11] ^ da[17] ^ da[23] ^ da[29] ^ da[35] ^ da[41] ^ da[47]
 * hi[4] = da[4] ^ da[10] ^ da[16] ^ da[22] ^ da[28] ^ da[34] ^ da[40] ^ da[46]
 * hi[3] = da[3] ^ da[09] ^ da[15] ^ da[21] ^ da[27] ^ da[33] ^ da[39] ^ da[45]
 * hi[2] = da[2] ^ da[08] ^ da[14] ^ da[20] ^ da[26] ^ da[32] ^ da[38] ^ da[44]
 * hi[1] = da[1] ^ da[07] ^ da[13] ^ da[19] ^ da[25] ^ da[31] ^ da[37] ^ da[43]
 * hi[0] = da[0] ^ da[06] ^ da[12] ^ da[18] ^ da[24] ^ da[30] ^ da[36] ^ da[42]
 *
 * da[0] represents the least significant bit of the first byte
 * received, that is, the multicast/unicast indicator, and da[47]
 * represents the most significant bit of the last byte received.  If
 * the hash index, hi[n], points to a bit that is set in the hash
 * register then the frame will be matched according to whether the
 * frame is multicast or unicast.  A multicast match will be signalled
 * if the multicast hash enable bit is set, da[0] is 1 and the hash
 * index points to a bit set in the hash register.  A unicast match
 * will be signalled if the unicast hash enable bit is set, da[0] is 0
 * and the hash index points to a bit set in the hash register.  To
 * receive all multicast frames, the hash register should be set with
 * all ones and the multicast hash enable bit should be set in the
 * network configuration register.
 **/
static void xemacpss_set_hashtable(struct net_device *ndev)
{
	struct netdev_hw_addr *curr;
	u32 regvalh, regvall, hash_index;
	u8 *mc_addr;
	struct net_local *lp;

	lp = netdev_priv(ndev);

	regvalh = regvall = 0;

	netdev_for_each_mc_addr(curr, ndev) {
		if (!curr)	/* end of list */
			break;
		mc_addr = curr->addr;
#ifdef DEBUG
		printk(KERN_INFO "GEM: mc addr 0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\n",
		mc_addr[0], mc_addr[1], mc_addr[2],
		mc_addr[3], mc_addr[4], mc_addr[5]);
#endif
		hash_index = calc_mac_hash(mc_addr);

		if (hash_index >= XEMACPSS_MAX_HASH_BITS) {
			printk(KERN_ERR "hash calculation out of range %d\n",
				hash_index);
			break;
		}
		if (hash_index < 32)
			regvall |= (1 << hash_index);
		else
			regvalh |= (1 << (hash_index - 32));
	}

	xemacpss_write(lp->baseaddr, XEMACPSS_HASHL_OFFSET, regvall);
	xemacpss_write(lp->baseaddr, XEMACPSS_HASHH_OFFSET, regvalh);
}

/**
 * xemacpss_set_rx_mode - enable/disable promiscuous and multicast modes
 * @ndev: network interface device structure
 **/
static void xemacpss_set_rx_mode(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCFG_OFFSET);

	/* promisc mode */
	if (ndev->flags & IFF_PROMISC)
		regval |= XEMACPSS_NWCFG_COPYALLEN_MASK;
	if (!(ndev->flags & IFF_PROMISC))
		regval &= ~XEMACPSS_NWCFG_COPYALLEN_MASK;

	/* All multicast mode */
	if (ndev->flags & IFF_ALLMULTI) {
		regval |= XEMACPSS_NWCFG_MCASTHASHEN_MASK;
		xemacpss_write(lp->baseaddr, XEMACPSS_HASHL_OFFSET, ~0UL);
		xemacpss_write(lp->baseaddr, XEMACPSS_HASHH_OFFSET, ~0UL);
	/* Specific multicast mode */
	} else if ((ndev->flags & IFF_MULTICAST) && (netdev_mc_count(ndev) > 0)) {
		regval |= XEMACPSS_NWCFG_MCASTHASHEN_MASK;
		xemacpss_set_hashtable(ndev);
	/* Disable multicast mode */
	} else {
		xemacpss_write(lp->baseaddr, XEMACPSS_HASHL_OFFSET, 0x0);
		xemacpss_write(lp->baseaddr, XEMACPSS_HASHH_OFFSET, 0x0);
		regval &= ~XEMACPSS_NWCFG_MCASTHASHEN_MASK;
	}

	/* broadcast mode */
	if (ndev->flags & IFF_BROADCAST)
		regval &= ~XEMACPSS_NWCFG_BCASTDI_MASK;
	/* No broadcast */
	if (!(ndev->flags & IFF_BROADCAST))
		regval |= XEMACPSS_NWCFG_BCASTDI_MASK;

	xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET, regval);
}

#define MIN_MTU 60
#define MAX_MTU 1500
/**
 * xemacpss_change_mtu - Change maximum transfer unit
 * @ndev: network interface device structure
 * @new_mtu: new vlaue for maximum frame size
 * return: 0 on success, negative value if error.
 **/
static int xemacpss_change_mtu(struct net_device *ndev, int new_mtu)
{
	if ((new_mtu < MIN_MTU) ||
		((new_mtu + ndev->hard_header_len) > MAX_MTU))
		return -EINVAL;

	ndev->mtu = new_mtu;	/* change mtu in net_device structure */
	return 0;
}

/**
 * xemacpss_get_settings - get device specific settings.
 * Usage: Issue "ethtool ethX" under linux prompt.
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacpss_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, ecmd);
}

/**
 * xemacpss_set_settings - set device specific settings.
 * Usage: Issue "ethtool -s ethX speed 1000" under linux prompt
 * to change speed
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacpss_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, ecmd);
}

/**
 * xemacpss_get_drvinfo - report driver information
 * Usage: Issue "ethtool -i ethX" under linux prompt
 * @ndev: network device
 * @ed: device driver information structure
 **/
static void
xemacpss_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *ed)
{
	struct net_local *lp = netdev_priv(ndev);

	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strcpy(ed->driver, lp->pdev->dev.driver->name);
	strcpy(ed->version, DRIVER_VERSION);
}

/**
 * xemacpss_get_ringparam - get device dma ring information.
 * Usage: Issue "ethtool -g ethX" under linux prompt
 * @ndev: network device
 * @erp: ethtool ring parameter structure
 **/
static void
xemacpss_get_ringparam(struct net_device *ndev, struct ethtool_ringparam *erp)
{
	struct net_local *lp = netdev_priv(ndev);
	memset(erp, 0, sizeof(struct ethtool_ringparam));

	erp->rx_max_pending = XEMACPSS_RECV_BD_CNT;
	erp->tx_max_pending = XEMACPSS_SEND_BD_CNT;
	erp->rx_pending = lp->rx_ring.hwcnt;
	erp->tx_pending = lp->tx_ring.hwcnt;
}

/**
 * xemacpss_get_rx_csum - get device rxcsum status
 * Usage: Issue "ethtool -k ethX" under linux prompt
 * @ndev: network device
 * return 0 csum off, else csum on
 **/
static u32
xemacpss_get_rx_csum(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);

	return (lp->ip_summed & CHECKSUM_UNNECESSARY) != 0;
}

/**
 * xemacpss_set_rx_csum - set device rx csum enable/disable
 * Usage: Issue "ethtool -K ethX rx on|off" under linux prompt
 * @ndev: network device
 * @data: 0 to disable, other to enable
 * return 0 on success, negative value if error
 * note : If there is no need to turn on/off checksum engine e.g always on,
 * xemacpss_set_rx_csum can be removed.
 **/
static int
xemacpss_set_rx_csum(struct net_device *ndev, u32 data)
{
	struct net_local *lp = netdev_priv(ndev);

	if (data)
		lp->ip_summed = CHECKSUM_UNNECESSARY;
	else
		lp->ip_summed = CHECKSUM_NONE;

	return 0;
}

/**
 * xemacpss_get_tx_csum - get device txcsum status
 * Usage: Issue "ethtool -k ethX" under linux prompt
 * @ndev: network device
 * return 0 csum off, 1 csum on
 **/
static u32
xemacpss_get_tx_csum(struct net_device *ndev)
{
	return (ndev->features & NETIF_F_IP_CSUM) != 0;
}

/**
 * xemacpss_set_tx_csum - set device tx csum enable/disable
 * Usage: Issue "ethtool -K ethX tx on|off" under linux prompt
 * @ndev: network device
 * @data: 0 to disable, other to enable
 * return 0 on success, negative value if error
 * note : If there is no need to turn on/off checksum engine e.g always on,
 * xemacpss_set_tx_csum can be removed.
 **/
static int
xemacpss_set_tx_csum(struct net_device *ndev, u32 data)
{
	if (data)
		ndev->features |= NETIF_F_IP_CSUM;
	else
		ndev->features &= ~NETIF_F_IP_CSUM;
	return 0;
}

/**
 * xemacpss_get_wol - get device wake on lan status
 * Usage: Issue "ethtool ethX" under linux prompt
 * @ndev: network device
 * @ewol: wol status
 **/
static void
xemacpss_get_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	ewol->supported = WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST;
	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacpss_read(lp->baseaddr, XEMACPSS_WOL_OFFSET);
	if (regval | XEMACPSS_WOL_MCAST_MASK)
		ewol->wolopts |= WAKE_MCAST;
	if (regval | XEMACPSS_WOL_ARP_MASK)
		ewol->wolopts |= WAKE_ARP;
	if (regval | XEMACPSS_WOL_SPEREG1_MASK)
		ewol->wolopts |= WAKE_UCAST;
	if (regval | XEMACPSS_WOL_MAGIC_MASK)
		ewol->wolopts |= WAKE_MAGIC;
	spin_unlock_irqrestore(&lp->lock, flags);
}

/**
 * xemacpss_set_wol - set device wake on lan configuration
 * Usage: Issue "ethtool -s ethX wol u|m|b|g" under linux prompt to enable
 * specified type of packet.
 * Usage: Issue "ethtool -s ethX wol d" under linux prompt to disable
 * this feature.
 * @ndev: network device
 * @ewol: wol status
 * return 0 on success, negative value if not supported
 **/
static int
xemacpss_set_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	if (ewol->wolopts & ~(WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST))
		return -EOPNOTSUPP;

	spin_lock_irqsave(&lp->lock, flags);
	regval  = xemacpss_read(lp->baseaddr, XEMACPSS_WOL_OFFSET);
	regval &= ~(XEMACPSS_WOL_MCAST_MASK | XEMACPSS_WOL_ARP_MASK |
		XEMACPSS_WOL_SPEREG1_MASK | XEMACPSS_WOL_MAGIC_MASK);

	if (ewol->wolopts & WAKE_MAGIC)
		regval |= XEMACPSS_WOL_MAGIC_MASK;
	if (ewol->wolopts & WAKE_ARP)
		regval |= XEMACPSS_WOL_ARP_MASK;
	if (ewol->wolopts & WAKE_UCAST)
		regval |= XEMACPSS_WOL_SPEREG1_MASK;
	if (ewol->wolopts & WAKE_MCAST)
		regval |= XEMACPSS_WOL_MCAST_MASK;

	xemacpss_write(lp->baseaddr, XEMACPSS_WOL_OFFSET, regval);
	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

/**
 * xemacpss_get_pauseparam - get device pause status
 * Usage: Issue "ethtool -a ethX" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 *
 * note: hardware supports only tx flow control
 **/
static void
xemacpss_get_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	epauseparm->autoneg  = 0;
	epauseparm->rx_pause = 0;

	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCFG_OFFSET);
	epauseparm->tx_pause = regval & XEMACPSS_NWCFG_PAUSEEN_MASK;
	spin_unlock_irqrestore(&lp->lock, flags);
}

/**
 * xemacpss_set_pauseparam - set device pause parameter(flow control)
 * Usage: Issue "ethtool -A ethX tx on|off" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 * return 0 on success, negative value if not supported
 *
 * note: hardware supports only tx flow control
 **/
static int
xemacpss_set_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	if (netif_running(ndev)) {
		printk(KERN_ERR
			"%s: Please stop netif before apply configruation\n",
			ndev->name);
		return -EFAULT;
	}

	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCFG_OFFSET);

	if (epauseparm->tx_pause)
		regval |= XEMACPSS_NWCFG_PAUSEEN_MASK;
	if (!(epauseparm->tx_pause))
		regval &= ~XEMACPSS_NWCFG_PAUSEEN_MASK;

	xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET, regval);
	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

/**
 * xemacpss_get_stats - get device statistic raw data in 64bit mode
 * @ndev: network device
 **/
static struct net_device_stats
*xemacpss_get_stats(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct net_device_stats *nstat = &lp->stats;

	nstat->rx_errors +=
		(xemacpss_read(lp->baseaddr, XEMACPSS_RXUNDRCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXOVRCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXJABCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXFCSCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXLENGTHCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXSYMBCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXALIGNCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXRESERRCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXORCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXIPCCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXTCPCCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXUDPCCNT_OFFSET));
	nstat->rx_length_errors +=
		(xemacpss_read(lp->baseaddr, XEMACPSS_RXUNDRCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXOVRCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXJABCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_RXLENGTHCNT_OFFSET));
	nstat->rx_over_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_RXRESERRCNT_OFFSET);
	nstat->rx_crc_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_RXFCSCNT_OFFSET);
	nstat->rx_frame_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_RXALIGNCNT_OFFSET);
	nstat->rx_fifo_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_RXORCNT_OFFSET);
	nstat->tx_errors +=
		(xemacpss_read(lp->baseaddr, XEMACPSS_TXURUNCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_SNGLCOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_MULTICOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_EXCESSCOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_LATECOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_CSENSECNT_OFFSET));
	nstat->tx_aborted_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_EXCESSCOLLCNT_OFFSET);
	nstat->tx_carrier_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_CSENSECNT_OFFSET);
	nstat->tx_fifo_errors +=
		xemacpss_read(lp->baseaddr, XEMACPSS_TXURUNCNT_OFFSET);
	nstat->collisions +=
		(xemacpss_read(lp->baseaddr, XEMACPSS_SNGLCOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_MULTICOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_EXCESSCOLLCNT_OFFSET) +
		xemacpss_read(lp->baseaddr, XEMACPSS_LATECOLLCNT_OFFSET));
	return nstat;
}

static struct ethtool_ops xemacpss_ethtool_ops = {
	.get_settings   = xemacpss_get_settings,
	.set_settings   = xemacpss_set_settings,
	.get_drvinfo    = xemacpss_get_drvinfo,
	.get_link       = ethtool_op_get_link,       /* ethtool default */
	.get_ringparam  = xemacpss_get_ringparam,
	.get_rx_csum    = xemacpss_get_rx_csum,
	.set_rx_csum    = xemacpss_set_rx_csum,
	.get_tx_csum    = xemacpss_get_tx_csum,
	.set_tx_csum    = xemacpss_set_tx_csum,
	.get_wol        = xemacpss_get_wol,
	.set_wol        = xemacpss_set_wol,
	.get_sg         = ethtool_op_get_sg,         /* ethtool default */
	.get_tso        = ethtool_op_get_tso,        /* ethtool default */
	.get_pauseparam = xemacpss_get_pauseparam,
	.set_pauseparam = xemacpss_set_pauseparam,
};

#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP
static int xemacpss_hwtstamp_ioctl(struct net_device *netdev,
                              struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config config;
	struct net_local *lp;
	u32 regval;

	lp = netdev_priv(netdev);

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

printk(KERN_INFO "GEM: harware packet timestamp not yet implemented.\n");
printk(KERN_INFO "     cmd %d config.rx_filter %d\n", cmd, config.rx_filter); 

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		regval = xemacpss_read(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET);
		xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET,
			(regval | XEMACPSS_NWCTRL_RXTSTAMP_MASK));
		break;
	default:
		return -ERANGE;
	}

	lp->hwtstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}
#endif /* CONFIG_XILINX_PSS_EMAC_HWTSTAMP */

/**
 * xemacpss_ioctl - ioctl entry point
 * @ndev: network device
 * @rq: interface request ioctl
 * @cmd: command code
 *
 * Called when user issues an ioctl request to the network device.
 **/
static int xemacpss_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

printk(KERN_INFO "xemacpss_ioctl: cmd %d \n", cmd); 

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(phydev, if_mii(rq), cmd);
#ifdef CONFIG_XILINX_PSS_EMAC_HWTSTAMP
	case SIOCSHWTSTAMP:
		return xemacpss_hwtstamp_ioctl(ndev, rq, cmd);
#endif
	default:
		printk(KERN_INFO "GEM: ioctl %d not implemented.\n", cmd);
		return -EOPNOTSUPP;
	}

}

/**
 * xemacpss_probe - Platform driver probe
 * @pdev: Pointer to platform device structure
 *
 * Return 0 on success, negative value if error
 **/
static int __init xemacpss_probe(struct platform_device *pdev)
{
	struct eth_platform_data *pdata;
	struct resource *r_mem = NULL;
	struct resource *r_irq = NULL;
	struct net_device *ndev;
	struct net_local *lp;
	u32 regval = 0;
	int rc = -ENXIO;

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r_mem || !r_irq) {
		dev_err(&pdev->dev, "no IO resource defined.\n");
		rc = -ENXIO;
		goto err_out;
	}

	ndev = alloc_etherdev(sizeof(*lp));
	if (!ndev) {
		dev_err(&pdev->dev, "etherdev allocation failed.\n");
		rc = -ENOMEM;
		goto err_out;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	lp = netdev_priv(ndev);
	lp->pdev = pdev;
	lp->ndev = ndev;

	spin_lock_init(&lp->lock);

	lp->baseaddr = ioremap(r_mem->start, (r_mem->end - r_mem->start + 1));
	if (!lp->baseaddr) {
		dev_err(&pdev->dev, "failed to map baseaddress.\n");
		rc = -ENOMEM;
		goto err_out_free_netdev;
	}
#ifdef DEBUG
	printk(KERN_INFO "GEM: BASEADDRESS hw: %p virt: %p\n", (void*)r_mem->start, lp->baseaddr);
#endif

	ndev->irq = platform_get_irq(pdev, 0);

	rc = request_irq(ndev->irq, xemacpss_interrupt, IRQF_SAMPLE_RANDOM,
		ndev->name, ndev);
	if (rc) {
		printk(KERN_ERR "%s: Unable to request IRQ %p, error %d\n",
		ndev->name, r_irq, rc);
		goto err_out_iounmap;
	}

	ndev->netdev_ops 	 = &netdev_ops;
	ndev->watchdog_timeo     = TX_TIMEOUT;
	ndev->ethtool_ops        = &xemacpss_ethtool_ops;
	ndev->base_addr          = r_mem->start;
	ndev->features           = NETIF_F_IP_CSUM;
	netif_napi_add(ndev, &lp->napi, xemacpss_rx_poll, XEMACPSS_NAPI_WEIGHT);

	lp->ip_summed = CHECKSUM_UNNECESSARY;

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_irq;
	}

	/* Set MDIO clock divider */
	regval = (MDC_DIV_32 << XEMACPSS_NWCFG_MDC_SHIFT_MASK);
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCFG_OFFSET, regval);

	regval = XEMACPSS_NWCTRL_MDEN_MASK;
	xemacpss_write(lp->baseaddr, XEMACPSS_NWCTRL_OFFSET, regval);

	if (xemacpss_mii_init(lp) != 0) {
		printk(KERN_ERR "%s: error in xemacpss_mii_init\n", ndev->name);
		goto err_out_unregister_netdev;
	}

	xemacpss_update_hwaddr(lp);

	pdata = pdev->dev.platform_data;

	platform_set_drvdata(pdev, ndev);

	printk(KERN_INFO "%s, pdev->id %d, baseaddr 0x%08lx, irq %d\n",
		ndev->name, pdev->id, ndev->base_addr, ndev->irq);

	printk(KERN_INFO "%s, phy_addr 0x%x, phy_id 0x%08x\n",
		ndev->name, lp->phy_dev->addr, lp->phy_dev->phy_id);

	printk(KERN_INFO "%s, attach [%s] phy driver\n", ndev->name,
		lp->phy_dev->drv->name);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(ndev);
err_out_free_irq:
	free_irq(ndev->irq, ndev);
err_out_iounmap:
	iounmap(lp->baseaddr);
err_out_free_netdev:
	free_netdev(ndev);
err_out:
	platform_set_drvdata(pdev, NULL);
	return rc;
}

/**
 * xemacpss_remove - called when platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * return: 0 on success
 **/
static int __exit xemacpss_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp;

	if (ndev) {
		lp = netdev_priv(ndev);
		if (lp->phy_dev)
			phy_disconnect(lp->phy_dev);

		mdiobus_unregister(lp->mii_bus);
		kfree(lp->mii_bus->irq);
		mdiobus_free(lp->mii_bus);
		unregister_netdev(ndev);
		free_irq(ndev->irq, ndev);
		iounmap(lp->baseaddr);
		free_netdev(ndev);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

/**
 * xemacpss_suspend - Suspend event
 * @pdev: Pointer to platform device structure
 * @state: State of the device
 *
 * Return 0
 **/
static int xemacpss_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	netif_device_detach(ndev);
	return 0;
}

/**
 * xemacpss_resume - Resume after previous suspend
 * @pdev: Pointer to platform device structure
 *
 * Return 0
 **/
static int xemacpss_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	netif_device_attach(ndev);
	return 0;
}

static struct net_device_ops netdev_ops = {
	.ndo_open 		= xemacpss_open,
	.ndo_stop		= xemacpss_close,
	.ndo_start_xmit		= xemacpss_start_xmit,
	.ndo_set_multicast_list = xemacpss_set_rx_mode,
	.ndo_set_mac_address    = xemacpss_set_mac_address,
	.ndo_do_ioctl		= xemacpss_ioctl,
	.ndo_change_mtu		= xemacpss_change_mtu,
	.ndo_tx_timeout		= xemacpss_tx_timeout,
	.ndo_get_stats		= xemacpss_get_stats,
};

static struct platform_driver xemacpss_driver = {
	.probe   = xemacpss_probe,
	.remove  = __exit_p(xemacpss_remove),
	.suspend = xemacpss_suspend,
	.resume  = xemacpss_resume,
	.driver  = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

/**
 * xemacpss_init - Initial driver registration call
 *
 * Retunrs whether the driver registration was successful or not.
 **/
static int __init xemacpss_init(void)
{
    /*
     * No kernel boot options used,
     * so we just need to register the driver.
     * If we are sure the device is non-hotpluggable, call
     * platform_driver_probe(&xemacpss_driver, xemacpss_probe);
     * to remove run-once probe from memory.
     * Typical use for system-on-chip processor.
     */
	return platform_driver_probe(&xemacpss_driver, xemacpss_probe);
}

/**
 * xemacpss_exit - Driver unregistration call
 **/
static void __exit xemacpss_exit(void)
{
	platform_driver_unregister(&xemacpss_driver);
}

module_init(xemacpss_init);
module_exit(xemacpss_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(Xilinx Ethernet driver);
MODULE_LICENSE("GPL");
