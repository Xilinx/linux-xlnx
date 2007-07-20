/*
 * include/asm-microblaze/timex.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 * FIXME -- need review
 */

#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#define CLOCK_TICK_RATE 1000 /* Timer input freq. */

typedef unsigned long cycles_t;

#define get_cycles()	(0)

#endif /* _ASM_TIMEX_H */
