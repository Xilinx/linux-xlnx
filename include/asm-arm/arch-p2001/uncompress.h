/*
 *  linux/include/asm-armnommu/arch-p2001/uncompress.h
 *
 *  Copyright (C) 2004 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNCOMPRESS_H__
#define __UNCOMPRESS_H__

#include <asm/hardware.h>

static __inline__ void putc(char c)
{
	while ((P2001_UART->r.STATUS & 0x3f) > 0)
		barrier();
	P2001_UART->w.TX[0] = c;
}

/*
 * This does not append a newline
 */
static void puts(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()

#endif
