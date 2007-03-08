#ifndef __NIOS_STRING_H__
#define __NIOS_STRING_H__

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/string.h
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2004   Microtronix Datacom Ltd
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
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


#ifdef __KERNEL__ /* only set these up for kernel code */

#define __HAVE_ARCH_MEMMOVE
void * memmove(void * d, const void * s, size_t count);
#define __HAVE_ARCH_MEMCPY
extern void * memcpy(void *d, const void *s, size_t count);
#define __HAVE_ARCH_MEMSET
extern void * memset(void * s,int c,size_t count);

#if 0
#define __HAVE_ARCH_BCOPY
#define __HAVE_ARCH_STRLEN
#endif

#endif /* KERNEL */

#endif /* !(__NIOS_STRING_H__) */
