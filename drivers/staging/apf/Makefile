# gpio support: dedicated expander chips, etc

ccflags-$(CONFIG_DEBUG_XILINX_APF) += -DDEBUG
ccflags-$(CONFIG_XILINX_APF) += -Idrivers/dma

obj-$(CONFIG_XILINX_APF) += xlnk.o
obj-$(CONFIG_XILINX_APF) += xlnk-eng.o
obj-$(CONFIG_XILINX_DMA_APF) += xilinx-dma-apf.o

