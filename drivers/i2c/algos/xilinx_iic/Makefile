#
# Makefile for the Xilinx IIC driver
#

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/xilinx_common

obj-$(CONFIG_XILINX_IIC) := xilinx_iic.o

# The Linux adapter for the Xilinx driver code.
xilinx_iic-objs	:= i2c-algo-xilinx.o

# The Xilinx OS independent code.
xilinx_iic-objs	+= xiic.o xiic_options.o xiic_master.o \
		   xiic_intr.o xiic_l.o
