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

#include "dsi.h"

struct drm_encoder *msm_dsi_get_encoder(struct msm_dsi *msm_dsi)
{
	if (!msm_dsi || !msm_dsi_device_connected(msm_dsi))
		return NULL;

	return (msm_dsi->device_flags & MIPI_DSI_MODE_VIDEO) ?
		msm_dsi->encoders[MSM_DSI_VIDEO_ENCODER_ID] :
		msm_dsi->encoders[MSM_DSI_CMD_ENCODER_ID];
}

static int dsi_get_phy(struct msm_dsi *msm_dsi)
{
	struct platform_device *pdev = msm_dsi->pdev;
	struct platform_device *phy_pdev;
	struct device_node *phy_node;

	phy_node = of_parse_phandle(pdev->dev.of_node, "phys", 0);
	if (!phy_node) {
		dev_err(&pdev->dev, "cannot find phy device\n");
		return -ENXIO;
	}

	phy_pdev = of_find_device_by_node(phy_node);
	if (phy_pdev)
		msm_dsi->phy = platform_get_drvdata(phy_pdev);

	of_node_put(phy_node);

	if (!phy_pdev || !msm_dsi->phy) {
		dev_err(&pdev->dev, "%s: phy driver is not ready\n", __func__);
		return -EPROBE_DEFER;
	}

	msm_dsi->phy_dev = get_device(&phy_pdev->dev);

	return 0;
}

static void dsi_destroy(struct msm_dsi *msm_dsi)
{
	if (!msm_dsi)
		return;

	msm_dsi_manager_unregister(msm_dsi);

	if (msm_dsi->phy_dev) {
		put_device(msm_dsi->phy_dev);
		msm_dsi->phy = NULL;
		msm_dsi->phy_dev = NULL;
	}

	if (msm_dsi->host) {
		msm_dsi_host_destroy(msm_dsi->host);
		msm_dsi->host = NULL;
	}

	platform_set_drvdata(msm_dsi->pdev, NULL);
}

static struct msm_dsi *dsi_init(struct platform_device *pdev)
{
	struct msm_dsi *msm_dsi;
	int ret;

	if (!pdev)
		return ERR_PTR(-ENXIO);

	msm_dsi = devm_kzalloc(&pdev->dev, sizeof(*msm_dsi), GFP_KERNEL);
	if (!msm_dsi)
		return ERR_PTR(-ENOMEM);
	DBG("dsi probed=%p", msm_dsi);

	msm_dsi->pdev = pdev;
	platform_set_drvdata(pdev, msm_dsi);

	/* Init dsi host */
	ret = msm_dsi_host_init(msm_dsi);
	if (ret)
		goto destroy_dsi;

	/* GET dsi PHY */
	ret = dsi_get_phy(msm_dsi);
	if (ret)
		goto destroy_dsi;

	/* Register to dsi manager */
	ret = msm_dsi_manager_register(msm_dsi);
	if (ret)
		goto destroy_dsi;

	return msm_dsi;

destroy_dsi:
	dsi_destroy(msm_dsi);
	return ERR_PTR(ret);
}

static int dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_dsi *msm_dsi;

	DBG("");
	msm_dsi = dsi_init(pdev);
	if (IS_ERR(msm_dsi))
		return PTR_ERR(msm_dsi);

	priv->dsi[msm_dsi->id] = msm_dsi;

	return 0;
}

static void dsi_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct msm_dsi *msm_dsi = dev_get_drvdata(dev);
	int id = msm_dsi->id;

	if (priv->dsi[id]) {
		dsi_destroy(msm_dsi);
		priv->dsi[id] = NULL;
	}
}

static const struct component_ops dsi_ops = {
	.bind   = dsi_bind,
	.unbind = dsi_unbind,
};

static int dsi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dsi_ops);
}

static int dsi_dev_remove(struct platform_device *pdev)
{
	DBG("");
	component_del(&pdev->dev, &dsi_ops);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,mdss-dsi-ctrl" },
	{}
};

static struct platform_driver dsi_driver = {
	.probe = dsi_dev_probe,
	.remove = dsi_dev_remove,
	.driver = {
		.name = "msm_dsi",
		.of_match_table = dt_match,
	},
};

void __init msm_dsi_register(void)
{
	DBG("");
	msm_dsi_phy_driver_register();
	platform_driver_register(&dsi_driver);
}

void __exit msm_dsi_unregister(void)
{
	DBG("");
	msm_dsi_phy_driver_unregister();
	platform_driver_unregister(&dsi_driver);
}

int msm_dsi_modeset_init(struct msm_dsi *msm_dsi, struct drm_device *dev,
		struct drm_encoder *encoders[MSM_DSI_ENCODER_NUM])
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_bridge *ext_bridge;
	int ret, i;

	if (WARN_ON(!encoders[MSM_DSI_VIDEO_ENCODER_ID] ||
		!encoders[MSM_DSI_CMD_ENCODER_ID]))
		return -EINVAL;

	msm_dsi->dev = dev;

	ret = msm_dsi_host_modeset_init(msm_dsi->host, dev);
	if (ret) {
		dev_err(dev->dev, "failed to modeset init host: %d\n", ret);
		goto fail;
	}

	msm_dsi->bridge = msm_dsi_manager_bridge_init(msm_dsi->id);
	if (IS_ERR(msm_dsi->bridge)) {
		ret = PTR_ERR(msm_dsi->bridge);
		dev_err(dev->dev, "failed to create dsi bridge: %d\n", ret);
		msm_dsi->bridge = NULL;
		goto fail;
	}

	for (i = 0; i < MSM_DSI_ENCODER_NUM; i++) {
		encoders[i]->bridge = msm_dsi->bridge;
		msm_dsi->encoders[i] = encoders[i];
	}

	/*
	 * check if the dsi encoder output is connected to a panel or an
	 * external bridge. We create a connector only if we're connected to a
	 * drm_panel device. When we're connected to an external bridge, we
	 * assume that the drm_bridge driver will create the connector itself.
	 */
	ext_bridge = msm_dsi_host_get_bridge(msm_dsi->host);

	if (ext_bridge)
		msm_dsi->connector =
			msm_dsi_manager_ext_bridge_init(msm_dsi->id);
	else
		msm_dsi->connector =
			msm_dsi_manager_connector_init(msm_dsi->id);

	if (IS_ERR(msm_dsi->connector)) {
		ret = PTR_ERR(msm_dsi->connector);
		dev_err(dev->dev,
			"failed to create dsi connector: %d\n", ret);
		msm_dsi->connector = NULL;
		goto fail;
	}

	priv->bridges[priv->num_bridges++]       = msm_dsi->bridge;
	priv->connectors[priv->num_connectors++] = msm_dsi->connector;

	return 0;
fail:
	if (msm_dsi) {
		/* bridge/connector are normally destroyed by drm: */
		if (msm_dsi->bridge) {
			msm_dsi_manager_bridge_destroy(msm_dsi->bridge);
			msm_dsi->bridge = NULL;
		}

		/* don't destroy connector if we didn't make it */
		if (msm_dsi->connector && !msm_dsi->external_bridge)
			msm_dsi->connector->funcs->destroy(msm_dsi->connector);

		msm_dsi->connector = NULL;
	}

	return ret;
}

