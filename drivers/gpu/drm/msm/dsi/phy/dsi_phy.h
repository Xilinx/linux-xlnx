/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DSI_PHY_H__
#define __DSI_PHY_H__

#include <linux/regulator/consumer.h>

#include "dsi.h"

#define dsi_phy_read(offset) msm_readl((offset))
#define dsi_phy_write(offset, data) msm_writel((data), (offset))

struct msm_dsi_phy_ops {
	int (*enable)(struct msm_dsi_phy *phy, int src_pll_id,
		const unsigned long bit_rate, const unsigned long esc_rate);
	void (*disable)(struct msm_dsi_phy *phy);
};

struct msm_dsi_phy_cfg {
	enum msm_dsi_phy_type type;
	struct dsi_reg_config reg_cfg;
	struct msm_dsi_phy_ops ops;

	/*
	 * Each cell {phy_id, pll_id} of the truth table indicates
	 * if the source PLL selection bit should be set for each PHY.
	 * Fill default H/W values in illegal cells, eg. cell {0, 1}.
	 */
	bool src_pll_truthtable[DSI_MAX][DSI_MAX];
	const resource_size_t io_start[DSI_MAX];
	const int num_dsi_phy;
};

extern const struct msm_dsi_phy_cfg dsi_phy_28nm_hpm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_28nm_lp_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_20nm_cfgs;
extern const struct msm_dsi_phy_cfg dsi_phy_28nm_8960_cfgs;

struct msm_dsi_dphy_timing {
	u32 clk_pre;
	u32 clk_post;
	u32 clk_zero;
	u32 clk_trail;
	u32 clk_prepare;
	u32 hs_exit;
	u32 hs_zero;
	u32 hs_prepare;
	u32 hs_trail;
	u32 hs_rqst;
	u32 ta_go;
	u32 ta_sure;
	u32 ta_get;
};

struct msm_dsi_phy {
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *reg_base;
	int id;

	struct clk *ahb_clk;
	struct regulator_bulk_data supplies[DSI_DEV_REGULATOR_MAX];

	struct msm_dsi_dphy_timing timing;
	const struct msm_dsi_phy_cfg *cfg;

	bool regulator_ldo_mode;

	struct msm_dsi_pll *pll;
};

/*
 * PHY internal functions
 */
int msm_dsi_dphy_timing_calc(struct msm_dsi_dphy_timing *timing,
	const unsigned long bit_rate, const unsigned long esc_rate);
void msm_dsi_phy_set_src_pll(struct msm_dsi_phy *phy, int pll_id, u32 reg,
				u32 bit_mask);

#endif /* __DSI_PHY_H__ */

