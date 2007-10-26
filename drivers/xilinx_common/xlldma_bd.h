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
*       (c) Copyright 2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
 *
 * @file xlldma_bd.h
 *
 * This header provides operations to manage buffer descriptors (BD) in support
 * of Local-Link scatter-gather DMA (see xlldma.h).
 *
 * The API exported by this header defines abstracted macros that allow the
 * application to read/write specific BD fields.
 *
 * <b>Buffer Descriptors</b>
 *
 * A buffer descriptor defines a DMA transaction (see "Transaction"
 * section in xlldma.h). The macros defined by this header file allow access
 * to most fields within a BD to tailor a DMA transaction according to
 * application and hardware requirements.  See the hardware IP DMA spec for
 * more information on BD fields and how they affect transfers.
 *
 * The XLlDma_Bd structure defines a BD. The organization of this structure is
 * driven mainly by the hardware for use in scatter-gather DMA transfers.
 *
 * <b>Accessor Macros</b>
 *
 * Most of the BD attributes can be accessed through macro functions defined
 * here in this API. Words such as XLLDMA_BD_USR1_OFFSET (see xlldma_hw.h)
 * should be accessed using XLlDma_mBdRead() and XLlDma_mBdWrite() as defined
 * in xlldma_hw.h. The USR words are implementation dependent. For example,
 * they may implement checksum offloading fields for Ethernet devices. Accessor
 * macros may be defined in the device specific API to get at this data.
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
 * 1.00a xd   12/21/06 First release
 * </pre>
 *
 *****************************************************************************/

#ifndef XLLDMA_BD_H		/* prevent circular inclusions */
#define XLLDMA_BD_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xenv.h"
#include "xlldma_hw.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/**
 * The XLlDma_Bd is the type for buffer descriptors (BDs).
 */
typedef u32 XLlDma_Bd[XLLDMA_BD_NUM_WORDS];


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
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
*    u32 XLlDma_mBdRead(u32 BaseAddress, u32 Offset)
*
******************************************************************************/
#define XLlDma_mBdRead(BaseAddress, Offset)				\
	(*(u32*)((u32)(BaseAddress) + (u32)(Offset)))


/*****************************************************************************/
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
*    void XLlDma_mBdWrite(u32 BaseAddress, u32 RegOffset, u32 Data)
*
******************************************************************************/
#define XLlDma_mBdWrite(BaseAddress, Offset, Data)			\
	(*(u32*)((u32)(BaseAddress) + (u32)(Offset)) = (Data))


/*****************************************************************************/
/**
 * Zero out all BD fields
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Nothing
 *
 * @note
 * C-style signature:
 *    void XLlDma_mBdClear(XLlDma_Bd* BdPtr)
 *
 *****************************************************************************/
#define XLlDma_mBdClear(BdPtr)                    \
	memset((BdPtr), 0, sizeof(XLlDma_Bd))


/*****************************************************************************/
/**
 * Set the BD's STS/CTRL field. The word containing STS/CTRL also contains the
 * USR0 field. USR0 will not be modified. This operation requires a read-
 * modify-write operation. If it is wished to set both STS/CTRL and USR0 with
 * a single write operation, then use XLlDma_mBdWrite(BdPtr,
 * XLLDMA_BD_STSCTRL_USR0_OFFSET, Data).
 *
 * @param  BdPtr is the BD to operate on
 * @param  Data is the value to write to STS/CTRL. Or 0 or more
 *         XLLDMA_BD_STSCTRL_*** values defined in xlldma_hw.h to create a
 *         valid value for this parameter
 *
 * @note
 * C-style signature:
 *    u32 XLlDma_mBdSetStsCtrl(XLlDma_Bd* BdPtr, u32 Data)
 *
 *****************************************************************************/
#define XLlDma_mBdSetStsCtrl(BdPtr, Data)                                   \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET,             \
		(XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)     \
		& XLLDMA_BD_STSCTRL_USR0_MASK) |			    \
		((Data) & XLLDMA_BD_STSCTRL_MASK))


/*****************************************************************************/
/**
 * Retrieve the word containing the BD's STS/CTRL field. This word also
 * contains the USR0 field.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Word at offset XLLDMA_BD_DMASR_OFFSET. Use XLLDMA_BD_STSCTRL_***
 *         values defined in xlldma_hw.h to interpret this returned value
 *
 * @note
 * C-style signature:
 *    u32 XLlDma_mBdGetStsCtrl(XLlDma_Bd* BdPtr)
 *
 *****************************************************************************/
#define XLlDma_mBdGetStsCtrl(BdPtr)              \
	XLlDma_mBdRead((BdPtr), XLLDMA_BD_STSCTRL_USR0_OFFSET)


/*****************************************************************************/
/**
 * Set transfer length in bytes for the given BD. The length must be set each
 * time a BD is submitted to hardware.
 *
 * @param  BdPtr is the BD to operate on
 * @param  LenBytes is the number of bytes to transfer.
 *
 * @note
 * C-style signature:
 *    void XLlDma_mBdSetLength(XLlDma_Bd* BdPtr, u32 LenBytes)
 *
 *****************************************************************************/
#define XLlDma_mBdSetLength(BdPtr, LenBytes)                            \
	XLlDma_mBdWrite((BdPtr), XLLDMA_BD_BUFL_OFFSET, (LenBytes))


/*****************************************************************************/
/**
 * Retrieve the BD length field.
 *
 * For TX channels, the returned value is the same as that written with
 * XLlDma_mBdSetLength().
 *
 * For RX channels, the returned value is what was written by the DMA engine
 * after processing the BD. This value represents the number of bytes
 * processed.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @return Bytes processed by hardware or set by XLlDma_mBdSetLength().
 *
 * @note
 * C-style signature:
 *    u32 XLlDma_mBdGetLength(XLlDma_Bd* BdPtr)
 *
 *****************************************************************************/
#define XLlDma_mBdGetLength(BdPtr)                      \
	XLlDma_mBdRead((BdPtr), XLLDMA_BD_BUFL_OFFSET)


/*****************************************************************************/
/**
 * Set the ID field of the given BD. The ID is an arbitrary piece of data the
 * application can associate with a specific BD.
 *
 * @param  BdPtr is the BD to operate on
 * @param  Id is a 32 bit quantity to set in the BD
 *
 * @note
 * C-style signature:
 *    void XLlDma_mBdSetId(XLlDma_Bd* BdPtr, void Id)
 *
 *****************************************************************************/
#define XLlDma_mBdSetId(BdPtr, Id)                                      \
	(XLlDma_mBdWrite((BdPtr), XLLDMA_BD_ID_OFFSET, (u32)(Id)))


/*****************************************************************************/
/**
 * Retrieve the ID field of the given BD previously set with XLlDma_mBdSetId.
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    u32 XLlDma_mBdGetId(XLlDma_Bd* BdPtr)
 *
 *****************************************************************************/
#define XLlDma_mBdGetId(BdPtr) (XLlDma_mBdRead((BdPtr), XLLDMA_BD_ID_OFFSET))


/*****************************************************************************/
/**
 * Set the BD's buffer address.
 *
 * @param  BdPtr is the BD to operate on
 * @param  Addr is the address to set
 *
 * @note
 * C-style signature:
 *    void XLlDma_mBdSetBufAddr(XLlDma_Bd* BdPtr, u32 Addr)
 *
 *****************************************************************************/
#define XLlDma_mBdSetBufAddr(BdPtr, Addr)                               \
	(XLlDma_mBdWrite((BdPtr), XLLDMA_BD_BUFA_OFFSET, (u32)(Addr)))


/*****************************************************************************/
/**
 * Get the BD's buffer address
 *
 * @param  BdPtr is the BD to operate on
 *
 * @note
 * C-style signature:
 *    u32 XLlDma_mBdGetBufAddrLow(XLlDma_Bd* BdPtr)
 *
 *****************************************************************************/
#define XLlDma_mBdGetBufAddr(BdPtr)                     \
	(XLlDma_mBdRead((BdPtr), XLLDMA_BD_BUFA_OFFSET))


/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
