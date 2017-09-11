/*
 * Xilinx DRM KMS support for Xilinx Video Mixer
 *
 *  Copyright (C) 2017 Xilinx, Inc.
 *
 *  Author: Jeffrey Mouroux <jmouroux@xilinx.com>
 *
 *  Based on Xilinx drm driver by Hyun Kwon <hyunk@xilinx.com>
 *  Copyright (C) 2013
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

#ifndef _XILINX_DRM_MIXER_H_
#define _XILINX_DRM_MIXER_H_

enum xilinx_video_format {
	XILINX_VIDEO_FORMAT_YUV422 = 0,
	XILINX_VIDEO_FORMAT_YUV444 = 1,
	XILINX_VIDEO_FORMAT_RGB = 2,
	XILINX_VIDEO_FORMAT_YUV420 = 3,
	XILINX_VIDEO_FORMAT_XRGB = 16,
	XILINX_VIDEO_FORMAT_NONE = 32,
};

unsigned int xvmixer_drm_format_bpp(uint32_t drm_format);

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

static inline void xilinx_drm_clr(void __iomem *base, int offset, u32 clr)
{
	xilinx_drm_writel(base, offset, xilinx_drm_readl(base, offset) & ~clr);
}

static inline void xilinx_drm_set(void __iomem *base, int offset, u32 set)
{
	xilinx_drm_writel(base, offset, xilinx_drm_readl(base, offset) | set);
}

struct drm_device;
struct drm_mode_set;

bool xvmixer_drm_check_format(struct drm_device *drm, uint32_t fourcc);
uint32_t xvmixer_drm_get_format(struct drm_device *drm);
unsigned int xvmixer_drm_get_align(struct drm_device *drm);
void xvmixer_drm_set_config(struct drm_device *drm, struct drm_mode_set *set);

#endif /* _XILINX_DRM_MIXER_H_ */
