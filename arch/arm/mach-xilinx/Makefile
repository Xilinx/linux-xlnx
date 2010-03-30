#
# Makefile for the linux kernel.
#

# Common support
obj-y := time.o devices.o xilinx.o smp.o timer-mpu.o pl330.o

# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o
