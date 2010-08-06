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
 * @file xdmabdv3.h
 *
 * This header provides operations to manage buffer descriptors in support
 * of simple and scatter-gather DMA (see xdmav3.h).
 *
 * The API exported by this header defines abstracted macros that allow the
 * user to read/write specific BD fields.
 *
 * <b>Buffer Descriptors</b>
 *
 * A buffer descriptor (BD) defines a DMA transaction (see "Transaction"
 * section in xdmav3.h). The macros defined by this header file allow access
 * to most fields within a BD to tailor a DMA transaction according to user
 * and HW requirements.  See the HW IP DMA spec for more information on BD
 * fields and how they affect transfers.
 *
 * The XDmaBdV3 structure defines a BD. The organization of this structure is
 * driven mainly by the hardware for use in scatter-gather DMA transfers.
 *
 * <b>Accessor Macros</b>
 *
 * Most of the BD attributes can be accessed through macro functions defined
 * here in this API. Words such as XDMAV3_BD_USR0_OFFSET (see xdmav3_l.h)
 * should be accessed using XDmaV3_mReadBd() and XDmaV3_mWriteBd() as defined in
 * xdmav3_l.h. The USR words are implementation dependent. For example, they may
 * implement checksum offloading fields for Ethernet devices. Accessor macros
 * may be defined in the device specific API to get at this data.
 *
 * <b>Performance</b>
 *
 * BDs are typically in a non-cached memory space. Limiting I/O to BDs can
 * improve overall performance of the DMA channel.
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
 * ***************************************************************************
 */

#ifndef XDMABD_H		/* prevent circular inclusions */
#define XDMABD_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include <asm/delay.h>
#include "xdmav3_l.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/**
 * The XDmaBdV3 is the type for buffer descriptors (BDs).
 */
typedef u32 XDmaBdV3[XDMAV3_BD_NUM_WORDS];


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
 * Zero out BD fields
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Nothing
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mClear(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mClear(BdPtr)                    \
    memset((BdPtr), 0, sizeof(XDmaBdV3))


/*****************************************************************************/
/**
 * Retrieve the BD's Packet DMA transfer status word.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Word at offset XDMAV3_BD_DMASR_OFFSET
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetStatus(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetStatus(BdPtr)              \
    XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMASR_OFFSET)


/*****************************************************************************/
/**
 * Retrieve the BD's Packet status word. This is the first word of local link
 * footer information for receive channels.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Word at offset XDMAV3_BD_SR_OFFSET
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetPacketStatus(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetPacketStatus(BdPtr)                \
    XDmaV3_mReadBd((BdPtr), XDMAV3_BD_SR_OFFSET)


/*****************************************************************************/
/**
 * Retrieve the BD length field.
 *
 * For Tx channels, the returned value is the same as that written with
 * XDmaBdV3_mSetLength().
 *
 * For Rx channels, the returned value is the size of the received packet.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Bytes processed by HW or set by XDmaBdV3_mSetLength().
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetLength(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetLength(BdPtr)                      \
    XDmaV3_mReadBd((BdPtr), XDMAV3_BD_LENGTH_OFFSET)


/*****************************************************************************/
/**
 * Retrieve the BD length copy field. See XDmaBdV3_mSetLengthCopy() for
 * more information.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Value as set by XDmaBdV3_mSetLengthCopy().
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetLengthCopy(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetLengthCopy(BdPtr)                  \
    XDmaV3_mReadBd((BdPtr), XDMAV3_BD_LENCPY_OFFSET)


/*****************************************************************************/
/**
 * Test whether the given BD has been marked as the last BD of a packet.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return TRUE if BD represents the "Last" BD of a packet, FALSE otherwise
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mIsLast(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mIsLast(BdPtr)                                         \
    ((XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) & XDMAV3_DMACR_LAST_MASK) ? \
     TRUE : FALSE)

/*****************************************************************************/
/**
 * Set the ID field of the given BD. The ID is an arbitrary piece of data the
 * user can associate with a specific BD.
 *
 * @param  BdPtr is the BD to operate on
 * @param  Id is a 32 bit quantity to set in the BD
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetId(XDmaBdV3* BdPtr, void Id)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetId(BdPtr, Id)                                      \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_ID_OFFSET, (u32)Id))


/*****************************************************************************/
/**
 * Retrieve the ID field of the given BD previously set with XDmaBdV3_mSetId.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetId(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetId(BdPtr) (XDmaV3_mReadBd((BdPtr), XDMAV3_BD_ID_OFFSET))


/*****************************************************************************/
/**
 * Causes the DMA engine to increment the buffer address during the DMA
 * transfer for this BD. This is the desirable setting when the buffer data
 * occupies a memory range.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetBufIncrement(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetBufIncrement(BdPtr)                                \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
     XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) | XDMAV3_DMACR_AINC_MASK))


/*****************************************************************************/
/**
 * Cause the DMA engine to use the same memory buffer address during the DMA
 * transfer for this BD. This is the desirable setting when the buffer data
 * occupies a single address as may be the case if transferring to/from a FIFO.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetBufNoIncrement(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetBufNoIncrement(BdPtr)                              \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
        XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) & ~XDMAV3_DMACR_AINC_MASK))


/*****************************************************************************/
/**
 * Bypass data realignment engine (DRE) if DMA channel has DRE capability.
 * Has no effect if channel does not have DRE.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mIgnoreDre(XDmaBdV3* BdPtr)
 *
 ******************************************************************************/
#define XDmaBdV3_mIgnoreDre(BdPtr)                                      \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
        XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) | XDMAV3_DMACR_BPDRE_MASK))


/*****************************************************************************/
/**
 * Use data realignment engine (DRE) if DMA channel has DRE capability.
 * Has no effect if channel does not have DRE.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mUseDre(XDmaBdV3* BdPtr)
 *
 ******************************************************************************/
#define XDmaBdV3_mUseDre(BdPtr)                                         \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
        XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) & ~XDMAV3_DMACR_BPDRE_MASK))


/*****************************************************************************/
/**
 * Tell the SG DMA engine that the given BD marks the end of the current packet
 * to be processed.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetLast(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetLast(BdPtr)                                        \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
        XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) | XDMAV3_DMACR_LAST_MASK))


/*****************************************************************************/
/**
 * Tell the SG DMA engine that the current packet does not end with the given
 * BD.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mClearLast(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mClearLast(BdPtr)                                      \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
        XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) & ~XDMAV3_DMACR_LAST_MASK))


/*****************************************************************************/
/**
 * Set the Device Select field of the given BD.
 *
 * @param  BdPtr is the BD to operate on
 * @param  DevSel is the IP device select to use with LSB of 1. This value
 *         selects which IP block the transaction will address. Normally this
 *         is set to 0, but complex IP may require a specific DEVSEL.
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetDevSel(XDmaBdV3* BdPtr, unsigned DevSel)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetDevSel(BdPtr, DevSel)                              \
    {                                                                   \
        u32 Dmacr;                                                  \
        Dmacr = XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET);        \
        Dmacr = Dmacr | (((DevSel) << XDMAV3_DMACR_DEVSEL_SHIFT) &      \
                  XDMAV3_DMACR_DEVSEL_MASK);                            \
        XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET, Dmacr);        \
    }


/*****************************************************************************/
/**
 * Set the Page field of the given BD. The Page must be in terms of a physical
 * address. Use this macro if using 36 bit bus addressing.
 *
 * @param  BdPtr is the BD to operate on
 * @param  Page is the page to set. LSB=1
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetBdPage(XDmaBdV3* BdPtr, unsigned Page)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetBdPage(BdPtr, Page)                                \
    {                                                                   \
        u32 Dmacr;                                                  \
        Dmacr = XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET);        \
        Dmacr = Dmacr | (((Page) << XDMAV3_DMACR_BDPAGE_SHIFT) &        \
                  XDMAV3_DMACR_BDPAGE_MASK);                            \
        XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET, Dmacr);        \
    }


/*****************************************************************************/
/**
 * Set transfer attributes for the given BD.
 *
 * @param  BdPtr is the BD to operate on
 * @param  Type defines whether the transfer occurs with single beat or burst
 *         transfers on the target bus. This parameter must be one of the
 *         XDMAV3_DMACR_TYPE_*_MASK constants defined in xdma_l.h.
 * @param  Width defines the width of the transfer as it occurs on the target
 *         bus. This parameter must be one of the XDMAV3_DMACR_DSIZE_*_MASK
 *         constants defined in xdma_l.h
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetTransferType(XDmaBdV3* BdPtr, unsigned Type,
 *                                   unsigned Width)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetTransferType(BdPtr, Type, Width)                   \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_DMACR_OFFSET,                   \
         XDmaV3_mReadBd((BdPtr), XDMAV3_BD_DMACR_OFFSET) |              \
         ((Type) & XDMAV3_DMACR_TYPE_MASK) | ((Width) & XDMAV3_DMACR_DSIZE_MASK)))


/*****************************************************************************/
/**
 * Set transfer length in bytes for the given BD. The length must be set each
 * time a BD is submitted to HW.
 *
 * @param  BdPtr is the BD to operate on
 * @param  LenBytes is the number of bytes to transfer.
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetLength(XDmaBdV3* BdPtr, u32 LenBytes)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetLength(BdPtr, LenBytes)                            \
    XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_LENGTH_OFFSET, (LenBytes))


/*****************************************************************************/
/**
 * Write the given length to the length copy offset of the BD. This function
 * is useful only if an application needs to recover the number of bytes
 * originally set by XDmaBdV3_mSetLength() for Rx channels.
 *
 * To effectively use this function, an application would call
 * XDmaBdV3_mSetLength() to set the length on a Rx descriptor, followed by a
 * call to this macro to set the same length. When HW has processed the Rx
 * descriptor it will overwrite the BD length field with the actual length of
 * the packet. When the application performs post processing of the Rx
 * descriptor, it can call XDmaBdV3_mGetLengthCopy() to find out how many bytes
 * were originally allocated to the descriptor.
 *
 * @param  BdPtr is the BD to operate on
 * @param  LenBytes is the number of bytes to transfer.
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetLengthCopy(XDmaBdV3* BdPtr, u32 LenBytes)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetLengthCopy(BdPtr, LenBytes)                            \
    XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_LENCPY_OFFSET, (LenBytes))


/*****************************************************************************/
/**
 * Set the high order address of the BD's buffer address. Use this macro when
 * the address bus width is greater than 32 bits.
 *
 * @param  BdPtr is the BD to operate on
 * @param  HighAddr is the high order address bits to set, LSB = 2^32.
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetBufAddrHigh(XDmaBdV3* BdPtr, u32 HighAddr)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetBufAddrHigh(BdPtr, HighAddr)               \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_MSBA_OFFSET, (u32)(HighAddr)))


/*****************************************************************************/
/**
 * Set the low order address (bits 0..31) of the BD's buffer address.
 *
 * @param  BdPtr is the BD to operate on
 * @param  LowAddr is the low order address bits to set, LSB = 1.
 *
 * @note
 * C-style signature:
 *    void XDmaBdV3_mSetBufAddrLow(XDmaBdV3* BdPtr, u32 LowAddr)
 *
 *****************************************************************************/
#define XDmaBdV3_mSetBufAddrLow(BdPtr, LowAddr)                 \
    (XDmaV3_mWriteBd((BdPtr), XDMAV3_BD_LSBA_OFFSET, (u32)(LowAddr)))


/*****************************************************************************/
/**
 * Get the high order address of the BD's buffer address. Use this macro when
 * the address bus width is greater than 32 bits.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetBufAddrHigh(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetBufAddrHigh(BdPtr)                 \
    (XDmaV3_mReadBd((BdPtr), XDMAV3_BD_MSBA_OFFSET))


/*****************************************************************************/
/**
 * Get the low order address (bits 0..31) of the BD's buffer address.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    u32 XDmaBdV3_mGetBufAddrLow(XDmaBdV3* BdPtr)
 *
 *****************************************************************************/
#define XDmaBdV3_mGetBufAddrLow(BdPtr)                  \
    (XDmaV3_mReadBd((BdPtr), XDMAV3_BD_LSBA_OFFSET))


/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
