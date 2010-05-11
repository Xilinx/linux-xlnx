#
# Makefile for the linux kernel.
#

# Common support
obj-y := time.o devices.o xilinx.o pl330.o

obj-$(CONFIG_SMP)		+= local_timer.o smp.o head_smp.o

# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o
