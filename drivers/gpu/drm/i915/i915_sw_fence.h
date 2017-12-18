/*
 * i915_sw_fence.h - library routines for N:M synchronisation points
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * This file is released under the GPLv2.
 *
 */

#ifndef _I915_SW_FENCE_H_
#define _I915_SW_FENCE_H_

#include <linux/gfp.h>
#include <linux/kref.h>
#include <linux/notifier.h> /* for NOTIFY_DONE */
#include <linux/wait.h>

struct completion;
struct fence;
struct fence_ops;
struct reservation_object;

struct i915_sw_fence {
	wait_queue_head_t wait;
	unsigned long flags;
	struct kref kref;
	atomic_t pending;
};

#define I915_SW_FENCE_CHECKED_BIT	0 /* used internally for DAG checking */
#define I915_SW_FENCE_PRIVATE_BIT	1 /* available for use by owner */
#define I915_SW_FENCE_MASK		(~3)

enum i915_sw_fence_notify {
	FENCE_COMPLETE,
	FENCE_FREE
};

typedef int (*i915_sw_fence_notify_t)(struct i915_sw_fence *,
				      enum i915_sw_fence_notify state);
#define __i915_sw_fence_call __aligned(4)

void i915_sw_fence_init(struct i915_sw_fence *fence, i915_sw_fence_notify_t fn);
void i915_sw_fence_commit(struct i915_sw_fence *fence);

int i915_sw_fence_await_sw_fence(struct i915_sw_fence *fence,
				 struct i915_sw_fence *after,
				 wait_queue_t *wq);
int i915_sw_fence_await_dma_fence(struct i915_sw_fence *fence,
				  struct fence *dma,
				  unsigned long timeout,
				  gfp_t gfp);
int i915_sw_fence_await_reservation(struct i915_sw_fence *fence,
				    struct reservation_object *resv,
				    const struct fence_ops *exclude,
				    bool write,
				    unsigned long timeout,
				    gfp_t gfp);

static inline bool i915_sw_fence_done(const struct i915_sw_fence *fence)
{
	return atomic_read(&fence->pending) < 0;
}

#endif /* _I915_SW_FENCE_H_ */
