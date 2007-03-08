#ifndef __NIOS_TLB_H__
#define __NIOS_TLB_H__

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/tlb.h
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 *  Copyright (C) 2003  Microtronix Datacom Ltd
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
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
 *
 * Written by Miles Bader <miles@gnu.org>
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/

#define tlb_flush(tlb)	((void)0)

#include <asm-generic/tlb.h>

#endif /* __NIOS_TLB_H__ */

