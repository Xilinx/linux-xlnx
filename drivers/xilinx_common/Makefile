# The Xilinx OS common code 
EXTRA_CFLAGS		+= -Iarch/ppc/platforms/4xx/xparameters

obj-$(CONFIG_XILINX_EDK) += xbasic_types.o xpacket_fifo_l_v2_00_a.o \
			    xpacket_fifo_v2_00_a.o xversion.o \
			    xdma_channel.o xdma_channel_sg.o

obj-$(CONFIG_XILINX_LLTEMAC) += xlldma_bdring.o xlldma.o \
				xllfifo.o xstreamer.o \
				xio.o xio_dcr.o

obj-$(CONFIG_XILINX_TEMAC) += xdmav3.o xdmav3_intr.o \
			      xdmav3_selftest.o xdmav3_sg.o xdmav3_simple.o \
			      xio.o xipif_v1_23_b.o
