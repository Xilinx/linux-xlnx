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

#include "msm_drv.h"
#include "msm_gpu.h"

static void msm_fb_output_poll_changed(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	if (priv->fbdev)
		drm_fb_helper_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = msm_framebuffer_create,
	.output_poll_changed = msm_fb_output_poll_changed,
};

static int msm_fault_handler(struct iommu_domain *iommu, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	DBG("*** fault: iova=%08lx, flags=%d", iova, flags);
	return 0;
}

int msm_register_iommu(struct drm_device *dev, struct iommu_domain *iommu)
{
	struct msm_drm_private *priv = dev->dev_private;
	int idx = priv->num_iommus++;

	if (WARN_ON(idx >= ARRAY_SIZE(priv->iommus)))
		return -EINVAL;

	priv->iommus[idx] = iommu;

	iommu_set_fault_handler(iommu, msm_fault_handler, dev);

	/* need to iommu_attach_device() somewhere??  on resume?? */

	return idx;
}

int msm_iommu_attach(struct drm_device *dev, struct iommu_domain *iommu,
		const char **names, int cnt)
{
	int i, ret;

	for (i = 0; i < cnt; i++) {
		/* TODO maybe some day msm iommu won't require this hack: */
		struct device *msm_iommu_get_ctx(const char *ctx_name);
		struct device *ctx = msm_iommu_get_ctx(names[i]);
		if (!ctx)
			continue;
		ret = iommu_attach_device(iommu, ctx);
		if (ret) {
			dev_warn(dev->dev, "could not attach iommu to %s", names[i]);
			return ret;
		}
	}
	return 0;
}

#ifdef CONFIG_DRM_MSM_REGISTER_LOGGING
static bool reglog = false;
MODULE_PARM_DESC(reglog, "Enable register read/write logging");
module_param(reglog, bool, 0600);
#else
#define reglog 0
#endif

void __iomem *msm_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname)
{
	struct resource *res;
	unsigned long size;
	void __iomem *ptr;

	if (name)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	else
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource: %s\n", name);
		return ERR_PTR(-EINVAL);
	}

	size = resource_size(res);

	ptr = devm_ioremap_nocache(&pdev->dev, res->start, size);
	if (!ptr) {
		dev_err(&pdev->dev, "failed to ioremap: %s\n", name);
		return ERR_PTR(-ENOMEM);
	}

	if (reglog)
		printk(KERN_DEBUG "IO:region %s %08x %08lx\n", dbgname, (u32)ptr, size);

	return ptr;
}

void msm_writel(u32 data, void __iomem *addr)
{
	if (reglog)
		printk(KERN_DEBUG "IO:W %08x %08x\n", (u32)addr, data);
	writel(data, addr);
}

u32 msm_readl(const void __iomem *addr)
{
	u32 val = readl(addr);
	if (reglog)
		printk(KERN_ERR "IO:R %08x %08x\n", (u32)addr, val);
	return val;
}

/*
 * DRM operations:
 */

static int msm_unload(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct msm_gpu *gpu = priv->gpu;

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);

	pm_runtime_get_sync(dev->dev);
	drm_irq_uninstall(dev);
	pm_runtime_put_sync(dev->dev);

	flush_workqueue(priv->wq);
	destroy_workqueue(priv->wq);

	if (kms) {
		pm_runtime_disable(dev->dev);
		kms->funcs->destroy(kms);
	}

	if (gpu) {
		mutex_lock(&dev->struct_mutex);
		gpu->funcs->pm_suspend(gpu);
		gpu->funcs->destroy(gpu);
		mutex_unlock(&dev->struct_mutex);
	}

	dev->dev_private = NULL;

	kfree(priv);

	return 0;
}

static int msm_load(struct drm_device *dev, unsigned long flags)
{
	struct platform_device *pdev = dev->platformdev;
	struct msm_drm_private *priv;
	struct msm_kms *kms;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev->dev, "failed to allocate private data\n");
		return -ENOMEM;
	}

	dev->dev_private = priv;

	priv->wq = alloc_ordered_workqueue("msm", 0);
	init_waitqueue_head(&priv->fence_event);

	INIT_LIST_HEAD(&priv->inactive_list);
	INIT_LIST_HEAD(&priv->fence_cbs);

	drm_mode_config_init(dev);

	kms = mdp4_kms_init(dev);
	if (IS_ERR(kms)) {
		/*
		 * NOTE: once we have GPU support, having no kms should not
		 * be considered fatal.. ideally we would still support gpu
		 * and (for example) use dmabuf/prime to share buffers with
		 * imx drm driver on iMX5
		 */
		dev_err(dev->dev, "failed to load kms\n");
		ret = PTR_ERR(kms);
		goto fail;
	}

	priv->kms = kms;

	if (kms) {
		pm_runtime_enable(dev->dev);
		ret = kms->funcs->hw_init(kms);
		if (ret) {
			dev_err(dev->dev, "kms hw init failed: %d\n", ret);
			goto fail;
		}
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &mode_config_funcs;

	ret = drm_vblank_init(dev, 1);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		goto fail;
	}

	pm_runtime_get_sync(dev->dev);
	ret = drm_irq_install(dev);
	pm_runtime_put_sync(dev->dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to install IRQ handler\n");
		goto fail;
	}

	platform_set_drvdata(pdev, dev);

#ifdef CONFIG_DRM_MSM_FBDEV
	priv->fbdev = msm_fbdev_init(dev);
#endif

	drm_kms_helper_poll_init(dev);

	return 0;

fail:
	msm_unload(dev);
	return ret;
}

static void load_gpu(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu;

	if (priv->gpu)
		return;

	mutex_lock(&dev->struct_mutex);
	gpu = a3xx_gpu_init(dev);
	if (IS_ERR(gpu)) {
		dev_warn(dev->dev, "failed to load a3xx gpu\n");
		gpu = NULL;
		/* not fatal */
	}
	mutex_unlock(&dev->struct_mutex);

	if (gpu) {
		int ret;
		gpu->funcs->pm_resume(gpu);
		ret = gpu->funcs->hw_init(gpu);
		if (ret) {
			dev_err(dev->dev, "gpu hw init failed: %d\n", ret);
			gpu->funcs->destroy(gpu);
			gpu = NULL;
		}
	}

	priv->gpu = gpu;
}

static int msm_open(struct drm_device *dev, struct drm_file *file)
{
	struct msm_file_private *ctx;

	/* For now, load gpu on open.. to avoid the requirement of having
	 * firmware in the initrd.
	 */
	load_gpu(dev);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->driver_priv = ctx;

	return 0;
}

static void msm_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_file_private *ctx = file->driver_priv;
	struct msm_kms *kms = priv->kms;

	if (kms)
		kms->funcs->preclose(kms, file);

	mutex_lock(&dev->struct_mutex);
	if (ctx == priv->lastctx)
		priv->lastctx = NULL;
	mutex_unlock(&dev->struct_mutex);

	kfree(ctx);
}

static void msm_lastclose(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	if (priv->fbdev) {
		drm_modeset_lock_all(dev);
		drm_fb_helper_restore_fbdev_mode(priv->fbdev);
		drm_modeset_unlock_all(dev);
	}
}

static irqreturn_t msm_irq(DRM_IRQ_ARGS)
{
	struct drm_device *dev = arg;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	return kms->funcs->irq(kms);
}

static void msm_irq_preinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	kms->funcs->irq_preinstall(kms);
}

static int msm_irq_postinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	return kms->funcs->irq_postinstall(kms);
}

static void msm_irq_uninstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	BUG_ON(!kms);
	kms->funcs->irq_uninstall(kms);
}

static int msm_enable_vblank(struct drm_device *dev, int crtc_id)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return -ENXIO;
	DBG("dev=%p, crtc=%d", dev, crtc_id);
	return kms->funcs->enable_vblank(kms, priv->crtcs[crtc_id]);
}

static void msm_disable_vblank(struct drm_device *dev, int crtc_id)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return;
	DBG("dev=%p, crtc=%d", dev, crtc_id);
	kms->funcs->disable_vblank(kms, priv->crtcs[crtc_id]);
}

/*
 * DRM debugfs:
 */

#ifdef CONFIG_DEBUG_FS
static int msm_gpu_show(struct drm_device *dev, struct seq_file *m)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;

	if (gpu) {
		seq_printf(m, "%s Status:\n", gpu->name);
		gpu->funcs->show(gpu, m);
	}

	return 0;
}

static int msm_gem_show(struct drm_device *dev, struct seq_file *m)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;

	if (gpu) {
		seq_printf(m, "Active Objects (%s):\n", gpu->name);
		msm_gem_describe_objects(&gpu->active_list, m);
	}

	seq_printf(m, "Inactive Objects:\n");
	msm_gem_describe_objects(&priv->inactive_list, m);

	return 0;
}

static int msm_mm_show(struct drm_device *dev, struct seq_file *m)
{
	return drm_mm_dump_table(m, dev->mm_private);
}

static int msm_fb_show(struct drm_device *dev, struct seq_file *m)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_framebuffer *fb, *fbdev_fb = NULL;

	if (priv->fbdev) {
		seq_printf(m, "fbcon ");
		fbdev_fb = priv->fbdev->fb;
		msm_framebuffer_describe(fbdev_fb, m);
	}

	mutex_lock(&dev->mode_config.fb_lock);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		if (fb == fbdev_fb)
			continue;

		seq_printf(m, "user ");
		msm_framebuffer_describe(fb, m);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

static int show_locked(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	int (*show)(struct drm_device *dev, struct seq_file *m) =
			node->info_ent->data;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	ret = show(dev, m);

	mutex_unlock(&dev->struct_mutex);

	return ret;
}

static struct drm_info_list msm_debugfs_list[] = {
		{"gpu", show_locked, 0, msm_gpu_show},
		{"gem", show_locked, 0, msm_gem_show},
		{ "mm", show_locked, 0, msm_mm_show },
		{ "fb", show_locked, 0, msm_fb_show },
};

static int msm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	ret = drm_debugfs_create_files(msm_debugfs_list,
			ARRAY_SIZE(msm_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install msm_debugfs_list\n");
		return ret;
	}

	return ret;
}

static void msm_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(msm_debugfs_list,
			ARRAY_SIZE(msm_debugfs_list), minor);
}
#endif

/*
 * Fences:
 */

int msm_wait_fence_interruptable(struct drm_device *dev, uint32_t fence,
		struct timespec *timeout)
{
	struct msm_drm_private *priv = dev->dev_private;
	int ret;

	if (!priv->gpu)
		return 0;

	if (fence > priv->gpu->submitted_fence) {
		DRM_ERROR("waiting on invalid fence: %u (of %u)\n",
				fence, priv->gpu->submitted_fence);
		return -EINVAL;
	}

	if (!timeout) {
		/* no-wait: */
		ret = fence_completed(dev, fence) ? 0 : -EBUSY;
	} else {
		unsigned long timeout_jiffies = timespec_to_jiffies(timeout);
		unsigned long start_jiffies = jiffies;
		unsigned long remaining_jiffies;

		if (time_after(start_jiffies, timeout_jiffies))
			remaining_jiffies = 0;
		else
			remaining_jiffies = timeout_jiffies - start_jiffies;

		ret = wait_event_interruptible_timeout(priv->fence_event,
				fence_completed(dev, fence),
				remaining_jiffies);

		if (ret == 0) {
			DBG("timeout waiting for fence: %u (completed: %u)",
					fence, priv->completed_fence);
			ret = -ETIMEDOUT;
		} else if (ret != -ERESTARTSYS) {
			ret = 0;
		}
	}

	return ret;
}

/* called from workqueue */
void msm_update_fence(struct drm_device *dev, uint32_t fence)
{
	struct msm_drm_private *priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	priv->completed_fence = max(fence, priv->completed_fence);

	while (!list_empty(&priv->fence_cbs)) {
		struct msm_fence_cb *cb;

		cb = list_first_entry(&priv->fence_cbs,
				struct msm_fence_cb, work.entry);

		if (cb->fence > priv->completed_fence)
			break;

		list_del_init(&cb->work.entry);
		queue_work(priv->wq, &cb->work);
	}

	mutex_unlock(&dev->struct_mutex);

	wake_up_all(&priv->fence_event);
}

void __msm_fence_worker(struct work_struct *work)
{
	struct msm_fence_cb *cb = container_of(work, struct msm_fence_cb, work);
	cb->func(cb);
}

/*
 * DRM ioctls:
 */

static int msm_ioctl_get_param(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_msm_param *args = data;
	struct msm_gpu *gpu;

	/* for now, we just have 3d pipe.. eventually this would need to
	 * be more clever to dispatch to appropriate gpu module:
	 */
	if (args->pipe != MSM_PIPE_3D0)
		return -EINVAL;

	gpu = priv->gpu;

	if (!gpu)
		return -ENXIO;

	return gpu->funcs->get_param(gpu, args->param, &args->value);
}

static int msm_ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_new *args = data;
	return msm_gem_new_handle(dev, file, args->size,
			args->flags, &args->handle);
}

#define TS(t) ((struct timespec){ .tv_sec = (t).tv_sec, .tv_nsec = (t).tv_nsec })

static int msm_ioctl_gem_cpu_prep(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_cpu_prep *args = data;
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = msm_gem_cpu_prep(obj, args->op, &TS(args->timeout));

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_gem_cpu_fini(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_cpu_fini *args = data;
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (!obj)
		return -ENOENT;

	ret = msm_gem_cpu_fini(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret = 0;

	if (args->pad)
		return -EINVAL;

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (!obj)
		return -ENOENT;

	args->offset = msm_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_ioctl_wait_fence(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_msm_wait_fence *args = data;
	return msm_wait_fence_interruptable(dev, args->fence, &TS(args->timeout));
}

static const struct drm_ioctl_desc msm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MSM_GET_PARAM,    msm_ioctl_get_param,    DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_NEW,      msm_ioctl_gem_new,      DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_INFO,     msm_ioctl_gem_info,     DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_PREP, msm_ioctl_gem_cpu_prep, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_FINI, msm_ioctl_gem_cpu_fini, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_SUBMIT,   msm_ioctl_gem_submit,   DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_WAIT_FENCE,   msm_ioctl_wait_fence,   DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct vm_operations_struct vm_ops = {
	.fault = msm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl       = drm_compat_ioctl,
#endif
	.poll               = drm_poll,
	.read               = drm_read,
	.llseek             = no_llseek,
	.mmap               = msm_gem_mmap,
};

static struct drm_driver msm_driver = {
	.driver_features    = DRIVER_HAVE_IRQ |
				DRIVER_GEM |
				DRIVER_PRIME |
				DRIVER_RENDER |
				DRIVER_MODESET,
	.load               = msm_load,
	.unload             = msm_unload,
	.open               = msm_open,
	.preclose           = msm_preclose,
	.lastclose          = msm_lastclose,
	.irq_handler        = msm_irq,
	.irq_preinstall     = msm_irq_preinstall,
	.irq_postinstall    = msm_irq_postinstall,
	.irq_uninstall      = msm_irq_uninstall,
	.get_vblank_counter = drm_vblank_count,
	.enable_vblank      = msm_enable_vblank,
	.disable_vblank     = msm_disable_vblank,
	.gem_free_object    = msm_gem_free_object,
	.gem_vm_ops         = &vm_ops,
	.dumb_create        = msm_gem_dumb_create,
	.dumb_map_offset    = msm_gem_dumb_map_offset,
	.dumb_destroy       = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export   = drm_gem_prime_export,
	.gem_prime_import   = drm_gem_prime_import,
	.gem_prime_pin      = msm_gem_prime_pin,
	.gem_prime_unpin    = msm_gem_prime_unpin,
	.gem_prime_get_sg_table = msm_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = msm_gem_prime_import_sg_table,
	.gem_prime_vmap     = msm_gem_prime_vmap,
	.gem_prime_vunmap   = msm_gem_prime_vunmap,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = msm_debugfs_init,
	.debugfs_cleanup    = msm_debugfs_cleanup,
#endif
	.ioctls             = msm_ioctls,
	.num_ioctls         = DRM_MSM_NUM_IOCTLS,
	.fops               = &fops,
	.name               = "msm",
	.desc               = "MSM Snapdragon DRM",
	.date               = "20130625",
	.major              = 1,
	.minor              = 0,
};

#ifdef CONFIG_PM_SLEEP
static int msm_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(ddev);

	return 0;
}

static int msm_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_kms_helper_poll_enable(ddev);

	return 0;
}
#endif

static const struct dev_pm_ops msm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_pm_suspend, msm_pm_resume)
};

/*
 * Platform driver:
 */

static int msm_pdev_probe(struct platform_device *pdev)
{
	return drm_platform_init(&msm_driver, pdev);
}

static int msm_pdev_remove(struct platform_device *pdev)
{
	drm_platform_exit(&msm_driver, pdev);

	return 0;
}

static const struct platform_device_id msm_id[] = {
	{ "mdp", 0 },
	{ }
};

static struct platform_driver msm_platform_driver = {
	.probe      = msm_pdev_probe,
	.remove     = msm_pdev_remove,
	.driver     = {
		.owner  = THIS_MODULE,
		.name   = "msm",
		.pm     = &msm_pm_ops,
	},
	.id_table   = msm_id,
};

static int __init msm_drm_register(void)
{
	DBG("init");
	hdmi_register();
	a3xx_register();
	return platform_driver_register(&msm_platform_driver);
}

static void __exit msm_drm_unregister(void)
{
	DBG("fini");
	platform_driver_unregister(&msm_platform_driver);
	hdmi_unregister();
	a3xx_unregister();
}

module_init(msm_drm_register);
module_exit(msm_drm_unregister);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("MSM DRM Driver");
MODULE_LICENSE("GPL");
