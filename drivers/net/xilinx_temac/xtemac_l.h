/* $Id: */
/******************************************************************************
*
*       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
*       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
*       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
*       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
*       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
*       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
*       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
*       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
*       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
*       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
*       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
*       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*       FOR A PARTICULAR PURPOSE.
*
*       (c) Copyright 2004-2006 Xilinx Inc.
*       All rights reserved.
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xtemac_l.h
*
* This header file contains identifiers and low-level driver functions (or
* macros) that can be used to access the Tri-Mode MAC Ethernet (TEMAC) device.
* High-level driver functions are defined in xtemac.h.
*
* @note
*
* Some registers are not accessible when a HW instance is configured for SGDMA.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rmm  05/24/04 First release for early access.
* 1.00a rmm  06/01/05 General release
* 1.00b rmm  09/23/05 Added MII interrupt bit definitions, removed IFGP register
*                     and associated bit masks, added MGT register and
*                     associated bit masks, removed DIID register, renamed host
*                     register names to match those in the latest HW spec.
* 2.00a rmm  11/21/05 Modified to match HW 3.00a
* </pre>
*
******************************************************************************/

#ifndef XTEMAC_L_H		/* prevent circular inclusions */
#define XTEMAC_L_H		/* by using protection macros */

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"
#include "xdmav3_l.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************** Constant Definitions *****************************/

#define XTE_PLB_BD_ALIGNMENT  4	     /**< Minimum buffer descriptor alignment
                                          on the PLB bus */
#define XTE_PLB_BUF_ALIGNMENT 8	     /**< Minimum buffer alignment when using
                                          HW options that impose alignment
                                          restrictions on the buffer data on
                                          the PLB bus */

#define XTE_RESET_IPIF_DELAY_US 1    /**< Number of Us to delay after IPIF
                                          reset */
#define XTE_RESET_HARD_DELAY_US 4    /**< Number of Us to delay after hard core
                                          reset */

/* Register offset definitions. Unless otherwise noted, register access is
 * 32 bit.
 */

/** @name IPIF interrupt and reset registers
 *  @{
 */
#define XTE_DISR_OFFSET  0x00000000  /**< Device interrupt status */
#define XTE_DIPR_OFFSET  0x00000004  /**< Device interrupt pending */
#define XTE_DIER_OFFSET  0x00000008  /**< Device interrupt enable */
#define XTE_DGIE_OFFSET  0x0000001C  /**< Device global interrupt enable */
#define XTE_IPISR_OFFSET 0x00000020  /**< IP interrupt status */
#define XTE_IPIER_OFFSET 0x00000028  /**< IP interrupt enable */
#define XTE_DSR_OFFSET   0x00000040  /**< Device software reset (write) */
#define XTE_MIR_OFFSET   0x00000040  /**< Device software reset (read) */
/*@}*/


/** @name IPIF transmit and receive packet fifo base offsets
 *        Individual registers and bit definitions are defined in
 *        xpacket_fifo_l_v2_00_a.h. This register group is not accessible if
 *        the device instance is configured for SGDMA.
 *  @{
 */
#define XTE_PFIFO_TXREG_OFFSET   0x00002000  /**< Packet FIFO Tx channel */
#define XTE_PFIFO_RXREG_OFFSET   0x00002010  /**< Packet FIFO Rx channel */
/*@}*/


/** @name IPIF transmit and receive packet fifo data offsets. This register
 *        group is not accessible if the device instance is configured for
 *        SGDMA.
 *  @{
 */
#define XTE_PFIFO_TXDATA_OFFSET  0x00002100  /**< IPIF Tx packet fifo port */
#define XTE_PFIFO_RXDATA_OFFSET  0x00002200  /**< IPIF Rx packet fifo port */
/*@}*/


/** @name IPIF transmit and recieve DMA offsets
 *        Individual registers and bit definitions are defined in xdmav3.h.
 *        This register group is not accessible if the device instance is
 *        configured for FIFO direct.
 *  @{
 */
#define XTE_DMA_SEND_OFFSET      0x00002300  /**< DMA Tx channel */
#define XTE_DMA_RECV_OFFSET      0x00002340  /**< DMA Rx channel */
/*@}*/


/** @name PLB_TEMAC registers. The TPLR, TSR, RPLR, and RSR are not accessible
 *        when a device instance is configured for SGDMA. LLPS is not accessible
 *        when a device instance is configured for FIFO direct.
 *  @{
 */
#define XTE_CR_OFFSET           0x00001000  /**< Control */
#define XTE_TPLR_OFFSET         0x00001004  /**< Tx packet length (FIFO) */
#define XTE_TSR_OFFSET          0x00001008  /**< Tx status (FIFO) */
#define XTE_RPLR_OFFSET         0x0000100C  /**< Rx packet length (FIFO) */
#define XTE_RSR_OFFSET          0x00001010  /**< Receive status */
#define XTE_TPPR_OFFSET         0x00001014  /**< Tx pause packet */
#define XTE_LLPS_OFFSET         0x00001018  /**< LLINK PFIFO status */
#define XTE_MGTDR_OFFSET        0x000033B0  /**< MII data */
#define XTE_MGTCR_OFFSET        0x000033B4  /**< MII control */
/*@}*/


/** @name HARD_TEMAC Core Registers
 * These are registers defined within the device's hard core located in the
 * processor block. They are accessed with the host interface. These registers
 * are addressed offset by XTE_HOST_IPIF_OFFSET or by the DCR base address
 * if so configured.
 *
 * Access to these registers should go through macros XTemac_mReadHostReg()
 * and XTemac_mWriteHostReg() to guarantee proper access.
 * @{
 */
#define XTE_HOST_IPIF_OFFSET    0x00003000  /**< Offset of host registers when
                                                 memory mapped into IPIF */
#define XTE_RXC0_OFFSET         0x00000200  /**< Rx configuration word 0 */
#define XTE_RXC1_OFFSET         0x00000240  /**< Rx configuration word 1 */
#define XTE_TXC_OFFSET          0x00000280  /**< Tx configuration */
#define XTE_FCC_OFFSET          0x000002C0  /**< Flow control configuration */
#define XTE_EMCFG_OFFSET        0x00000300  /**< EMAC configuration */
#define XTE_GMIC_OFFSET         0x00000320  /**< RGMII/SGMII configuration */
#define XTE_MC_OFFSET           0x00000340  /**< Management configuration */
#define XTE_UAW0_OFFSET         0x00000380  /**< Unicast address word 0 */
#define XTE_UAW1_OFFSET         0x00000384  /**< Unicast address word 1 */
#define XTE_MAW0_OFFSET         0x00000388  /**< Multicast address word 0 */
#define XTE_MAW1_OFFSET         0x0000038C  /**< Multicast address word 1 */
#define XTE_AFM_OFFSET          0x00000390  /**< Promisciuous mode */
/*@}*/


/* Register masks. The following constants define bit locations of various
 * control bits in the registers. Constants are not defined for those registers
 * that have a single bit field representing all 32 bits. For further
 * information on the meaning of the various bit masks, refer to the HW spec.
 */

/** @name Interrupt status bits for top level interrupts
 *  These bits are associated with the XTE_DISR_OFFSET, XTE_DIPR_OFFSET,
 *  and XTE_DIER_OFFSET registers.
 * @{
 */
#define XTE_DXR_SEND_FIFO_MASK          0x00000040 /**< Send FIFO channel */
#define XTE_DXR_RECV_FIFO_MASK          0x00000020 /**< Receive FIFO channel */
#define XTE_DXR_CORE_MASK               0x00000004 /**< Core */
#define XTE_DXR_DPTO_MASK               0x00000002 /**< Data phase timeout */
#define XTE_DXR_TERR_MASK               0x00000001 /**< Transaction error */
/*@}*/

/** @name Interrupt status bits for MAC interrupts
 *  These bits are associated with XTE_IPISR_OFFSET and XTE_IPIER_OFFSET
 *  registers.
 *
 *  @{
 */
#define XTE_IPXR_XMIT_DONE_MASK         0x00000001 /**< Tx complete */
#define XTE_IPXR_RECV_DONE_MASK         0x00000002 /**< Rx complete */
#define XTE_IPXR_AUTO_NEG_MASK          0x00000004 /**< Auto negotiation complete */
#define XTE_IPXR_RECV_REJECT_MASK       0x00000008 /**< Rx packet rejected */
#define XTE_IPXR_XMIT_SFIFO_EMPTY_MASK  0x00000010 /**< Tx status fifo empty */
#define XTE_IPXR_RECV_LFIFO_EMPTY_MASK  0x00000020 /**< Rx length fifo empty */
#define XTE_IPXR_XMIT_LFIFO_FULL_MASK   0x00000040 /**< Tx length fifo full */
#define XTE_IPXR_RECV_LFIFO_OVER_MASK   0x00000080 /**< Rx length fifo overrun
                                                        Note that this signal is
                                                        no longer asserted by HW
                                                        */
#define XTE_IPXR_RECV_LFIFO_UNDER_MASK  0x00000100 /**< Rx length fifo underrun */
#define XTE_IPXR_XMIT_SFIFO_OVER_MASK   0x00000200 /**< Tx status fifo overrun */
#define XTE_IPXR_XMIT_SFIFO_UNDER_MASK  0x00000400 /**< Tx status fifo underrun */
#define XTE_IPXR_XMIT_LFIFO_OVER_MASK   0x00000800 /**< Tx length fifo overrun */
#define XTE_IPXR_XMIT_LFIFO_UNDER_MASK  0x00001000 /**< Tx length fifo underrun */
#define XTE_IPXR_RECV_PFIFO_ABORT_MASK  0x00002000 /**< Rx packet rejected due to
                                                        full packet FIFO */
#define XTE_IPXR_RECV_LFIFO_ABORT_MASK  0x00004000 /**< Rx packet rejected due to
                                                        full length FIFO */
#define XTE_IPXR_MII_PEND_MASK          0x00008000 /**< Mii operation now
                                                        pending */
#define XTE_IPXR_MII_DONE_MASK          0x00010000 /**< Mii operation has
                                                        completed */
#define XTE_IPXR_XMIT_PFIFO_UNDER_MASK  0x00020000 /**< Tx packet FIFO
                                                        underrun */
#define XTE_IPXR_XMIT_DMA_MASK          0x00080000 /**< Rx dma channel */
#define XTE_IPXR_RECV_DMA_MASK          0x00100000 /**< Tx dma channel */
#define XTE_IPXR_RECV_FIFO_LOCK_MASK    0x00200000 /**< Rx FIFO deadlock */
#define XTE_IPXR_XMIT_FIFO_LOCK_MASK    0x00400000 /**< Tx FIFO deadlock */


#define XTE_IPXR_RECV_DROPPED_MASK                                      \
    (XTE_IPXR_RECV_REJECT_MASK |                                        \
     XTE_IPXR_RECV_PFIFO_ABORT_MASK |                                   \
     XTE_IPXR_RECV_LFIFO_ABORT_MASK)	/**< IPXR bits that indicate a dropped
                                             receive frame */

#define XTE_IPXR_XMIT_ERROR_MASK                                        \
    (XTE_IPXR_XMIT_SFIFO_OVER_MASK |                                    \
     XTE_IPXR_XMIT_SFIFO_UNDER_MASK |                                   \
     XTE_IPXR_XMIT_LFIFO_OVER_MASK |                                    \
     XTE_IPXR_XMIT_LFIFO_UNDER_MASK |                                   \
     XTE_IPXR_XMIT_PFIFO_UNDER_MASK)	/**< IPXR bits that indicate transmit
                                             errors */

#define XTE_IPXR_RECV_ERROR_MASK                                        \
    (XTE_IPXR_RECV_DROPPED_MASK |                                       \
     XTE_IPXR_RECV_LFIFO_UNDER_MASK)	/**< IPXR bits that indicate receive
                                             errors */

#define XTE_IPXR_FIFO_FATAL_ERROR_MASK                                  \
    (XTE_IPXR_RECV_FIFO_LOCK_MASK |                                     \
     XTE_IPXR_XMIT_FIFO_LOCK_MASK |                                     \
     XTE_IPXR_XMIT_SFIFO_OVER_MASK |                                    \
     XTE_IPXR_XMIT_SFIFO_UNDER_MASK |                                   \
     XTE_IPXR_XMIT_LFIFO_OVER_MASK |                                    \
     XTE_IPXR_XMIT_LFIFO_UNDER_MASK |                                   \
     XTE_IPXR_XMIT_PFIFO_UNDER_MASK |                                   \
     XTE_IPXR_RECV_LFIFO_UNDER_MASK)	/**< IPXR bits that indicate fatal FIFO
                                             errors. These bits can only be
                                             cleared by a device reset */
/*@}*/


/** @name Software reset register (DSR)
 *  @{
 */
#define XTE_DSR_RESET_MASK      0x0000000A  /**< Write this value to DSR to
                                                 reset entire core */
/*@}*/


/** @name Global interrupt enable register (DGIE)
 *  @{
 */
#define XTE_DGIE_ENABLE_MASK    0x80000000  /**< Write this value to DGIE to
                                                 enable interrupts from this
                                                 device */
/*@}*/

/** @name Control Register (CR)
 *  @{
 */
#define XTE_CR_BCREJ_MASK       0x00000004   /**< Disable broadcast address
                                                  filtering */
#define XTE_CR_MCREJ_MASK       0x00000002   /**< Disable multicast address
                                                  filtering */
#define XTE_CR_HRST_MASK        0x00000001   /**< Reset the hard TEMAC core */
/*@}*/


/** @name Transmit Packet Length Register (TPLR)
 *  @{
 */
#define XTE_TPLR_TXPL_MASK      0x00003FFF   /**< Tx packet length in bytes */
/*@}*/


/** @name Transmit Status Register (TSR)
 *  @{
 */
#define XTE_TSR_TPCF_MASK       0x00000001   /**< Transmit packet complete
                                                  flag */

/*@}*/


/** @name Receive Packet Length Register (RPLR)
 *  @{
 */
#define XTE_RPLR_RXPL_MASK      0x00003FFF   /**< Rx packet length in bytes */
/*@}*/


/** @name Receive Status Register (RSR)
 * @{
 */
#define XTE_RSR_RPCF_MASK       0x00000001   /**< Receive packet complete
                                                  flag */
/*@}*/


/** @name MII Mamagement Data register (MGTDR)
 *  @{
 */
#define XTE_MGTDR_MIID_MASK     0x0000FFFF   /**< MII data */
/*@}*/


/** @name MII Mamagement Control register (MGTCR)
 *  @{
 */
#define XTE_MGTCR_RWN_MASK      0x00000400  /**< Read-not-write,0=read
                                                 1=write */
#define XTE_MGTCR_PHYAD_MASK    0x000003E0  /**< PHY address */
#define XTE_MGTCR_REGAD_MASK    0x0000001F  /**< PHY register address */

#define XTE_MGTCR_PHYAD_SHIFT_MASK       5  /**< Shift bits for PHYAD */
/*@}*/


/** @name Transmit Pause Packet Register (TPPR)
 *  @{
 */
#define XTE_TPPR_TPPD_MASK      0x0000FFFF   /**< Tx pause packet data */
/*@}*/


/** @name Receiver Configuration Word 1 (RXC1)
 *  @{
 */
#define XTE_RXC1_RXRST_MASK     0x80000000   /**< Receiver reset */
#define XTE_RXC1_RXJMBO_MASK    0x40000000   /**< Jumbo frame enable */
#define XTE_RXC1_RXFCS_MASK     0x20000000   /**< FCS not stripped */
#define XTE_RXC1_RXEN_MASK      0x10000000   /**< Receiver enable */
#define XTE_RXC1_RXVLAN_MASK    0x08000000   /**< VLAN enable */
#define XTE_RXC1_RXHD_MASK      0x04000000   /**< Half duplex */
#define XTE_RXC1_RXLT_MASK      0x02000000   /**< Length/type check disable */
#define XTE_RXC1_ERXC1_MASK     0x0000FFFF   /**< Pause frame source address
                                                  bits [47:32]. Bits [31:0]
                                                  are stored in register
                                                  ERXC0 */
/*@}*/


/** @name Transmitter Configuration (TXC)
 *  @{
 */
#define XTE_TXC_TXRST_MASK      0x80000000   /**< Transmitter reset */
#define XTE_TXC_TXJMBO_MASK     0x40000000   /**< Jumbo frame enable */
#define XTE_TXC_TXFCS_MASK      0x20000000   /**< Generate FCS */
#define XTE_TXC_TXEN_MASK       0x10000000   /**< Transmitter enable */
#define XTE_TXC_TXVLAN_MASK     0x08000000   /**< VLAN enable */
#define XTE_TXC_TXHD_MASK       0x04000000   /**< Half duplex */
#define XTE_TXC_TXIFG_MASK      0x02000000   /**< IFG adjust enable */
/*@}*/


/** @name Flow Control Configuration (FCC)
 *  @{
 */
#define XTE_FCC_RXFLO_MASK      0x20000000   /**< Rx flow control enable */
#define XTE_FCC_TXFLO_MASK      0x40000000   /**< Tx flow control enable */
/*@}*/


/** @name EMAC Configuration (EMCFG)
 * @{
 */
#define XTE_EMCFG_LINKSPD_MASK   0xC0000000  /**< Link speed */
#define XTE_EMCFG_RGMII_MASK     0x20000000  /**< RGMII mode enable */
#define XTE_EMCFG_SGMII_MASK     0x10000000  /**< SGMII mode enable */
#define XTE_EMCFG_1000BASEX_MASK 0x08000000  /**< 1000BaseX mode enable */
#define XTE_EMCFG_HOSTEN_MASK    0x04000000  /**< Host interface enable */
#define XTE_EMCFG_TX16BIT        0x02000000  /**< 16 bit Tx client enable */
#define XTE_EMCFG_RX16BIT        0x01000000  /**< 16 bit Rx client enable */

#define XTE_EMCFG_LINKSPD_10     0x00000000   /**< XTE_EMCFG_LINKSPD_MASK for
                                                   10 Mbit */
#define XTE_EMCFG_LINKSPD_100    0x40000000   /**< XTE_EMCFG_LINKSPD_MASK for
                                                   100 Mbit */
#define XTE_EMCFG_LINKSPD_1000   0x80000000   /**< XTE_EMCFG_LINKSPD_MASK for
                                                   1000 Mbit */
/*@}*/


/** @name EMAC RGMII/SGMII Configuration (GMIC)
 * @{
 */
#define XTE_GMIC_RGLINKSPD_MASK    0xC0000000	/**< RGMII link speed */
#define XTE_GMIC_SGLINKSPD_MASK    0x0000000C	/**< SGMII link speed */
#define XTE_GMIC_RGSTATUS_MASK     0x00000002	/**< RGMII link status */
#define XTE_GMIC_RGHALFDUPLEX_MASK 0x00000001	/**< RGMII half duplex */

#define XTE_GMIC_RGLINKSPD_10      0x00000000	/**< XTE_GMIC_RGLINKSPD_MASK
                                                     for 10 Mbit */
#define XTE_GMIC_RGLINKSPD_100     0x40000000	/**< XTE_GMIC_RGLINKSPD_MASK
                                                     for 100 Mbit */
#define XTE_GMIC_RGLINKSPD_1000    0x80000000	/**< XTE_GMIC_RGLINKSPD_MASK
                                                     for 1000 Mbit */
#define XTE_GMIC_SGLINKSPD_10      0x00000000	/**< XTE_SGMIC_RGLINKSPD_MASK
                                                     for 10 Mbit */
#define XTE_GMIC_SGLINKSPD_100     0x00000004	/**< XTE_SGMIC_RGLINKSPD_MASK
                                                     for 100 Mbit */
#define XTE_GMIC_SGLINKSPD_1000    0x00000008	/**< XTE_SGMIC_RGLINKSPD_MASK
                                                     for 1000 Mbit */
/*@}*/


/** @name EMAC Management Configuration (MC)
 * @{
 */
#define XTE_MC_MDIO_MASK       0x00000040   /**< MII management enable */
#define XTE_MC_CLK_DVD_MAX     0x3F	    /**< Maximum MDIO divisor */
/*@}*/


/** @name EMAC Unicast Address Register Word 1 (UAW1)
 * @{
 */
#define XTE_UAW1_MASK          0x0000FFFF   /**< Station address bits [47:32]
                                                 Station address bits [31:0]
                                                 are stored in register
                                                 UAW0 */
/*@}*/


/** @name EMAC Multicast Address Register Word 1 (MAW1)
 * @{
 */
#define XTE_MAW1_CAMRNW_MASK   0x00800000   /**< CAM read/write control */
#define XTE_MAW1_CAMADDR_MASK  0x00030000   /**< CAM address mask */
#define XTE_MAW1_MASK          0x0000FFFF   /**< Multicast address bits [47:32]
                                                 Multicast address bits [31:0]
                                                 are stored in register
                                                 MAW0 */
#define XTE_MAW1_CAMMADDR_SHIFT_MASK 16	    /**< Number of bits to shift right
                                                 to align with
                                                 XTE_MAW1_CAMADDR_MASK */
/*@}*/


/** @name EMAC Address Filter Mode (AFM)
 * @{
 */
#define XTE_AFM_EPPRM_MASK     0x80000000   /**< Promiscuous mode enable */
/*@}*/


/** @name Checksum offload buffer descriptor extensions
 * @{
 */
/** Byte offset where checksum should begin (16 bit word) */
#define XTE_BD_TX_CSBEGIN_OFFSET  XDMAV3_BD_USR0_OFFSET

/** Offset where checksum should be inserted (16 bit word) */
#define XTE_BD_TX_CSINSERT_OFFSET (XDMAV3_BD_USR0_OFFSET + 2)

/** Checksum offload control for transmit (16 bit word) */
#define XTE_BD_TX_CSCNTRL_OFFSET  XDMAV3_BD_USR1_OFFSET

/** Seed value for checksum calculation (16 bit word) */
#define XTE_BD_TX_CSINIT_OFFSET   (XDMAV3_BD_USR1_OFFSET + 2)

/** Receive frame checksum calculation (16 bit word) */
#define XTE_BD_RX_CSRAW_OFFSET    (XDMAV3_BD_USR5_OFFSET + 2)

/*@}*/

/** @name TX_CSCNTRL bit mask
 * @{
 */
#define XTE_BD_TX_CSCNTRL_CALC_MASK  0x0001  /**< Enable/disable Tx
                                                  checksum */
/*@}*/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/


/****************************************************************************/
/**
*
* Read the given IPIF register.
*
* @param    BaseAddress is the IPIF base address of the device
* @param    RegOffset is the register offset to be read
*
* @return   The 32-bit value of the register
*
* @note
* C-style signature:
*    u32 XTemac_mReadReg(u32 BaseAddress, u32 RegOffset)
*
*****************************************************************************/
#define XTemac_mReadReg(BaseAddress, RegOffset) \
    XIo_In32((BaseAddress) + (RegOffset))


/****************************************************************************/
/**
*
* Write the given IPIF register.
*
* @param    BaseAddress is the IPIF base address of the device
* @param    RegOffset is the register offset to be written
* @param    Data is the 32-bit value to write to the register
*
* @return   None.
*
* @note
* C-style signature:
*    void XTemac_mWriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
*****************************************************************************/
#define XTemac_mWriteReg(BaseAddress, RegOffset, Data) \
    XIo_Out32((BaseAddress) + (RegOffset), (Data))

/****************************************************************************/
/**
*
* Convert host register offset to a proper DCR or memory mapped offset (DCR
* not currently supported).
*
* @param    HostRegOffset is the relative regster offset to be converted
*
* @return   The correct offset of the register
*
* @note
* C-style signature:
*    u32 XTemac_mHostOffset(u32 RegOffset)
*
*****************************************************************************/
#define XTemac_mHostOffset(HostRegOffset) \
    ((u32)(HostRegOffset) + XTE_HOST_IPIF_OFFSET)

/****************************************************************************/
/**
*
* Read the given host register.
*
* @param    BaseAddress is the base address of the device
* @param    HostRegOffset is the register offset to be read
*
* @return   The 32-bit value of the register
*
* @note
* C-style signature:
*    u32 XTemac_mReadHostReg(u32 BaseAddress, u32 HostRegOffset)
*
*****************************************************************************/
#define XTemac_mReadHostReg(BaseAddress, HostRegOffset) \
    XIo_In32((BaseAddress) + XTemac_mHostOffset(HostRegOffset))


/****************************************************************************/
/**
*
* Write the given host register.
*
* @param    BaseAddress is the base address of the device
* @param    HostRegOffset is the register offset to be written
* @param    Data is the 32-bit value to write to the register
*
* @return   None.
*
* C-style signature:
*    void XTemac_mWriteHostReg(u32 BaseAddress, u32 RegOffset,
*                              u32 Data)
*
*****************************************************************************/
#define XTemac_mWriteHostReg(BaseAddress, HostRegOffset, Data) \
    XIo_Out32((BaseAddress) + XTemac_mHostOffset(HostRegOffset), (Data))


/****************************************************************************/
/**
*
* Set the station address.
*
* @param    BaseAddress is the base address of the device
* @param    AddressPtr is a pointer to a 6-byte MAC address
*
* @return   None.
*
* @note
* C-style signature:
*    u32 XTemac_mSetMacAddress(u32 BaseAddress, u8 *AddressPtr)
*
*****************************************************************************/
#define XTemac_mSetMacAddress(BaseAddress, AddressPtr)              \
{                                                                   \
    u32 Reg;                                                    \
    u8* Aptr = (u8*)(AddressPtr);                           \
                                                                    \
    Reg =  Aptr[0] & 0x000000FF;                                    \
    Reg |= Aptr[1] << 8;                                            \
    Reg |= Aptr[2] << 16;                                           \
    Reg |= Aptr[3] << 24;                                           \
    XTemac_mWriteHostReg((BaseAddress), XTE_UAW0_OFFSET, Reg);      \
                                                                    \
    Reg = XTemac_mReadHostReg((BaseAddress), XTE_UAW1_OFFSET);      \
    Reg &= ~XTE_UAW1_MASK;                                          \
    Reg |= Aptr[4] & 0x000000FF;                                    \
    Reg |= Aptr[5] << 8;                                            \
    XTemac_mWriteHostReg((BaseAddress), XTE_UAW1_OFFSET, Reg);      \
}


/****************************************************************************/
/**
*
* Check to see if the transmission is complete.
*
* @param    BaseAddress is the base address of the device
*
* @return   TRUE if it is done, or FALSE if it is not.
*
* @note
* C-style signature:
*    XBoolean XTemac_mIsTxDone(u32 BaseAddress)
*
*****************************************************************************/
#define XTemac_mIsTxDone(BaseAddress)                                   \
    (((XIo_In32((BaseAddress) + XTE_IPISR_OFFSET) & XTE_IPXR_XMIT_DONE_MASK) == \
      XTE_IPXR_XMIT_DONE_MASK) ? TRUE : FALSE)


/****************************************************************************/
/**
*
* Check to see if the receive FIFO is empty.
*
* @param    BaseAddress is the base address of the device
*
* @return   TRUE if it is empty, or FALSE if it is not.
*
* @note
* C-style signature:
*    XBoolean XTemac_mIsRxEmpty(u32 BaseAddress)
*
*****************************************************************************/
#define XTemac_mIsRxEmpty(BaseAddress)                                  \
    ((XIo_In32((BaseAddress) + XTE_IPISR_OFFSET) & XTE_IPXR_RECV_DONE_MASK) \
     ? FALSE : TRUE)


/****************************************************************************/
/**
*
* Reset the entire core including any attached PHY. Note that there may be a
* settling time required after initiating a reset. See the core spec and the
* PHY datasheet.
*
* @param    BaseAddress is the base address of the device
*
* @return   Nothing
*
* @note
* C-style signature:
*    void XTemac_mReset(u32 BaseAddress)
*
*****************************************************************************/
#define XTemac_mReset(BaseAddress)                                      \
    XIo_Out32((BaseAddress) + XTE_DSR_OFFSET, XTE_DSR_RESET_MASK)


/************************** Function Prototypes ******************************/

void XTemac_Enable(u32 BaseAddress);
void XTemac_Disable(u32 BaseAddress);
int XTemac_SendFrame(u32 BaseAddress, void *FramePtr, int Size);
int XTemac_RecvFrame(u32 BaseAddress, void *FramePtr);

#ifdef __cplusplus
  }
#endif

#endif /* end of protection macro */
