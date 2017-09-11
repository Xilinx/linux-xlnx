/*
 * Xilinx DRM Mixer KMS GEM helper

 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx DRM GEM driver by Hyun Kwon <hyunk@xilinx.com>
 *  Copyright (C) 2015
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

#include <drm/drmP.h>
#include <drm/drm_gem_cma_helper.h>

#include "xvmixer_drm_drv.h"
#include "xvmixer_drm_gem.h"

/*
 * xvmixer_drm_gem_cma_dumb_create - (struct drm_driver)->dumb_create callback
 * @file_priv: drm_file object
 * @drm: DRM object
 * @args: info for dumb scanout buffer creation
 *
 * This function is for dumb_create callback of drm_driver struct. Simply
 * it wraps around drm_gem_cma_dumb_create() and sets the pitch value
 * by retrieving the value from the device.
 *
 * Return: The return value from drm_gem_cma_dumb_create()
 */
int xvmixer_drm_gem_cma_dumb_create(struct drm_file *file_priv,
				   struct drm_device *drm,
				   struct drm_mode_create_dumb *args)
{
	int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	args->pitch = ALIGN(pitch, xvmixer_drm_get_align(drm));

	return drm_gem_cma_dumb_create_internal(file_priv, drm, args);
}
