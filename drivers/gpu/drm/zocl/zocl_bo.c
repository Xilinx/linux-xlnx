/*
 * A GEM style CMA backed memory manager for ZynQ based OpenCL accelerators.
 *
 * Copyright (C) 2016 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *    Sonal Santan <sonal.santan@xilinx.com>
 *    Umang Parekh <umang.parekh@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/ramfs.h>
#include <linux/shmem_fs.h>
#include "zocl_drv.h"
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/zocl_ioctl.h>

static inline void __user *to_user_ptr(u64 address)
{
	return (void __user *)(uintptr_t)address;
}

void zocl_describe(const struct drm_zocl_bo *obj)
{
	size_t size_in_kb = obj->base.base.size / 1024;
	size_t physical_addr = obj->base.paddr;

	DRM_INFO("%p: H[0x%zxKB] D[0x%zx]\n",
		  obj,
		  size_in_kb,
		  physical_addr);
}

static struct drm_zocl_bo *zocl_create_bo(struct drm_device *dev,
	uint64_t unaligned_size)
{
	size_t size = PAGE_ALIGN(unaligned_size);
	struct drm_gem_cma_object *cma_obj;

	DRM_DEBUG("%s:%s:%d: %zd\n", __FILE__, __func__, __LINE__, size);

	if (!size)
		return ERR_PTR(-EINVAL);

	cma_obj = drm_gem_cma_create(dev, size);
	if (IS_ERR(cma_obj))
		return ERR_PTR(-ENOMEM);

	return to_zocl_bo(&cma_obj->base);
}

int zocl_create_bo_ioctl(struct drm_device *dev,
		void *data,
		struct drm_file *filp)
{
	int ret;
	struct drm_zocl_create_bo *args = data;
	struct drm_zocl_bo *bo;

	if (((args->flags & DRM_ZOCL_BO_FLAGS_COHERENT) == 0) ||
	    ((args->flags & DRM_ZOCL_BO_FLAGS_CMA) == 0))
		return -EINVAL;

	bo = zocl_create_bo(dev, args->size);
	bo->flags |= DRM_ZOCL_BO_FLAGS_COHERENT;
	bo->flags |= DRM_ZOCL_BO_FLAGS_CMA;

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, bo);

	if (IS_ERR(bo)) {
		DRM_DEBUG("object creation failed\n");
		return PTR_ERR(bo);
	}
	ret = drm_gem_handle_create(filp, &bo->base.base, &args->handle);
	if (ret) {
		drm_gem_cma_free_object(&bo->base.base);
		DRM_DEBUG("handle creation failed\n");
		return ret;
	}

	zocl_describe(bo);
	drm_gem_object_unreference_unlocked(&bo->base.base);

	return ret;
}

int zocl_map_bo_ioctl(struct drm_device *dev,
		void *data,
		struct drm_file *filp)
{
	struct drm_zocl_map_bo *args = data;
	struct drm_gem_object *gem_obj;

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, data);
	gem_obj = drm_gem_object_lookup(dev, filp, args->handle);
	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	/* The mmap offset was set up at BO allocation time. */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	zocl_describe(to_zocl_bo(gem_obj));
	drm_gem_object_unreference_unlocked(gem_obj);

	return 0;
}

int zocl_sync_bo_ioctl(struct drm_device *dev,
		void *data,
		struct drm_file *filp)
{
	const struct drm_zocl_sync_bo *args = data;
	struct drm_gem_object *gem_obj = drm_gem_object_lookup(dev, filp,
							       args->handle);
	void *kaddr;
	int ret = 0;

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, data);

	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	if ((args->offset > gem_obj->size) || (args->size > gem_obj->size) ||
		((args->offset + args->size) > gem_obj->size)) {
		ret = -EINVAL;
		goto out;
	}

	kaddr = drm_gem_cma_prime_vmap(gem_obj);

	/* only invalidate the range of addresses requested by the user */
	kaddr += args->offset;

	if (args->dir == DRM_ZOCL_SYNC_BO_TO_DEVICE)
		flush_kernel_vmap_range(kaddr, args->size);
	else if (args->dir == DRM_ZOCL_SYNC_BO_FROM_DEVICE)
		invalidate_kernel_vmap_range(kaddr, args->size);
	else
		ret = -EINVAL;

out:
	drm_gem_object_unreference_unlocked(gem_obj);

	return ret;
}

int zocl_info_bo_ioctl(struct drm_device *dev,
		void *data,
		struct drm_file *filp)
{
	const struct drm_zocl_bo *bo;
	struct drm_zocl_info_bo *args = data;
	struct drm_gem_object *gem_obj = drm_gem_object_lookup(dev, filp,
							       args->handle);

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, data);

	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	bo = to_zocl_bo(gem_obj);

	args->size = bo->base.base.size;
	args->paddr = bo->base.paddr;
	drm_gem_object_unreference_unlocked(gem_obj);

	return 0;
}

int zocl_pwrite_bo_ioctl(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	const struct drm_zocl_pwrite_bo *args = data;
	struct drm_gem_object *gem_obj = drm_gem_object_lookup(dev, filp,
							       args->handle);
	char __user *user_data = to_user_ptr(args->data_ptr);
	int ret = 0;
	void *kaddr;

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, data);

	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	if ((args->offset > gem_obj->size) || (args->size > gem_obj->size)
		|| ((args->offset + args->size) > gem_obj->size)) {
		ret = -EINVAL;
		goto out;
	}

	if (args->size == 0) {
		ret = 0;
		goto out;
	}

	if (!access_ok(VERIFY_READ, user_data, args->size)) {
		ret = -EFAULT;
		goto out;
	}

	kaddr = drm_gem_cma_prime_vmap(gem_obj);
	kaddr += args->offset;

	ret = copy_from_user(kaddr, user_data, args->size);
out:
	drm_gem_object_unreference_unlocked(gem_obj);

	return ret;
}

int zocl_pread_bo_ioctl(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	const struct drm_zocl_pread_bo *args = data;
	struct drm_gem_object *gem_obj = drm_gem_object_lookup(dev, filp,
							       args->handle);
	char __user *user_data = to_user_ptr(args->data_ptr);
	int ret = 0;
	void *kaddr;

	DRM_DEBUG("%s:%s:%d: %p\n", __FILE__, __func__, __LINE__, data);

	if (!gem_obj) {
		DRM_ERROR("Failed to look up GEM BO %d\n", args->handle);
		return -EINVAL;
	}

	if ((args->offset > gem_obj->size) || (args->size > gem_obj->size)
		|| ((args->offset + args->size) > gem_obj->size)) {
		ret = -EINVAL;
		goto out;
	}

	if (args->size == 0) {
		ret = 0;
		goto out;
	}

	if (!access_ok(VERIFY_WRITE, user_data, args->size)) {
		ret = EFAULT;
		goto out;
	}

	kaddr = drm_gem_cma_prime_vmap(gem_obj);
	kaddr += args->offset;

	ret = copy_to_user(user_data, kaddr, args->size);

out:
	drm_gem_object_unreference_unlocked(gem_obj);

	return ret;
}
