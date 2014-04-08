/*
 * Xylon DRM driver IRQ header
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

#ifndef __XYLON_DRM_IRQ_H__
#define __XYLON_DRM_IRQ_H__

irqreturn_t xylon_drm_irq_handler(int irq, void *arg);

void xylon_drm_irq_preinst(struct drm_device *dev);
int xylon_drm_irq_postinst(struct drm_device *dev);
void xylon_drm_irq_uninst(struct drm_device *dev);
int xylon_drm_irq_install(struct drm_device *dev);
int xylon_drm_irq_uninstall(struct drm_device *dev);

#endif /* __XYLON_DRM_IRQ_H__ */
