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
#include <linux/autoconf.h>

#ifndef L1_CACHE_BYTES
/* word-granular cache in microblaze */
#define L1_CACHE_BYTES		4
#define L1_CACHE_SHIFT		4
#endif

/* Define MSR enable bits for instruction and data caches */
#define ICACHE_MSR_BIT (1 << 5)
#define DCACHE_MSR_BIT (1 << 7)

#if XPAR_MICROBLAZE_0_USE_ICACHE==1		/* Cache support? */

#if XPAR_MICROBLAZE_0_USE_MSR_INSTR==1

#define __enable_icache()						\
	__asm__ __volatile__ ("	msrset	r0, %0;				\
				nop;"					\
				: 					\
				: "i" (MSR_ICE)			\
				: "memory")

#define __disable_icache()						\
	__asm__ __volatile__ ("	msrclr r0, %0;				\
				nop;"					\
				: 					\
				: "i" (MSR_ICE)			\
				: "memory")


#else /* !XPAR_MICROBLAZE_0_USE_MSR_INSTR */
#define __enable_icache()						\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (MSR_ICE)			\
				: "memory", "r12")

#define __disable_icache()						\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (MSR_ICE)			\
				: "memory", "r12")

 
#endif /* XPAR_MICROBLAZE_0_USE_MSR_INSTR */

#define __invalidate_icache(addr)					\
	__asm__ __volatile__ ("						\
				wic	%0, r0"				\
				:					\
				: "r" (addr))
#else
#define __enable_icache()
#define __disable_icache()
#define __invalidate_icache(addr)
#endif

#if XPAR_MICROBLAZE_0_USE_DCACHE==1

#if XPAR_MICROBLAZE_0_USE_MSR_INSTR==1
#define __enable_dcache()						\
	__asm__ __volatile__ (" msrset	r0, %0;				\
				nop;"					\
				: 					\
				: "i" (MSR_DCE)			\
				: "memory")

#define __disable_dcache()						\
	__asm__ __volatile__ (" msrclr	r0, %0;				\
				nop;"					\
				: 					\
				: "i" (MSR_DCE)			\
				: "memory")

#else /* !XPAR_MICROBLAZE_0_USE_MSR_INSTR */
#define __enable_dcache()						\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (MSR_DCE)			\
				: "memory", "r12")

#define __disable_dcache()						\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (MSR_DCE)			\
				: "memory", "r12")

#endif /* XPAR_MICROBLAZE_0_USE_MSR_INSTR */

#define __invalidate_dcache(addr)					\
	__asm__ __volatile__ ("						\
				wdc	%0, r0"				\
				:					\
				: "r" (addr))
#else
#define __enable_dcache()
#define __disable_dcache()
#define __invalidate_dcache(addr)
#endif /* XPAR_MICROBLAZE_0_USE_DCACHE */

#ifdef CONFIG_XILINX_UNCACHED_SHADOW

#define UNCACHED_SHADOW_MASK (DDR_SDRAM_HIGHADDR + 1 - DDR_SDRAM_BASEADDR) 

#endif

#endif /* __MICROBLAZE_CACHE_H__ */
