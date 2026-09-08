/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Audio interface between the MMI Display Controller and MMI DisplayPort Tx
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __LINUX_XLNX_MMI_AUDIO_H__
#define __LINUX_XLNX_MMI_AUDIO_H__

#include <linux/errno.h>
#include <linux/types.h>

/**
 * enum mmi_dc_aud_clk_src - Source of audio clock for the MMI DC
 * @MMIDC_27M: PS audio clock source (27 MHz reference)
 * @MMIDC_PL_AUD_CLK: PL audio clock source
 * @MMIDC_AUD_CLK_SRC_COUNT: Count of audio clock source enums
 */
enum mmi_dc_aud_clk_src {
	MMIDC_27M,
	MMIDC_PL_AUD_CLK,
	MMIDC_AUD_CLK_SRC_COUNT
};

/*
 * The Display Controller pushes into the DisplayPort Tx driver, never the
 * other way round, so the two are built independently. Without the DisplayPort
 * Tx driver reachable from the caller the pushes become no-ops reporting
 * -ENODEV, which is what the Display Controller already does when no
 * DisplayPort Tx instance is bound.
 */
#if IS_REACHABLE(CONFIG_DRM_AMD_MMI_DPTX)
int mmi_dp_set_audio_channels(u32 num_audio_channels);
int mmi_dp_set_audio_clk_src(enum mmi_dc_aud_clk_src aud_clk_src);
#else
static inline int mmi_dp_set_audio_channels(u32 num_audio_channels)
{
	return -ENODEV;
}

static inline int mmi_dp_set_audio_clk_src(enum mmi_dc_aud_clk_src aud_clk_src)
{
	return -ENODEV;
}
#endif

#endif /* __LINUX_XLNX_MMI_AUDIO_H__ */
