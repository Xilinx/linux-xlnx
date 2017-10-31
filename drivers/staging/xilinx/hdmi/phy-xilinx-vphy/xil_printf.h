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

 #ifndef XIL_PRINTF_H
 #define XIL_PRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#endif
#include "xil_types.h"


/*----------------------------------------------------*/
/* Use the following parameter passing structure to   */
/* make xil_printf re-entrant.                        */
/*----------------------------------------------------*/

struct params_s;


/*---------------------------------------------------*/
/* The purpose of this routine is to output data the */
/* same as the standard printf function without the  */
/* overhead most run-time libraries involve. Usually */
/* the printf brings in many kilobytes of code and   */
/* that is unacceptable in most embedded systems.    */
/*---------------------------------------------------*/

typedef char8* charptr;
typedef s32 (*func_ptr)(int c);

/*                                                   */

void xil_printf( const char8 *ctrl1, ...);
void print( const char8 *ptr);
extern void outbyte (char8 c);
extern char8 inbyte(void);

#ifdef __cplusplus
}
#endif

#include <linux/module.h> 
#define xil_printf(format, ...) printk(KERN_INFO format, ## __VA_ARGS__) 
#define print(format) printk(KERN_INFO format)

#endif	/* end of protection macro */
