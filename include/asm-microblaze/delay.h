/*
 * include/asm-microblaze/delay.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_DELAY_H
#define _ASM_DELAY_H

#include <asm/param.h>

static inline void __delay(unsigned long loops)
{
	asm volatile ("# __delay		\n\t"		\
		      "1: addi	%0, %0, -1	\t\n"		\
		      "bneid	%0, 1b		\t\n"		\
		      "nop			\t\n"
		      : "=r" (loops)
		      : "0" (loops));
}

static inline void udelay(unsigned long usec)
{
	unsigned long long tmp = usec;
	unsigned long loops = (tmp * 4295 * HZ * loops_per_jiffy) >> 32;
	__delay(loops);
}

#endif /* _ASM_DELAY_H */
