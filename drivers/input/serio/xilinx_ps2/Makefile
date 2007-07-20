#
# Makefile for the Xilinx PS/2 driver
#

EXTRA_CFLAGS		+= -Idrivers/xilinx_common

# The Linux adapter for the Xilinx driver code.
xilinx_ps2-objs	:= adapter.o

# The Xilinx OS independent code.
xilinx_ps2-objs	+= xps2.o xps2_intr.o xps2_l.o

obj-$(CONFIG_SERIO_XILINXPS2) += xilinx_ps2.o
