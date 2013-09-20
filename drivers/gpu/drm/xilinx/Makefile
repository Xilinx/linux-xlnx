#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

xilinx_drm-y := xilinx_drm_drv.o xilinx_drm_crtc.o xilinx_drm_plane.o \
	      xilinx_drm_encoder.o xilinx_drm_connector.o
xilinx_drm-y += xilinx_cresample.o xilinx_rgb2yuv.o xilinx_osd.o xilinx_vtc.o

obj-$(CONFIG_DRM_XILINX) += xilinx_drm.o
