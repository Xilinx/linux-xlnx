/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013-2020 Xilinx, Inc.
 */

#ifndef _ASM_MICROBLAZE_ATOMIC_H
#define _ASM_MICROBLAZE_ATOMIC_H

#include <linux/types.h>
#include <asm/cmpxchg.h>

#define ATOMIC_INIT(i)	{ (i) }

#define arch_atomic_read(v)	READ_ONCE((v)->counter)

static inline void arch_atomic_set(atomic_t *v, int i)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1:	lwx	%0, %2, r0;\n"
		/* attempt store */
		"	swx	%3, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		/* Outputs: result value */
		: "=&r" (result), "=&r" (tmp)
		/* Inputs: counter address */
		: "r" (&v->counter), "r" (i)
		: "cc", "memory"
	);
}
#define arch_atomic_set	arch_atomic_set

/* Atomically perform op with v->counter and i, return result */
#define ATOMIC_OP_RETURN(op, asm)					\
static inline int arch_atomic_##op##_return_relaxed(int i, atomic_t *v)	\
{									\
	int result, tmp;						\
									\
	__asm__ __volatile__ (						\
		/* load conditional address in %2 to %0 */		\
		"1:	lwx	%0, %2, r0;\n"				\
		/* perform operation and save it to result */		\
		#asm		" %0, %3, %0;\n"			\
		/* attempt store */					\
		"	swx	%0, %2, r0;\n"				\
		/* checking msr carry flag */				\
		"	addic	%1, r0, 0;\n"				\
		/* store failed (MSR[C] set)? try again */		\
		"	bnei	%1, 1b;\n"				\
		/* Outputs: result value */				\
		: "=&r" (result), "=&r" (tmp)				\
		/* Inputs: counter address */				\
		: "r"   (&v->counter), "r" (i)				\
		: "cc", "memory"					\
	);								\
									\
	return result;							\
}									\
									\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
{									\
	arch_atomic_##op##_return_relaxed(i, v);				\
}

/* Atomically perform op with v->counter and i, return orig v->counter */
#define ATOMIC_FETCH_OP_RELAXED(op, asm)				\
static inline int arch_atomic_fetch_##op##_relaxed(int i, atomic_t *v)	\
{									\
	int old, tmp;							\
									\
	__asm__ __volatile__ (						\
		/* load conditional address in %2 to %0 */		\
		"1:	lwx	%0, %2, r0;\n"				\
		/* perform operation and save it to tmp */		\
		#asm		" %1, %3, %0;\n"			\
		/* attempt store */					\
		"	swx	%1, %2, r0;\n"				\
		/* checking msr carry flag */				\
		"	addic	%1, r0, 0;\n"				\
		/* store failed (MSR[C] set)? try again */		\
		"	bnei	%1, 1b;\n"				\
		/* Outputs: old value */				\
		: "=&r" (old), "=&r" (tmp)				\
		/* Inputs: counter address */				\
		: "r"   (&v->counter), "r" (i)				\
		: "cc", "memory"					\
	);								\
									\
	return old;							\
}

#define ATOMIC_OPS(op, asm) \
	ATOMIC_FETCH_OP_RELAXED(op, asm) \
	ATOMIC_OP_RETURN(op, asm)

ATOMIC_OPS(and, and)
#define arch_atomic_and			arch_atomic_and
#define arch_atomic_and_return_relaxed	arch_atomic_and_return_relaxed
#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed

ATOMIC_OPS(add, add)
#define arch_atomic_add			arch_atomic_add
#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed

ATOMIC_OPS(xor, xor)
#define arch_atomic_xor			arch_atomic_xor
#define arch_atomic_xor_return_relaxed	arch_atomic_xor_return_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed

ATOMIC_OPS(or, or)
#define arch_atomic_or			arch_atomic_or
#define arch_atomic_or_return_relaxed	arch_atomic_or_return_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed

ATOMIC_OPS(sub, rsub)
#define arch_atomic_sub			arch_atomic_sub
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed

static inline int arch_atomic_inc_return_relaxed(atomic_t *v)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1:	lwx	%0, %2, r0;\n"
		/* increment counter by 1 */
		"	addi	%0, %0, 1;\n"
		/* attempt store */
		"	swx	%0, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		/* Outputs: result value */
		: "=&r" (result), "=&r" (tmp)
		/* Inputs: counter address */
		: "r"   (&v->counter)
		: "cc", "memory"
	);

	return result;
}
#define arch_atomic_inc_return_relaxed	arch_atomic_inc_return_relaxed

#define arch_atomic_inc_and_test(v)	(arch_atomic_inc_return(v) == 0)

static inline int arch_atomic_dec_return(atomic_t *v)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1:	lwx	%0, %2, r0;\n"
		/* increment counter by -1 */
		"	addi	%0, %0, -1;\n"
		/* attempt store */
		"	swx	%0, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		/* Outputs: result value */
		: "=&r" (result), "=&r" (tmp)
		/* Inputs: counter address */
		: "r"   (&v->counter)
		: "cc", "memory"
	);

	return result;
}
#define arch_atomic_dec_return	arch_atomic_dec_return

static inline void arch_atomic_dec(atomic_t *v)
{
	arch_atomic_dec_return(v);
}
#define arch_atomic_dec	arch_atomic_dec

#define arch_atomic_sub_and_test(a, v)	(arch_atomic_sub_return((a), (v)) == 0)
#define arch_atomic_dec_and_test(v)	(arch_atomic_dec_return((v)) == 0)

#define arch_atomic_cmpxchg(v, o, n)	(arch_cmpxchg(&((v)->counter), (o), (n)))
#define arch_atomic_xchg(v, new)	(arch_xchg(&((v)->counter), new))

/**
 * atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1: lwx	 %0, %2, r0;\n"
		/* compare loaded value with old value*/
		"   cmp   %1, %0, %3;\n"
		/* equal to u, don't increment */
		"   beqid %1, 2f;\n"
		/* increment counter by i */
		"   add   %1, %0, %4;\n"
		/* attempt store of new value*/
		"   swx   %1, %2, r0;\n"
		/* checking msr carry flag */
		"   addic %1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"   bnei  %1, 1b;\n"
		"2:"
		/* Outputs: result value */
		: "=&r" (result), "=&r" (tmp)
		/* Inputs: counter address, old, new */
		: "r"   (&v->counter), "r" (u), "r" (a)
		: "cc", "memory"
	);

	return result;
}

/*
 * Atomically test *v and decrement if it is greater than 0.
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
static inline int arch_atomic_dec_if_positive(atomic_t *v)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1:	lwx	%0, %2, r0;\n"
		/* decrement counter by 1*/
		"	addi	%0, %0, -1;\n"
		/* if < 0 abort (*v was <= 0)*/
		"	blti	%0, 2f;\n"
		/* attempt store of new value*/
		"	swx	%0, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		"2: "
		/* Outputs: result value */
		: "=&r" (result), "=&r" (tmp)
		/* Inputs: counter address */
		: "r"   (&v->counter)
		: "cc", "memory"
	);

	return result;
}
#define arch_atomic_dec_if_positive	arch_atomic_dec_if_positive

#define arch_atomic_add_negative(i, v)	(arch_atomic_add_return(i, v) < 0)

#include <asm-generic/atomic64.h>

#endif /* _ASM_MICROBLAZE_ATOMIC_H */
