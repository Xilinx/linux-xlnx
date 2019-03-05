// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyun.kwon@xilinx.com>
 *
 * DMA buf support for UIO device
 *
 */

#include <linux/dma-buf.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/uio_driver.h>
#include <linux/slab.h>

#include <uapi/linux/uio/uio.h>

#include "uio_dmabuf.h"

struct uio_dmabuf_mem {
	int dbuf_fd;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *dbuf_attach;
	struct sg_table *sgt;
	enum dma_data_direction dir;
	struct list_head list;
};

long uio_dmabuf_map(struct uio_device *dev, struct list_head *dbufs,
		    struct mutex *dbufs_lock, void __user *user_args)
{
	struct uio_dmabuf_args args;
	struct uio_dmabuf_mem *dbuf_mem;
	struct dma_buf *dbuf;
	struct dma_buf_attachment *dbuf_attach;
	enum dma_data_direction dir;
	struct sg_table *sgt;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args))) {
		ret = -EFAULT;
		dev_err(dev->dev.parent, "failed to copy from user\n");
		goto err;
	}

	dbuf = dma_buf_get(args.dbuf_fd);
	if (IS_ERR(dbuf)) {
		dev_err(dev->dev.parent, "failed to get dmabuf\n");
		return PTR_ERR(dbuf);
	}

	dbuf_attach = dma_buf_attach(dbuf, dev->dev.parent);
	if (IS_ERR(dbuf_attach)) {
		dev_err(dev->dev.parent, "failed to attach dmabuf\n");
		ret = PTR_ERR(dbuf_attach);
		goto err_put;
	}

	switch (args.dir) {
	case UIO_DMABUF_DIR_BIDIR:
		dir = DMA_BIDIRECTIONAL;
		break;
	case UIO_DMABUF_DIR_TO_DEV:
		dir = DMA_TO_DEVICE;
		break;
	case UIO_DMABUF_DIR_FROM_DEV:
		dir = DMA_FROM_DEVICE;
		break;
	default:
		/* Not needed with check. Just here for any future change  */
		dev_err(dev->dev.parent, "invalid direction\n");
		ret = -EINVAL;
		goto err_detach;
	}

	sgt = dma_buf_map_attachment(dbuf_attach, dir);
	if (IS_ERR(sgt)) {
		dev_err(dev->dev.parent, "failed to get dmabuf scatterlist\n");
		ret = PTR_ERR(sgt);
		goto err_detach;
	}

	/* Accept only contiguous one */
	if (sgt->nents != 1) {
		dma_addr_t next_addr = sg_dma_address(sgt->sgl);
		struct scatterlist *s;
		unsigned int i;

		for_each_sg(sgt->sgl, s, sgt->nents, i) {
			if (!sg_dma_len(s))
				continue;

			if (sg_dma_address(s) != next_addr) {
				dev_err(dev->dev.parent,
					"dmabuf not contiguous\n");
				ret = -EINVAL;
				goto err_unmap;
			}

			next_addr = sg_dma_address(s) + sg_dma_len(s);
		}
	}

	dbuf_mem = kzalloc(sizeof(*dbuf_mem), GFP_KERNEL);
	if (!dbuf_mem) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	dbuf_mem->dbuf_fd = args.dbuf_fd;
	dbuf_mem->dbuf = dbuf;
	dbuf_mem->dbuf_attach = dbuf_attach;
	dbuf_mem->sgt = sgt;
	dbuf_mem->dir = dir;
	args.dma_addr = sg_dma_address(sgt->sgl);
	args.size = dbuf->size;

	if (copy_to_user(user_args, &args, sizeof(args))) {
		ret = -EFAULT;
		dev_err(dev->dev.parent, "failed to copy to user\n");
		goto err_free;
	}

	mutex_lock(dbufs_lock);
	list_add(&dbuf_mem->list, dbufs);
	mutex_unlock(dbufs_lock);

	return 0;

err_free:
	kfree(dbuf_mem);
err_unmap:
	dma_buf_unmap_attachment(dbuf_attach, sgt, dir);
err_detach:
	dma_buf_detach(dbuf, dbuf_attach);
err_put:
	dma_buf_put(dbuf);
err:
	return ret;
}

long uio_dmabuf_unmap(struct uio_device *dev, struct list_head *dbufs,
		      struct mutex *dbufs_lock, void __user *user_args)

{
	struct uio_dmabuf_args args;
	struct uio_dmabuf_mem *dbuf_mem;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args))) {
		ret = -EFAULT;
		goto err;
	}

	mutex_lock(dbufs_lock);
	list_for_each_entry(dbuf_mem, dbufs, list) {
		if (dbuf_mem->dbuf_fd == args.dbuf_fd)
			break;
	}

	if (dbuf_mem->dbuf_fd != args.dbuf_fd) {
		dev_err(dev->dev.parent, "failed to find the dmabuf (%d)\n",
			args.dbuf_fd);
		ret = -EINVAL;
		goto err_unlock;
	}
	list_del(&dbuf_mem->list);
	mutex_unlock(dbufs_lock);

	dma_buf_unmap_attachment(dbuf_mem->dbuf_attach, dbuf_mem->sgt,
				 dbuf_mem->dir);
	dma_buf_detach(dbuf_mem->dbuf, dbuf_mem->dbuf_attach);
	dma_buf_put(dbuf_mem->dbuf);
	kfree(dbuf_mem);

	memset(&args, 0x0, sizeof(args));

	if (copy_to_user(user_args, &args, sizeof(args))) {
		ret = -EFAULT;
		goto err;
	}

	return 0;

err_unlock:
	mutex_unlock(dbufs_lock);
err:
	return ret;
}

int uio_dmabuf_cleanup(struct uio_device *dev, struct list_head *dbufs,
		       struct mutex *dbufs_lock)
{
	struct uio_dmabuf_mem *dbuf_mem, *next;

	mutex_lock(dbufs_lock);
	list_for_each_entry_safe(dbuf_mem, next, dbufs, list) {
		list_del(&dbuf_mem->list);
		dma_buf_unmap_attachment(dbuf_mem->dbuf_attach, dbuf_mem->sgt,
					 dbuf_mem->dir);
		dma_buf_detach(dbuf_mem->dbuf, dbuf_mem->dbuf_attach);
		dma_buf_put(dbuf_mem->dbuf);
		kfree(dbuf_mem);
	}
	mutex_unlock(dbufs_lock);

	return 0;
}
