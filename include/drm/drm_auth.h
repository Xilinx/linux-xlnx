/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 2016 Intel Corporation
 *
 * Author: Daniel Vetter <daniel.vetter@ffwll.ch>
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_AUTH_H_
#define _DRM_AUTH_H_

/**
 * struct drm_master - drm master structure
 *
 * @refcount: Refcount for this master object.
 * @dev: Link back to the DRM device
 * @unique: Unique identifier: e.g. busid. Protected by drm_global_mutex.
 * @unique_len: Length of unique field. Protected by drm_global_mutex.
 * @magic_map: Map of used authentication tokens. Protected by struct_mutex.
 * @lock: DRI lock information.
 * @driver_priv: Pointer to driver-private information.
 *
 * Note that master structures are only relevant for the legacy/primary device
 * nodes, hence there can only be one per device, not one per drm_minor.
 */
struct drm_master {
	struct kref refcount;
	struct drm_device *dev;
	char *unique;
	int unique_len;
	struct idr magic_map;
	struct drm_lock_data lock;
	void *driver_priv;
};

struct drm_master *drm_master_get(struct drm_master *master);
void drm_master_put(struct drm_master **master);
bool drm_is_current_master(struct drm_file *fpriv);

#endif
