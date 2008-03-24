# The Xilinx OS common code 

obj-$(CONFIG_XILINX_EDK) += xbasic_types.o					\
				xversion.o	xpacket_fifo_v2_00_a.o xpacket_fifo_l_v2_00_a.o	\
				xdma_channel.o xdma_channel_sg.o xio.o

obj-$(CONFIG_NEED_XILINX_DMAV3) += \
			    xdmav3.o xdmav3_intr.o xdmav3_sg.o			\
			    xdmav3_selftest.o xdmav3_simple.o

obj-$(CONFIG_NEED_XILINX_LLDMA) += \
			    xlldma_bdring.o xlldma.o				\
			    xllfifo.o xstreamer.o xio_dcr.o

obj-$(CONFIG_NEED_XILINX_IPIF) += \
			    xipif_v1_23_b.o
				 
