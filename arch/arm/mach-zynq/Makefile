#
# Makefile for the linux kernel.
#

# Common support
obj-y				:= common.o slcr.o zynq_ocm.o pm.o

obj-$(CONFIG_SMP)		+= headsmp.o platsmp.o
ORIG_AFLAGS := $(KBUILD_AFLAGS)
KBUILD_AFLAGS = $(subst -march=armv6k,,$(ORIG_AFLAGS))
AFLAGS_suspend.o 		+=-Wa,-march=armv7-a -mcpu=cortex-a9
obj-$(CONFIG_SUSPEND)		+= suspend.o
