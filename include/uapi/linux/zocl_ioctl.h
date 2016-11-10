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

#ifndef _XCL_ZOCL_IOCTL_H_
#define _XCL_ZOCL_IOCTL_H_

enum {
	DRM_ZOCL_CREATE_BO = 0,
	DRM_ZOCL_MAP_BO,
	DRM_ZOCL_SYNC_BO,
	DRM_ZOCL_INFO_BO,
	DRM_ZOCL_PWRITE_BO,
	DRM_ZOCL_PREAD_BO,
	DRM_ZOCL_NUM_IOCTLS
};

enum drm_zocl_sync_bo_dir {
	DRM_ZOCL_SYNC_BO_TO_DEVICE,
	DRM_ZOCL_SYNC_BO_FROM_DEVICE
};

#define DRM_ZOCL_BO_FLAGS_COHERENT   0x00000001
#define DRM_ZOCL_BO_FLAGS_CMA        0x00000002

struct drm_zocl_create_bo {
	uint64_t size;
	uint32_t handle;
	uint32_t flags;
};

struct drm_zocl_map_bo {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
};

/**
 * struct drm_zocl_sync_bo - used for SYNQ_BO IOCTL
 * @handle:	GEM object handle
 * @dir:	DRM_ZOCL_SYNC_DIR_XXX
 * @offset:	Offset into the object to write to
 * @size:	Length of data to write
 */
struct drm_zocl_sync_bo {
	uint32_t handle;
	enum drm_zocl_sync_bo_dir dir;
	uint64_t offset;
	uint64_t size;
};

/**
 * struct drm_zocl_info_bo - used for INFO_BO IOCTL
 * @handle:	GEM object handle
 * @size:	Size of BO
 * @paddr:	physical address
 */
struct drm_zocl_info_bo {
	uint32_t	handle;
	uint64_t	size;
	uint64_t	paddr;
};

/**
 * struct drm_zocl_pwrite_bo - used for PWRITE_BO IOCTL
 * @handle:	GEM object handle
 * @pad:	Padding
 * @offset:	Offset into the object to write to
 * @size:	Length of data to write
 * @data_ptr:	Pointer to read the data from (pointers not 32/64 compatible)
 */
struct drm_zocl_pwrite_bo {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
	uint64_t size;
	uint64_t data_ptr;
};

/**
 * struct drm_zocl_pread_bo - used for PREAD_BO IOCTL
 * @handle:	GEM object handle
 * @pad:	Padding
 * @offset:	Offset into the object to read from
 * @size:	Length of data to wrreadite
 * @data_ptr:	Pointer to write the data into (pointers not 32/64 compatible)
 */
struct drm_zocl_pread_bo {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
	uint64_t size;
	uint64_t data_ptr;
};

#define DRM_IOCTL_ZOCL_CREATE_BO   DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_CREATE_BO, \
				   struct drm_zocl_create_bo)
#define DRM_IOCTL_ZOCL_MAP_BO      DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_MAP_BO, struct drm_zocl_map_bo)
#define DRM_IOCTL_ZOCL_SYNC_BO     DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_SYNC_BO, struct drm_zocl_sync_bo)
#define DRM_IOCTL_ZOCL_INFO_BO     DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_INFO_BO, struct drm_zocl_info_bo)
#define DRM_IOCTL_ZOCL_PWRITE_BO   DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_PWRITE_BO, \
				   struct drm_zocl_pwrite_bo)
#define DRM_IOCTL_ZOCL_PREAD_BO    DRM_IOWR(DRM_COMMAND_BASE + \
				   DRM_ZOCL_PREAD_BO, struct drm_zocl_pread_bo)
#endif
