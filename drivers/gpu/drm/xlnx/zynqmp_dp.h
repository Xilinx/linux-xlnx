/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP DisplayPort Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_DP_H_
#define _ZYNQMP_DP_H_

struct zynqmp_dp;
struct drm_display_mode;

int zynqmp_dp_set_color(struct zynqmp_dp *dp, const char *color);
void zynqmp_dp_enable_vblank(struct zynqmp_dp *dp);
void zynqmp_dp_disable_vblank(struct zynqmp_dp *dp);
void zynqmp_dp_encoder_mode_set_stream(struct zynqmp_dp *dp,
				       struct drm_display_mode *mode);
void __maybe_unused zynqmp_dp_pm_suspend(struct zynqmp_dp *dp);
void __maybe_unused zynqmp_dp_pm_resume(struct zynqmp_dp *dp);
int zynqmp_dp_bind(struct device *dev, struct device *master, void *data);
void zynqmp_dp_unbind(struct device *dev, struct device *master, void *data);

int zynqmp_dp_probe(struct platform_device *pdev);
int zynqmp_dp_remove(struct platform_device *pdev);

#endif /* _ZYNQMP_DP_H_ */
