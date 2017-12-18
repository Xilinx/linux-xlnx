/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/sync_file.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_gem.h"

/*
 * Cmdstream submission:
 */

/* make sure these don't conflict w/ MSM_SUBMIT_BO_x */
#define BO_VALID    0x8000   /* is current addr in cmdstream correct/valid? */
#define BO_LOCKED   0x4000
#define BO_PINNED   0x2000

static struct msm_gem_submit *submit_create(struct drm_device *dev,
		struct msm_gpu *gpu, int nr_bos, int nr_cmds)
{
	struct msm_gem_submit *submit;
	int sz = sizeof(*submit) + (nr_bos * sizeof(submit->bos[0])) +
			(nr_cmds * sizeof(*submit->cmd));

	submit = kmalloc(sz, GFP_TEMPORARY | __GFP_NOWARN | __GFP_NORETRY);
	if (!submit)
		return NULL;

	submit->dev = dev;
	submit->gpu = gpu;
	submit->fence = NULL;
	submit->pid = get_pid(task_pid(current));
	submit->cmd = (void *)&submit->bos[nr_bos];

	/* initially, until copy_from_user() and bo lookup succeeds: */
	submit->nr_bos = 0;
	submit->nr_cmds = 0;

	INIT_LIST_HEAD(&submit->node);
	INIT_LIST_HEAD(&submit->bo_list);
	ww_acquire_init(&submit->ticket, &reservation_ww_class);

	return submit;
}

void msm_gem_submit_free(struct msm_gem_submit *submit)
{
	fence_put(submit->fence);
	list_del(&submit->node);
	put_pid(submit->pid);
	kfree(submit);
}

static inline unsigned long __must_check
copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		return __copy_from_user_inatomic(to, from, n);
	return -EFAULT;
}

static int submit_lookup_objects(struct msm_gem_submit *submit,
		struct drm_msm_gem_submit *args, struct drm_file *file)
{
	unsigned i;
	int ret = 0;

	spin_lock(&file->table_lock);
	pagefault_disable();

	for (i = 0; i < args->nr_bos; i++) {
		struct drm_msm_gem_submit_bo submit_bo;
		struct drm_gem_object *obj;
		struct msm_gem_object *msm_obj;
		void __user *userptr =
			u64_to_user_ptr(args->bos + (i * sizeof(submit_bo)));

		/* make sure we don't have garbage flags, in case we hit
		 * error path before flags is initialized:
		 */
		submit->bos[i].flags = 0;

		ret = copy_from_user_inatomic(&submit_bo, userptr, sizeof(submit_bo));
		if (unlikely(ret)) {
			pagefault_enable();
			spin_unlock(&file->table_lock);
			ret = copy_from_user(&submit_bo, userptr, sizeof(submit_bo));
			if (ret)
				goto out;
			spin_lock(&file->table_lock);
			pagefault_disable();
		}

		if (submit_bo.flags & ~MSM_SUBMIT_BO_FLAGS) {
			DRM_ERROR("invalid flags: %x\n", submit_bo.flags);
			ret = -EINVAL;
			goto out_unlock;
		}

		submit->bos[i].flags = submit_bo.flags;
		/* in validate_objects() we figure out if this is true: */
		submit->bos[i].iova  = submit_bo.presumed;

		/* normally use drm_gem_object_lookup(), but for bulk lookup
		 * all under single table_lock just hit object_idr directly:
		 */
		obj = idr_find(&file->object_idr, submit_bo.handle);
		if (!obj) {
			DRM_ERROR("invalid handle %u at index %u\n", submit_bo.handle, i);
			ret = -EINVAL;
			goto out_unlock;
		}

		msm_obj = to_msm_bo(obj);

		if (!list_empty(&msm_obj->submit_entry)) {
			DRM_ERROR("handle %u at index %u already on submit list\n",
					submit_bo.handle, i);
			ret = -EINVAL;
			goto out_unlock;
		}

		drm_gem_object_reference(obj);

		submit->bos[i].obj = msm_obj;

		list_add_tail(&msm_obj->submit_entry, &submit->bo_list);
	}

out_unlock:
	pagefault_enable();
	spin_unlock(&file->table_lock);

out:
	submit->nr_bos = i;

	return ret;
}

static void submit_unlock_unpin_bo(struct msm_gem_submit *submit, int i)
{
	struct msm_gem_object *msm_obj = submit->bos[i].obj;

	if (submit->bos[i].flags & BO_PINNED)
		msm_gem_put_iova(&msm_obj->base, submit->gpu->id);

	if (submit->bos[i].flags & BO_LOCKED)
		ww_mutex_unlock(&msm_obj->resv->lock);

	if (!(submit->bos[i].flags & BO_VALID))
		submit->bos[i].iova = 0;

	submit->bos[i].flags &= ~(BO_LOCKED | BO_PINNED);
}

/* This is where we make sure all the bo's are reserved and pin'd: */
static int submit_lock_objects(struct msm_gem_submit *submit)
{
	int contended, slow_locked = -1, i, ret = 0;

retry:
	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;

		if (slow_locked == i)
			slow_locked = -1;

		contended = i;

		if (!(submit->bos[i].flags & BO_LOCKED)) {
			ret = ww_mutex_lock_interruptible(&msm_obj->resv->lock,
					&submit->ticket);
			if (ret)
				goto fail;
			submit->bos[i].flags |= BO_LOCKED;
		}
	}

	ww_acquire_done(&submit->ticket);

	return 0;

fail:
	for (; i >= 0; i--)
		submit_unlock_unpin_bo(submit, i);

	if (slow_locked > 0)
		submit_unlock_unpin_bo(submit, slow_locked);

	if (ret == -EDEADLK) {
		struct msm_gem_object *msm_obj = submit->bos[contended].obj;
		/* we lost out in a seqno race, lock and retry.. */
		ret = ww_mutex_lock_slow_interruptible(&msm_obj->resv->lock,
				&submit->ticket);
		if (!ret) {
			submit->bos[contended].flags |= BO_LOCKED;
			slow_locked = contended;
			goto retry;
		}
	}

	return ret;
}

static int submit_fence_sync(struct msm_gem_submit *submit)
{
	int i, ret = 0;

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;
		bool write = submit->bos[i].flags & MSM_SUBMIT_BO_WRITE;

		ret = msm_gem_sync_object(&msm_obj->base, submit->gpu->fctx, write);
		if (ret)
			break;
	}

	return ret;
}

static int submit_pin_objects(struct msm_gem_submit *submit)
{
	int i, ret = 0;

	submit->valid = true;

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;
		uint32_t iova;

		/* if locking succeeded, pin bo: */
		ret = msm_gem_get_iova_locked(&msm_obj->base,
				submit->gpu->id, &iova);

		if (ret)
			break;

		submit->bos[i].flags |= BO_PINNED;

		if (iova == submit->bos[i].iova) {
			submit->bos[i].flags |= BO_VALID;
		} else {
			submit->bos[i].iova = iova;
			/* iova changed, so address in cmdstream is not valid: */
			submit->bos[i].flags &= ~BO_VALID;
			submit->valid = false;
		}
	}

	return ret;
}

static int submit_bo(struct msm_gem_submit *submit, uint32_t idx,
		struct msm_gem_object **obj, uint32_t *iova, bool *valid)
{
	if (idx >= submit->nr_bos) {
		DRM_ERROR("invalid buffer index: %u (out of %u)\n",
				idx, submit->nr_bos);
		return -EINVAL;
	}

	if (obj)
		*obj = submit->bos[idx].obj;
	if (iova)
		*iova = submit->bos[idx].iova;
	if (valid)
		*valid = !!(submit->bos[idx].flags & BO_VALID);

	return 0;
}

/* process the reloc's and patch up the cmdstream as needed: */
static int submit_reloc(struct msm_gem_submit *submit, struct msm_gem_object *obj,
		uint32_t offset, uint32_t nr_relocs, uint64_t relocs)
{
	uint32_t i, last_offset = 0;
	uint32_t *ptr;
	int ret;

	if (offset % 4) {
		DRM_ERROR("non-aligned cmdstream buffer: %u\n", offset);
		return -EINVAL;
	}

	/* For now, just map the entire thing.  Eventually we probably
	 * to do it page-by-page, w/ kmap() if not vmap()d..
	 */
	ptr = msm_gem_get_vaddr_locked(&obj->base);

	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		DBG("failed to map: %d", ret);
		return ret;
	}

	for (i = 0; i < nr_relocs; i++) {
		struct drm_msm_gem_submit_reloc submit_reloc;
		void __user *userptr =
			u64_to_user_ptr(relocs + (i * sizeof(submit_reloc)));
		uint32_t iova, off;
		bool valid;

		ret = copy_from_user(&submit_reloc, userptr, sizeof(submit_reloc));
		if (ret)
			return -EFAULT;

		if (submit_reloc.submit_offset % 4) {
			DRM_ERROR("non-aligned reloc offset: %u\n",
					submit_reloc.submit_offset);
			return -EINVAL;
		}

		/* offset in dwords: */
		off = submit_reloc.submit_offset / 4;

		if ((off >= (obj->base.size / 4)) ||
				(off < last_offset)) {
			DRM_ERROR("invalid offset %u at reloc %u\n", off, i);
			return -EINVAL;
		}

		ret = submit_bo(submit, submit_reloc.reloc_idx, NULL, &iova, &valid);
		if (ret)
			return ret;

		if (valid)
			continue;

		iova += submit_reloc.reloc_offset;

		if (submit_reloc.shift < 0)
			iova >>= -submit_reloc.shift;
		else
			iova <<= submit_reloc.shift;

		ptr[off] = iova | submit_reloc.or;

		last_offset = off;
	}

	msm_gem_put_vaddr_locked(&obj->base);

	return 0;
}

static void submit_cleanup(struct msm_gem_submit *submit)
{
	unsigned i;

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;
		submit_unlock_unpin_bo(submit, i);
		list_del_init(&msm_obj->submit_entry);
		drm_gem_object_unreference(&msm_obj->base);
	}

	ww_acquire_fini(&submit->ticket);
}

int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_gem_submit *args = data;
	struct msm_file_private *ctx = file->driver_priv;
	struct msm_gem_submit *submit;
	struct msm_gpu *gpu = priv->gpu;
	struct fence *in_fence = NULL;
	struct sync_file *sync_file = NULL;
	int out_fence_fd = -1;
	unsigned i;
	int ret;

	if (!gpu)
		return -ENXIO;

	/* for now, we just have 3d pipe.. eventually this would need to
	 * be more clever to dispatch to appropriate gpu module:
	 */
	if (MSM_PIPE_ID(args->flags) != MSM_PIPE_3D0)
		return -EINVAL;

	if (MSM_PIPE_FLAGS(args->flags) & ~MSM_SUBMIT_FLAGS)
		return -EINVAL;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (args->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		out_fence_fd = get_unused_fd_flags(O_CLOEXEC);
		if (out_fence_fd < 0) {
			ret = out_fence_fd;
			goto out_unlock;
		}
	}
	priv->struct_mutex_task = current;

	submit = submit_create(dev, gpu, args->nr_bos, args->nr_cmds);
	if (!submit) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	ret = submit_lookup_objects(submit, args, file);
	if (ret)
		goto out;

	ret = submit_lock_objects(submit);
	if (ret)
		goto out;

	if (args->flags & MSM_SUBMIT_FENCE_FD_IN) {
		in_fence = sync_file_get_fence(args->fence_fd);

		if (!in_fence) {
			ret = -EINVAL;
			goto out;
		}

		/* TODO if we get an array-fence due to userspace merging multiple
		 * fences, we need a way to determine if all the backing fences
		 * are from our own context..
		 */

		if (in_fence->context != gpu->fctx->context) {
			ret = fence_wait(in_fence, true);
			if (ret)
				goto out;
		}

	}

	if (!(args->fence & MSM_SUBMIT_NO_IMPLICIT)) {
		ret = submit_fence_sync(submit);
		if (ret)
			goto out;
	}

	ret = submit_pin_objects(submit);
	if (ret)
		goto out;

	for (i = 0; i < args->nr_cmds; i++) {
		struct drm_msm_gem_submit_cmd submit_cmd;
		void __user *userptr =
			u64_to_user_ptr(args->cmds + (i * sizeof(submit_cmd)));
		struct msm_gem_object *msm_obj;
		uint32_t iova;

		ret = copy_from_user(&submit_cmd, userptr, sizeof(submit_cmd));
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		/* validate input from userspace: */
		switch (submit_cmd.type) {
		case MSM_SUBMIT_CMD_BUF:
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			break;
		default:
			DRM_ERROR("invalid type: %08x\n", submit_cmd.type);
			ret = -EINVAL;
			goto out;
		}

		ret = submit_bo(submit, submit_cmd.submit_idx,
				&msm_obj, &iova, NULL);
		if (ret)
			goto out;

		if (submit_cmd.size % 4) {
			DRM_ERROR("non-aligned cmdstream buffer size: %u\n",
					submit_cmd.size);
			ret = -EINVAL;
			goto out;
		}

		if ((submit_cmd.size + submit_cmd.submit_offset) >=
				msm_obj->base.size) {
			DRM_ERROR("invalid cmdstream size: %u\n", submit_cmd.size);
			ret = -EINVAL;
			goto out;
		}

		submit->cmd[i].type = submit_cmd.type;
		submit->cmd[i].size = submit_cmd.size / 4;
		submit->cmd[i].iova = iova + submit_cmd.submit_offset;
		submit->cmd[i].idx  = submit_cmd.submit_idx;

		if (submit->valid)
			continue;

		ret = submit_reloc(submit, msm_obj, submit_cmd.submit_offset,
				submit_cmd.nr_relocs, submit_cmd.relocs);
		if (ret)
			goto out;
	}

	submit->nr_cmds = i;

	submit->fence = msm_fence_alloc(gpu->fctx);
	if (IS_ERR(submit->fence)) {
		ret = PTR_ERR(submit->fence);
		submit->fence = NULL;
		goto out;
	}

	if (args->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		sync_file = sync_file_create(submit->fence);
		if (!sync_file) {
			ret = -ENOMEM;
			goto out;
		}
	}

	msm_gpu_submit(gpu, submit, ctx);

	args->fence = submit->fence->seqno;

	if (args->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		fd_install(out_fence_fd, sync_file->file);
		args->fence_fd = out_fence_fd;
	}

out:
	if (in_fence)
		fence_put(in_fence);
	submit_cleanup(submit);
	if (ret)
		msm_gem_submit_free(submit);
out_unlock:
	if (ret && (out_fence_fd >= 0))
		put_unused_fd(out_fence_fd);
	priv->struct_mutex_task = NULL;
	mutex_unlock(&dev->struct_mutex);
	return ret;
}
