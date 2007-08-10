# The Xilinx OS common code 
EXTRA_CFLAGS		+= -Iarch/ppc/platforms/4xx/xparameters

obj-$(CONFIG_XILINX_EDK) += xbasic_types.o xpacket_fifo_l_v2_00_a.o \
			    xpacket_fifo_v2_00_a.o xversion.o \
			    xdma_channel.o xdma_channel_sg.o

obj-$(CONFIG_XILINX_LLTEMAC) += xlldma_bdring.o xlldma.o \
				xllfifo.o xstreamer.o \
				xio.o xio_dcr.o
