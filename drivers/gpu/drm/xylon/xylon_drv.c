/*
 * Xylon DRM driver functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * Based on Xilinx DRM driver.
 * Copyright (C) 2013 Xilinx, Inc.
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

#include "xylon_connector.h"
#include "xylon_crtc.h"
#include "xylon_drv.h"
#include "xylon_encoder.h"
#include "xylon_fb.h"
#include "xylon_fbdev.h"
#include "xylon_irq.h"

#define DEVICE_NAME "logicvc"

#define DRIVER_NAME "xylon-drm"
#define DRIVER_DESCRIPTION "Xylon DRM driver for logiCVC IP core"
#define DRIVER_VERSION "1.1"
#define DRIVER_DATE "20140701"

#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0

static int xylon_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct platform_device *pdev = dev->platformdev;
	struct xylon_drm_device *xdev;
	unsigned int bpp;
	int ret;

	xdev = devm_kzalloc(dev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;
	xdev->dev = dev;

	dev->dev_private = xdev;

	drm_mode_config_init(dev);

	drm_kms_helper_poll_init(dev);

	xdev->crtc = xylon_drm_crtc_create(dev);
	if (IS_ERR(xdev->crtc)) {
		DRM_ERROR("failed create xylon crtc\n");
		ret = PTR_ERR(xdev->crtc);
		goto err_out;
	}

	xylon_drm_mode_config_init(dev);

	xdev->encoder = xylon_drm_encoder_create(dev);
	if (IS_ERR(xdev->encoder)) {
		DRM_ERROR("failed create xylon encoder\n");
		ret = PTR_ERR(xdev->encoder);
		goto err_out;
	}

	xdev->connector = xylon_drm_connector_create(dev, xdev->encoder);
	if (IS_ERR(xdev->connector)) {
		DRM_ERROR("failed create xylon connector\n");
		ret = PTR_ERR(xdev->connector);
		goto err_out;
	}

	ret = drm_vblank_init(dev, 1);
	if (ret) {
		DRM_ERROR("failed initialize vblank\n");
		goto err_out;
	}
	dev->vblank_disable_allowed = 1;

	ret = xylon_drm_irq_install(dev);
	if (ret < 0) {
		DRM_ERROR("failed install irq\n");
		goto err_irq;
	}

	ret = xylon_drm_crtc_get_param(xdev->crtc, &bpp,
				       XYLON_DRM_CRTC_BUFF_BPP);
	if (ret) {
		DRM_ERROR("failed get bpp\n");
		goto err_fbdev;
	}
	xdev->fbdev = xylon_drm_fbdev_init(dev, bpp, 1, 1);
	if (IS_ERR(xdev->fbdev)) {
		DRM_ERROR("failed initialize fbdev\n");
		ret = PTR_ERR(xdev->fbdev);
		goto err_fbdev;
	}

	drm_helper_disable_unused_functions(dev);

	platform_set_drvdata(pdev, xdev);

	return 0;

err_fbdev:
	xylon_drm_irq_uninstall(dev);
err_irq:
	drm_vblank_cleanup(dev);
err_out:
	drm_mode_config_cleanup(dev);

	if (ret == -EPROBE_DEFER)
		DRM_INFO("driver load deferred, will be called again\n");

	return ret;
}

static int xylon_drm_unload(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_irq_uninstall(dev);

	drm_vblank_cleanup(dev);

	drm_kms_helper_poll_fini(dev);

	xylon_drm_fbdev_fini(xdev->fbdev);

	drm_mode_config_cleanup(dev);

	return 0;
}

static void xylon_drm_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_cancel_page_flip(xdev->crtc, file);
}

static void xylon_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
}

static void xylon_drm_lastclose(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_properties_restore(xdev->crtc);

	xylon_drm_fbdev_restore_mode(xdev->fbdev);
}

static int xylon_drm_vblank_enable(struct drm_device *dev, int crtc)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_vblank(xdev->crtc, true);

	return 0;
}

static void xylon_drm_vblank_disable(struct drm_device *dev, int crtc)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_vblank(xdev->crtc, false);
}

static int xylon_drm_gem_dumb_create(struct drm_file *file_priv,
				     struct drm_device *dev,
				     struct drm_mode_create_dumb *args)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	struct xylon_drm_device *xdev = dev->dev_private;
	unsigned int buff_width;
	int ret;

	ret = xylon_drm_crtc_get_param(xdev->crtc, &buff_width,
				       XYLON_DRM_CRTC_BUFF_WIDTH);
	if (ret)
		return ret;

	args->pitch = buff_width * DIV_ROUND_UP(args->bpp, 8);
	args->size = (u64)(buff_width * DIV_ROUND_UP(args->bpp, 8) *
			   args->height);

	cma_obj = drm_gem_cma_create(dev, (unsigned int)args->size);
	if (IS_ERR(cma_obj))
		return PTR_ERR(cma_obj);

	gem_obj = &cma_obj->base;

	ret = drm_gem_handle_create(file_priv, gem_obj, &args->handle);
	if (ret)
		goto err_handle_create;

	drm_gem_object_unreference_unlocked(gem_obj);

	return PTR_ERR_OR_ZERO(cma_obj);

err_handle_create:
	drm_gem_cma_free_object(gem_obj);

	return ret;
}

static const struct file_operations xylon_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver xylon_drm_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED |
			   DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = xylon_drm_load,
	.unload = xylon_drm_unload,
	.preclose = xylon_drm_preclose,
	.postclose = xylon_drm_postclose,
	.lastclose = xylon_drm_lastclose,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = xylon_drm_vblank_enable,
	.disable_vblank = xylon_drm_vblank_disable,

	.irq_preinstall = xylon_drm_irq_preinst,
	.irq_postinstall = xylon_drm_irq_postinst,
	.irq_uninstall = xylon_drm_irq_uninst,
	.irq_handler = xylon_drm_irq_handler,

	.gem_free_object = drm_gem_cma_free_object,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,

	.dumb_create = xylon_drm_gem_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,

	.gem_vm_ops = &drm_gem_cma_vm_ops,

	.fops = &xylon_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESCRIPTION,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

static int __maybe_unused xylon_drm_pm_suspend(struct device *dev)
{
	struct xylon_drm_device *xdev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(xdev->dev);
	drm_helper_connector_dpms(xdev->connector, DRM_MODE_DPMS_SUSPEND);

	return 0;
}

static int __maybe_unused xylon_drm_pm_resume(struct device *dev)
{
	struct xylon_drm_device *xdev = dev_get_drvdata(dev);

	drm_helper_connector_dpms(xdev->connector, DRM_MODE_DPMS_ON);
	drm_kms_helper_poll_enable(xdev->dev);

	return 0;
}

static const struct dev_pm_ops xylon_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xylon_drm_pm_suspend, xylon_drm_pm_resume)
	SET_RUNTIME_PM_OPS(xylon_drm_pm_suspend, xylon_drm_pm_resume, NULL)
};

static int xylon_drm_platform_probe(struct platform_device *pdev)
{
	return drm_platform_init(&xylon_drm_driver, pdev);
}

static int xylon_drm_platform_remove(struct platform_device *pdev)
{
	struct xylon_drm_device *xdev = platform_get_drvdata(pdev);

	drm_put_dev(xdev->dev);

	return 0;
}

static const struct of_device_id xylon_drm_of_match[] = {
	{ .compatible = "xylon,drm-1.00.a", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xylon_drm_of_match);

static struct platform_driver xylon_drm_platform_driver = {
	.probe = xylon_drm_platform_probe,
	.remove = xylon_drm_platform_remove,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &xylon_drm_pm_ops,
		.of_match_table = xylon_drm_of_match,
	},
};

module_platform_driver(xylon_drm_platform_driver);

MODULE_AUTHOR("Xylon d.o.o.");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL v2");
