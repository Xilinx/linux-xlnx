/*
 * Tegra host1x GEM implementation
 *
 * Copyright (c) 2012-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HOST1X_GEM_H
#define __HOST1X_GEM_H

#include <linux/host1x.h>

#include <drm/drm.h>
#include <drm/drmP.h>

#define TEGRA_BO_TILED     (1 << 0)
#define TEGRA_BO_BOTTOM_UP (1 << 1)

struct tegra_bo {
	struct drm_gem_object gem;
	struct host1x_bo base;
	unsigned long flags;
	dma_addr_t paddr;
	void *vaddr;
};

static inline struct tegra_bo *to_tegra_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct tegra_bo, gem);
}

extern const struct host1x_bo_ops tegra_bo_ops;

struct tegra_bo *tegra_bo_create(struct drm_device *drm, unsigned int size,
				 unsigned long flags);
struct tegra_bo *tegra_bo_create_with_handle(struct drm_file *file,
					     struct drm_device *drm,
					     unsigned int size,
					     unsigned long flags,
					     unsigned int *handle);
void tegra_bo_free_object(struct drm_gem_object *gem);
int tegra_bo_dumb_create(struct drm_file *file, struct drm_device *drm,
			 struct drm_mode_create_dumb *args);
int tegra_bo_dumb_map_offset(struct drm_file *file, struct drm_device *drm,
			     uint32_t handle, uint64_t *offset);

int tegra_drm_mmap(struct file *file, struct vm_area_struct *vma);

extern const struct vm_operations_struct tegra_bo_vm_ops;

#endif
