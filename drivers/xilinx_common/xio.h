/* $Id: xio.h,v 1.4 2007/07/24 22:01:35 xduan Exp $ */
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
*       (c) Copyright 2007-2008 Xilinx Inc.
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
* @file xio.h
*
* This file contains the interface for the XIo component, which encapsulates
* the Input/Output functions for the PowerPC architecture.
* This header file needs to be updated to replace eieio with mbar when
* compilers support the mbar mnemonic.
*
* @note
*
* This file contains architecture-dependent items (memory mapped or non memory
* mapped I/O).
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- --------------------------------------------------------
* 1.00a ecm  10/18/05 initial release
*                     needs to be updated to replace eieio with mbar when
*                     compilers support this mnemonic.
*
* 1.00a ecm  01/24/07 update for new coding standard.
* 1.10a xd   07/24/07 Corrected the format in asm functions in __DCC__ mode.
* </pre>
******************************************************************************/

#ifndef XIO_H           /* prevent circular inclusions */
#define XIO_H           /* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/

/**
 * Typedef for an I/O address.  Typically correlates to the width of the
 * address bus.
 */
typedef u32 XIo_Address;

/***************** Macros (Inline Functions) Definitions *********************/

/* The following macro is specific to the GNU compiler and PowerPC family. It
 * performs an EIEIO instruction such that I/O operations are synced correctly.
 * This macro is not necessarily portable across compilers since it uses
 * inline assembly.
 */
#ifdef CONFIG_PPC
#  define SYNCHRONIZE_IO __asm__ volatile ("eieio") /* should be 'mbar' ultimately */
#else
#  define SYNCHRONIZE_IO
#endif

/* The following macros allow the software to be transportable across
 * processors which use big or little endian memory models.
 *
 * Defined first are processor-specific endian conversion macros specific to
 * the GNU compiler and the PowerPC family, as well as a no-op endian conversion
 * macro. These macros are not to be used directly by software. Instead, the
 * XIo_To/FromLittleEndianXX and XIo_To/FromBigEndianXX macros below are to be
 * used to allow the endian conversion to only be performed when necessary
 */

#define XIo_EndianNoop(Source, DestPtr)    (*DestPtr = Source)

#ifdef CONFIG_PPC

#define XIo_EndianSwap16(Source, DestPtr)  __asm__ __volatile__(\
                                           "sthbrx %0,0,%1\n"\
                                           : : "r" (Source), "r" (DestPtr)\
                                           )

#define XIo_EndianSwap32(Source, DestPtr)  __asm__ __volatile__(\
                                           "stwbrx %0,0,%1\n"\
                                           : : "r" (Source), "r" (DestPtr)\
                                           )
#else

#define XIo_EndianSwap16(Source, DestPtr) \
{\
   u16 src = (Source); \
   u16 *destptr = (DestPtr); \
   *destptr = src >> 8; \
   *destptr |= (src << 8); \
}

#define XIo_EndianSwap32(Source, DestPtr) \
{\
   u32 src = (Source); \
   u32 *destptr = (DestPtr); \
   *destptr = src >> 24; \
   *destptr |= ((src >> 8)  & 0x0000FF00); \
   *destptr |= ((src << 8)  & 0x00FF0000); \
   *destptr |= ((src << 24) & 0xFF000000); \
}

#endif

// #ifdef XLITTLE_ENDIAN
// /* little-endian processor */

// #define XIo_ToLittleEndian16                XIo_EndianNoop
// #define XIo_ToLittleEndian32                XIo_EndianNoop
// #define XIo_FromLittleEndian16              XIo_EndianNoop
// #define XIo_FromLittleEndian32              XIo_EndianNoop

// #define XIo_ToBigEndian16(Source, DestPtr)  XIo_EndianSwap16(Source, DestPtr)
// #define XIo_ToBigEndian32(Source, DestPtr)  XIo_EndianSwap32(Source, DestPtr)
// #define XIo_FromBigEndian16                 XIo_ToBigEndian16
// #define XIo_FromBigEndian32                 XIo_ToBigEndian32

// #else
/* big-endian processor */ // ppc or microblaze

#define XIo_ToLittleEndian16(Source, DestPtr) XIo_EndianSwap16(Source, DestPtr)
#define XIo_ToLittleEndian32(Source, DestPtr) XIo_EndianSwap32(Source, DestPtr)
#define XIo_FromLittleEndian16                XIo_ToLittleEndian16
#define XIo_FromLittleEndian32                XIo_ToLittleEndian32

#define XIo_ToBigEndian16                     XIo_EndianNoop
#define XIo_ToBigEndian32                     XIo_EndianNoop
#define XIo_FromBigEndian16                   XIo_EndianNoop
#define XIo_FromBigEndian32                   XIo_EndianNoop

// #endif


/************************** Function Prototypes ******************************/

/* The following macros allow optimized I/O operations for memory mapped I/O
 * Note that the SYNCHRONIZE_IO may be moved by the compiler during
 * optimization.
 */

u8 XIo_In8(XIo_Address InAddress);
u16 XIo_In16(XIo_Address InAddress);
u32 XIo_In32(XIo_Address InAddress);

void XIo_Out8(XIo_Address OutAddress, u8 Value);
void XIo_Out16(XIo_Address OutAddress, u16 Value);
void XIo_Out32(XIo_Address OutAddress, u32 Value);


/*
#define XIo_In8(InputPtr)  (*(volatile u8  *)(InputPtr)); SYNCHRONIZE_IO;
#define XIo_In16(InputPtr) (*(volatile u16 *)(InputPtr)); SYNCHRONIZE_IO;
#define XIo_In32(InputPtr) (*(volatile u32 *)(InputPtr)); SYNCHRONIZE_IO;

#define XIo_Out8(OutputPtr, Value)  \
    { (*(volatile u8  *)(OutputPtr) = Value); SYNCHRONIZE_IO; }
#define XIo_Out16(OutputPtr, Value) \
    { (*(volatile u16 *)(OutputPtr) = Value); SYNCHRONIZE_IO; }
#define XIo_Out32(OutputPtr, Value) \
    { (*(volatile u32 *)(OutputPtr) = Value); SYNCHRONIZE_IO; }
 */

/* The following functions handle IO addresses where data must be swapped
 * They cannot be implemented as macros
 */
u16 XIo_InSwap16(XIo_Address InAddress);
u32 XIo_InSwap32(XIo_Address InAddress);
void XIo_OutSwap16(XIo_Address OutAddress, u16 Value);
void XIo_OutSwap32(XIo_Address OutAddress, u32 Value);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
