/*--------------------------------------------------------------------
 *
 * arch/nios2nommu/lib/string.c
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
 * Jun/09/2004		dgt	    Split out memcpy into separate source file
 *
 ---------------------------------------------------------------------*/

#include <linux/types.h>
#include <linux/autoconf.h>
#include <asm/nios.h>
#include <asm/string.h>

#ifdef __HAVE_ARCH_MEMSET
void * memset(void * s,int c,size_t count)
{

    if (count > 8) {
        int destptr, charcnt, dwordcnt, fill8reg, wrkrega;
        __asm__ __volatile__ (
                 // fill8 %3, %5 (c & 0xff)\n\t"
		 //
            "    slli    %4,    %5,   8\n\t"
            "    or      %4,    %4,   %5\n\t"
            "    slli    %3,    %4,   16\n\t"
            "    or      %3,    %3,   %4\n\t"
                 //
                 // Word-align %0 (s) if necessary
                 //
            "    andi    %4,    %0,   0x01\n\t"
            "    beq     %4,    zero, 1f\n\t"
            "    addi    %1,    %1,   -1\n\t"
            "    stb     %3,  0(%0)\n\t"
            "    addi    %0,    %0,   1\n\t"
            "1:  \n\t"
            "    mov     %2,    %1\n\t"
                 //
                 // Dword-align %0 (s) if necessary
                 //
            "    andi    %4,    %0,   0x02\n\t"
            "    beq     %4,    zero, 2f\n\t"
            "    addi    %1,    %1,   -2\n\t"
            "    sth     %3,  0(%0)\n\t"
            "    addi    %0,    %0,   2\n\t"
            "    mov     %2,    %1\n\t"
            "2:  \n\t"
                 // %1 and %2 are how many more bytes to set
                 //
            "    srli    %2,    %2,   2\n\t"
	         //
                 // %2 is how many dwords to set
	         //
            "3:  ;\n\t"
            "    stw     %3,  0(%0)\n\t"
            "    addi    %0,    %0,   4\n\t"
            "    addi    %2,    %2,   -1\n\t"
            "    bne     %2,    zero, 3b\n\t"
	         //
                 // store residual word and/or byte if necessary
                 //
            "    andi    %4,    %1,   0x02\n\t"
            "    beq     %4,    zero, 4f\n\t"
            "    sth     %3,  0(%0)\n\t"
            "    addi    %0,    %0,   2\n\t"
            "4:  \n\t"
                 // store residual byte if necessary
                 //
            "    andi    %4,    %1,   0x01\n\t"
            "    beq     %4,    zero, 5f\n\t"
            "    stb     %3,  0(%0)\n\t"
            "5:  \n\t"

            : "=r" (destptr),               /* %0  Output               */
              "=r" (charcnt),               /* %1  Output               */
              "=r" (dwordcnt),              /* %2  Output               */
              "=r" (fill8reg),              /* %3  Output               */
              "=r" (wrkrega)                /* %4  Output               */

            : "r" (c & 0xff),               /* %5  Input                */
              "0" (s),                      /* %0  Input/Output         */
              "1" (count)                   /* %1  Input/Output         */

            : "memory"                      /* clobbered                */
            );
	}
	else {
	char* xs=(char*)s;
	while (count--)
		*xs++ = c;
	}
	return s;
}
#endif

#ifdef __HAVE_ARCH_MEMMOVE
void * memmove(void * d, const void * s, size_t count)
{
    unsigned long dst, src;

    if (d < s) {
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
    } else {
	dst = (unsigned long) d + count;
	src = (unsigned long) s + count;

	if ((count < 8) || ((dst ^ src) & 3))
	    goto restdown;

	if (dst & 1) {
	    src--;
	    dst--;
	    count--;
		*(char*)dst=*(char*)src;
	}
	if (dst & 2) {
	    src -= 2;
	    dst -= 2;
	    count -= 2;
		*(short*)dst=*(short*)src;
	}
	while (count > 3) {
	    src -= 4;
	    dst -= 4;
	    count -= 4;
		*(long*)dst=*(long*)src;
	}

    restdown:
	while (count--) {
	    src--;
	    dst--;
		*(char*)dst=*(char*)src;
	}
    }

    return d;	

}
#endif
