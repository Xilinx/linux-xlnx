/*
 * Xilinx Ethernet: Linux driver for Ethernet.
 *
 * Author: Xilinx, Inc.
 *
 * 2010 (c) Xilinx, Inc. This file is licensed uner the terms of the GNU
 * General Public License version 2. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * This is a driver for xilinx processor sub-system (ps) ethernet device.
 * This driver is mainly used in Linux 2.6.30 and above and it does _not_
 * support Linux 2.4 kernel due to certain new features (e.g. NAPI) is
 * introduced in this driver.
 *
 * TODO:
 * 1. JUMBO frame is not enabled per EPs spec. Please update it if this
 *    support is added in and set MAX_MTU to 9000.
 * 2. For PEEP boards the Linux PHY driver state machine is not used. Hence
 *    no autonegotiation happens for PEEP. The speed of 100 Mbps is used and
 *    it is fixed. The speed cannot be changed to 10 Mbps or 1000 Mbps. However
 *    for Zynq there is no such issue and it can work at all 3 speeds after
 *    autonegotiation.
 * 3. The SLCR clock divisors are hard coded for PEEP board.
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
#include <linux/of.h>
#include <mach/slcr.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/timecompare.h>
#include <linux/net_tstamp.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>

/************************** Constant Definitions *****************************/

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME			"xemacps"
#define DRIVER_DESCRIPTION		"Xilinx Tri-Mode Ethernet MAC driver"
#define DRIVER_VERSION			"1.00a"

/* Transmission timeout is 3 seconds. */
#define TX_TIMEOUT			(3*HZ)

/* for RX skb IP header word-aligned */
#define RX_IP_ALIGN_OFFSET		2

/* DMA buffer descriptors must be aligned on a 4-byte boundary. */
#define ALIGNMENT_BD			8

/* Maximum value for hash bits. 2**6 */
#define XEMACPS_MAX_HASH_BITS		64

/* MDC clock division
 * currently supporting 8, 16, 32, 48, 64, 96, 128, 224.
 */
enum { MDC_DIV_8 = 0, MDC_DIV_16, MDC_DIV_32, MDC_DIV_48,
MDC_DIV_64, MDC_DIV_96, MDC_DIV_128, MDC_DIV_224 };

/* Specify the receive buffer size in bytes, 64, 128, 192, 10240 */
#define XEMACPS_RX_BUF_SIZE		1600

/* Number of receive buffer bytes as a unit, this is HW setup */
#define XEMACPS_RX_BUF_UNIT		64

/* Default SEND and RECV buffer descriptors (BD) numbers.
 * BD Space needed is (XEMACPS_SEND_BD_CNT+XEMACPS_RECV_BD_CNT)*8
 */
#undef  DEBUG
#define DEBUG

#define XEMACPS_SEND_BD_CNT		32
#define XEMACPS_RECV_BD_CNT		32

#define XEMACPS_NAPI_WEIGHT		64

/* Register offset definitions. Unless otherwise noted, register access is
 * 32 bit. Names are self explained here.
 */
#define XEMACPS_NWCTRL_OFFSET		0x00000000 /* Network Control reg */
#define XEMACPS_NWCFG_OFFSET		0x00000004 /* Network Config reg */
#define XEMACPS_NWSR_OFFSET		0x00000008 /* Network Status reg */
#define XEMACPS_USERIO_OFFSET		0x0000000C /* User IO reg */
#define XEMACPS_DMACR_OFFSET		0x00000010 /* DMA Control reg */
#define XEMACPS_TXSR_OFFSET		0x00000014 /* TX Status reg */
#define XEMACPS_RXQBASE_OFFSET		0x00000018 /* RX Q Base address reg */
#define XEMACPS_TXQBASE_OFFSET		0x0000001C /* TX Q Base address reg */
#define XEMACPS_RXSR_OFFSET		0x00000020 /* RX Status reg */
#define XEMACPS_ISR_OFFSET		0x00000024 /* Interrupt Status reg */
#define XEMACPS_IER_OFFSET		0x00000028 /* Interrupt Enable reg */
#define XEMACPS_IDR_OFFSET		0x0000002C /* Interrupt Disable reg */
#define XEMACPS_IMR_OFFSET		0x00000030 /* Interrupt Mask reg */
#define XEMACPS_PHYMNTNC_OFFSET		0x00000034 /* Phy Maintaince reg */
#define XEMACPS_RXPAUSE_OFFSET		0x00000038 /* RX Pause Time reg */
#define XEMACPS_TXPAUSE_OFFSET		0x0000003C /* TX Pause Time reg */
#define XEMACPS_HASHL_OFFSET		0x00000080 /* Hash Low address reg */
#define XEMACPS_HASHH_OFFSET		0x00000084 /* Hash High address reg */
#define XEMACPS_LADDR1L_OFFSET		0x00000088 /* Specific1 addr low */
#define XEMACPS_LADDR1H_OFFSET		0x0000008C /* Specific1 addr high */
#define XEMACPS_LADDR2L_OFFSET		0x00000090 /* Specific2 addr low */
#define XEMACPS_LADDR2H_OFFSET		0x00000094 /* Specific2 addr high */
#define XEMACPS_LADDR3L_OFFSET		0x00000098 /* Specific3 addr low */
#define XEMACPS_LADDR3H_OFFSET		0x0000009C /* Specific3 addr high */
#define XEMACPS_LADDR4L_OFFSET		0x000000A0 /* Specific4 addr low */
#define XEMACPS_LADDR4H_OFFSET		0x000000A4 /* Specific4 addr high */
#define XEMACPS_MATCH1_OFFSET		0x000000A8 /* Type ID1 Match reg */
#define XEMACPS_MATCH2_OFFSET		0x000000AC /* Type ID2 Match reg */
#define XEMACPS_MATCH3_OFFSET		0x000000B0 /* Type ID3 Match reg */
#define XEMACPS_MATCH4_OFFSET		0x000000B4 /* Type ID4 Match reg */
#define XEMACPS_WOL_OFFSET		0x000000B8 /* Wake on LAN reg */
#define XEMACPS_STRETCH_OFFSET		0x000000BC /* IPG Stretch reg */
#define XEMACPS_SVLAN_OFFSET		0x000000C0 /* Stacked VLAN reg */
#define XEMACPS_MODID_OFFSET		0x000000FC /* Module ID reg */
#define XEMACPS_OCTTXL_OFFSET		0x00000100 /* Octects transmitted Low
						reg */
#define XEMACPS_OCTTXH_OFFSET		0x00000104 /* Octects transmitted High
						reg */
#define XEMACPS_TXCNT_OFFSET		0x00000108 /* Error-free Frmaes
						transmitted counter */
#define XEMACPS_TXBCCNT_OFFSET		0x0000010C /* Error-free Broadcast
						Frames counter*/
#define XEMACPS_TXMCCNT_OFFSET		0x00000110 /* Error-free Multicast
						Frame counter */
#define XEMACPS_TXPAUSECNT_OFFSET	0x00000114 /* Pause Frames Transmitted
						Counter */
#define XEMACPS_TX64CNT_OFFSET		0x00000118 /* Error-free 64 byte Frames
						Transmitted counter */
#define XEMACPS_TX65CNT_OFFSET		0x0000011C /* Error-free 65-127 byte
						Frames Transmitted counter */
#define XEMACPS_TX128CNT_OFFSET		0x00000120 /* Error-free 128-255 byte
						Frames Transmitted counter */
#define XEMACPS_TX256CNT_OFFSET		0x00000124 /* Error-free 256-511 byte
						Frames transmitted counter */
#define XEMACPS_TX512CNT_OFFSET		0x00000128 /* Error-free 512-1023 byte
						Frames transmitted counter */
#define XEMACPS_TX1024CNT_OFFSET	0x0000012C /* Error-free 1024-1518 byte
						Frames transmitted counter */
#define XEMACPS_TX1519CNT_OFFSET	0x00000130 /* Error-free larger than
						1519 byte Frames transmitted
						Counter */
#define XEMACPS_TXURUNCNT_OFFSET	0x00000134 /* TX under run error
						Counter */
#define XEMACPS_SNGLCOLLCNT_OFFSET	0x00000138 /* Single Collision Frame
						Counter */
#define XEMACPS_MULTICOLLCNT_OFFSET	0x0000013C /* Multiple Collision Frame
						Counter */
#define XEMACPS_EXCESSCOLLCNT_OFFSET	0x00000140 /* Excessive Collision Frame
						Counter */
#define XEMACPS_LATECOLLCNT_OFFSET	0x00000144 /* Late Collision Frame
						Counter */
#define XEMACPS_TXDEFERCNT_OFFSET	0x00000148 /* Deferred Transmission
						Frame Counter */
#define XEMACPS_CSENSECNT_OFFSET	0x0000014C /* Carrier Sense Error
						Counter */
#define XEMACPS_OCTRXL_OFFSET		0x00000150 /* Octects Received register
						Low */
#define XEMACPS_OCTRXH_OFFSET		0x00000154 /* Octects Received register
						High */
#define XEMACPS_RXCNT_OFFSET		0x00000158 /* Error-free Frames
						Received Counter */
#define XEMACPS_RXBROADCNT_OFFSET	0x0000015C /* Error-free Broadcast
						Frames Received Counter */
#define XEMACPS_RXMULTICNT_OFFSET	0x00000160 /* Error-free Multicast
						Frames Received Counter */
#define XEMACPS_RXPAUSECNT_OFFSET	0x00000164 /* Pause Frames
						Received Counter */
#define XEMACPS_RX64CNT_OFFSET		0x00000168 /* Error-free 64 byte Frames
						Received Counter */
#define XEMACPS_RX65CNT_OFFSET		0x0000016C /* Error-free 65-127 byte
						Frames Received Counter */
#define XEMACPS_RX128CNT_OFFSET		0x00000170 /* Error-free 128-255 byte
						Frames Received Counter */
#define XEMACPS_RX256CNT_OFFSET		0x00000174 /* Error-free 256-512 byte
						Frames Received Counter */
#define XEMACPS_RX512CNT_OFFSET		0x00000178 /* Error-free 512-1023 byte
						Frames Received Counter */
#define XEMACPS_RX1024CNT_OFFSET	0x0000017C /* Error-free 1024-1518 byte
						Frames Received Counter */
#define XEMACPS_RX1519CNT_OFFSET	0x00000180 /* Error-free 1519-max byte
						Frames Received Counter */
#define XEMACPS_RXUNDRCNT_OFFSET	0x00000184 /* Undersize Frames Received
						Counter */
#define XEMACPS_RXOVRCNT_OFFSET		0x00000188 /* Oversize Frames Received
						Counter */
#define XEMACPS_RXJABCNT_OFFSET		0x0000018C /* Jabbers Received
						Counter */
#define XEMACPS_RXFCSCNT_OFFSET		0x00000190 /* Frame Check Sequence
						Error Counter */
#define XEMACPS_RXLENGTHCNT_OFFSET	0x00000194 /* Length Field Error
						Counter */
#define XEMACPS_RXSYMBCNT_OFFSET	0x00000198 /* Symbol Error Counter */
#define XEMACPS_RXALIGNCNT_OFFSET	0x0000019C /* Alignment Error
						Counter */
#define XEMACPS_RXRESERRCNT_OFFSET	0x000001A0 /* Receive Resource Error
						Counter */
#define XEMACPS_RXORCNT_OFFSET		0x000001A4 /* Receive Overrun */
#define XEMACPS_RXIPCCNT_OFFSET		0x000001A8 /* IP header Checksum Error
						Counter */
#define XEMACPS_RXTCPCCNT_OFFSET	0x000001AC /* TCP Checksum Error
						Counter */
#define XEMACPS_RXUDPCCNT_OFFSET	0x000001B0 /* UDP Checksum Error
						Counter */

#define XEMACPS_1588S_OFFSET		0x000001D0 /* 1588 Timer Seconds */
#define XEMACPS_1588NS_OFFSET		0x000001D4 /* 1588 Timer Nanoseconds */
#define XEMACPS_1588ADJ_OFFSET		0x000001D8 /* 1588 Timer Adjust */
#define XEMACPS_1588INC_OFFSET		0x000001DC /* 1588 Timer Increment */
#define XEMACPS_PTPETXS_OFFSET		0x000001E0 /* PTP Event Frame
						Transmitted Seconds */
#define XEMACPS_PTPETXNS_OFFSET		0x000001E4 /* PTP Event Frame
						Transmitted Nanoseconds */
#define XEMACPS_PTPERXS_OFFSET		0x000001E8 /* PTP Event Frame Received
						Seconds */
#define XEMACPS_PTPERXNS_OFFSET		0x000001EC /* PTP Event Frame Received
						Nanoseconds */
#define XEMACPS_PTPPTXS_OFFSET		0x000001E0 /* PTP Peer Frame
						Transmitted Seconds */
#define XEMACPS_PTPPTXNS_OFFSET		0x000001E4 /* PTP Peer Frame
						Transmitted Nanoseconds */
#define XEMACPS_PTPPRXS_OFFSET		0x000001E8 /* PTP Peer Frame Received
						Seconds */
#define XEMACPS_PTPPRXNS_OFFSET		0x000001EC /* PTP Peer Frame Received
						Nanoseconds */

/* network control register bit definitions */
#define XEMACPS_NWCTRL_RXTSTAMP_MASK	0x00008000 /* RX Timestamp in CRC */
#define XEMACPS_NWCTRL_ZEROPAUSETX_MASK	0x00001000 /* Transmit zero quantum
						pause frame */
#define XEMACPS_NWCTRL_PAUSETX_MASK	0x00000800 /* Transmit pause frame */
#define XEMACPS_NWCTRL_HALTTX_MASK	0x00000400 /* Halt transmission
						after current frame */
#define XEMACPS_NWCTRL_STARTTX_MASK	0x00000200 /* Start tx (tx_go) */

#define XEMACPS_NWCTRL_STATWEN_MASK	0x00000080 /* Enable writing to
						stat counters */
#define XEMACPS_NWCTRL_STATINC_MASK	0x00000040 /* Increment statistic
						registers */
#define XEMACPS_NWCTRL_STATCLR_MASK	0x00000020 /* Clear statistic
						registers */
#define XEMACPS_NWCTRL_MDEN_MASK	0x00000010 /* Enable MDIO port */
#define XEMACPS_NWCTRL_TXEN_MASK	0x00000008 /* Enable transmit */
#define XEMACPS_NWCTRL_RXEN_MASK	0x00000004 /* Enable receive */
#define XEMACPS_NWCTRL_LOOPEN_MASK	0x00000002 /* local loopback */

/* name network configuration register bit definitions */
#define XEMACPS_NWCFG_BADPREAMBEN_MASK	0x20000000 /* disable rejection of
						non-standard preamble */
#define XEMACPS_NWCFG_IPDSTRETCH_MASK	0x10000000 /* enable transmit IPG */
#define XEMACPS_NWCFG_FCSIGNORE_MASK	0x04000000 /* disable rejection of
						FCS error */
#define XEMACPS_NWCFG_HDRXEN_MASK	0x02000000 /* RX half duplex */
#define XEMACPS_NWCFG_RXCHKSUMEN_MASK	0x01000000 /* enable RX checksum
						offload */
#define XEMACPS_NWCFG_PAUSECOPYDI_MASK	0x00800000 /* Do not copy pause
						Frames to memory */
#define XEMACPS_NWCFG_MDC_SHIFT_MASK	18 /* shift bits for MDC */
#define XEMACPS_NWCFG_MDCCLKDIV_MASK	0x001C0000 /* MDC Mask PCLK divisor */
#define XEMACPS_NWCFG_FCSREM_MASK	0x00020000 /* Discard FCS from
						received frames */
#define XEMACPS_NWCFG_LENGTHERRDSCRD_MASK 0x00010000
/* RX length error discard */
#define XEMACPS_NWCFG_RXOFFS_MASK	0x0000C000 /* RX buffer offset */
#define XEMACPS_NWCFG_PAUSEEN_MASK	0x00002000 /* Enable pause TX */
#define XEMACPS_NWCFG_RETRYTESTEN_MASK	0x00001000 /* Retry test */
#define XEMACPS_NWCFG_1000_MASK		0x00000400 /* Gigbit mode */
#define XEMACPS_NWCFG_EXTADDRMATCHEN_MASK	0x00000200
/* External address match enable */
#define XEMACPS_NWCFG_UCASTHASHEN_MASK	0x00000080 /* Receive unicast hash
						frames */
#define XEMACPS_NWCFG_MCASTHASHEN_MASK	0x00000040 /* Receive multicast hash
						frames */
#define XEMACPS_NWCFG_BCASTDI_MASK	0x00000020 /* Do not receive
						broadcast frames */
#define XEMACPS_NWCFG_COPYALLEN_MASK	0x00000010 /* Copy all frames */

#define XEMACPS_NWCFG_NVLANDISC_MASK	0x00000004 /* Receive only VLAN
						frames */
#define XEMACPS_NWCFG_FDEN_MASK		0x00000002 /* Full duplex */
#define XEMACPS_NWCFG_100_MASK		0x00000001 /* 10 or 100 Mbs */

/* network status register bit definitaions */
#define XEMACPS_NWSR_MDIOIDLE_MASK	0x00000004 /* PHY management idle */
#define XEMACPS_NWSR_MDIO_MASK		0x00000002 /* Status of mdio_in */

/* MAC address register word 1 mask */
#define XEMACPS_LADDR_MACH_MASK		0x0000FFFF /* Address bits[47:32]
						bit[31:0] are in BOTTOM */

/* DMA control register bit definitions */
#define XEMACPS_DMACR_RXBUF_MASK	0x00FF0000 /* Mask bit for RX buffer
						size */
#define XEMACPS_DMACR_RXBUF_SHIFT	16 /* Shift bit for RX buffer
						size */
#define XEMACPS_DMACR_TCPCKSUM_MASK	0x00000800 /* enable/disable TX
						checksum offload */
#define XEMACPS_DMACR_TXSIZE_MASK	0x00000400 /* TX buffer memory size */
#define XEMACPS_DMACR_RXSIZE_MASK	0x00000300 /* RX buffer memory size */
#define XEMACPS_DMACR_ENDIAN_MASK	0x00000080 /* Endian configuration */
#define XEMACPS_DMACR_BLENGTH_MASK	0x0000001F /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR16	0x00000010 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR8	0x00000008 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_INCR4	0x00000004 /* Buffer burst length */
#define XEMACPS_DMACR_BLENGTH_SINGLE	0x00000002 /* Buffer burst length */

/* transmit status register bit definitions */
#define XEMACPS_TXSR_HRESPNOK_MASK	0x00000100 /* Transmit hresp not OK */
#define XEMACPS_TXSR_COL1000_MASK	0x00000080 /* Collision Gbs mode */
#define XEMACPS_TXSR_URUN_MASK		0x00000040 /* Transmit underrun */
#define XEMACPS_TXSR_TXCOMPL_MASK	0x00000020 /* Transmit completed OK */
#define XEMACPS_TXSR_BUFEXH_MASK	0x00000010 /* Transmit buffs exhausted
						mid frame */
#define XEMACPS_TXSR_TXGO_MASK		0x00000008 /* Status of go flag */
#define XEMACPS_TXSR_RXOVR_MASK		0x00000004 /* Retry limit exceeded */
#define XEMACPS_TXSR_COL100_MASK	0x00000002 /* Collision 10/100  mode */
#define XEMACPS_TXSR_USEDREAD_MASK	0x00000001 /* TX buffer used bit set */

#define XEMACPS_TXSR_ERROR_MASK	(XEMACPS_TXSR_HRESPNOK_MASK |		\
					XEMACPS_TXSR_COL1000_MASK |	\
					XEMACPS_TXSR_URUN_MASK |	\
					XEMACPS_TXSR_BUFEXH_MASK |	\
					XEMACPS_TXSR_RXOVR_MASK |	\
					XEMACPS_TXSR_COL100_MASK |	\
					XEMACPS_TXSR_USEDREAD_MASK)

/* receive status register bit definitions */
#define XEMACPS_RXSR_HRESPNOK_MASK	0x00000008 /* Receive hresp not OK */
#define XEMACPS_RXSR_RXOVR_MASK		0x00000004 /* Receive overrun */
#define XEMACPS_RXSR_FRAMERX_MASK	0x00000002 /* Frame received OK */
#define XEMACPS_RXSR_BUFFNA_MASK	0x00000001 /* RX buffer used bit set */

#define XEMACPS_RXSR_ERROR_MASK	(XEMACPS_RXSR_HRESPNOK_MASK | \
					XEMACPS_RXSR_RXOVR_MASK | \
					XEMACPS_RXSR_BUFFNA_MASK)

/* interrupts bit definitions
 * Bits definitions are same in XEMACPS_ISR_OFFSET,
 * XEMACPS_IER_OFFSET, XEMACPS_IDR_OFFSET, and XEMACPS_IMR_OFFSET
 */
#define XEMACPS_IXR_PTPPSTX_MASK	0x02000000 /* PTP Psync transmitted */
#define XEMACPS_IXR_PTPPDRTX_MASK	0x01000000 /* PTP Pdelay_req
							transmitted */
#define XEMACPS_IXR_PTPSTX_MASK		0x00800000 /* PTP Sync transmitted */
#define XEMACPS_IXR_PTPDRTX_MASK	0x00400000 /* PTP Delay_req
							transmitted */
#define XEMACPS_IXR_PTPPSRX_MASK	0x00200000 /* PTP Psync received */
#define XEMACPS_IXR_PTPPDRRX_MASK	0x00100000 /* PTP Pdelay_req
							received */
#define XEMACPS_IXR_PTPSRX_MASK		0x00080000 /* PTP Sync received */
#define XEMACPS_IXR_PTPDRRX_MASK	0x00040000 /* PTP Delay_req received */
#define XEMACPS_IXR_PAUSETX_MASK	0x00004000 /* Pause frame
							transmitted */
#define XEMACPS_IXR_PAUSEZERO_MASK	0x00002000 /* Pause time has reached
							zero */
#define XEMACPS_IXR_PAUSENZERO_MASK	0x00001000 /* Pause frame received */
#define XEMACPS_IXR_HRESPNOK_MASK	0x00000800 /* hresp not ok */
#define XEMACPS_IXR_RXOVR_MASK		0x00000400 /* Receive overrun
							occurred */
#define XEMACPS_IXR_TXCOMPL_MASK	0x00000080 /* Frame transmitted ok */
#define XEMACPS_IXR_TXEXH_MASK		0x00000040 /* Transmit err occurred or
							no buffers*/
#define XEMACPS_IXR_RETRY_MASK		0x00000020 /* Retry limit exceeded */
#define XEMACPS_IXR_URUN_MASK		0x00000010 /* Transmit underrun */
#define XEMACPS_IXR_TXUSED_MASK		0x00000008 /* Tx buffer used bit read */
#define XEMACPS_IXR_RXUSED_MASK		0x00000004 /* Rx buffer used bit read */
#define XEMACPS_IXR_FRAMERX_MASK	0x00000002 /* Frame received ok */
#define XEMACPS_IXR_MGMNT_MASK		0x00000001 /* PHY management complete */
#define XEMACPS_IXR_ALL_MASK		0x03FC7FFF /* Everything! */

#define XEMACPS_IXR_TX_ERR_MASK	(XEMACPS_IXR_TXEXH_MASK |		\
					XEMACPS_IXR_RETRY_MASK |	\
					XEMACPS_IXR_URUN_MASK |		\
					XEMACPS_IXR_TXUSED_MASK)

#define XEMACPS_IXR_RX_ERR_MASK	(XEMACPS_IXR_HRESPNOK_MASK |		\
					XEMACPS_IXR_RXUSED_MASK |	\
					XEMACPS_IXR_RXOVR_MASK)
/* PHY Maintenance bit definitions */
#define XEMACPS_PHYMNTNC_OP_MASK	0x40020000 /* operation mask bits */
#define XEMACPS_PHYMNTNC_OP_R_MASK	0x20000000 /* read operation */
#define XEMACPS_PHYMNTNC_OP_W_MASK	0x10000000 /* write operation */
#define XEMACPS_PHYMNTNC_ADDR_MASK	0x0F800000 /* Address bits */
#define XEMACPS_PHYMNTNC_REG_MASK	0x007C0000 /* register bits */
#define XEMACPS_PHYMNTNC_DATA_MASK	0x0000FFFF /* data bits */
#define XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK	23 /* Shift bits for PHYAD */
#define XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK	18 /* Shift bits for PHREG */

/* Wake on LAN bit definition */
#define XEMACPS_WOL_MCAST_MASK		0x00080000
#define XEMACPS_WOL_SPEREG1_MASK	0x00040000
#define XEMACPS_WOL_ARP_MASK		0x00020000
#define XEMACPS_WOL_MAGIC_MASK		0x00010000
#define XEMACPS_WOL_ARP_ADDR_MASK	0x0000FFFF

/* Buffer descriptor status words offset */
#define XEMACPS_BD_ADDR_OFFSET		0x00000000 /**< word 0/addr of BDs */
#define XEMACPS_BD_STAT_OFFSET		0x00000004 /**< word 1/status of BDs */

/* Transmit buffer descriptor status words bit positions.
 * Transmit buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit address pointing to the location of
 * the transmit data.
 * The following register - word1, consists of various information to
 * control transmit process.  After transmit, this is updated with status
 * information, whether the frame was transmitted OK or why it had failed.
 */
#define XEMACPS_TXBUF_USED_MASK		0x80000000 /* Used bit. */
#define XEMACPS_TXBUF_WRAP_MASK		0x40000000 /* Wrap bit, last
							descriptor */
#define XEMACPS_TXBUF_RETRY_MASK	0x20000000 /* Retry limit exceeded */
#define XEMACPS_TXBUF_EXH_MASK		0x08000000 /* Buffers exhausted */
#define XEMACPS_TXBUF_LAC_MASK		0x04000000 /* Late collision. */
#define XEMACPS_TXBUF_NOCRC_MASK	0x00010000 /* No CRC */
#define XEMACPS_TXBUF_LAST_MASK		0x00008000 /* Last buffer */
#define XEMACPS_TXBUF_LEN_MASK		0x00003FFF /* Mask for length field */

#define XEMACPS_TXBUF_ERR_MASK		0x3C000000 /* Mask for length field */

/* Receive buffer descriptor status words bit positions.
 * Receive buffer descriptor consists of two 32-bit registers,
 * the first - word0 contains a 32-bit word aligned address pointing to the
 * address of the buffer. The lower two bits make up the wrap bit indicating
 * the last descriptor and the ownership bit to indicate it has been used.
 * The following register - word1, contains status information regarding why
 * the frame was received (the filter match condition) as well as other
 * useful info.
 */
#define XEMACPS_RXBUF_BCAST_MASK	0x80000000 /* Broadcast frame */
#define XEMACPS_RXBUF_MULTIHASH_MASK	0x40000000 /* Multicast hashed frame */
#define XEMACPS_RXBUF_UNIHASH_MASK	0x20000000 /* Unicast hashed frame */
#define XEMACPS_RXBUF_EXH_MASK		0x08000000 /* buffer exhausted */
#define XEMACPS_RXBUF_AMATCH_MASK	0x06000000 /* Specific address
						matched */
#define XEMACPS_RXBUF_IDFOUND_MASK	0x01000000 /* Type ID matched */
#define XEMACPS_RXBUF_IDMATCH_MASK	0x00C00000 /* ID matched mask */
#define XEMACPS_RXBUF_VLAN_MASK		0x00200000 /* VLAN tagged */
#define XEMACPS_RXBUF_PRI_MASK		0x00100000 /* Priority tagged */
#define XEMACPS_RXBUF_VPRI_MASK		0x000E0000 /* Vlan priority */
#define XEMACPS_RXBUF_CFI_MASK		0x00010000 /* CFI frame */
#define XEMACPS_RXBUF_EOF_MASK		0x00008000 /* End of frame. */
#define XEMACPS_RXBUF_SOF_MASK		0x00004000 /* Start of frame. */
#define XEMACPS_RXBUF_LEN_MASK		0x00003FFF /* Mask for length field */

#define XEMACPS_RXBUF_WRAP_MASK		0x00000002 /* Wrap bit, last BD */
#define XEMACPS_RXBUF_NEW_MASK		0x00000001 /* Used bit.. */
#define XEMACPS_RXBUF_ADD_MASK		0xFFFFFFFC /* Mask for address */


#define XSLCR_EMAC0_CLK_CTRL_OFFSET	0x140 /* EMAC0 Reference Clk Control */
#define XSLCR_EMAC1_CLK_CTRL_OFFSET	0x144 /* EMAC1 Reference Clk Control */
#define BOARD_TYPE_ZYNQ			0x01
#define BOARD_TYPE_PEEP			0x02

#define XEMACPS_DFLT_SLCR_DIV0_1000	8
#define XEMACPS_DFLT_SLCR_DIV1_1000	1
#define XEMACPS_DFLT_SLCR_DIV0_100	8
#define XEMACPS_DFLT_SLCR_DIV1_100	5
#define XEMACPS_DFLT_SLCR_DIV0_10	8
#define XEMACPS_DFLT_SLCR_DIV1_10	50
#define XEMACPS_SLCR_DIV_MASK		0xFC0FC0FF

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
#define NS_PER_SEC			1000000000ULL /* Nanoseconds per
							second */
#define PEEP_TSU_CLK			50000000ULL /* PTP TSU CLOCK */
#endif

#define xemacps_read(base, reg)						\
	__raw_readl((u32)(base) + (u32)(reg))
#define xemacps_write(base, reg, val)					\
	__raw_writel((val), (u32)(base) + (u32)(reg))

#define XEMACPS_RING_SEEKAHEAD(ringptr, bdptr, numbd)			\
{									\
	u32 addr = (u32)bdptr;						\
	addr += ((ringptr)->separation * numbd);			\
	if ((addr > (ringptr)->lastbdaddr) || ((u32)bdptr > addr)) {	\
		addr -= (ringptr)->length;				\
	}								\
	bdptr = (struct xemacps_bd *)addr;				\
}

#define XEMACPS_RING_SEEKBACK(ringptr, bdptr, numbd)			\
{									\
	u32 addr = (u32)bdptr;						\
	addr -= ((ringptr)->separation * numbd);			\
	if ((addr < (ringptr)->firstbdaddr) || ((u32)bdptr < addr)) {	\
		addr += (ringptr)->length;				\
	}								\
	bdptr = (struct xemacps_bd *)addr;				\
}

#define XEMACPS_BDRING_NEXT(ringptr, bdptr)				\
	(((u32)(bdptr) >= (ringptr)->lastbdaddr) ?			\
	(struct xemacps_bd *)(ringptr)->firstbdaddr :			\
	(struct xemacps_bd *)((u32)(bdptr) + (ringptr)->separation))

#define XEMACPS_BDRING_PREV(ringptr, bdptr)				\
	(((u32)(bdptr) <= (ringptr)->firstbdaddr) ?			\
	(struct xemacps_bd *)(ringptr)->lastbdaddr :			\
	(struct xemacps_bd *)((u32)(bdptr) - (ringptr)->separation))

#define XEMACPS_SET_BUFADDR_RX(bdptr, addr)				\
	xemacps_write((bdptr), XEMACPS_BD_ADDR_OFFSET,		\
	((xemacps_read((bdptr), XEMACPS_BD_ADDR_OFFSET) &		\
	~XEMACPS_RXBUF_ADD_MASK) | (u32)(addr)))

#define XEMACPS_BD_TO_INDEX(ringptr, bdptr)				\
	(((u32)bdptr - (u32)(ringptr)->firstbdaddr) / (ringptr)->separation)

struct ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};

/* DMA buffer descriptor structure. Each BD is two words */
struct xemacps_bd {
	u32 addr;
	u32 ctrl;
};

/* This is an internal structure used to maintain the DMA list */
struct xemacps_bdring {
	u32 physbaseaddr; /* Physical address of 1st BD in list */
	u32 firstbdaddr; /* Virtual address of 1st BD in list */
	u32 lastbdaddr; /* Virtual address of last BD in the list */
	u32 length; /* size of ring in bytes */
	u32 separation; /* Number of bytes between the starting
				address of adjacent BDs */
	struct xemacps_bd *freehead; /* First BD in the free group */
	struct xemacps_bd *prehead; /* First BD in the pre-work group */
	struct xemacps_bd *hwhead; /* First BD in the work group */
	struct xemacps_bd *hwtail; /* Last BD in the work group */
	struct xemacps_bd *posthead; /* First BD in the post-work group */
	unsigned freecnt; /* Number of BDs in the free group */
	unsigned hwcnt; /* Number of BDs in work group */
	unsigned precnt; /* Number of BDs in pre-work group */
	unsigned postcnt; /* Number of BDs in post-work group */
	unsigned allcnt; /* Total Number of BDs for channel */

	int is_rx; /* Is this an RX or a TX ring? */
};

/* Our private device data. */
struct net_local {
	void __iomem *baseaddr;
	struct clk *devclk;
	struct clk *aperclk;
	struct notifier_block clk_rate_change_nb;

	struct xemacps_bdring tx_ring;
	struct xemacps_bdring rx_ring;
	struct device_node *phy_node;
	struct ring_info *tx_skb;
	struct ring_info *rx_skb;

	void *rx_bd; /* virtual address */
	void *tx_bd; /* virtual address */

	dma_addr_t rx_bd_dma; /* physical address */
	dma_addr_t tx_bd_dma; /* physical address */

	spinlock_t lock;

	struct platform_device *pdev;
	struct net_device *ndev; /* this device */

	struct napi_struct napi; /* napi information for device */
	struct net_device_stats stats; /* Statistics for this device */

	/* Manage internal timer for packet timestamping */
	struct cyclecounter cycles;
	struct timecounter clock;
	struct timecompare compare;
	struct hwtstamp_config hwtstamp_config;

	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;
	unsigned int link;
	unsigned int speed;
	unsigned int duplex;
	/* RX ip/tcp/udp checksum */
	unsigned ip_summed;
	unsigned int enetnum;
	unsigned int board_type;
#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	unsigned int ptpenetclk;
#endif
};
#define to_net_local(_nb)	container_of(_nb, struct net_local,\
		clk_rate_change_nb)

static struct net_device_ops netdev_ops;

/**
 * xemacps_mdio_read - Read current value of phy register indicated by
 * phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be read
 *
 * @return: value read from specified phy register.
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacps_mdio_read(struct mii_bus *bus, int mii_id, int phyreg)
{
	struct net_local *lp = bus->priv;
	u32 regval;
	int value;
	volatile u32 ipisr;

	regval  = XEMACPS_PHYMNTNC_OP_MASK;
	regval |= XEMACPS_PHYMNTNC_OP_R_MASK;
	regval |= (mii_id << XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK);

	xemacps_write(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	do {
		cpu_relax();
		ipisr = xemacps_read(lp->baseaddr, XEMACPS_NWSR_OFFSET);
	} while ((ipisr & XEMACPS_NWSR_MDIOIDLE_MASK) == 0);

	value = xemacps_read(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET) &
			XEMACPS_PHYMNTNC_DATA_MASK;

	return value;
}

/**
 * xemacps_mdio_write - Write passed in value to phy register indicated
 * by phyreg.
 * @bus: mdio bus
 * @mii_id: mii id
 * @phyreg: phy register to be configured.
 * @value: value to be written to phy register.
 * return 0. This API requires to be int type or compile warning generated
 *
 * note: This is for 802.3 clause 22 phys access. For 802.3 clause 45 phys
 * access, set bit 30 to be 1. e.g. change XEMACPS_PHYMNTNC_OP_MASK to
 * 0x00020000.
 */
static int xemacps_mdio_write(struct mii_bus *bus, int mii_id, int phyreg,
	u16 value)
{
	struct net_local *lp = bus->priv;
	u32 regval;
	volatile u32 ipisr;

	regval  = XEMACPS_PHYMNTNC_OP_MASK;
	regval |= XEMACPS_PHYMNTNC_OP_W_MASK;
	regval |= (mii_id << XEMACPS_PHYMNTNC_PHYAD_SHIFT_MASK);
	regval |= (phyreg << XEMACPS_PHYMNTNC_PHREG_SHIFT_MASK);
	regval |= value;

	xemacps_write(lp->baseaddr, XEMACPS_PHYMNTNC_OFFSET, regval);

	/* wait for end of transfer */
	do {
		cpu_relax();
		ipisr = xemacps_read(lp->baseaddr, XEMACPS_NWSR_OFFSET);
	} while ((ipisr & XEMACPS_NWSR_MDIOIDLE_MASK) == 0);

	return 0;
}


/**
 * xemacps_mdio_reset - mdio reset. It seems to be required per open
 * source documentation phy.txt. But there is no reset in this device.
 * Provide function API for now.
 * @bus: mdio bus
 **/
static int xemacps_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

static void xemacps_phy_init(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u16 regval;
	int i = 0;

	/* set RX delay */
	regval = xemacps_mdio_read(lp->mii_bus, lp->phy_dev->addr, 20);
	/* 0x0080 for 100Mbps, 0x0060 for 1Gbps. */
	regval |= 0x0080;
	xemacps_mdio_write(lp->mii_bus, lp->phy_dev->addr, 20, regval);

	/* 0x2100 for 100Mbps, 0x0140 for 1Gbps. */
	xemacps_mdio_write(lp->mii_bus, lp->phy_dev->addr, 0, 0x2100);

	regval = xemacps_mdio_read(lp->mii_bus, lp->phy_dev->addr, 0);
	regval |= 0x8000;
	xemacps_mdio_write(lp->mii_bus, lp->phy_dev->addr, 0, regval);
	for (i = 0; i < 10; i++)
		mdelay(500);
#ifdef DEBUG_VERBOSE
	dev_dbg(&lp->pdev->dev,
			"phy register dump, start from 0, four in a row.");
	for (i = 0; i <= 30; i++) {
		if (!(i%4))
			dev_dbg(&lp->pdev->dev, "\n %02d:  ", i);
		regval = xemacps_mdio_read(lp->mii_bus, lp->phy_dev->addr, i);
		dev_dbg(&lp->pdev->dev, " 0x%08x", regval);
	}
	dev_dbg(&lp->pdev->dev, "\n");
#endif
}


/**
 * xemacps_adjust_link - handles link status changes, such as speed,
 * duplex, up/down, ...
 * @ndev: network device
 */
static void xemacps_adjust_link(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;
	int status_change = 0;
	u32 regval;
	long rate;

	if (phydev->link) {
		if ((lp->speed != phydev->speed) ||
			(lp->duplex != phydev->duplex)) {
			regval = xemacps_read(lp->baseaddr,
				XEMACPS_NWCFG_OFFSET);
			if (phydev->duplex)
				regval |= XEMACPS_NWCFG_FDEN_MASK;
			else
				regval &= ~XEMACPS_NWCFG_FDEN_MASK;

			if (phydev->speed == SPEED_1000) {
				regval |= XEMACPS_NWCFG_1000_MASK;
				rate = clk_round_rate(lp->devclk, 125000000);
				dev_info(&lp->pdev->dev, "Set clk to %ld Hz\n",
						rate);
				if (clk_set_rate(lp->devclk, rate))
					dev_err(&lp->pdev->dev,
					"Setting new clock rate failed.\n");
			} else {
				regval &= ~XEMACPS_NWCFG_1000_MASK;
			}

			if (phydev->speed == SPEED_100) {
				regval |= XEMACPS_NWCFG_100_MASK;
				rate = clk_round_rate(lp->devclk, 25000000);
				dev_info(&lp->pdev->dev, "Set clk to %ld Hz\n",
						rate);
				if (clk_set_rate(lp->devclk, rate))
					dev_err(&lp->pdev->dev,
					"Setting new clock rate failed.\n");
			} else {
				regval &= ~XEMACPS_NWCFG_100_MASK;
			}

			if (phydev->speed == SPEED_10) {
				rate = clk_round_rate(lp->devclk, 2500000);
				dev_info(&lp->pdev->dev, "Set clk to %ld Hz\n",
						rate);
				if (clk_set_rate(lp->devclk, rate))
					dev_err(&lp->pdev->dev,
					"Setting new clock rate failed.\n");
			}

			xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET,
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

	if (status_change) {
		if (phydev->link)
			dev_info(&lp->pdev->dev, "link up (%d/%s)\n",
				phydev->speed,
				DUPLEX_FULL == phydev->duplex ?
				"FULL" : "HALF");
		else
			dev_info(&lp->pdev->dev, "link down\n");
	}
}

static int xemacps_clk_notifier_cb(struct notifier_block *nb, unsigned long
		event, void *data)
{
/*
	struct clk_notifier_data *ndata = data;
	struct net_local *nl = to_net_local(nb);
*/

	switch (event) {
	case PRE_RATE_CHANGE:
		/* if a rate change is announced we need to check whether we can
		 * maintain the current frequency by changing the clock
		 * dividers.
		 * I don't see how this can be done using the current fmwk!?
		 * For now we always allow the rate change. Otherwise we would
		 * even prevent ourself to change the rate.
		 */
		return NOTIFY_OK;
	case POST_RATE_CHANGE:
		/* not sure this will work. actually i'm sure it does not. this
		 * callback is not allowed to call back into COMMON_CLK, what
		 * adjust_link() does...*/
		/*xemacps_adjust_link(nl->ndev); would likely lock up kernel */
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

/**
 * xemacps_mii_probe - probe mii bus, find the right bus_id to register
 * phy callback function.
 * @ndev: network interface device structure
 * return 0 on success, negative value if error
 **/
static int xemacps_mii_probe(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = NULL;

	if (lp->phy_node) {
		phydev = of_phy_connect(lp->ndev,
					lp->phy_node,
					&xemacps_adjust_link,
					0,
					PHY_INTERFACE_MODE_RGMII_ID);
	}
	if (!phydev) {
		dev_err(&lp->pdev->dev, "%s: no PHY found\n", ndev->name);
		return -1;
	}

	dev_dbg(&lp->pdev->dev,
		"GEM: phydev %p, phydev->phy_id 0x%x, phydev->addr 0x%x\n",
		phydev, phydev->phy_id, phydev->addr);

	phydev->supported &= (PHY_GBIT_FEATURES | SUPPORTED_Pause |
							SUPPORTED_Asym_Pause);
	phydev->advertising = phydev->supported;

	lp->link    = 0;
	lp->speed   = 0;
	lp->duplex  = -1;
	lp->phy_dev = phydev;

	if (lp->board_type == BOARD_TYPE_ZYNQ)
		phy_start(lp->phy_dev);
	else
		xemacps_phy_init(lp->ndev);

	dev_dbg(&lp->pdev->dev, "phy_addr 0x%x, phy_id 0x%08x\n",
			lp->phy_dev->addr, lp->phy_dev->phy_id);

	dev_dbg(&lp->pdev->dev, "attach [%s] phy driver\n",
			lp->phy_dev->drv->name);

	return 0;
}

/**
 * xemacps_mii_init - Initialize and register mii bus to network device
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacps_mii_init(struct net_local *lp)
{
	int rc = -ENXIO, i;
	struct resource res;
	struct device_node *np = of_get_parent(lp->phy_node);
	struct device_node *npp;

	lp->mii_bus = mdiobus_alloc();
	if (lp->mii_bus == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	lp->mii_bus->name  = "XEMACPS mii bus";
	lp->mii_bus->read  = &xemacps_mdio_read;
	lp->mii_bus->write = &xemacps_mdio_write;
	lp->mii_bus->reset = &xemacps_mdio_reset;
	lp->mii_bus->priv = lp;
	lp->mii_bus->parent = &lp->ndev->dev;

	lp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!lp->mii_bus->irq) {
		rc = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		lp->mii_bus->irq[i] = PHY_POLL;
	npp = of_get_parent(np);
	of_address_to_resource(npp, 0, &res);
	snprintf(lp->mii_bus->id, MII_BUS_ID_SIZE, "%.8llx",
		 (unsigned long long)res.start);
	if (of_mdiobus_register(lp->mii_bus, np))
		goto err_out_free_mdio_irq;

	return 0;

err_out_free_mdio_irq:
	kfree(lp->mii_bus->irq);
err_out_free_mdiobus:
	mdiobus_free(lp->mii_bus);
err_out:
	return rc;
}

/**
 * xemacps_update_hdaddr - Update device's MAC address when configured
 * MAC address is not valid, reconfigure with a good one.
 * @lp: local device instance pointer
 **/
static void __devinit xemacps_update_hwaddr(struct net_local *lp)
{
	u32 regvall;
	u16 regvalh;
	u8  addr[6];

	regvall = xemacps_read(lp->baseaddr, XEMACPS_LADDR1L_OFFSET);
	regvalh = xemacps_read(lp->baseaddr, XEMACPS_LADDR1H_OFFSET);
	addr[0] = regvall & 0xFF;
	addr[1] = (regvall >> 8) & 0xFF;
	addr[2] = (regvall >> 16) & 0xFF;
	addr[3] = (regvall >> 24) & 0xFF;
	addr[4] = regvalh & 0xFF;
	addr[5] = (regvalh >> 8) & 0xFF;

	if (is_valid_ether_addr(addr)) {
		memcpy(lp->ndev->dev_addr, addr, sizeof(addr));
	} else {
		dev_info(&lp->pdev->dev, "invalid address, use assigned\n");
		random_ether_addr(lp->ndev->dev_addr);
		dev_info(&lp->pdev->dev,
				"MAC updated %02x:%02x:%02x:%02x:%02x:%02x\n",
				lp->ndev->dev_addr[0], lp->ndev->dev_addr[1],
				lp->ndev->dev_addr[2], lp->ndev->dev_addr[3],
				lp->ndev->dev_addr[4], lp->ndev->dev_addr[5]);
	}
}

/**
 * xemacps_set_hwaddr - Set device's MAC address from ndev->dev_addr
 * @lp: local device instance pointer
 **/
static void xemacps_set_hwaddr(struct net_local *lp)
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
	xemacps_write(lp->baseaddr, XEMACPS_LADDR1L_OFFSET, regvall);
	xemacps_write(lp->baseaddr, XEMACPS_LADDR1H_OFFSET, regvalh);
#ifdef DEBUG
	regvall = xemacps_read(lp->baseaddr, XEMACPS_LADDR1L_OFFSET);
	regvalh = xemacps_read(lp->baseaddr, XEMACPS_LADDR1H_OFFSET);
	dev_dbg(&lp->pdev->dev,
			"MAC 0x%08x, 0x%08x, %02x:%02x:%02x:%02x:%02x:%02x\n",
		regvall, regvalh,
		(regvall & 0xff), ((regvall >> 8) & 0xff),
		((regvall >> 16) & 0xff), (regvall >> 24),
		(regvalh & 0xff), (regvalh >> 8));
#endif
}

/*
 * xemacps_reset_hw - Helper function to reset the underlying hardware.
 * This is called when we get into such deep trouble that we don't know
 * how to handle otherwise.
 * @lp: local device instance pointer
 */
static void xemacps_reset_hw(struct net_local *lp)
{
	u32 regisr;
	/* make sure we have the buffer for ourselves */
	wmb();

	/* Have a clean start */
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, 0);

	/* Clear statistic counters */
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
		XEMACPS_NWCTRL_STATCLR_MASK);

	/* Clear TX and RX status */
	xemacps_write(lp->baseaddr, XEMACPS_TXSR_OFFSET, ~0UL);
	xemacps_write(lp->baseaddr, XEMACPS_RXSR_OFFSET, ~0UL);

	/* Disable all interrupts */
	xemacps_write(lp->baseaddr, XEMACPS_IDR_OFFSET, ~0UL);
	regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
	xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);
}

/**
 * xemacps_bdringalloc - reserve locations in BD list.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to allocate.
 * @bdptr: output parameter points to the first BD available for
 *         modification.
 * return 0 on success, negative value if not enough BDs.
 **/
static int xemacps_bdringalloc(struct xemacps_bdring *ringptr, unsigned numbd,
		struct xemacps_bd **bdptr)
{
	/* Enough free BDs available for the request? */
	if (ringptr->freecnt < numbd)
		return NETDEV_TX_BUSY;

	/* Set the return argument and move FreeHead forward */
	*bdptr = ringptr->freehead;
	XEMACPS_RING_SEEKAHEAD(ringptr, ringptr->freehead, numbd);
	ringptr->freecnt -= numbd;
	ringptr->precnt  += numbd;
	return 0;
}

/**
 * xemacps_bdringunalloc - Fully or partially undo xemacps_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to unallocate.
 * @bdptr: points to the first of BDs to be unallocated.
 * return 0 on success, negative value if error.
 **/
static int xemacps_bdringunalloc(struct xemacps_bdring *ringptr, unsigned numbd,
		struct xemacps_bd *bdptr)
{
	/* Enough BDs in the free state for the request? */
	if (ringptr->precnt < numbd)
		return -ENOSPC;

	/* Set the return argument and move FreeHead backward */
	XEMACPS_RING_SEEKBACK(ringptr, ringptr->freehead, numbd);
	ringptr->freecnt += numbd;
	ringptr->precnt  -= numbd;
	return 0;
}

#ifdef DEBUG_VERBOSE
static void print_ring(struct xemacps_bdring *ring)
{
	int i;
	unsigned regval;
	struct xemacps_bd *bd;

	pr_info("freehead %p prehead %p hwhead %p hwtail %p posthead %p\n",
			ring->freehead, ring->prehead, ring->hwhead,
			ring->hwtail, ring->posthead);
	pr_info("freecnt %d hwcnt %d precnt %d postcnt %d allcnt %d\n",
			ring->freecnt, ring->hwcnt, ring->precnt,
			ring->postcnt, ring->allcnt);

	bd = (struct xemacps_bd *)ring->firstbdaddr;
	for (i = 0; i < XEMACPS_RECV_BD_CNT; i++) {
		regval = xemacps_read(bd, XEMACPS_BD_ADDR_OFFSET);
		pr_info("BD %p: ADDR: 0x%08x\n", bd, regval);
		regval = xemacps_read(bd, XEMACPS_BD_STAT_OFFSET);
		pr_info("BD %p: STAT: 0x%08x\n", bd, regval);
		bd++;
	}
}
#endif

/**
 * xemacps_bdringtohw - Enqueue a set of BDs to hardware that were
 * previously allocated by xemacps_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to hardware.
 * @bdptr: points to the first of BDs to be processed.
 * return 0 on success, negative value if error.
 **/
static int xemacps_bdringtohw(struct xemacps_bdring *ringptr, unsigned numbd,
		struct xemacps_bd *bdptr)
{
	struct xemacps_bd *curbdptr;
	unsigned int i;
	unsigned int regval;

	/* if no bds to process, simply return. */
	if (numbd == 0)
		return 0;

	/* Make sure we are in sync with xemacps_bdringalloc() */
	if ((ringptr->precnt < numbd) || (ringptr->prehead != bdptr))
		return -ENOSPC;

	curbdptr = bdptr;
	for (i = 0; i < numbd; i++) {
		/* Assign ownership back to hardware */
		if (ringptr->is_rx) {
			xemacps_write(curbdptr, XEMACPS_BD_STAT_OFFSET, 0);
			wmb();

			regval = xemacps_read(curbdptr, XEMACPS_BD_ADDR_OFFSET);
			regval &= ~XEMACPS_RXBUF_NEW_MASK;
			xemacps_write(curbdptr, XEMACPS_BD_ADDR_OFFSET, regval);
		} else {
			regval = xemacps_read(curbdptr, XEMACPS_BD_STAT_OFFSET);
			/* clear used bit - hardware to own this descriptor */
			regval &= ~XEMACPS_TXBUF_USED_MASK;
			xemacps_write(curbdptr, XEMACPS_BD_STAT_OFFSET, regval);
		}
		wmb();
		curbdptr = XEMACPS_BDRING_NEXT(ringptr, curbdptr);
	}
	/* Adjust ring pointers & counters */
	XEMACPS_RING_SEEKAHEAD(ringptr, ringptr->prehead, numbd);
	ringptr->hwtail  = curbdptr;
	ringptr->precnt -= numbd;
	ringptr->hwcnt  += numbd;

	return 0;
}

/**
 * xemacps_bdringfromhwtx - returns a set of BD(s) that have been
 * processed by hardware in tx direction.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @bdlimit: maximum number of BDs to return in the set.
 * @bdptr: output parameter points to the first BD available for
 *         examination.
 * return number of BDs processed by hardware.
 **/
static u32 xemacps_bdringfromhwtx(struct xemacps_bdring *ringptr,
		unsigned bdlimit, struct xemacps_bd **bdptr)
{
	struct xemacps_bd *curbdptr;
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

	if (bdlimit > ringptr->hwcnt)
		bdlimit = ringptr->hwcnt;

	/* Starting at hwhead, keep moving forward in the list until:
	 *  - ringptr->hwtail is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (bdcount < bdlimit) {
		/* Read the status */
		bdstr = xemacps_read(curbdptr, XEMACPS_BD_STAT_OFFSET);

		if (sop == 0) {
			if (bdstr & XEMACPS_TXBUF_USED_MASK)
				sop = 1;
			else
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
		if ((sop == 1) && (bdstr & XEMACPS_TXBUF_LAST_MASK)) {
			sop = 0;
			bdpartialcount = 0;
		}

		/* Move on to next BD in work group */
		curbdptr = XEMACPS_BDRING_NEXT(ringptr, curbdptr);
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
		XEMACPS_RING_SEEKAHEAD(ringptr, ringptr->hwhead, bdcount);
		return bdcount;
	} else {
		*bdptr = NULL;
		return 0;
	}
}

/**
 * xemacps_bdringfromhwrx - returns a set of BD(s) that have been
 * processed by hardware in rx direction.
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @bdlimit: maximum number of BDs to return in the set.
 * @bdptr: output parameter points to the first BD available for
 *         examination.
 * return number of BDs processed by hardware.
 **/
static u32 xemacps_bdringfromhwrx(struct xemacps_bdring *ringptr, int bdlimit,
		struct xemacps_bd **bdptr)
{
	struct xemacps_bd *curbdptr;
	u32 bdadd = 0;
	int bdcount = 0;
	curbdptr = ringptr->hwhead;

	/* If no BDs in work group, then there's nothing to search */
	if (ringptr->hwcnt == 0) {
		*bdptr = NULL;
		return 0;
	}

	if (bdlimit > ringptr->hwcnt)
		bdlimit = ringptr->hwcnt;

	/* Starting at hwhead, keep moving forward in the list until:
	 *  - A BD is encountered with its new/used bit set which means
	 *    hardware has not completed processing of that BD.
	 *  - ringptr->hwtail is reached.
	 *  - The number of requested BDs has been processed
	 */
	while (bdcount < bdlimit) {
		/* Read the status word to see if BD has been processed. */
		bdadd = xemacps_read(curbdptr, XEMACPS_BD_ADDR_OFFSET);
		if (bdadd & XEMACPS_RXBUF_NEW_MASK)
			bdcount++;
		else
			break;

		/* Move on to next BD in work group */
		curbdptr = XEMACPS_BDRING_NEXT(ringptr, curbdptr);
	}

	/* If bdcount is non-zero then BDs were found to return. Set return
	 * parameters, update pointers and counters, return number of BDs
	 */
	if (bdcount > 0) {
		*bdptr = ringptr->hwhead;
		ringptr->hwcnt   -= bdcount;
		ringptr->postcnt += bdcount;
		XEMACPS_RING_SEEKAHEAD(ringptr, ringptr->hwhead, bdcount);
		return bdcount;
	} else {
		*bdptr = NULL;
		return 0;
	}
}

/**
 * xemacps_bdringfree - Free a set of BDs that has been retrieved with
 * xemacps_bdringfromhw().
 * previously allocated by xemacps_bdringalloc().
 * @ringptr: pointer to the BD ring instance to be worked on.
 * @numbd: number of BDs to allocate.
 * @bdptr: the head of BD list returned by xemacps_bdringfromhw().
 * return 0 on success, negative value if error.
 **/
static int xemacps_bdringfree(struct xemacps_bdring *ringptr, unsigned numbd,
		struct xemacps_bd *bdptr)
{
	/* if no bds to free, simply return. */
	if (0 == numbd)
		return 0;

	/* Make sure we are in sync with xemacps_bdringfromhw() */
	if ((ringptr->postcnt < numbd) || (ringptr->posthead != bdptr))
		return -ENOSPC;

	/* Update pointers and counters */
	ringptr->freecnt += numbd;
	ringptr->postcnt -= numbd;
	XEMACPS_RING_SEEKAHEAD(ringptr, ringptr->posthead, numbd);
	return 0;
}


/**
 * xemacps_DmaSetupRecvBuffers - allocates socket buffers (sk_buff's)
 * up to the number of free RX buffer descriptors. Then it sets up the RX
 * buffer descriptors to DMA into the socket_buffers.
 * @ndev: the net_device
 **/
static void xemacps_DmaSetupRecvBuffers(struct net_device *ndev)
{
	struct net_local *lp;
	struct xemacps_bdring *rxringptr;
	struct xemacps_bd *bdptr;
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
		new_skb = netdev_alloc_skb(ndev, XEMACPS_RX_BUF_SIZE);
		if (new_skb == NULL) {
			lp->stats.rx_dropped++;
			break;
		}

		result = xemacps_bdringalloc(rxringptr, 1, &bdptr);
		if (result) {
			dev_err(&lp->pdev->dev, "RX bdringalloc() error.\n");
			break;
		}

		/* Get dma handle of skb->data */
		new_skb_baddr = (u32) dma_map_single(ndev->dev.parent,
			new_skb->data, XEMACPS_RX_BUF_SIZE, DMA_FROM_DEVICE);

		XEMACPS_SET_BUFADDR_RX(bdptr, new_skb_baddr);
		bdidx = XEMACPS_BD_TO_INDEX(rxringptr, bdptr);
		lp->rx_skb[bdidx].skb = new_skb;
		lp->rx_skb[bdidx].mapping = new_skb_baddr;
		wmb();

		/* enqueue RxBD with the attached skb buffers such that it is
		 * ready for frame reception
		 */
		result = xemacps_bdringtohw(rxringptr, 1, bdptr);
		if (result) {
			dev_err(&lp->pdev->dev,
					"bdringtohw unsuccessful (%d)\n",
					result);
			break;
		}
	}
}

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP

/**
 * xemacps_get_hwticks - get the current value of the GEM internal timer
 * @lp: local device instance pointer
 * return: nothing
 **/
static inline void
xemacps_get_hwticks(struct net_local *lp, u64 *sec, u64 *nsec)
{
	do {
		*nsec = xemacps_read(lp->baseaddr, XEMACPS_1588NS_OFFSET);
		*sec = xemacps_read(lp->baseaddr, XEMACPS_1588S_OFFSET);
	} while (*nsec > xemacps_read(lp->baseaddr, XEMACPS_1588NS_OFFSET));
}

/**
 * xemacps_read_clock - read raw cycle counter (to be used by time counter)
 */
static cycle_t xemacps_read_clock(const struct cyclecounter *tc)
{
	struct net_local *lp =
			container_of(tc, struct net_local, cycles);
	u64 stamp;
	u64 sec, nsec;

	xemacps_get_hwticks(lp, &sec, &nsec);
	stamp = (sec << 32) | nsec;

	return stamp;
}


/**
 * xemacps_systim_to_hwtstamp - convert system time value to hw timestamp
 * @adapter: board private structure
 * @shhwtstamps: timestamp structure to update
 * @regval: unsigned 64bit system time value.
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions
 */
static void xemacps_systim_to_hwtstamp(struct net_local *lp,
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

static void
xemacps_rx_hwtstamp(struct net_local *lp,
			struct sk_buff *skb, unsigned msg_type)
{
	u64 time64, sec, nsec;

	if (!msg_type) {
		/* PTP Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPERXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPERXNS_OFFSET);
	} else {
		/* PTP Peer Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPPRXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPPRXNS_OFFSET);
	}
	time64 = (sec << 32) | nsec;
	xemacps_systim_to_hwtstamp(lp, skb_hwtstamps(skb), time64);
}

static void
xemacps_tx_hwtstamp(struct net_local *lp,
			struct sk_buff *skb, unsigned msg_type)
{
	u64 time64, sec, nsec;

	if (!msg_type) {
		/* PTP Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPETXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPETXNS_OFFSET);
	} else {
		/* PTP Peer Event Frame packets */
		sec = xemacps_read(lp->baseaddr, XEMACPS_PTPPTXS_OFFSET);
		nsec = xemacps_read(lp->baseaddr, XEMACPS_PTPPTXNS_OFFSET);
	}

	time64 = (sec << 32) | nsec;
	xemacps_systim_to_hwtstamp(lp, skb_hwtstamps(skb), time64);
	skb_tstamp_tx(skb, skb_hwtstamps(skb));
}

#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_rx - process received packets when napi called
 * @lp: local device instance pointer
 * @budget: NAPI budget
 * return: number of BDs processed
 **/
static int xemacps_rx(struct net_local *lp, int budget)
{
	u32 regval, len = 0;
	struct sk_buff *skb = NULL;
	struct xemacps_bd *bdptr, *bdptrfree;
	unsigned int numbdfree, numbd = 0, bdidx = 0, rc = 0;

	numbd = xemacps_bdringfromhwrx(&lp->rx_ring, budget, &bdptr);

	numbdfree = numbd;
	bdptrfree = bdptr;

#ifdef DEBUG_VERBOSE
	dev_dbg(&lp->pdev->dev, "%s: numbd %d\n", __func__, numbd);
#endif

	while (numbd) {
		bdidx = XEMACPS_BD_TO_INDEX(&lp->rx_ring, bdptr);
		regval = xemacps_read(bdptr, XEMACPS_BD_STAT_OFFSET);

#ifdef DEBUG_VERBOSE
		dev_dbg(&lp->pdev->dev,
			"%s: RX BD index %d, BDptr %p, BD_STAT 0x%08x\n",
			__func__, bdidx, bdptr, regval);
#endif

		/* look for start of packet */
		if (!(regval & XEMACPS_RXBUF_SOF_MASK) ||
		    !(regval & XEMACPS_RXBUF_EOF_MASK)) {
			dev_info(&lp->pdev->dev,
				"%s: SOF and EOF not set (0x%08x) BD %p\n",
				__func__, regval, bdptr);
			lp->stats.rx_dropped++;
			return 0;
		}

		/* the packet length */
		len = regval & XEMACPS_RXBUF_LEN_MASK;

		skb = lp->rx_skb[bdidx].skb;
		dma_unmap_single(lp->ndev->dev.parent,
						lp->rx_skb[bdidx].mapping,
						XEMACPS_RX_BUF_SIZE,
						DMA_FROM_DEVICE);

		lp->rx_skb[bdidx].skb = NULL;
		lp->rx_skb[bdidx].mapping = 0;

		/* setup received skb and send it upstream */
		skb_put(skb, len);  /* Tell the skb how much data we got. */
		skb->dev = lp->ndev;

		/* Why does this return the protocol in network bye order ? */
		skb->protocol = eth_type_trans(skb, lp->ndev);

		skb->ip_summed = lp->ip_summed;

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
		if ((lp->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) &&
		    (ntohs(skb->protocol) == 0x800)) {
			unsigned ip_proto, dest_port, msg_type;

			/* While the GEM can timestamp PTP packets, it does
			 * not mark the RX descriptor to identify them.  This
			 * is entirely the wrong place to be parsing UDP
			 * headers, but some minimal effort must be made.
			 * NOTE: the below parsing of ip_proto and dest_port
			 * depend on the use of Ethernet_II encapsulation,
			 * IPv4 without any options.
			 */
			ip_proto = *((u8 *)skb->mac_header + 14 + 9);
			dest_port = ntohs(*(((u16 *)skb->mac_header) +
						((14 + 20 + 2)/2)));
			msg_type = *((u8 *)skb->mac_header + 42);
			if ((ip_proto == IPPROTO_UDP) &&
			    (dest_port == 0x13F)) {
				/* Timestamp this packet */
				xemacps_rx_hwtstamp(lp, skb, msg_type & 0x2);
			}
		}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

		lp->stats.rx_packets++;
		lp->stats.rx_bytes += len;
		netif_receive_skb(skb);

		bdptr = XEMACPS_BDRING_NEXT(&lp->rx_ring, bdptr);
		numbd--;
	}

	/* Make used BDs available */
	rc = xemacps_bdringfree(&lp->rx_ring, numbdfree, bdptrfree);
	if (rc)
		dev_err(&lp->pdev->dev, "RX bdringfree() error.\n");

	/* Refill RX buffers */
	xemacps_DmaSetupRecvBuffers(lp->ndev);

	return numbdfree;
}

/**
 * xemacps_rx_poll - NAPI poll routine
 * napi: pointer to napi struct
 * budget:
 **/
static int xemacps_rx_poll(struct napi_struct *napi, int budget)
{
	struct net_local *lp = container_of(napi, struct net_local, napi);
	int work_done = 0;
	int temp_work_done;
	u32 regval;


	while (work_done < budget) {
		regval = xemacps_read(lp->baseaddr, XEMACPS_RXSR_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_RXSR_OFFSET, regval);
		if (regval & (XEMACPS_RXSR_HRESPNOK_MASK |
					XEMACPS_RXSR_BUFFNA_MASK))
			lp->stats.rx_errors++;
		temp_work_done = xemacps_rx(lp, budget - work_done);
		work_done += temp_work_done;
		if (temp_work_done <= 0)
			break;
	}

	if (work_done >= budget)
		return work_done;

	napi_complete(napi);
	/* We disabled RX interrupts in interrupt service
	 * routine, now it is time to enable it back.
	 */
	xemacps_write(lp->baseaddr, XEMACPS_IER_OFFSET,
					(XEMACPS_IXR_FRAMERX_MASK |
					XEMACPS_IXR_RX_ERR_MASK));

	return work_done;
}

/**
 * xemacps_tx_poll - tx isr handler routine
 * @data: pointer to network interface device structure
 **/
static void xemacps_tx_poll(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval, bdlen = 0;
	struct xemacps_bd *bdptr, *bdptrfree;
	struct ring_info *rp;
	struct sk_buff *skb;
	unsigned int numbd, numbdfree, bdidx, rc;

	regval = xemacps_read(lp->baseaddr, XEMACPS_TXSR_OFFSET);
	xemacps_write(lp->baseaddr, XEMACPS_TXSR_OFFSET, regval);
	dev_dbg(&lp->pdev->dev, "TX status 0x%x\n", regval);

	/* If this error is seen, it is in deep trouble and nothing
	 * we can do to revive hardware other than reset hardware.
	 * Or try to close this interface and reopen it.
	 */
	if (regval & (XEMACPS_TXSR_RXOVR_MASK | XEMACPS_TXSR_HRESPNOK_MASK
					| XEMACPS_TXSR_BUFEXH_MASK))
		lp->stats.tx_errors++;

	/* This may happen when a buffer becomes complete
	 * between reading the ISR and scanning the descriptors.
	 * Nothing to worry about.
	 */
	if (!(regval & XEMACPS_TXSR_TXCOMPL_MASK))
		goto tx_poll_out;

	numbd = xemacps_bdringfromhwtx(&lp->tx_ring, XEMACPS_SEND_BD_CNT,
		&bdptr);
	numbdfree = numbd;
	bdptrfree = bdptr;

	while (numbd) {
		regval  = xemacps_read(bdptr, XEMACPS_BD_STAT_OFFSET);
		rmb();
		bdlen = regval & XEMACPS_TXBUF_LEN_MASK;
		bdidx = XEMACPS_BD_TO_INDEX(&lp->tx_ring, bdptr);
		rp = &lp->tx_skb[bdidx];
		skb = rp->skb;

		BUG_ON(skb == NULL);

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
		if ((lp->hwtstamp_config.tx_type == HWTSTAMP_TX_ON) &&
			(ntohs(skb->protocol) == 0x800)) {
			unsigned ip_proto, dest_port, msg_type;

			skb_reset_mac_header(skb);

			ip_proto = *((u8 *)skb->mac_header + 14 + 9);
			dest_port = ntohs(*(((u16 *)skb->mac_header) +
					((14 + 20 + 2)/2)));
			msg_type = *((u8 *)skb->mac_header + 42);
			if ((ip_proto == IPPROTO_UDP) &&
				(dest_port == 0x13F)) {
				/* Timestamp this packet */
				xemacps_tx_hwtstamp(lp, skb, msg_type & 0x2);
			}
		}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

		dma_unmap_single(&lp->pdev->dev, rp->mapping, skb->len,
			DMA_TO_DEVICE);
		rp->skb = NULL;
		dev_kfree_skb_irq(skb);
#ifdef DEBUG_VERBOSE_TX
		dev_dbg(&lp->pdev->dev,
				"TX bd index %d BD_STAT 0x%08x after sent.\n",
				bdidx, regval);
#endif
		/* log tx completed packets and bytes, errors logs
		 * are in other error counters.
		 */
		if (regval & XEMACPS_TXBUF_LAST_MASK) {
			if (!(regval & XEMACPS_TXBUF_ERR_MASK)) {
				lp->stats.tx_packets++;
				lp->stats.tx_bytes += bdlen;
			}
		}

		/* Preserve used and wrap bits; clear everything else. */
		regval &= (XEMACPS_TXBUF_USED_MASK | XEMACPS_TXBUF_WRAP_MASK);
		xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET, regval);

		bdptr = XEMACPS_BDRING_NEXT(&lp->tx_ring, bdptr);
		numbd--;
		wmb();
	}

	rc = xemacps_bdringfree(&lp->tx_ring, numbdfree, bdptrfree);
	if (rc)
		dev_err(&lp->pdev->dev, "TX bdringfree() error.\n");

tx_poll_out:
	if (netif_queue_stopped(ndev))
		netif_start_queue(ndev);
}

/**
 * xemacps_interrupt - interrupt main service routine
 * @irq: interrupt number
 * @dev_id: pointer to a network device structure
 * return IRQ_HANDLED or IRQ_NONE
 **/
static irqreturn_t xemacps_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct net_local *lp = netdev_priv(ndev);
	u32 regisr;

	spin_lock(&lp->lock);
	regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
	if (unlikely(!regisr)) {
		spin_unlock(&lp->lock);
		return IRQ_NONE;
	}
	xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);

	while (regisr) {
		if (regisr & (XEMACPS_IXR_TXCOMPL_MASK |
				XEMACPS_IXR_TX_ERR_MASK)) {
			xemacps_tx_poll(ndev);
		}
		if (regisr & (XEMACPS_IXR_FRAMERX_MASK |
			XEMACPS_IXR_RX_ERR_MASK)) {
			xemacps_write(lp->baseaddr, XEMACPS_IDR_OFFSET,
					(XEMACPS_IXR_FRAMERX_MASK |
					XEMACPS_IXR_RX_ERR_MASK));
			napi_schedule(&lp->napi);
		}
		regisr = xemacps_read(lp->baseaddr, XEMACPS_ISR_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_ISR_OFFSET, regisr);
	}
	spin_unlock(&lp->lock);

	return IRQ_HANDLED;
}

/*
 * Free all packets presently in the descriptor rings.
 */
static void xemacps_clean_rings(struct net_local *lp)
{
	int i;

	for (i = 0; i < XEMACPS_RECV_BD_CNT; i++) {
		if (lp->rx_skb && lp->rx_skb[i].skb) {
			dma_unmap_single(lp->ndev->dev.parent,
					 lp->rx_skb[i].mapping,
					 XEMACPS_RX_BUF_SIZE,
					 DMA_FROM_DEVICE);

			dev_kfree_skb(lp->rx_skb[i].skb);
			lp->rx_skb[i].skb = NULL;
			lp->rx_skb[i].mapping = 0;
		}
	}

	for (i = 0; i < XEMACPS_SEND_BD_CNT; i++) {
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
 * xemacps_descriptor_free - Free allocated TX and RX BDs
 * @lp: local device instance pointer
 **/
static void xemacps_descriptor_free(struct net_local *lp)
{
	int size;

	xemacps_clean_rings(lp);

	/* kfree(NULL) is safe, no need to check here */
	kfree(lp->tx_skb);
	lp->tx_skb = NULL;
	kfree(lp->rx_skb);
	lp->rx_skb = NULL;

	size = XEMACPS_RECV_BD_CNT * sizeof(struct xemacps_bd);
	if (lp->rx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->rx_bd, lp->rx_bd_dma);
		lp->rx_bd = NULL;
	}

	size = XEMACPS_SEND_BD_CNT * sizeof(struct xemacps_bd);
	if (lp->tx_bd) {
		dma_free_coherent(&lp->pdev->dev, size,
			lp->tx_bd, lp->tx_bd_dma);
		lp->tx_bd = NULL;
	}
}

/**
 * xemacps_descriptor_init - Allocate both TX and RX BDs
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacps_descriptor_init(struct net_local *lp)
{
	int size;

	size = XEMACPS_SEND_BD_CNT * sizeof(struct ring_info);
	lp->tx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->tx_skb)
		goto err_out;
	size = XEMACPS_RECV_BD_CNT * sizeof(struct ring_info);
	lp->rx_skb = kzalloc(size, GFP_KERNEL);
	if (!lp->rx_skb)
		goto err_out;

	size = XEMACPS_RECV_BD_CNT * sizeof(struct xemacps_bd);
	lp->rx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->rx_bd_dma, GFP_KERNEL);
	if (!lp->rx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "RX ring %d bytes at 0x%x mapped %p\n",
			size, lp->rx_bd_dma, lp->rx_bd);

	size = XEMACPS_SEND_BD_CNT * sizeof(struct xemacps_bd);
	lp->tx_bd = dma_alloc_coherent(&lp->pdev->dev, size,
			&lp->tx_bd_dma, GFP_KERNEL);
	if (!lp->tx_bd)
		goto err_out;
	dev_dbg(&lp->pdev->dev, "TX ring %d bytes at 0x%x mapped %p\n",
			size, lp->tx_bd_dma, lp->tx_bd);

	dev_dbg(&lp->pdev->dev,
		"lp->tx_bd %p lp->tx_bd_dma %p lp->tx_skb %p\n",
		lp->tx_bd, (void *)lp->tx_bd_dma, lp->tx_skb);
	dev_dbg(&lp->pdev->dev,
		"lp->rx_bd %p lp->rx_bd_dma %p lp->rx_skb %p\n",
		lp->rx_bd, (void *)lp->rx_bd_dma, lp->rx_skb);

	return 0;

err_out:
	xemacps_descriptor_free(lp);
	return -ENOMEM;
}

/**
 * xemacps_setup_ring - Setup both TX and RX BD rings
 * @lp: local device instance pointer
 * return 0 on success, negative value if error
 **/
static int xemacps_setup_ring(struct net_local *lp)
{
	int i;
	u32 regval;
	struct xemacps_bd *bdptr;

	lp->rx_ring.separation   = (sizeof(struct xemacps_bd) +
		(ALIGNMENT_BD - 1)) & ~(ALIGNMENT_BD - 1);
	lp->rx_ring.physbaseaddr = lp->rx_bd_dma;
	lp->rx_ring.firstbdaddr  = (u32)lp->rx_bd;
	lp->rx_ring.lastbdaddr   = (u32)(lp->rx_bd +
		(XEMACPS_RECV_BD_CNT - 1) * sizeof(struct xemacps_bd));
	lp->rx_ring.length       = lp->rx_ring.lastbdaddr -
		lp->rx_ring.firstbdaddr + lp->rx_ring.separation;
	lp->rx_ring.freehead     = (struct xemacps_bd *)lp->rx_bd;
	lp->rx_ring.prehead      = (struct xemacps_bd *)lp->rx_bd;
	lp->rx_ring.hwhead       = (struct xemacps_bd *)lp->rx_bd;
	lp->rx_ring.hwtail       = (struct xemacps_bd *)lp->rx_bd;
	lp->rx_ring.posthead     = (struct xemacps_bd *)lp->rx_bd;
	lp->rx_ring.allcnt       = XEMACPS_RECV_BD_CNT;
	lp->rx_ring.freecnt      = XEMACPS_RECV_BD_CNT;
	lp->rx_ring.precnt       = 0;
	lp->rx_ring.hwcnt        = 0;
	lp->rx_ring.postcnt      = 0;
	lp->rx_ring.is_rx        = 1;

	bdptr = (struct xemacps_bd *)lp->rx_ring.firstbdaddr;

	/* Setup RX BD ring structure and populate buffer address. */
	for (i = 0; i < (XEMACPS_RECV_BD_CNT - 1); i++) {
		xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET, 0);
		xemacps_write(bdptr, XEMACPS_BD_ADDR_OFFSET, 0);
		bdptr = XEMACPS_BDRING_NEXT(&lp->rx_ring, bdptr);
	}
	/* wrap bit set for last BD, bdptr is moved to last here */
	xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET, 0);
	xemacps_write(bdptr, XEMACPS_BD_ADDR_OFFSET, XEMACPS_RXBUF_WRAP_MASK);

	/* Allocate RX skbuffs; set descriptor buffer addresses */
	xemacps_DmaSetupRecvBuffers(lp->ndev);

	lp->tx_ring.separation   = (sizeof(struct xemacps_bd) +
		(ALIGNMENT_BD - 1)) & ~(ALIGNMENT_BD - 1);
	lp->tx_ring.physbaseaddr = lp->tx_bd_dma;
	lp->tx_ring.firstbdaddr  = (u32)lp->tx_bd;
	lp->tx_ring.lastbdaddr   = (u32)(lp->tx_bd +
		(XEMACPS_SEND_BD_CNT - 1) * sizeof(struct xemacps_bd));
	lp->tx_ring.length       = lp->tx_ring.lastbdaddr -
		lp->tx_ring.firstbdaddr + lp->tx_ring.separation;
	lp->tx_ring.freehead     = (struct xemacps_bd *)lp->tx_bd;
	lp->tx_ring.prehead      = (struct xemacps_bd *)lp->tx_bd;
	lp->tx_ring.hwhead       = (struct xemacps_bd *)lp->tx_bd;
	lp->tx_ring.hwtail       = (struct xemacps_bd *)lp->tx_bd;
	lp->tx_ring.posthead     = (struct xemacps_bd *)lp->tx_bd;
	lp->tx_ring.allcnt       = XEMACPS_SEND_BD_CNT;
	lp->tx_ring.freecnt      = XEMACPS_SEND_BD_CNT;
	lp->tx_ring.precnt       = 0;
	lp->tx_ring.hwcnt        = 0;
	lp->tx_ring.postcnt      = 0;
	lp->tx_ring.is_rx        = 0;

	bdptr = (struct xemacps_bd *)lp->tx_ring.firstbdaddr;

	/* Setup TX BD ring structure and assert used bit initially. */
	for (i = 0; i < (XEMACPS_SEND_BD_CNT - 1); i++) {
		xemacps_write(bdptr, XEMACPS_BD_ADDR_OFFSET, 0);
		xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET,
			XEMACPS_TXBUF_USED_MASK);
		bdptr = XEMACPS_BDRING_NEXT(&lp->tx_ring, bdptr);
	}
	/* wrap bit set for last BD, bdptr is moved to last here */
	xemacps_write(bdptr, XEMACPS_BD_ADDR_OFFSET, 0);
	regval = (XEMACPS_TXBUF_WRAP_MASK | XEMACPS_TXBUF_USED_MASK);
	xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET, regval);

	return 0;
}

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
/*
 * Initialize the GEM Time Stamp Unit
 */
static void xemacps_init_tsu(struct net_local *lp)
{

	memset(&lp->cycles, 0, sizeof(lp->cycles));
	lp->cycles.read = xemacps_read_clock;
	lp->cycles.mask = CLOCKSOURCE_MASK(64);
	lp->cycles.mult = 1;
	lp->cycles.shift = 0;

	/* Set registers so that rollover occurs soon to test this. */
	xemacps_write(lp->baseaddr, XEMACPS_1588NS_OFFSET, 0x00000000);
	xemacps_write(lp->baseaddr, XEMACPS_1588S_OFFSET, 0xFF800000);

	/* program the timer increment register with the numer of nanoseconds
	 * per clock tick.
	 *
	 * Note: The value is calculated based on the current operating
	 * frequency 50MHz
	 */
	xemacps_write(lp->baseaddr, XEMACPS_1588INC_OFFSET,
			(NS_PER_SEC/lp->ptpenetclk));

	timecounter_init(&lp->clock, &lp->cycles,
				ktime_to_ns(ktime_get_real()));
	/*
	 * Synchronize our NIC clock against system wall clock.
	 */
	memset(&lp->compare, 0, sizeof(lp->compare));
	lp->compare.source = &lp->clock;
	lp->compare.target = ktime_get_real;
	lp->compare.num_samples = 10;
	timecompare_update(&lp->compare, 0);

	/* Initialize hwstamp config */
	lp->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	lp->hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;

}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_init_hw - Initialize hardware to known good state
 * @lp: local device instance pointer
 **/
static void xemacps_init_hw(struct net_local *lp)
{
	u32 regval;

	xemacps_reset_hw(lp);
	xemacps_set_hwaddr(lp);

	/* network configuration */
	regval  = 0;
	regval |= XEMACPS_NWCFG_FDEN_MASK;
	regval |= XEMACPS_NWCFG_RXCHKSUMEN_MASK;
	regval |= XEMACPS_NWCFG_PAUSECOPYDI_MASK;
	regval |= XEMACPS_NWCFG_FCSREM_MASK;
	regval |= XEMACPS_NWCFG_PAUSEEN_MASK;
	regval |= XEMACPS_NWCFG_100_MASK;
	regval |= XEMACPS_NWCFG_HDRXEN_MASK;

	if (lp->board_type == BOARD_TYPE_ZYNQ)
		regval |= (MDC_DIV_224 << XEMACPS_NWCFG_MDC_SHIFT_MASK);
	if (lp->ndev->flags & IFF_PROMISC)	/* copy all */
		regval |= XEMACPS_NWCFG_COPYALLEN_MASK;
	if (!(lp->ndev->flags & IFF_BROADCAST))	/* No broadcast */
		regval |= XEMACPS_NWCFG_BCASTDI_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);

	/* Init TX and RX DMA Q address */
	xemacps_write(lp->baseaddr, XEMACPS_RXQBASE_OFFSET,
		lp->rx_ring.physbaseaddr);
	xemacps_write(lp->baseaddr, XEMACPS_TXQBASE_OFFSET,
		lp->tx_ring.physbaseaddr);

	/* DMACR configurations */
	regval  = (((XEMACPS_RX_BUF_SIZE / XEMACPS_RX_BUF_UNIT) +
		((XEMACPS_RX_BUF_SIZE % XEMACPS_RX_BUF_UNIT) ? 1 : 0)) <<
		XEMACPS_DMACR_RXBUF_SHIFT);
	regval |= XEMACPS_DMACR_RXSIZE_MASK;
	regval |= XEMACPS_DMACR_TXSIZE_MASK;
	regval |= XEMACPS_DMACR_TCPCKSUM_MASK;
#ifdef __LITTLE_ENDIAN
	regval &= ~XEMACPS_DMACR_ENDIAN_MASK;
#endif
#ifdef __BIG_ENDIAN
	regval |= XEMACPS_DMACR_ENDIAN_MASK;
#endif
	regval |= XEMACPS_DMACR_BLENGTH_INCR16;
	xemacps_write(lp->baseaddr, XEMACPS_DMACR_OFFSET, regval);

	/* Enable TX, RX and MDIO port */
	regval  = 0;
	regval |= XEMACPS_NWCTRL_MDEN_MASK;
	regval |= XEMACPS_NWCTRL_TXEN_MASK;
	regval |= XEMACPS_NWCTRL_RXEN_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regval);

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	/* Initialize the Time Stamp Unit */
	xemacps_init_tsu(lp);
#endif

	/* Enable interrupts */
	regval  = XEMACPS_IXR_ALL_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_IER_OFFSET, regval);
}

/**
 * xemacps_open - Called when a network device is made active
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
static int xemacps_open(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	int rc;

	dev_dbg(&lp->pdev->dev, "open\n");
	if (!is_valid_ether_addr(ndev->dev_addr))
		return  -EADDRNOTAVAIL;

	rc = xemacps_descriptor_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev,
			"Unable to allocate DMA memory, rc %d\n", rc);
		return rc;
	}

	rc = pm_runtime_get(&lp->pdev->dev);
	if (rc < 0) {
		dev_err(&lp->pdev->dev, "pm_runtime_get() failed, rc %d\n", rc);
		goto err_free_rings;
	}

	rc = xemacps_setup_ring(lp);
	if (rc) {
		dev_err(&lp->pdev->dev,
			"Unable to setup BD rings, rc %d\n", rc);
		goto err_pm_put;
	}

	xemacps_init_hw(lp);
	napi_enable(&lp->napi);
	rc = xemacps_mii_probe(ndev);
	if (rc != 0) {
		dev_err(&lp->pdev->dev,
			"%s mii_probe fail.\n", lp->mii_bus->name);
		if (rc == (-2)) {
			mdiobus_unregister(lp->mii_bus);
			kfree(lp->mii_bus->irq);
			mdiobus_free(lp->mii_bus);
		}
		rc = -ENXIO;
		goto err_pm_put;
	}

	netif_carrier_on(ndev);

	netif_start_queue(ndev);

	return 0;

err_pm_put:
	pm_runtime_put(&lp->pdev->dev);
err_free_rings:
	xemacps_descriptor_free(lp);

	return rc;
}

/**
 * xemacps_close - disable a network interface
 * @ndev: network interface device structure
 * return 0
 *
 * The close entry point is called when a network interface is de-activated
 * by OS. The hardware is still under the driver control, but needs to be
 * disabled. A global MAC reset is issued to stop the hardware, and all
 * transmit and receive resources are freed.
 **/
static int xemacps_close(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;

	netif_stop_queue(ndev);
	napi_disable(&lp->napi);
	spin_lock_irqsave(&lp->lock, flags);
	xemacps_reset_hw(lp);
	netif_carrier_off(ndev);
	spin_unlock_irqrestore(&lp->lock, flags);
	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	xemacps_descriptor_free(lp);

	pm_runtime_put(&lp->pdev->dev);

	return 0;
}

/**
 * xemacps_tx_timeout - callback uses when the transmitter has not made
 * any progress for dev->watchdog ticks.
 * @ndev: network interface device structure
 **/
static void xemacps_tx_timeout(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	int rc;

	dev_err(&lp->pdev->dev, "transmit timeout %lu ms, reseting...\n",
		TX_TIMEOUT * 1000UL / HZ);
	netif_stop_queue(ndev);

	spin_lock(&lp->lock);
	napi_disable(&lp->napi);
	xemacps_reset_hw(lp);
	xemacps_descriptor_free(lp);
	if (lp->phy_dev)
		phy_stop(lp->phy_dev);
	rc = xemacps_descriptor_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev,
			"Unable to allocate DMA memory, rc %d\n", rc);
		spin_unlock(&lp->lock);
		return;
	}

	rc = xemacps_setup_ring(lp);
	if (rc) {
		dev_err(&lp->pdev->dev, "Unable to setup BD rings, rc %d\n",
									rc);
		spin_unlock(&lp->lock);
		return;
	}
	xemacps_init_hw(lp);

	lp->link    = 0;
	lp->speed   = 0;
	lp->duplex  = -1;
	if (lp->phy_dev)
		phy_start(lp->phy_dev);
	napi_enable(&lp->napi);

	spin_unlock(&lp->lock);
	netif_start_queue(ndev);
}

/**
 * xemacps_set_mac_address - set network interface mac address
 * @ndev: network interface device structure
 * @addr: pointer to MAC address
 * return 0 on success, negative value if error
 **/
static int xemacps_set_mac_address(struct net_device *ndev, void *addr)
{
	struct net_local *lp = netdev_priv(ndev);
	struct sockaddr *hwaddr = (struct sockaddr *)addr;

	if (netif_running(ndev))
		return -EBUSY;

	if (!is_valid_ether_addr(hwaddr->sa_data))
		return -EADDRNOTAVAIL;

	dev_dbg(&lp->pdev->dev, "hwaddr 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		hwaddr->sa_data[0], hwaddr->sa_data[1], hwaddr->sa_data[2],
		hwaddr->sa_data[3], hwaddr->sa_data[4], hwaddr->sa_data[5]);

	memcpy(ndev->dev_addr, hwaddr->sa_data, ndev->addr_len);

	xemacps_set_hwaddr(lp);
	return 0;
}

/**
 * xemacps_start_xmit - transmit a packet (called by kernel)
 * @skb: socket buffer
 * @ndev: network interface device structure
 * return 0 on success, other value if error
 **/
static int xemacps_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	dma_addr_t  mapping;
	unsigned int nr_frags, bdidx, len;
	int i, rc;
	u32 regval;
	struct xemacps_bd *bdptr, *bdptrs;
	void       *virt_addr;
	skb_frag_t *frag;

#ifdef DEBUG_VERBOSE_TX
	dev_dbg(&lp->pdev->dev, "%s: TX data:", __func__);
	for (i = 0; i < 48; i++) {
		if (!(i % 16))
			dev_dbg(&lp->pdev->dev, "\n");
		dev_dbg(&lp->pdev->dev, " %02x", (unsigned int)skb->data[i]);
	}
	dev_dbg(&lp->pdev->dev, "\n");
#endif

	nr_frags = skb_shinfo(skb)->nr_frags + 1;
	spin_lock_irq(&lp->lock);

	if (nr_frags < lp->tx_ring.freecnt) {
		rc = xemacps_bdringalloc(&lp->tx_ring, nr_frags, &bdptr);
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
	dev_dbg(&lp->pdev->dev,
		"TX nr_frags %d, skb->len 0x%x, skb_headlen(skb) 0x%x\n",
		nr_frags, skb->len, skb_headlen(skb));
#endif

	for (i = 0; i < nr_frags; i++) {
		if (i == 0) {
			len = skb_headlen(skb);
			mapping = dma_map_single(&lp->pdev->dev, skb->data,
				len, DMA_TO_DEVICE);
		} else {
			len = skb_frag_size(frag);
			virt_addr = skb_frag_address(frag);
			mapping = dma_map_single(&lp->pdev->dev, virt_addr,
				len, DMA_TO_DEVICE);
			frag++;
		}

		bdidx = XEMACPS_BD_TO_INDEX(&lp->tx_ring, bdptr);

		lp->tx_skb[bdidx].skb = skb;
		lp->tx_skb[bdidx].mapping = mapping;
		wmb();

		xemacps_write(bdptr, XEMACPS_BD_ADDR_OFFSET, mapping);
		wmb();

		regval = xemacps_read(bdptr, XEMACPS_BD_STAT_OFFSET);
		/* Preserve only critical status bits.  Packet is NOT to be
		 * committed to hardware at this time.
		 */
		regval &= (XEMACPS_TXBUF_USED_MASK | XEMACPS_TXBUF_WRAP_MASK);
		/* update length field */
		regval |= ((regval & ~XEMACPS_TXBUF_LEN_MASK) | len);
		/* last fragment of this packet? */
		if (i == (nr_frags - 1))
			regval |= XEMACPS_TXBUF_LAST_MASK;
		xemacps_write(bdptr, XEMACPS_BD_STAT_OFFSET, regval);

#ifdef DEBUG_VERBOSE_TX
		dev_dbg(&lp->pdev->dev,
				"TX BD index %d, BDptr %p, BD_STAT 0x%08x\n",
				bdidx, bdptr, regval);
#endif
		bdptr = XEMACPS_BDRING_NEXT(&lp->tx_ring, bdptr);
	}
	wmb();

	rc = xemacps_bdringtohw(&lp->tx_ring, nr_frags, bdptrs);

	if (rc) {
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		lp->stats.tx_dropped++;
		xemacps_bdringunalloc(&lp->tx_ring, nr_frags, bdptrs);
		dev_err(&lp->pdev->dev, "cannot send, commit TX buffer desc\n");
		spin_unlock_irq(&lp->lock);
		return rc;
	} else {
		regval = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
			(regval | XEMACPS_NWCTRL_STARTTX_MASK));
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
					get_bit(mac, mac_bit + 42))
						<< index_bit;
		mac_bit--;
	}

	return hash_index;
}

/**
 * xemacps_set_hashtable - Add multicast addresses to the internal
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
static void xemacps_set_hashtable(struct net_device *ndev)
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
		hash_index = calc_mac_hash(mc_addr);

		if (hash_index >= XEMACPS_MAX_HASH_BITS) {
			dev_err(&lp->pdev->dev,
					"hash calculation out of range %d\n",
					hash_index);
			break;
		}
		if (hash_index < 32)
			regvall |= (1 << hash_index);
		else
			regvalh |= (1 << (hash_index - 32));
	}

	xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, regvall);
	xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, regvalh);
}

/**
 * xemacps_set_rx_mode - enable/disable promiscuous and multicast modes
 * @ndev: network interface device structure
 **/
static void xemacps_set_rx_mode(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	u32 regval;

	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);

	/* promisc mode */
	if (ndev->flags & IFF_PROMISC)
		regval |= XEMACPS_NWCFG_COPYALLEN_MASK;
	if (!(ndev->flags & IFF_PROMISC))
		regval &= ~XEMACPS_NWCFG_COPYALLEN_MASK;

	/* All multicast mode */
	if (ndev->flags & IFF_ALLMULTI) {
		regval |= XEMACPS_NWCFG_MCASTHASHEN_MASK;
		xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, ~0UL);
		xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, ~0UL);
	/* Specific multicast mode */
	} else if ((ndev->flags & IFF_MULTICAST)
			&& (netdev_mc_count(ndev) > 0)) {
		regval |= XEMACPS_NWCFG_MCASTHASHEN_MASK;
		xemacps_set_hashtable(ndev);
	/* Disable multicast mode */
	} else {
		xemacps_write(lp->baseaddr, XEMACPS_HASHL_OFFSET, 0x0);
		xemacps_write(lp->baseaddr, XEMACPS_HASHH_OFFSET, 0x0);
		regval &= ~XEMACPS_NWCFG_MCASTHASHEN_MASK;
	}

	/* broadcast mode */
	if (ndev->flags & IFF_BROADCAST)
		regval &= ~XEMACPS_NWCFG_BCASTDI_MASK;
	/* No broadcast */
	if (!(ndev->flags & IFF_BROADCAST))
		regval |= XEMACPS_NWCFG_BCASTDI_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);
}

#define MIN_MTU 60
#define MAX_MTU 1500
/**
 * xemacps_change_mtu - Change maximum transfer unit
 * @ndev: network interface device structure
 * @new_mtu: new vlaue for maximum frame size
 * return: 0 on success, negative value if error.
 **/
static int xemacps_change_mtu(struct net_device *ndev, int new_mtu)
{
	if ((new_mtu < MIN_MTU) ||
		((new_mtu + ndev->hard_header_len) > MAX_MTU))
		return -EINVAL;

	ndev->mtu = new_mtu;	/* change mtu in net_device structure */
	return 0;
}

/**
 * xemacps_get_settings - get device specific settings.
 * Usage: Issue "ethtool ethX" under linux prompt.
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacps_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, ecmd);
}

/**
 * xemacps_set_settings - set device specific settings.
 * Usage: Issue "ethtool -s ethX speed 1000" under linux prompt
 * to change speed
 * @ndev: network device
 * @ecmd: ethtool command structure
 * return: 0 on success, negative value if error.
 **/
static int
xemacps_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, ecmd);
}

/**
 * xemacps_get_drvinfo - report driver information
 * Usage: Issue "ethtool -i ethX" under linux prompt
 * @ndev: network device
 * @ed: device driver information structure
 **/
static void
xemacps_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *ed)
{
	struct net_local *lp = netdev_priv(ndev);

	memset(ed, 0, sizeof(struct ethtool_drvinfo));
	strcpy(ed->driver, lp->pdev->dev.driver->name);
	strcpy(ed->version, DRIVER_VERSION);
}

/**
 * xemacps_get_ringparam - get device dma ring information.
 * Usage: Issue "ethtool -g ethX" under linux prompt
 * @ndev: network device
 * @erp: ethtool ring parameter structure
 **/
static void
xemacps_get_ringparam(struct net_device *ndev, struct ethtool_ringparam *erp)
{
	struct net_local *lp = netdev_priv(ndev);
	memset(erp, 0, sizeof(struct ethtool_ringparam));

	erp->rx_max_pending = XEMACPS_RECV_BD_CNT;
	erp->tx_max_pending = XEMACPS_SEND_BD_CNT;
	erp->rx_pending = lp->rx_ring.hwcnt;
	erp->tx_pending = lp->tx_ring.hwcnt;
}

/**
 * xemacps_get_wol - get device wake on lan status
 * Usage: Issue "ethtool ethX" under linux prompt
 * @ndev: network device
 * @ewol: wol status
 **/
static void
xemacps_get_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	ewol->supported = WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST;
	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacps_read(lp->baseaddr, XEMACPS_WOL_OFFSET);
	if (regval | XEMACPS_WOL_MCAST_MASK)
		ewol->wolopts |= WAKE_MCAST;
	if (regval | XEMACPS_WOL_ARP_MASK)
		ewol->wolopts |= WAKE_ARP;
	if (regval | XEMACPS_WOL_SPEREG1_MASK)
		ewol->wolopts |= WAKE_UCAST;
	if (regval | XEMACPS_WOL_MAGIC_MASK)
		ewol->wolopts |= WAKE_MAGIC;
	spin_unlock_irqrestore(&lp->lock, flags);
}

/**
 * xemacps_set_wol - set device wake on lan configuration
 * Usage: Issue "ethtool -s ethX wol u|m|b|g" under linux prompt to enable
 * specified type of packet.
 * Usage: Issue "ethtool -s ethX wol d" under linux prompt to disable
 * this feature.
 * @ndev: network device
 * @ewol: wol status
 * return 0 on success, negative value if not supported
 **/
static int
xemacps_set_wol(struct net_device *ndev, struct ethtool_wolinfo *ewol)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	if (ewol->wolopts & ~(WAKE_MAGIC | WAKE_ARP | WAKE_UCAST | WAKE_MCAST))
		return -EOPNOTSUPP;

	spin_lock_irqsave(&lp->lock, flags);
	regval  = xemacps_read(lp->baseaddr, XEMACPS_WOL_OFFSET);
	regval &= ~(XEMACPS_WOL_MCAST_MASK | XEMACPS_WOL_ARP_MASK |
		XEMACPS_WOL_SPEREG1_MASK | XEMACPS_WOL_MAGIC_MASK);

	if (ewol->wolopts & WAKE_MAGIC)
		regval |= XEMACPS_WOL_MAGIC_MASK;
	if (ewol->wolopts & WAKE_ARP)
		regval |= XEMACPS_WOL_ARP_MASK;
	if (ewol->wolopts & WAKE_UCAST)
		regval |= XEMACPS_WOL_SPEREG1_MASK;
	if (ewol->wolopts & WAKE_MCAST)
		regval |= XEMACPS_WOL_MCAST_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_WOL_OFFSET, regval);
	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

/**
 * xemacps_get_pauseparam - get device pause status
 * Usage: Issue "ethtool -a ethX" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 *
 * note: hardware supports only tx flow control
 **/
static void
xemacps_get_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	epauseparm->autoneg  = 0;
	epauseparm->rx_pause = 0;

	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);
	epauseparm->tx_pause = regval & XEMACPS_NWCFG_PAUSEEN_MASK;
	spin_unlock_irqrestore(&lp->lock, flags);
}

/**
 * xemacps_set_pauseparam - set device pause parameter(flow control)
 * Usage: Issue "ethtool -A ethX tx on|off" under linux prompt
 * @ndev: network device
 * @epauseparam: pause parameter
 * return 0 on success, negative value if not supported
 *
 * note: hardware supports only tx flow control
 **/
static int
xemacps_set_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	struct net_local *lp = netdev_priv(ndev);
	unsigned long flags;
	u32 regval;

	if (netif_running(ndev)) {
		dev_err(&lp->pdev->dev,
			"Please stop netif before apply configruation\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&lp->lock, flags);
	regval = xemacps_read(lp->baseaddr, XEMACPS_NWCFG_OFFSET);

	if (epauseparm->tx_pause)
		regval |= XEMACPS_NWCFG_PAUSEEN_MASK;
	if (!(epauseparm->tx_pause))
		regval &= ~XEMACPS_NWCFG_PAUSEEN_MASK;

	xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);
	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

/**
 * xemacps_get_stats - get device statistic raw data in 64bit mode
 * @ndev: network device
 **/
static struct net_device_stats
*xemacps_get_stats(struct net_device *ndev)
{
	struct net_local *lp = netdev_priv(ndev);
	struct net_device_stats *nstat = &lp->stats;

	nstat->rx_errors +=
		(xemacps_read(lp->baseaddr, XEMACPS_RXUNDRCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXOVRCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXJABCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXFCSCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXLENGTHCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXALIGNCNT_OFFSET));
	nstat->rx_length_errors +=
		(xemacps_read(lp->baseaddr, XEMACPS_RXUNDRCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXOVRCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXJABCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_RXLENGTHCNT_OFFSET));
	nstat->rx_over_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_RXORCNT_OFFSET);
	nstat->rx_crc_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_RXFCSCNT_OFFSET);
	nstat->rx_frame_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_RXALIGNCNT_OFFSET);
	nstat->rx_fifo_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_RXORCNT_OFFSET);
	nstat->tx_errors +=
		(xemacps_read(lp->baseaddr, XEMACPS_TXURUNCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_SNGLCOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_MULTICOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_EXCESSCOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_LATECOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_CSENSECNT_OFFSET));
	nstat->tx_aborted_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_EXCESSCOLLCNT_OFFSET);
	nstat->tx_carrier_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_CSENSECNT_OFFSET);
	nstat->tx_fifo_errors +=
		xemacps_read(lp->baseaddr, XEMACPS_TXURUNCNT_OFFSET);
	nstat->collisions +=
		(xemacps_read(lp->baseaddr, XEMACPS_SNGLCOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_MULTICOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_EXCESSCOLLCNT_OFFSET) +
		xemacps_read(lp->baseaddr, XEMACPS_LATECOLLCNT_OFFSET));
	return nstat;
}

static struct ethtool_ops xemacps_ethtool_ops = {
	.get_settings   = xemacps_get_settings,
	.set_settings   = xemacps_set_settings,
	.get_drvinfo    = xemacps_get_drvinfo,
	.get_link       = ethtool_op_get_link, /* ethtool default */
	.get_ringparam  = xemacps_get_ringparam,
	.get_wol        = xemacps_get_wol,
	.set_wol        = xemacps_set_wol,
	.get_pauseparam = xemacps_get_pauseparam,
	.set_pauseparam = xemacps_set_pauseparam,
};

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
static int xemacps_hwtstamp_ioctl(struct net_device *netdev,
				struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config config;
	struct net_local *lp;
	u32 regval;

	lp = netdev_priv(netdev);

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	if ((config.tx_type != HWTSTAMP_TX_OFF) &&
		(config.tx_type != HWTSTAMP_TX_ON))
		return -ERANGE;

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
		regval = xemacps_read(lp->baseaddr, XEMACPS_NWCTRL_OFFSET);
		xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET,
			(regval | XEMACPS_NWCTRL_RXTSTAMP_MASK));
		break;
	default:
		return -ERANGE;
	}

	config.tx_type = HWTSTAMP_TX_ON;
	lp->hwtstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}
#endif /* CONFIG_XILINX_PS_EMAC_HWTSTAMP */

/**
 * xemacps_ioctl - ioctl entry point
 * @ndev: network device
 * @rq: interface request ioctl
 * @cmd: command code
 *
 * Called when user issues an ioctl request to the network device.
 **/
static int xemacps_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct net_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(phydev, rq, cmd);
#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	case SIOCSHWTSTAMP:
		return xemacps_hwtstamp_ioctl(ndev, rq, cmd);
#endif
	default:
		dev_info(&lp->pdev->dev, "ioctl %d not implemented.\n", cmd);
		return -EOPNOTSUPP;
	}

}

/**
 * xemacps_probe - Platform driver probe
 * @pdev: Pointer to platform device structure
 *
 * Return 0 on success, negative value if error
 */
static int __devinit xemacps_probe(struct platform_device *pdev)
{
	struct resource *r_mem = NULL;
	struct resource *r_irq = NULL;
	struct net_device *ndev;
	struct net_local *lp;
	struct device_node *np;
	const void *prop;
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

	dev_dbg(&lp->pdev->dev, "BASEADDRESS hw: %p virt: %p\n",
			(void *)r_mem->start, lp->baseaddr);

	ndev->irq = platform_get_irq(pdev, 0);

	rc = request_irq(ndev->irq, xemacps_interrupt, 0,
		ndev->name, ndev);
	if (rc) {
		dev_err(&lp->pdev->dev, "Unable to request IRQ %p, error %d\n",
				r_irq, rc);
		goto err_out_iounmap;
	}

	ndev->netdev_ops = &netdev_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->ethtool_ops = &xemacps_ethtool_ops;
	ndev->base_addr = r_mem->start;
	ndev->features = NETIF_F_IP_CSUM;
	netif_napi_add(ndev, &lp->napi, xemacps_rx_poll, XEMACPS_NAPI_WEIGHT);

	lp->ip_summed = CHECKSUM_UNNECESSARY;
	lp->board_type = BOARD_TYPE_ZYNQ;

	rc = register_netdev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_irq;
	}

	if (ndev->irq == 54)
		lp->enetnum = 0;
	else
		lp->enetnum = 1;

	np = of_get_next_parent(lp->pdev->dev.of_node);
	np = of_get_next_parent(np);
	prop = of_get_property(np, "compatible", NULL);

	if (prop != NULL) {
		if ((strcmp((const char *)prop, "xlnx,zynq-ep107")) == 0)
			lp->board_type = BOARD_TYPE_PEEP;
		else
			lp->board_type = BOARD_TYPE_ZYNQ;
	} else {
		lp->board_type = BOARD_TYPE_ZYNQ;
	}
	if (lp->board_type == BOARD_TYPE_ZYNQ) {
		if (lp->enetnum == 0)
			lp->aperclk = clk_get_sys("GEM0_APER", NULL);
		else
			lp->aperclk = clk_get_sys("GEM1_APER", NULL);
		if (IS_ERR(lp->aperclk)) {
			dev_err(&pdev->dev, "APER clock not found.\n");
			rc = PTR_ERR(lp->aperclk);
			goto err_out_unregister_netdev;
		}
		if (lp->enetnum == 0)
			lp->devclk = clk_get_sys("GEM0", NULL);
		else
			lp->devclk = clk_get_sys("GEM1", NULL);
		if (IS_ERR(lp->devclk)) {
			dev_err(&pdev->dev, "Device clock not found.\n");
			rc = PTR_ERR(lp->devclk);
			goto err_out_clk_put_aper;
		}

		rc = clk_prepare_enable(lp->aperclk);
		if (rc) {
			dev_err(&pdev->dev, "Unable to enable APER clock.\n");
			goto err_out_clk_put;
		}
		rc = clk_prepare_enable(lp->devclk);
		if (rc) {
			dev_err(&pdev->dev, "Unable to enable device clock.\n");
			goto err_out_clk_dis_aper;
		}

		lp->clk_rate_change_nb.notifier_call = xemacps_clk_notifier_cb;
		lp->clk_rate_change_nb.next = NULL;
		if (clk_notifier_register(lp->devclk, &lp->clk_rate_change_nb))
			dev_warn(&pdev->dev,
				"Unable to register clock notifier.\n");
	}

#ifdef CONFIG_XILINX_PS_EMAC_HWTSTAMP
	if (lp->board_type == BOARD_TYPE_ZYNQ) {
		prop = of_get_property(lp->pdev->dev.of_node,
					"xlnx,ptp-enet-clock", NULL);
		if (prop)
			lp->ptpenetclk = (u32)be32_to_cpup(prop);
		else
			lp->ptpenetclk = 133333328;
	} else {
		lp->ptpenetclk = PEEP_TSU_CLK;
	}
#endif

	lp->phy_node = of_parse_phandle(lp->pdev->dev.of_node,
						"phy-handle", 0);

	if (lp->board_type == BOARD_TYPE_ZYNQ) {
		/* Set MDIO clock divider */
		regval = (MDC_DIV_224 << XEMACPS_NWCFG_MDC_SHIFT_MASK);
		xemacps_write(lp->baseaddr, XEMACPS_NWCFG_OFFSET, regval);
	}

	regval = XEMACPS_NWCTRL_MDEN_MASK;
	xemacps_write(lp->baseaddr, XEMACPS_NWCTRL_OFFSET, regval);

	rc = xemacps_mii_init(lp);
	if (rc) {
		dev_err(&lp->pdev->dev, "error in xemacps_mii_init\n");
		goto err_out_unregister_clk_notifier;
	}

	xemacps_update_hwaddr(lp);

	platform_set_drvdata(pdev, ndev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dev_info(&lp->pdev->dev, "pdev->id %d, baseaddr 0x%08lx, irq %d\n",
		pdev->id, ndev->base_addr, ndev->irq);

	return 0;

err_out_unregister_clk_notifier:
	clk_notifier_unregister(lp->devclk, &lp->clk_rate_change_nb);
	clk_disable_unprepare(lp->devclk);
err_out_clk_dis_aper:
	clk_disable_unprepare(lp->aperclk);
err_out_clk_put:
	clk_put(lp->devclk);
err_out_clk_put_aper:
	clk_put(lp->aperclk);
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
 * xemacps_remove - called when platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * return: 0 on success
 */
static int __exit xemacps_remove(struct platform_device *pdev)
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

		clk_notifier_unregister(lp->devclk, &lp->clk_rate_change_nb);
		clk_disable_unprepare(lp->devclk);
		clk_put(lp->devclk);
		clk_disable_unprepare(lp->aperclk);
		clk_put(lp->aperclk);
	}

	return 0;
}

#ifdef CONFIG_PM_NOT_DEFINE
#ifdef CONFIG_PM_SLEEP
/**
 * xemacps_suspend - Suspend event
 * @device: Pointer to device structure
 *
 * Return 0
 */
static int xemacps_suspend(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	netif_device_detach(ndev);
	if (!pm_runtime_suspended(device)) {
		clk_disable(lp->devclk);
		clk_disable(lp->aperclk);
	}
	return 0;
}

/**
 * xemacps_resume - Resume after previous suspend
 * @pdev: Pointer to platform device structure
 *
 * Returns 0 on success, errno otherwise.
 */
static int xemacps_resume(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	if (!pm_runtime_suspended(device)) {
		int ret;

		ret = clk_enable(lp->aperclk);
		if (ret)
			return ret;

		ret = clk_enable(lp->devclk);
		if (ret) {
			clk_disable(lp->aperclk);
			return ret;
		}
	}
	netif_device_attach(ndev);
	return 0;
}
#endif /* ! CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int xemacps_runtime_idle(struct device *dev)
{
	return pm_schedule_suspend(dev, 1);
}

static int xemacps_runtime_resume(struct device *device)
{
	int ret;
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	ret = clk_enable(lp->aperclk);
	if (ret)
		return ret;

	ret = clk_enable(lp->devclk);
	if (ret) {
		clk_disable(lp->aperclk);
		return ret;
	}

	return 0;
}

static int xemacps_runtime_suspend(struct device *device)
{
	struct platform_device *pdev = container_of(device,
			struct platform_device, dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct net_local *lp = netdev_priv(ndev);

	clk_disable(lp->devclk);
	clk_disable(lp->aperclk);
	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops xemacps_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xemacps_suspend, xemacps_resume)
	SET_RUNTIME_PM_OPS(xemacps_runtime_suspend, xemacps_runtime_resume,
			xemacps_runtime_idle)
};
#define XEMACPS_PM	(&xemacps_dev_pm_ops)
#else /* ! CONFIG_PM */
#define XEMACPS_PM	NULL
#endif /* ! CONFIG_PM */

static struct net_device_ops netdev_ops = {
	.ndo_open		= xemacps_open,
	.ndo_stop		= xemacps_close,
	.ndo_start_xmit		= xemacps_start_xmit,
	.ndo_set_rx_mode	= xemacps_set_rx_mode,
	.ndo_set_mac_address    = xemacps_set_mac_address,
	.ndo_do_ioctl		= xemacps_ioctl,
	.ndo_change_mtu		= xemacps_change_mtu,
	.ndo_tx_timeout		= xemacps_tx_timeout,
	.ndo_get_stats		= xemacps_get_stats,
};

static struct of_device_id xemacps_of_match[] __devinitdata = {
	{ .compatible = "xlnx,ps7-ethernet-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xemacps_of_match);

static struct platform_driver xemacps_driver = {
	.probe   = xemacps_probe,
	.remove  = __exit_p(xemacps_remove),
	.driver  = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = xemacps_of_match,
		.pm = XEMACPS_PM,
	},
};

module_platform_driver(xemacps_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Ethernet driver");
MODULE_LICENSE("GPL v2");
