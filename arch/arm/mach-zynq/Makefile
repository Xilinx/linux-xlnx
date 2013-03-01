#
# Makefile for the linux kernel.
#

# Common support
obj-y	:= common.o timer.o slcr.o

obj-$(CONFIG_CPU_IDLE) 		+= cpuidle.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_PCI_MSI)           += xaxipcie-msi.o
obj-$(CONFIG_SMP)		+= platsmp.o
obj-$(CONFIG_XILINX_AXIPCIE)    += xaxipcie.o
