# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

xilinx_drm_xvmixer-y := xvmixer_drm_crtc.o xvmixer_drm_connector.o xvmixer_drm_drv.o\
		      xvmixer_drm_encoder.o xvmixer_drm_fb.o xvmixer_drm_gem.o\
		      xvmixer_drm_plane.o
xilinx_drm_xvmixer-y += xilinx_mixer_hw.o
xilinx_drm_xvmixer-y += xilinx_drm_mixer.o

obj-$(CONFIG_DRM_XILINX_XVMIXER) += xilinx_drm_xvmixer.o
