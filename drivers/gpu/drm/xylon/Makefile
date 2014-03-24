#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

xylon_drm-y := xylon_drv.o xylon_crtc.o xylon_plane.o xylon_encoder.o \
	       xylon_connector.o xylon_logicvc.o xylon_fbdev.o xylon_fb.o \
	       xylon_irq.o xylon_property.o

obj-$(CONFIG_DRM_XYLON) += xylon_drm.o
