#
# Makefile for the Xilinx 10/100 OPB EMAC driver
#

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/xilinx_common

obj-$(CONFIG_XILINX_EMAC) := xilinx_emac.o

# The Linux driver for the Xilinx EMAC core.
xilinx_emac-objs	:= xemac_linux.o

# The Xilinx OS independent code.
xilinx_emac-objs	+= xemac.o xemac_intr.o xemac_intr_dma.o \
			   xemac_options.o xemac_phy.o xemac_intr_fifo.o
