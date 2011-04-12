#
# Makefile for the linux kernel.
#

# Common support
obj-y := time.o devices.o xilinx.o pl330.o clock.o usb.o

obj-$(CONFIG_SMP)		+= smp.o head_smp.o
obj-$(CONFIG_LOCAL_TIMERS)	+= local_timer.o 
obj-$(CONFIG_XILINX_SLCR)	+= xilinx_slcr.o
obj-$(CONFIG_CPU_IDLE) 		+= cpuidle.o

# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o
