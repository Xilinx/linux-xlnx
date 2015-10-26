/* See include/linux/lglock.h for description */
#include <linux/module.h>
#include <linux/lglock.h>
#include <linux/cpu.h>
#include <linux/string.h>

#ifndef CONFIG_PREEMPT_RT_FULL
# define lg_lock_ptr		arch_spinlock_t
# define lg_do_lock(l)		arch_spin_lock(l)
# define lg_do_unlock(l)	arch_spin_unlock(l)
#else
# define lg_lock_ptr		struct rt_mutex
# define lg_do_lock(l)		__rt_spin_lock(l)
# define lg_do_unlock(l)	__rt_spin_unlock(l)
#endif
/*
 * Note there is no uninit, so lglocks cannot be defined in
 * modules (but it's fine to use them from there)
 * Could be added though, just undo lg_lock_init
 */

void lg_lock_init(struct lglock *lg, char *name)
{
#ifdef CONFIG_PREEMPT_RT_FULL
	int i;

	for_each_possible_cpu(i) {
		struct rt_mutex *lock = per_cpu_ptr(lg->lock, i);

		rt_mutex_init(lock);
	}
#endif
	LOCKDEP_INIT_MAP(&lg->lock_dep_map, name, &lg->lock_key, 0);
}
EXPORT_SYMBOL(lg_lock_init);

void lg_local_lock(struct lglock *lg)
{
	lg_lock_ptr *lock;

	migrate_disable();
	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	lock = this_cpu_ptr(lg->lock);
	lg_do_lock(lock);
}
EXPORT_SYMBOL(lg_local_lock);

void lg_local_unlock(struct lglock *lg)
{
	lg_lock_ptr *lock;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	lock = this_cpu_ptr(lg->lock);
	lg_do_unlock(lock);
	migrate_enable();
}
EXPORT_SYMBOL(lg_local_unlock);

void lg_local_lock_cpu(struct lglock *lg, int cpu)
{
	lg_lock_ptr *lock;

	preempt_disable_nort();
	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	lock = per_cpu_ptr(lg->lock, cpu);
	lg_do_lock(lock);
}
EXPORT_SYMBOL(lg_local_lock_cpu);

void lg_local_unlock_cpu(struct lglock *lg, int cpu)
{
	lg_lock_ptr *lock;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	lock = per_cpu_ptr(lg->lock, cpu);
	lg_do_unlock(lock);
	preempt_enable_nort();
}
EXPORT_SYMBOL(lg_local_unlock_cpu);

void lg_global_lock(struct lglock *lg)
{
	int i;

	preempt_disable_nort();
	lock_acquire_exclusive(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	for_each_possible_cpu(i) {
		lg_lock_ptr *lock;
		lock = per_cpu_ptr(lg->lock, i);
		lg_do_lock(lock);
	}
}
EXPORT_SYMBOL(lg_global_lock);

void lg_global_unlock(struct lglock *lg)
{
	int i;

	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
	for_each_possible_cpu(i) {
		lg_lock_ptr *lock;
		lock = per_cpu_ptr(lg->lock, i);
		lg_do_unlock(lock);
	}
	preempt_enable_nort();
}
EXPORT_SYMBOL(lg_global_unlock);

#ifdef CONFIG_PREEMPT_RT_FULL
/*
 * HACK: If you use this, you get to keep the pieces.
 * Used in queue_stop_cpus_work() when stop machinery
 * is called from inactive CPU, so we can't schedule.
 */
# define lg_do_trylock_relax(l)			\
	do {					\
		while (!__rt_spin_trylock(l))	\
			cpu_relax();		\
	} while (0)

void lg_global_trylock_relax(struct lglock *lg)
{
	int i;

	lock_acquire_exclusive(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
	for_each_possible_cpu(i) {
		lg_lock_ptr *lock;
		lock = per_cpu_ptr(lg->lock, i);
		lg_do_trylock_relax(lock);
	}
}
#endif
