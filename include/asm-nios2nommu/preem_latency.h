#ifndef _ASM_PREEM_LATENCY_H
#define _ASM_PREEM_LATENCY_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/preem_latency.h
 *
 * timing support for preempt-stats patch
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
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


#include <asm/nios.h>

#define readclock(low) \
do {\
	*(volatile unsigned long *)na_Counter_64_bit=1;	\
	low=*(volatile unsigned long *)na_Counter_64_bit; \
} while (0)
#define readclock_init()

#endif /* _ASM_PREEM_LATENCY_H */
