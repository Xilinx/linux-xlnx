#
# Makefile for the linux kernel.
#

# Common support
obj-y				:= common.o slcr.o

obj-$(CONFIG_CPU_IDLE) 		+= cpuidle.o
obj-$(CONFIG_PCI_MSI)           += xaxipcie-msi.o
CFLAGS_REMOVE_hotplug.o		=-march=armv6k
CFLAGS_hotplug.o 		=-Wa,-march=armv7-a -mcpu=cortex-a9
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_SMP)		+= headsmp.o platsmp.o
obj-$(CONFIG_SUSPEND)		+= pm.o suspend.o
obj-$(CONFIG_XILINX_AXIPCIE)    += xaxipcie.o
