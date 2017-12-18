/*
 * Queued spinlock
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
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 * (C) Copyright 2013-2014 Red Hat, Inc.
 * (C) Copyright 2015 Intel Corp.
 * (C) Copyright 2015 Hewlett-Packard Enterprise Development LP
 *
 * Authors: Waiman Long <waiman.long@hpe.com>
 *          Peter Zijlstra <peterz@infradead.org>
 */

#ifndef _GEN_PV_LOCK_SLOWPATH

#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <asm/byteorder.h>
#include <asm/qspinlock.h>

/*
 * The basic principle of a queue-based spinlock can best be understood
 * by studying a classic queue-based spinlock implementation called the
 * MCS lock. The paper below provides a good description for this kind
 * of lock.
 *
 * http://www.cise.ufl.edu/tr/DOC/REP-1992-71.pdf
 *
 * This queued spinlock implementation is based on the MCS lock, however to make
 * it fit the 4 bytes we assume spinlock_t to be, and preserve its existing
 * API, we must modify it somehow.
 *
 * In particular; where the traditional MCS lock consists of a tail pointer
 * (8 bytes) and needs the next pointer (another 8 bytes) of its own node to
 * unlock the next pending (next->locked), we compress both these: {tail,
 * next->locked} into a single u32 value.
 *
 * Since a spinlock disables recursion of its own context and there is a limit
 * to the contexts that can nest; namely: task, softirq, hardirq, nmi. As there
 * are at most 4 nesting levels, it can be encoded by a 2-bit number. Now
 * we can encode the tail by combining the 2-bit nesting level with the cpu
 * number. With one byte for the lock value and 3 bytes for the tail, only a
 * 32-bit word is now needed. Even though we only need 1 bit for the lock,
 * we extend it to a full byte to achieve better performance for architectures
 * that support atomic byte write.
 *
 * We also change the first spinner to spin on the lock bit instead of its
 * node; whereby avoiding the need to carry a node from lock to unlock, and
 * preserving existing lock API. This also makes the unlock code simpler and
 * faster.
 *
 * N.B. The current implementation only supports architectures that allow
 *      atomic operations on smaller 8-bit and 16-bit data types.
 *
 */

#include "mcs_spinlock.h"

#ifdef CONFIG_PARAVIRT_SPINLOCKS
#define MAX_NODES	8
#else
#define MAX_NODES	4
#endif

/*
 * Per-CPU queue node structures; we can never have more than 4 nested
 * contexts: task, softirq, hardirq, nmi.
 *
 * Exactly fits one 64-byte cacheline on a 64-bit architecture.
 *
 * PV doubles the storage and uses the second cacheline for PV state.
 */
static DEFINE_PER_CPU_ALIGNED(struct mcs_spinlock, mcs_nodes[MAX_NODES]);

/*
 * We must be able to distinguish between no-tail and the tail at 0:0,
 * therefore increment the cpu number by one.
 */

static inline __pure u32 encode_tail(int cpu, int idx)
{
	u32 tail;

#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(idx > 3);
#endif
	tail  = (cpu + 1) << _Q_TAIL_CPU_OFFSET;
	tail |= idx << _Q_TAIL_IDX_OFFSET; /* assume < 4 */

	return tail;
}

static inline __pure struct mcs_spinlock *decode_tail(u32 tail)
{
	int cpu = (tail >> _Q_TAIL_CPU_OFFSET) - 1;
	int idx = (tail &  _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return per_cpu_ptr(&mcs_nodes[idx], cpu);
}

#define _Q_LOCKED_PENDING_MASK (_Q_LOCKED_MASK | _Q_PENDING_MASK)

/*
 * By using the whole 2nd least significant byte for the pending bit, we
 * can allow better optimization of the lock acquisition for the pending
 * bit holder.
 *
 * This internal structure is also used by the set_locked function which
 * is not restricted to _Q_PENDING_BITS == 8.
 */
struct __qspinlock {
	union {
		atomic_t val;
#ifdef __LITTLE_ENDIAN
		struct {
			u8	locked;
			u8	pending;
		};
		struct {
			u16	locked_pending;
			u16	tail;
		};
#else
		struct {
			u16	tail;
			u16	locked_pending;
		};
		struct {
			u8	reserved[2];
			u8	pending;
			u8	locked;
		};
#endif
	};
};

#if _Q_PENDING_BITS == 8
/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 *
 * Lock stealing is not allowed if this function is used.
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->locked_pending, _Q_LOCKED_VAL);
}

/*
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	struct __qspinlock *l = (void *)lock;

	/*
	 * Use release semantics to make sure that the MCS node is properly
	 * initialized before changing the tail code.
	 */
	return (u32)xchg_release(&l->tail,
				 tail >> _Q_TAIL_OFFSET) << _Q_TAIL_OFFSET;
}

#else /* _Q_PENDING_BITS == 8 */

/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	atomic_add(-_Q_PENDING_VAL + _Q_LOCKED_VAL, &lock->val);
}

/**
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	u32 old, new, val = atomic_read(&lock->val);

	for (;;) {
		new = (val & _Q_LOCKED_PENDING_MASK) | tail;
		/*
		 * Use release semantics to make sure that the MCS node is
		 * properly initialized before changing the tail code.
		 */
		old = atomic_cmpxchg_release(&lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}
	return old;
}
#endif /* _Q_PENDING_BITS == 8 */

/**
 * set_locked - Set the lock bit and own the lock
 * @lock: Pointer to queued spinlock structure
 *
 * *,*,0 -> *,0,1
 */
static __always_inline void set_locked(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->locked, _Q_LOCKED_VAL);
}


/*
 * Generate the native code for queued_spin_unlock_slowpath(); provide NOPs for
 * all the PV callbacks.
 */

static __always_inline void __pv_init_node(struct mcs_spinlock *node) { }
static __always_inline void __pv_wait_node(struct mcs_spinlock *node,
					   struct mcs_spinlock *prev) { }
static __always_inline void __pv_kick_node(struct qspinlock *lock,
					   struct mcs_spinlock *node) { }
static __always_inline u32  __pv_wait_head_or_lock(struct qspinlock *lock,
						   struct mcs_spinlock *node)
						   { return 0; }

#define pv_enabled()		false

#define pv_init_node		__pv_init_node
#define pv_wait_node		__pv_wait_node
#define pv_kick_node		__pv_kick_node
#define pv_wait_head_or_lock	__pv_wait_head_or_lock

#ifdef CONFIG_PARAVIRT_SPINLOCKS
#define queued_spin_lock_slowpath	native_queued_spin_lock_slowpath
#endif

/*
 * Various notes on spin_is_locked() and spin_unlock_wait(), which are
 * 'interesting' functions:
 *
 * PROBLEM: some architectures have an interesting issue with atomic ACQUIRE
 * operations in that the ACQUIRE applies to the LOAD _not_ the STORE (ARM64,
 * PPC). Also qspinlock has a similar issue per construction, the setting of
 * the locked byte can be unordered acquiring the lock proper.
 *
 * This gets to be 'interesting' in the following cases, where the /should/s
 * end up false because of this issue.
 *
 *
 * CASE 1:
 *
 * So the spin_is_locked() correctness issue comes from something like:
 *
 *   CPU0				CPU1
 *
 *   global_lock();			local_lock(i)
 *     spin_lock(&G)			  spin_lock(&L[i])
 *     for (i)				  if (!spin_is_locked(&G)) {
 *       spin_unlock_wait(&L[i]);	    smp_acquire__after_ctrl_dep();
 *					    return;
 *					  }
 *					  // deal with fail
 *
 * Where it is important CPU1 sees G locked or CPU0 sees L[i] locked such
 * that there is exclusion between the two critical sections.
 *
 * The load from spin_is_locked(&G) /should/ be constrained by the ACQUIRE from
 * spin_lock(&L[i]), and similarly the load(s) from spin_unlock_wait(&L[i])
 * /should/ be constrained by the ACQUIRE from spin_lock(&G).
 *
 * Similarly, later stuff is constrained by the ACQUIRE from CTRL+RMB.
 *
 *
 * CASE 2:
 *
 * For spin_unlock_wait() there is a second correctness issue, namely:
 *
 *   CPU0				CPU1
 *
 *   flag = set;
 *   smp_mb();				spin_lock(&l)
 *   spin_unlock_wait(&l);		if (!flag)
 *					  // add to lockless list
 *					spin_unlock(&l);
 *   // iterate lockless list
 *
 * Which wants to ensure that CPU1 will stop adding bits to the list and CPU0
 * will observe the last entry on the list (if spin_unlock_wait() had ACQUIRE
 * semantics etc..)
 *
 * Where flag /should/ be ordered against the locked store of l.
 */

/*
 * queued_spin_lock_slowpath() can (load-)ACQUIRE the lock before
 * issuing an _unordered_ store to set _Q_LOCKED_VAL.
 *
 * This means that the store can be delayed, but no later than the
 * store-release from the unlock. This means that simply observing
 * _Q_LOCKED_VAL is not sufficient to determine if the lock is acquired.
 *
 * There are two paths that can issue the unordered store:
 *
 *  (1) clear_pending_set_locked():	*,1,0 -> *,0,1
 *
 *  (2) set_locked():			t,0,0 -> t,0,1 ; t != 0
 *      atomic_cmpxchg_relaxed():	t,0,0 -> 0,0,1
 *
 * However, in both cases we have other !0 state we've set before to queue
 * ourseves:
 *
 * For (1) we have the atomic_cmpxchg_acquire() that set _Q_PENDING_VAL, our
 * load is constrained by that ACQUIRE to not pass before that, and thus must
 * observe the store.
 *
 * For (2) we have a more intersting scenario. We enqueue ourselves using
 * xchg_tail(), which ends up being a RELEASE. This in itself is not
 * sufficient, however that is followed by an smp_cond_acquire() on the same
 * word, giving a RELEASE->ACQUIRE ordering. This again constrains our load and
 * guarantees we must observe that store.
 *
 * Therefore both cases have other !0 state that is observable before the
 * unordered locked byte store comes through. This means we can use that to
 * wait for the lock store, and then wait for an unlock.
 */
#ifndef queued_spin_unlock_wait
void queued_spin_unlock_wait(struct qspinlock *lock)
{
	u32 val;

	for (;;) {
		val = atomic_read(&lock->val);

		if (!val) /* not locked, we're done */
			goto done;

		if (val & _Q_LOCKED_MASK) /* locked, go wait for unlock */
			break;

		/* not locked, but pending, wait until we observe the lock */
		cpu_relax();
	}

	/* any unlock is good */
	while (atomic_read(&lock->val) & _Q_LOCKED_MASK)
		cpu_relax();

done:
	smp_acquire__after_ctrl_dep();
}
EXPORT_SYMBOL(queued_spin_unlock_wait);
#endif

#endif /* _GEN_PV_LOCK_SLOWPATH */

/**
 * queued_spin_lock_slowpath - acquire the queued spinlock
 * @lock: Pointer to queued spinlock structure
 * @val: Current value of the queued spinlock 32-bit word
 *
 * (queue tail, pending bit, lock value)
 *
 *              fast     :    slow                                  :    unlock
 *                       :                                          :
 * uncontended  (0,0,0) -:--> (0,0,1) ------------------------------:--> (*,*,0)
 *                       :       | ^--------.------.             /  :
 *                       :       v           \      \            |  :
 * pending               :    (0,1,1) +--> (0,1,0)   \           |  :
 *                       :       | ^--'              |           |  :
 *                       :       v                   |           |  :
 * uncontended           :    (n,x,y) +--> (n,0,0) --'           |  :
 *   queue               :       | ^--'                          |  :
 *                       :       v                               |  :
 * contended             :    (*,x,y) +--> (*,0,0) ---> (*,0,1) -'  :
 *   queue               :         ^--'                             :
 */
void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 new, old, tail;
	int idx;

	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));

	if (pv_enabled())
		goto queue;

	if (virt_spin_lock(lock))
		return;

	/*
	 * wait for in-progress pending->locked hand-overs
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL) {
		while ((val = atomic_read(&lock->val)) == _Q_PENDING_VAL)
			cpu_relax();
	}

	/*
	 * trylock || pending
	 *
	 * 0,0,0 -> 0,0,1 ; trylock
	 * 0,0,1 -> 0,1,1 ; pending
	 */
	for (;;) {
		/*
		 * If we observe any contention; queue.
		 */
		if (val & ~_Q_LOCKED_MASK)
			goto queue;

		new = _Q_LOCKED_VAL;
		if (val == new)
			new |= _Q_PENDING_VAL;

		/*
		 * Acquire semantic is required here as the function may
		 * return immediately if the lock was free.
		 */
		old = atomic_cmpxchg_acquire(&lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}

	/*
	 * we won the trylock
	 */
	if (new == _Q_LOCKED_VAL)
		return;

	/*
	 * we're pending, wait for the owner to go away.
	 *
	 * *,1,1 -> *,1,0
	 *
	 * this wait loop must be a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because not all clear_pending_set_locked()
	 * implementations imply full barriers.
	 */
	smp_cond_load_acquire(&lock->val.counter, !(VAL & _Q_LOCKED_MASK));

	/*
	 * take ownership and clear the pending bit.
	 *
	 * *,1,0 -> *,0,1
	 */
	clear_pending_set_locked(lock);
	return;

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	node = this_cpu_ptr(&mcs_nodes[0]);
	idx = node->count++;
	tail = encode_tail(smp_processor_id(), idx);

	node += idx;
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 *
	 * RELEASE, such that the stores to @node must be complete.
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);
		/*
		 * The above xchg_tail() is also a load of @lock which generates,
		 * through decode_tail(), a pointer.
		 *
		 * The address dependency matches the RELEASE of xchg_tail()
		 * such that the access to @prev must happen after.
		 */
		smp_read_barrier_depends();

		WRITE_ONCE(prev->next, node);

		pv_wait_node(node, prev);
		arch_mcs_spin_lock_contended(&node->locked);

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * The PV pv_wait_head_or_lock function, if active, will acquire
	 * the lock and return a non-zero value. So we have to skip the
	 * smp_cond_load_acquire() call. As the next PV queue head hasn't been
	 * designated yet, there is no way for the locked value to become
	 * _Q_SLOW_VAL. So both the set_locked() and the
	 * atomic_cmpxchg_relaxed() calls will be safe.
	 *
	 * If PV isn't active, 0 will be returned instead.
	 *
	 */
	if ((val = pv_wait_head_or_lock(lock, node)))
		goto locked;

	val = smp_cond_load_acquire(&lock->val.counter, !(VAL & _Q_LOCKED_PENDING_MASK));

locked:
	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,0,0 -> *,0,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail),
	 * clear the tail code and grab the lock. Otherwise, we only need
	 * to grab the lock.
	 */
	for (;;) {
		/* In the PV case we might already have _Q_LOCKED_VAL set */
		if ((val & _Q_TAIL_MASK) != tail) {
			set_locked(lock);
			break;
		}
		/*
		 * The smp_cond_load_acquire() call above has provided the
		 * necessary acquire semantics required for locking. At most
		 * two iterations of this loop may be ran.
		 */
		old = atomic_cmpxchg_relaxed(&lock->val, val, _Q_LOCKED_VAL);
		if (old == val)
			goto release;	/* No contention */

		val = old;
	}

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
	if (!next) {
		while (!(next = READ_ONCE(node->next)))
			cpu_relax();
	}

	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);

release:
	/*
	 * release the node
	 */
	__this_cpu_dec(mcs_nodes[0].count);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath);

/*
 * Generate the paravirt code for queued_spin_unlock_slowpath().
 */
#if !defined(_GEN_PV_LOCK_SLOWPATH) && defined(CONFIG_PARAVIRT_SPINLOCKS)
#define _GEN_PV_LOCK_SLOWPATH

#undef  pv_enabled
#define pv_enabled()	true

#undef pv_init_node
#undef pv_wait_node
#undef pv_kick_node
#undef pv_wait_head_or_lock

#undef  queued_spin_lock_slowpath
#define queued_spin_lock_slowpath	__pv_queued_spin_lock_slowpath

#include "qspinlock_paravirt.h"
#include "qspinlock.c"

#endif
