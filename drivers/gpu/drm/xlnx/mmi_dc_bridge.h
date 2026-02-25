/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated Display Controller Bridge Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DC_BRIDGE_H__
#define __MMI_DC_BRIDGE_H__

#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>

struct mmi_dc;
struct mmi_dc_plane;

/**
 * struct mmi_dc_bridge - DRM bridge wrapper for the MMI Display Controller
 * @base: DRM core bridge object embedded for registration with the bridge API
 * @dc: MMI DC instance
 * @plane: Optional MMI DC plane associated with this bridge instance
 * @mst_id: Bridge MST id [0..3]
 * @connector_status: Cached connector state reported to the DRM core
 * @display_mode: Display timings currently programmed on the controller
 */
struct mmi_dc_bridge {
	struct drm_bridge		base;
	struct mmi_dc			*dc;
	struct mmi_dc_plane		*plane;
	u32				mst_id;
	enum drm_connector_status	connector_status;
	struct drm_display_mode		display_mode;
};

struct mmi_dc_bridge *mmi_dc_bridge_init(struct device *dev,
					 struct mmi_dc_plane *plane);
void mmi_dc_bridge_connect(struct mmi_dc_bridge *bridge,
			   const struct drm_display_mode *mode);
void mmi_dc_bridge_disconnect(struct mmi_dc_bridge *bridge);

#endif /* __MMI_DC_BRIDGE_H__ */
