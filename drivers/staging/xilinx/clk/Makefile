# status: implements/tested only features required by Xilinx HDMI
#
# mainlining: merge si5324drv.c into clk-si5323.c; Linux code style
# mainlining: drivers/clk/Makefile

obj-$(CONFIG_COMMON_CLK_SI5324)		+= clk-si5324-all.o
clk-si5324-all-objs := clk-si5324.o si5324drv.o
CFLAGS_si5324drv.o += -DFALSE=0 -Dllabs=abs
CFLAGS_clk-si5324.o += -Wno-unused-variable
