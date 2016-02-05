/*
 * Xilinx DRM KMS support for Xilinx
 *
 *  Copyright (C) 2013 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "xilinx_drm_connector.h"
#include "xilinx_drm_crtc.h"
#include "xilinx_drm_drv.h"
#include "xilinx_drm_encoder.h"
#include "xilinx_drm_fb.h"
#include "xilinx_drm_gem.h"

#define DRIVER_NAME	"xilinx_drm"
#define DRIVER_DESC	"Xilinx DRM KMS support for Xilinx"
#define DRIVER_DATE	"20130509"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static uint xilinx_drm_fbdev_vres = 2;
module_param_named(fbdev_vres, xilinx_drm_fbdev_vres, uint, 0444);
MODULE_PARM_DESC(fbdev_vres,
		 "fbdev virtual resolution multiplier for fb (default: 2)");

/*
 * TODO: The possible pipeline configurations are numerous with Xilinx soft IPs.
 * It's not too bad for now, but the more proper way(Common Display Framework,
 * or some internal abstraction) should be considered, when it reaches a point
 * that such thing is required.
 */

struct xilinx_drm_private {
	struct drm_device *drm;
	struct drm_crtc *crtc;
	struct drm_fb_helper *fb;
	struct platform_device *pdev;
};

/**
 * struct xilinx_video_format_desc - Xilinx Video IP video format description
 * @name: Xilinx video format name
 * @depth: color depth
 * @bpp: bits per pixel
 * @xilinx_format: xilinx format code
 * @drm_format: drm format code
 */
struct xilinx_video_format_desc {
	const char *name;
	unsigned int depth;
	unsigned int bpp;
	unsigned int xilinx_format;
	uint32_t drm_format;
};

static const struct xilinx_video_format_desc xilinx_video_formats[] = {
	{ "yuv422", 16, 16, XILINX_VIDEO_FORMAT_YUV422, DRM_FORMAT_YUYV },
	{ "yuv444", 24, 24, XILINX_VIDEO_FORMAT_YUV444, DRM_FORMAT_YUV444 },
	{ "rgb888", 24, 24, XILINX_VIDEO_FORMAT_RGB, DRM_FORMAT_RGB888 },
	{ "yuv420", 16, 16, XILINX_VIDEO_FORMAT_YUV420, DRM_FORMAT_YUV420 },
	{ "xrgb8888", 24, 32, XILINX_VIDEO_FORMAT_XRGB, DRM_FORMAT_XRGB8888 },
	{ "rgba8888", 32, 32, XILINX_VIDEO_FORMAT_NONE, DRM_FORMAT_RGBA8888 },
	{ "rgb565", 16, 16, XILINX_VIDEO_FORMAT_NONE, DRM_FORMAT_RGB565 },
	{ "xbgr8888", 24, 32, XILINX_VIDEO_FORMAT_NONE, DRM_FORMAT_XBGR8888 },
	{ "abgr8888", 32, 32, XILINX_VIDEO_FORMAT_NONE, DRM_FORMAT_ABGR8888 },
};

/**
 * xilinx_drm_check_format - Check if the given format is supported
 * @drm: DRM device
 * @fourcc: format fourcc
 *
 * Check if the given format @fourcc is supported by the current pipeline
 *
 * Return: true if the format is supported, or false
 */
bool xilinx_drm_check_format(struct drm_device *drm, uint32_t fourcc)
{
	struct xilinx_drm_private *private = drm->dev_private;

	return xilinx_drm_crtc_check_format(private->crtc, fourcc);
}

/**
 * xilinx_drm_get_format - Get the current device format
 * @drm: DRM device
 *
 * Get the current format of pipeline
 *
 * Return: the corresponding DRM_FORMAT_XXX
 */
uint32_t xilinx_drm_get_format(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	return xilinx_drm_crtc_get_format(private->crtc);
}

/**
 * xilinx_drm_get_align - Get the alignment value for pitch
 * @drm: DRM object
 *
 * Get the alignment value for pitch from the plane
 *
 * Return: The alignment value if successful, or the error code.
 */
unsigned int xilinx_drm_get_align(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	return xilinx_drm_crtc_get_align(private->crtc);
}

/* poll changed handler */
static void xilinx_drm_output_poll_changed(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	xilinx_drm_fb_hotplug_event(private->fb);
}

static const struct drm_mode_config_funcs xilinx_drm_mode_config_funcs = {
	.fb_create		= xilinx_drm_fb_create,
	.output_poll_changed	= xilinx_drm_output_poll_changed,
};

/* enable vblank */
static int xilinx_drm_enable_vblank(struct drm_device *drm, unsigned int crtc)
{
	struct xilinx_drm_private *private = drm->dev_private;

	xilinx_drm_crtc_enable_vblank(private->crtc);

	return 0;
}

/* disable vblank */
static void xilinx_drm_disable_vblank(struct drm_device *drm, unsigned int crtc)
{
	struct xilinx_drm_private *private = drm->dev_private;

	xilinx_drm_crtc_disable_vblank(private->crtc);
}

/* initialize mode config */
static void xilinx_drm_mode_config_init(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width =
		xilinx_drm_crtc_get_max_width(private->crtc);
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &xilinx_drm_mode_config_funcs;
}

/* convert xilinx format to drm format by code */
int xilinx_drm_format_by_code(unsigned int xilinx_format, uint32_t *drm_format)
{
	const struct xilinx_video_format_desc *format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xilinx_video_formats); i++) {
		format = &xilinx_video_formats[i];
		if (format->xilinx_format == xilinx_format) {
			*drm_format = format->drm_format;
			return 0;
		}
	}

	DRM_ERROR("Unknown Xilinx video format: %d\n", xilinx_format);

	return -EINVAL;
}

/* convert xilinx format to drm format by name */
int xilinx_drm_format_by_name(const char *name, uint32_t *drm_format)
{
	const struct xilinx_video_format_desc *format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xilinx_video_formats); i++) {
		format = &xilinx_video_formats[i];
		if (strcmp(format->name, name) == 0) {
			*drm_format = format->drm_format;
			return 0;
		}
	}

	DRM_ERROR("Unknown Xilinx video format: %s\n", name);

	return -EINVAL;
}

/* get bpp of given format */
unsigned int xilinx_drm_format_bpp(uint32_t drm_format)
{
	const struct xilinx_video_format_desc *format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xilinx_video_formats); i++) {
		format = &xilinx_video_formats[i];
		if (format->drm_format == drm_format)
			return format->bpp;
	}

	return 0;
}

/* get color depth of given format */
unsigned int xilinx_drm_format_depth(uint32_t drm_format)
{
	const struct xilinx_video_format_desc *format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xilinx_video_formats); i++) {
		format = &xilinx_video_formats[i];
		if (format->drm_format == drm_format)
			return format->depth;
	}

	return 0;
}

/* load xilinx drm */
static int xilinx_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct xilinx_drm_private *private;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *encoder_node;
	struct platform_device *pdev = drm->platformdev;
	unsigned int bpp, align, i = 0;
	int ret;

	private = devm_kzalloc(drm->dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	drm_mode_config_init(drm);

	/* create a xilinx crtc */
	private->crtc = xilinx_drm_crtc_create(drm);
	if (IS_ERR(private->crtc)) {
		DRM_DEBUG_DRIVER("failed to create xilinx crtc\n");
		ret = PTR_ERR(private->crtc);
		goto err_out;
	}

	while ((encoder_node = of_parse_phandle(drm->dev->of_node,
						"xlnx,encoder-slave", i))) {
		encoder = xilinx_drm_encoder_create(drm, encoder_node);
		of_node_put(encoder_node);
		if (IS_ERR(encoder)) {
			DRM_DEBUG_DRIVER("failed to create xilinx encoder\n");
			ret = PTR_ERR(encoder);
			goto err_out;
		}

		connector = xilinx_drm_connector_create(drm, encoder, i);
		if (IS_ERR(connector)) {
			DRM_DEBUG_DRIVER("failed to create xilinx connector\n");
			ret = PTR_ERR(connector);
			goto err_out;
		}

		i++;
	}

	if (i == 0) {
		DRM_ERROR("failed to get an encoder slave node\n");
		return -ENODEV;
	}

	ret = drm_vblank_init(drm, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto err_out;
	}

	/* enable irq to enable vblank feature */
	drm->irq_enabled = 1;

	/* allow disable vblank */
	drm->vblank_disable_allowed = 1;

	drm->dev_private = private;
	private->drm = drm;

	/* initialize xilinx framebuffer */
	bpp = xilinx_drm_format_bpp(xilinx_drm_crtc_get_format(private->crtc));
	align = xilinx_drm_crtc_get_align(private->crtc);
	private->fb = xilinx_drm_fb_init(drm, bpp, 1, 1, align,
					 xilinx_drm_fbdev_vres);
	if (IS_ERR(private->fb)) {
		DRM_ERROR("failed to initialize drm cma fb\n");
		ret = PTR_ERR(private->fb);
		goto err_fb;
	}

	drm_kms_helper_poll_init(drm);

	/* set up mode config for xilinx */
	xilinx_drm_mode_config_init(drm);

	drm_helper_disable_unused_functions(drm);

	platform_set_drvdata(pdev, private);

	return 0;

err_fb:
	drm_vblank_cleanup(drm);
err_out:
	drm_mode_config_cleanup(drm);
	if (ret == -EPROBE_DEFER)
		DRM_INFO("load() is defered & will be called again\n");
	return ret;
}

/* unload xilinx drm */
static int xilinx_drm_unload(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	drm_vblank_cleanup(drm);

	drm_kms_helper_poll_fini(drm);

	xilinx_drm_fb_fini(private->fb);

	drm_mode_config_cleanup(drm);

	return 0;
}

/* preclose */
static void xilinx_drm_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct xilinx_drm_private *private = drm->dev_private;

	/* cancel pending page flip request */
	xilinx_drm_crtc_cancel_page_flip(private->crtc, file);
}

/* restore the default mode when xilinx drm is released */
static void xilinx_drm_lastclose(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	xilinx_drm_crtc_restore(private->crtc);

	xilinx_drm_fb_restore_mode(private->fb);
}

static const struct file_operations xilinx_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.mmap		= drm_gem_cma_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.llseek		= noop_llseek,
};

static struct drm_driver xilinx_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_PRIME,
	.load				= xilinx_drm_load,
	.unload				= xilinx_drm_unload,
	.preclose			= xilinx_drm_preclose,
	.lastclose			= xilinx_drm_lastclose,
	.set_busid			= drm_platform_set_busid,

	.get_vblank_counter		= drm_vblank_no_hw_counter,
	.enable_vblank			= xilinx_drm_enable_vblank,
	.disable_vblank			= xilinx_drm_disable_vblank,

	.prime_handle_to_fd		= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle		= drm_gem_prime_fd_to_handle,
	.gem_prime_export		= drm_gem_prime_export,
	.gem_prime_import		= drm_gem_prime_import,
	.gem_prime_get_sg_table		= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table	= drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap			= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap		= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap			= drm_gem_cma_prime_mmap,
	.gem_free_object		= drm_gem_cma_free_object,
	.gem_vm_ops			= &drm_gem_cma_vm_ops,
	.dumb_create			= xilinx_drm_gem_cma_dumb_create,
	.dumb_map_offset		= drm_gem_cma_dumb_map_offset,
	.dumb_destroy			= drm_gem_dumb_destroy,

	.fops				= &xilinx_drm_fops,

	.name				= DRIVER_NAME,
	.desc				= DRIVER_DESC,
	.date				= DRIVER_DATE,
	.major				= DRIVER_MAJOR,
	.minor				= DRIVER_MINOR,
};

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_RUNTIME)
/* suspend xilinx drm */
static int xilinx_drm_pm_suspend(struct device *dev)
{
	struct xilinx_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;
	struct drm_connector *connector;

	drm_modeset_lock_all(drm);
	drm_kms_helper_poll_disable(drm);
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		int old_dpms = connector->dpms;

		if (connector->funcs->dpms)
			connector->funcs->dpms(connector,
					       DRM_MODE_DPMS_SUSPEND);

		connector->dpms = old_dpms;
	}
	drm_modeset_unlock_all(drm);

	return 0;
}

/* resume xilinx drm */
static int xilinx_drm_pm_resume(struct device *dev)
{
	struct xilinx_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;
	struct drm_connector *connector;

	drm_modeset_lock_all(drm);
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		if (connector->funcs->dpms) {
			int dpms = connector->dpms;

			connector->dpms = DRM_MODE_DPMS_OFF;
			connector->funcs->dpms(connector, dpms);
		}
	}
	drm_kms_helper_poll_enable(drm);
	drm_modeset_unlock_all(drm);

	return 0;
}
#endif

static const struct dev_pm_ops xilinx_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xilinx_drm_pm_suspend, xilinx_drm_pm_resume)
	SET_RUNTIME_PM_OPS(xilinx_drm_pm_suspend, xilinx_drm_pm_resume, NULL)
};

/* init xilinx drm platform */
static int xilinx_drm_platform_probe(struct platform_device *pdev)
{
	return drm_platform_init(&xilinx_drm_driver, pdev);
}

/* exit xilinx drm platform */
static int xilinx_drm_platform_remove(struct platform_device *pdev)
{
	struct xilinx_drm_private *private = platform_get_drvdata(pdev);

	drm_put_dev(private->drm);

	return 0;
}

static const struct of_device_id xilinx_drm_of_match[] = {
	{ .compatible = "xlnx,drm", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_drm_of_match);

static struct platform_driver xilinx_drm_private_driver = {
	.probe			= xilinx_drm_platform_probe,
	.remove			= xilinx_drm_platform_remove,
	.driver			= {
		.name		= "xilinx-drm",
		.pm		= &xilinx_drm_pm_ops,
		.of_match_table	= xilinx_drm_of_match,
	},
};

module_platform_driver(xilinx_drm_private_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DRM KMS Driver");
MODULE_LICENSE("GPL v2");
