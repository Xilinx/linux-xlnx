/*
 * include/asm-microblaze/cache.h -- Cache operations
 *
 *  Copyright (C) 2003  John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef __MICROBLAZE_CACHE_H__
#define __MICROBLAZE_CACHE_H__

#include <asm/xparameters.h>
#include <asm/registers.h>
#include <asm/cpuinfo.h>
#include <linux/autoconf.h>

#ifndef L1_CACHE_BYTES
/* word-granular cache in microblaze */
#define L1_CACHE_BYTES		4
#define L1_CACHE_SHIFT		4
#endif

/* Assumes that caches are, in fact, enabled. */
static inline void __enable_icache(void) {
	if(cpuinfo->use_msr_instr) {
		__asm__ __volatile__ ("	msrset	r0, %0;			\
				nop;"					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory");
	} else {
		__asm__ __volatile__ ("				\
				mfs	r12, rmsr;			\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory", "r12");
	}
}

/* Assumes that caches are, in fact, enabled. */
static inline void __disable_icache(void) {
	if(cpuinfo->use_msr_instr) {
		__asm__ __volatile__ ("	msrclr r0, %0;		\
				nop;"					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory");
	} else {
		__asm__ __volatile__ ("				\
				mfs	r12, rmsr;			\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory", "r12");
	}
}

/* Assumes that caches are, in fact, enabled. */
static inline void __invalidate_icache(unsigned int addr) {
	__asm__ __volatile__ ("						\
				wic	%0, r0"				\
			:						\
			: "r" (addr));
}

static inline void enable_icache(void) {
	if(cpuinfo->use_icache) __enable_icache();
}

/* Assumes that caches are, in fact, enabled. */
static inline void __enable_dcache(void) {
	if(cpuinfo->use_msr_instr) {
		__asm__ __volatile__ (" msrset	r0, %0;		\
				nop;"					\
				:					\
				: "i" (MSR_DCE)				\
				: "memory");
	} else {
		__asm__ __volatile__ ("				\
				mfs	r12, rmsr;			\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (MSR_DCE)				\
				: "memory", "r12");
	}
}

/* Assumes that caches are, in fact, enabled. */
static inline void __disable_dcache(void) {
	if(cpuinfo->use_msr_instr) {
		__asm__ __volatile__ (" msrclr	r0, %0;		\
				nop;"					\
				:					\
				: "i" (MSR_DCE)				\
				: "memory");
	} else {
		__asm__ __volatile__ ("				\
				mfs	r12, rmsr;			\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				:					\
				: "i" (MSR_DCE)				\
				: "memory", "r12");
	}
}

/* Assumes that caches are, in fact, enabled. */
static inline void __invalidate_dcache(unsigned int addr) {
	__asm__ __volatile__ ("						\
				wdc	%0, r0"				\
			:						\
			: "r" (addr));
}

static inline void enable_dcache(void) {
	if(cpuinfo->use_dcache) __enable_dcache();
}

#ifdef CONFIG_XILINX_UNCACHED_SHADOW

#define UNCACHED_SHADOW_MASK (DDR_SDRAM_HIGHADDR + 1 - DDR_SDRAM_BASEADDR)

#endif

#endif /* __MICROBLAZE_CACHE_H__ */
