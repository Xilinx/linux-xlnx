/******************************************************************************
*
* (c) Copyright 2009 Xilinx, Inc. All rights reserved.
*     This program is free software; you can redistribute it and/or modify
*     it under the terms of the GNU General Public License as published by
*     the Free Software Foundation; either version 2 of the License, or (at
*     your option) any later version.
*
*     You should have received a copy of the GNU General Public License
*     along with this program; if not, write to the Free Software
*     Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
*     MA  02110-1301  USA
*
* This file contains confidential and proprietary information of Xilinx, Inc.
* and is protected under U.S. and international copyright and other
* intellectual property laws.
*
* DISCLAIMER
* This disclaimer is not a license and does not grant any rights to the
* materials distributed herewith. Except as otherwise provided in a valid
* license issued to you by Xilinx, and to the maximum extent permitted by
* applicable law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND WITH ALL
* FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES AND CONDITIONS, EXPRESS,
* IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF
* MERCHANTABILITY, NON-INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE;
* and (2) Xilinx shall not be liable (whether in contract or tort, including
* negligence, or under any other theory of liability) for any loss or damage
* of any kind or nature related to, arising under or in connection with these
* materials, including for any direct, or any indirect, special, incidental,
* or consequential loss or damage (including loss of data, profits, goodwill,
* or any type of loss or damage suffered as a result of any action brought by
* a third party) even if such damage or loss was reasonably foreseeable or
* Xilinx had been advised of the possibility of the same.
*
* CRITICAL APPLICATIONS
* Xilinx products are not designed or intended to be fail-safe, or for use in
* any application requiring fail-safe performance, such as life-support or
* safety devices or systems, Class III medical devices, nuclear facilities,
* applications related to the deployment of airbags, or any other applications
* that could lead to death, personal injury, or severe property or
* environmental damage (individually and collectively, "Critical
* Applications"). Customer assumes the sole risk and liability of any use of
* Xilinx products in Critical Applications, subject only to applicable laws
* and regulations governing limitations on product liability.
*
* THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS PART OF THIS FILE
* AT ALL TIMES.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xil_io.h
*
* This file contains IO functions for Xilinx OCP in terms of Linux primitives.
*
******************************************************************************/
#ifndef XIL_IO_H
#define XIL_IO_H

#include "xil_types.h"
#include <asm/io.h>

extern inline u8
Xil_In8(u32 InAddress)
{
	return (u8) in_8((volatile unsigned char *) InAddress);
}
extern inline u16
Xil_In16(u32 InAddress)
{
	return (u16) in_be16((volatile unsigned short *) InAddress);
}
extern inline u32
Xil_In32(u32 InAddress)
{
	return (u32) in_be32((volatile unsigned *) InAddress);
}
extern inline void
Xil_Out8(u32 OutAddress, u8 Value)
{
	out_8((volatile unsigned char *) OutAddress, Value);
}
extern inline void
Xil_Out16(u32 OutAddress, u16 Value)
{
	out_be16((volatile unsigned short *) OutAddress, Value);
}
extern inline void
Xil_Out32(u32 OutAddress, u32 Value)
{
	out_be32((volatile unsigned *) OutAddress, Value);
}
extern inline u16
Xil_In16LE(u32 Addr)
{
	u16 Value;

	__asm__ volatile ("eieio; lhbrx %0,0,%1":"=r" (Value):"b" (Addr));
	return Value;
}
extern inline u32
Xil_In32LE(u32 Addr)
{
	u32 Value;

	__asm__ volatile ("eieio; lwbrx %0,0,%1":"=r" (Value):"b" (Addr));
	return Value;
}
extern inline void
Xil_Out16LE(u32 Addr, u16 Value)
{
	__asm__ volatile ("sthbrx %0,0,%1; eieio"::"r" (Value), "b"(Addr));
}
extern inline void
Xil_Out32LE(u32 Addr, u32 Value)
{
	__asm__ volatile ("stwbrx %0,0,%1; eieio"::"r" (Value), "b"(Addr));
}

#endif /* XIL_IO_H */
