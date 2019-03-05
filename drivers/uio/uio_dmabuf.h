// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *
 * DMA buf support for UIO device
 *
 */

#ifndef _UIO_DMABUF_H_
#define _UIO_DMABUF_H_

struct uio_device;
struct list_head;
struct mutex;

long uio_dmabuf_map(struct uio_device *dev, struct list_head *dbufs,
		    struct mutex *dbufs_lock, void __user *user_args);
long uio_dmabuf_unmap(struct uio_device *dev, struct list_head *dbufs,
		      struct mutex *dbufs_lock, void __user *user_args);

int uio_dmabuf_cleanup(struct uio_device *dev, struct list_head *dbufs,
		       struct mutex *dbufs_lock);

#endif
