/*
	Copyright (c) 2002, Micrel Kendin Operations

	Written 2002 by LIQUN RUAN

	This software may be used and distributed according to the terms of 
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice. This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as lruan@kendin.com
	Micrel Kendin Operations
	486 Mercury Dr.
	Sunnyvale, CA 94085

	This driver is for Kendin's KS8695 SOHO Router Chipset as ethernet driver.

	Support and updates available at
	www.kendin.com/ks8695/

*/
#ifndef KS8695_FXHW_H
#define KS8695_FXHW_H
#include "ks8695_drv.h"

#define ASSERT(x) if(!(x)) panic("KS8695: x")
#define DelayInMicroseconds(x) udelay(x)
#define DelayInMilliseconds(x) mdelay(x)

typedef uint8_t UCHAR, UINT8, BOOLEAN, *PUCHAR;
typedef uint16_t USHORT, UINT16, *PUSHORT;
typedef uint32_t UINT, ULONG, UINT32, *PUINT, *PULONG;

#define SPEED_UNKNOWN					0
#define SPEED_10						10
#define SPEED_100						100
#define FULL_DUPLEX						1		// default for full duplex
#define HALF_DUPLEX						0

#define KS8695_WRITE_REG(reg, value)		((*(volatile uint32_t *)(Adapter->stDMAInfo.nBaseAddr + (reg))) = value)
#define KS8695_READ_REG(reg)				(*(volatile uint32_t *)(Adapter->stDMAInfo.nBaseAddr + (reg)))

#endif /*KS8695_FXHW_H*/

