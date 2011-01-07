#
# Makefile for the linux kernel.
#

# Common support
obj-y := time.o devices.o xilinx.o pl330.o clock.o

obj-$(CONFIG_SMP)		+= smp.o head_smp.o
obj-$(CONFIG_LOCAL_TIMERS)	+= local_timer.o 


# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o
