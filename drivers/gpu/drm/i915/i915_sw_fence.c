/*
 * (C) Copyright 2016 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/slab.h>
#include <linux/fence.h>
#include <linux/reservation.h>

#include "i915_sw_fence.h"

static DEFINE_SPINLOCK(i915_sw_fence_lock);

static int __i915_sw_fence_notify(struct i915_sw_fence *fence,
				  enum i915_sw_fence_notify state)
{
	i915_sw_fence_notify_t fn;

	fn = (i915_sw_fence_notify_t)(fence->flags & I915_SW_FENCE_MASK);
	return fn(fence, state);
}

static void i915_sw_fence_free(struct kref *kref)
{
	struct i915_sw_fence *fence = container_of(kref, typeof(*fence), kref);

	WARN_ON(atomic_read(&fence->pending) > 0);

	if (fence->flags & I915_SW_FENCE_MASK)
		__i915_sw_fence_notify(fence, FENCE_FREE);
	else
		kfree(fence);
}

static void i915_sw_fence_put(struct i915_sw_fence *fence)
{
	kref_put(&fence->kref, i915_sw_fence_free);
}

static struct i915_sw_fence *i915_sw_fence_get(struct i915_sw_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

static void __i915_sw_fence_wake_up_all(struct i915_sw_fence *fence,
					struct list_head *continuation)
{
	wait_queue_head_t *x = &fence->wait;
	wait_queue_t *pos, *next;
	unsigned long flags;

	atomic_set_release(&fence->pending, -1); /* 0 -> -1 [done] */

	/*
	 * To prevent unbounded recursion as we traverse the graph of
	 * i915_sw_fences, we move the task_list from this, the next ready
	 * fence, to the tail of the original fence's task_list
	 * (and so added to the list to be woken).
	 */

	spin_lock_irqsave_nested(&x->lock, flags, 1 + !!continuation);
	if (continuation) {
		list_for_each_entry_safe(pos, next, &x->task_list, task_list) {
			if (pos->func == autoremove_wake_function)
				pos->func(pos, TASK_NORMAL, 0, continuation);
			else
				list_move_tail(&pos->task_list, continuation);
		}
	} else {
		LIST_HEAD(extra);

		do {
			list_for_each_entry_safe(pos, next,
						 &x->task_list, task_list)
				pos->func(pos, TASK_NORMAL, 0, &extra);

			if (list_empty(&extra))
				break;

			list_splice_tail_init(&extra, &x->task_list);
		} while (1);
	}
	spin_unlock_irqrestore(&x->lock, flags);
}

static void __i915_sw_fence_complete(struct i915_sw_fence *fence,
				     struct list_head *continuation)
{
	if (!atomic_dec_and_test(&fence->pending))
		return;

	if (fence->flags & I915_SW_FENCE_MASK &&
	    __i915_sw_fence_notify(fence, FENCE_COMPLETE) != NOTIFY_DONE)
		return;

	__i915_sw_fence_wake_up_all(fence, continuation);
}

static void i915_sw_fence_complete(struct i915_sw_fence *fence)
{
	if (WARN_ON(i915_sw_fence_done(fence)))
		return;

	__i915_sw_fence_complete(fence, NULL);
}

static void i915_sw_fence_await(struct i915_sw_fence *fence)
{
	WARN_ON(atomic_inc_return(&fence->pending) <= 1);
}

void i915_sw_fence_init(struct i915_sw_fence *fence, i915_sw_fence_notify_t fn)
{
	BUG_ON((unsigned long)fn & ~I915_SW_FENCE_MASK);

	init_waitqueue_head(&fence->wait);
	kref_init(&fence->kref);
	atomic_set(&fence->pending, 1);
	fence->flags = (unsigned long)fn;
}

void i915_sw_fence_commit(struct i915_sw_fence *fence)
{
	i915_sw_fence_complete(fence);
	i915_sw_fence_put(fence);
}

static int i915_sw_fence_wake(wait_queue_t *wq, unsigned mode, int flags, void *key)
{
	list_del(&wq->task_list);
	__i915_sw_fence_complete(wq->private, key);
	i915_sw_fence_put(wq->private);
	return 0;
}

static bool __i915_sw_fence_check_if_after(struct i915_sw_fence *fence,
				    const struct i915_sw_fence * const signaler)
{
	wait_queue_t *wq;

	if (__test_and_set_bit(I915_SW_FENCE_CHECKED_BIT, &fence->flags))
		return false;

	if (fence == signaler)
		return true;

	list_for_each_entry(wq, &fence->wait.task_list, task_list) {
		if (wq->func != i915_sw_fence_wake)
			continue;

		if (__i915_sw_fence_check_if_after(wq->private, signaler))
			return true;
	}

	return false;
}

static void __i915_sw_fence_clear_checked_bit(struct i915_sw_fence *fence)
{
	wait_queue_t *wq;

	if (!__test_and_clear_bit(I915_SW_FENCE_CHECKED_BIT, &fence->flags))
		return;

	list_for_each_entry(wq, &fence->wait.task_list, task_list) {
		if (wq->func != i915_sw_fence_wake)
			continue;

		__i915_sw_fence_clear_checked_bit(wq->private);
	}
}

static bool i915_sw_fence_check_if_after(struct i915_sw_fence *fence,
				  const struct i915_sw_fence * const signaler)
{
	unsigned long flags;
	bool err;

	if (!IS_ENABLED(CONFIG_I915_SW_FENCE_CHECK_DAG))
		return false;

	spin_lock_irqsave(&i915_sw_fence_lock, flags);
	err = __i915_sw_fence_check_if_after(fence, signaler);
	__i915_sw_fence_clear_checked_bit(fence);
	spin_unlock_irqrestore(&i915_sw_fence_lock, flags);

	return err;
}

int i915_sw_fence_await_sw_fence(struct i915_sw_fence *fence,
				 struct i915_sw_fence *signaler,
				 wait_queue_t *wq)
{
	unsigned long flags;
	int pending;

	if (i915_sw_fence_done(signaler))
		return 0;

	/* The dependency graph must be acyclic. */
	if (unlikely(i915_sw_fence_check_if_after(fence, signaler)))
		return -EINVAL;

	INIT_LIST_HEAD(&wq->task_list);
	wq->flags = 0;
	wq->func = i915_sw_fence_wake;
	wq->private = i915_sw_fence_get(fence);

	i915_sw_fence_await(fence);

	spin_lock_irqsave(&signaler->wait.lock, flags);
	if (likely(!i915_sw_fence_done(signaler))) {
		__add_wait_queue_tail(&signaler->wait, wq);
		pending = 1;
	} else {
		i915_sw_fence_wake(wq, 0, 0, NULL);
		pending = 0;
	}
	spin_unlock_irqrestore(&signaler->wait.lock, flags);

	return pending;
}

struct dma_fence_cb {
	struct fence_cb base;
	struct i915_sw_fence *fence;
	struct fence *dma;
	struct timer_list timer;
};

static void timer_i915_sw_fence_wake(unsigned long data)
{
	struct dma_fence_cb *cb = (struct dma_fence_cb *)data;

	printk(KERN_WARNING "asynchronous wait on fence %s:%s:%x timed out\n",
	       cb->dma->ops->get_driver_name(cb->dma),
	       cb->dma->ops->get_timeline_name(cb->dma),
	       cb->dma->seqno);
	fence_put(cb->dma);
	cb->dma = NULL;

	i915_sw_fence_commit(cb->fence);
	cb->timer.function = NULL;
}

static void dma_i915_sw_fence_wake(struct fence *dma, struct fence_cb *data)
{
	struct dma_fence_cb *cb = container_of(data, typeof(*cb), base);

	del_timer_sync(&cb->timer);
	if (cb->timer.function)
		i915_sw_fence_commit(cb->fence);
	fence_put(cb->dma);

	kfree(cb);
}

int i915_sw_fence_await_dma_fence(struct i915_sw_fence *fence,
				  struct fence *dma,
				  unsigned long timeout,
				  gfp_t gfp)
{
	struct dma_fence_cb *cb;
	int ret;

	if (fence_is_signaled(dma))
		return 0;

	cb = kmalloc(sizeof(*cb), gfp);
	if (!cb) {
		if (!gfpflags_allow_blocking(gfp))
			return -ENOMEM;

		return fence_wait(dma, false);
	}

	cb->fence = i915_sw_fence_get(fence);
	i915_sw_fence_await(fence);

	cb->dma = NULL;
	__setup_timer(&cb->timer,
		      timer_i915_sw_fence_wake, (unsigned long)cb,
		      TIMER_IRQSAFE);
	if (timeout) {
		cb->dma = fence_get(dma);
		mod_timer(&cb->timer, round_jiffies_up(jiffies + timeout));
	}

	ret = fence_add_callback(dma, &cb->base, dma_i915_sw_fence_wake);
	if (ret == 0) {
		ret = 1;
	} else {
		dma_i915_sw_fence_wake(dma, &cb->base);
		if (ret == -ENOENT) /* fence already signaled */
			ret = 0;
	}

	return ret;
}

int i915_sw_fence_await_reservation(struct i915_sw_fence *fence,
				    struct reservation_object *resv,
				    const struct fence_ops *exclude,
				    bool write,
				    unsigned long timeout,
				    gfp_t gfp)
{
	struct fence *excl;
	int ret = 0, pending;

	if (write) {
		struct fence **shared;
		unsigned int count, i;

		ret = reservation_object_get_fences_rcu(resv,
							&excl, &count, &shared);
		if (ret)
			return ret;

		for (i = 0; i < count; i++) {
			if (shared[i]->ops == exclude)
				continue;

			pending = i915_sw_fence_await_dma_fence(fence,
								shared[i],
								timeout,
								gfp);
			if (pending < 0) {
				ret = pending;
				break;
			}

			ret |= pending;
		}

		for (i = 0; i < count; i++)
			fence_put(shared[i]);
		kfree(shared);
	} else {
		excl = reservation_object_get_excl_rcu(resv);
	}

	if (ret >= 0 && excl && excl->ops != exclude) {
		pending = i915_sw_fence_await_dma_fence(fence,
							excl,
							timeout,
							gfp);
		if (pending < 0)
			ret = pending;
		else
			ret |= pending;
	}

	fence_put(excl);

	return ret;
}
