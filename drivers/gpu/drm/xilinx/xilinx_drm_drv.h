/*
 * Xilinx DRM KMS Header for Xilinx
 *
 *  Copyright (C) 2013 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
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

#ifndef _XILINX_DRM_H_
#define _XILINX_DRM_H_

/* io write operations */
static inline void xilinx_drm_writel(void __iomem *base, int offset, u32 val)
{
	writel(val, base + offset);
}

/* io read operations */
static inline u32 xilinx_drm_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

#endif /* _XILINX_DRM_H_ */
