#ifndef __ASM_NIOS_BUGS_H
#define __ASM_NIOS_BUGS_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/bugs.h
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 *  Copyright (C) 1994  Linus Torvalds
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


/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

static void check_bugs(void)
{
}

#endif
