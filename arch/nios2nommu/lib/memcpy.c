/*--------------------------------------------------------------------
 *
 * arch/nios2nommu/lib/memcpy.c
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
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
 * Jun/09/2004		dgt	    Split out separate source file from string.c
 *
 ---------------------------------------------------------------------*/

#include <linux/types.h>
#include <linux/autoconf.h>
#include <asm/nios.h>
#include <asm/string.h>

#ifdef __HAVE_ARCH_MEMCPY
  void * memcpy(void * d, const void * s, size_t count)
   {
    unsigned long dst, src;
	dst = (unsigned long) d;
	src = (unsigned long) s;

	if ((count < 8) || ((dst ^ src) & 3))
	    goto restup;

	if (dst & 1) {
		*(char*)dst++=*(char*)src++;
	    count--;
	}
	if (dst & 2) {
		*(short*)dst=*(short*)src;
	    src += 2;
	    dst += 2;
	    count -= 2;
	}
	while (count > 3) {
		*(long*)dst=*(long*)src;
	    src += 4;
	    dst += 4;
	    count -= 4;
	}

    restup:
	while (count--)
		*(char*)dst++=*(char*)src++;

	return d;
   }
#endif
