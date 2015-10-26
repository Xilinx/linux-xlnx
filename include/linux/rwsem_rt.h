#ifndef _LINUX_RWSEM_RT_H
#define _LINUX_RWSEM_RT_H

#ifndef _LINUX_RWSEM_H
#error "Include rwsem.h"
#endif

/*
 * RW-semaphores are a spinlock plus a reader-depth count.
 *
 * Note that the semantics are different from the usual
 * Linux rw-sems, in PREEMPT_RT mode we do not allow
 * multiple readers to hold the lock at once, we only allow
 * a read-lock owner to read-lock recursively. This is
 * better for latency, makes the implementation inherently
 * fair and makes it simpler as well.
 */

#include <linux/rtmutex.h>

struct rw_semaphore {
	struct rt_mutex		lock;
	int			read_depth;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#define __RWSEM_INITIALIZER(name) \
	{ .lock = __RT_MUTEX_INITIALIZER(name.lock), \
	  RW_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(lockname) \
	struct rw_semaphore lockname = __RWSEM_INITIALIZER(lockname)

extern void  __rt_rwsem_init(struct rw_semaphore *rwsem, const char *name,
				     struct lock_class_key *key);

#define __rt_init_rwsem(sem, name, key)			\
	do {						\
		rt_mutex_init(&(sem)->lock);		\
		__rt_rwsem_init((sem), (name), (key));\
	} while (0)

#define __init_rwsem(sem, name, key) __rt_init_rwsem(sem, name, key)

# define rt_init_rwsem(sem)				\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_init_rwsem((sem), #sem, &__key);		\
} while (0)

extern void  rt_down_write(struct rw_semaphore *rwsem);
extern void rt_down_read_nested(struct rw_semaphore *rwsem, int subclass);
extern void rt_down_write_nested(struct rw_semaphore *rwsem, int subclass);
extern void rt_down_write_nested_lock(struct rw_semaphore *rwsem,
		struct lockdep_map *nest);
extern void  rt_down_read(struct rw_semaphore *rwsem);
extern int  rt_down_write_trylock(struct rw_semaphore *rwsem);
extern int  rt_down_read_trylock(struct rw_semaphore *rwsem);
extern void  __rt_up_read(struct rw_semaphore *rwsem);
extern void  rt_up_read(struct rw_semaphore *rwsem);
extern void  rt_up_write(struct rw_semaphore *rwsem);
extern void  rt_downgrade_write(struct rw_semaphore *rwsem);

#define init_rwsem(sem)		rt_init_rwsem(sem)
#define rwsem_is_locked(s)	rt_mutex_is_locked(&(s)->lock)

static inline int rwsem_is_contended(struct rw_semaphore *sem)
{
	/* rt_mutex_has_waiters() */
	return !RB_EMPTY_ROOT(&sem->lock.waiters);
}

static inline void down_read(struct rw_semaphore *sem)
{
	rt_down_read(sem);
}

static inline int down_read_trylock(struct rw_semaphore *sem)
{
	return rt_down_read_trylock(sem);
}

static inline void down_write(struct rw_semaphore *sem)
{
	rt_down_write(sem);
}

static inline int down_write_trylock(struct rw_semaphore *sem)
{
	return rt_down_write_trylock(sem);
}

static inline void __up_read(struct rw_semaphore *sem)
{
	__rt_up_read(sem);
}

static inline void up_read(struct rw_semaphore *sem)
{
	rt_up_read(sem);
}

static inline void up_write(struct rw_semaphore *sem)
{
	rt_up_write(sem);
}

static inline void downgrade_write(struct rw_semaphore *sem)
{
	rt_downgrade_write(sem);
}

static inline void down_read_nested(struct rw_semaphore *sem, int subclass)
{
	return rt_down_read_nested(sem, subclass);
}

static inline void down_write_nested(struct rw_semaphore *sem, int subclass)
{
	rt_down_write_nested(sem, subclass);
}
#ifdef CONFIG_DEBUG_LOCK_ALLOC
static inline void down_write_nest_lock(struct rw_semaphore *sem,
		struct rw_semaphore *nest_lock)
{
	rt_down_write_nested_lock(sem, &nest_lock->dep_map);
}

#else

static inline void down_write_nest_lock(struct rw_semaphore *sem,
		struct rw_semaphore *nest_lock)
{
	rt_down_write_nested_lock(sem, NULL);
}
#endif
#endif
