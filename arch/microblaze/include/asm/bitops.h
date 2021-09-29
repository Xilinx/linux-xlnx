/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Microblaze atomic bit operations.
 *
 * Copyright (C) 2013 - 2020 Xilinx, Inc.
 *
 * Merged version by David Gibson <david@gibson.dropbear.id.au>.
 * Based on ppc64 versions by: Dave Engebretsen, Todd Inglett, Don
 * Reed, Pat McCarthy, Peter Bergner, Anton Blanchard.  They
 * originally took it from the ppc32 code.
 *
 * Within a word, bits are numbered LSB first.  Lot's of places make
 * this assumption by directly testing bits with (val & (1<<nr)).
 * This can cause confusion for large (> 1 word) bitmaps on a
 * big-endian system because, unlike little endian, the number of each
 * bit depends on the word size.
 *
 * The bitop functions are defined to work on unsigned longs, so for a
 * ppc64 system the bits end up numbered:
 *   |63..............0|127............64|191...........128|255...........196|
 * and on ppc32:
 *   |31.....0|63....31|95....64|127...96|159..128|191..160|223..192|255..224|
 *
 * There are a few little-endian macros used mostly for filesystem
 * bitmaps, these work on similar bit arrays layouts, but
 * byte-oriented:
 *   |7...0|15...8|23...16|31...24|39...32|47...40|55...48|63...56|
 *
 * The main difference is that bit 3-5 (64b) or 3-4 (32b) in the bit
 * number field needs to be reversed compared to the big-endian bit
 * fields. This can be achieved by XOR with 0x38 (64b) or 0x18 (32b).
 */

#ifndef _ASM_MICROBLAZE_BITOPS_H
#define _ASM_MICROBLAZE_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/types.h>
#include <linux/compiler.h>
#include <asm/asm-compat.h>
#include <asm/barrier.h>
#include <linux/stringify.h>

/*
 * clear_bit doesn't imply a memory barrier
 */
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()

#define BITOP_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

/* Macro for generating the ***_bits() functions */
#define DEFINE_BITOP(fn, op)						\
static inline void fn(unsigned long mask, volatile unsigned long *_p)	\
{									\
	unsigned long tmp;						\
	unsigned long *p = (unsigned long *)_p;				\
									\
	__asm__ __volatile__ (						\
		/* load conditional address in %2 to %0 */		\
		"1:	lwx		%0, %3, r0;\n"			\
		/* perform bit operation with mask */			\
		stringify_in_c(op)"	%0, %0, %2;\n"			\
		/* attempt store */					\
		"	swx		%0, %3, r0;\n"			\
		/* checking msr carry flag */				\
		"	addic		%0, r0, 0;\n"			\
		/* store failed (MSR[C] set)? try again */		\
		"	bnei		%0, 1b;\n"			\
		: "=&r" (tmp), "+m" (*p)  /* Outputs: tmp, p */		\
		: "r" (mask), "r" (p)     /* Inputs: mask, p */		\
		: "cc", "memory"					\
	);								\
}

DEFINE_BITOP(set_bits, or)
DEFINE_BITOP(clear_bits, andn)
DEFINE_BITOP(clear_bits_unlock, andn)
DEFINE_BITOP(change_bits, xor)

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	set_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr));
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	clear_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr));
}

static inline void clear_bit_unlock(int nr, volatile unsigned long *addr)
{
	clear_bits_unlock(BITOP_MASK(nr), addr + BITOP_WORD(nr));
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	change_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr));
}

/*
 * Like DEFINE_BITOP(), with changes to the arguments to 'op' and the output
 * operands.
 */
#define DEFINE_TESTOP(fn, op)						\
static inline unsigned long fn(unsigned long mask,			\
			       volatile unsigned long *_p)		\
{									\
	unsigned long old, tmp;						\
	unsigned long *p = (unsigned long *)_p;				\
									\
	__asm__ __volatile__ (						\
		/* load conditional address in %4 to %0 */		\
		"1:	lwx		%0, %4, r0;\n"			\
		/* perform bit operation with mask */			\
		stringify_in_c(op)"	%1, %0, %3;\n"			\
		/* attempt store */					\
		"	swx		%1, %4, r0;\n"			\
		/* checking msr carry flag */				\
		"	addic		%1, r0, 0;\n"			\
		/* store failed (MSR[C] set)? try again */		\
		"	bnei		%1, 1b;\n"			\
		/* Outputs: old, tmp, p */				\
		: "=&r" (old), "=&r" (tmp), "+m" (*p)			\
		 /* Inputs: mask, p */					\
		: "r" (mask), "r" (p)					\
		: "cc", "memory"					\
	);								\
	return (old & mask);						\
}

DEFINE_TESTOP(test_and_set_bits, or)
DEFINE_TESTOP(test_and_set_bits_lock, or)
DEFINE_TESTOP(test_and_clear_bits, andn)
DEFINE_TESTOP(test_and_change_bits, xor)

static inline int test_and_set_bit(unsigned long nr,
				       volatile unsigned long *addr)
{
	return test_and_set_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr)) != 0;
}

static inline int test_and_set_bit_lock(unsigned long nr,
				       volatile unsigned long *addr)
{
	return test_and_set_bits_lock(BITOP_MASK(nr),
				addr + BITOP_WORD(nr)) != 0;
}

static inline int test_and_clear_bit(unsigned long nr,
					 volatile unsigned long *addr)
{
	return test_and_clear_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr)) != 0;
}

static inline int test_and_change_bit(unsigned long nr,
					  volatile unsigned long *addr)
{
	return test_and_change_bits(BITOP_MASK(nr), addr + BITOP_WORD(nr)) != 0;
}

#include <asm-generic/bitops/non-atomic.h>

static inline void __clear_bit_unlock(int nr, volatile unsigned long *addr)
{
	__clear_bit(nr, addr);
}

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/fls64.h>

/* Little-endian versions */
#include <asm-generic/bitops/le.h>

/* Bitmap functions for the ext2 filesystem */
#include <asm-generic/bitops/ext2-atomic-setbit.h>

#include <asm-generic/bitops/sched.h>

#endif /* _ASM_MICROBLAZE_BITOPS_H */
