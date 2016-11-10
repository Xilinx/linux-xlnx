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

#ifndef _ZOCL_DRV_H_
#define _ZOCL_DRV_H_
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_mm.h>
#include <drm/drm_gem_cma_helper.h>

struct drm_zocl_bo {
	struct drm_gem_cma_object base;
	uint32_t                  flags;
};

struct drm_zocl_dev {
	struct drm_device       *ddev;
	void __iomem            *regs;
	phys_addr_t              res_start;
	resource_size_t          res_len;
	unsigned int             irq;
};

static inline struct drm_zocl_bo *to_zocl_bo(struct drm_gem_object *bo)
{
	return (struct drm_zocl_bo *) bo;
}

int zocl_create_bo_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *filp);
int zocl_sync_bo_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp);
int zocl_map_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *filp);
int zocl_info_bo_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp);
int zocl_pwrite_bo_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *filp);
int zocl_pread_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *filp);
void zocl_describe(const struct drm_zocl_bo *obj);

#endif
