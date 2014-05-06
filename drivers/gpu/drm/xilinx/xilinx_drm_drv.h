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

enum xilinx_video_format {
	XILINX_VIDEO_FORMAT_YUV422 = 0,
	XILINX_VIDEO_FORMAT_YUV444 = 1,
	XILINX_VIDEO_FORMAT_RGB = 2,
	XILINX_VIDEO_FORMAT_YUV420 = 3,
	XILINX_VIDEO_FORMAT_XRGB = 16,
};

/* convert the xilinx format to the drm format */
int xilinx_drm_format_by_code(unsigned int xilinx_format, uint32_t *drm_format);
int xilinx_drm_format_by_name(const char *name, uint32_t *drm_format);

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
