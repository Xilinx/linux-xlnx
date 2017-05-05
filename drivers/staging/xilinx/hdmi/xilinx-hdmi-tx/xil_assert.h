/******************************************************************************
*
* Copyright (C) 2009 - 2015 Xilinx, Inc. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xil_assert.h
*
* This file contains assert related functions.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date   Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a hbm  07/14/09 First release
* </pre>
*
******************************************************************************/

#ifndef XIL_ASSERT_H	/* prevent circular inclusions */
#define XIL_ASSERT_H	/* by using protection macros */

#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/


/************************** Constant Definitions *****************************/

#define XIL_ASSERT_NONE     0U
#define XIL_ASSERT_OCCURRED 1U
#define XNULL NULL

/**
 * This data type defines a callback to be invoked when an
 * assert occurs. The callback is invoked only when asserts are enabled
 */
//typedef void (*Xil_AssertCallback) (const char8 *File, s32 Line);

/***************** Macros (Inline Functions) Definitions *********************/

//#define Xil_Assert(const char8 *File, s32 Line)
//#define Xil_AssertSetCallback(Xil_AssertCallback Routine)

#define Xil_AssertVoid(Expression)
#define Xil_AssertVoidAlways()
#define Xil_AssertNonvoid(Expression)
#define Xil_AssertNonvoidAlways()


/************************** Function Prototypes ******************************/

#ifdef __cplusplus
}
#endif

#endif	/* end of protection macro */
