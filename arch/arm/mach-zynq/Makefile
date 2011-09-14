#
# Makefile for the linux kernel.
#

# Common support
obj-y 				:= common.o timer.o slcr.o scu_gtimer.o

obj-$(CONFIG_SMP)		+= platsmp.o
# obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o 
obj-$(CONFIG_CPU_IDLE) 		+= cpuidle.o

# Testing
obj-$(CONFIG_XILINX_TEST)	+= test/pl330_test.o

