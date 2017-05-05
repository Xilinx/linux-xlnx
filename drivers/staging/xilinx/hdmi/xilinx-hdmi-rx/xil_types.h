/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xil_types.h
*
* @addtogroup common_types Basic Data types for Xilinx&reg; Software IP
*
* The xil_types.h file contains basic types for Xilinx software IP. These data types
* are applicable for all processors supported by Xilinx.
* @{
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date   Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a hbm  07/14/09 First release
* 3.03a sdm  05/30/11 Added Xuint64 typedef and XUINT64_MSW/XUINT64_LSW macros
* 5.00 	pkp  05/29/14 Made changes for 64 bit architecture
*	srt  07/14/14 Use standard definitions from stdint.h and stddef.h
*		      Define LONG and ULONG datatypes and mask values
* </pre>
*
******************************************************************************/

#ifndef XIL_TYPES_H	/* prevent circular inclusions */
#define XIL_TYPES_H	/* by using protection macros */

#ifndef __KERNEL__
#include <stdint.h>
#include <stddef.h>
#endif

/************************** Constant Definitions *****************************/

#ifndef TRUE
#  define TRUE		1U
#endif

#ifndef FALSE
#  define FALSE		0U
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define XIL_COMPONENT_IS_READY     0x11111111U  /**< In device drivers, This macro will be
                                                 assigend to "IsReady" member of driver
												 instance to indicate that driver
												 instance is initialized and ready to use. */
#define XIL_COMPONENT_IS_STARTED   0x22222222U  /**< In device drivers, This macro will be assigend to
                                                 "IsStarted" member of driver instance
												 to indicate that driver instance is
												 started and it can be enabled. */

/* @name New types
 * New simple types.
 * @{
 */
#ifndef __KERNEL__
#ifndef XBASIC_TYPES_H
/*
 * guarded against xbasic_types.h.
 */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
/** @}*/
#define __XUINT64__
typedef struct
{
	u32 Upper;
	u32 Lower;
} Xuint64;


/*****************************************************************************/
/**
* @brief    Return the most significant half of the 64 bit data type.
*
* @param    x is the 64 bit word.
*
* @return   The upper 32 bits of the 64 bit word.
*
******************************************************************************/
#define XUINT64_MSW(x) ((x).Upper)

/*****************************************************************************/
/**
* @brief    Return the least significant half of the 64 bit data type.
*
* @param    x is the 64 bit word.
*
* @return   The lower 32 bits of the 64 bit word.
*
******************************************************************************/
#define XUINT64_LSW(x) ((x).Lower)

#endif /* XBASIC_TYPES_H */

/*
 * xbasic_types.h does not typedef s* or u64
 */
/** @{ */
typedef char char8;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int sint32;

typedef intptr_t INTPTR;
typedef uintptr_t UINTPTR;
typedef ptrdiff_t PTRDIFF;
/** @}*/
#if !defined(LONG) || !defined(ULONG)
typedef long LONG;
typedef unsigned long ULONG;
#endif

#define ULONG64_HI_MASK	0xFFFFFFFF00000000U
#define ULONG64_LO_MASK	~ULONG64_HI_MASK

#else
#include <linux/types.h>
// Used by xil_io.h
typedef char char8;
typedef long INTPTR;
typedef uintptr_t UINTPTR;
typedef ptrdiff_t PTRDIFF;
#endif

/** @{ */
/**
 * This data type defines an interrupt handler for a device.
 * The argument points to the instance of the component
 */
typedef void (*XInterruptHandler) (void *InstancePtr);

/**
 * This data type defines an exception handler for a processor.
 * The argument points to the instance of the component
 */
typedef void (*XExceptionHandler) (void *InstancePtr);

/**
 * @brief  Returns 32-63 bits of a number.
 * @param  n : Number being accessed.
 * @return Bits 32-63 of number.
 *
 * @note    A basic shift-right of a 64- or 32-bit quantity.
 *          Use this to suppress the "right shift count >= width of type"
 *          warning when that quantity is 32-bits.
 */
#define UPPER_32_BITS(n) ((u32)(((n) >> 16) >> 16))

/**
 * @brief  Returns 0-31 bits of a number
 * @param  n : Number being accessed.
 * @return Bits 0-31 of number
 */
#define LOWER_32_BITS(n) ((u32)(n))




/************************** Constant Definitions *****************************/

#ifndef TRUE
#define TRUE		1U
#endif

#ifndef FALSE
#define FALSE		0U
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif	/* end of protection macro */
/**
* @} End of "addtogroup common_types".
*/
