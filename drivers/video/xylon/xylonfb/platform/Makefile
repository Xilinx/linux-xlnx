obj-y += ../core/

xylonfb_platform-objs := xylonfb-platform.o
ifeq ($(CONFIG_FB_XYLON),m)
xylonfb_platform-objs += ../core/xylonfb_core.o
endif
obj-$(CONFIG_FB_XYLON) += xylonfb_platform.o
