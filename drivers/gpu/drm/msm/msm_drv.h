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

#ifndef __MSM_DRV_H__
#define __MSM_DRV_H__

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/types.h>
#include <asm/sizes.h>

#ifndef CONFIG_OF
#include <mach/board.h>
#include <mach/socinfo.h>
#include <mach/iommu_domains.h>
#endif

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/msm_drm.h>

struct msm_kms;
struct msm_gpu;

#define NUM_DOMAINS 2    /* one for KMS, then one per gpu core (?) */

struct msm_file_private {
	/* currently we don't do anything useful with this.. but when
	 * per-context address spaces are supported we'd keep track of
	 * the context's page-tables here.
	 */
	int dummy;
};

struct msm_drm_private {

	struct msm_kms *kms;

	/* when we have more than one 'msm_gpu' these need to be an array: */
	struct msm_gpu *gpu;
	struct msm_file_private *lastctx;

	struct drm_fb_helper *fbdev;

	uint32_t next_fence, completed_fence;
	wait_queue_head_t fence_event;

	/* list of GEM objects: */
	struct list_head inactive_list;

	struct workqueue_struct *wq;

	/* callbacks deferred until bo is inactive: */
	struct list_head fence_cbs;

	/* registered IOMMU domains: */
	unsigned int num_iommus;
	struct iommu_domain *iommus[NUM_DOMAINS];

	unsigned int num_planes;
	struct drm_plane *planes[8];

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[8];

	unsigned int num_encoders;
	struct drm_encoder *encoders[8];

	unsigned int num_bridges;
	struct drm_bridge *bridges[8];

	unsigned int num_connectors;
	struct drm_connector *connectors[8];
};

struct msm_format {
	uint32_t pixel_format;
};

/* callback from wq once fence has passed: */
struct msm_fence_cb {
	struct work_struct work;
	uint32_t fence;
	void (*func)(struct msm_fence_cb *cb);
};

void __msm_fence_worker(struct work_struct *work);

#define INIT_FENCE_CB(_cb, _func)  do {                     \
		INIT_WORK(&(_cb)->work, __msm_fence_worker); \
		(_cb)->func = _func;                         \
	} while (0)

/* As there are different display controller blocks depending on the
 * snapdragon version, the kms support is split out and the appropriate
 * implementation is loaded at runtime.  The kms module is responsible
 * for constructing the appropriate planes/crtcs/encoders/connectors.
 */
struct msm_kms_funcs {
	/* hw initialization: */
	int (*hw_init)(struct msm_kms *kms);
	/* irq handling: */
	void (*irq_preinstall)(struct msm_kms *kms);
	int (*irq_postinstall)(struct msm_kms *kms);
	void (*irq_uninstall)(struct msm_kms *kms);
	irqreturn_t (*irq)(struct msm_kms *kms);
	int (*enable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);
	void (*disable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);
	/* misc: */
	const struct msm_format *(*get_format)(struct msm_kms *kms, uint32_t format);
	long (*round_pixclk)(struct msm_kms *kms, unsigned long rate,
			struct drm_encoder *encoder);
	/* cleanup: */
	void (*preclose)(struct msm_kms *kms, struct drm_file *file);
	void (*destroy)(struct msm_kms *kms);
};

struct msm_kms {
	const struct msm_kms_funcs *funcs;
};

struct msm_kms *mdp4_kms_init(struct drm_device *dev);

int msm_register_iommu(struct drm_device *dev, struct iommu_domain *iommu);
int msm_iommu_attach(struct drm_device *dev, struct iommu_domain *iommu,
		const char **names, int cnt);

int msm_wait_fence_interruptable(struct drm_device *dev, uint32_t fence,
		struct timespec *timeout);
void msm_update_fence(struct drm_device *dev, uint32_t fence);

int msm_ioctl_gem_submit(struct drm_device *dev, void *data,
		struct drm_file *file);

int msm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int msm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj);
int msm_gem_get_iova_locked(struct drm_gem_object *obj, int id,
		uint32_t *iova);
int msm_gem_get_iova(struct drm_gem_object *obj, int id, uint32_t *iova);
struct page **msm_gem_get_pages(struct drm_gem_object *obj);
void msm_gem_put_pages(struct drm_gem_object *obj);
void msm_gem_put_iova(struct drm_gem_object *obj, int id);
int msm_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int msm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj);
void *msm_gem_prime_vmap(struct drm_gem_object *obj);
void msm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		size_t size, struct sg_table *sg);
int msm_gem_prime_pin(struct drm_gem_object *obj);
void msm_gem_prime_unpin(struct drm_gem_object *obj);
void *msm_gem_vaddr_locked(struct drm_gem_object *obj);
void *msm_gem_vaddr(struct drm_gem_object *obj);
int msm_gem_queue_inactive_cb(struct drm_gem_object *obj,
		struct msm_fence_cb *cb);
void msm_gem_move_to_active(struct drm_gem_object *obj,
		struct msm_gpu *gpu, bool write, uint32_t fence);
void msm_gem_move_to_inactive(struct drm_gem_object *obj);
int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op,
		struct timespec *timeout);
int msm_gem_cpu_fini(struct drm_gem_object *obj);
void msm_gem_free_object(struct drm_gem_object *obj);
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle);
struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags);
struct drm_gem_object *msm_gem_import(struct drm_device *dev,
		uint32_t size, struct sg_table *sgt);

struct drm_gem_object *msm_framebuffer_bo(struct drm_framebuffer *fb, int plane);
const struct msm_format *msm_framebuffer_format(struct drm_framebuffer *fb);
struct drm_framebuffer *msm_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
struct drm_framebuffer *msm_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd2 *mode_cmd);

struct drm_fb_helper *msm_fbdev_init(struct drm_device *dev);

int hdmi_init(struct drm_device *dev, struct drm_encoder *encoder);
void __init hdmi_register(void);
void __exit hdmi_unregister(void);

#ifdef CONFIG_DEBUG_FS
void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m);
void msm_gem_describe_objects(struct list_head *list, struct seq_file *m);
void msm_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);
#endif

void __iomem *msm_ioremap(struct platform_device *pdev, const char *name,
		const char *dbgname);
void msm_writel(u32 data, void __iomem *addr);
u32 msm_readl(const void __iomem *addr);

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt, ...) if (0) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

static inline bool fence_completed(struct drm_device *dev, uint32_t fence)
{
	struct msm_drm_private *priv = dev->dev_private;
	return priv->completed_fence >= fence;
}

static inline int align_pitch(int width, int bpp)
{
	int bytespp = (bpp + 7) / 8;
	/* adreno needs pitch aligned to 32 pixels: */
	return bytespp * ALIGN(width, 32);
}

/* for the generated headers: */
#define INVALID_IDX(idx) ({BUG(); 0;})
#define fui(x)                ({BUG(); 0;})
#define util_float_to_half(x) ({BUG(); 0;})


#define FIELD(val, name) (((val) & name ## __MASK) >> name ## __SHIFT)

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)


#endif /* __MSM_DRV_H__ */
