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

#include <linux/platform_device.h>

#include "dsi_phy.h"

#define S_DIV_ROUND_UP(n, d)	\
	(((n) >= 0) ? (((n) + (d) - 1) / (d)) : (((n) - (d) + 1) / (d)))

static inline s32 linear_inter(s32 tmax, s32 tmin, s32 percent,
				s32 min_result, bool even)
{
	s32 v;

	v = (tmax - tmin) * percent;
	v = S_DIV_ROUND_UP(v, 100) + tmin;
	if (even && (v & 0x1))
		return max_t(s32, min_result, v - 1);
	else
		return max_t(s32, min_result, v);
}

static void dsi_dphy_timing_calc_clk_zero(struct msm_dsi_dphy_timing *timing,
					s32 ui, s32 coeff, s32 pcnt)
{
	s32 tmax, tmin, clk_z;
	s32 temp;

	/* reset */
	temp = 300 * coeff - ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	if (tmin > 255) {
		tmax = 511;
		clk_z = linear_inter(2 * tmin, tmin, pcnt, 0, true);
	} else {
		tmax = 255;
		clk_z = linear_inter(tmax, tmin, pcnt, 0, true);
	}

	/* adjust */
	temp = (timing->hs_rqst + timing->clk_prepare + clk_z) & 0x7;
	timing->clk_zero = clk_z + 8 - temp;
}

int msm_dsi_dphy_timing_calc(struct msm_dsi_dphy_timing *timing,
	const unsigned long bit_rate, const unsigned long esc_rate)
{
	s32 ui, lpx;
	s32 tmax, tmin;
	s32 pcnt0 = 10;
	s32 pcnt1 = (bit_rate > 1200000000) ? 15 : 10;
	s32 pcnt2 = 10;
	s32 pcnt3 = (bit_rate > 180000000) ? 10 : 40;
	s32 coeff = 1000; /* Precision, should avoid overflow */
	s32 temp;

	if (!bit_rate || !esc_rate)
		return -EINVAL;

	ui = mult_frac(NSEC_PER_MSEC, coeff, bit_rate / 1000);
	lpx = mult_frac(NSEC_PER_MSEC, coeff, esc_rate / 1000);

	tmax = S_DIV_ROUND_UP(95 * coeff, ui) - 2;
	tmin = S_DIV_ROUND_UP(38 * coeff, ui) - 2;
	timing->clk_prepare = linear_inter(tmax, tmin, pcnt0, 0, true);

	temp = lpx / ui;
	if (temp & 0x1)
		timing->hs_rqst = temp;
	else
		timing->hs_rqst = max_t(s32, 0, temp - 2);

	/* Calculate clk_zero after clk_prepare and hs_rqst */
	dsi_dphy_timing_calc_clk_zero(timing, ui, coeff, pcnt2);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	tmin = S_DIV_ROUND_UP(60 * coeff, ui) - 2;
	timing->clk_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	temp = 85 * coeff + 6 * ui;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 40 * coeff + 4 * ui;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_prepare = linear_inter(tmax, tmin, pcnt1, 0, true);

	tmax = 255;
	temp = ((timing->hs_prepare >> 1) + 1) * 2 * ui + 2 * ui;
	temp = 145 * coeff + 10 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_zero = linear_inter(tmax, tmin, pcnt2, 24, true);

	temp = 105 * coeff + 12 * ui - 20 * coeff;
	tmax = S_DIV_ROUND_UP(temp, ui) - 2;
	temp = 60 * coeff + 4 * ui;
	tmin = DIV_ROUND_UP(temp, ui) - 2;
	timing->hs_trail = linear_inter(tmax, tmin, pcnt3, 0, true);

	tmax = 255;
	tmin = S_DIV_ROUND_UP(100 * coeff, ui) - 2;
	timing->hs_exit = linear_inter(tmax, tmin, pcnt2, 0, true);

	tmax = 63;
	temp = ((timing->hs_exit >> 1) + 1) * 2 * ui;
	temp = 60 * coeff + 52 * ui - 24 * ui - temp;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	timing->clk_post = linear_inter(tmax, tmin, pcnt2, 0, false);

	tmax = 63;
	temp = ((timing->clk_prepare >> 1) + 1) * 2 * ui;
	temp += ((timing->clk_zero >> 1) + 1) * 2 * ui;
	temp += 8 * ui + lpx;
	tmin = S_DIV_ROUND_UP(temp, 8 * ui) - 1;
	if (tmin > tmax) {
		temp = linear_inter(2 * tmax, tmin, pcnt2, 0, false);
		timing->clk_pre = temp >> 1;
	} else {
		timing->clk_pre = linear_inter(tmax, tmin, pcnt2, 0, false);
	}

	timing->ta_go = 3;
	timing->ta_sure = 0;
	timing->ta_get = 4;

	DBG("PHY timings: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
		timing->clk_pre, timing->clk_post, timing->clk_zero,
		timing->clk_trail, timing->clk_prepare, timing->hs_exit,
		timing->hs_zero, timing->hs_prepare, timing->hs_trail,
		timing->hs_rqst);

	return 0;
}

void msm_dsi_phy_set_src_pll(struct msm_dsi_phy *phy, int pll_id, u32 reg,
				u32 bit_mask)
{
	int phy_id = phy->id;
	u32 val;

	if ((phy_id >= DSI_MAX) || (pll_id >= DSI_MAX))
		return;

	val = dsi_phy_read(phy->base + reg);

	if (phy->cfg->src_pll_truthtable[phy_id][pll_id])
		dsi_phy_write(phy->base + reg, val | bit_mask);
	else
		dsi_phy_write(phy->base + reg, val & (~bit_mask));
}

static int dsi_phy_regulator_init(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	struct device *dev = &phy->pdev->dev;
	int num = phy->cfg->reg_cfg.num;
	int i, ret;

	for (i = 0; i < num; i++)
		s[i].supply = regs[i].name;

	ret = devm_regulator_bulk_get(dev, num, s);
	if (ret < 0) {
		dev_err(dev, "%s: failed to init regulator, ret=%d\n",
						__func__, ret);
		return ret;
	}

	return 0;
}

static void dsi_phy_regulator_disable(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	int num = phy->cfg->reg_cfg.num;
	int i;

	DBG("");
	for (i = num - 1; i >= 0; i--)
		if (regs[i].disable_load >= 0)
			regulator_set_load(s[i].consumer, regs[i].disable_load);

	regulator_bulk_disable(num, s);
}

static int dsi_phy_regulator_enable(struct msm_dsi_phy *phy)
{
	struct regulator_bulk_data *s = phy->supplies;
	const struct dsi_reg_entry *regs = phy->cfg->reg_cfg.regs;
	struct device *dev = &phy->pdev->dev;
	int num = phy->cfg->reg_cfg.num;
	int ret, i;

	DBG("");
	for (i = 0; i < num; i++) {
		if (regs[i].enable_load >= 0) {
			ret = regulator_set_load(s[i].consumer,
							regs[i].enable_load);
			if (ret < 0) {
				dev_err(dev,
					"regulator %d set op mode failed, %d\n",
					i, ret);
				goto fail;
			}
		}
	}

	ret = regulator_bulk_enable(num, s);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		regulator_set_load(s[i].consumer, regs[i].disable_load);
	return ret;
}

static int dsi_phy_enable_resource(struct msm_dsi_phy *phy)
{
	struct device *dev = &phy->pdev->dev;
	int ret;

	pm_runtime_get_sync(dev);

	ret = clk_prepare_enable(phy->ahb_clk);
	if (ret) {
		dev_err(dev, "%s: can't enable ahb clk, %d\n", __func__, ret);
		pm_runtime_put_sync(dev);
	}

	return ret;
}

static void dsi_phy_disable_resource(struct msm_dsi_phy *phy)
{
	clk_disable_unprepare(phy->ahb_clk);
	pm_runtime_put_sync(&phy->pdev->dev);
}

static const struct of_device_id dsi_phy_dt_match[] = {
#ifdef CONFIG_DRM_MSM_DSI_28NM_PHY
	{ .compatible = "qcom,dsi-phy-28nm-hpm",
	  .data = &dsi_phy_28nm_hpm_cfgs },
	{ .compatible = "qcom,dsi-phy-28nm-lp",
	  .data = &dsi_phy_28nm_lp_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_20NM_PHY
	{ .compatible = "qcom,dsi-phy-20nm",
	  .data = &dsi_phy_20nm_cfgs },
#endif
#ifdef CONFIG_DRM_MSM_DSI_28NM_8960_PHY
	{ .compatible = "qcom,dsi-phy-28nm-8960",
	  .data = &dsi_phy_28nm_8960_cfgs },
#endif
	{}
};

/*
 * Currently, we only support one SoC for each PHY type. When we have multiple
 * SoCs for the same PHY, we can try to make the index searching a bit more
 * clever.
 */
static int dsi_phy_get_id(struct msm_dsi_phy *phy)
{
	struct platform_device *pdev = phy->pdev;
	const struct msm_dsi_phy_cfg *cfg = phy->cfg;
	struct resource *res;
	int i;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dsi_phy");
	if (!res)
		return -EINVAL;

	for (i = 0; i < cfg->num_dsi_phy; i++) {
		if (cfg->io_start[i] == res->start)
			return i;
	}

	return -EINVAL;
}

static int dsi_phy_driver_probe(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	match = of_match_node(dsi_phy_dt_match, dev->of_node);
	if (!match)
		return -ENODEV;

	phy->cfg = match->data;
	phy->pdev = pdev;

	phy->id = dsi_phy_get_id(phy);
	if (phy->id < 0) {
		ret = phy->id;
		dev_err(dev, "%s: couldn't identify PHY index, %d\n",
			__func__, ret);
		goto fail;
	}

	phy->regulator_ldo_mode = of_property_read_bool(dev->of_node,
				"qcom,dsi-phy-regulator-ldo-mode");

	phy->base = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR(phy->base)) {
		dev_err(dev, "%s: failed to map phy base\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	phy->reg_base = msm_ioremap(pdev, "dsi_phy_regulator",
				"DSI_PHY_REG");
	if (IS_ERR(phy->reg_base)) {
		dev_err(dev, "%s: failed to map phy regulator base\n",
			__func__);
		ret = -ENOMEM;
		goto fail;
	}

	ret = dsi_phy_regulator_init(phy);
	if (ret) {
		dev_err(dev, "%s: failed to init regulator\n", __func__);
		goto fail;
	}

	phy->ahb_clk = devm_clk_get(dev, "iface_clk");
	if (IS_ERR(phy->ahb_clk)) {
		dev_err(dev, "%s: Unable to get ahb clk\n", __func__);
		ret = PTR_ERR(phy->ahb_clk);
		goto fail;
	}

	/* PLL init will call into clk_register which requires
	 * register access, so we need to enable power and ahb clock.
	 */
	ret = dsi_phy_enable_resource(phy);
	if (ret)
		goto fail;

	phy->pll = msm_dsi_pll_init(pdev, phy->cfg->type, phy->id);
	if (!phy->pll)
		dev_info(dev,
			"%s: pll init failed, need separate pll clk driver\n",
			__func__);

	dsi_phy_disable_resource(phy);

	platform_set_drvdata(pdev, phy);

	return 0;

fail:
	return ret;
}

static int dsi_phy_driver_remove(struct platform_device *pdev)
{
	struct msm_dsi_phy *phy = platform_get_drvdata(pdev);

	if (phy && phy->pll) {
		msm_dsi_pll_destroy(phy->pll);
		phy->pll = NULL;
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver dsi_phy_platform_driver = {
	.probe      = dsi_phy_driver_probe,
	.remove     = dsi_phy_driver_remove,
	.driver     = {
		.name   = "msm_dsi_phy",
		.of_match_table = dsi_phy_dt_match,
	},
};

void __init msm_dsi_phy_driver_register(void)
{
	platform_driver_register(&dsi_phy_platform_driver);
}

void __exit msm_dsi_phy_driver_unregister(void)
{
	platform_driver_unregister(&dsi_phy_platform_driver);
}

int msm_dsi_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
	const unsigned long bit_rate, const unsigned long esc_rate)
{
	struct device *dev = &phy->pdev->dev;
	int ret;

	if (!phy || !phy->cfg->ops.enable)
		return -EINVAL;

	ret = dsi_phy_regulator_enable(phy);
	if (ret) {
		dev_err(dev, "%s: regulator enable failed, %d\n",
			__func__, ret);
		return ret;
	}

	ret = phy->cfg->ops.enable(phy, src_pll_id, bit_rate, esc_rate);
	if (ret) {
		dev_err(dev, "%s: phy enable failed, %d\n", __func__, ret);
		dsi_phy_regulator_disable(phy);
		return ret;
	}

	return 0;
}

void msm_dsi_phy_disable(struct msm_dsi_phy *phy)
{
	if (!phy || !phy->cfg->ops.disable)
		return;

	phy->cfg->ops.disable(phy);

	dsi_phy_regulator_disable(phy);
}

void msm_dsi_phy_get_clk_pre_post(struct msm_dsi_phy *phy,
					u32 *clk_pre, u32 *clk_post)
{
	if (!phy)
		return;

	if (clk_pre)
		*clk_pre = phy->timing.clk_pre;
	if (clk_post)
		*clk_post = phy->timing.clk_post;
}

struct msm_dsi_pll *msm_dsi_phy_get_pll(struct msm_dsi_phy *phy)
{
	if (!phy)
		return NULL;

	return phy->pll;
}

