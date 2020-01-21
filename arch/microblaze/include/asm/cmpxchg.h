/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MICROBLAZE_CMPXCHG_H
#define _ASM_MICROBLAZE_CMPXCHG_H

#ifndef CONFIG_SMP
# include <asm-generic/cmpxchg.h>
#else

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg_u32(volatile void *p, unsigned long val)
{
	unsigned long prev, temp;

	__asm__ __volatile__ (
		/* load conditional address in %3 to %0 */
		"1:	lwx	%0, %3, r0;\n"
		/* attempt store of new value */
		"	swx	%4, %3, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		/* Outputs: result value */
		: "=&r" (prev), "=&r" (temp), "+m" (*(volatile unsigned int *)p)
		/* Inputs: counter address */
		: "r"   (p), "r" (val)
		: "cc", "memory"
	);

	return prev;
}

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
								int size)
{
	if (size == 4)
		return __xchg_u32(ptr, x);

	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr, x) ({							\
	((__typeof__(*(ptr)))						\
		__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))));	\
})

static inline unsigned long __cmpxchg_u32(volatile unsigned int *p,
					  unsigned long old, unsigned long new)
{
	int result, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %3 to %0 */
		"1:	lwx	%0, %3, r0;\n"
		/* compare loaded value with old value */
		"	cmp	%2, %0, %4;\n"
		/* not equal to old value, write old value */
		"	bnei	%2, 2f;\n"
		/* attempt store of new value*/
		"	swx	%5, %3, r0;\n"
		/* checking msr carry flag */
		"	addic	%2, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%2, 1b;\n"
		"2: "
		/* Outputs : result value */
		: "=&r" (result), "+m" (*p), "=&r" (tmp)
		/* Inputs  : counter address, old, new */
		: "r"   (p), "r" (old), "r" (new), "r" (&tmp)
		: "cc", "memory"
	);

	return result;
}

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, unsigned int size)
{
	if (size == 4)
		return __cmpxchg_u32(ptr, old, new);

	__xchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n) ({						\
	((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))));		\
})


#endif

#endif /* _ASM_MICROBLAZE_CMPXCHG_H */
