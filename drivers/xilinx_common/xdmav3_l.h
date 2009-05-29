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
*       (c) Copyright 2006 Xilinx Inc.
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
* @file xdmav3_l.h
*
* This header file contains identifiers and low-level driver functions (or
* macros) that can be used to access the Direct Memory Access and Scatter
* Gather (SG DMA) device.
*
* For more information about the operation of this device, see the hardware
* specification and documentation in the higher level driver xdma.h source
* code file.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 3.00a rmm  03/11/06 First release
*       rmm  06/22/06 Added extern "C"
* </pre>
*
******************************************************************************/

#ifndef XDMAV3_L_H		/* prevent circular inclusions */
#define XDMAV3_L_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xio.h"

/************************** Constant Definitions *****************************/


/* Register offset definitions. Unless otherwise noted, register access is
 * 32 bit.
 */

/** @name DMA channel registers
 *  @{
 */
#define XDMAV3_DMASR_OFFSET  0x00000000	 /**< DMA Status Register */
#define XDMAV3_DMACR_OFFSET  0x00000004	 /**< DMA Control Register */
#define XDMAV3_MSBA_OFFSET   0x00000008	 /**< Most Significant Bus Address */
#define XDMAV3_LSBA_OFFSET   0x0000000C	 /**< Least Significant Bus Address */
#define XDMAV3_BDA_OFFSET    0x00000010	 /**< Buffer Descriptor Address */
#define XDMAV3_LENGTH_OFFSET 0x00000014	 /**< DMA Length */
#define XDMAV3_ISR_OFFSET    0x00000018	 /**< Interrupt Status Register */
#define XDMAV3_IER_OFFSET    0x0000001C	 /**< Interrupt Enable Register */
#define XDMAV3_SWCR_OFFSET   0x00000020	 /**< Software Control Register */
/*@}*/

/** @name Buffer Descriptor register offsets
 *  @{
 */
#define XDMAV3_BD_DMASR_OFFSET     0x00	 /**< Channel DMASR register contents */
#define XDMAV3_BD_DMACR_OFFSET     0x04	 /**< Channel DMACR register contents */
#define XDMAV3_BD_MSBA_OFFSET      0x08	 /**< Channel MSBA register contents */
#define XDMAV3_BD_LSBA_OFFSET      0x0C	 /**< Channel LSBA register contents */
#define XDMAV3_BD_BDA_OFFSET       0x10	 /**< Next buffer descriptor pointer */
#define XDMAV3_BD_LENGTH_OFFSET    0x14	 /**< Channel LENGTH register contents */
#define XDMAV3_BD_SR_OFFSET        0x18	 /**< Packet Status */
#define XDMAV3_BD_RSVD_OFFSET      0x1C	 /**< Reserved */
#define XDMAV3_BD_USR0_OFFSET      0x20	 /**< HW User defined */
#define XDMAV3_BD_USR1_OFFSET      0x24	 /**< HW User defined */
#define XDMAV3_BD_USR2_OFFSET      0x28	 /**< HW User defined */
#define XDMAV3_BD_USR3_OFFSET      0x2C	 /**< HW User defined */
#define XDMAV3_BD_USR4_OFFSET      0x30	 /**< HW User defined */
#define XDMAV3_BD_USR5_OFFSET      0x34	 /**< HW User defined */
#define XDMAV3_BD_LENCPY_OFFSET    0x38	 /**< SW Driver usage */
#define XDMAV3_BD_ID_OFFSET        0x3C	 /**< SW Driver usage */

#define XDMAV3_BD_NUM_WORDS        16	 /**< Number of 32-bit words that make
                                              up a BD */
/*@}*/

/* Register masks. The following constants define bit locations of various
 * control bits in the registers. Constants are not defined for those registers
 * that have a single bit field representing all 32 bits. For further
 * information on the meaning of the various bit masks, refer to the HW spec.
 */


/** @name DMA Status Register (DMASR) bitmasks
 *  @note These bitmasks are identical between XDMAV3_DMASR_OFFSET and
 *  XDMAV3_BD_DMASR_OFFSET
 * @{
 */
#define XDMAV3_DMASR_DMABSY_MASK  0x80000000  /**< DMA busy */
#define XDMAV3_DMASR_DBE_MASK     0x40000000  /**< Bus error */
#define XDMAV3_DMASR_DBT_MASK     0x20000000  /**< Bus timeout */
#define XDMAV3_DMASR_DMADONE_MASK 0x10000000  /**< DMA done */
#define XDMAV3_DMASR_SGBSY_MASK   0x08000000  /**< SG channel busy */
#define XDMAV3_DMASR_LAST_MASK    0x04000000  /**< Last BD of packet */
#define XDMAV3_DMASR_SGDONE_MASK  0x01000000  /**< SGDMA done */
#define XDMAV3_DMASR_DMACNFG_MASK 0x00300000  /**< DMA configuration */

#define XDMAV3_DMASR_DMACNFG_SIMPLE_MASK  0x00000000  /**< Simple DMA config */
#define XDMAV3_DMASR_DMACNFG_SSGDMA_MASK  0x00100000  /**< Simple SGDMA config */
#define XDMAV3_DMASR_DMACNFG_SGDMATX_MASK 0x00200000  /**< SGDMA xmit config */
#define XDMAV3_DMASR_DMACNFG_SGDMARX_MASK 0x00300000  /**< SGDMA recv config */
#define XDMAV3_DMASR_DMACNFG_MASK         0x00300000  /**< Mask for all */

/*@}*/

/** @name DMA Control Register (DMACR) bitmasks
 *  @note These bitmasks are identical between XDMAV3_DMACR_OFFSET and
 *  XDMAV3_BD_DMACR_OFFSET
 * @{
 */
#define XDMAV3_DMACR_AINC_MASK    0x80000000  /**< Address increment */
#define XDMAV3_DMACR_BPDRE_MASK   0x20000000  /**< Bypass DRE */
#define XDMAV3_DMACR_SGS_MASK     0x08000000  /**< Scatter gather stop */
#define XDMAV3_DMACR_LAST_MASK    0x04000000  /**< Last BD of packet */
#define XDMAV3_DMACR_DEVSEL_MASK  0x00FF0000  /**< Device select */
#define XDMAV3_DMACR_BDPAGE_MASK  0x00000F00  /**< BD page address */
#define XDMAV3_DMACR_TYPE_MASK    0x00000070  /**< DMA transfer type */
#define XDMAV3_DMACR_DSIZE_MASK   0x00000007  /**< DMA transfer width */

/* Sub-fields within XDMAV3_DMACR_DIR_MASK */
#define XDMAV3_DMACR_DIR_RX_MASK       0x40000000  /**< Xfer in Rx direction */
#define XDMAV3_DMACR_DIR_TX_MASK       0x00000000  /**< Xfer in Tx direction */

/* Sub-fields within XDMAV3_DMACR_TYPE_MASK */
#define XDMAV3_DMACR_TYPE_BFBURST_MASK 0x00000010  /**< Bounded fixed length
                                                        burst */
#define XDMAV3_DMACR_TYPE_BIBURST_MASK 0x00000020  /**< Bounded indeterminate
                                                        burst */

/* Sub-fields within XDMAV3_DMACR_DSIZE_MASK */
#define XDMAV3_DMACR_DSIZE_8_MASK      0x00000000  /**< Xfer width = 8 bits */
#define XDMAV3_DMACR_DSIZE_16_MASK     0x00000001  /**< Xfer width = 16 bits */
#define XDMAV3_DMACR_DSIZE_32_MASK     0x00000002  /**< Xfer width = 32 bits */
#define XDMAV3_DMACR_DSIZE_64_MASK     0x00000003  /**< Xfer width = 64 bits */
#define XDMAV3_DMACR_DSIZE_128_MASK    0x00000004  /**< Xfer width = 128 bits */

/* Left shift values for selected masks */
#define XDMAV3_DMACR_DEVSEL_SHIFT      16
#define XDMAV3_DMACR_BDPAGE_SHIFT      8
/*@}*/

/** @name Interrupt status bits for MAC interrupts
 *  These bits are associated with XDMAV3_ISR_OFFSET and
 *  XDMAV3_IER_OFFSET registers.
 *  @{
 */
#define XDMAV3_IPXR_DD_MASK      0x00000040  /**< DMA complete */
#define XDMAV3_IPXR_DE_MASK      0x00000020  /**< DMA error */
#define XDMAV3_IPXR_PD_MASK      0x00000010  /**< Pkt done */
#define XDMAV3_IPXR_PCTR_MASK    0x00000008  /**< Pkt count threshold reached */
#define XDMAV3_IPXR_PWBR_MASK    0x00000004  /**< Pkt waitbound reached */
#define XDMAV3_IPXR_SGDA_MASK    0x00000002  /**< SG Disable ack */
#define XDMAV3_IPXR_SGEND_MASK   0x00000001  /**< SG End */
/*@}*/

/** @name Software control register (SWCR) bitmasks
 *  @{
 */
#define XDMAV3_SWCR_SGE_MASK     0x80000000  /**< SG Enable */
#define XDMAV3_SWCR_SGD_MASK     0x40000000  /**< SG Disable */
#define XDMAV3_SWCR_DSGAR_MASK   0x20000000  /**< SG Disable auto-restart */
#define XDMAV3_SWCR_PWB_MASK     0x00FFF000  /**< Pkt waitbound */
#define XDMAV3_SWCR_PCT_MASK     0x00000FFF  /**< Pkt threshold count */

/* Left shift values for selected masks */
#define XDMAV3_SWCR_PCT_SHIFT    0
#define XDMAV3_SWCR_PWB_SHIFT    12
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
*    u32 XDmaV3_mReadReg(u32 BaseAddress, u32 RegOffset)
*
*****************************************************************************/
#define XDmaV3_mReadReg(BaseAddress, RegOffset) \
    XIo_In32((u32)(BaseAddress) + (u32)(RegOffset))


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
*    void XDmaV3_mWriteReg(u32 BaseAddress, u32 RegOffset, u32 Data)
*
*****************************************************************************/
#define XDmaV3_mWriteReg(BaseAddress, RegOffset, Data)  \
    XIo_Out32((u32)(BaseAddress) + (u32)(RegOffset), (u32)(Data))


/****************************************************************************/
/**
*
* Read the given Buffer Descriptor word.
*
* @param    BaseAddress is the base address of the BD to read
* @param    Offset is the word offset to be read
*
* @return   The 32-bit value of the field
*
* @note
* C-style signature:
*    u32 XDmaV3_mReadBd(u32 BaseAddress, u32 Offset)
*
*****************************************************************************/
#define XDmaV3_mReadBd(BaseAddress, Offset)             \
    (*(u32*)((u32)(BaseAddress) + (u32)(Offset)))


/****************************************************************************/
/**
*
* Write the given Buffer Descriptor word.
*
* @param    BaseAddress is the base address of the BD to write
* @param    Offset is the word offset to be written
* @param    Data is the 32-bit value to write to the field
*
* @return   None.
*
* @note
* C-style signature:
*    void XDmaV3_mWriteReg(u32 BaseAddress, u32 Offset, u32 Data)
*
*****************************************************************************/
#define XDmaV3_mWriteBd(BaseAddress, Offset, Data)              \
    (*(u32*)((u32)(BaseAddress) + (u32)(Offset)) = (Data))


/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
