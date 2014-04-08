/*
 * Copyright © 2008 Intel Corporation
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
 *
 */

#include <drm/drmP.h>
#include <drm/drm_vma_manager.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>

static void i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj);
static void i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj,
						   bool force);
static __must_check int
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool readonly);
static __must_check int
i915_gem_object_bind_to_vm(struct drm_i915_gem_object *obj,
			   struct i915_address_space *vm,
			   unsigned alignment,
			   bool map_and_fenceable,
			   bool nonblocking);
static int i915_gem_phys_pwrite(struct drm_device *dev,
				struct drm_i915_gem_object *obj,
				struct drm_i915_gem_pwrite *args,
				struct drm_file *file);

static void i915_gem_write_fence(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj);
static void i915_gem_object_update_fence(struct drm_i915_gem_object *obj,
					 struct drm_i915_fence_reg *fence,
					 bool enable);

static unsigned long i915_gem_inactive_count(struct shrinker *shrinker,
					     struct shrink_control *sc);
static unsigned long i915_gem_inactive_scan(struct shrinker *shrinker,
					    struct shrink_control *sc);
static unsigned long i915_gem_purge(struct drm_i915_private *dev_priv, long target);
static unsigned long i915_gem_shrink_all(struct drm_i915_private *dev_priv);
static void i915_gem_object_truncate(struct drm_i915_gem_object *obj);

static bool cpu_cache_is_coherent(struct drm_device *dev,
				  enum i915_cache_level level)
{
	return HAS_LLC(dev) || level != I915_CACHE_NONE;
}

static bool cpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	if (!cpu_cache_is_coherent(obj->base.dev, obj->cache_level))
		return true;

	return obj->pin_display;
}

static inline void i915_gem_object_fence_lost(struct drm_i915_gem_object *obj)
{
	if (obj->tiling_mode)
		i915_gem_release_mmap(obj);

	/* As we do not have an associated fence register, we will force
	 * a tiling change if we ever need to acquire one.
	 */
	obj->fence_dirty = false;
	obj->fence_reg = I915_FENCE_REG_NONE;
}

/* some bookkeeping */
static void i915_gem_info_add_obj(struct drm_i915_private *dev_priv,
				  size_t size)
{
	spin_lock(&dev_priv->mm.object_stat_lock);
	dev_priv->mm.object_count++;
	dev_priv->mm.object_memory += size;
	spin_unlock(&dev_priv->mm.object_stat_lock);
}

static void i915_gem_info_remove_obj(struct drm_i915_private *dev_priv,
				     size_t size)
{
	spin_lock(&dev_priv->mm.object_stat_lock);
	dev_priv->mm.object_count--;
	dev_priv->mm.object_memory -= size;
	spin_unlock(&dev_priv->mm.object_stat_lock);
}

static int
i915_gem_wait_for_error(struct i915_gpu_error *error)
{
	int ret;

#define EXIT_COND (!i915_reset_in_progress(error) || \
		   i915_terminally_wedged(error))
	if (EXIT_COND)
		return 0;

	/*
	 * Only wait 10 seconds for the gpu reset to complete to avoid hanging
	 * userspace. If it takes that long something really bad is going on and
	 * we should simply try to bail out and fail as gracefully as possible.
	 */
	ret = wait_event_interruptible_timeout(error->reset_queue,
					       EXIT_COND,
					       10*HZ);
	if (ret == 0) {
		DRM_ERROR("Timed out waiting for the gpu reset to complete\n");
		return -EIO;
	} else if (ret < 0) {
		return ret;
	}
#undef EXIT_COND

	return 0;
}

int i915_mutex_lock_interruptible(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_wait_for_error(&dev_priv->gpu_error);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	WARN_ON(i915_verify_lists(dev));
	return 0;
}

static inline bool
i915_gem_object_is_inactive(struct drm_i915_gem_object *obj)
{
	return i915_gem_obj_bound_any(obj) && !obj->active;
}

int
i915_gem_init_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_init *args = data;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (args->gtt_start >= args->gtt_end ||
	    (args->gtt_end | args->gtt_start) & (PAGE_SIZE - 1))
		return -EINVAL;

	/* GEM with user mode setting was never supported on ilk and later. */
	if (INTEL_INFO(dev)->gen >= 5)
		return -ENODEV;

	mutex_lock(&dev->struct_mutex);
	i915_gem_setup_global_gtt(dev, args->gtt_start, args->gtt_end,
				  args->gtt_end);
	dev_priv->gtt.mappable_end = args->gtt_end;
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int
i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_get_aperture *args = data;
	struct drm_i915_gem_object *obj;
	size_t pinned;

	pinned = 0;
	mutex_lock(&dev->struct_mutex);
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list)
		if (obj->pin_count)
			pinned += i915_gem_obj_ggtt_size(obj);
	mutex_unlock(&dev->struct_mutex);

	args->aper_size = dev_priv->gtt.base.total;
	args->aper_available_size = args->aper_size - pinned;

	return 0;
}

void *i915_gem_object_alloc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	return kmem_cache_zalloc(dev_priv->slab, GFP_KERNEL);
}

void i915_gem_object_free(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	kmem_cache_free(dev_priv->slab, obj);
}

static int
i915_gem_create(struct drm_file *file,
		struct drm_device *dev,
		uint64_t size,
		uint32_t *handle_p)
{
	struct drm_i915_gem_object *obj;
	int ret;
	u32 handle;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	/* Allocate the new object */
	obj = i915_gem_alloc_object(dev, size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&obj->base);
	if (ret)
		return ret;

	*handle_p = handle;
	return 0;
}

int
i915_gem_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	/* have to work out size/pitch and return them */
	args->pitch = ALIGN(args->width * DIV_ROUND_UP(args->bpp, 8), 64);
	args->size = args->pitch * args->height;
	return i915_gem_create(file, dev,
			       args->size, &args->handle);
}

/**
 * Creates a new mm object and returns a handle to it.
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_gem_create *args = data;

	return i915_gem_create(file, dev,
			       args->size, &args->handle);
}

static inline int
__copy_to_user_swizzled(char __user *cpu_vaddr,
			const char *gpu_vaddr, int gpu_offset,
			int length)
{
	int ret, cpu_offset = 0;

	while (length > 0) {
		int cacheline_end = ALIGN(gpu_offset + 1, 64);
		int this_length = min(cacheline_end - gpu_offset, length);
		int swizzled_gpu_offset = gpu_offset ^ 64;

		ret = __copy_to_user(cpu_vaddr + cpu_offset,
				     gpu_vaddr + swizzled_gpu_offset,
				     this_length);
		if (ret)
			return ret + length;

		cpu_offset += this_length;
		gpu_offset += this_length;
		length -= this_length;
	}

	return 0;
}

static inline int
__copy_from_user_swizzled(char *gpu_vaddr, int gpu_offset,
			  const char __user *cpu_vaddr,
			  int length)
{
	int ret, cpu_offset = 0;

	while (length > 0) {
		int cacheline_end = ALIGN(gpu_offset + 1, 64);
		int this_length = min(cacheline_end - gpu_offset, length);
		int swizzled_gpu_offset = gpu_offset ^ 64;

		ret = __copy_from_user(gpu_vaddr + swizzled_gpu_offset,
				       cpu_vaddr + cpu_offset,
				       this_length);
		if (ret)
			return ret + length;

		cpu_offset += this_length;
		gpu_offset += this_length;
		length -= this_length;
	}

	return 0;
}

/* Per-page copy function for the shmem pread fastpath.
 * Flushes invalid cachelines before reading the target if
 * needs_clflush is set. */
static int
shmem_pread_fast(struct page *page, int shmem_page_offset, int page_length,
		 char __user *user_data,
		 bool page_do_bit17_swizzling, bool needs_clflush)
{
	char *vaddr;
	int ret;

	if (unlikely(page_do_bit17_swizzling))
		return -EINVAL;

	vaddr = kmap_atomic(page);
	if (needs_clflush)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	ret = __copy_to_user_inatomic(user_data,
				      vaddr + shmem_page_offset,
				      page_length);
	kunmap_atomic(vaddr);

	return ret ? -EFAULT : 0;
}

static void
shmem_clflush_swizzled_range(char *addr, unsigned long length,
			     bool swizzled)
{
	if (unlikely(swizzled)) {
		unsigned long start = (unsigned long) addr;
		unsigned long end = (unsigned long) addr + length;

		/* For swizzling simply ensure that we always flush both
		 * channels. Lame, but simple and it works. Swizzled
		 * pwrite/pread is far from a hotpath - current userspace
		 * doesn't use it at all. */
		start = round_down(start, 128);
		end = round_up(end, 128);

		drm_clflush_virt_range((void *)start, end - start);
	} else {
		drm_clflush_virt_range(addr, length);
	}

}

/* Only difference to the fast-path function is that this can handle bit17
 * and uses non-atomic copy and kmap functions. */
static int
shmem_pread_slow(struct page *page, int shmem_page_offset, int page_length,
		 char __user *user_data,
		 bool page_do_bit17_swizzling, bool needs_clflush)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (needs_clflush)
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);

	if (page_do_bit17_swizzling)
		ret = __copy_to_user_swizzled(user_data,
					      vaddr, shmem_page_offset,
					      page_length);
	else
		ret = __copy_to_user(user_data,
				     vaddr + shmem_page_offset,
				     page_length);
	kunmap(page);

	return ret ? - EFAULT : 0;
}

static int
i915_gem_shmem_pread(struct drm_device *dev,
		     struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pread *args,
		     struct drm_file *file)
{
	char __user *user_data;
	ssize_t remain;
	loff_t offset;
	int shmem_page_offset, page_length, ret = 0;
	int obj_do_bit17_swizzling, page_do_bit17_swizzling;
	int prefaulted = 0;
	int needs_clflush = 0;
	struct sg_page_iter sg_iter;

	user_data = to_user_ptr(args->data_ptr);
	remain = args->size;

	obj_do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);

	if (!(obj->base.read_domains & I915_GEM_DOMAIN_CPU)) {
		/* If we're not in the cpu read domain, set ourself into the gtt
		 * read domain and manually flush cachelines (if required). This
		 * optimizes for the case when the gpu will dirty the data
		 * anyway again before the next pread happens. */
		needs_clflush = !cpu_cache_is_coherent(dev, obj->cache_level);
		ret = i915_gem_object_wait_rendering(obj, true);
		if (ret)
			return ret;
	}

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	offset = args->offset;

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents,
			 offset >> PAGE_SHIFT) {
		struct page *page = sg_page_iter_page(&sg_iter);

		if (remain <= 0)
			break;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset_in_page(offset);
		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;

		page_do_bit17_swizzling = obj_do_bit17_swizzling &&
			(page_to_phys(page) & (1 << 17)) != 0;

		ret = shmem_pread_fast(page, shmem_page_offset, page_length,
				       user_data, page_do_bit17_swizzling,
				       needs_clflush);
		if (ret == 0)
			goto next_page;

		mutex_unlock(&dev->struct_mutex);

		if (likely(!i915_prefault_disable) && !prefaulted) {
			ret = fault_in_multipages_writeable(user_data, remain);
			/* Userspace is tricking us, but we've already clobbered
			 * its pages with the prefault and promised to write the
			 * data up to the first fault. Hence ignore any errors
			 * and just continue. */
			(void)ret;
			prefaulted = 1;
		}

		ret = shmem_pread_slow(page, shmem_page_offset, page_length,
				       user_data, page_do_bit17_swizzling,
				       needs_clflush);

		mutex_lock(&dev->struct_mutex);

next_page:
		mark_page_accessed(page);

		if (ret)
			goto out;

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out:
	i915_gem_object_unpin_pages(obj);

	return ret;
}

/**
 * Reads data from the object referenced by handle.
 *
 * On error, the contents of *data are undefined.
 */
int
i915_gem_pread_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_pread *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE,
		       to_user_ptr(args->data_ptr),
		       args->size))
		return -EFAULT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Bounds check source.  */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto out;
	}

	/* prime objects have no backing filp to GEM pread/pwrite
	 * pages from.
	 */
	if (!obj->base.filp) {
		ret = -EINVAL;
		goto out;
	}

	trace_i915_gem_object_pread(obj, args->offset, args->size);

	ret = i915_gem_shmem_pread(dev, obj, args, file);

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/* This is the fast write path which cannot handle
 * page faults in the source data
 */

static inline int
fast_user_write(struct io_mapping *mapping,
		loff_t page_base, int page_offset,
		char __user *user_data,
		int length)
{
	void __iomem *vaddr_atomic;
	void *vaddr;
	unsigned long unwritten;

	vaddr_atomic = io_mapping_map_atomic_wc(mapping, page_base);
	/* We can use the cpu mem copy function because this is X86. */
	vaddr = (void __force*)vaddr_atomic + page_offset;
	unwritten = __copy_from_user_inatomic_nocache(vaddr,
						      user_data, length);
	io_mapping_unmap_atomic(vaddr_atomic);
	return unwritten;
}

/**
 * This is the fast pwrite path, where we copy the data directly from the
 * user into the GTT, uncached.
 */
static int
i915_gem_gtt_pwrite_fast(struct drm_device *dev,
			 struct drm_i915_gem_object *obj,
			 struct drm_i915_gem_pwrite *args,
			 struct drm_file *file)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	ssize_t remain;
	loff_t offset, page_base;
	char __user *user_data;
	int page_offset, page_length, ret;

	ret = i915_gem_obj_ggtt_pin(obj, 0, true, true);
	if (ret)
		goto out;

	ret = i915_gem_object_set_to_gtt_domain(obj, true);
	if (ret)
		goto out_unpin;

	ret = i915_gem_object_put_fence(obj);
	if (ret)
		goto out_unpin;

	user_data = to_user_ptr(args->data_ptr);
	remain = args->size;

	offset = i915_gem_obj_ggtt_offset(obj) + args->offset;

	while (remain > 0) {
		/* Operation in this page
		 *
		 * page_base = page offset within aperture
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		page_base = offset & PAGE_MASK;
		page_offset = offset_in_page(offset);
		page_length = remain;
		if ((page_offset + remain) > PAGE_SIZE)
			page_length = PAGE_SIZE - page_offset;

		/* If we get a fault while copying data, then (presumably) our
		 * source page isn't available.  Return the error and we'll
		 * retry in the slow path.
		 */
		if (fast_user_write(dev_priv->gtt.mappable, page_base,
				    page_offset, user_data, page_length)) {
			ret = -EFAULT;
			goto out_unpin;
		}

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out_unpin:
	i915_gem_object_unpin(obj);
out:
	return ret;
}

/* Per-page copy function for the shmem pwrite fastpath.
 * Flushes invalid cachelines before writing to the target if
 * needs_clflush_before is set and flushes out any written cachelines after
 * writing if needs_clflush is set. */
static int
shmem_pwrite_fast(struct page *page, int shmem_page_offset, int page_length,
		  char __user *user_data,
		  bool page_do_bit17_swizzling,
		  bool needs_clflush_before,
		  bool needs_clflush_after)
{
	char *vaddr;
	int ret;

	if (unlikely(page_do_bit17_swizzling))
		return -EINVAL;

	vaddr = kmap_atomic(page);
	if (needs_clflush_before)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	ret = __copy_from_user_inatomic_nocache(vaddr + shmem_page_offset,
						user_data,
						page_length);
	if (needs_clflush_after)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	kunmap_atomic(vaddr);

	return ret ? -EFAULT : 0;
}

/* Only difference to the fast-path function is that this can handle bit17
 * and uses non-atomic copy and kmap functions. */
static int
shmem_pwrite_slow(struct page *page, int shmem_page_offset, int page_length,
		  char __user *user_data,
		  bool page_do_bit17_swizzling,
		  bool needs_clflush_before,
		  bool needs_clflush_after)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (unlikely(needs_clflush_before || page_do_bit17_swizzling))
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);
	if (page_do_bit17_swizzling)
		ret = __copy_from_user_swizzled(vaddr, shmem_page_offset,
						user_data,
						page_length);
	else
		ret = __copy_from_user(vaddr + shmem_page_offset,
				       user_data,
				       page_length);
	if (needs_clflush_after)
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);
	kunmap(page);

	return ret ? -EFAULT : 0;
}

static int
i915_gem_shmem_pwrite(struct drm_device *dev,
		      struct drm_i915_gem_object *obj,
		      struct drm_i915_gem_pwrite *args,
		      struct drm_file *file)
{
	ssize_t remain;
	loff_t offset;
	char __user *user_data;
	int shmem_page_offset, page_length, ret = 0;
	int obj_do_bit17_swizzling, page_do_bit17_swizzling;
	int hit_slowpath = 0;
	int needs_clflush_after = 0;
	int needs_clflush_before = 0;
	struct sg_page_iter sg_iter;

	user_data = to_user_ptr(args->data_ptr);
	remain = args->size;

	obj_do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);

	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU) {
		/* If we're not in the cpu write domain, set ourself into the gtt
		 * write domain and manually flush cachelines (if required). This
		 * optimizes for the case when the gpu will use the data
		 * right away and we therefore have to clflush anyway. */
		needs_clflush_after = cpu_write_needs_clflush(obj);
		ret = i915_gem_object_wait_rendering(obj, false);
		if (ret)
			return ret;
	}
	/* Same trick applies to invalidate partially written cachelines read
	 * before writing. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0)
		needs_clflush_before =
			!cpu_cache_is_coherent(dev, obj->cache_level);

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	offset = args->offset;
	obj->dirty = 1;

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents,
			 offset >> PAGE_SHIFT) {
		struct page *page = sg_page_iter_page(&sg_iter);
		int partial_cacheline_write;

		if (remain <= 0)
			break;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset_in_page(offset);

		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;

		/* If we don't overwrite a cacheline completely we need to be
		 * careful to have up-to-date data by first clflushing. Don't
		 * overcomplicate things and flush the entire patch. */
		partial_cacheline_write = needs_clflush_before &&
			((shmem_page_offset | page_length)
				& (boot_cpu_data.x86_clflush_size - 1));

		page_do_bit17_swizzling = obj_do_bit17_swizzling &&
			(page_to_phys(page) & (1 << 17)) != 0;

		ret = shmem_pwrite_fast(page, shmem_page_offset, page_length,
					user_data, page_do_bit17_swizzling,
					partial_cacheline_write,
					needs_clflush_after);
		if (ret == 0)
			goto next_page;

		hit_slowpath = 1;
		mutex_unlock(&dev->struct_mutex);
		ret = shmem_pwrite_slow(page, shmem_page_offset, page_length,
					user_data, page_do_bit17_swizzling,
					partial_cacheline_write,
					needs_clflush_after);

		mutex_lock(&dev->struct_mutex);

next_page:
		set_page_dirty(page);
		mark_page_accessed(page);

		if (ret)
			goto out;

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out:
	i915_gem_object_unpin_pages(obj);

	if (hit_slowpath) {
		/*
		 * Fixup: Flush cpu caches in case we didn't flush the dirty
		 * cachelines in-line while writing and the object moved
		 * out of the cpu write domain while we've dropped the lock.
		 */
		if (!needs_clflush_after &&
		    obj->base.write_domain != I915_GEM_DOMAIN_CPU) {
			if (i915_gem_clflush_object(obj, obj->pin_display))
				i915_gem_chipset_flush(dev);
		}
	}

	if (needs_clflush_after)
		i915_gem_chipset_flush(dev);

	return ret;
}

/**
 * Writes data to the object referenced by handle.
 *
 * On error, the contents of the buffer that were to be modified are undefined.
 */
int
i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_gem_pwrite *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_READ,
		       to_user_ptr(args->data_ptr),
		       args->size))
		return -EFAULT;

	if (likely(!i915_prefault_disable)) {
		ret = fault_in_multipages_readable(to_user_ptr(args->data_ptr),
						   args->size);
		if (ret)
			return -EFAULT;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Bounds check destination. */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto out;
	}

	/* prime objects have no backing filp to GEM pread/pwrite
	 * pages from.
	 */
	if (!obj->base.filp) {
		ret = -EINVAL;
		goto out;
	}

	trace_i915_gem_object_pwrite(obj, args->offset, args->size);

	ret = -EFAULT;
	/* We can only do the GTT pwrite on untiled buffers, as otherwise
	 * it would end up going through the fenced access, and we'll get
	 * different detiling behavior between reading and writing.
	 * pread/pwrite currently are reading and writing from the CPU
	 * perspective, requiring manual detiling by the client.
	 */
	if (obj->phys_obj) {
		ret = i915_gem_phys_pwrite(dev, obj, args, file);
		goto out;
	}

	if (obj->tiling_mode == I915_TILING_NONE &&
	    obj->base.write_domain != I915_GEM_DOMAIN_CPU &&
	    cpu_write_needs_clflush(obj)) {
		ret = i915_gem_gtt_pwrite_fast(dev, obj, args, file);
		/* Note that the gtt paths might fail with non-page-backed user
		 * pointers (e.g. gtt mappings when moving data between
		 * textures). Fallback to the shmem path in that case. */
	}

	if (ret == -EFAULT || ret == -ENOSPC)
		ret = i915_gem_shmem_pwrite(dev, obj, args, file);

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_check_wedge(struct i915_gpu_error *error,
		     bool interruptible)
{
	if (i915_reset_in_progress(error)) {
		/* Non-interruptible callers can't handle -EAGAIN, hence return
		 * -EIO unconditionally for these. */
		if (!interruptible)
			return -EIO;

		/* Recovery complete, but the reset failed ... */
		if (i915_terminally_wedged(error))
			return -EIO;

		return -EAGAIN;
	}

	return 0;
}

/*
 * Compare seqno against outstanding lazy request. Emit a request if they are
 * equal.
 */
static int
i915_gem_check_olr(struct intel_ring_buffer *ring, u32 seqno)
{
	int ret;

	BUG_ON(!mutex_is_locked(&ring->dev->struct_mutex));

	ret = 0;
	if (seqno == ring->outstanding_lazy_seqno)
		ret = i915_add_request(ring, NULL);

	return ret;
}

static void fake_irq(unsigned long data)
{
	wake_up_process((struct task_struct *)data);
}

static bool missed_irq(struct drm_i915_private *dev_priv,
		       struct intel_ring_buffer *ring)
{
	return test_bit(ring->id, &dev_priv->gpu_error.missed_irq_rings);
}

static bool can_wait_boost(struct drm_i915_file_private *file_priv)
{
	if (file_priv == NULL)
		return true;

	return !atomic_xchg(&file_priv->rps_wait_boost, true);
}

/**
 * __wait_seqno - wait until execution of seqno has finished
 * @ring: the ring expected to report seqno
 * @seqno: duh!
 * @reset_counter: reset sequence associated with the given seqno
 * @interruptible: do an interruptible wait (normally yes)
 * @timeout: in - how long to wait (NULL forever); out - how much time remaining
 *
 * Note: It is of utmost importance that the passed in seqno and reset_counter
 * values have been read by the caller in an smp safe manner. Where read-side
 * locks are involved, it is sufficient to read the reset_counter before
 * unlocking the lock that protects the seqno. For lockless tricks, the
 * reset_counter _must_ be read before, and an appropriate smp_rmb must be
 * inserted.
 *
 * Returns 0 if the seqno was found within the alloted time. Else returns the
 * errno with remaining time filled in timeout argument.
 */
static int __wait_seqno(struct intel_ring_buffer *ring, u32 seqno,
			unsigned reset_counter,
			bool interruptible,
			struct timespec *timeout,
			struct drm_i915_file_private *file_priv)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	struct timespec before, now;
	DEFINE_WAIT(wait);
	long timeout_jiffies;
	int ret;

	WARN(dev_priv->pc8.irqs_disabled, "IRQs disabled\n");

	if (i915_seqno_passed(ring->get_seqno(ring, true), seqno))
		return 0;

	timeout_jiffies = timeout ? timespec_to_jiffies_timeout(timeout) : 1;

	if (dev_priv->info->gen >= 6 && can_wait_boost(file_priv)) {
		gen6_rps_boost(dev_priv);
		if (file_priv)
			mod_delayed_work(dev_priv->wq,
					 &file_priv->mm.idle_work,
					 msecs_to_jiffies(100));
	}

	if (!(dev_priv->gpu_error.test_irq_rings & intel_ring_flag(ring)) &&
	    WARN_ON(!ring->irq_get(ring)))
		return -ENODEV;

	/* Record current time in case interrupted by signal, or wedged */
	trace_i915_gem_request_wait_begin(ring, seqno);
	getrawmonotonic(&before);
	for (;;) {
		struct timer_list timer;
		unsigned long expire;

		prepare_to_wait(&ring->irq_queue, &wait,
				interruptible ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);

		/* We need to check whether any gpu reset happened in between
		 * the caller grabbing the seqno and now ... */
		if (reset_counter != atomic_read(&dev_priv->gpu_error.reset_counter)) {
			/* ... but upgrade the -EAGAIN to an -EIO if the gpu
			 * is truely gone. */
			ret = i915_gem_check_wedge(&dev_priv->gpu_error, interruptible);
			if (ret == 0)
				ret = -EAGAIN;
			break;
		}

		if (i915_seqno_passed(ring->get_seqno(ring, false), seqno)) {
			ret = 0;
			break;
		}

		if (interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		if (timeout_jiffies <= 0) {
			ret = -ETIME;
			break;
		}

		timer.function = NULL;
		if (timeout || missed_irq(dev_priv, ring)) {
			setup_timer_on_stack(&timer, fake_irq, (unsigned long)current);
			expire = jiffies + (missed_irq(dev_priv, ring) ? 1: timeout_jiffies);
			mod_timer(&timer, expire);
		}

		io_schedule();

		if (timeout)
			timeout_jiffies = expire - jiffies;

		if (timer.function) {
			del_singleshot_timer_sync(&timer);
			destroy_timer_on_stack(&timer);
		}
	}
	getrawmonotonic(&now);
	trace_i915_gem_request_wait_end(ring, seqno);

	ring->irq_put(ring);

	finish_wait(&ring->irq_queue, &wait);

	if (timeout) {
		struct timespec sleep_time = timespec_sub(now, before);
		*timeout = timespec_sub(*timeout, sleep_time);
		if (!timespec_valid(timeout)) /* i.e. negative time remains */
			set_normalized_timespec(timeout, 0, 0);
	}

	return ret;
}

/**
 * Waits for a sequence number to be signaled, and cleans up the
 * request and object lists appropriately for that event.
 */
int
i915_wait_seqno(struct intel_ring_buffer *ring, uint32_t seqno)
{
	struct drm_device *dev = ring->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible = dev_priv->mm.interruptible;
	int ret;

	BUG_ON(!mutex_is_locked(&dev->struct_mutex));
	BUG_ON(seqno == 0);

	ret = i915_gem_check_wedge(&dev_priv->gpu_error, interruptible);
	if (ret)
		return ret;

	ret = i915_gem_check_olr(ring, seqno);
	if (ret)
		return ret;

	return __wait_seqno(ring, seqno,
			    atomic_read(&dev_priv->gpu_error.reset_counter),
			    interruptible, NULL, NULL);
}

static int
i915_gem_object_wait_rendering__tail(struct drm_i915_gem_object *obj,
				     struct intel_ring_buffer *ring)
{
	i915_gem_retire_requests_ring(ring);

	/* Manually manage the write flush as we may have not yet
	 * retired the buffer.
	 *
	 * Note that the last_write_seqno is always the earlier of
	 * the two (read/write) seqno, so if we haved successfully waited,
	 * we know we have passed the last write.
	 */
	obj->last_write_seqno = 0;
	obj->base.write_domain &= ~I915_GEM_GPU_DOMAINS;

	return 0;
}

/**
 * Ensures that all rendering to the object has completed and the object is
 * safe to unbind from the GTT or access from the CPU.
 */
static __must_check int
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool readonly)
{
	struct intel_ring_buffer *ring = obj->ring;
	u32 seqno;
	int ret;

	seqno = readonly ? obj->last_write_seqno : obj->last_read_seqno;
	if (seqno == 0)
		return 0;

	ret = i915_wait_seqno(ring, seqno);
	if (ret)
		return ret;

	return i915_gem_object_wait_rendering__tail(obj, ring);
}

/* A nonblocking variant of the above wait. This is a highly dangerous routine
 * as the object state may change during this call.
 */
static __must_check int
i915_gem_object_wait_rendering__nonblocking(struct drm_i915_gem_object *obj,
					    struct drm_file *file,
					    bool readonly)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = obj->ring;
	unsigned reset_counter;
	u32 seqno;
	int ret;

	BUG_ON(!mutex_is_locked(&dev->struct_mutex));
	BUG_ON(!dev_priv->mm.interruptible);

	seqno = readonly ? obj->last_write_seqno : obj->last_read_seqno;
	if (seqno == 0)
		return 0;

	ret = i915_gem_check_wedge(&dev_priv->gpu_error, true);
	if (ret)
		return ret;

	ret = i915_gem_check_olr(ring, seqno);
	if (ret)
		return ret;

	reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);
	mutex_unlock(&dev->struct_mutex);
	ret = __wait_seqno(ring, seqno, reset_counter, true, NULL, file->driver_priv);
	mutex_lock(&dev->struct_mutex);
	if (ret)
		return ret;

	return i915_gem_object_wait_rendering__tail(obj, ring);
}

/**
 * Called when user space prepares to use an object with the CPU, either
 * through the mmap ioctl's mapping or a GTT mapping.
 */
int
i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_gem_set_domain *args = data;
	struct drm_i915_gem_object *obj;
	uint32_t read_domains = args->read_domains;
	uint32_t write_domain = args->write_domain;
	int ret;

	/* Only handle setting domains to types used by the CPU. */
	if (write_domain & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	if (read_domains & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	/* Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain != 0 && read_domains != write_domain)
		return -EINVAL;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Try to flush the object off the GPU without holding the lock.
	 * We will repeat the flush holding the lock in the normal manner
	 * to catch cases where we are gazumped.
	 */
	ret = i915_gem_object_wait_rendering__nonblocking(obj, file, !write_domain);
	if (ret)
		goto unref;

	if (read_domains & I915_GEM_DOMAIN_GTT) {
		ret = i915_gem_object_set_to_gtt_domain(obj, write_domain != 0);

		/* Silently promote "you're not bound, there was nothing to do"
		 * to success, since the client was just asking us to
		 * make sure everything was done.
		 */
		if (ret == -EINVAL)
			ret = 0;
	} else {
		ret = i915_gem_object_set_to_cpu_domain(obj, write_domain != 0);
	}

unref:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Called when user space has done writes to this buffer
 */
int
i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file)
{
	struct drm_i915_gem_sw_finish *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Pinned buffers may be scanout, so flush the cache */
	if (obj->pin_display)
		i915_gem_object_flush_cpu_write_domain(obj, true);

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * Maps the contents of an object, returning the address it is mapped
 * into.
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 */
int
i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_mmap *args = data;
	struct drm_gem_object *obj;
	unsigned long addr;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (obj == NULL)
		return -ENOENT;

	/* prime objects have no backing filp to GEM mmap
	 * pages from.
	 */
	if (!obj->filp) {
		drm_gem_object_unreference_unlocked(obj);
		return -EINVAL;
	}

	addr = vm_mmap(obj->filp, 0, args->size,
		       PROT_READ | PROT_WRITE, MAP_SHARED,
		       args->offset);
	drm_gem_object_unreference_unlocked(obj);
	if (IS_ERR((void *)addr))
		return addr;

	args->addr_ptr = (uint64_t) addr;

	return 0;
}

/**
 * i915_gem_fault - fault a page into the GTT
 * vma: VMA in question
 * vmf: fault info
 *
 * The fault handler is set up by drm_gem_mmap() when a object is GTT mapped
 * from userspace.  The fault handler takes care of binding the object to
 * the GTT (if needed), allocating and programming a fence register (again,
 * only if needed based on whether the old reg is still valid or the object
 * is tiled) and inserting a new PTE into the faulting process.
 *
 * Note that the faulting process may involve evicting existing objects
 * from the GTT and/or fence registers to make room.  So performance may
 * suffer if the GTT working set is large or there are few fence registers
 * left.
 */
int i915_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_i915_gem_object *obj = to_intel_bo(vma->vm_private_data);
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	pgoff_t page_offset;
	unsigned long pfn;
	int ret = 0;
	bool write = !!(vmf->flags & FAULT_FLAG_WRITE);

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto out;

	trace_i915_gem_object_fault(obj, page_offset, true, write);

	/* Access to snoopable pages through the GTT is incoherent. */
	if (obj->cache_level != I915_CACHE_NONE && !HAS_LLC(dev)) {
		ret = -EINVAL;
		goto unlock;
	}

	/* Now bind it into the GTT if needed */
	ret = i915_gem_obj_ggtt_pin(obj,  0, true, false);
	if (ret)
		goto unlock;

	ret = i915_gem_object_set_to_gtt_domain(obj, write);
	if (ret)
		goto unpin;

	ret = i915_gem_object_get_fence(obj);
	if (ret)
		goto unpin;

	obj->fault_mappable = true;

	pfn = dev_priv->gtt.mappable_base + i915_gem_obj_ggtt_offset(obj);
	pfn >>= PAGE_SHIFT;
	pfn += page_offset;

	/* Finally, remap it using the new GTT offset */
	ret = vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);
unpin:
	i915_gem_object_unpin(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
out:
	switch (ret) {
	case -EIO:
		/* If this -EIO is due to a gpu hang, give the reset code a
		 * chance to clean up the mess. Otherwise return the proper
		 * SIGBUS. */
		if (i915_terminally_wedged(&dev_priv->gpu_error))
			return VM_FAULT_SIGBUS;
	case -EAGAIN:
		/*
		 * EAGAIN means the gpu is hung and we'll wait for the error
		 * handler to reset everything when re-faulting in
		 * i915_mutex_lock_interruptible.
		 */
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	case -ENOSPC:
		return VM_FAULT_SIGBUS;
	default:
		WARN_ONCE(ret, "unhandled error in i915_gem_fault: %i\n", ret);
		return VM_FAULT_SIGBUS;
	}
}

/**
 * i915_gem_release_mmap - remove physical page mappings
 * @obj: obj in question
 *
 * Preserve the reservation of the mmapping with the DRM core code, but
 * relinquish ownership of the pages back to the system.
 *
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by i915_gem_fault().
 */
void
i915_gem_release_mmap(struct drm_i915_gem_object *obj)
{
	if (!obj->fault_mappable)
		return;

	drm_vma_node_unmap(&obj->base.vma_node, obj->base.dev->dev_mapping);
	obj->fault_mappable = false;
}

uint32_t
i915_gem_get_gtt_size(struct drm_device *dev, uint32_t size, int tiling_mode)
{
	uint32_t gtt_size;

	if (INTEL_INFO(dev)->gen >= 4 ||
	    tiling_mode == I915_TILING_NONE)
		return size;

	/* Previous chips need a power-of-two fence region when tiling */
	if (INTEL_INFO(dev)->gen == 3)
		gtt_size = 1024*1024;
	else
		gtt_size = 512*1024;

	while (gtt_size < size)
		gtt_size <<= 1;

	return gtt_size;
}

/**
 * i915_gem_get_gtt_alignment - return required GTT alignment for an object
 * @obj: object to check
 *
 * Return the required GTT alignment for an object, taking into account
 * potential fence register mapping.
 */
uint32_t
i915_gem_get_gtt_alignment(struct drm_device *dev, uint32_t size,
			   int tiling_mode, bool fenced)
{
	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (INTEL_INFO(dev)->gen >= 4 || (!fenced && IS_G33(dev)) ||
	    tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	return i915_gem_get_gtt_size(dev, size, tiling_mode);
}

static int i915_gem_object_create_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	int ret;

	if (drm_vma_node_has_offset(&obj->base.vma_node))
		return 0;

	dev_priv->mm.shrinker_no_lock_stealing = true;

	ret = drm_gem_create_mmap_offset(&obj->base);
	if (ret != -ENOSPC)
		goto out;

	/* Badly fragmented mmap space? The only way we can recover
	 * space is by destroying unwanted objects. We can't randomly release
	 * mmap_offsets as userspace expects them to be persistent for the
	 * lifetime of the objects. The closest we can is to release the
	 * offsets on purgeable objects by truncating it and marking it purged,
	 * which prevents userspace from ever using that object again.
	 */
	i915_gem_purge(dev_priv, obj->base.size >> PAGE_SHIFT);
	ret = drm_gem_create_mmap_offset(&obj->base);
	if (ret != -ENOSPC)
		goto out;

	i915_gem_shrink_all(dev_priv);
	ret = drm_gem_create_mmap_offset(&obj->base);
out:
	dev_priv->mm.shrinker_no_lock_stealing = false;

	return ret;
}

static void i915_gem_object_free_mmap_offset(struct drm_i915_gem_object *obj)
{
	drm_gem_free_mmap_offset(&obj->base);
}

int
i915_gem_mmap_gtt(struct drm_file *file,
		  struct drm_device *dev,
		  uint32_t handle,
		  uint64_t *offset)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->base.size > dev_priv->gtt.mappable_end) {
		ret = -E2BIG;
		goto out;
	}

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to mmap a purgeable buffer\n");
		ret = -EINVAL;
		goto out;
	}

	ret = i915_gem_object_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = drm_vma_node_offset_addr(&obj->base.vma_node);

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * i915_gem_mmap_gtt_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct drm_i915_gem_mmap_gtt *args = data;

	return i915_gem_mmap_gtt(file, dev, args->handle, &args->offset);
}

/* Immediately discard the backing storage */
static void
i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	struct inode *inode;

	i915_gem_object_free_mmap_offset(obj);

	if (obj->base.filp == NULL)
		return;

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	inode = file_inode(obj->base.filp);
	shmem_truncate_range(inode, 0, (loff_t)-1);

	obj->madv = __I915_MADV_PURGED;
}

static inline int
i915_gem_object_is_purgeable(struct drm_i915_gem_object *obj)
{
	return obj->madv == I915_MADV_DONTNEED;
}

static void
i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj)
{
	struct sg_page_iter sg_iter;
	int ret;

	BUG_ON(obj->madv == __I915_MADV_PURGED);

	ret = i915_gem_object_set_to_cpu_domain(obj, true);
	if (ret) {
		/* In the event of a disaster, abandon all caches and
		 * hope for the best.
		 */
		WARN_ON(ret != -EIO);
		i915_gem_clflush_object(obj, true);
		obj->base.read_domains = obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj);

	if (obj->madv == I915_MADV_DONTNEED)
		obj->dirty = 0;

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0) {
		struct page *page = sg_page_iter_page(&sg_iter);

		if (obj->dirty)
			set_page_dirty(page);

		if (obj->madv == I915_MADV_WILLNEED)
			mark_page_accessed(page);

		page_cache_release(page);
	}
	obj->dirty = 0;

	sg_free_table(obj->pages);
	kfree(obj->pages);
}

int
i915_gem_object_put_pages(struct drm_i915_gem_object *obj)
{
	const struct drm_i915_gem_object_ops *ops = obj->ops;

	if (obj->pages == NULL)
		return 0;

	if (obj->pages_pin_count)
		return -EBUSY;

	BUG_ON(i915_gem_obj_bound_any(obj));

	/* ->put_pages might need to allocate memory for the bit17 swizzle
	 * array, hence protect them from being reaped by removing them from gtt
	 * lists early. */
	list_del(&obj->global_list);

	ops->put_pages(obj);
	obj->pages = NULL;

	if (i915_gem_object_is_purgeable(obj))
		i915_gem_object_truncate(obj);

	return 0;
}

static unsigned long
__i915_gem_shrink(struct drm_i915_private *dev_priv, long target,
		  bool purgeable_only)
{
	struct list_head still_bound_list;
	struct drm_i915_gem_object *obj, *next;
	unsigned long count = 0;

	list_for_each_entry_safe(obj, next,
				 &dev_priv->mm.unbound_list,
				 global_list) {
		if ((i915_gem_object_is_purgeable(obj) || !purgeable_only) &&
		    i915_gem_object_put_pages(obj) == 0) {
			count += obj->base.size >> PAGE_SHIFT;
			if (count >= target)
				return count;
		}
	}

	/*
	 * As we may completely rewrite the bound list whilst unbinding
	 * (due to retiring requests) we have to strictly process only
	 * one element of the list at the time, and recheck the list
	 * on every iteration.
	 */
	INIT_LIST_HEAD(&still_bound_list);
	while (count < target && !list_empty(&dev_priv->mm.bound_list)) {
		struct i915_vma *vma, *v;

		obj = list_first_entry(&dev_priv->mm.bound_list,
				       typeof(*obj), global_list);
		list_move_tail(&obj->global_list, &still_bound_list);

		if (!i915_gem_object_is_purgeable(obj) && purgeable_only)
			continue;

		/*
		 * Hold a reference whilst we unbind this object, as we may
		 * end up waiting for and retiring requests. This might
		 * release the final reference (held by the active list)
		 * and result in the object being freed from under us.
		 * in this object being freed.
		 *
		 * Note 1: Shrinking the bound list is special since only active
		 * (and hence bound objects) can contain such limbo objects, so
		 * we don't need special tricks for shrinking the unbound list.
		 * The only other place where we have to be careful with active
		 * objects suddenly disappearing due to retiring requests is the
		 * eviction code.
		 *
		 * Note 2: Even though the bound list doesn't hold a reference
		 * to the object we can safely grab one here: The final object
		 * unreferencing and the bound_list are both protected by the
		 * dev->struct_mutex and so we won't ever be able to observe an
		 * object on the bound_list with a reference count equals 0.
		 */
		drm_gem_object_reference(&obj->base);

		list_for_each_entry_safe(vma, v, &obj->vma_list, vma_link)
			if (i915_vma_unbind(vma))
				break;

		if (i915_gem_object_put_pages(obj) == 0)
			count += obj->base.size >> PAGE_SHIFT;

		drm_gem_object_unreference(&obj->base);
	}
	list_splice(&still_bound_list, &dev_priv->mm.bound_list);

	return count;
}

static unsigned long
i915_gem_purge(struct drm_i915_private *dev_priv, long target)
{
	return __i915_gem_shrink(dev_priv, target, true);
}

static unsigned long
i915_gem_shrink_all(struct drm_i915_private *dev_priv)
{
	struct drm_i915_gem_object *obj, *next;
	long freed = 0;

	i915_gem_evict_everything(dev_priv->dev);

	list_for_each_entry_safe(obj, next, &dev_priv->mm.unbound_list,
				 global_list) {
		if (i915_gem_object_put_pages(obj) == 0)
			freed += obj->base.size >> PAGE_SHIFT;
	}
	return freed;
}

static int
i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	int page_count, i;
	struct address_space *mapping;
	struct sg_table *st;
	struct scatterlist *sg;
	struct sg_page_iter sg_iter;
	struct page *page;
	unsigned long last_pfn = 0;	/* suppress gcc warning */
	gfp_t gfp;

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	BUG_ON(obj->base.read_domains & I915_GEM_GPU_DOMAINS);
	BUG_ON(obj->base.write_domain & I915_GEM_GPU_DOMAINS);

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;

	page_count = obj->base.size / PAGE_SIZE;
	if (sg_alloc_table(st, page_count, GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping = file_inode(obj->base.filp)->i_mapping;
	gfp = mapping_gfp_mask(mapping);
	gfp |= __GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD;
	gfp &= ~(__GFP_IO | __GFP_WAIT);
	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		if (IS_ERR(page)) {
			i915_gem_purge(dev_priv, page_count);
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		}
		if (IS_ERR(page)) {
			/* We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 */
			gfp &= ~(__GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD);
			gfp |= __GFP_IO | __GFP_WAIT;

			i915_gem_shrink_all(dev_priv);
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
			if (IS_ERR(page))
				goto err_pages;

			gfp |= __GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD;
			gfp &= ~(__GFP_IO | __GFP_WAIT);
		}
#ifdef CONFIG_SWIOTLB
		if (swiotlb_nr_tbl()) {
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
			sg = sg_next(sg);
			continue;
		}
#endif
		if (!i || page_to_pfn(page) != last_pfn + 1) {
			if (i)
				sg = sg_next(sg);
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
		} else {
			sg->length += PAGE_SIZE;
		}
		last_pfn = page_to_pfn(page);

		/* Check that the i965g/gm workaround works. */
		WARN_ON((gfp & __GFP_DMA32) && (last_pfn >= 0x00100000UL));
	}
#ifdef CONFIG_SWIOTLB
	if (!swiotlb_nr_tbl())
#endif
		sg_mark_end(sg);
	obj->pages = st;

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj);

	return 0;

err_pages:
	sg_mark_end(sg);
	for_each_sg_page(st->sgl, &sg_iter, st->nents, 0)
		page_cache_release(sg_page_iter_page(&sg_iter));
	sg_free_table(st);
	kfree(st);
	return PTR_ERR(page);
}

/* Ensure that the associated pages are gathered from the backing storage
 * and pinned into our object. i915_gem_object_get_pages() may be called
 * multiple times before they are released by a single call to
 * i915_gem_object_put_pages() - once the pages are no longer referenced
 * either as a result of memory pressure (reaping pages under the shrinker)
 * or as the object is itself released.
 */
int
i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	const struct drm_i915_gem_object_ops *ops = obj->ops;
	int ret;

	if (obj->pages)
		return 0;

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to obtain a purgeable object\n");
		return -EINVAL;
	}

	BUG_ON(obj->pages_pin_count);

	ret = ops->get_pages(obj);
	if (ret)
		return ret;

	list_add_tail(&obj->global_list, &dev_priv->mm.unbound_list);
	return 0;
}

static void
i915_gem_object_move_to_active(struct drm_i915_gem_object *obj,
			       struct intel_ring_buffer *ring)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 seqno = intel_ring_get_seqno(ring);

	BUG_ON(ring == NULL);
	if (obj->ring != ring && obj->last_write_seqno) {
		/* Keep the seqno relative to the current ring */
		obj->last_write_seqno = seqno;
	}
	obj->ring = ring;

	/* Add a reference if we're newly entering the active list. */
	if (!obj->active) {
		drm_gem_object_reference(&obj->base);
		obj->active = 1;
	}

	list_move_tail(&obj->ring_list, &ring->active_list);

	obj->last_read_seqno = seqno;

	if (obj->fenced_gpu_access) {
		obj->last_fenced_seqno = seqno;

		/* Bump MRU to take account of the delayed flush */
		if (obj->fence_reg != I915_FENCE_REG_NONE) {
			struct drm_i915_fence_reg *reg;

			reg = &dev_priv->fence_regs[obj->fence_reg];
			list_move_tail(&reg->lru_list,
				       &dev_priv->mm.fence_list);
		}
	}
}

void i915_vma_move_to_active(struct i915_vma *vma,
			     struct intel_ring_buffer *ring)
{
	list_move_tail(&vma->mm_list, &vma->vm->active_list);
	return i915_gem_object_move_to_active(vma->obj, ring);
}

static void
i915_gem_object_move_to_inactive(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	struct i915_address_space *ggtt_vm = &dev_priv->gtt.base;
	struct i915_vma *vma = i915_gem_obj_to_vma(obj, ggtt_vm);

	BUG_ON(obj->base.write_domain & ~I915_GEM_GPU_DOMAINS);
	BUG_ON(!obj->active);

	list_move_tail(&vma->mm_list, &ggtt_vm->inactive_list);

	list_del_init(&obj->ring_list);
	obj->ring = NULL;

	obj->last_read_seqno = 0;
	obj->last_write_seqno = 0;
	obj->base.write_domain = 0;

	obj->last_fenced_seqno = 0;
	obj->fenced_gpu_access = false;

	obj->active = 0;
	drm_gem_object_unreference(&obj->base);

	WARN_ON(i915_verify_lists(dev));
}

static int
i915_gem_init_seqno(struct drm_device *dev, u32 seqno)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int ret, i, j;

	/* Carefully retire all requests without writing to the rings */
	for_each_ring(ring, dev_priv, i) {
		ret = intel_ring_idle(ring);
		if (ret)
			return ret;
	}
	i915_gem_retire_requests(dev);

	/* Finally reset hw state */
	for_each_ring(ring, dev_priv, i) {
		intel_ring_init_seqno(ring, seqno);

		for (j = 0; j < ARRAY_SIZE(ring->sync_seqno); j++)
			ring->sync_seqno[j] = 0;
	}

	return 0;
}

int i915_gem_set_seqno(struct drm_device *dev, u32 seqno)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (seqno == 0)
		return -EINVAL;

	/* HWS page needs to be set less than what we
	 * will inject to ring
	 */
	ret = i915_gem_init_seqno(dev, seqno - 1);
	if (ret)
		return ret;

	/* Carefully set the last_seqno value so that wrap
	 * detection still works
	 */
	dev_priv->next_seqno = seqno;
	dev_priv->last_seqno = seqno - 1;
	if (dev_priv->last_seqno == 0)
		dev_priv->last_seqno--;

	return 0;
}

int
i915_gem_get_seqno(struct drm_device *dev, u32 *seqno)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* reserve 0 for non-seqno */
	if (dev_priv->next_seqno == 0) {
		int ret = i915_gem_init_seqno(dev, 0);
		if (ret)
			return ret;

		dev_priv->next_seqno = 1;
	}

	*seqno = dev_priv->last_seqno = dev_priv->next_seqno++;
	return 0;
}

int __i915_add_request(struct intel_ring_buffer *ring,
		       struct drm_file *file,
		       struct drm_i915_gem_object *obj,
		       u32 *out_seqno)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_request *request;
	u32 request_ring_position, request_start;
	int was_empty;
	int ret;

	request_start = intel_ring_get_tail(ring);
	/*
	 * Emit any outstanding flushes - execbuf can fail to emit the flush
	 * after having emitted the batchbuffer command. Hence we need to fix
	 * things up similar to emitting the lazy request. The difference here
	 * is that the flush _must_ happen before the next request, no matter
	 * what.
	 */
	ret = intel_ring_flush_all_caches(ring);
	if (ret)
		return ret;

	request = ring->preallocated_lazy_request;
	if (WARN_ON(request == NULL))
		return -ENOMEM;

	/* Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	request_ring_position = intel_ring_get_tail(ring);

	ret = ring->add_request(ring);
	if (ret)
		return ret;

	request->seqno = intel_ring_get_seqno(ring);
	request->ring = ring;
	request->head = request_start;
	request->tail = request_ring_position;

	/* Whilst this request exists, batch_obj will be on the
	 * active_list, and so will hold the active reference. Only when this
	 * request is retired will the the batch_obj be moved onto the
	 * inactive_list and lose its active reference. Hence we do not need
	 * to explicitly hold another reference here.
	 */
	request->batch_obj = obj;

	/* Hold a reference to the current context so that we can inspect
	 * it later in case a hangcheck error event fires.
	 */
	request->ctx = ring->last_context;
	if (request->ctx)
		i915_gem_context_reference(request->ctx);

	request->emitted_jiffies = jiffies;
	was_empty = list_empty(&ring->request_list);
	list_add_tail(&request->list, &ring->request_list);
	request->file_priv = NULL;

	if (file) {
		struct drm_i915_file_private *file_priv = file->driver_priv;

		spin_lock(&file_priv->mm.lock);
		request->file_priv = file_priv;
		list_add_tail(&request->client_list,
			      &file_priv->mm.request_list);
		spin_unlock(&file_priv->mm.lock);
	}

	trace_i915_gem_request_add(ring, request->seqno);
	ring->outstanding_lazy_seqno = 0;
	ring->preallocated_lazy_request = NULL;

	if (!dev_priv->ums.mm_suspended) {
		i915_queue_hangcheck(ring->dev);

		if (was_empty) {
			cancel_delayed_work_sync(&dev_priv->mm.idle_work);
			queue_delayed_work(dev_priv->wq,
					   &dev_priv->mm.retire_work,
					   round_jiffies_up_relative(HZ));
			intel_mark_busy(dev_priv->dev);
		}
	}

	if (out_seqno)
		*out_seqno = request->seqno;
	return 0;
}

static inline void
i915_gem_request_remove_from_client(struct drm_i915_gem_request *request)
{
	struct drm_i915_file_private *file_priv = request->file_priv;

	if (!file_priv)
		return;

	spin_lock(&file_priv->mm.lock);
	list_del(&request->client_list);
	request->file_priv = NULL;
	spin_unlock(&file_priv->mm.lock);
}

static bool i915_head_inside_object(u32 acthd, struct drm_i915_gem_object *obj,
				    struct i915_address_space *vm)
{
	if (acthd >= i915_gem_obj_offset(obj, vm) &&
	    acthd < i915_gem_obj_offset(obj, vm) + obj->base.size)
		return true;

	return false;
}

static bool i915_head_inside_request(const u32 acthd_unmasked,
				     const u32 request_start,
				     const u32 request_end)
{
	const u32 acthd = acthd_unmasked & HEAD_ADDR;

	if (request_start < request_end) {
		if (acthd >= request_start && acthd < request_end)
			return true;
	} else if (request_start > request_end) {
		if (acthd >= request_start || acthd < request_end)
			return true;
	}

	return false;
}

static struct i915_address_space *
request_to_vm(struct drm_i915_gem_request *request)
{
	struct drm_i915_private *dev_priv = request->ring->dev->dev_private;
	struct i915_address_space *vm;

	vm = &dev_priv->gtt.base;

	return vm;
}

static bool i915_request_guilty(struct drm_i915_gem_request *request,
				const u32 acthd, bool *inside)
{
	/* There is a possibility that unmasked head address
	 * pointing inside the ring, matches the batch_obj address range.
	 * However this is extremely unlikely.
	 */
	if (request->batch_obj) {
		if (i915_head_inside_object(acthd, request->batch_obj,
					    request_to_vm(request))) {
			*inside = true;
			return true;
		}
	}

	if (i915_head_inside_request(acthd, request->head, request->tail)) {
		*inside = false;
		return true;
	}

	return false;
}

static bool i915_context_is_banned(const struct i915_ctx_hang_stats *hs)
{
	const unsigned long elapsed = get_seconds() - hs->guilty_ts;

	if (hs->banned)
		return true;

	if (elapsed <= DRM_I915_CTX_BAN_PERIOD) {
		DRM_ERROR("context hanging too fast, declaring banned!\n");
		return true;
	}

	return false;
}

static void i915_set_reset_status(struct intel_ring_buffer *ring,
				  struct drm_i915_gem_request *request,
				  u32 acthd)
{
	struct i915_ctx_hang_stats *hs = NULL;
	bool inside, guilty;
	unsigned long offset = 0;

	/* Innocent until proven guilty */
	guilty = false;

	if (request->batch_obj)
		offset = i915_gem_obj_offset(request->batch_obj,
					     request_to_vm(request));

	if (ring->hangcheck.action != HANGCHECK_WAIT &&
	    i915_request_guilty(request, acthd, &inside)) {
		DRM_ERROR("%s hung %s bo (0x%lx ctx %d) at 0x%x\n",
			  ring->name,
			  inside ? "inside" : "flushing",
			  offset,
			  request->ctx ? request->ctx->id : 0,
			  acthd);

		guilty = true;
	}

	/* If contexts are disabled or this is the default context, use
	 * file_priv->reset_state
	 */
	if (request->ctx && request->ctx->id != DEFAULT_CONTEXT_ID)
		hs = &request->ctx->hang_stats;
	else if (request->file_priv)
		hs = &request->file_priv->hang_stats;

	if (hs) {
		if (guilty) {
			hs->banned = i915_context_is_banned(hs);
			hs->batch_active++;
			hs->guilty_ts = get_seconds();
		} else {
			hs->batch_pending++;
		}
	}
}

static void i915_gem_free_request(struct drm_i915_gem_request *request)
{
	list_del(&request->list);
	i915_gem_request_remove_from_client(request);

	if (request->ctx)
		i915_gem_context_unreference(request->ctx);

	kfree(request);
}

static void i915_gem_reset_ring_status(struct drm_i915_private *dev_priv,
				       struct intel_ring_buffer *ring)
{
	u32 completed_seqno = ring->get_seqno(ring, false);
	u32 acthd = intel_ring_get_active_head(ring);
	struct drm_i915_gem_request *request;

	list_for_each_entry(request, &ring->request_list, list) {
		if (i915_seqno_passed(completed_seqno, request->seqno))
			continue;

		i915_set_reset_status(ring, request, acthd);
	}
}

static void i915_gem_reset_ring_cleanup(struct drm_i915_private *dev_priv,
					struct intel_ring_buffer *ring)
{
	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		i915_gem_free_request(request);
	}

	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&ring->active_list,
				       struct drm_i915_gem_object,
				       ring_list);

		i915_gem_object_move_to_inactive(obj);
	}
}

void i915_gem_restore_fences(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *reg = &dev_priv->fence_regs[i];

		/*
		 * Commit delayed tiling changes if we have an object still
		 * attached to the fence, otherwise just clear the fence.
		 */
		if (reg->obj) {
			i915_gem_object_update_fence(reg->obj, reg,
						     reg->obj->tiling_mode);
		} else {
			i915_gem_write_fence(dev, i, NULL);
		}
	}
}

void i915_gem_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int i;

	/*
	 * Before we free the objects from the requests, we need to inspect
	 * them for finding the guilty party. As the requests only borrow
	 * their reference to the objects, the inspection must be done first.
	 */
	for_each_ring(ring, dev_priv, i)
		i915_gem_reset_ring_status(dev_priv, ring);

	for_each_ring(ring, dev_priv, i)
		i915_gem_reset_ring_cleanup(dev_priv, ring);

	i915_gem_cleanup_ringbuffer(dev);

	i915_gem_restore_fences(dev);
}

/**
 * This function clears the request list as sequence numbers are passed.
 */
void
i915_gem_retire_requests_ring(struct intel_ring_buffer *ring)
{
	uint32_t seqno;

	if (list_empty(&ring->request_list))
		return;

	WARN_ON(i915_verify_lists(ring->dev));

	seqno = ring->get_seqno(ring, true);

	while (!list_empty(&ring->request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&ring->request_list,
					   struct drm_i915_gem_request,
					   list);

		if (!i915_seqno_passed(seqno, request->seqno))
			break;

		trace_i915_gem_request_retire(ring, request->seqno);
		/* We know the GPU must have read the request to have
		 * sent us the seqno + interrupt, so use the position
		 * of tail of the request to update the last known position
		 * of the GPU head.
		 */
		ring->last_retired_head = request->tail;

		i915_gem_free_request(request);
	}

	/* Move any buffers on the active list that are no longer referenced
	 * by the ringbuffer to the flushing/inactive lists as appropriate.
	 */
	while (!list_empty(&ring->active_list)) {
		struct drm_i915_gem_object *obj;

		obj = list_first_entry(&ring->active_list,
				      struct drm_i915_gem_object,
				      ring_list);

		if (!i915_seqno_passed(seqno, obj->last_read_seqno))
			break;

		i915_gem_object_move_to_inactive(obj);
	}

	if (unlikely(ring->trace_irq_seqno &&
		     i915_seqno_passed(seqno, ring->trace_irq_seqno))) {
		ring->irq_put(ring);
		ring->trace_irq_seqno = 0;
	}

	WARN_ON(i915_verify_lists(ring->dev));
}

bool
i915_gem_retire_requests(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	bool idle = true;
	int i;

	for_each_ring(ring, dev_priv, i) {
		i915_gem_retire_requests_ring(ring);
		idle &= list_empty(&ring->request_list);
	}

	if (idle)
		mod_delayed_work(dev_priv->wq,
				   &dev_priv->mm.idle_work,
				   msecs_to_jiffies(100));

	return idle;
}

static void
i915_gem_retire_work_handler(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), mm.retire_work.work);
	struct drm_device *dev = dev_priv->dev;
	bool idle;

	/* Come back later if the device is busy... */
	idle = false;
	if (mutex_trylock(&dev->struct_mutex)) {
		idle = i915_gem_retire_requests(dev);
		mutex_unlock(&dev->struct_mutex);
	}
	if (!idle)
		queue_delayed_work(dev_priv->wq, &dev_priv->mm.retire_work,
				   round_jiffies_up_relative(HZ));
}

static void
i915_gem_idle_work_handler(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), mm.idle_work.work);

	intel_mark_idle(dev_priv->dev);
}

/**
 * Ensures that an object will eventually get non-busy by flushing any required
 * write domains, emitting any outstanding lazy request and retiring and
 * completed requests.
 */
static int
i915_gem_object_flush_active(struct drm_i915_gem_object *obj)
{
	int ret;

	if (obj->active) {
		ret = i915_gem_check_olr(obj->ring, obj->last_read_seqno);
		if (ret)
			return ret;

		i915_gem_retire_requests_ring(obj->ring);
	}

	return 0;
}

/**
 * i915_gem_wait_ioctl - implements DRM_IOCTL_I915_GEM_WAIT
 * @DRM_IOCTL_ARGS: standard ioctl arguments
 *
 * Returns 0 if successful, else an error is returned with the remaining time in
 * the timeout parameter.
 *  -ETIME: object is still busy after timeout
 *  -ERESTARTSYS: signal interrupted the wait
 *  -ENONENT: object doesn't exist
 * Also possible, but rare:
 *  -EAGAIN: GPU wedged
 *  -ENOMEM: damn
 *  -ENODEV: Internal IRQ fail
 *  -E?: The add request failed
 *
 * The wait ioctl with a timeout of 0 reimplements the busy ioctl. With any
 * non-zero timeout parameter the wait ioctl will wait for the given number of
 * nanoseconds on an object becoming unbusy. Since the wait itself does so
 * without holding struct_mutex the object may become re-busied before this
 * function completes. A similar but shorter * race condition exists in the busy
 * ioctl
 */
int
i915_gem_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_wait *args = data;
	struct drm_i915_gem_object *obj;
	struct intel_ring_buffer *ring = NULL;
	struct timespec timeout_stack, *timeout = NULL;
	unsigned reset_counter;
	u32 seqno = 0;
	int ret = 0;

	if (args->timeout_ns >= 0) {
		timeout_stack = ns_to_timespec(args->timeout_ns);
		timeout = &timeout_stack;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->bo_handle));
	if (&obj->base == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return -ENOENT;
	}

	/* Need to make sure the object gets inactive eventually. */
	ret = i915_gem_object_flush_active(obj);
	if (ret)
		goto out;

	if (obj->active) {
		seqno = obj->last_read_seqno;
		ring = obj->ring;
	}

	if (seqno == 0)
		 goto out;

	/* Do this after OLR check to make sure we make forward progress polling
	 * on this IOCTL with a 0 timeout (like busy ioctl)
	 */
	if (!args->timeout_ns) {
		ret = -ETIME;
		goto out;
	}

	drm_gem_object_unreference(&obj->base);
	reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);
	mutex_unlock(&dev->struct_mutex);

	ret = __wait_seqno(ring, seqno, reset_counter, true, timeout, file->driver_priv);
	if (timeout)
		args->timeout_ns = timespec_to_ns(timeout);
	return ret;

out:
	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 * i915_gem_object_sync - sync an object to a ring.
 *
 * @obj: object which may be in use on another ring.
 * @to: ring we wish to use the object on. May be NULL.
 *
 * This code is meant to abstract object synchronization with the GPU.
 * Calling with NULL implies synchronizing the object with the CPU
 * rather than a particular GPU ring.
 *
 * Returns 0 if successful, else propagates up the lower layer error.
 */
int
i915_gem_object_sync(struct drm_i915_gem_object *obj,
		     struct intel_ring_buffer *to)
{
	struct intel_ring_buffer *from = obj->ring;
	u32 seqno;
	int ret, idx;

	if (from == NULL || to == from)
		return 0;

	if (to == NULL || !i915_semaphore_is_enabled(obj->base.dev))
		return i915_gem_object_wait_rendering(obj, false);

	idx = intel_ring_sync_index(from, to);

	seqno = obj->last_read_seqno;
	if (seqno <= from->sync_seqno[idx])
		return 0;

	ret = i915_gem_check_olr(obj->ring, seqno);
	if (ret)
		return ret;

	trace_i915_gem_ring_sync_to(from, to, seqno);
	ret = to->sync_to(to, from, seqno);
	if (!ret)
		/* We use last_read_seqno because sync_to()
		 * might have just caused seqno wrap under
		 * the radar.
		 */
		from->sync_seqno[idx] = obj->last_read_seqno;

	return ret;
}

static void i915_gem_object_finish_gtt(struct drm_i915_gem_object *obj)
{
	u32 old_write_domain, old_read_domains;

	/* Force a pagefault for domain tracking on next user access */
	i915_gem_release_mmap(obj);

	if ((obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0)
		return;

	/* Wait for any direct GTT access to complete */
	mb();

	old_read_domains = obj->base.read_domains;
	old_write_domain = obj->base.write_domain;

	obj->base.read_domains &= ~I915_GEM_DOMAIN_GTT;
	obj->base.write_domain &= ~I915_GEM_DOMAIN_GTT;

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);
}

int i915_vma_unbind(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;
	int ret;

	/* For now we only ever use 1 vma per object */
	WARN_ON(!list_is_singular(&obj->vma_list));

	if (list_empty(&vma->vma_link))
		return 0;

	if (!drm_mm_node_allocated(&vma->node)) {
		i915_gem_vma_destroy(vma);

		return 0;
	}

	if (obj->pin_count)
		return -EBUSY;

	BUG_ON(obj->pages == NULL);

	ret = i915_gem_object_finish_gpu(obj);
	if (ret)
		return ret;
	/* Continue on if we fail due to EIO, the GPU is hung so we
	 * should be safe and we need to cleanup or else we might
	 * cause memory corruption through use-after-free.
	 */

	i915_gem_object_finish_gtt(obj);

	/* release the fence reg _after_ flushing */
	ret = i915_gem_object_put_fence(obj);
	if (ret)
		return ret;

	trace_i915_vma_unbind(vma);

	if (obj->has_global_gtt_mapping)
		i915_gem_gtt_unbind_object(obj);
	if (obj->has_aliasing_ppgtt_mapping) {
		i915_ppgtt_unbind_object(dev_priv->mm.aliasing_ppgtt, obj);
		obj->has_aliasing_ppgtt_mapping = 0;
	}
	i915_gem_gtt_finish_object(obj);
	i915_gem_object_unpin_pages(obj);

	list_del(&vma->mm_list);
	/* Avoid an unnecessary call to unbind on rebind. */
	if (i915_is_ggtt(vma->vm))
		obj->map_and_fenceable = true;

	drm_mm_remove_node(&vma->node);

	i915_gem_vma_destroy(vma);

	/* Since the unbound list is global, only move to that list if
	 * no more VMAs exist. */
	if (list_empty(&obj->vma_list))
		list_move_tail(&obj->global_list, &dev_priv->mm.unbound_list);

	return 0;
}

/**
 * Unbinds an object from the global GTT aperture.
 */
int
i915_gem_object_ggtt_unbind(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	struct i915_address_space *ggtt = &dev_priv->gtt.base;

	if (!i915_gem_obj_ggtt_bound(obj))
		return 0;

	if (obj->pin_count)
		return -EBUSY;

	BUG_ON(obj->pages == NULL);

	return i915_vma_unbind(i915_gem_obj_to_vma(obj, ggtt));
}

int i915_gpu_idle(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int ret, i;

	/* Flush everything onto the inactive list. */
	for_each_ring(ring, dev_priv, i) {
		ret = i915_switch_context(ring, NULL, DEFAULT_CONTEXT_ID);
		if (ret)
			return ret;

		ret = intel_ring_idle(ring);
		if (ret)
			return ret;
	}

	return 0;
}

static void i965_write_fence_reg(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int fence_reg;
	int fence_pitch_shift;

	if (INTEL_INFO(dev)->gen >= 6) {
		fence_reg = FENCE_REG_SANDYBRIDGE_0;
		fence_pitch_shift = SANDYBRIDGE_FENCE_PITCH_SHIFT;
	} else {
		fence_reg = FENCE_REG_965_0;
		fence_pitch_shift = I965_FENCE_PITCH_SHIFT;
	}

	fence_reg += reg * 8;

	/* To w/a incoherency with non-atomic 64-bit register updates,
	 * we split the 64-bit update into two 32-bit writes. In order
	 * for a partial fence not to be evaluated between writes, we
	 * precede the update with write to turn off the fence register,
	 * and only enable the fence as the last step.
	 *
	 * For extra levels of paranoia, we make sure each step lands
	 * before applying the next step.
	 */
	I915_WRITE(fence_reg, 0);
	POSTING_READ(fence_reg);

	if (obj) {
		u32 size = i915_gem_obj_ggtt_size(obj);
		uint64_t val;

		val = (uint64_t)((i915_gem_obj_ggtt_offset(obj) + size - 4096) &
				 0xfffff000) << 32;
		val |= i915_gem_obj_ggtt_offset(obj) & 0xfffff000;
		val |= (uint64_t)((obj->stride / 128) - 1) << fence_pitch_shift;
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I965_FENCE_TILING_Y_SHIFT;
		val |= I965_FENCE_REG_VALID;

		I915_WRITE(fence_reg + 4, val >> 32);
		POSTING_READ(fence_reg + 4);

		I915_WRITE(fence_reg + 0, val);
		POSTING_READ(fence_reg);
	} else {
		I915_WRITE(fence_reg + 4, 0);
		POSTING_READ(fence_reg + 4);
	}
}

static void i915_write_fence_reg(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 val;

	if (obj) {
		u32 size = i915_gem_obj_ggtt_size(obj);
		int pitch_val;
		int tile_width;

		WARN((i915_gem_obj_ggtt_offset(obj) & ~I915_FENCE_START_MASK) ||
		     (size & -size) != size ||
		     (i915_gem_obj_ggtt_offset(obj) & (size - 1)),
		     "object 0x%08lx [fenceable? %d] not 1M or pot-size (0x%08x) aligned\n",
		     i915_gem_obj_ggtt_offset(obj), obj->map_and_fenceable, size);

		if (obj->tiling_mode == I915_TILING_Y && HAS_128_BYTE_Y_TILING(dev))
			tile_width = 128;
		else
			tile_width = 512;

		/* Note: pitch better be a power of two tile widths */
		pitch_val = obj->stride / tile_width;
		pitch_val = ffs(pitch_val) - 1;

		val = i915_gem_obj_ggtt_offset(obj);
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I830_FENCE_TILING_Y_SHIFT;
		val |= I915_FENCE_SIZE_BITS(size);
		val |= pitch_val << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	} else
		val = 0;

	if (reg < 8)
		reg = FENCE_REG_830_0 + reg * 4;
	else
		reg = FENCE_REG_945_8 + (reg - 8) * 4;

	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

static void i830_write_fence_reg(struct drm_device *dev, int reg,
				struct drm_i915_gem_object *obj)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t val;

	if (obj) {
		u32 size = i915_gem_obj_ggtt_size(obj);
		uint32_t pitch_val;

		WARN((i915_gem_obj_ggtt_offset(obj) & ~I830_FENCE_START_MASK) ||
		     (size & -size) != size ||
		     (i915_gem_obj_ggtt_offset(obj) & (size - 1)),
		     "object 0x%08lx not 512K or pot-size 0x%08x aligned\n",
		     i915_gem_obj_ggtt_offset(obj), size);

		pitch_val = obj->stride / 128;
		pitch_val = ffs(pitch_val) - 1;

		val = i915_gem_obj_ggtt_offset(obj);
		if (obj->tiling_mode == I915_TILING_Y)
			val |= 1 << I830_FENCE_TILING_Y_SHIFT;
		val |= I830_FENCE_SIZE_BITS(size);
		val |= pitch_val << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	} else
		val = 0;

	I915_WRITE(FENCE_REG_830_0 + reg * 4, val);
	POSTING_READ(FENCE_REG_830_0 + reg * 4);
}

inline static bool i915_gem_object_needs_mb(struct drm_i915_gem_object *obj)
{
	return obj && obj->base.read_domains & I915_GEM_DOMAIN_GTT;
}

static void i915_gem_write_fence(struct drm_device *dev, int reg,
				 struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Ensure that all CPU reads are completed before installing a fence
	 * and all writes before removing the fence.
	 */
	if (i915_gem_object_needs_mb(dev_priv->fence_regs[reg].obj))
		mb();

	WARN(obj && (!obj->stride || !obj->tiling_mode),
	     "bogus fence setup with stride: 0x%x, tiling mode: %i\n",
	     obj->stride, obj->tiling_mode);

	switch (INTEL_INFO(dev)->gen) {
	case 8:
	case 7:
	case 6:
	case 5:
	case 4: i965_write_fence_reg(dev, reg, obj); break;
	case 3: i915_write_fence_reg(dev, reg, obj); break;
	case 2: i830_write_fence_reg(dev, reg, obj); break;
	default: BUG();
	}

	/* And similarly be paranoid that no direct access to this region
	 * is reordered to before the fence is installed.
	 */
	if (i915_gem_object_needs_mb(obj))
		mb();
}

static inline int fence_number(struct drm_i915_private *dev_priv,
			       struct drm_i915_fence_reg *fence)
{
	return fence - dev_priv->fence_regs;
}

static void i915_gem_object_update_fence(struct drm_i915_gem_object *obj,
					 struct drm_i915_fence_reg *fence,
					 bool enable)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	int reg = fence_number(dev_priv, fence);

	i915_gem_write_fence(obj->base.dev, reg, enable ? obj : NULL);

	if (enable) {
		obj->fence_reg = reg;
		fence->obj = obj;
		list_move_tail(&fence->lru_list, &dev_priv->mm.fence_list);
	} else {
		obj->fence_reg = I915_FENCE_REG_NONE;
		fence->obj = NULL;
		list_del_init(&fence->lru_list);
	}
	obj->fence_dirty = false;
}

static int
i915_gem_object_wait_fence(struct drm_i915_gem_object *obj)
{
	if (obj->last_fenced_seqno) {
		int ret = i915_wait_seqno(obj->ring, obj->last_fenced_seqno);
		if (ret)
			return ret;

		obj->last_fenced_seqno = 0;
	}

	obj->fenced_gpu_access = false;
	return 0;
}

int
i915_gem_object_put_fence(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = obj->base.dev->dev_private;
	struct drm_i915_fence_reg *fence;
	int ret;

	ret = i915_gem_object_wait_fence(obj);
	if (ret)
		return ret;

	if (obj->fence_reg == I915_FENCE_REG_NONE)
		return 0;

	fence = &dev_priv->fence_regs[obj->fence_reg];

	i915_gem_object_fence_lost(obj);
	i915_gem_object_update_fence(obj, fence, false);

	return 0;
}

static struct drm_i915_fence_reg *
i915_find_fence_reg(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_fence_reg *reg, *avail;
	int i;

	/* First try to find a free reg */
	avail = NULL;
	for (i = dev_priv->fence_reg_start; i < dev_priv->num_fence_regs; i++) {
		reg = &dev_priv->fence_regs[i];
		if (!reg->obj)
			return reg;

		if (!reg->pin_count)
			avail = reg;
	}

	if (avail == NULL)
		return NULL;

	/* None available, try to steal one or wait for a user to finish */
	list_for_each_entry(reg, &dev_priv->mm.fence_list, lru_list) {
		if (reg->pin_count)
			continue;

		return reg;
	}

	return NULL;
}

/**
 * i915_gem_object_get_fence - set up fencing for an object
 * @obj: object to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 *
 * For an untiled surface, this removes any existing fence.
 */
int
i915_gem_object_get_fence(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool enable = obj->tiling_mode != I915_TILING_NONE;
	struct drm_i915_fence_reg *reg;
	int ret;

	/* Have we updated the tiling parameters upon the object and so
	 * will need to serialise the write to the associated fence register?
	 */
	if (obj->fence_dirty) {
		ret = i915_gem_object_wait_fence(obj);
		if (ret)
			return ret;
	}

	/* Just update our place in the LRU if our fence is getting reused. */
	if (obj->fence_reg != I915_FENCE_REG_NONE) {
		reg = &dev_priv->fence_regs[obj->fence_reg];
		if (!obj->fence_dirty) {
			list_move_tail(&reg->lru_list,
				       &dev_priv->mm.fence_list);
			return 0;
		}
	} else if (enable) {
		reg = i915_find_fence_reg(dev);
		if (reg == NULL)
			return -EDEADLK;

		if (reg->obj) {
			struct drm_i915_gem_object *old = reg->obj;

			ret = i915_gem_object_wait_fence(old);
			if (ret)
				return ret;

			i915_gem_object_fence_lost(old);
		}
	} else
		return 0;

	i915_gem_object_update_fence(obj, reg, enable);

	return 0;
}

static bool i915_gem_valid_gtt_space(struct drm_device *dev,
				     struct drm_mm_node *gtt_space,
				     unsigned long cache_level)
{
	struct drm_mm_node *other;

	/* On non-LLC machines we have to be careful when putting differing
	 * types of snoopable memory together to avoid the prefetcher
	 * crossing memory domains and dying.
	 */
	if (HAS_LLC(dev))
		return true;

	if (!drm_mm_node_allocated(gtt_space))
		return true;

	if (list_empty(&gtt_space->node_list))
		return true;

	other = list_entry(gtt_space->node_list.prev, struct drm_mm_node, node_list);
	if (other->allocated && !other->hole_follows && other->color != cache_level)
		return false;

	other = list_entry(gtt_space->node_list.next, struct drm_mm_node, node_list);
	if (other->allocated && !gtt_space->hole_follows && other->color != cache_level)
		return false;

	return true;
}

static void i915_gem_verify_gtt(struct drm_device *dev)
{
#if WATCH_GTT
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int err = 0;

	list_for_each_entry(obj, &dev_priv->mm.gtt_list, global_list) {
		if (obj->gtt_space == NULL) {
			printk(KERN_ERR "object found on GTT list with no space reserved\n");
			err++;
			continue;
		}

		if (obj->cache_level != obj->gtt_space->color) {
			printk(KERN_ERR "object reserved space [%08lx, %08lx] with wrong color, cache_level=%x, color=%lx\n",
			       i915_gem_obj_ggtt_offset(obj),
			       i915_gem_obj_ggtt_offset(obj) + i915_gem_obj_ggtt_size(obj),
			       obj->cache_level,
			       obj->gtt_space->color);
			err++;
			continue;
		}

		if (!i915_gem_valid_gtt_space(dev,
					      obj->gtt_space,
					      obj->cache_level)) {
			printk(KERN_ERR "invalid GTT space found at [%08lx, %08lx] - color=%x\n",
			       i915_gem_obj_ggtt_offset(obj),
			       i915_gem_obj_ggtt_offset(obj) + i915_gem_obj_ggtt_size(obj),
			       obj->cache_level);
			err++;
			continue;
		}
	}

	WARN_ON(err);
#endif
}

/**
 * Finds free space in the GTT aperture and binds the object there.
 */
static int
i915_gem_object_bind_to_vm(struct drm_i915_gem_object *obj,
			   struct i915_address_space *vm,
			   unsigned alignment,
			   bool map_and_fenceable,
			   bool nonblocking)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 size, fence_size, fence_alignment, unfenced_alignment;
	size_t gtt_max =
		map_and_fenceable ? dev_priv->gtt.mappable_end : vm->total;
	struct i915_vma *vma;
	int ret;

	fence_size = i915_gem_get_gtt_size(dev,
					   obj->base.size,
					   obj->tiling_mode);
	fence_alignment = i915_gem_get_gtt_alignment(dev,
						     obj->base.size,
						     obj->tiling_mode, true);
	unfenced_alignment =
		i915_gem_get_gtt_alignment(dev,
						    obj->base.size,
						    obj->tiling_mode, false);

	if (alignment == 0)
		alignment = map_and_fenceable ? fence_alignment :
						unfenced_alignment;
	if (map_and_fenceable && alignment & (fence_alignment - 1)) {
		DRM_ERROR("Invalid object alignment requested %u\n", alignment);
		return -EINVAL;
	}

	size = map_and_fenceable ? fence_size : obj->base.size;

	/* If the object is bigger than the entire aperture, reject it early
	 * before evicting everything in a vain attempt to find space.
	 */
	if (obj->base.size > gtt_max) {
		DRM_ERROR("Attempting to bind an object larger than the aperture: object=%zd > %s aperture=%zu\n",
			  obj->base.size,
			  map_and_fenceable ? "mappable" : "total",
			  gtt_max);
		return -E2BIG;
	}

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	BUG_ON(!i915_is_ggtt(vm));

	vma = i915_gem_obj_lookup_or_create_vma(obj, vm);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unpin;
	}

	/* For now we only ever use 1 vma per object */
	WARN_ON(!list_is_singular(&obj->vma_list));

search_free:
	ret = drm_mm_insert_node_in_range_generic(&vm->mm, &vma->node,
						  size, alignment,
						  obj->cache_level, 0, gtt_max,
						  DRM_MM_SEARCH_DEFAULT);
	if (ret) {
		ret = i915_gem_evict_something(dev, vm, size, alignment,
					       obj->cache_level,
					       map_and_fenceable,
					       nonblocking);
		if (ret == 0)
			goto search_free;

		goto err_free_vma;
	}
	if (WARN_ON(!i915_gem_valid_gtt_space(dev, &vma->node,
					      obj->cache_level))) {
		ret = -EINVAL;
		goto err_remove_node;
	}

	ret = i915_gem_gtt_prepare_object(obj);
	if (ret)
		goto err_remove_node;

	list_move_tail(&obj->global_list, &dev_priv->mm.bound_list);
	list_add_tail(&vma->mm_list, &vm->inactive_list);

	if (i915_is_ggtt(vm)) {
		bool mappable, fenceable;

		fenceable = (vma->node.size == fence_size &&
			     (vma->node.start & (fence_alignment - 1)) == 0);

		mappable = (vma->node.start + obj->base.size <=
			    dev_priv->gtt.mappable_end);

		obj->map_and_fenceable = mappable && fenceable;
	}

	WARN_ON(map_and_fenceable && !obj->map_and_fenceable);

	trace_i915_vma_bind(vma, map_and_fenceable);
	i915_gem_verify_gtt(dev);
	return 0;

err_remove_node:
	drm_mm_remove_node(&vma->node);
err_free_vma:
	i915_gem_vma_destroy(vma);
err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

bool
i915_gem_clflush_object(struct drm_i915_gem_object *obj,
			bool force)
{
	/* If we don't have a page list set up, then we're not pinned
	 * to GPU, and we can ignore the cache flush because it'll happen
	 * again at bind time.
	 */
	if (obj->pages == NULL)
		return false;

	/*
	 * Stolen memory is always coherent with the GPU as it is explicitly
	 * marked as wc by the system, or the system is cache-coherent.
	 */
	if (obj->stolen)
		return false;

	/* If the GPU is snooping the contents of the CPU cache,
	 * we do not need to manually clear the CPU cache lines.  However,
	 * the caches are only snooped when the render cache is
	 * flushed/invalidated.  As we always have to emit invalidations
	 * and flushes when moving into and out of the RENDER domain, correct
	 * snooping behaviour occurs naturally as the result of our domain
	 * tracking.
	 */
	if (!force && cpu_cache_is_coherent(obj->base.dev, obj->cache_level))
		return false;

	trace_i915_gem_object_clflush(obj);
	drm_clflush_sg(obj->pages);

	return true;
}

/** Flushes the GTT write domain for the object if it's dirty. */
static void
i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_GTT)
		return;

	/* No actual flushing is required for the GTT write domain.  Writes
	 * to it immediately go to main memory as far as we know, so there's
	 * no chipset flush.  It also doesn't land in render cache.
	 *
	 * However, we do have to enforce the order so that all writes through
	 * the GTT land before any writes to the device, such as updates to
	 * the GATT itself.
	 */
	wmb();

	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
}

/** Flushes the CPU write domain for the object if it's dirty. */
static void
i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj,
				       bool force)
{
	uint32_t old_write_domain;

	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU)
		return;

	if (i915_gem_clflush_object(obj, force))
		i915_gem_chipset_flush(obj->base.dev);

	old_write_domain = obj->base.write_domain;
	obj->base.write_domain = 0;

	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    old_write_domain);
}

/**
 * Moves a single object to the GTT read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj, bool write)
{
	drm_i915_private_t *dev_priv = obj->base.dev->dev_private;
	uint32_t old_write_domain, old_read_domains;
	int ret;

	/* Not valid to be called on unbound objects. */
	if (!i915_gem_obj_bound_any(obj))
		return -EINVAL;

	if (obj->base.write_domain == I915_GEM_DOMAIN_GTT)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	i915_gem_object_flush_cpu_write_domain(obj, false);

	/* Serialise direct access to this object with the barriers for
	 * coherent writes from the GPU, by effectively invalidating the
	 * GTT domain upon first access.
	 */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0)
		mb();

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_GTT;
		obj->base.write_domain = I915_GEM_DOMAIN_GTT;
		obj->dirty = 1;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	/* And bump the LRU for this access */
	if (i915_gem_object_is_inactive(obj)) {
		struct i915_vma *vma = i915_gem_obj_to_ggtt(obj);
		if (vma)
			list_move_tail(&vma->mm_list,
				       &dev_priv->gtt.base.inactive_list);

	}

	return 0;
}

int i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct i915_vma *vma;
	int ret;

	if (obj->cache_level == cache_level)
		return 0;

	if (obj->pin_count) {
		DRM_DEBUG("can not change the cache level of pinned objects\n");
		return -EBUSY;
	}

	list_for_each_entry(vma, &obj->vma_list, vma_link) {
		if (!i915_gem_valid_gtt_space(dev, &vma->node, cache_level)) {
			ret = i915_vma_unbind(vma);
			if (ret)
				return ret;

			break;
		}
	}

	if (i915_gem_obj_bound_any(obj)) {
		ret = i915_gem_object_finish_gpu(obj);
		if (ret)
			return ret;

		i915_gem_object_finish_gtt(obj);

		/* Before SandyBridge, you could not use tiling or fence
		 * registers with snooped memory, so relinquish any fences
		 * currently pointing to our region in the aperture.
		 */
		if (INTEL_INFO(dev)->gen < 6) {
			ret = i915_gem_object_put_fence(obj);
			if (ret)
				return ret;
		}

		if (obj->has_global_gtt_mapping)
			i915_gem_gtt_bind_object(obj, cache_level);
		if (obj->has_aliasing_ppgtt_mapping)
			i915_ppgtt_bind_object(dev_priv->mm.aliasing_ppgtt,
					       obj, cache_level);
	}

	list_for_each_entry(vma, &obj->vma_list, vma_link)
		vma->node.color = cache_level;
	obj->cache_level = cache_level;

	if (cpu_write_needs_clflush(obj)) {
		u32 old_read_domains, old_write_domain;

		/* If we're coming from LLC cached, then we haven't
		 * actually been tracking whether the data is in the
		 * CPU cache or not, since we only allow one bit set
		 * in obj->write_domain and have been skipping the clflushes.
		 * Just set it to the CPU cache for now.
		 */
		WARN_ON(obj->base.write_domain & ~I915_GEM_DOMAIN_CPU);

		old_read_domains = obj->base.read_domains;
		old_write_domain = obj->base.write_domain;

		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;

		trace_i915_gem_object_change_domain(obj,
						    old_read_domains,
						    old_write_domain);
	}

	i915_gem_verify_gtt(dev);
	return 0;
}

int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	switch (obj->cache_level) {
	case I915_CACHE_LLC:
	case I915_CACHE_L3_LLC:
		args->caching = I915_CACHING_CACHED;
		break;

	case I915_CACHE_WT:
		args->caching = I915_CACHING_DISPLAY;
		break;

	default:
		args->caching = I915_CACHING_NONE;
		break;
	}

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	enum i915_cache_level level;
	int ret;

	switch (args->caching) {
	case I915_CACHING_NONE:
		level = I915_CACHE_NONE;
		break;
	case I915_CACHING_CACHED:
		level = I915_CACHE_LLC;
		break;
	case I915_CACHING_DISPLAY:
		level = HAS_WT(dev) ? I915_CACHE_WT : I915_CACHE_NONE;
		break;
	default:
		return -EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = i915_gem_object_set_cache_level(obj, level);

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static bool is_pin_display(struct drm_i915_gem_object *obj)
{
	/* There are 3 sources that pin objects:
	 *   1. The display engine (scanouts, sprites, cursors);
	 *   2. Reservations for execbuffer;
	 *   3. The user.
	 *
	 * We can ignore reservations as we hold the struct_mutex and
	 * are only called outside of the reservation path.  The user
	 * can only increment pin_count once, and so if after
	 * subtracting the potential reference by the user, any pin_count
	 * remains, it must be due to another use by the display engine.
	 */
	return obj->pin_count - !!obj->user_pin_count;
}

/*
 * Prepare buffer for display plane (scanout, cursors, etc).
 * Can be called from an uninterruptible phase (modesetting) and allows
 * any flushes to be pipelined (for pageflips).
 */
int
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     u32 alignment,
				     struct intel_ring_buffer *pipelined)
{
	u32 old_read_domains, old_write_domain;
	int ret;

	if (pipelined != obj->ring) {
		ret = i915_gem_object_sync(obj, pipelined);
		if (ret)
			return ret;
	}

	/* Mark the pin_display early so that we account for the
	 * display coherency whilst setting up the cache domains.
	 */
	obj->pin_display = true;

	/* The display engine is not coherent with the LLC cache on gen6.  As
	 * a result, we make sure that the pinning that is about to occur is
	 * done with uncached PTEs. This is lowest common denominator for all
	 * chipsets.
	 *
	 * However for gen6+, we could do better by using the GFDT bit instead
	 * of uncaching, which would allow us to flush all the LLC-cached data
	 * with that bit in the PTE to main memory with just one PIPE_CONTROL.
	 */
	ret = i915_gem_object_set_cache_level(obj,
					      HAS_WT(obj->base.dev) ? I915_CACHE_WT : I915_CACHE_NONE);
	if (ret)
		goto err_unpin_display;

	/* As the user may map the buffer once pinned in the display plane
	 * (e.g. libkms for the bootup splash), we have to ensure that we
	 * always use map_and_fenceable for all scanout buffers.
	 */
	ret = i915_gem_obj_ggtt_pin(obj, alignment, true, false);
	if (ret)
		goto err_unpin_display;

	i915_gem_object_flush_cpu_write_domain(obj, true);

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	obj->base.write_domain = 0;
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return 0;

err_unpin_display:
	obj->pin_display = is_pin_display(obj);
	return ret;
}

void
i915_gem_object_unpin_from_display_plane(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin(obj);
	obj->pin_display = is_pin_display(obj);
}

int
i915_gem_object_finish_gpu(struct drm_i915_gem_object *obj)
{
	int ret;

	if ((obj->base.read_domains & I915_GEM_GPU_DOMAINS) == 0)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, false);
	if (ret)
		return ret;

	/* Ensure that we invalidate the GPU's caches and TLBs. */
	obj->base.read_domains &= ~I915_GEM_GPU_DOMAINS;
	return 0;
}

/**
 * Moves a single object to the CPU read, and possibly write domain.
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write)
{
	uint32_t old_write_domain, old_read_domains;
	int ret;

	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU)
		return 0;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	i915_gem_object_flush_gtt_write_domain(obj);

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj, false);

		obj->base.read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return 0;
}

/* Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
static int
i915_gem_ring_throttle(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	unsigned long recent_enough = jiffies - msecs_to_jiffies(20);
	struct drm_i915_gem_request *request;
	struct intel_ring_buffer *ring = NULL;
	unsigned reset_counter;
	u32 seqno = 0;
	int ret;

	ret = i915_gem_wait_for_error(&dev_priv->gpu_error);
	if (ret)
		return ret;

	ret = i915_gem_check_wedge(&dev_priv->gpu_error, false);
	if (ret)
		return ret;

	spin_lock(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_list) {
		if (time_after_eq(request->emitted_jiffies, recent_enough))
			break;

		ring = request->ring;
		seqno = request->seqno;
	}
	reset_counter = atomic_read(&dev_priv->gpu_error.reset_counter);
	spin_unlock(&file_priv->mm.lock);

	if (seqno == 0)
		return 0;

	ret = __wait_seqno(ring, seqno, reset_counter, true, NULL, NULL);
	if (ret == 0)
		queue_delayed_work(dev_priv->wq, &dev_priv->mm.retire_work, 0);

	return ret;
}

int
i915_gem_object_pin(struct drm_i915_gem_object *obj,
		    struct i915_address_space *vm,
		    uint32_t alignment,
		    bool map_and_fenceable,
		    bool nonblocking)
{
	struct i915_vma *vma;
	int ret;

	if (WARN_ON(obj->pin_count == DRM_I915_GEM_OBJECT_MAX_PIN_COUNT))
		return -EBUSY;

	WARN_ON(map_and_fenceable && !i915_is_ggtt(vm));

	vma = i915_gem_obj_to_vma(obj, vm);

	if (vma) {
		if ((alignment &&
		     vma->node.start & (alignment - 1)) ||
		    (map_and_fenceable && !obj->map_and_fenceable)) {
			WARN(obj->pin_count,
			     "bo is already pinned with incorrect alignment:"
			     " offset=%lx, req.alignment=%x, req.map_and_fenceable=%d,"
			     " obj->map_and_fenceable=%d\n",
			     i915_gem_obj_offset(obj, vm), alignment,
			     map_and_fenceable,
			     obj->map_and_fenceable);
			ret = i915_vma_unbind(vma);
			if (ret)
				return ret;
		}
	}

	if (!i915_gem_obj_bound(obj, vm)) {
		struct drm_i915_private *dev_priv = obj->base.dev->dev_private;

		ret = i915_gem_object_bind_to_vm(obj, vm, alignment,
						 map_and_fenceable,
						 nonblocking);
		if (ret)
			return ret;

		if (!dev_priv->mm.aliasing_ppgtt)
			i915_gem_gtt_bind_object(obj, obj->cache_level);
	}

	if (!obj->has_global_gtt_mapping && map_and_fenceable)
		i915_gem_gtt_bind_object(obj, obj->cache_level);

	obj->pin_count++;
	obj->pin_mappable |= map_and_fenceable;

	return 0;
}

void
i915_gem_object_unpin(struct drm_i915_gem_object *obj)
{
	BUG_ON(obj->pin_count == 0);
	BUG_ON(!i915_gem_obj_bound_any(obj));

	if (--obj->pin_count == 0)
		obj->pin_mappable = false;
}

int
i915_gem_pin_ioctl(struct drm_device *dev, void *data,
		   struct drm_file *file)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_ERROR("Attempting to pin a purgeable buffer\n");
		ret = -EINVAL;
		goto out;
	}

	if (obj->pin_filp != NULL && obj->pin_filp != file) {
		DRM_ERROR("Already pinned in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		ret = -EINVAL;
		goto out;
	}

	if (obj->user_pin_count == ULONG_MAX) {
		ret = -EBUSY;
		goto out;
	}

	if (obj->user_pin_count == 0) {
		ret = i915_gem_obj_ggtt_pin(obj, args->alignment, true, false);
		if (ret)
			goto out;
	}

	obj->user_pin_count++;
	obj->pin_filp = file;

	args->offset = i915_gem_obj_ggtt_offset(obj);
out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_unpin_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_pin *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->pin_filp != file) {
		DRM_ERROR("Not pinned by caller in i915_gem_pin_ioctl(): %d\n",
			  args->handle);
		ret = -EINVAL;
		goto out;
	}
	obj->user_pin_count--;
	if (obj->user_pin_count == 0) {
		obj->pin_filp = NULL;
		i915_gem_object_unpin(obj);
	}

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_busy_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	/* Count all active objects as busy, even if they are currently not used
	 * by the gpu. Users of this interface expect objects to eventually
	 * become non-busy without any further actions, therefore emit any
	 * necessary flushes here.
	 */
	ret = i915_gem_object_flush_active(obj);

	args->busy = obj->active;
	if (obj->ring) {
		BUILD_BUG_ON(I915_NUM_RINGS > 16);
		args->busy |= intel_ring_flag(obj->ring) << 16;
	}

	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	return i915_gem_ring_throttle(dev, file_priv);
}

int
i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_i915_gem_madvise *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	switch (args->madv) {
	case I915_MADV_DONTNEED:
	case I915_MADV_WILLNEED:
	    break;
	default:
	    return -EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file_priv, args->handle));
	if (&obj->base == NULL) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->pin_count) {
		ret = -EINVAL;
		goto out;
	}

	if (obj->madv != __I915_MADV_PURGED)
		obj->madv = args->madv;

	/* if the object is no longer attached, discard its backing storage */
	if (i915_gem_object_is_purgeable(obj) && obj->pages == NULL)
		i915_gem_object_truncate(obj);

	args->retained = obj->madv != __I915_MADV_PURGED;

out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops)
{
	INIT_LIST_HEAD(&obj->global_list);
	INIT_LIST_HEAD(&obj->ring_list);
	INIT_LIST_HEAD(&obj->obj_exec_link);
	INIT_LIST_HEAD(&obj->vma_list);

	obj->ops = ops;

	obj->fence_reg = I915_FENCE_REG_NONE;
	obj->madv = I915_MADV_WILLNEED;
	/* Avoid an unnecessary call to unbind on the first bind. */
	obj->map_and_fenceable = true;

	i915_gem_info_add_obj(obj->base.dev->dev_private, obj->base.size);
}

static const struct drm_i915_gem_object_ops i915_gem_object_ops = {
	.get_pages = i915_gem_object_get_pages_gtt,
	.put_pages = i915_gem_object_put_pages_gtt,
};

struct drm_i915_gem_object *i915_gem_alloc_object(struct drm_device *dev,
						  size_t size)
{
	struct drm_i915_gem_object *obj;
	struct address_space *mapping;
	gfp_t mask;

	obj = i915_gem_object_alloc(dev);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->base, size) != 0) {
		i915_gem_object_free(obj);
		return NULL;
	}

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_CRESTLINE(dev) || IS_BROADWATER(dev)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

	mapping = file_inode(obj->base.filp)->i_mapping;
	mapping_set_gfp_mask(mapping, mask);

	i915_gem_object_init(obj, &i915_gem_object_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	if (HAS_LLC(dev)) {
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		obj->cache_level = I915_CACHE_LLC;
	} else
		obj->cache_level = I915_CACHE_NONE;

	trace_i915_gem_object_create(obj);

	return obj;
}

void i915_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_device *dev = obj->base.dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct i915_vma *vma, *next;

	trace_i915_gem_object_destroy(obj);

	if (obj->phys_obj)
		i915_gem_detach_phys_object(dev, obj);

	obj->pin_count = 0;
	/* NB: 0 or 1 elements */
	WARN_ON(!list_empty(&obj->vma_list) &&
		!list_is_singular(&obj->vma_list));
	list_for_each_entry_safe(vma, next, &obj->vma_list, vma_link) {
		int ret = i915_vma_unbind(vma);
		if (WARN_ON(ret == -ERESTARTSYS)) {
			bool was_interruptible;

			was_interruptible = dev_priv->mm.interruptible;
			dev_priv->mm.interruptible = false;

			WARN_ON(i915_vma_unbind(vma));

			dev_priv->mm.interruptible = was_interruptible;
		}
	}

	/* Stolen objects don't hold a ref, but do hold pin count. Fix that up
	 * before progressing. */
	if (obj->stolen)
		i915_gem_object_unpin_pages(obj);

	if (WARN_ON(obj->pages_pin_count))
		obj->pages_pin_count = 0;
	i915_gem_object_put_pages(obj);
	i915_gem_object_free_mmap_offset(obj);
	i915_gem_object_release_stolen(obj);

	BUG_ON(obj->pages);

	if (obj->base.import_attach)
		drm_prime_gem_destroy(&obj->base, NULL);

	drm_gem_object_release(&obj->base);
	i915_gem_info_remove_obj(dev_priv, obj->base.size);

	kfree(obj->bit_17);
	i915_gem_object_free(obj);
}

struct i915_vma *i915_gem_obj_to_vma(struct drm_i915_gem_object *obj,
				     struct i915_address_space *vm)
{
	struct i915_vma *vma;
	list_for_each_entry(vma, &obj->vma_list, vma_link)
		if (vma->vm == vm)
			return vma;

	return NULL;
}

static struct i915_vma *__i915_gem_vma_create(struct drm_i915_gem_object *obj,
					      struct i915_address_space *vm)
{
	struct i915_vma *vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (vma == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&vma->vma_link);
	INIT_LIST_HEAD(&vma->mm_list);
	INIT_LIST_HEAD(&vma->exec_list);
	vma->vm = vm;
	vma->obj = obj;

	/* Keep GGTT vmas first to make debug easier */
	if (i915_is_ggtt(vm))
		list_add(&vma->vma_link, &obj->vma_list);
	else
		list_add_tail(&vma->vma_link, &obj->vma_list);

	return vma;
}

struct i915_vma *
i915_gem_obj_lookup_or_create_vma(struct drm_i915_gem_object *obj,
				  struct i915_address_space *vm)
{
	struct i915_vma *vma;

	vma = i915_gem_obj_to_vma(obj, vm);
	if (!vma)
		vma = __i915_gem_vma_create(obj, vm);

	return vma;
}

void i915_gem_vma_destroy(struct i915_vma *vma)
{
	WARN_ON(vma->node.allocated);

	/* Keep the vma as a placeholder in the execbuffer reservation lists */
	if (!list_empty(&vma->exec_list))
		return;

	list_del(&vma->vma_link);

	kfree(vma);
}

int
i915_gem_suspend(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->ums.mm_suspended)
		goto err;

	ret = i915_gpu_idle(dev);
	if (ret)
		goto err;

	i915_gem_retire_requests(dev);

	/* Under UMS, be paranoid and evict. */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		i915_gem_evict_everything(dev);

	i915_kernel_lost_context(dev);
	i915_gem_cleanup_ringbuffer(dev);

	/* Hack!  Don't let anybody do execbuf while we don't control the chip.
	 * We need to replace this with a semaphore, or something.
	 * And not confound ums.mm_suspended!
	 */
	dev_priv->ums.mm_suspended = !drm_core_check_feature(dev,
							     DRIVER_MODESET);
	mutex_unlock(&dev->struct_mutex);

	del_timer_sync(&dev_priv->gpu_error.hangcheck_timer);
	cancel_delayed_work_sync(&dev_priv->mm.retire_work);
	cancel_delayed_work_sync(&dev_priv->mm.idle_work);

	return 0;

err:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int i915_gem_l3_remap(struct intel_ring_buffer *ring, int slice)
{
	struct drm_device *dev = ring->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 reg_base = GEN7_L3LOG_BASE + (slice * 0x200);
	u32 *remap_info = dev_priv->l3_parity.remap_info[slice];
	int i, ret;

	if (!HAS_L3_DPF(dev) || !remap_info)
		return 0;

	ret = intel_ring_begin(ring, GEN7_L3LOG_SIZE / 4 * 3);
	if (ret)
		return ret;

	/*
	 * Note: We do not worry about the concurrent register cacheline hang
	 * here because no other code should access these registers other than
	 * at initialization time.
	 */
	for (i = 0; i < GEN7_L3LOG_SIZE; i += 4) {
		intel_ring_emit(ring, MI_LOAD_REGISTER_IMM(1));
		intel_ring_emit(ring, reg_base + i);
		intel_ring_emit(ring, remap_info[i/4]);
	}

	intel_ring_advance(ring);

	return ret;
}

void i915_gem_init_swizzling(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen < 5 ||
	    dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_NONE)
		return;

	I915_WRITE(DISP_ARB_CTL, I915_READ(DISP_ARB_CTL) |
				 DISP_TILE_SURFACE_SWIZZLING);

	if (IS_GEN5(dev))
		return;

	I915_WRITE(TILECTL, I915_READ(TILECTL) | TILECTL_SWZCTL);
	if (IS_GEN6(dev))
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_SNB));
	else if (IS_GEN7(dev))
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_IVB));
	else if (IS_GEN8(dev))
		I915_WRITE(GAMTARBMODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_BDW));
	else
		BUG();
}

static bool
intel_enable_blt(struct drm_device *dev)
{
	if (!HAS_BLT(dev))
		return false;

	/* The blitter was dysfunctional on early prototypes */
	if (IS_GEN6(dev) && dev->pdev->revision < 8) {
		DRM_INFO("BLT not supported on this pre-production hardware;"
			 " graphics performance will be degraded.\n");
		return false;
	}

	return true;
}

static int i915_gem_init_rings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_init_render_ring_buffer(dev);
	if (ret)
		return ret;

	if (HAS_BSD(dev)) {
		ret = intel_init_bsd_ring_buffer(dev);
		if (ret)
			goto cleanup_render_ring;
	}

	if (intel_enable_blt(dev)) {
		ret = intel_init_blt_ring_buffer(dev);
		if (ret)
			goto cleanup_bsd_ring;
	}

	if (HAS_VEBOX(dev)) {
		ret = intel_init_vebox_ring_buffer(dev);
		if (ret)
			goto cleanup_blt_ring;
	}


	ret = i915_gem_set_seqno(dev, ((u32)~0 - 0x1000));
	if (ret)
		goto cleanup_vebox_ring;

	return 0;

cleanup_vebox_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[VECS]);
cleanup_blt_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[BCS]);
cleanup_bsd_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[VCS]);
cleanup_render_ring:
	intel_cleanup_ring_buffer(&dev_priv->ring[RCS]);

	return ret;
}

int
i915_gem_init_hw(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i;

	if (INTEL_INFO(dev)->gen < 6 && !intel_enable_gtt())
		return -EIO;

	if (dev_priv->ellc_size)
		I915_WRITE(HSW_IDICR, I915_READ(HSW_IDICR) | IDIHASHMSK(0xf));

	if (IS_HASWELL(dev))
		I915_WRITE(MI_PREDICATE_RESULT_2, IS_HSW_GT3(dev) ?
			   LOWER_SLICE_ENABLED : LOWER_SLICE_DISABLED);

	if (HAS_PCH_NOP(dev)) {
		u32 temp = I915_READ(GEN7_MSG_CTL);
		temp &= ~(WAIT_FOR_PCH_FLR_ACK | WAIT_FOR_PCH_RESET_ACK);
		I915_WRITE(GEN7_MSG_CTL, temp);
	}

	i915_gem_init_swizzling(dev);

	ret = i915_gem_init_rings(dev);
	if (ret)
		return ret;

	for (i = 0; i < NUM_L3_SLICES(dev); i++)
		i915_gem_l3_remap(&dev_priv->ring[RCS], i);

	/*
	 * XXX: There was some w/a described somewhere suggesting loading
	 * contexts before PPGTT.
	 */
	i915_gem_context_init(dev);
	if (dev_priv->mm.aliasing_ppgtt) {
		ret = dev_priv->mm.aliasing_ppgtt->enable(dev);
		if (ret) {
			i915_gem_cleanup_aliasing_ppgtt(dev);
			DRM_INFO("PPGTT enable failed. This is not fatal, but unexpected\n");
		}
	}

	return 0;
}

int i915_gem_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	mutex_lock(&dev->struct_mutex);

	if (IS_VALLEYVIEW(dev)) {
		/* VLVA0 (potential hack), BIOS isn't actually waking us */
		I915_WRITE(VLV_GTLC_WAKE_CTRL, 1);
		if (wait_for((I915_READ(VLV_GTLC_PW_STATUS) & 1) == 1, 10))
			DRM_DEBUG_DRIVER("allow wake ack timed out\n");
	}

	i915_gem_init_global_gtt(dev);

	ret = i915_gem_init_hw(dev);
	mutex_unlock(&dev->struct_mutex);
	if (ret) {
		i915_gem_cleanup_aliasing_ppgtt(dev);
		return ret;
	}

	/* Allow hardware batchbuffers unless told otherwise, but not for KMS. */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev_priv->dri1.allow_batchbuffer = 1;
	return 0;
}

void
i915_gem_cleanup_ringbuffer(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int i;

	for_each_ring(ring, dev_priv, i)
		intel_cleanup_ring_buffer(ring);
}

int
i915_gem_entervt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	if (i915_reset_in_progress(&dev_priv->gpu_error)) {
		DRM_ERROR("Reenabling wedged hardware, good luck\n");
		atomic_set(&dev_priv->gpu_error.reset_counter, 0);
	}

	mutex_lock(&dev->struct_mutex);
	dev_priv->ums.mm_suspended = 0;

	ret = i915_gem_init_hw(dev);
	if (ret != 0) {
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	BUG_ON(!list_empty(&dev_priv->gtt.base.active_list));
	mutex_unlock(&dev->struct_mutex);

	ret = drm_irq_install(dev);
	if (ret)
		goto cleanup_ringbuffer;

	return 0;

cleanup_ringbuffer:
	mutex_lock(&dev->struct_mutex);
	i915_gem_cleanup_ringbuffer(dev);
	dev_priv->ums.mm_suspended = 1;
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
i915_gem_leavevt_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	drm_irq_uninstall(dev);

	return i915_gem_suspend(dev);
}

void
i915_gem_lastclose(struct drm_device *dev)
{
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ret = i915_gem_suspend(dev);
	if (ret)
		DRM_ERROR("failed to idle hardware: %d\n", ret);
}

static void
init_ring_lists(struct intel_ring_buffer *ring)
{
	INIT_LIST_HEAD(&ring->active_list);
	INIT_LIST_HEAD(&ring->request_list);
}

static void i915_init_vm(struct drm_i915_private *dev_priv,
			 struct i915_address_space *vm)
{
	vm->dev = dev_priv->dev;
	INIT_LIST_HEAD(&vm->active_list);
	INIT_LIST_HEAD(&vm->inactive_list);
	INIT_LIST_HEAD(&vm->global_link);
	list_add(&vm->global_link, &dev_priv->vm_list);
}

void
i915_gem_load(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	dev_priv->slab =
		kmem_cache_create("i915_gem_object",
				  sizeof(struct drm_i915_gem_object), 0,
				  SLAB_HWCACHE_ALIGN,
				  NULL);

	INIT_LIST_HEAD(&dev_priv->vm_list);
	i915_init_vm(dev_priv, &dev_priv->gtt.base);

	INIT_LIST_HEAD(&dev_priv->context_list);
	INIT_LIST_HEAD(&dev_priv->mm.unbound_list);
	INIT_LIST_HEAD(&dev_priv->mm.bound_list);
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	for (i = 0; i < I915_NUM_RINGS; i++)
		init_ring_lists(&dev_priv->ring[i]);
	for (i = 0; i < I915_MAX_NUM_FENCES; i++)
		INIT_LIST_HEAD(&dev_priv->fence_regs[i].lru_list);
	INIT_DELAYED_WORK(&dev_priv->mm.retire_work,
			  i915_gem_retire_work_handler);
	INIT_DELAYED_WORK(&dev_priv->mm.idle_work,
			  i915_gem_idle_work_handler);
	init_waitqueue_head(&dev_priv->gpu_error.reset_queue);

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	if (IS_GEN3(dev)) {
		I915_WRITE(MI_ARB_STATE,
			   _MASKED_BIT_ENABLE(MI_ARB_C3_LP_WRITE_ENABLE));
	}

	dev_priv->relative_constants_mode = I915_EXEC_CONSTANTS_REL_GENERAL;

	/* Old X drivers will take 0-2 for front, back, depth buffers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		dev_priv->fence_reg_start = 3;

	if (INTEL_INFO(dev)->gen >= 7 && !IS_VALLEYVIEW(dev))
		dev_priv->num_fence_regs = 32;
	else if (INTEL_INFO(dev)->gen >= 4 || IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
		dev_priv->num_fence_regs = 16;
	else
		dev_priv->num_fence_regs = 8;

	/* Initialize fence registers to zero */
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	i915_gem_restore_fences(dev);

	i915_gem_detect_bit_6_swizzle(dev);
	init_waitqueue_head(&dev_priv->pending_flip_queue);

	dev_priv->mm.interruptible = true;

	dev_priv->mm.inactive_shrinker.scan_objects = i915_gem_inactive_scan;
	dev_priv->mm.inactive_shrinker.count_objects = i915_gem_inactive_count;
	dev_priv->mm.inactive_shrinker.seeks = DEFAULT_SEEKS;
	register_shrinker(&dev_priv->mm.inactive_shrinker);
}

/*
 * Create a physically contiguous memory object for this object
 * e.g. for cursor + overlay regs
 */
static int i915_gem_init_phys_object(struct drm_device *dev,
				     int id, int size, int align)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;
	int ret;

	if (dev_priv->mm.phys_objs[id - 1] || !size)
		return 0;

	phys_obj = kzalloc(sizeof(*phys_obj), GFP_KERNEL);
	if (!phys_obj)
		return -ENOMEM;

	phys_obj->id = id;

	phys_obj->handle = drm_pci_alloc(dev, size, align);
	if (!phys_obj->handle) {
		ret = -ENOMEM;
		goto kfree_obj;
	}
#ifdef CONFIG_X86
	set_memory_wc((unsigned long)phys_obj->handle->vaddr, phys_obj->handle->size / PAGE_SIZE);
#endif

	dev_priv->mm.phys_objs[id - 1] = phys_obj;

	return 0;
kfree_obj:
	kfree(phys_obj);
	return ret;
}

static void i915_gem_free_phys_object(struct drm_device *dev, int id)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_phys_object *phys_obj;

	if (!dev_priv->mm.phys_objs[id - 1])
		return;

	phys_obj = dev_priv->mm.phys_objs[id - 1];
	if (phys_obj->cur_obj) {
		i915_gem_detach_phys_object(dev, phys_obj->cur_obj);
	}

#ifdef CONFIG_X86
	set_memory_wb((unsigned long)phys_obj->handle->vaddr, phys_obj->handle->size / PAGE_SIZE);
#endif
	drm_pci_free(dev, phys_obj->handle);
	kfree(phys_obj);
	dev_priv->mm.phys_objs[id - 1] = NULL;
}

void i915_gem_free_all_phys_object(struct drm_device *dev)
{
	int i;

	for (i = I915_GEM_PHYS_CURSOR_0; i <= I915_MAX_PHYS_OBJECT; i++)
		i915_gem_free_phys_object(dev, i);
}

void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_i915_gem_object *obj)
{
	struct address_space *mapping = file_inode(obj->base.filp)->i_mapping;
	char *vaddr;
	int i;
	int page_count;

	if (!obj->phys_obj)
		return;
	vaddr = obj->phys_obj->handle->vaddr;

	page_count = obj->base.size / PAGE_SIZE;
	for (i = 0; i < page_count; i++) {
		struct page *page = shmem_read_mapping_page(mapping, i);
		if (!IS_ERR(page)) {
			char *dst = kmap_atomic(page);
			memcpy(dst, vaddr + i*PAGE_SIZE, PAGE_SIZE);
			kunmap_atomic(dst);

			drm_clflush_pages(&page, 1);

			set_page_dirty(page);
			mark_page_accessed(page);
			page_cache_release(page);
		}
	}
	i915_gem_chipset_flush(dev);

	obj->phys_obj->cur_obj = NULL;
	obj->phys_obj = NULL;
}

int
i915_gem_attach_phys_object(struct drm_device *dev,
			    struct drm_i915_gem_object *obj,
			    int id,
			    int align)
{
	struct address_space *mapping = file_inode(obj->base.filp)->i_mapping;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret = 0;
	int page_count;
	int i;

	if (id > I915_MAX_PHYS_OBJECT)
		return -EINVAL;

	if (obj->phys_obj) {
		if (obj->phys_obj->id == id)
			return 0;
		i915_gem_detach_phys_object(dev, obj);
	}

	/* create a new object */
	if (!dev_priv->mm.phys_objs[id - 1]) {
		ret = i915_gem_init_phys_object(dev, id,
						obj->base.size, align);
		if (ret) {
			DRM_ERROR("failed to init phys object %d size: %zu\n",
				  id, obj->base.size);
			return ret;
		}
	}

	/* bind to the object */
	obj->phys_obj = dev_priv->mm.phys_objs[id - 1];
	obj->phys_obj->cur_obj = obj;

	page_count = obj->base.size / PAGE_SIZE;

	for (i = 0; i < page_count; i++) {
		struct page *page;
		char *dst, *src;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page))
			return PTR_ERR(page);

		src = kmap_atomic(page);
		dst = obj->phys_obj->handle->vaddr + (i * PAGE_SIZE);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(src);

		mark_page_accessed(page);
		page_cache_release(page);
	}

	return 0;
}

static int
i915_gem_phys_pwrite(struct drm_device *dev,
		     struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     struct drm_file *file_priv)
{
	void *vaddr = obj->phys_obj->handle->vaddr + args->offset;
	char __user *user_data = to_user_ptr(args->data_ptr);

	if (__copy_from_user_inatomic_nocache(vaddr, user_data, args->size)) {
		unsigned long unwritten;

		/* The physical object once assigned is fixed for the lifetime
		 * of the obj, so we can safely drop the lock and continue
		 * to access vaddr.
		 */
		mutex_unlock(&dev->struct_mutex);
		unwritten = copy_from_user(vaddr, user_data, args->size);
		mutex_lock(&dev->struct_mutex);
		if (unwritten)
			return -EFAULT;
	}

	i915_gem_chipset_flush(dev);
	return 0;
}

void i915_gem_release(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	cancel_delayed_work_sync(&file_priv->mm.idle_work);

	/* Clean up our request list when the client is going away, so that
	 * later retire_requests won't dereference our soon-to-be-gone
	 * file_priv.
	 */
	spin_lock(&file_priv->mm.lock);
	while (!list_empty(&file_priv->mm.request_list)) {
		struct drm_i915_gem_request *request;

		request = list_first_entry(&file_priv->mm.request_list,
					   struct drm_i915_gem_request,
					   client_list);
		list_del(&request->client_list);
		request->file_priv = NULL;
	}
	spin_unlock(&file_priv->mm.lock);
}

static void
i915_gem_file_idle_work_handler(struct work_struct *work)
{
	struct drm_i915_file_private *file_priv =
		container_of(work, typeof(*file_priv), mm.idle_work.work);

	atomic_set(&file_priv->rps_wait_boost, false);
}

int i915_gem_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv;

	DRM_DEBUG_DRIVER("\n");

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;
	file_priv->dev_priv = dev->dev_private;

	spin_lock_init(&file_priv->mm.lock);
	INIT_LIST_HEAD(&file_priv->mm.request_list);
	INIT_DELAYED_WORK(&file_priv->mm.idle_work,
			  i915_gem_file_idle_work_handler);

	idr_init(&file_priv->context_idr);

	return 0;
}

static bool mutex_is_locked_by(struct mutex *mutex, struct task_struct *task)
{
	if (!mutex_is_locked(mutex))
		return false;

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_MUTEXES)
	return mutex->owner == task;
#else
	/* Since UP may be pre-empted, we cannot assume that we own the lock */
	return false;
#endif
}

static unsigned long
i915_gem_inactive_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct drm_i915_private *dev_priv =
		container_of(shrinker,
			     struct drm_i915_private,
			     mm.inactive_shrinker);
	struct drm_device *dev = dev_priv->dev;
	struct drm_i915_gem_object *obj;
	bool unlock = true;
	unsigned long count;

	if (!mutex_trylock(&dev->struct_mutex)) {
		if (!mutex_is_locked_by(&dev->struct_mutex, current))
			return 0;

		if (dev_priv->mm.shrinker_no_lock_stealing)
			return 0;

		unlock = false;
	}

	count = 0;
	list_for_each_entry(obj, &dev_priv->mm.unbound_list, global_list)
		if (obj->pages_pin_count == 0)
			count += obj->base.size >> PAGE_SHIFT;

	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		if (obj->active)
			continue;

		if (obj->pin_count == 0 && obj->pages_pin_count == 0)
			count += obj->base.size >> PAGE_SHIFT;
	}

	if (unlock)
		mutex_unlock(&dev->struct_mutex);

	return count;
}

/* All the new VM stuff */
unsigned long i915_gem_obj_offset(struct drm_i915_gem_object *o,
				  struct i915_address_space *vm)
{
	struct drm_i915_private *dev_priv = o->base.dev->dev_private;
	struct i915_vma *vma;

	if (vm == &dev_priv->mm.aliasing_ppgtt->base)
		vm = &dev_priv->gtt.base;

	BUG_ON(list_empty(&o->vma_list));
	list_for_each_entry(vma, &o->vma_list, vma_link) {
		if (vma->vm == vm)
			return vma->node.start;

	}
	return -1;
}

bool i915_gem_obj_bound(struct drm_i915_gem_object *o,
			struct i915_address_space *vm)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &o->vma_list, vma_link)
		if (vma->vm == vm && drm_mm_node_allocated(&vma->node))
			return true;

	return false;
}

bool i915_gem_obj_bound_any(struct drm_i915_gem_object *o)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &o->vma_list, vma_link)
		if (drm_mm_node_allocated(&vma->node))
			return true;

	return false;
}

unsigned long i915_gem_obj_size(struct drm_i915_gem_object *o,
				struct i915_address_space *vm)
{
	struct drm_i915_private *dev_priv = o->base.dev->dev_private;
	struct i915_vma *vma;

	if (vm == &dev_priv->mm.aliasing_ppgtt->base)
		vm = &dev_priv->gtt.base;

	BUG_ON(list_empty(&o->vma_list));

	list_for_each_entry(vma, &o->vma_list, vma_link)
		if (vma->vm == vm)
			return vma->node.size;

	return 0;
}

static unsigned long
i915_gem_inactive_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct drm_i915_private *dev_priv =
		container_of(shrinker,
			     struct drm_i915_private,
			     mm.inactive_shrinker);
	struct drm_device *dev = dev_priv->dev;
	unsigned long freed;
	bool unlock = true;

	if (!mutex_trylock(&dev->struct_mutex)) {
		if (!mutex_is_locked_by(&dev->struct_mutex, current))
			return SHRINK_STOP;

		if (dev_priv->mm.shrinker_no_lock_stealing)
			return SHRINK_STOP;

		unlock = false;
	}

	freed = i915_gem_purge(dev_priv, sc->nr_to_scan);
	if (freed < sc->nr_to_scan)
		freed += __i915_gem_shrink(dev_priv,
					   sc->nr_to_scan - freed,
					   false);
	if (freed < sc->nr_to_scan)
		freed += i915_gem_shrink_all(dev_priv);

	if (unlock)
		mutex_unlock(&dev->struct_mutex);

	return freed;
}

struct i915_vma *i915_gem_obj_to_ggtt(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;

	if (WARN_ON(list_empty(&obj->vma_list)))
		return NULL;

	vma = list_first_entry(&obj->vma_list, typeof(*vma), vma_link);
	if (WARN_ON(vma->vm != obj_to_ggtt(obj)))
		return NULL;

	return vma;
}
