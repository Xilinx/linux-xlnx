/*
 * Xilinx DRM KMS GEM helper header
 *
 *  Copyright (C) 2015 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
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

#ifndef _XILINX_DRM_GEM_H_
#define _XILINX_DRM_GEM_H_

int xilinx_drm_gem_cma_dumb_create(struct drm_file *file_priv,
				   struct drm_device *drm,
				   struct drm_mode_create_dumb *args);

#endif /* _XILINX_DRM_GEM_H_ */
