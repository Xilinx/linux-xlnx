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

#include <asm/registers.h>
#include <linux/autoconf.h>

#ifndef L1_CACHE_BYTES
/* word-granular cache in microblaze */
#define L1_CACHE_BYTES		4
#endif

/* Detect cache style used, to set cache line size*/
/* FSL caches (CacheLink) uses 16 byte cache line */
#if CONFIG_XILINX_MICROBLAZE0_ICACHE_USE_FSL
#define ICACHE_LINE_SIZE 16
#else
#define ICACHE_LINE_SIZE 4
#endif

#if CONFIG_XILINX_MICROBLAZE0_DCACHE_USE_FSL
#define DCACHE_LINE_SIZE 16
#else
#define DCACHE_LINE_SIZE 4
#endif

#if CONFIG_XILINX_MICROBLAZE0_USE_ICACHE		/* Cache support? */

#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
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


#else /* !CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */
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

 
#endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

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

#if CONFIG_XILINX_MICROBLAZE0_USE_DCACHE

#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
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

#else /* !CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */
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

#endif /* CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR */

#define __invalidate_dcache(addr)					\
	__asm__ __volatile__ ("						\
				wdc	%0, r0"				\
				:					\
				: "r" (addr))
#else
#define __enable_dcache()
#define __disable_dcache()
#define __invalidate_dcache(addr)
#endif /* CONFIG_XILINX_MICROBLAZE0_USE_DCACHE */

#endif /* __MICROBLAZE_CACHE_H__ */
