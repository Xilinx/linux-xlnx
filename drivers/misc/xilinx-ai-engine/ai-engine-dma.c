// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver DMA implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include "ai-engine-internal.h"
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/**
 * struct aie_dmabuf - AI engine dmabuf information
 * @attach: dmabuf attachment pointer
 * @sgt: scatter/gather table
 * @refs: refcount of the attached aie_dmabuf
 * @node: list node
 */
struct aie_dmabuf {
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	refcount_t refs;
	struct list_head node;
};

/**
 * aie_part_find_dmabuf() - find a attached dmabuf
 * @apart: AI engine partition
 * @dmabuf: pointer to dmabuf
 * @return: pointer to AI engine dmabuf struct of the found dmabuf, if dmabuf
 *	    is not found, returns NULL.
 *
 * This function scans all the attached dmabufs to see the input dmabuf is
 * in the list. if it is attached, return the corresponding struct aie_dmabuf
 * pointer.
 */
static struct aie_dmabuf *
aie_part_find_dmabuf(struct aie_partition *apart, struct dma_buf *dmabuf)
{
	struct aie_dmabuf *adbuf;

	list_for_each_entry(adbuf, &apart->dbufs, node) {
		if (dmabuf == adbuf->attach->dmabuf)
			return adbuf;
	}

	return NULL;
}

/**
 * aie_part_find_dmabuf_from_file() - find a attached dmabuf from file
 * @apart: AI engine partition
 * @file: file which belongs to a dmabuf
 * @return: pointer to AI engine dmabuf struct of the found dmabuf, if dmabuf
 *	    is not found, returns NULL.
 *
 * This function scans all the attached dmabufs of the AI engine partition,
 * it checks the file with the attached dmabufs, if it founds a match, it
 * returns the aie_dmabuf pointer.
 */
static struct aie_dmabuf *
aie_part_find_dmabuf_from_file(struct aie_partition *apart,
			       const struct file *file)
{
	struct aie_dmabuf *adbuf;

	list_for_each_entry(adbuf, &apart->dbufs, node) {
		if (file == adbuf->attach->dmabuf->file)
			return adbuf;
	}

	return NULL;
}

/**
 * aie_part_get_dmabuf_da() -  get DMA address from the va
 * @apart: AI engine partition
 * @va: virtual address
 * @len: memory length
 * @return: dma address of the specified va, or 0 if va is not valid
 *
 * This function returns DMA address if the has been mapped to a dmabuf which
 * has been attached to the AI engine partition.
 */
static dma_addr_t aie_part_get_dmabuf_da(struct aie_partition *apart,
					 void *va, size_t len)
{
	struct vm_area_struct *vma;
	struct aie_dmabuf *adbuf;
	unsigned long va_start, va_off;

	va_start = (unsigned long)((uintptr_t)va);
	if (!current->mm) {
		dev_err(&apart->dev,
			"failed to get dma address from va, no process mm.\n");
		return 0;
	}

	vma = find_vma(current->mm, va_start);
	if (!vma) {
		dev_err(&apart->dev, "failed to find vma for %p, 0x%zx.\n",
			va, len);
		return 0;
	}

	adbuf = aie_part_find_dmabuf_from_file(apart, vma->vm_file);
	if (!adbuf) {
		dev_err(&apart->dev,
			"failed to get dma address for %p, no dma buf is found.\n",
			va);
		return 0;
	}

	va_off = va_start - vma->vm_start;
	/*
	 * As we only support continuous DMA memory which is guaranteed from
	 * dmabuf attachment, we will compared with the size of the dmabuf only
	 */
	if (va_off + len >= adbuf->attach->dmabuf->size) {
		dev_err(&apart->dev,
			"failed to get dma address for %p, 0x%zx.\n", va, len);
		return 0;
	}

	return sg_dma_address(adbuf->sgt->sgl) + va_off;
}

/**
 * aie_part_get_dmabuf_da_from_off() - get DMA address from offset to a dmabuf
 * @apart: AI engine partition
 * @dmabuf_fd: dmabuf file descriptor
 * @off: offset to the start of a dmabuf
 * @len: memory length
 * @return: dma address, or 0 if @off or @len is invalid, or if @dmabuf_fd is
 *	    not attached.
 *
 * This function returns DMA address if has been mapped to a dmabuf which has
 * been attached to the AI engine partition.
 */
static dma_addr_t
aie_part_get_dmabuf_da_from_off(struct aie_partition *apart, int dmabuf_fd,
				u64 off, size_t len)
{
	struct dma_buf *dbuf = dma_buf_get(dmabuf_fd);
	struct aie_dmabuf *adbuf;

	if (IS_ERR(dbuf)) {
		dev_err(&apart->dev,
			"failed to get dma address, not able to get dmabuf from %d.\n",
			dmabuf_fd);
		return 0;
	}

	adbuf = aie_part_find_dmabuf(apart, dbuf);
	dma_buf_put(dbuf);
	if (!adbuf) {
		dev_err(&apart->dev,
			"failed to get dma address, dmabuf %d not attached.\n",
			dmabuf_fd);
		return 0;
	}

	if (off >= dbuf->size || off + len >= dbuf->size) {
		dev_err(&apart->dev,
			"failed to get dma address from buf %d, off=0x%llx, len=0x%zx.\n",
			dmabuf_fd, off, len);
		return 0;
	}

	return sg_dma_address(adbuf->sgt->sgl) + off;
}

/**
 * aie_part_set_shimdma_bd() - Set the buffer descriptor to AI engine partition
 *			       hardware
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @bd_id: buffer descriptor ID
 * @bd: pointer buffer descriptor content
 * @return: 0 for success, negative value for failure
 *
 * This function sets the specified buffer descriptor content to the
 * specified buffer descriptor in the specified AI engine SHIM NOC tile.
 */
static int aie_part_set_shimdma_bd(struct aie_partition *apart,
				   struct aie_location loc, u32 bd_id, u32 *bd)
{
	const struct aie_dma_attr *shim_dma = apart->adev->shim_dma;
	struct aie_location loc_adjust;
	u32 i, regoff, intile_regoff;

	intile_regoff = shim_dma->bd_regoff + shim_dma->bd_len * bd_id;
	loc_adjust.col = loc.col + apart->range.start.col;
	loc_adjust.row = loc.row + apart->range.start.row;
	regoff = aie_cal_regoff(apart->adev, loc_adjust, intile_regoff);

	for (i = 0; i < shim_dma->bd_len / (sizeof(*bd));
	     i++, regoff += sizeof(*bd))
		iowrite32(bd[i], apart->adev->base + regoff);
	return 0;
}

/**
 * aie_part_validate_bdloc() - Validate SHIM DMA buffer descriptor location
 * @apart: AI engine partition
 * @loc: tile location
 * @bd_id: buffer descriptor id
 *
 * @return: 0 for success, negative value for failure
 *
 * This function validate the SHIM DMA buffer descriptor base address.
 */
static int aie_part_validate_bdloc(struct aie_partition *apart,
				   struct aie_location loc, u32 bd_id)
{
	const struct aie_dma_attr *shim_dma = apart->adev->shim_dma;
	struct aie_location loc_adjust;
	u32 ttype;

	loc_adjust.col = loc.col + apart->range.start.col;
	loc_adjust.row = loc.row + apart->range.start.row;

	if (aie_validate_location(apart, loc_adjust) < 0) {
		dev_err(&apart->dev,
			"invalid loc (%u,%u) in (%u,%u).\n",
			loc.col, loc.row,
			apart->range.size.col, apart->range.size.row);
		return -EINVAL;
	}

	ttype = apart->adev->ops->get_tile_type(&loc_adjust);
	if (ttype != AIE_TILE_TYPE_SHIMNOC) {
		dev_err(&apart->dev,
			"failed to set bd, (%u,%u) is not SHIM NOC\n",
			loc.col, loc.row);
		return -EINVAL;
	}

	if (bd_id >= shim_dma->num_bds) {
		dev_err(&apart->dev,
			"invalid SHIM DMA bd id: %u.\n", bd_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * aie_part_attach_dmabuf() - Attach dmabuf to an AI engine
 * @apart: AI engine partition
 * @dbuf: pointer to the DMA buffer to attach
 * @return: pointer to AI engine dmabuf structure for success, or error value
 *	    for failure
 *
 * This function attaches a dmabuf to the specified AI engine partition.
 */
static struct aie_dmabuf *aie_part_attach_dmabuf(struct aie_partition *apart,
						 struct dma_buf *dbuf)
{
	struct aie_dmabuf *adbuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;

	attach = dma_buf_attach(dbuf, &apart->dev);
	if (IS_ERR(attach)) {
		dev_err(&apart->dev, "failed to attach dmabuf\n");
		return ERR_CAST(attach);
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dev_err(&apart->dev, "failed to map dmabuf attachment\n");
		dma_buf_detach(dbuf, attach);
		return ERR_CAST(sgt);
	}

	if (sgt->nents != 1) {
		dma_addr_t next_sg_addr = sg_dma_address(sgt->sgl);
		struct scatterlist *s;
		unsigned int i;

		for_each_sg(sgt->sgl, s, sgt->nents, i) {
			if (sg_dma_address(s) != next_sg_addr) {
				dev_err(&apart->dev,
					"dmabuf not contiguous\n");
				dma_buf_unmap_attachment(attach, sgt,
							 attach->dir);
				dma_buf_detach(dbuf, attach);
				return ERR_PTR(-EINVAL);
			}

			next_sg_addr = sg_dma_address(s) + sg_dma_len(s);
		}
	}

	adbuf = kmem_cache_alloc(apart->dbufs_cache, GFP_KERNEL);
	if (!adbuf) {
		dma_buf_unmap_attachment(attach, sgt, attach->dir);
		dma_buf_detach(dbuf, attach);
		return ERR_PTR(-ENOMEM);
	}

	adbuf->attach = attach;
	/*
	 * dmabuf attachment doesn't always include the sgt, store it in
	 * AI engine dma buf structure.
	 */
	adbuf->sgt = sgt;

	refcount_set(&adbuf->refs, 1);

	list_add(&adbuf->node, &apart->dbufs);
	return adbuf;
}

/**
 * aie_part_dmabuf_attach_get() - Get reference to an dmabuf attachment
 * @adbuf: AI engine partition attached dmabuf
 *
 * This call will increase the reference count by 1
 */
static void aie_part_dmabuf_attach_get(struct aie_dmabuf *adbuf)
{
	refcount_inc(&adbuf->refs);
}

/**
 * aie_part_dmabuf_attach_put() - Put reference to an dmabuf attachment
 * @adbuf: AI engine partition attached dmabuf
 *
 * This call will decrease the reference count by 1. If the refcount reaches
 * 0, it will detach the dmabuf.
 */
static void aie_part_dmabuf_attach_put(struct aie_dmabuf *adbuf)
{
	struct dma_buf *dbuf;
	struct aie_partition *apart;

	if (!refcount_dec_and_test(&adbuf->refs))
		return;

	apart = dev_to_aiepart(adbuf->attach->dev);
	dbuf = adbuf->attach->dmabuf;
	dma_buf_unmap_attachment(adbuf->attach, adbuf->sgt, adbuf->attach->dir);
	dma_buf_detach(dbuf, adbuf->attach);
	dma_buf_put(dbuf);
	list_del(&adbuf->node);
	kmem_cache_free(apart->dbufs_cache, adbuf);
}

/**
 * aie_part_release_dmabufs() - detach all the attached dmabufs from partition
 * @apart: AI engine partition
 */
void aie_part_release_dmabufs(struct aie_partition *apart)
{
	struct aie_dmabuf *adbuf, *tmpadbuf;

	list_for_each_entry_safe(adbuf, tmpadbuf, &apart->dbufs, node) {
		struct dma_buf *dbuf = adbuf->attach->dmabuf;

		dma_buf_unmap_attachment(adbuf->attach, adbuf->sgt,
					 adbuf->attach->dir);
		dma_buf_detach(dbuf, adbuf->attach);
		dma_buf_put(dbuf);
		list_del(&adbuf->node);
		kmem_cache_free(apart->dbufs_cache, adbuf);
	}
}

/**
 * aie_part_attach_dmabuf_req() - Handle attaching dmabuf to an AI engine
 *				  partition request
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function attaches a dmabuf to the specified AI engine partition and map
 * the attachment. It checks if the dmabuf is already attached, if it is not
 * attached, attach it. It returns the number of entries of the attachment to
 * the AI engine dmabuf user argument. If user wants to know the sg list, it
 * can use AI engine get sg ioctl.
 */
long aie_part_attach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args)
{
	struct aie_dmabuf *adbuf;
	struct dma_buf *dbuf;
	long ret;
	int dmabuf_fd = (int)(uintptr_t)user_args;

	dbuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(dbuf)) {
		dev_err(&apart->dev, "failed to get dmabuf from %d.\n",
			dmabuf_fd);
		return PTR_ERR(dbuf);
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dma_buf_put(dbuf);
		return ret;
	}

	adbuf = aie_part_find_dmabuf(apart, dbuf);
	if (!adbuf)
		adbuf = aie_part_attach_dmabuf(apart, dbuf);
	else
		aie_part_dmabuf_attach_get(adbuf);

	mutex_unlock(&apart->mlock);

	if (IS_ERR(adbuf)) {
		dev_err(&apart->dev, "failed to attach dmabuf\n");
		dma_buf_put(dbuf);
		return PTR_ERR(adbuf);
	}

	return 0;
}

/**
 * aie_part_detach_dmabuf_req() - Handle detaching dmabuf from an AI engine
 *				  partition request
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function unmaps and detaches a dmabuf from the specified AI engine
 * partition.
 */
long aie_part_detach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args)
{
	int dmabuf_fd;
	struct dma_buf *dbuf;
	struct aie_dmabuf *adbuf;
	int ret;

	dmabuf_fd = (int)(uintptr_t)user_args;

	dbuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(dbuf)) {
		dev_err(&apart->dev, "failed to get dmabuf %d.\n", dmabuf_fd);
		return PTR_ERR(dbuf);
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dma_buf_put(dbuf);
		return ret;
	}

	adbuf = aie_part_find_dmabuf(apart, dbuf);
	dma_buf_put(dbuf);
	if (!adbuf) {
		dev_err(&apart->dev, "failed to find dmabuf %d.\n", dmabuf_fd);
		mutex_unlock(&apart->mlock);
		return -EINVAL;
	}

	aie_part_dmabuf_attach_put(adbuf);

	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_part_set_bd() - Set AI engine SHIM DMA buffer descriptor
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor.
 */
long aie_part_set_bd(struct aie_partition *apart, void __user *user_args)
{
	struct aie_device *adev = apart->adev;
	const struct aie_dma_attr *shim_dma = adev->shim_dma;
	struct aie_dma_bd_args args;
	u32 *bd, *tmpbd, buf_len, laddr, haddr, regval;
	dma_addr_t addr;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = aie_part_validate_bdloc(apart, args.loc, args.bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		return -EINVAL;
	}

	bd = memdup_user((void __user *)args.bd, shim_dma->bd_len);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	regval = bd[shim_dma->buflen.regoff / sizeof(u32)];
	buf_len = aie_get_reg_field(&shim_dma->buflen, regval);
	if (!buf_len) {
		dev_err(&apart->dev, "no buf length from shim dma bd.\n");
		kfree(bd);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(bd);
		return ret;
	}

	/* Get device address from virtual address */
	addr = aie_part_get_dmabuf_da(apart, (void *)(uintptr_t)args.data_va,
				      buf_len);
	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%llx, 0x%x.\n",
			args.data_va, buf_len);
		mutex_unlock(&apart->mlock);
		kfree(bd);
		return -EINVAL;
	}

	/* Set low 32bit address */
	laddr = lower_32_bits(addr);
	tmpbd = (u32 *)((char *)bd + shim_dma->laddr.regoff);
	*tmpbd &= ~shim_dma->laddr.mask;
	*tmpbd |= aie_get_field_val(&shim_dma->laddr, laddr);

	/* Set high 32bit address */
	haddr = upper_32_bits(addr);
	tmpbd = (u32 *)((char *)bd + shim_dma->haddr.regoff);
	*tmpbd &= ~shim_dma->haddr.mask;
	*tmpbd |= aie_get_field_val(&shim_dma->haddr, haddr);

	ret = aie_part_set_shimdma_bd(apart, args.loc, args.bd_id, bd);
	mutex_unlock(&apart->mlock);
	if (ret)
		dev_err(&apart->dev, "failed to set to shim dma bd.\n");

	kfree(bd);
	return ret;
}

/**
 * aie_part_set_dmabuf_bd() - Set AI engine SHIM DMA dmabuf buffer descriptor
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor. The buffer descriptor contained in the @user_args has the
 * offset to the start of the buffer descriptor.
 */
long aie_part_set_dmabuf_bd(struct aie_partition *apart,
			    void __user *user_args)
{
	struct aie_device *adev = apart->adev;
	const struct aie_dma_attr *shim_dma = adev->shim_dma;
	struct aie_dmabuf_bd_args args;
	u32 *bd, *tmpbd, len, laddr, haddr, regval;
	u64 off;
	dma_addr_t addr;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = aie_part_validate_bdloc(apart, args.loc, args.bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		return -EINVAL;
	}

	bd = memdup_user((void __user *)args.bd, shim_dma->bd_len);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	regval = bd[shim_dma->buflen.regoff / sizeof(u32)];
	len = aie_get_reg_field(&shim_dma->buflen, regval);
	if (!len) {
		dev_err(&apart->dev, "no buf length from shim dma bd.\n");
		kfree(bd);
		return -EINVAL;
	}

	/* Get low 32bit address offset */
	tmpbd = (u32 *)((char *)bd + shim_dma->laddr.regoff);
	laddr = *tmpbd & shim_dma->laddr.mask;
	/* Get high 32bit address offset */
	tmpbd = (u32 *)((char *)bd + shim_dma->haddr.regoff);
	haddr = *tmpbd & shim_dma->haddr.mask;
	off = laddr | ((u64)haddr << 32);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(bd);
		return ret;
	}

	/* Get device address from offset */
	addr = aie_part_get_dmabuf_da_from_off(apart, args.buf_fd, off, len);
	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%llx, 0x%x.\n",
			off, len);
		mutex_unlock(&apart->mlock);
		kfree(bd);
		return -EINVAL;
	}

	/* Set low 32bit address */
	laddr = lower_32_bits(addr);
	tmpbd = (u32 *)((char *)bd + shim_dma->laddr.regoff);
	*tmpbd &= ~shim_dma->laddr.mask;
	*tmpbd |= aie_get_field_val(&shim_dma->laddr, laddr);

	/* Set high 32bit address */
	haddr = upper_32_bits(addr);
	tmpbd = (u32 *)((char *)bd + shim_dma->haddr.regoff);
	*tmpbd &= ~shim_dma->haddr.mask;
	*tmpbd |= aie_get_field_val(&shim_dma->haddr, haddr);

	ret = aie_part_set_shimdma_bd(apart, args.loc, args.bd_id, bd);
	mutex_unlock(&apart->mlock);
	if (ret)
		dev_err(&apart->dev, "failed to set to shim dma bd.\n");

	kfree(bd);
	return ret;
}

/**
 * aie_part_prealloc_dbufs_cache() - Preallocate dmabuf descriptors memory
 *
 * @apart: AI engine partition
 *
 * @return: 0 for success, negative value for failure
 *
 * This function preallocate memories to save dmabuf descriptors. When dmabuf
 * is attached to the partition at runtime, it can get the descriptor memory
 * from this preallocated memory pool.
 */
int aie_part_prealloc_dbufs_cache(struct aie_partition *apart)
{
	struct kmem_cache *dbufs_cache;
	char name[64];

	sprintf(name, "%s_dbufs", dev_name(&apart->dev));
	dbufs_cache = kmem_cache_create(name, sizeof(struct aie_dmabuf),
					0, 0, NULL);
	if (!dbufs_cache)
		return -ENOMEM;

	apart->dbufs_cache = dbufs_cache;

	return 0;
}
