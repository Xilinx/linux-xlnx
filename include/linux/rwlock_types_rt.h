#ifndef __LINUX_RWLOCK_TYPES_RT_H
#define __LINUX_RWLOCK_TYPES_RT_H

#ifndef __LINUX_SPINLOCK_TYPES_H
#error "Do not include directly. Include spinlock_types.h instead"
#endif

/*
 * rwlocks - rtmutex which allows single reader recursion
 */
typedef struct {
	struct rt_mutex		lock;
	int			read_depth;
	unsigned int		break_lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} rwlock_t;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define RW_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
#else
# define RW_DEP_MAP_INIT(lockname)
#endif

#define __RW_LOCK_UNLOCKED(name) \
	{ .lock = __RT_MUTEX_INITIALIZER_SAVE_STATE(name.lock),	\
	  RW_DEP_MAP_INIT(name) }

#define DEFINE_RWLOCK(name) \
	rwlock_t name __cacheline_aligned_in_smp = __RW_LOCK_UNLOCKED(name)

#endif
