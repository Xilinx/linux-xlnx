/*
 * Copyright © 2008-2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef I915_GEM_REQUEST_H
#define I915_GEM_REQUEST_H

#include <linux/fence.h>

#include "i915_gem.h"
#include "i915_sw_fence.h"

struct intel_wait {
	struct rb_node node;
	struct task_struct *tsk;
	u32 seqno;
};

struct intel_signal_node {
	struct rb_node node;
	struct intel_wait wait;
};

/**
 * Request queue structure.
 *
 * The request queue allows us to note sequence numbers that have been emitted
 * and may be associated with active buffers to be retired.
 *
 * By keeping this list, we can avoid having to do questionable sequence
 * number comparisons on buffer last_read|write_seqno. It also allows an
 * emission time to be associated with the request for tracking how far ahead
 * of the GPU the submission is.
 *
 * When modifying this structure be very aware that we perform a lockless
 * RCU lookup of it that may race against reallocation of the struct
 * from the slab freelist. We intentionally do not zero the structure on
 * allocation so that the lookup can use the dangling pointers (and is
 * cogniscent that those pointers may be wrong). Instead, everything that
 * needs to be initialised must be done so explicitly.
 *
 * The requests are reference counted.
 */
struct drm_i915_gem_request {
	struct fence fence;
	spinlock_t lock;

	/** On Which ring this request was generated */
	struct drm_i915_private *i915;

	/**
	 * Context and ring buffer related to this request
	 * Contexts are refcounted, so when this request is associated with a
	 * context, we must increment the context's refcount, to guarantee that
	 * it persists while any request is linked to it. Requests themselves
	 * are also refcounted, so the request will only be freed when the last
	 * reference to it is dismissed, and the code in
	 * i915_gem_request_free() will then decrement the refcount on the
	 * context.
	 */
	struct i915_gem_context *ctx;
	struct intel_engine_cs *engine;
	struct intel_ring *ring;
	struct intel_signal_node signaling;

	struct i915_sw_fence submit;
	wait_queue_t submitq;

	/** GEM sequence number associated with the previous request,
	 * when the HWS breadcrumb is equal to this the GPU is processing
	 * this request.
	 */
	u32 previous_seqno;

	/** Position in the ring of the start of the request */
	u32 head;

	/**
	 * Position in the ring of the start of the postfix.
	 * This is required to calculate the maximum available ring space
	 * without overwriting the postfix.
	 */
	u32 postfix;

	/** Position in the ring of the end of the whole request */
	u32 tail;

	/** Position in the ring of the end of any workarounds after the tail */
	u32 wa_tail;

	/** Preallocate space in the ring for the emitting the request */
	u32 reserved_space;

	/**
	 * Context related to the previous request.
	 * As the contexts are accessed by the hardware until the switch is
	 * completed to a new context, the hardware may still be writing
	 * to the context object after the breadcrumb is visible. We must
	 * not unpin/unbind/prune that object whilst still active and so
	 * we keep the previous context pinned until the following (this)
	 * request is retired.
	 */
	struct i915_gem_context *previous_context;

	/** Batch buffer related to this request if any (used for
	 * error state dump only).
	 */
	struct i915_vma *batch;
	struct list_head active_list;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	/** engine->request_list entry for this request */
	struct list_head link;

	/** ring->request_list entry for this request */
	struct list_head ring_link;

	struct drm_i915_file_private *file_priv;
	/** file_priv list entry for this request */
	struct list_head client_list;

	/** Link in the execlist submission queue, guarded by execlist_lock. */
	struct list_head execlist_link;
};

extern const struct fence_ops i915_fence_ops;

static inline bool fence_is_i915(struct fence *fence)
{
	return fence->ops == &i915_fence_ops;
}

struct drm_i915_gem_request * __must_check
i915_gem_request_alloc(struct intel_engine_cs *engine,
		       struct i915_gem_context *ctx);
int i915_gem_request_add_to_client(struct drm_i915_gem_request *req,
				   struct drm_file *file);
void i915_gem_request_retire_upto(struct drm_i915_gem_request *req);

static inline u32
i915_gem_request_get_seqno(struct drm_i915_gem_request *req)
{
	return req ? req->fence.seqno : 0;
}

static inline struct intel_engine_cs *
i915_gem_request_get_engine(struct drm_i915_gem_request *req)
{
	return req ? req->engine : NULL;
}

static inline struct drm_i915_gem_request *
to_request(struct fence *fence)
{
	/* We assume that NULL fence/request are interoperable */
	BUILD_BUG_ON(offsetof(struct drm_i915_gem_request, fence) != 0);
	GEM_BUG_ON(fence && !fence_is_i915(fence));
	return container_of(fence, struct drm_i915_gem_request, fence);
}

static inline struct drm_i915_gem_request *
i915_gem_request_get(struct drm_i915_gem_request *req)
{
	return to_request(fence_get(&req->fence));
}

static inline struct drm_i915_gem_request *
i915_gem_request_get_rcu(struct drm_i915_gem_request *req)
{
	return to_request(fence_get_rcu(&req->fence));
}

static inline void
i915_gem_request_put(struct drm_i915_gem_request *req)
{
	fence_put(&req->fence);
}

static inline void i915_gem_request_assign(struct drm_i915_gem_request **pdst,
					   struct drm_i915_gem_request *src)
{
	if (src)
		i915_gem_request_get(src);

	if (*pdst)
		i915_gem_request_put(*pdst);

	*pdst = src;
}

int
i915_gem_request_await_object(struct drm_i915_gem_request *to,
			      struct drm_i915_gem_object *obj,
			      bool write);

void __i915_add_request(struct drm_i915_gem_request *req, bool flush_caches);
#define i915_add_request(req) \
	__i915_add_request(req, true)
#define i915_add_request_no_flush(req) \
	__i915_add_request(req, false)

struct intel_rps_client;
#define NO_WAITBOOST ERR_PTR(-1)
#define IS_RPS_CLIENT(p) (!IS_ERR(p))
#define IS_RPS_USER(p) (!IS_ERR_OR_NULL(p))

int i915_wait_request(struct drm_i915_gem_request *req,
		      unsigned int flags,
		      s64 *timeout,
		      struct intel_rps_client *rps)
	__attribute__((nonnull(1)));
#define I915_WAIT_INTERRUPTIBLE	BIT(0)
#define I915_WAIT_LOCKED	BIT(1) /* struct_mutex held, handle GPU reset */

static inline u32 intel_engine_get_seqno(struct intel_engine_cs *engine);

/**
 * Returns true if seq1 is later than seq2.
 */
static inline bool i915_seqno_passed(u32 seq1, u32 seq2)
{
	return (s32)(seq1 - seq2) >= 0;
}

static inline bool
i915_gem_request_started(const struct drm_i915_gem_request *req)
{
	return i915_seqno_passed(intel_engine_get_seqno(req->engine),
				 req->previous_seqno);
}

static inline bool
i915_gem_request_completed(const struct drm_i915_gem_request *req)
{
	return i915_seqno_passed(intel_engine_get_seqno(req->engine),
				 req->fence.seqno);
}

bool __i915_spin_request(const struct drm_i915_gem_request *request,
			 int state, unsigned long timeout_us);
static inline bool i915_spin_request(const struct drm_i915_gem_request *request,
				     int state, unsigned long timeout_us)
{
	return (i915_gem_request_started(request) &&
		__i915_spin_request(request, state, timeout_us));
}

/* We treat requests as fences. This is not be to confused with our
 * "fence registers" but pipeline synchronisation objects ala GL_ARB_sync.
 * We use the fences to synchronize access from the CPU with activity on the
 * GPU, for example, we should not rewrite an object's PTE whilst the GPU
 * is reading them. We also track fences at a higher level to provide
 * implicit synchronisation around GEM objects, e.g. set-domain will wait
 * for outstanding GPU rendering before marking the object ready for CPU
 * access, or a pageflip will wait until the GPU is complete before showing
 * the frame on the scanout.
 *
 * In order to use a fence, the object must track the fence it needs to
 * serialise with. For example, GEM objects want to track both read and
 * write access so that we can perform concurrent read operations between
 * the CPU and GPU engines, as well as waiting for all rendering to
 * complete, or waiting for the last GPU user of a "fence register". The
 * object then embeds a #i915_gem_active to track the most recent (in
 * retirement order) request relevant for the desired mode of access.
 * The #i915_gem_active is updated with i915_gem_active_set() to track the
 * most recent fence request, typically this is done as part of
 * i915_vma_move_to_active().
 *
 * When the #i915_gem_active completes (is retired), it will
 * signal its completion to the owner through a callback as well as mark
 * itself as idle (i915_gem_active.request == NULL). The owner
 * can then perform any action, such as delayed freeing of an active
 * resource including itself.
 */
struct i915_gem_active;

typedef void (*i915_gem_retire_fn)(struct i915_gem_active *,
				   struct drm_i915_gem_request *);

struct i915_gem_active {
	struct drm_i915_gem_request __rcu *request;
	struct list_head link;
	i915_gem_retire_fn retire;
};

void i915_gem_retire_noop(struct i915_gem_active *,
			  struct drm_i915_gem_request *request);

/**
 * init_request_active - prepares the activity tracker for use
 * @active - the active tracker
 * @func - a callback when then the tracker is retired (becomes idle),
 *         can be NULL
 *
 * init_request_active() prepares the embedded @active struct for use as
 * an activity tracker, that is for tracking the last known active request
 * associated with it. When the last request becomes idle, when it is retired
 * after completion, the optional callback @func is invoked.
 */
static inline void
init_request_active(struct i915_gem_active *active,
		    i915_gem_retire_fn retire)
{
	INIT_LIST_HEAD(&active->link);
	active->retire = retire ?: i915_gem_retire_noop;
}

/**
 * i915_gem_active_set - updates the tracker to watch the current request
 * @active - the active tracker
 * @request - the request to watch
 *
 * i915_gem_active_set() watches the given @request for completion. Whilst
 * that @request is busy, the @active reports busy. When that @request is
 * retired, the @active tracker is updated to report idle.
 */
static inline void
i915_gem_active_set(struct i915_gem_active *active,
		    struct drm_i915_gem_request *request)
{
	list_move(&active->link, &request->active_list);
	rcu_assign_pointer(active->request, request);
}

static inline struct drm_i915_gem_request *
__i915_gem_active_peek(const struct i915_gem_active *active)
{
	/* Inside the error capture (running with the driver in an unknown
	 * state), we want to bend the rules slightly (a lot).
	 *
	 * Work is in progress to make it safer, in the meantime this keeps
	 * the known issue from spamming the logs.
	 */
	return rcu_dereference_protected(active->request, 1);
}

/**
 * i915_gem_active_raw - return the active request
 * @active - the active tracker
 *
 * i915_gem_active_raw() returns the current request being tracked, or NULL.
 * It does not obtain a reference on the request for the caller, so the caller
 * must hold struct_mutex.
 */
static inline struct drm_i915_gem_request *
i915_gem_active_raw(const struct i915_gem_active *active, struct mutex *mutex)
{
	return rcu_dereference_protected(active->request,
					 lockdep_is_held(mutex));
}

/**
 * i915_gem_active_peek - report the active request being monitored
 * @active - the active tracker
 *
 * i915_gem_active_peek() returns the current request being tracked if
 * still active, or NULL. It does not obtain a reference on the request
 * for the caller, so the caller must hold struct_mutex.
 */
static inline struct drm_i915_gem_request *
i915_gem_active_peek(const struct i915_gem_active *active, struct mutex *mutex)
{
	struct drm_i915_gem_request *request;

	request = i915_gem_active_raw(active, mutex);
	if (!request || i915_gem_request_completed(request))
		return NULL;

	return request;
}

/**
 * i915_gem_active_get - return a reference to the active request
 * @active - the active tracker
 *
 * i915_gem_active_get() returns a reference to the active request, or NULL
 * if the active tracker is idle. The caller must hold struct_mutex.
 */
static inline struct drm_i915_gem_request *
i915_gem_active_get(const struct i915_gem_active *active, struct mutex *mutex)
{
	return i915_gem_request_get(i915_gem_active_peek(active, mutex));
}

/**
 * __i915_gem_active_get_rcu - return a reference to the active request
 * @active - the active tracker
 *
 * __i915_gem_active_get() returns a reference to the active request, or NULL
 * if the active tracker is idle. The caller must hold the RCU read lock, but
 * the returned pointer is safe to use outside of RCU.
 */
static inline struct drm_i915_gem_request *
__i915_gem_active_get_rcu(const struct i915_gem_active *active)
{
	/* Performing a lockless retrieval of the active request is super
	 * tricky. SLAB_DESTROY_BY_RCU merely guarantees that the backing
	 * slab of request objects will not be freed whilst we hold the
	 * RCU read lock. It does not guarantee that the request itself
	 * will not be freed and then *reused*. Viz,
	 *
	 * Thread A			Thread B
	 *
	 * req = active.request
	 *				retire(req) -> free(req);
	 *				(req is now first on the slab freelist)
	 *				active.request = NULL
	 *
	 *				req = new submission on a new object
	 * ref(req)
	 *
	 * To prevent the request from being reused whilst the caller
	 * uses it, we take a reference like normal. Whilst acquiring
	 * the reference we check that it is not in a destroyed state
	 * (refcnt == 0). That prevents the request being reallocated
	 * whilst the caller holds on to it. To check that the request
	 * was not reallocated as we acquired the reference we have to
	 * check that our request remains the active request across
	 * the lookup, in the same manner as a seqlock. The visibility
	 * of the pointer versus the reference counting is controlled
	 * by using RCU barriers (rcu_dereference and rcu_assign_pointer).
	 *
	 * In the middle of all that, we inspect whether the request is
	 * complete. Retiring is lazy so the request may be completed long
	 * before the active tracker is updated. Querying whether the
	 * request is complete is far cheaper (as it involves no locked
	 * instructions setting cachelines to exclusive) than acquiring
	 * the reference, so we do it first. The RCU read lock ensures the
	 * pointer dereference is valid, but does not ensure that the
	 * seqno nor HWS is the right one! However, if the request was
	 * reallocated, that means the active tracker's request was complete.
	 * If the new request is also complete, then both are and we can
	 * just report the active tracker is idle. If the new request is
	 * incomplete, then we acquire a reference on it and check that
	 * it remained the active request.
	 *
	 * It is then imperative that we do not zero the request on
	 * reallocation, so that we can chase the dangling pointers!
	 * See i915_gem_request_alloc().
	 */
	do {
		struct drm_i915_gem_request *request;

		request = rcu_dereference(active->request);
		if (!request || i915_gem_request_completed(request))
			return NULL;

		/* An especially silly compiler could decide to recompute the
		 * result of i915_gem_request_completed, more specifically
		 * re-emit the load for request->fence.seqno. A race would catch
		 * a later seqno value, which could flip the result from true to
		 * false. Which means part of the instructions below might not
		 * be executed, while later on instructions are executed. Due to
		 * barriers within the refcounting the inconsistency can't reach
		 * past the call to i915_gem_request_get_rcu, but not executing
		 * that while still executing i915_gem_request_put() creates
		 * havoc enough.  Prevent this with a compiler barrier.
		 */
		barrier();

		request = i915_gem_request_get_rcu(request);

		/* What stops the following rcu_access_pointer() from occurring
		 * before the above i915_gem_request_get_rcu()? If we were
		 * to read the value before pausing to get the reference to
		 * the request, we may not notice a change in the active
		 * tracker.
		 *
		 * The rcu_access_pointer() is a mere compiler barrier, which
		 * means both the CPU and compiler are free to perform the
		 * memory read without constraint. The compiler only has to
		 * ensure that any operations after the rcu_access_pointer()
		 * occur afterwards in program order. This means the read may
		 * be performed earlier by an out-of-order CPU, or adventurous
		 * compiler.
		 *
		 * The atomic operation at the heart of
		 * i915_gem_request_get_rcu(), see fence_get_rcu(), is
		 * atomic_inc_not_zero() which is only a full memory barrier
		 * when successful. That is, if i915_gem_request_get_rcu()
		 * returns the request (and so with the reference counted
		 * incremented) then the following read for rcu_access_pointer()
		 * must occur after the atomic operation and so confirm
		 * that this request is the one currently being tracked.
		 *
		 * The corresponding write barrier is part of
		 * rcu_assign_pointer().
		 */
		if (!request || request == rcu_access_pointer(active->request))
			return rcu_pointer_handoff(request);

		i915_gem_request_put(request);
	} while (1);
}

/**
 * i915_gem_active_get_unlocked - return a reference to the active request
 * @active - the active tracker
 *
 * i915_gem_active_get_unlocked() returns a reference to the active request,
 * or NULL if the active tracker is idle. The reference is obtained under RCU,
 * so no locking is required by the caller.
 *
 * The reference should be freed with i915_gem_request_put().
 */
static inline struct drm_i915_gem_request *
i915_gem_active_get_unlocked(const struct i915_gem_active *active)
{
	struct drm_i915_gem_request *request;

	rcu_read_lock();
	request = __i915_gem_active_get_rcu(active);
	rcu_read_unlock();

	return request;
}

/**
 * i915_gem_active_isset - report whether the active tracker is assigned
 * @active - the active tracker
 *
 * i915_gem_active_isset() returns true if the active tracker is currently
 * assigned to a request. Due to the lazy retiring, that request may be idle
 * and this may report stale information.
 */
static inline bool
i915_gem_active_isset(const struct i915_gem_active *active)
{
	return rcu_access_pointer(active->request);
}

/**
 * i915_gem_active_is_idle - report whether the active tracker is idle
 * @active - the active tracker
 *
 * i915_gem_active_is_idle() returns true if the active tracker is currently
 * unassigned or if the request is complete (but not yet retired). Requires
 * the caller to hold struct_mutex (but that can be relaxed if desired).
 */
static inline bool
i915_gem_active_is_idle(const struct i915_gem_active *active,
			struct mutex *mutex)
{
	return !i915_gem_active_peek(active, mutex);
}

/**
 * i915_gem_active_wait - waits until the request is completed
 * @active - the active request on which to wait
 *
 * i915_gem_active_wait() waits until the request is completed before
 * returning. Note that it does not guarantee that the request is
 * retired first, see i915_gem_active_retire().
 *
 * i915_gem_active_wait() returns immediately if the active
 * request is already complete.
 */
static inline int __must_check
i915_gem_active_wait(const struct i915_gem_active *active, struct mutex *mutex)
{
	struct drm_i915_gem_request *request;

	request = i915_gem_active_peek(active, mutex);
	if (!request)
		return 0;

	return i915_wait_request(request,
				 I915_WAIT_INTERRUPTIBLE | I915_WAIT_LOCKED,
				 NULL, NULL);
}

/**
 * i915_gem_active_wait_unlocked - waits until the request is completed
 * @active - the active request on which to wait
 * @flags - how to wait
 * @timeout - how long to wait at most
 * @rps - userspace client to charge for a waitboost
 *
 * i915_gem_active_wait_unlocked() waits until the request is completed before
 * returning, without requiring any locks to be held. Note that it does not
 * retire any requests before returning.
 *
 * This function relies on RCU in order to acquire the reference to the active
 * request without holding any locks. See __i915_gem_active_get_rcu() for the
 * glory details on how that is managed. Once the reference is acquired, we
 * can then wait upon the request, and afterwards release our reference,
 * free of any locking.
 *
 * This function wraps i915_wait_request(), see it for the full details on
 * the arguments.
 *
 * Returns 0 if successful, or a negative error code.
 */
static inline int
i915_gem_active_wait_unlocked(const struct i915_gem_active *active,
			      unsigned int flags,
			      s64 *timeout,
			      struct intel_rps_client *rps)
{
	struct drm_i915_gem_request *request;
	int ret = 0;

	request = i915_gem_active_get_unlocked(active);
	if (request) {
		ret = i915_wait_request(request, flags, timeout, rps);
		i915_gem_request_put(request);
	}

	return ret;
}

/**
 * i915_gem_active_retire - waits until the request is retired
 * @active - the active request on which to wait
 *
 * i915_gem_active_retire() waits until the request is completed,
 * and then ensures that at least the retirement handler for this
 * @active tracker is called before returning. If the @active
 * tracker is idle, the function returns immediately.
 */
static inline int __must_check
i915_gem_active_retire(struct i915_gem_active *active,
		       struct mutex *mutex)
{
	struct drm_i915_gem_request *request;
	int ret;

	request = i915_gem_active_raw(active, mutex);
	if (!request)
		return 0;

	ret = i915_wait_request(request,
				I915_WAIT_INTERRUPTIBLE | I915_WAIT_LOCKED,
				NULL, NULL);
	if (ret)
		return ret;

	list_del_init(&active->link);
	RCU_INIT_POINTER(active->request, NULL);

	active->retire(active, request);

	return 0;
}

/* Convenience functions for peeking at state inside active's request whilst
 * guarded by the struct_mutex.
 */

static inline uint32_t
i915_gem_active_get_seqno(const struct i915_gem_active *active,
			  struct mutex *mutex)
{
	return i915_gem_request_get_seqno(i915_gem_active_peek(active, mutex));
}

static inline struct intel_engine_cs *
i915_gem_active_get_engine(const struct i915_gem_active *active,
			   struct mutex *mutex)
{
	return i915_gem_request_get_engine(i915_gem_active_peek(active, mutex));
}

#define for_each_active(mask, idx) \
	for (; mask ? idx = ffs(mask) - 1, 1 : 0; mask &= ~BIT(idx))

#endif /* I915_GEM_REQUEST_H */
