#
# Makefile for the Xilinx OPB SPI driver
#

EXTRA_CFLAGS	+= -I$(TOPDIR)/drivers/xilinx_common

obj-$(CONFIG_XILINX_SPI) := xilinx_spi.o

# The Linux adapter for the Xilinx driver code.
xilinx_spi-objs	:= adapter.o

# The Xilinx OS independent code.
xilinx_spi-objs	+= xspi.o xspi_options.o
