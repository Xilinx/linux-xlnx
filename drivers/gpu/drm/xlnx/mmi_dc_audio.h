/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multimedia Integrated Display Controller Driver - Audio Support
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __MMI_DC_AUDIO_H__
#define __MMI_DC_AUDIO_H__

#include <linux/types.h>

#include "mmi_dc.h"

int mmi_dc_audio_init(struct mmi_dc *dc);
void mmi_dc_audio_uninit(struct mmi_dc *dc);
int mmi_dc_audio_set_clk_src(struct mmi_dc *dc,
			     enum mmi_dc_aud_clk_src audclksrc);

#endif /* __MMI_DC_AUDIO_H__ */
