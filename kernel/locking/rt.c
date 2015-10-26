/*
 * kernel/rt.c
 *
 * Real-Time Preemption Support
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * historic credit for proving that Linux spinlocks can be implemented via
 * RT-aware mutexes goes to many people: The Pmutex project (Dirk Grambow
 * and others) who prototyped it on 2.4 and did lots of comparative
 * research and analysis; TimeSys, for proving that you can implement a
 * fully preemptible kernel via the use of IRQ threading and mutexes;
 * Bill Huey for persuasively arguing on lkml that the mutex model is the
 * right one; and to MontaVista, who ported pmutexes to 2.6.
 *
 * This code is a from-scratch implementation and is not based on pmutexes,
 * but the idea of converting spinlocks to mutexes is used here too.
 *
 * lock debugging, locking tree, deadlock detection:
 *
 *  Copyright (C) 2004, LynuxWorks, Inc., Igor Manyilov, Bill Huey
 *  Released under the General Public License (GPL).
 *
 * Includes portions of the generic R/W semaphore implementation from:
 *
 *  Copyright (c) 2001   David Howells (dhowells@redhat.com).
 *  - Derived partially from idea by Andrea Arcangeli <andrea@suse.de>
 *  - Derived also from comments by Linus
 *
 * Pending ownership of locks and ownership stealing:
 *
 *  Copyright (C) 2005, Kihon Technologies Inc., Steven Rostedt
 *
 *   (also by Steven Rostedt)
 *    - Converted single pi_lock to individual task locks.
 *
 * By Esben Nielsen:
 *    Doing priority inheritance with help of the scheduler.
 *
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *  - major rework based on Esben Nielsens initial patch
 *  - replaced thread_info references by task_struct refs
 *  - removed task->pending_owner dependency
 *  - BKL drop/reacquire for semaphore style locks to avoid deadlocks
 *    in the scheduler return path as discussed with Steven Rostedt
 *
 *  Copyright (C) 2006, Kihon Technologies Inc.
 *    Steven Rostedt <rostedt@goodmis.org>
 *  - debugged and patched Thomas Gleixner's rework.
 *  - added back the cmpxchg to the rework.
 *  - turned atomic require back on for SMP.
 */

#include <linux/spinlock.h>
#include <linux/rtmutex.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/plist.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/hrtimer.h>

#include "rtmutex_common.h"

/*
 * struct mutex functions
 */
void __mutex_do_init(struct mutex *mutex, const char *name,
		     struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)mutex, sizeof(*mutex));
	lockdep_init_map(&mutex->dep_map, name, key, 0);
#endif
	mutex->lock.save_state = 0;
}
EXPORT_SYMBOL(__mutex_do_init);

void __lockfunc _mutex_lock(struct mutex *lock)
{
	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	rt_mutex_lock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_lock);

int __lockfunc _mutex_lock_interruptible(struct mutex *lock)
{
	int ret;

	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	ret = rt_mutex_lock_interruptible(&lock->lock);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_interruptible);

int __lockfunc _mutex_lock_killable(struct mutex *lock)
{
	int ret;

	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	ret = rt_mutex_lock_killable(&lock->lock);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_killable);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __lockfunc _mutex_lock_nested(struct mutex *lock, int subclass)
{
	mutex_acquire_nest(&lock->dep_map, subclass, 0, NULL, _RET_IP_);
	rt_mutex_lock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_lock_nested);

void __lockfunc _mutex_lock_nest_lock(struct mutex *lock, struct lockdep_map *nest)
{
	mutex_acquire_nest(&lock->dep_map, 0, 0, nest, _RET_IP_);
	rt_mutex_lock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_lock_nest_lock);

int __lockfunc _mutex_lock_interruptible_nested(struct mutex *lock, int subclass)
{
	int ret;

	mutex_acquire_nest(&lock->dep_map, subclass, 0, NULL, _RET_IP_);
	ret = rt_mutex_lock_interruptible(&lock->lock);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_interruptible_nested);

int __lockfunc _mutex_lock_killable_nested(struct mutex *lock, int subclass)
{
	int ret;

	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	ret = rt_mutex_lock_killable(&lock->lock);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_killable_nested);
#endif

int __lockfunc _mutex_trylock(struct mutex *lock)
{
	int ret = rt_mutex_trylock(&lock->lock);

	if (ret)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(_mutex_trylock);

void __lockfunc _mutex_unlock(struct mutex *lock)
{
	mutex_release(&lock->dep_map, 1, _RET_IP_);
	rt_mutex_unlock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_unlock);

/*
 * rwlock_t functions
 */
int __lockfunc rt_write_trylock(rwlock_t *rwlock)
{
	int ret;

	migrate_disable();
	ret = rt_mutex_trylock(&rwlock->lock);
	if (ret)
		rwlock_acquire(&rwlock->dep_map, 0, 1, _RET_IP_);
	else
		migrate_enable();

	return ret;
}
EXPORT_SYMBOL(rt_write_trylock);

int __lockfunc rt_write_trylock_irqsave(rwlock_t *rwlock, unsigned long *flags)
{
	int ret;

	*flags = 0;
	ret = rt_write_trylock(rwlock);
	return ret;
}
EXPORT_SYMBOL(rt_write_trylock_irqsave);

int __lockfunc rt_read_trylock(rwlock_t *rwlock)
{
	struct rt_mutex *lock = &rwlock->lock;
	int ret = 1;

	/*
	 * recursive read locks succeed when current owns the lock,
	 * but not when read_depth == 0 which means that the lock is
	 * write locked.
	 */
	if (rt_mutex_owner(lock) != current) {
		migrate_disable();
		ret = rt_mutex_trylock(lock);
		if (ret)
			rwlock_acquire(&rwlock->dep_map, 0, 1, _RET_IP_);
		else
			migrate_enable();

	} else if (!rwlock->read_depth) {
		ret = 0;
	}

	if (ret)
		rwlock->read_depth++;

	return ret;
}
EXPORT_SYMBOL(rt_read_trylock);

void __lockfunc rt_write_lock(rwlock_t *rwlock)
{
	rwlock_acquire(&rwlock->dep_map, 0, 0, _RET_IP_);
	migrate_disable();
	__rt_spin_lock(&rwlock->lock);
}
EXPORT_SYMBOL(rt_write_lock);

void __lockfunc rt_read_lock(rwlock_t *rwlock)
{
	struct rt_mutex *lock = &rwlock->lock;


	/*
	 * recursive read locks succeed when current owns the lock
	 */
	if (rt_mutex_owner(lock) != current) {
		migrate_disable();
		rwlock_acquire(&rwlock->dep_map, 0, 0, _RET_IP_);
		__rt_spin_lock(lock);
	}
	rwlock->read_depth++;
}

EXPORT_SYMBOL(rt_read_lock);

void __lockfunc rt_write_unlock(rwlock_t *rwlock)
{
	/* NOTE: we always pass in '1' for nested, for simplicity */
	rwlock_release(&rwlock->dep_map, 1, _RET_IP_);
	__rt_spin_unlock(&rwlock->lock);
	migrate_enable();
}
EXPORT_SYMBOL(rt_write_unlock);

void __lockfunc rt_read_unlock(rwlock_t *rwlock)
{
	/* Release the lock only when read_depth is down to 0 */
	if (--rwlock->read_depth == 0) {
		rwlock_release(&rwlock->dep_map, 1, _RET_IP_);
		__rt_spin_unlock(&rwlock->lock);
		migrate_enable();
	}
}
EXPORT_SYMBOL(rt_read_unlock);

unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock)
{
	rt_write_lock(rwlock);

	return 0;
}
EXPORT_SYMBOL(rt_write_lock_irqsave);

unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock)
{
	rt_read_lock(rwlock);

	return 0;
}
EXPORT_SYMBOL(rt_read_lock_irqsave);

void __rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)rwlock, sizeof(*rwlock));
	lockdep_init_map(&rwlock->dep_map, name, key, 0);
#endif
	rwlock->lock.save_state = 1;
	rwlock->read_depth = 0;
}
EXPORT_SYMBOL(__rt_rwlock_init);

/*
 * rw_semaphores
 */

void  rt_up_write(struct rw_semaphore *rwsem)
{
	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
	rt_mutex_unlock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_up_write);


void  __rt_up_read(struct rw_semaphore *rwsem)
{
	if (--rwsem->read_depth == 0)
		rt_mutex_unlock(&rwsem->lock);
}

void  rt_up_read(struct rw_semaphore *rwsem)
{
	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
	__rt_up_read(rwsem);
}
EXPORT_SYMBOL(rt_up_read);

/*
 * downgrade a write lock into a read lock
 * - just wake up any readers at the front of the queue
 */
void  rt_downgrade_write(struct rw_semaphore *rwsem)
{
	BUG_ON(rt_mutex_owner(&rwsem->lock) != current);
	rwsem->read_depth = 1;
}
EXPORT_SYMBOL(rt_downgrade_write);

int  rt_down_write_trylock(struct rw_semaphore *rwsem)
{
	int ret = rt_mutex_trylock(&rwsem->lock);

	if (ret)
		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(rt_down_write_trylock);

void  rt_down_write(struct rw_semaphore *rwsem)
{
	rwsem_acquire(&rwsem->dep_map, 0, 0, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_down_write);

void  rt_down_write_nested(struct rw_semaphore *rwsem, int subclass)
{
	rwsem_acquire(&rwsem->dep_map, subclass, 0, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_down_write_nested);

void rt_down_write_nested_lock(struct rw_semaphore *rwsem,
			       struct lockdep_map *nest)
{
	rwsem_acquire_nest(&rwsem->dep_map, 0, 0, nest, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_down_write_nested_lock);

int  rt_down_read_trylock(struct rw_semaphore *rwsem)
{
	struct rt_mutex *lock = &rwsem->lock;
	int ret = 1;

	/*
	 * recursive read locks succeed when current owns the rwsem,
	 * but not when read_depth == 0 which means that the rwsem is
	 * write locked.
	 */
	if (rt_mutex_owner(lock) != current)
		ret = rt_mutex_trylock(&rwsem->lock);
	else if (!rwsem->read_depth)
		ret = 0;

	if (ret) {
		rwsem->read_depth++;
		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
	}
	return ret;
}
EXPORT_SYMBOL(rt_down_read_trylock);

static void __rt_down_read(struct rw_semaphore *rwsem, int subclass)
{
	struct rt_mutex *lock = &rwsem->lock;

	rwsem_acquire_read(&rwsem->dep_map, subclass, 0, _RET_IP_);

	if (rt_mutex_owner(lock) != current)
		rt_mutex_lock(&rwsem->lock);
	rwsem->read_depth++;
}

void  rt_down_read(struct rw_semaphore *rwsem)
{
	__rt_down_read(rwsem, 0);
}
EXPORT_SYMBOL(rt_down_read);

void  rt_down_read_nested(struct rw_semaphore *rwsem, int subclass)
{
	__rt_down_read(rwsem, subclass);
}
EXPORT_SYMBOL(rt_down_read_nested);

void  __rt_rwsem_init(struct rw_semaphore *rwsem, const char *name,
			      struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)rwsem, sizeof(*rwsem));
	lockdep_init_map(&rwsem->dep_map, name, key, 0);
#endif
	rwsem->read_depth = 0;
	rwsem->lock.save_state = 0;
}
EXPORT_SYMBOL(__rt_rwsem_init);

/**
 * atomic_dec_and_mutex_lock - return holding mutex if we dec to 0
 * @cnt: the atomic which we are to dec
 * @lock: the mutex to return holding if we dec to 0
 *
 * return true and hold lock if we dec to 0, return false otherwise
 */
int atomic_dec_and_mutex_lock(atomic_t *cnt, struct mutex *lock)
{
	/* dec if we can't possibly hit 0 */
	if (atomic_add_unless(cnt, -1, 1))
		return 0;
	/* we might hit 0, so take the lock */
	mutex_lock(lock);
	if (!atomic_dec_and_test(cnt)) {
		/* when we actually did the dec, we didn't hit 0 */
		mutex_unlock(lock);
		return 0;
	}
	/* we hit 0, and we hold the lock */
	return 1;
}
EXPORT_SYMBOL(atomic_dec_and_mutex_lock);
