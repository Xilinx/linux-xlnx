xylonfb_core-y := xylonfb.o xylonfb-ioctl.o xylonfb-pixclk.o
xylonfb_core-$(CONFIG_FB_XYLON_PIXCLK_LOGICLK) += logiclk.o

xylonfb_core-$(CONFIG_FB_XYLON_MISC) += ../misc/xylonfb-misc.o
xylonfb_core-$(CONFIG_FB_XYLON_MISC_ADV7511) += ../misc/xylonfb-adv7511.o

obj-y += xylonfb_core.o
