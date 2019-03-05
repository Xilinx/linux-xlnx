/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * The header for UIO driver
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 */

#ifndef _UAPI_UIO_UIO_H_
#define _UAPI_UIO_UIO_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * enum uio_dmabuf_dir - list of dma directions for mapping management
 * @UIO_DMABUF_DIR_BIDIR: Bidirectional DMA. To and from device
 * @UIO_DMABUF_DIR_TO_DEV: DMA to device
 * @UIO_DMABUF_DIR_FROM_DEV: DMA from device
 * @UIO_DMABUF_DIR_NONE: Direction not specified
 */
enum uio_dmabuf_dir {
	UIO_DMABUF_DIR_BIDIR	= 1,
	UIO_DMABUF_DIR_TO_DEV	= 2,
	UIO_DMABUF_DIR_FROM_DEV	= 3,
	UIO_DMABUF_DIR_NONE	= 4,
};

/**
 * struct uio_dmabuf_args - arguments from userspace to map / unmap dmabuf
 * @dbuf_fd: The fd or dma buf
 * @dma_addr: The dma address of dmabuf @dbuf_fd
 * @size: The size of dmabuf @dbuf_fd
 * @dir: direction of dma transfer of dmabuf @dbuf_fd
 */
struct uio_dmabuf_args {
	__s32	dbuf_fd;
	__u64	dma_addr;
	__u64	size;
	__u8	dir;
};

#define UIO_IOC_BASE		'U'

/**
 * DOC: UIO_IOC_MAP_DMABUF - Map the dma buf to userspace uio application
 *
 * This takes uio_dmabuf_args, and maps the given dmabuf @dbuf_fd and returns
 * information to userspace.
 * FIXME: This is experimental and may change at any time. Don't consider this
 * as stable ABI.
 */
#define	UIO_IOC_MAP_DMABUF	_IOWR(UIO_IOC_BASE, 0x1, struct uio_dmabuf_args)

/**
 * DOC: UIO_IOC_UNMAP_DMABUF - Unmap the dma buf
 *
 * This takes uio_dmabuf_args, and unmaps the previous mapped dmabuf @dbuf_fd.
 * FIXME: This is experimental and may change at any time. Don't consider this
 * as stable ABI.
 */
#define	UIO_IOC_UNMAP_DMABUF	_IOWR(UIO_IOC_BASE, 0x2, struct uio_dmabuf_args)

#endif
