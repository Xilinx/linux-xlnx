/* $Id: xio.h,v 1.1 2006/12/13 14:22:33 imanuilov Exp $ */
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
*       (c) Copyright 2002-2003 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xio.h
*
* This file contains the interface for the XIo component, which encapsulates
* the Input/Output functions for processors that do not require any special
* I/O handling.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rpm  11/07/03 Added InSwap/OutSwap routines for endian conversion
* 1.00a xd   11/04/04 Improved support for doxygen
* 1.01a ecm  02/24/06 CR225908 corrected the extra curly braces in macros 
*                     and bumped version to 1.01.a.
*
* </pre>
*
* @note
*
* This file may contain architecture-dependent items (memory-mapped or
* non-memory-mapped I/O).
*
******************************************************************************/

#ifndef XIO_H           /* prevent circular inclusions */
#define XIO_H           /* by using protection macros */

/***************************** Include Files *********************************/

#include "xbasic_types.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/

/**
 * Typedef for an I/O address.  Typically correlates to the width of the
 * address bus.
 */
typedef Xuint32 XIo_Address;

/***************** Macros (Inline Functions) Definitions *********************/

/*
 * The following macros allow optimized I/O operations for memory mapped I/O.
 * It should be noted that macros cannot be used if synchronization of the I/O
 * operation is needed as it will likely break some code.
 */

/*****************************************************************************/
/**
*
* Performs an input operation for an 8-bit memory location by reading from the
* specified address and returning the value read from that address.
*
* @param    InputPtr contains the address to perform the input operation at.
*
* @return   The value read from the specified input address.
*
******************************************************************************/
#define XIo_In8(InputPtr)  (*(volatile Xuint8  *)(InputPtr))

/*****************************************************************************/
/**
*
* Performs an input operation for a 16-bit memory location by reading from the
* specified address and returning the value read from that address.
*
* @param    InputPtr contains the address to perform the input operation at.
*
* @return   The value read from the specified input address.
*
******************************************************************************/
#define XIo_In16(InputPtr) (*(volatile Xuint16 *)(InputPtr))

/*****************************************************************************/
/**
*
* Performs an input operation for a 32-bit memory location by reading from the
* specified address and returning the value read from that address.
*
* @param    InputPtr contains the address to perform the input operation at.
*
* @return   The value read from the specified input address.
*
******************************************************************************/
#define XIo_In32(InputPtr)  (*(volatile Xuint32 *)(InputPtr))


/*****************************************************************************/
/**
*
* Performs an output operation for an 8-bit memory location by writing the
* specified value to the the specified address.
*
* @param    OutputPtr contains the address to perform the output operation at.
* @param    Value contains the value to be output at the specified address.
*
* @return   None.
*
******************************************************************************/
#define XIo_Out8(OutputPtr, Value)  \
    (*(volatile Xuint8  *)((OutputPtr)) = (Value))

/*****************************************************************************/
/**
*
* Performs an output operation for a 16-bit memory location by writing the
* specified value to the the specified address.
*
* @param    OutputPtr contains the address to perform the output operation at.
* @param    Value contains the value to be output at the specified address.
*
* @return   None.
*
******************************************************************************/
#define XIo_Out16(OutputPtr, Value) \
    (*(volatile Xuint16 *)((OutputPtr)) = (Value))

/*****************************************************************************/
/**
*
* Performs an output operation for a 32-bit memory location by writing the
* specified value to the the specified address.
*
* @param    OutputPtr contains the address to perform the output operation at.
* @param    Value contains the value to be output at the specified address.
*
* @return   None.
*
******************************************************************************/
#define XIo_Out32(OutputPtr, Value) \
    (*(volatile Xuint32 *)((OutputPtr)) = (Value))


/* The following macros allow the software to be transportable across
 * processors which use big or little endian memory models.
 *
 * Defined first is a no-op endian conversion macro. This macro is not to
 * be used directly by software. Instead, the XIo_To/FromLittleEndianXX and
 * XIo_To/FromBigEndianXX macros below are to be used to allow the endian
 * conversion to only be performed when necessary
 */
#define XIo_EndianNoop(Source, Destination)    (*DestPtr = Source)

#ifdef XLITTLE_ENDIAN

#define XIo_ToLittleEndian16                XIo_EndianNoop
#define XIo_ToLittleEndian32                XIo_EndianNoop
#define XIo_FromLittleEndian16              XIo_EndianNoop
#define XIo_FromLittleEndian32              XIo_EndianNoop

#define XIo_ToBigEndian16(Source, DestPtr)  XIo_EndianSwap16(Source, DestPtr)
#define XIo_ToBigEndian32(Source, DestPtr)  XIo_EndianSwap32(Source, DestPtr)
#define XIo_FromBigEndian16                 XIo_ToBigEndian16
#define XIo_FromBigEndian32                 XIo_ToBigEndian32

#else

#define XIo_ToLittleEndian16(Source, DestPtr) XIo_EndianSwap16(Source, DestPtr)
#define XIo_ToLittleEndian32(Source, DestPtr) XIo_EndianSwap32(Source, DestPtr)
#define XIo_FromLittleEndian16                XIo_ToLittleEndian16
#define XIo_FromLittleEndian32                XIo_ToLittleEndian32

#define XIo_ToBigEndian16                     XIo_EndianNoop
#define XIo_ToBigEndian32                     XIo_EndianNoop
#define XIo_FromBigEndian16                   XIo_EndianNoop
#define XIo_FromBigEndian32                   XIo_EndianNoop

#endif

/************************** Function Prototypes ******************************/

/* The following functions allow the software to be transportable across
 * processors which use big or little endian memory models. These functions
 * should not be directly called, but the macros XIo_To/FromLittleEndianXX and
 * XIo_To/FromBigEndianXX should be used to allow the endian conversion to only
 * be performed when necessary.
 */
void XIo_EndianSwap16(Xuint16 Source, Xuint16* DestPtr);
void XIo_EndianSwap32(Xuint32 Source, Xuint32* DestPtr);

/* The following functions handle IO addresses where data must be swapped
 * They cannot be implemented as macros
 */
Xuint16 XIo_InSwap16(XIo_Address InAddress);
Xuint32 XIo_InSwap32(XIo_Address InAddress);
void XIo_OutSwap16(XIo_Address OutAddress, Xuint16 Value);
void XIo_OutSwap32(XIo_Address OutAddress, Xuint32 Value);

#endif          /* end of protection macro */
