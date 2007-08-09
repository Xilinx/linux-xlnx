#
# Makefile for the Xilinx Tri-mode ethernet driver
#

EXTRA_CFLAGS		+= -Idrivers/xilinx_common -Iarch/ppc/platforms/4xx/xparameters

# The Linux adapter for the Xilinx driver code.
xilinx_temac-objs	:= xlltemac_main.o xlltemac.o xlltemac_control.o

obj-$(CONFIG_XILINX_TEMAC) := xilinx_temac.o
