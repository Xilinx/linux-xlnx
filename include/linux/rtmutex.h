/*
 * RT Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains the public data structure and API definitions.
 */

#ifndef __LINUX_RT_MUTEX_H
#define __LINUX_RT_MUTEX_H

#include <linux/linkage.h>
#include <linux/rbtree.h>
#include <linux/spinlock_types_raw.h>

extern int max_lock_depth; /* for sysctl */

#ifdef CONFIG_DEBUG_MUTEXES
#include <linux/debug_locks.h>
#endif

/**
 * The rt_mutex structure
 *
 * @wait_lock:	spinlock to protect the structure
 * @waiters:	rbtree root to enqueue waiters in priority order
 * @waiters_leftmost: top waiter
 * @owner:	the mutex owner
 */
struct rt_mutex {
	raw_spinlock_t		wait_lock;
	struct rb_root          waiters;
	struct rb_node          *waiters_leftmost;
	struct task_struct	*owner;
	int			save_state;
#ifdef CONFIG_DEBUG_RT_MUTEXES
	const char 		*name, *file;
	int			line;
	void			*magic;
#endif
};

struct rt_mutex_waiter;
struct hrtimer_sleeper;

#ifdef CONFIG_DEBUG_RT_MUTEXES
 extern int rt_mutex_debug_check_no_locks_freed(const void *from,
						unsigned long len);
 extern void rt_mutex_debug_check_no_locks_held(struct task_struct *task);
#else
 static inline int rt_mutex_debug_check_no_locks_freed(const void *from,
						       unsigned long len)
 {
	return 0;
 }
# define rt_mutex_debug_check_no_locks_held(task)	do { } while (0)
#endif

# define rt_mutex_init(mutex)					\
	do {							\
		raw_spin_lock_init(&(mutex)->wait_lock);	\
		__rt_mutex_init(mutex, #mutex);			\
	} while (0)

#ifdef CONFIG_DEBUG_RT_MUTEXES
# define __DEBUG_RT_MUTEX_INITIALIZER(mutexname) \
	, .name = #mutexname, .file = __FILE__, .line = __LINE__
 extern void rt_mutex_debug_task_free(struct task_struct *tsk);
#else
# define __DEBUG_RT_MUTEX_INITIALIZER(mutexname)
# define rt_mutex_debug_task_free(t)			do { } while (0)
#endif

#define __RT_MUTEX_INITIALIZER_PLAIN(mutexname) \
	 .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(mutexname.wait_lock) \
	, .waiters = RB_ROOT \
	, .owner = NULL \
	__DEBUG_RT_MUTEX_INITIALIZER(mutexname)

#define __RT_MUTEX_INITIALIZER(mutexname) \
	{ __RT_MUTEX_INITIALIZER_PLAIN(mutexname) }

#define __RT_MUTEX_INITIALIZER_SAVE_STATE(mutexname) \
	{ __RT_MUTEX_INITIALIZER_PLAIN(mutexname)    \
	, .save_state = 1 }

#define DEFINE_RT_MUTEX(mutexname) \
	struct rt_mutex mutexname = __RT_MUTEX_INITIALIZER(mutexname)

/**
 * rt_mutex_is_locked - is the mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline int rt_mutex_is_locked(struct rt_mutex *lock)
{
	return lock->owner != NULL;
}

extern void __rt_mutex_init(struct rt_mutex *lock, const char *name);
extern void rt_mutex_destroy(struct rt_mutex *lock);

extern void rt_mutex_lock(struct rt_mutex *lock);
extern int rt_mutex_lock_interruptible(struct rt_mutex *lock);
extern int rt_mutex_lock_killable(struct rt_mutex *lock);
extern int rt_mutex_timed_lock(struct rt_mutex *lock,
			       struct hrtimer_sleeper *timeout);

extern int rt_mutex_trylock(struct rt_mutex *lock);

extern void rt_mutex_unlock(struct rt_mutex *lock);

#endif
