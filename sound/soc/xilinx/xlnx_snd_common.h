/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx ASoC sound card support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 */

#ifndef _XLNX_SND_COMMON_H
#define _XLNX_SND_COMMON_H

enum {
	XLNX_PLAYBACK = 0,
	XLNX_CAPTURE = 1,
	XLNX_MAX_PATHS = 2
};

struct pl_card_data {
	u32 mclk_val;
	u32 mclk_ratio;
	int xlnx_snd_dev_id;
	struct clk *mclk;
};
#endif /* _XLNX_SND_COMMON_H */
