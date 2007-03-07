/*
 * include/asm-microblaze/cache.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_CACHE_H
#define _ASM_CACHE_H

#define L1_CACHE_BYTES	4 /* FIXME */

#define __enable_icache()						\
	asm volatile ("msrset	r0, %0	\n\t"				\
		      :							\
		      : "i"(MSR_ICE)					\
		      : "memory")

#define __disable_icache()						\
	asm volatile ("msrclr	r0, %0	\n\t"				\
		      :							\
		      : "i"(MSR_ICE)					\
		      : "memory")

#define __invalidate_icache(addr)					\
	asm volatile ("wic	%0, r0	\n\t"				\
		      :							\
		      : "r"(addr))

#define __enable_dcache()						\
	asm volatile ("msrset	r0, %0	\n\t"				\
		      :							\
		      : "i" (MSR_DCE)					\
		      : "memory")

#define __disable_dcache()						\
	asm volatile ("msrclr	r0, %0	\n\t"				\
		      :							\
		      : "i" (MSR_DCE)					\
		      : "memory")

#define __invalidate_dcache(addr)					\
	asm volatile ("wdc	%0, r0"					\
		      :							\
		      : "r" (addr))

#endif /* _ASM_CACHE_H */
