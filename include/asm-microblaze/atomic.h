/*
 * include/asm-microblaze/atomic.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <asm/bug.h> /* FIXME */

typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)		{ (i) }
#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = (i))

#define atomic_inc(v)		(atomic_add_return(1, (v)))
#define atomic_dec(v)		(atomic_sub_return(1, (v)))

#define atomic_add(i, v)	(atomic_add_return(i, (v)))
#define atomic_sub(i, v)	(atomic_sub_return(i, (v)))

#define atomic_inc_return(v)	(atomic_add_return(1, (v)))
#define atomic_dec_return(v)	(atomic_sub_return(1, (v)))

#define atomic_inc_and_test(v)	(atomic_add_return(1, (v)) == 0)
#define atomic_dec_and_test(v)	(atomic_sub_return(1, (v)) == 0)

#define atomic_inc_not_zero(v)	(atomic_add_unless((v), 1, 0))

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	local_irq_restore(flags);

	return ret;
}

static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;

	c = atomic_read(v);
	while (c != u && (old = atomic_cmpxchg((v), c, c + a)) != c)
		c = old;
	return c != u;
}

static inline void atomic_clear_mask(unsigned long mask, unsigned long *addr)
{
	unsigned long flags;

	local_irq_save(flags);
	*addr &= ~mask;
	local_irq_restore(flags);
}

/**
 * atomic_add_return - add and return
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v and returns @i + @v
 */
static __inline__ int atomic_add_return(int i, atomic_t *v)
{
	unsigned long flags;
	int val;

	local_irq_save(flags);
	val = v->counter;
	v->counter = val += i;
	local_irq_restore(flags);

	return val;
}

static inline int atomic_sub_return(int i, atomic_t * v)
{
	return atomic_add_return(-i, v);
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

/* FIXME */
//#include <asm-generic/atomic.h>

#endif /* _ASM_ATOMIC_H */
