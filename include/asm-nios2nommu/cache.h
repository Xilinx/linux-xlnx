/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __ARCH_NIOS2NOMMU_CACHE_H
#define __ARCH_NIOS2NOMMU_CACHE_H

#include <asm/nios.h>

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES	nasys_icache_line_size 	/* 32, this need to be at least 1 */
#define L1_CACHE_ALIGN(x)	(((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))
#define L1_CACHE_SHIFT	5


#define __cacheline_aligned
#define ____cacheline_aligned

#endif
