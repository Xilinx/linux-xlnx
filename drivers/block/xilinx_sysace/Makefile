#
# Makefile for the Xilinx System ACE driver
#

EXTRA_CFLAGS		+= -Idrivers/xilinx_common -Iarch/ppc/platforms/4xx/xparameters

# The Linux Version for the Xilinx driver code.
xilinx_sysace-objs	+= xsysace_linux.o

# The Xilinx OS independent code.
xilinx_sysace-objs	+= xsysace.o xsysace_intr.o xsysace_l.o
xilinx_sysace-objs	+= xsysace_compactflash.o xsysace_jtagcfg.o

obj-$(CONFIG_XILINX_SYSACE_OLD) += xilinx_sysace.o
