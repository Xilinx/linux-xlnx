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

#ifndef __DSI_CONNECTOR_H__
#define __DSI_CONNECTOR_H__

#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "drm_crtc.h"
#include "drm_mipi_dsi.h"
#include "drm_panel.h"

#include "msm_drv.h"

#define DSI_0	0
#define DSI_1	1
#define DSI_MAX	2

enum msm_dsi_phy_type {
	MSM_DSI_PHY_28NM_HPM,
	MSM_DSI_PHY_28NM_LP,
	MSM_DSI_PHY_20NM,
	MSM_DSI_PHY_28NM_8960,
	MSM_DSI_PHY_MAX
};

#define DSI_DEV_REGULATOR_MAX	8
#define DSI_BUS_CLK_MAX		4

/* Regulators for DSI devices */
struct dsi_reg_entry {
	char name[32];
	int enable_load;
	int disable_load;
};

struct dsi_reg_config {
	int num;
	struct dsi_reg_entry regs[DSI_DEV_REGULATOR_MAX];
};

struct msm_dsi {
	struct drm_device *dev;
	struct platform_device *pdev;

	/* connector managed by us when we're connected to a drm_panel */
	struct drm_connector *connector;
	/* internal dsi bridge attached to MDP interface */
	struct drm_bridge *bridge;

	struct mipi_dsi_host *host;
	struct msm_dsi_phy *phy;

	/*
	 * panel/external_bridge connected to dsi bridge output, only one of the
	 * two can be valid at a time
	 */
	struct drm_panel *panel;
	struct drm_bridge *external_bridge;
	unsigned long device_flags;

	struct device *phy_dev;
	bool phy_enabled;

	/* the encoders we are hooked to (outside of dsi block) */
	struct drm_encoder *encoders[MSM_DSI_ENCODER_NUM];

	int id;
};

/* dsi manager */
struct drm_bridge *msm_dsi_manager_bridge_init(u8 id);
void msm_dsi_manager_bridge_destroy(struct drm_bridge *bridge);
struct drm_connector *msm_dsi_manager_connector_init(u8 id);
struct drm_connector *msm_dsi_manager_ext_bridge_init(u8 id);
int msm_dsi_manager_phy_enable(int id,
		const unsigned long bit_rate, const unsigned long esc_rate,
		u32 *clk_pre, u32 *clk_post);
void msm_dsi_manager_phy_disable(int id);
int msm_dsi_manager_cmd_xfer(int id, const struct mipi_dsi_msg *msg);
bool msm_dsi_manager_cmd_xfer_trigger(int id, u32 dma_base, u32 len);
int msm_dsi_manager_register(struct msm_dsi *msm_dsi);
void msm_dsi_manager_unregister(struct msm_dsi *msm_dsi);

/* msm dsi */
static inline bool msm_dsi_device_connected(struct msm_dsi *msm_dsi)
{
	return msm_dsi->panel || msm_dsi->external_bridge;
}

struct drm_encoder *msm_dsi_get_encoder(struct msm_dsi *msm_dsi);

/* dsi pll */
struct msm_dsi_pll;
#ifdef CONFIG_DRM_MSM_DSI_PLL
struct msm_dsi_pll *msm_dsi_pll_init(struct platform_device *pdev,
			enum msm_dsi_phy_type type, int dsi_id);
void msm_dsi_pll_destroy(struct msm_dsi_pll *pll);
int msm_dsi_pll_get_clk_provider(struct msm_dsi_pll *pll,
	struct clk **byte_clk_provider, struct clk **pixel_clk_provider);
void msm_dsi_pll_save_state(struct msm_dsi_pll *pll);
int msm_dsi_pll_restore_state(struct msm_dsi_pll *pll);
#else
static inline struct msm_dsi_pll *msm_dsi_pll_init(struct platform_device *pdev,
			 enum msm_dsi_phy_type type, int id) {
	return ERR_PTR(-ENODEV);
}
static inline void msm_dsi_pll_destroy(struct msm_dsi_pll *pll)
{
}
static inline int msm_dsi_pll_get_clk_provider(struct msm_dsi_pll *pll,
	struct clk **byte_clk_provider, struct clk **pixel_clk_provider)
{
	return -ENODEV;
}
static inline void msm_dsi_pll_save_state(struct msm_dsi_pll *pll)
{
}
static inline int msm_dsi_pll_restore_state(struct msm_dsi_pll *pll)
{
	return 0;
}
#endif

/* dsi host */
int msm_dsi_host_xfer_prepare(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_xfer_restore(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_tx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_rx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_cmd_xfer_commit(struct mipi_dsi_host *host,
					u32 dma_base, u32 len);
int msm_dsi_host_enable(struct mipi_dsi_host *host);
int msm_dsi_host_disable(struct mipi_dsi_host *host);
int msm_dsi_host_power_on(struct mipi_dsi_host *host);
int msm_dsi_host_power_off(struct mipi_dsi_host *host);
int msm_dsi_host_set_display_mode(struct mipi_dsi_host *host,
					struct drm_display_mode *mode);
struct drm_panel *msm_dsi_host_get_panel(struct mipi_dsi_host *host,
					unsigned long *panel_flags);
struct drm_bridge *msm_dsi_host_get_bridge(struct mipi_dsi_host *host);
int msm_dsi_host_register(struct mipi_dsi_host *host, bool check_defer);
void msm_dsi_host_unregister(struct mipi_dsi_host *host);
int msm_dsi_host_set_src_pll(struct mipi_dsi_host *host,
			struct msm_dsi_pll *src_pll);
void msm_dsi_host_destroy(struct mipi_dsi_host *host);
int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev);
int msm_dsi_host_init(struct msm_dsi *msm_dsi);

/* dsi phy */
struct msm_dsi_phy;
void msm_dsi_phy_driver_register(void);
void msm_dsi_phy_driver_unregister(void);
int msm_dsi_phy_enable(struct msm_dsi_phy *phy, int src_pll_id,
	const unsigned long bit_rate, const unsigned long esc_rate);
void msm_dsi_phy_disable(struct msm_dsi_phy *phy);
void msm_dsi_phy_get_clk_pre_post(struct msm_dsi_phy *phy,
					u32 *clk_pre, u32 *clk_post);
struct msm_dsi_pll *msm_dsi_phy_get_pll(struct msm_dsi_phy *phy);

#endif /* __DSI_CONNECTOR_H__ */

