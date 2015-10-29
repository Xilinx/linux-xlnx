#ifndef __LINUX_RWLOCK_RT_H
#define __LINUX_RWLOCK_RT_H

#ifndef __LINUX_SPINLOCK_H
#error Do not include directly. Use spinlock.h
#endif

#define rwlock_init(rwl)				\
do {							\
	static struct lock_class_key __key;		\
							\
	rt_mutex_init(&(rwl)->lock);			\
	__rt_rwlock_init(rwl, #rwl, &__key);		\
} while (0)

extern void __lockfunc rt_write_lock(rwlock_t *rwlock);
extern void __lockfunc rt_read_lock(rwlock_t *rwlock);
extern int __lockfunc rt_write_trylock(rwlock_t *rwlock);
extern int __lockfunc rt_write_trylock_irqsave(rwlock_t *trylock, unsigned long *flags);
extern int __lockfunc rt_read_trylock(rwlock_t *rwlock);
extern void __lockfunc rt_write_unlock(rwlock_t *rwlock);
extern void __lockfunc rt_read_unlock(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock);
extern void __rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key);

#define read_trylock(lock)	__cond_lock(lock, rt_read_trylock(lock))
#define write_trylock(lock)	__cond_lock(lock, rt_write_trylock(lock))

#define write_trylock_irqsave(lock, flags)	\
	__cond_lock(lock, rt_write_trylock_irqsave(lock, &flags))

#define read_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = rt_read_lock_irqsave(lock);	\
	} while (0)

#define write_lock_irqsave(lock, flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = rt_write_lock_irqsave(lock);	\
	} while (0)

#define read_lock(lock)		rt_read_lock(lock)

#define read_lock_bh(lock)				\
	do {						\
		local_bh_disable();			\
		rt_read_lock(lock);			\
	} while (0)

#define read_lock_irq(lock)	read_lock(lock)

#define write_lock(lock)	rt_write_lock(lock)

#define write_lock_bh(lock)				\
	do {						\
		local_bh_disable();			\
		rt_write_lock(lock);			\
	} while (0)

#define write_lock_irq(lock)	write_lock(lock)

#define read_unlock(lock)	rt_read_unlock(lock)

#define read_unlock_bh(lock)				\
	do {						\
		rt_read_unlock(lock);			\
		local_bh_enable();			\
	} while (0)

#define read_unlock_irq(lock)	read_unlock(lock)

#define write_unlock(lock)	rt_write_unlock(lock)

#define write_unlock_bh(lock)				\
	do {						\
		rt_write_unlock(lock);			\
		local_bh_enable();			\
	} while (0)

#define write_unlock_irq(lock)	write_unlock(lock)

#define read_unlock_irqrestore(lock, flags)		\
	do {						\
		typecheck(unsigned long, flags);	\
		(void) flags;				\
		rt_read_unlock(lock);			\
	} while (0)

#define write_unlock_irqrestore(lock, flags) \
	do {						\
		typecheck(unsigned long, flags);	\
		(void) flags;				\
		rt_write_unlock(lock);			\
	} while (0)

#endif
