/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZynqMP DPSUB Subsystem Driver
 *
 * Copyright (C) 2017 - 2020 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef _ZYNQMP_DPSUB_H_
#define _ZYNQMP_DPSUB_H_

struct zynqmp_dpsub {
	struct zynqmp_dp *dp;
	struct zynqmp_disp *disp;
	bool external_crtc_attached;
	struct platform_device *master;
};

#endif /* _ZYNQMP_DPSUB_H_ */
