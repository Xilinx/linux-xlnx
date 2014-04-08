/*
 * Copyright © 2008-2010 Intel Corporation
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
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Chris Wilson <chris@chris-wilson.co.uuk>
 *
 */

#include <drm/drmP.h>
#include "i915_drv.h"
#include <drm/i915_drm.h>
#include "i915_trace.h"

static bool
mark_free(struct i915_vma *vma, struct list_head *unwind)
{
	if (vma->obj->pin_count)
		return false;

	if (WARN_ON(!list_empty(&vma->exec_list)))
		return false;

	list_add(&vma->exec_list, unwind);
	return drm_mm_scan_add_block(&vma->node);
}

int
i915_gem_evict_something(struct drm_device *dev, struct i915_address_space *vm,
			 int min_size, unsigned alignment, unsigned cache_level,
			 bool mappable, bool nonblocking)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct list_head eviction_list, unwind_list;
	struct i915_vma *vma;
	int ret = 0;

	trace_i915_gem_evict(dev, min_size, alignment, mappable);

	/*
	 * The goal is to evict objects and amalgamate space in LRU order.
	 * The oldest idle objects reside on the inactive list, which is in
	 * retirement order. The next objects to retire are those on the (per
	 * ring) active list that do not have an outstanding flush. Once the
	 * hardware reports completion (the seqno is updated after the
	 * batchbuffer has been finished) the clean buffer objects would
	 * be retired to the inactive list. Any dirty objects would be added
	 * to the tail of the flushing list. So after processing the clean
	 * active objects we need to emit a MI_FLUSH to retire the flushing
	 * list, hence the retirement order of the flushing list is in
	 * advance of the dirty objects on the active lists.
	 *
	 * The retirement sequence is thus:
	 *   1. Inactive objects (already retired)
	 *   2. Clean active objects
	 *   3. Flushing list
	 *   4. Dirty active objects.
	 *
	 * On each list, the oldest objects lie at the HEAD with the freshest
	 * object on the TAIL.
	 */

	INIT_LIST_HEAD(&unwind_list);
	if (mappable) {
		BUG_ON(!i915_is_ggtt(vm));
		drm_mm_init_scan_with_range(&vm->mm, min_size,
					    alignment, cache_level, 0,
					    dev_priv->gtt.mappable_end);
	} else
		drm_mm_init_scan(&vm->mm, min_size, alignment, cache_level);

search_again:
	/* First see if there is a large enough contiguous idle region... */
	list_for_each_entry(vma, &vm->inactive_list, mm_list) {
		if (mark_free(vma, &unwind_list))
			goto found;
	}

	if (nonblocking)
		goto none;

	/* Now merge in the soon-to-be-expired objects... */
	list_for_each_entry(vma, &vm->active_list, mm_list) {
		if (mark_free(vma, &unwind_list))
			goto found;
	}

none:
	/* Nothing found, clean up and bail out! */
	while (!list_empty(&unwind_list)) {
		vma = list_first_entry(&unwind_list,
				       struct i915_vma,
				       exec_list);
		ret = drm_mm_scan_remove_block(&vma->node);
		BUG_ON(ret);

		list_del_init(&vma->exec_list);
	}

	/* Can we unpin some objects such as idle hw contents,
	 * or pending flips?
	 */
	ret = nonblocking ? -ENOSPC : i915_gpu_idle(dev);
	if (ret)
		return ret;

	/* Only idle the GPU and repeat the search once */
	i915_gem_retire_requests(dev);
	nonblocking = true;
	goto search_again;

found:
	/* drm_mm doesn't allow any other other operations while
	 * scanning, therefore store to be evicted objects on a
	 * temporary list. */
	INIT_LIST_HEAD(&eviction_list);
	while (!list_empty(&unwind_list)) {
		vma = list_first_entry(&unwind_list,
				       struct i915_vma,
				       exec_list);
		if (drm_mm_scan_remove_block(&vma->node)) {
			list_move(&vma->exec_list, &eviction_list);
			drm_gem_object_reference(&vma->obj->base);
			continue;
		}
		list_del_init(&vma->exec_list);
	}

	/* Unbinding will emit any required flushes */
	while (!list_empty(&eviction_list)) {
		struct drm_gem_object *obj;
		vma = list_first_entry(&eviction_list,
				       struct i915_vma,
				       exec_list);

		obj =  &vma->obj->base;
		list_del_init(&vma->exec_list);
		if (ret == 0)
			ret = i915_vma_unbind(vma);

		drm_gem_object_unreference(obj);
	}

	return ret;
}

/**
 * i915_gem_evict_vm - Try to free up VM space
 *
 * @vm: Address space to evict from
 * @do_idle: Boolean directing whether to idle first.
 *
 * VM eviction is about freeing up virtual address space. If one wants fine
 * grained eviction, they should see evict something for more details. In terms
 * of freeing up actual system memory, this function may not accomplish the
 * desired result. An object may be shared in multiple address space, and this
 * function will not assert those objects be freed.
 *
 * Using do_idle will result in a more complete eviction because it retires, and
 * inactivates current BOs.
 */
int i915_gem_evict_vm(struct i915_address_space *vm, bool do_idle)
{
	struct i915_vma *vma, *next;
	int ret;

	trace_i915_gem_evict_vm(vm);

	if (do_idle) {
		ret = i915_gpu_idle(vm->dev);
		if (ret)
			return ret;

		i915_gem_retire_requests(vm->dev);
	}

	list_for_each_entry_safe(vma, next, &vm->inactive_list, mm_list)
		if (vma->obj->pin_count == 0)
			WARN_ON(i915_vma_unbind(vma));

	return 0;
}

int
i915_gem_evict_everything(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct i915_address_space *vm;
	bool lists_empty = true;
	int ret;

	list_for_each_entry(vm, &dev_priv->vm_list, global_link) {
		lists_empty = (list_empty(&vm->inactive_list) &&
			       list_empty(&vm->active_list));
		if (!lists_empty)
			lists_empty = false;
	}

	if (lists_empty)
		return -ENOSPC;

	trace_i915_gem_evict_everything(dev);

	/* The gpu_idle will flush everything in the write domain to the
	 * active list. Then we must move everything off the active list
	 * with retire requests.
	 */
	ret = i915_gpu_idle(dev);
	if (ret)
		return ret;

	i915_gem_retire_requests(dev);

	/* Having flushed everything, unbind() should never raise an error */
	list_for_each_entry(vm, &dev_priv->vm_list, global_link)
		WARN_ON(i915_gem_evict_vm(vm, false));

	return 0;
}
