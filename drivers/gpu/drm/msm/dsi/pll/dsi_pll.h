/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
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

#ifndef __DSI_PLL_H__
#define __DSI_PLL_H__

#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "dsi.h"

#define NUM_DSI_CLOCKS_MAX	6
#define MAX_DSI_PLL_EN_SEQS	10

struct msm_dsi_pll {
	enum msm_dsi_phy_type type;

	struct clk_hw	clk_hw;
	bool		pll_on;
	bool		state_saved;

	unsigned long	min_rate;
	unsigned long	max_rate;
	u32		en_seq_cnt;

	int (*enable_seqs[MAX_DSI_PLL_EN_SEQS])(struct msm_dsi_pll *pll);
	void (*disable_seq)(struct msm_dsi_pll *pll);
	int (*get_provider)(struct msm_dsi_pll *pll,
			struct clk **byte_clk_provider,
			struct clk **pixel_clk_provider);
	void (*destroy)(struct msm_dsi_pll *pll);
	void (*save_state)(struct msm_dsi_pll *pll);
	int (*restore_state)(struct msm_dsi_pll *pll);
};

#define hw_clk_to_pll(x) container_of(x, struct msm_dsi_pll, clk_hw)

static inline void pll_write(void __iomem *reg, u32 data)
{
	msm_writel(data, reg);
}

static inline u32 pll_read(const void __iomem *reg)
{
	return msm_readl(reg);
}

static inline void pll_write_udelay(void __iomem *reg, u32 data, u32 delay_us)
{
	pll_write(reg, data);
	udelay(delay_us);
}

static inline void pll_write_ndelay(void __iomem *reg, u32 data, u32 delay_ns)
{
	pll_write((reg), data);
	ndelay(delay_ns);
}

/*
 * DSI PLL Helper functions
 */

/* clock callbacks */
long msm_dsi_pll_helper_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate);
int msm_dsi_pll_helper_clk_prepare(struct clk_hw *hw);
void msm_dsi_pll_helper_clk_unprepare(struct clk_hw *hw);
/* misc */
void msm_dsi_pll_helper_unregister_clks(struct platform_device *pdev,
					struct clk **clks, u32 num_clks);

/*
 * Initialization for Each PLL Type
 */
#ifdef CONFIG_DRM_MSM_DSI_28NM_PHY
struct msm_dsi_pll *msm_dsi_pll_28nm_init(struct platform_device *pdev,
					enum msm_dsi_phy_type type, int id);
#else
static inline struct msm_dsi_pll *msm_dsi_pll_28nm_init(
	struct platform_device *pdev, enum msm_dsi_phy_type type, int id)
{
	return ERR_PTR(-ENODEV);
}
#endif
#ifdef CONFIG_DRM_MSM_DSI_28NM_8960_PHY
struct msm_dsi_pll *msm_dsi_pll_28nm_8960_init(struct platform_device *pdev,
					       int id);
#else
static inline struct msm_dsi_pll *msm_dsi_pll_28nm_8960_init(
	struct platform_device *pdev, int id)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif /* __DSI_PLL_H__ */

