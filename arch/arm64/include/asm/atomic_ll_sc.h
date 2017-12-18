/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_ATOMIC_LL_SC_H
#define __ASM_ATOMIC_LL_SC_H

#ifndef __ARM64_IN_ATOMIC_IMPL
#error "please don't include this file directly"
#endif

/*
 * AArch64 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 *
 * NOTE: these functions do *not* follow the PCS and must explicitly
 * save any clobbered registers other than x0 (regardless of return
 * value).  This is achieved through -fcall-saved-* compiler flags for
 * this file, which unfortunately don't work on a per-function basis
 * (the optimize attribute silently ignores these options).
 */

#define ATOMIC_OP(op, asm_op)						\
__LL_SC_INLINE void							\
__LL_SC_PREFIX(atomic_##op(int i, atomic_t *v))				\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "\n"				\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%w0, %2\n"						\
"	" #asm_op "	%w0, %w0, %w3\n"				\
"	stxr	%w1, %w0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i));							\
}									\
__LL_SC_EXPORT(atomic_##op);

#define ATOMIC_OP_RETURN(name, mb, acq, rel, cl, op, asm_op)		\
__LL_SC_INLINE int							\
__LL_SC_PREFIX(atomic_##op##_return##name(int i, atomic_t *v))		\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "_return" #name "\n"		\
"	prfm	pstl1strm, %2\n"					\
"1:	ld" #acq "xr	%w0, %2\n"					\
"	" #asm_op "	%w0, %w0, %w3\n"				\
"	st" #rel "xr	%w1, %w0, %2\n"					\
"	cbnz	%w1, 1b\n"						\
"	" #mb								\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i)							\
	: cl);								\
									\
	return result;							\
}									\
__LL_SC_EXPORT(atomic_##op##_return##name);

#define ATOMIC_FETCH_OP(name, mb, acq, rel, cl, op, asm_op)		\
__LL_SC_INLINE int							\
__LL_SC_PREFIX(atomic_fetch_##op##name(int i, atomic_t *v))		\
{									\
	unsigned long tmp;						\
	int val, result;						\
									\
	asm volatile("// atomic_fetch_" #op #name "\n"			\
"	prfm	pstl1strm, %3\n"					\
"1:	ld" #acq "xr	%w0, %3\n"					\
"	" #asm_op "	%w1, %w0, %w4\n"				\
"	st" #rel "xr	%w2, %w1, %3\n"					\
"	cbnz	%w2, 1b\n"						\
"	" #mb								\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: "Ir" (i)							\
	: cl);								\
									\
	return result;							\
}									\
__LL_SC_EXPORT(atomic_fetch_##op##name);

#define ATOMIC_OPS(...)							\
	ATOMIC_OP(__VA_ARGS__)						\
	ATOMIC_OP_RETURN(        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_OP_RETURN(_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_OP_RETURN(_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_OP_RETURN(_release,        ,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_FETCH_OP (_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_release,        ,  , l, "memory", __VA_ARGS__)

ATOMIC_OPS(add, add)
ATOMIC_OPS(sub, sub)

#undef ATOMIC_OPS
#define ATOMIC_OPS(...)							\
	ATOMIC_OP(__VA_ARGS__)						\
	ATOMIC_FETCH_OP (        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_FETCH_OP (_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_release,        ,  , l, "memory", __VA_ARGS__)

ATOMIC_OPS(and, and)
ATOMIC_OPS(andnot, bic)
ATOMIC_OPS(or, orr)
ATOMIC_OPS(xor, eor)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define ATOMIC64_OP(op, asm_op)						\
__LL_SC_INLINE void							\
__LL_SC_PREFIX(atomic64_##op(long i, atomic64_t *v))			\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "\n"				\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%0, %2\n"						\
"	" #asm_op "	%0, %0, %3\n"					\
"	stxr	%w1, %0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i));							\
}									\
__LL_SC_EXPORT(atomic64_##op);

#define ATOMIC64_OP_RETURN(name, mb, acq, rel, cl, op, asm_op)		\
__LL_SC_INLINE long							\
__LL_SC_PREFIX(atomic64_##op##_return##name(long i, atomic64_t *v))	\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "_return" #name "\n"		\
"	prfm	pstl1strm, %2\n"					\
"1:	ld" #acq "xr	%0, %2\n"					\
"	" #asm_op "	%0, %0, %3\n"					\
"	st" #rel "xr	%w1, %0, %2\n"					\
"	cbnz	%w1, 1b\n"						\
"	" #mb								\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i)							\
	: cl);								\
									\
	return result;							\
}									\
__LL_SC_EXPORT(atomic64_##op##_return##name);

#define ATOMIC64_FETCH_OP(name, mb, acq, rel, cl, op, asm_op)		\
__LL_SC_INLINE long							\
__LL_SC_PREFIX(atomic64_fetch_##op##name(long i, atomic64_t *v))	\
{									\
	long result, val;						\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_fetch_" #op #name "\n"		\
"	prfm	pstl1strm, %3\n"					\
"1:	ld" #acq "xr	%0, %3\n"					\
"	" #asm_op "	%1, %0, %4\n"					\
"	st" #rel "xr	%w2, %1, %3\n"					\
"	cbnz	%w2, 1b\n"						\
"	" #mb								\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: "Ir" (i)							\
	: cl);								\
									\
	return result;							\
}									\
__LL_SC_EXPORT(atomic64_fetch_##op##name);

#define ATOMIC64_OPS(...)						\
	ATOMIC64_OP(__VA_ARGS__)					\
	ATOMIC64_OP_RETURN(, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_release,,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_release,,  , l, "memory", __VA_ARGS__)

ATOMIC64_OPS(add, add)
ATOMIC64_OPS(sub, sub)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(...)						\
	ATOMIC64_OP(__VA_ARGS__)					\
	ATOMIC64_FETCH_OP (, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_release,,  , l, "memory", __VA_ARGS__)

ATOMIC64_OPS(and, and)
ATOMIC64_OPS(andnot, bic)
ATOMIC64_OPS(or, orr)
ATOMIC64_OPS(xor, eor)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

__LL_SC_INLINE long
__LL_SC_PREFIX(atomic64_dec_if_positive(atomic64_t *v))
{
	long result;
	unsigned long tmp;

	asm volatile("// atomic64_dec_if_positive\n"
"	prfm	pstl1strm, %2\n"
"1:	ldxr	%0, %2\n"
"	subs	%0, %0, #1\n"
"	b.lt	2f\n"
"	stlxr	%w1, %0, %2\n"
"	cbnz	%w1, 1b\n"
"	dmb	ish\n"
"2:"
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)
	:
	: "cc", "memory");

	return result;
}
__LL_SC_EXPORT(atomic64_dec_if_positive);

#define __CMPXCHG_CASE(w, sz, name, mb, acq, rel, cl)			\
__LL_SC_INLINE unsigned long						\
__LL_SC_PREFIX(__cmpxchg_case_##name(volatile void *ptr,		\
				     unsigned long old,			\
				     unsigned long new))		\
{									\
	unsigned long tmp, oldval;					\
									\
	asm volatile(							\
	"	prfm	pstl1strm, %[v]\n"				\
	"1:	ld" #acq "xr" #sz "\t%" #w "[oldval], %[v]\n"		\
	"	eor	%" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"	\
	"	cbnz	%" #w "[tmp], 2f\n"				\
	"	st" #rel "xr" #sz "\t%w[tmp], %" #w "[new], %[v]\n"	\
	"	cbnz	%w[tmp], 1b\n"					\
	"	" #mb "\n"						\
	"	mov	%" #w "[oldval], %" #w "[old]\n"		\
	"2:"								\
	: [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),			\
	  [v] "+Q" (*(unsigned long *)ptr)				\
	: [old] "Lr" (old), [new] "r" (new)				\
	: cl);								\
									\
	return oldval;							\
}									\
__LL_SC_EXPORT(__cmpxchg_case_##name);

__CMPXCHG_CASE(w, b,     1,        ,  ,  ,         )
__CMPXCHG_CASE(w, h,     2,        ,  ,  ,         )
__CMPXCHG_CASE(w,  ,     4,        ,  ,  ,         )
__CMPXCHG_CASE( ,  ,     8,        ,  ,  ,         )
__CMPXCHG_CASE(w, b, acq_1,        , a,  , "memory")
__CMPXCHG_CASE(w, h, acq_2,        , a,  , "memory")
__CMPXCHG_CASE(w,  , acq_4,        , a,  , "memory")
__CMPXCHG_CASE( ,  , acq_8,        , a,  , "memory")
__CMPXCHG_CASE(w, b, rel_1,        ,  , l, "memory")
__CMPXCHG_CASE(w, h, rel_2,        ,  , l, "memory")
__CMPXCHG_CASE(w,  , rel_4,        ,  , l, "memory")
__CMPXCHG_CASE( ,  , rel_8,        ,  , l, "memory")
__CMPXCHG_CASE(w, b,  mb_1, dmb ish,  , l, "memory")
__CMPXCHG_CASE(w, h,  mb_2, dmb ish,  , l, "memory")
__CMPXCHG_CASE(w,  ,  mb_4, dmb ish,  , l, "memory")
__CMPXCHG_CASE( ,  ,  mb_8, dmb ish,  , l, "memory")

#undef __CMPXCHG_CASE

#define __CMPXCHG_DBL(name, mb, rel, cl)				\
__LL_SC_INLINE long							\
__LL_SC_PREFIX(__cmpxchg_double##name(unsigned long old1,		\
				      unsigned long old2,		\
				      unsigned long new1,		\
				      unsigned long new2,		\
				      volatile void *ptr))		\
{									\
	unsigned long tmp, ret;						\
									\
	asm volatile("// __cmpxchg_double" #name "\n"			\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ldxp	%0, %1, %2\n"					\
	"	eor	%0, %0, %3\n"					\
	"	eor	%1, %1, %4\n"					\
	"	orr	%1, %0, %1\n"					\
	"	cbnz	%1, 2f\n"					\
	"	st" #rel "xp	%w0, %5, %6, %2\n"			\
	"	cbnz	%w0, 1b\n"					\
	"	" #mb "\n"						\
	"2:"								\
	: "=&r" (tmp), "=&r" (ret), "+Q" (*(unsigned long *)ptr)	\
	: "r" (old1), "r" (old2), "r" (new1), "r" (new2)		\
	: cl);								\
									\
	return ret;							\
}									\
__LL_SC_EXPORT(__cmpxchg_double##name);

__CMPXCHG_DBL(   ,        ,  ,         )
__CMPXCHG_DBL(_mb, dmb ish, l, "memory")

#undef __CMPXCHG_DBL

#endif	/* __ASM_ATOMIC_LL_SC_H */
