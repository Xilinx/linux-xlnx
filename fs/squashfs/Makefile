#
# Makefile for the linux squashfs routines.
#

obj-$(CONFIG_SQUASHFS) += squashfs.o

squashfs-objs  := inode.o
ifdef CONFIG_SQUASHFS_LZMA
squashfs-objs += LzmaDecode.o LzmaWrapper.o
endif
