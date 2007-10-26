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
*       (c) Copyright 2002-2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xenv_linux.h
*
* Defines common services specified by xenv.h.
*
* @note
* 	This file is not intended to be included directly by driver code.
* 	Instead, the generic xenv.h file is intended to be included by driver
* 	code.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a wgr  02/28/07 Added cache handling macros.
* 1.00a wgr  02/27/07 Simplified code. Deprecated old-style macro names.
* 1.00a xd   11/03/04 Improved support for doxygen.
* 1.00a ch   10/24/02 First release
* 1.10a wgr  03/22/07 Converted to new coding style.
* </pre>
*
*
******************************************************************************/

#ifndef XENV_LINUX_H
#define XENV_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif


/***************************** Include Files *********************************/

#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <linux/string.h>
#include <linux/delay.h>


/******************************************************************************
 *
 * MEMCPY / MEMSET related macros.
 *
 * Those macros are defined to catch legacy code in Xilinx drivers. The
 * XENV_MEM_COPY and XENV_MEM_FILL macros were used in early Xilinx driver
 * code. They are being replaced by memcpy() and memset() function calls. These
 * macros are defined to catch any remaining occurences of those macros.
 *
 ******************************************************************************/

/*****************************************************************************/
/**
 *
 * Copies a non-overlapping block of memory.
 *
 * @param	DestPtr
 *		Destination address to copy data to.
 *
 * @param	SrcPtr
 * 		Source address to copy data from.
 *
 * @param	Bytes
 * 		Number of bytes to copy.
 *
 * @return	None.
 *
 *****************************************************************************/

#define XENV_MEM_COPY(DestPtr, SrcPtr, Bytes) \
		memcpy(DestPtr, SrcPtr, Bytes)
/*		do_not_use_XENV_MEM_COPY_use_memcpy_instead */


/*****************************************************************************/
/**
 *
 * Fills an area of memory with constant data.
 *
 * @param	DestPtr
 *		Destination address to copy data to.
 *
 * @param	Data
 * 		Value to set.
 *
 * @param	Bytes
 * 		Number of bytes to copy.
 *
 * @return	None.
 *
 *****************************************************************************/

#define XENV_MEM_FILL(DestPtr, Data, Bytes) \
		memset(DestPtr, Data, Bytes)
/*		do_not_use_XENV_MEM_FILL_use_memset_instead */


/******************************************************************************
 *
 * TIME related macros
 *
 ******************************************************************************/
/**
 * A structure that contains a time stamp used by other time stamp macros
 * defined below. This structure is processor dependent.
 */
typedef int XENV_TIME_STAMP;

/*****************************************************************************/
/**
 *
 * Time is derived from the 64 bit PPC timebase register
 *
 * @param   StampPtr is the storage for the retrieved time stamp.
 *
 * @return  None.
 *
 * @note
 *
 * Signature: void XENV_TIME_STAMP_GET(XTIME_STAMP *StampPtr)
 * <br><br>
 * This macro must be implemented by the user.
 *
 *****************************************************************************/
#define XENV_TIME_STAMP_GET(StampPtr)

/*****************************************************************************/
/**
 *
 * This macro is not yet implemented and always returns 0.
 *
 * @param   Stamp1Ptr is the first sampled time stamp.
 * @param   Stamp2Ptr is the second sampled time stamp.
 *
 * @return  0
 *
 * @note
 *
 * This macro must be implemented by the user.
 *
 *****************************************************************************/
#define XENV_TIME_STAMP_DELTA_US(Stamp1Ptr, Stamp2Ptr)     (0)

/*****************************************************************************/
/**
 *
 * This macro is not yet implemented and always returns 0.
 *
 * @param   Stamp1Ptr is the first sampled time stamp.
 * @param   Stamp2Ptr is the second sampled time stamp.
 *
 * @return  0
 *
 * @note
 *
 * This macro must be implemented by the user
 *
 *****************************************************************************/
#define XENV_TIME_STAMP_DELTA_MS(Stamp1Ptr, Stamp2Ptr)     (0)

/*****************************************************************************/
/**
 *
 * Delay the specified number of microseconds.
 *
 * @param	delay
 * 		Number of microseconds to delay.
 *
 * @return	None.
 *
 * @note	XENV_USLEEP is deprecated. Use udelay() instead.
 *
 *****************************************************************************/

#define XENV_USLEEP(delay)	udelay(delay)
/*		do_not_use_XENV_MEM_COPY_use_memcpy_instead */


/******************************************************************************
 *
 * CACHE handling macros / mappings
 *
 * The implementation of the cache handling functions can be found in
 * arch/microblaze.
 *
 * These #defines are simple mappings to the Linux API.
 *
 * The underlying Linux implementation will take care of taking the right
 * actions depending on the configuration of the MicroBlaze processor in the
 * system.
 *
 ******************************************************************************/

#define XCACHE_ENABLE_DCACHE()		__enable_dcache()
#define XCACHE_DISABLE_DCACHE()		__disable_dcache()
#define XCACHE_ENABLE_ICACHE()		__enable_icache()
#define XCACHE_DISABLE_ICACHE()		__disable_icache()

#define XCACHE_INVALIDATE_DCACHE_RANGE(Addr, Len) invalidate_dcache_range((u32)(Addr), (u32)((Addr)+(Len)))
#define XCACHE_FLUSH_DCACHE_RANGE(Addr, Len)      flush_dcache_range((u32)(Addr), (u32)((Addr)+(Len)))

#define XCACHE_INVALIDATE_ICACHE_RANGE(Addr, Len) "XCACHE_INVALIDATE_ICACHE_RANGE unsupported"
#define XCACHE_FLUSH_ICACHE_RANGE(Addr, Len)      flush_icache_range(Addr, Len)

#define XCACHE_ENABLE_CACHE()	\
		{ XCACHE_ENABLE_DCACHE(); XCACHE_ENABLE_ICACHE(); }

#define XCACHE_DISABLE_CACHE()	\
		{ XCACHE_DISABLE_DCACHE(); XCACHE_DISABLE_ICACHE(); }



#ifdef __cplusplus
}
#endif

#endif            /* end of protection macro */

