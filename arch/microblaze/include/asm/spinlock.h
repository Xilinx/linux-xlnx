/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013-2020 Xilinx, Inc.
 */

#ifndef _ASM_MICROBLAZE_SPINLOCK_H
#define _ASM_MICROBLAZE_SPINLOCK_H

/*
 * Unlocked value: 0
 * Locked value: 1
 */
#define arch_spin_is_locked(x)	(READ_ONCE((x)->lock) != 0)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	 %0, %1, r0;\n"
		/* not zero? try again */
		"	bnei	%0, 1b;\n"
		/* increment lock by 1 */
		"	addi	%0, r0, 1;\n"
		/* attempt store */
		"	swx	%0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&lock->lock)
		: "cc", "memory"
	);
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long prev, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %2 to %0 */
		"1:	lwx	 %0, %2, r0;\n"
		/* not zero? clear reservation */
		"	bneid	%0, 2f;\n"
		/* increment lock by one if lwx was sucessful */
		"	addi	%1, r0, 1;\n"
		/* attempt store */
		"	swx	%1, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		"2:"
		/* Outputs: temp variable for load result */
		: "=&r" (prev), "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&lock->lock)
		: "cc", "memory"
	);

	return (prev == 0);
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	%0, %1, r0;\n"
		/* clear */
		"	swx	r0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&lock->lock)
		: "cc", "memory"
	);
}

/* RWLOCKS */
static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	 %0, %1, r0;\n"
		/* not zero? try again */
		"	bneid	%0, 1b;\n"
		/* set tmp to -1 */
		"	addi	%0, r0, -1;\n"
		/* attempt store */
		"	swx	%0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long prev, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to tmp */
		"1:	lwx	%0, %2, r0;\n"
		/* not zero? abort */
		"	bneid	%0, 2f;\n"
		/* set tmp to -1 */
		"	addi	%1, r0, -1;\n"
		/* attempt store */
		"	swx	%1, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		"2:"
		/* Outputs: temp variable for load result */
		: "=&r" (prev), "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
	/* prev value should be zero and MSR should be clear */
	return (prev == 0);
}

static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	%0, %1, r0;\n"
		/* clear */
		"	swx	r0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
}

/* Read locks */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	%0, %1, r0;\n"
		/* < 0 (WRITE LOCK active) try again */
		"	bltid	%0, 1b;\n"
		/* increment lock by 1 if lwx was sucessful */
		"	addi	%0, %0, 1;\n"
		/* attempt store */
		"	swx	%0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to tmp */
		"1:	lwx	%0, %1, r0;\n"
		/* tmp = tmp - 1 */
		"	addi	%0, %0, -1;\n"
		/* attempt store */
		"	swx	%0, %1, r0;\n"
		/* checking msr carry flag */
		"	addic	%0, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%0, 1b;\n"
		/* Outputs: temp variable for load result */
		: "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long prev, tmp;

	__asm__ __volatile__ (
		/* load conditional address in %1 to %0 */
		"1:	lwx	%0, %2, r0;\n"
		/* < 0 bail, release lock */
		"	bltid	%0, 2f;\n"
		/* increment lock by 1 */
		"	addi	%1, %0, 1;\n"
		/* attempt store */
		"	swx	%1, %2, r0;\n"
		/* checking msr carry flag */
		"	addic	%1, r0, 0;\n"
		/* store failed (MSR[C] set)? try again */
		"	bnei	%1, 1b;\n"
		"2:"
		/* Outputs: temp variable for load result */
		: "=&r" (prev), "=&r" (tmp)
		/* Inputs: lock address */
		: "r" (&rw->lock)
		: "cc", "memory"
	);
	return (prev >= 0);
}

#endif /* _ASM_MICROBLAZE_SPINLOCK_H */
