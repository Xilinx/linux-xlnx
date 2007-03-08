#ifndef _NIOS2NOMMU_SEMAPHORE_HELPER_H
#define _NIOS2NOMMU_SEMAPHORE_HELPER_H

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/semaphore.h
 *
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * Derived from M68knommu
 *
 * (C) Copyright 1996 Linus Torvalds
 * m68k version by Andreas Schwab
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/

/*
 * These two _must_ execute atomically wrt each other.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 0;
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 */
static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 0;
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 1;
	} else if (signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 */
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 1;
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 0;
	} else
		atomic_inc(&sem->count);
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

#endif
