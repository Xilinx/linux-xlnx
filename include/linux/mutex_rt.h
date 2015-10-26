#ifndef __LINUX_MUTEX_RT_H
#define __LINUX_MUTEX_RT_H

#ifndef __LINUX_MUTEX_H
#error "Please include mutex.h"
#endif

#include <linux/rtmutex.h>

/* FIXME: Just for __lockfunc */
#include <linux/spinlock.h>

struct mutex {
	struct rt_mutex		lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#define __MUTEX_INITIALIZER(mutexname)					\
	{								\
		.lock = __RT_MUTEX_INITIALIZER(mutexname.lock)		\
		__DEP_MAP_MUTEX_INITIALIZER(mutexname)			\
	}

#define DEFINE_MUTEX(mutexname)						\
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

extern void __mutex_do_init(struct mutex *lock, const char *name, struct lock_class_key *key);
extern void __lockfunc _mutex_lock(struct mutex *lock);
extern int __lockfunc _mutex_lock_interruptible(struct mutex *lock);
extern int __lockfunc _mutex_lock_killable(struct mutex *lock);
extern void __lockfunc _mutex_lock_nested(struct mutex *lock, int subclass);
extern void __lockfunc _mutex_lock_nest_lock(struct mutex *lock, struct lockdep_map *nest_lock);
extern int __lockfunc _mutex_lock_interruptible_nested(struct mutex *lock, int subclass);
extern int __lockfunc _mutex_lock_killable_nested(struct mutex *lock, int subclass);
extern int __lockfunc _mutex_trylock(struct mutex *lock);
extern void __lockfunc _mutex_unlock(struct mutex *lock);

#define mutex_is_locked(l)		rt_mutex_is_locked(&(l)->lock)
#define mutex_lock(l)			_mutex_lock(l)
#define mutex_lock_interruptible(l)	_mutex_lock_interruptible(l)
#define mutex_lock_killable(l)		_mutex_lock_killable(l)
#define mutex_trylock(l)		_mutex_trylock(l)
#define mutex_unlock(l)			_mutex_unlock(l)
#define mutex_destroy(l)		rt_mutex_destroy(&(l)->lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define mutex_lock_nested(l, s)	_mutex_lock_nested(l, s)
# define mutex_lock_interruptible_nested(l, s) \
					_mutex_lock_interruptible_nested(l, s)
# define mutex_lock_killable_nested(l, s) \
					_mutex_lock_killable_nested(l, s)

# define mutex_lock_nest_lock(lock, nest_lock)				\
do {									\
	typecheck(struct lockdep_map *, &(nest_lock)->dep_map);		\
	_mutex_lock_nest_lock(lock, &(nest_lock)->dep_map);		\
} while (0)

#else
# define mutex_lock_nested(l, s)	_mutex_lock(l)
# define mutex_lock_interruptible_nested(l, s) \
					_mutex_lock_interruptible(l)
# define mutex_lock_killable_nested(l, s) \
					_mutex_lock_killable(l)
# define mutex_lock_nest_lock(lock, nest_lock) mutex_lock(lock)
#endif

# define mutex_init(mutex)				\
do {							\
	static struct lock_class_key __key;		\
							\
	rt_mutex_init(&(mutex)->lock);			\
	__mutex_do_init((mutex), #mutex, &__key);	\
} while (0)

# define __mutex_init(mutex, name, key)			\
do {							\
	rt_mutex_init(&(mutex)->lock);			\
	__mutex_do_init((mutex), name, key);		\
} while (0)

#endif
