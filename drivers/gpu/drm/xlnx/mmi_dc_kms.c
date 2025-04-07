// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated Display Controller Driver
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_plane.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "mmi_dc.h"

/* Enable/Disable Writeback through PL feedback path */
static bool wb;
module_param(wb, bool, 0600);

/**
 * struct mmi_dc_drm - MMI DC DRM pipeline
 * @dc: MMI DC device
 * @drm: DRM device
 * @crtc: DRM CRTC
 * @encoder: DRM encoder
 * @bridge: DRM chain pointer
 */
struct mmi_dc_drm {
	struct mmi_dc	*dc;

	struct drm_device	drm;
	struct drm_crtc		crtc;
	struct drm_encoder	encoder;
	struct drm_bridge	*bridge;
};

/**
 * drm_to_dc - Get DC device pointer from DRM device
 * @drm: DRM device
 *
 * Return: Corresponding MMI DC device
 */
static inline struct mmi_dc *drm_to_dc(struct drm_device *drm)
{
	return container_of(drm, struct mmi_dc_drm, drm)->dc;
}

/* ----------------------------------------------------------------------------
 * Power Management
 */

static int __maybe_unused mmi_dc_suspend(struct device *dev)
{
	struct mmi_dc *dc = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&dc->drm->drm);
}

static int __maybe_unused mmi_dc_resume(struct device *dev)
{
	struct mmi_dc *dc = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&dc->drm->drm);
}

static const struct dev_pm_ops mmi_dc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mmi_dc_suspend, mmi_dc_resume)
};

/* ----------------------------------------------------------------------------
 * DRM CRTC
 */

/**
 * crtc_to_dc - Get DC device pointer from DRM CRTC
 * @crtc: DRM CRTC
 *
 * Return: Corresponding MMI DC device
 */
static inline struct mmi_dc *crtc_to_dc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct mmi_dc_drm, crtc)->dc;
}

/**
 * mmi_dc_drm_handle_vblank - Handle VBLANK notification
 * @drm: pointer to MMI DC DRM
 *
 * Return: Corresponding MMI DC device
 */
void mmi_dc_drm_handle_vblank(struct mmi_dc_drm *drm)
{
	drm_crtc_handle_vblank(&drm->crtc);
}

static void mmi_dc_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct mmi_dc *dc = crtc_to_dc(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int vrefresh;

	pm_runtime_get_sync(dc->dev);

	/* TODO: setup DC clock */

	mmi_dc_enable(dc, adjusted_mode);

	/* TODO: Do we need this? */
	/* Delay of 3 vblank intervals for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(MMI_DC_VBLANKS * 1000 / vrefresh);
}

static void mmi_dc_crtc_atomic_disable(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct mmi_dc *dc = crtc_to_dc(crtc);

	mmi_dc_disable(dc);

	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	/* TODO: disable video clock */
	pm_runtime_put_sync(dc->dev);
}

static int mmi_dc_crtc_atomic_check(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct mmi_dc *dc = crtc_to_dc(crtc);
	struct drm_plane *primary_plane = mmi_dc_plane_get_primary(dc);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_plane_state *primary_plane_state =
		drm_atomic_get_new_plane_state(state, primary_plane);

	if (crtc_state && primary_plane_state && crtc_state->active &&
	    !primary_plane_state->fb)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state, crtc);
}

static void mmi_dc_crtc_atomic_begin(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void mmi_dc_crtc_atomic_flush(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_pending_vblank_event *vblank;

	if (!crtc->state->event)
		return;

	/* Consume the flip_done event from atomic helper. */
	vblank = crtc->state->event;
	crtc->state->event = NULL;

	vblank->pipe = drm_crtc_index(crtc);

	WARN_ON(drm_crtc_vblank_get(crtc) != 0);

	spin_lock_irq(&crtc->dev->event_lock);
	drm_crtc_arm_vblank_event(crtc, vblank);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_crtc_helper_funcs mmi_dc_crtc_helper_funcs = {
	.atomic_enable	= mmi_dc_crtc_atomic_enable,
	.atomic_disable	= mmi_dc_crtc_atomic_disable,
	.atomic_check	= mmi_dc_crtc_atomic_check,
	.atomic_begin	= mmi_dc_crtc_atomic_begin,
	.atomic_flush	= mmi_dc_crtc_atomic_flush,
};

static int mmi_dc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct mmi_dc *dc = crtc_to_dc(crtc);

	mmi_dc_enable_vblank(dc);

	return 0;
}

static void mmi_dc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct mmi_dc *dc = crtc_to_dc(crtc);

	mmi_dc_disable_vblank(dc);
}

static const struct drm_crtc_funcs mmi_dc_dpsub_crtc_funcs = {
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= mmi_dc_crtc_enable_vblank,
	.disable_vblank		= mmi_dc_crtc_disable_vblank,
};

/**
 * mmi_dc_create_crtc - Create DRM CRTC interface for MMI DC
 * @dc: MMI DC device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_dc_create_crtc(struct mmi_dc *dc)
{
	struct drm_plane *plane = mmi_dc_plane_get_primary(dc);
	struct drm_crtc *crtc = &dc->drm->crtc;
	int ret;

	/* TODO cursor plane */
	ret = drm_crtc_init_with_planes(&dc->drm->drm, crtc, plane, NULL,
					&mmi_dc_dpsub_crtc_funcs, NULL);
	if (ret < 0) {
		dev_err(dc->dev, "failed to init DRM CRTC: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(crtc, &mmi_dc_crtc_helper_funcs);

	drm_crtc_vblank_off(crtc);

	return 0;
}

/* ----------------------------------------------------------------------------
 * DRM Encoder
 */

/**
 * mmi_create_encoder - Create DRM encoder interface for MMI DC
 * @dc: MMI DC device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_create_encoder(struct mmi_dc *dc)
{
	struct mmi_dc_drm *dc_drm = dc->drm;
	struct drm_device *drm = &dc_drm->drm;
	struct drm_encoder *encoder = &dc_drm->encoder;
	struct drm_bridge *bridge;
	enum drm_bridge_attach_flags attach_flags = 0;
	int ret;

	encoder->possible_crtcs |= drm_crtc_mask(&dc_drm->crtc);
	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_NONE);
	if (ret < 0) {
		dev_err(dc->dev, "failed to init encoder: %d\n", ret);
		return ret;
	}

	dc_drm->bridge = devm_drm_of_get_bridge(dc->dev,
						dc->dev->of_node,
						MMI_DC_DPTX_PORT_0, 0);
	if (IS_ERR(dc_drm->bridge))
		return dev_err_probe(dc->dev, PTR_ERR(dc_drm->bridge),
				     "failed to find bridge\n");

	bridge = dc_drm->bridge;

	if (!wb)
		attach_flags = DRM_BRIDGE_ATTACH_NO_CONNECTOR;

	ret = drm_bridge_attach(encoder, bridge, NULL, attach_flags);
	if (ret < 0) {
		dev_err(dc->dev, "failed to attach bridge: %d\n", ret);
		return ret;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * DRM Connector
 */

/**
 * mmi_dc_setup_connector - Setup DRM connector interface for MMI DC
 * @dc: MMI DC device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_dc_setup_connector(struct mmi_dc *dc)
{
	struct mmi_dc_drm *dc_drm = dc->drm;
	struct drm_device *drm = &dc_drm->drm;
	struct drm_encoder *encoder = &dc_drm->encoder;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int ret;

	if (wb) {
		drm_connector_list_iter_begin(drm, &iter);
		drm_for_each_connector_iter(connector, &iter) {
			if (connector->connector_type ==
				DRM_MODE_CONNECTOR_WRITEBACK) {
				drm_connector_list_iter_end(&iter);
				return 0;
			}
		}
		drm_connector_list_iter_end(&iter);
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		dev_err(dc->dev, "failed to init connector: %d\n", ret);
		return ret;
	}

	return drm_connector_attach_encoder(connector, encoder);
}

/* ----------------------------------------------------------------------------
 * Buffers Allocation
 */

static int mmi_dc_dumb_create(struct drm_file *file_priv,
			      struct drm_device *drm,
			      struct drm_mode_create_dumb *args)
{
	struct mmi_dc *dc = drm_to_dc(drm);
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/* Enforce the alignment constraints of the DMA engine. */
	args->pitch = ALIGN(pitch, dc->dma_align);

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

static struct drm_framebuffer *
mmi_dc_fb_create(struct drm_device *drm, struct drm_file *file_priv,
		 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct mmi_dc *dc = drm_to_dc(drm);
	struct drm_mode_fb_cmd2 cmd = *mode_cmd;
	unsigned int i;

	/* Enforce the alignment constraints of the DMA engine. */
	for (i = 0; i < ARRAY_SIZE(cmd.pitches); ++i)
		cmd.pitches[i] = ALIGN(cmd.pitches[i], dc->dma_align);

	return drm_gem_fb_create(drm, file_priv, &cmd);
}

static const struct drm_mode_config_funcs mmi_dc_mode_config_funcs = {
	.fb_create		= mmi_dc_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

/* ----------------------------------------------------------------------------
 * DRM Driver
 */

DEFINE_DRM_GEM_DMA_FOPS(mmi_dc_drm_fops);

static const struct drm_driver mmi_dc_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(mmi_dc_dumb_create),
	/* TODO: fbdev emulation */
	.fops			= &mmi_dc_drm_fops,
	.name			= "mmi-dc",
	.desc			= "MMI Display Controller Driver",
	.date			= "20241226",
	.major			= 0,
	.minor			= 1,
};

/**
 * mmi_dc_drm_pipeline_init - Initialize DRM pipeline
 * @dc: MMI DC device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_dc_drm_pipeline_init(struct mmi_dc *dc)
{
	struct mmi_dc_drm *dc_drm = dc->drm;
	struct drm_device *drm = &dc_drm->drm;
	int ret;

	ret = mmi_dc_create_crtc(dc);
	if (ret < 0)
		return ret;

	mmi_dc_planes_set_possible_crtc(dc, drm_crtc_mask(&dc_drm->crtc));

	ret = mmi_create_encoder(dc);
	if (ret < 0)
		return ret;

	ret = mmi_dc_setup_connector(dc);
	if (ret < 0)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0) {
		dev_err(dc->dev, "failed to register DRM device: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * mmi_dc_drm_init - Initialize DRM subsystem
 * @dc: MMI DC device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_dc_drm_init(struct mmi_dc *dc)
{
	struct mmi_dc_drm *dc_drm;
	struct drm_device *drm;
	int ret;

	dc_drm = devm_drm_dev_alloc(dc->dev, &mmi_dc_drm_driver,
				    struct mmi_dc_drm, drm);
	if (IS_ERR(dc_drm)) {
		ret = PTR_ERR(dc_drm);
		dev_err(dc->dev, "failed to allocate DRM: %d\n", ret);
		return ret;
	}
	drm = &dc_drm->drm;

	dc_drm->dc = dc;
	dc->drm = dc_drm;

	ret = drmm_mode_config_init(drm);
	if (ret < 0) {
		dev_err(dc->dev, "failed to init mode config: %d\n", ret);
		return ret;
	}

	drm->mode_config.funcs = &mmi_dc_mode_config_funcs;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = MMI_DC_MAX_WIDTH;
	drm->mode_config.max_height = MMI_DC_MAX_HEIGHT;

	ret = drm_vblank_init(drm, 1);
	if (ret < 0) {
		dev_err(dc->dev, "failed to init vblank: %d\n", ret);
		return ret;
	}

	drm_kms_helper_poll_init(drm);

	return 0;
}

/**
 * mmi_dc_probe - Probe MMI DC device
 * @pdev: the platform device
 *
 * Return: 0 on success or error code otherwise
 */
static int mmi_dc_probe(struct platform_device *pdev)
{
	struct mmi_dc *dc;
	int ret;

	dc = devm_kzalloc(&pdev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	platform_set_drvdata(pdev, dc);
	dc->dev = &pdev->dev;

	ret = mmi_dc_drm_init(dc);
	if (ret < 0)
		return ret;

	ret = mmi_dc_init(dc, &dc->drm->drm);
	if (ret < 0)
		return ret;

	ret = mmi_dc_drm_pipeline_init(dc);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * mmi_dc_remove - Remove MMI DC device
 * @pdev: the platform device
 */
static void mmi_dc_remove(struct platform_device *pdev)
{
	struct mmi_dc *dc = dev_get_drvdata(&pdev->dev);
	struct drm_device *drm = &dc->drm->drm;

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
	drm_encoder_cleanup(&dc->drm->encoder);
	drm_kms_helper_poll_fini(drm);

	mmi_dc_fini(dc);
}

static const struct of_device_id mmi_dc_of_match[] = {
	{ .compatible = "amd,mmi-dc-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, mmi_dc_of_match);

static struct platform_driver mmi_dc_driver = {
	.probe			= mmi_dc_probe,
	.remove_new		= mmi_dc_remove,
	.driver			= {
		.name		= "mmi-dc",
		.pm		= &mmi_dc_pm_ops,
		.of_match_table	= mmi_dc_of_match,
	},
};

drm_module_platform_driver(mmi_dc_driver);

MODULE_DESCRIPTION("MMI Display Controller Driver");
MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_LICENSE("GPL");
