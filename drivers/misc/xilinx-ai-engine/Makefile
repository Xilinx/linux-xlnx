# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Xilinx AI engine device driver
#

# Necessary for CREATE_TRACE_POINTS
CFLAGS_ai-engine-dev.o = -I$(src)

obj-$(CONFIG_XILINX_AIE)	+= xilinx-aie.o

xilinx-aie-$(CONFIG_XILINX_AIE) := ai-engine-aie.o		\
				   ai-engine-aieml.o		\
				   ai-engine-aie2ps.o		\
				   ai-engine-aperture.o		\
				   ai-engine-clock.o		\
				   ai-engine-dev.o		\
				   ai-engine-dev-v1_0.o		\
				   ai-engine-dma.o		\
				   ai-engine-interrupt.o	\
				   ai-engine-mem.o		\
				   ai-engine-overlay.o		\
				   ai-engine-part.o		\
				   ai-engine-res.o		\
				   ai-engine-reset.o		\
				   ai-engine-rscmgr.o		\
				   ai-engine-sysfs.o		\
				   ai-engine-sysfs-clock.o	\
				   ai-engine-sysfs-core.o	\
				   ai-engine-sysfs-dma.o	\
				   ai-engine-sysfs-error.o	\
				   ai-engine-sysfs-event.o	\
				   ai-engine-sysfs-info.o	\
				   ai-engine-sysfs-lock.o	\
				   ai-engine-sysfs-status.o	\
				   ai-engine-status-dump.o	\
				   ai-engine-sysfs-ucstatus.o	\
				   ai-engine-pm.o		\
				   ai-engine-status-dump.o
