/*
 * include/asm-arm/arch-s3c24a0/clocks.h
 *
 * $Id: clocks.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _S3C24A0_CLOCK_H_
#define _S3C24A0_CLOCK_H_

#include <asm/hardware.h>

#define GET_PCLK	0
#define GET_HCLK	1
#define GET_UPLL        2

#define GET_MDIV(x)	FExtr(x, fPLL_MDIV)
#define GET_PDIV(x)	FExtr(x, fPLL_PDIV)
#define GET_SDIV(x)	FExtr(x, fPLL_SDIV)

#define get_cpu_clk()  elfin_get_cpu_clk()
#define get_bus_clk(x) elfin_get_bus_clk((x))

unsigned long elfin_get_cpu_clk(void);
unsigned long elfin_get_bus_clk(int);
#endif /* _S3C24A0_CLOCK_H_ */
