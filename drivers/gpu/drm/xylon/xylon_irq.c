/*
 * Xylon DRM driver IRQ functions
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
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

#include "xylon_drv.h"
#include "xylon_crtc.h"
#include "xylon_irq.h"

irqreturn_t xylon_drm_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct xylon_drm_device *xdev;

	if (!dev)
		return IRQ_NONE;

	xdev = dev->dev_private;

	xylon_drm_crtc_int_handle(xdev->crtc);

	return IRQ_HANDLED;
}

void xylon_drm_irq_preinst(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_int_hw_disable(xdev->crtc);
}

int xylon_drm_irq_postinst(struct drm_device *dev)
{
	return 0;
}

void xylon_drm_irq_uninst(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;

	xylon_drm_crtc_int_hw_disable(xdev->crtc);
}

int xylon_drm_irq_install(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;
	unsigned long irq_flags;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	if (dev->irq_enabled)
		return -EBUSY;
	mutex_unlock(&dev->struct_mutex);

	if (dev->driver->irq_preinstall)
		dev->driver->irq_preinstall(dev);

	if (drm_core_check_feature(dev, DRIVER_IRQ_SHARED))
		irq_flags = IRQF_SHARED;
	else
		irq_flags = 0;

	ret = xylon_drm_crtc_int_request(xdev->crtc, irq_flags,
					 xylon_drm_irq_handler, dev);
	if (ret < 0)
		return ret;

	if (dev->driver->irq_postinstall)
		ret = dev->driver->irq_postinstall(dev);

	if (ret < 0) {
		xylon_drm_crtc_int_free(xdev->crtc, dev);
		return ret;
	}

	mutex_lock(&dev->struct_mutex);
	dev->irq_enabled = 1;
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int xylon_drm_irq_uninstall(struct drm_device *dev)
{
	struct xylon_drm_device *xdev = dev->dev_private;
	unsigned long irqflags;
	int i, irq_enabled;

	if (!drm_core_check_feature(dev, DRIVER_HAVE_IRQ))
		return -EINVAL;

	mutex_lock(&dev->struct_mutex);
	irq_enabled = dev->irq_enabled;
	dev->irq_enabled = 0;
	mutex_unlock(&dev->struct_mutex);

	if (dev->num_crtcs) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		for (i = 0; i < dev->num_crtcs; i++) {
			DRM_WAKEUP(&dev->vblank[i].queue);
			dev->vblank[i].enabled = 0;
			dev->vblank[i].last =
				dev->driver->get_vblank_counter(dev, i);
		}
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
	}

	if (!irq_enabled)
		return -EINVAL;

	if (dev->driver->irq_uninstall)
		dev->driver->irq_uninstall(dev);

	xylon_drm_crtc_int_free(xdev->crtc, dev);

	return 0;
}
