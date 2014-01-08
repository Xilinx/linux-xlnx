#
# Makefile for the linux kernel.
#

# Common support
obj-y				:= common.o slcr.o zynq_ocm.o

obj-$(CONFIG_PCI_MSI)           += xaxipcie-msi.o
CFLAGS_REMOVE_hotplug.o		=-march=armv6k
CFLAGS_hotplug.o 		=-Wa,-march=armv7-a -mcpu=cortex-a9
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_SMP)		+= headsmp.o platsmp.o
ORIG_AFLAGS := $(KBUILD_AFLAGS)
KBUILD_AFLAGS = $(subst -march=armv6k,,$(ORIG_AFLAGS))
AFLAGS_suspend.o 		+=-Wa,-march=armv7-a -mcpu=cortex-a9
obj-$(CONFIG_SUSPEND)		+= pm.o suspend.o
obj-$(CONFIG_XILINX_AXIPCIE)    += xaxipcie.o
