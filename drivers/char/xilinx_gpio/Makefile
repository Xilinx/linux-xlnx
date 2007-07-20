#
# Makefile for the Xilinx OPB GPIO driver
#

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/xilinx_common

obj-$(CONFIG_XILINX_GPIO) := xilinx_gpio.o

# The Linux adapter for the Xilinx driver code.
xilinx_gpio-objs	:= adapter.o

# The Xilinx OS independent code.
xilinx_gpio-objs	+= xgpio.o
