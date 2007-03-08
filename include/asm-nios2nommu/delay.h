#ifndef _NIOS_DELAY_H
#define _NIOS_DELAY_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/delay.h
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
 * Jan/20/2004      dgt     NiosII
 *
 ---------------------------------------------------------------------*/


#include <asm/param.h>

extern __inline__ void __delay(unsigned long loops)
{
	int dummy;

	__asm__ __volatile__(
        "1:  \n\t"
        "    beq    %0,zero,2f\n\t"
        "    addi   %0, %0, -1\n\t" 
        "    br     1b\n\t" 
        "2:  \n\t" 

        :  "=r" (dummy)                     /* Need output for optimizer */

        :  "0" (loops)                      /* %0  Input                */
        );
}

/*
 * Note that 19 * 226 == 4294 ==~ 2^32 / 10^6, so
 * loops = (4294 * usecs * loops_per_jiffy * HZ) / 2^32.
 *
 * The mul instruction gives us loops = (a * b) / 2^32.
 * We choose a = usecs * 19 * HZ and b = loops_per_jiffy * 226
 * because this lets us support a wide range of HZ and
 * loops_per_jiffy values without either a or b overflowing 2^32.
 * Thus we need usecs * HZ <= (2^32 - 1) / 19 = 226050910 and
 * loops_per_jiffy <= (2^32 - 1) / 226 = 19004280
 * (which corresponds to ~3800 bogomips at HZ = 100).
 *  -- paulus
 */
#define __MAX_UDELAY	(226050910UL/HZ)	/* maximum udelay argument */
#define __MAX_NDELAY	(4294967295UL/HZ)	/* maximum ndelay argument */

extern unsigned long loops_per_jiffy;

extern __inline__ void __udelay(unsigned int x)
{
	unsigned int loops;

	__asm__("mulxuu %0,%1,%2" : "=r" (loops) :
		"r" (x), "r" (loops_per_jiffy * 226));
	__delay(loops);
}

extern __inline__ void __ndelay(unsigned int x)
{
	unsigned int loops;

	__asm__("mulxuu %0,%1,%2" : "=r" (loops) :
		"r" (x), "r" (loops_per_jiffy * 5));
	__delay(loops);
}

extern void __bad_udelay(void);		/* deliberately undefined */
extern void __bad_ndelay(void);		/* deliberately undefined */

#define udelay(n) (__builtin_constant_p(n)? \
	((n) > __MAX_UDELAY? __bad_udelay(): __udelay((n) * (19 * HZ))) : \
	__udelay((n) * (19 * HZ)))

#define ndelay(n) (__builtin_constant_p(n)? \
	((n) > __MAX_NDELAY? __bad_ndelay(): __ndelay((n) * HZ)) : \
	__ndelay((n) * HZ))

#define muldiv(a, b, c)    (((a)*(b))/(c))

#endif /* defined(_NIOS_DELAY_H) */
