#
# Makefile for the linux kernel.
#

# Common support
obj-y 				:= common.o timer.o slcr.o scu_gtimer.o board_ep107.o pl330.o platform_devices.o

obj-$(CONFIG_SMP)		+= platsmp.o
obj-$(CONFIG_CPU_IDLE) 		+= cpuidle.o

# needed for QEMU framebuffer support
obj-$(CONFIG_SERIO_AMBAKMI)	+= ps2.o

# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o

