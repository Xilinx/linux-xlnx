#
# Makefile for the Xilinx Tri-mode ethernet driver
#

EXTRA_CFLAGS		+= -Idrivers/xilinx_common -Iarch/ppc/platforms/4xx/xparameters
#ifeq ($(CONFIG_PPC32),y)
#EXTRA_CFLAGS		+= -I$(TOPDIR)/arch/ppc/platforms/xilinx_ocp
#endif

# The Linux version for the Xilinx driver code.
xilinx_temac-objs	:= xtemac_linux.o

# The Xilinx OS independent code.
xilinx_temac-objs	+= xtemac.o xtemac_fifo.o xtemac_intr.o \
			   xtemac_intr_sgdma.o xtemac_sgdma.o \
			   xtemac_control.o xtemac_intr_fifo.o \
			   xtemac_l.o xtemac_selftest.o xtemac_stats.o

obj-$(CONFIG_XILINX_TEMAC) := xilinx_temac.o
