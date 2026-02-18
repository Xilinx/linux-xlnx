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

#include "ai-engine-trace.h"
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
	unsigned long va_off;
	unsigned long va_end;
	struct aie_dmabuf_xa *entry;
	unsigned long fd;

	lockdep_assert_held(&apart->mlock);

	if (check_add_overflow((unsigned long)va, len, &va_end)) {
		dev_dbg(&apart->dev, "Invalid len: 0x%zx for va %pK.\n", len, va);
		return 0;
	}

	xa_for_each(&apart->dbuf_xa, fd, entry) {
		if ((unsigned long)va >= entry->vm_start &&
		    va_end <= entry->vm_end) {
			va_off = (unsigned long)va - entry->vm_start;
			return sg_dma_address(entry->sgt->sgl) + va_off;
		}
	}

	dev_dbg(&apart->dev,
		"failed to get dma address from va %pK, 0x%zx.\n", va, len);
	return 0;
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
	struct aie_dmabuf_xa *dma_buf_xa;
	size_t end;

	lockdep_assert_held(&apart->mlock);

	dma_buf_xa = xa_load(&apart->dbuf_xa, dmabuf_fd);
	if (!dma_buf_xa) {
		dev_dbg(&apart->dev,
			"failed to get dma address, no dma buf found for fd %d.\n",
			dmabuf_fd);
		return 0;
	}
	if (off >= dma_buf_xa->size) {
		dev_dbg(&apart->dev,
			"failed to get dma address from buf %d, off=0x%llx, len=0x%zx.\n",
			dmabuf_fd, off, len);
		return 0;
	}
	if (check_add_overflow(off, len, &end) ||
	    end > dma_buf_xa->size) {
		dev_dbg(&apart->dev,
			"failed to get dma address from buf %d, off=0x%llx, len=0x%zx.\n",
			dmabuf_fd, off, len);
		return 0;
	}

	return sg_dma_address(dma_buf_xa->sgt->sgl) + off;
}

/**
 * aie_part_set_shimdma_bd() - Set the buffer descriptor to AI engine partition
 *			       hardware
 * @apart: AI engine partition
 * @loc: AI engine tile location relative in partition
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
	struct aie_aperture *aperture = apart->aperture;
	const struct aie_dma_attr *shim_dma = apart->adev->shim_dma;
	struct aie_location loc_adjust;
	u32 i, regoff, intile_regoff;

	intile_regoff = shim_dma->bd_regoff + shim_dma->bd_len * bd_id;
	loc_adjust.col = loc.col + apart->range.start.col;
	loc_adjust.row = loc.row + apart->range.start.row;
	regoff = aie_aperture_cal_regoff(aperture, loc_adjust, intile_regoff);

	for (i = 0; i < shim_dma->num_bd_regs; i++, regoff += sizeof(*bd)) {
		iowrite32(bd[i], aperture->base + regoff);
		trace_aie_part_set_shimdma_bd(apart, loc, bd_id, bd[i], i);
	}
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

	if (aie_validate_location(apart, loc) < 0) {
		dev_err(&apart->dev,
			"invalid loc (%u,%u) in (%u,%u).\n",
			loc.col, loc.row,
			apart->range.size.col, apart->range.size.row);
		return -EINVAL;
	}

	ttype = apart->adev->ops->get_tile_type(apart->adev, &loc_adjust);
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
 * aie_part_release_dmabufs() - detach all the attached dmabufs from partition
 * @apart: AI engine partition
 */
void aie_part_release_dmabufs(struct aie_partition *apart)
{
	struct aie_part_mem *mems;
	int num_mems;

	num_mems = apart->adev->ops->get_mem_info(apart->adev, &apart->range,
						  NULL);

	if (num_mems <= 0) {
		dev_err(&apart->dev, "Failed to get partition memory info\n");
		return;
	}

	for (int i = 0; i < num_mems; i++) {
		mems = &apart->pmems[i];
		if (mems->dbuf)
			dma_buf_put(mems->dbuf);
	}

}

int aie_dma_begin_cpu_access_xa(struct dma_buf *dmabuf, enum dma_data_direction direction)
{
	struct aie_dmabuf_xa *dma_buf_xa = (struct aie_dmabuf_xa *)dmabuf->priv;

	dma_sync_sg_for_cpu(&dma_buf_xa->apart->dev, dma_buf_xa->sgt->sgl,
			    dma_buf_xa->sgt->nents, direction);
	return 0;
}

int aie_dma_end_cpu_access_xa(struct dma_buf *dmabuf, enum dma_data_direction direction)
{
	struct aie_dmabuf_xa *dma_buf_xa = (struct aie_dmabuf_xa *)dmabuf->priv;

	dma_sync_sg_for_device(&dma_buf_xa->apart->dev, dma_buf_xa->sgt->sgl,
			       dma_buf_xa->sgt->nents, direction);
	return 0;
}

/**
 * aie_part_push_bd() - push buffer descriptor to DMA task queue
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @bd_id: buffer descriptor index
 * @dir: direction of data movement (MM2S, S2MM)
 * @chan_id: dma channel index
 *
 * @return: 0 for success, -EINVAL for failure
 */
int aie_part_push_bd(struct aie_partition *apart, struct aie_location *loc,
		     u8 bd_id, u8 dir, u8 chan_id)
{
	struct aie_device *adev = apart->adev;
	const struct aie_dma_attr *dma_attr;
	u32 regval, offset;
	void __iomem *va;
	u8 ttype;

	ttype = adev->ops->get_tile_type(adev, loc);
	if (ttype != AIE_TILE_TYPE_SHIMNOC) {
		dev_err(&apart->dev, "invalid tile type");
		return -EINVAL;
	}

	dma_attr = adev->shim_dma;
	/* check valid direction and channel id */
	if (dir == AIE_MM2S_DIR) {
		if (chan_id >= dma_attr->num_mm2s_chan)
			return -EINVAL;
	} else if (dir == AIE_S2MM_DIR) {
		if (chan_id >= dma_attr->num_s2mm_chan)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	offset = dma_attr->taskq_bd.regoff +
		(dma_attr->chan_idx_offset * chan_id) +
		(dma_attr->chan_dir_offset * dir);
	regval = aie_get_field_val(&dma_attr->taskq_bd, bd_id);
	va = apart->aperture->base + aie_cal_regoff(adev, *loc, offset);

	writel(regval, va);
	return 0;
}

/**
 * aie_part_set_valid_bd() - set buffer descriptor valid field
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @bd: pointer to buffer descriptor data
 *
 * @return: 0 for success, -EINVAL for failure
 */
int aie_part_set_valid_bd(struct aie_partition *apart, struct aie_location loc,
			  u32 *bd)
{
	struct aie_device *adev = apart->adev;
	const struct aie_bd_attr *bd_attr;
	u32 *tmpbd;
	u8 ttype;

	ttype = adev->ops->get_tile_type(apart->adev, &loc);
	switch (ttype) {
	case AIE_TILE_TYPE_TILE:
		bd_attr = adev->tile_bd;
		break;
	case AIE_TILE_TYPE_MEMORY:
		bd_attr = adev->memtile_bd;
		break;
	case AIE_TILE_TYPE_SHIMNOC:
		bd_attr = adev->shim_bd;
		break;
	default:
		return -EINVAL;
	}

	if (!bd_attr || !bd)
		return -EINVAL;

	tmpbd = (u32 *)((char *)bd + bd_attr->valid_bd.regoff);
	*tmpbd |= aie_get_field_val(&bd_attr->valid_bd, 1);

	return 0;
}

/**
 * aie_part_set_len_bd() - set buffer descriptor length field
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @bd: pointer to buffer descriptor data
 * @len: length of data
 *
 * @return: 0 for success, -EINVAL for failure
 */
int aie_part_set_len_bd(struct aie_partition *apart, struct aie_location loc,
			u32 *bd, size_t len)
{
	struct aie_device *adev = apart->adev;
	const struct aie_bd_attr *bd_attr;
	u32 *tmpbd;
	u8 ttype;

	ttype = adev->ops->get_tile_type(apart->adev, &loc);
	switch (ttype) {
	case AIE_TILE_TYPE_TILE:
		bd_attr = adev->tile_bd;
		break;
	case AIE_TILE_TYPE_MEMORY:
		bd_attr = adev->memtile_bd;
		break;
	case AIE_TILE_TYPE_SHIMNOC:
		bd_attr = adev->shim_bd;
		break;
	default:
		return -EINVAL;
	}

	if (!bd_attr || !bd)
		return -EINVAL;

	tmpbd = (u32 *)((char *)bd + bd_attr->addr.length.regoff);
	*tmpbd |= aie_get_field_val(&bd_attr->addr.length, len);

	return 0;
}

/**
 * aie_part_set_dmabuf_bd_kernel() - Set AI engine SHIM DMA buffer descriptor
 * @apart: AI engine partition
 * @args: user AI engine dmabuf argument
 * @addr: DMA address of the buffer
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor. The structure should contain no userspace pointers
 */
int aie_part_set_dmabuf_bd_kernel(struct aie_partition *apart,
				  struct aie_dmabuf_bd_args *args,
				  dma_addr_t addr)
{
	const struct aie_dma_attr *shim_dma = apart->adev->shim_dma;
	u32 *bd, *tmpbd, len, laddr, haddr, regval;
	int ret;

	ret = aie_part_validate_bdloc(apart, args->loc, args->bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		return -EINVAL;
	}

	bd = args->bd;
	if (!bd)
		return -EINVAL;

	regval = bd[shim_dma->buflen.regoff / sizeof(u32)];
	len = aie_get_reg_field(&shim_dma->buflen, regval);
	if (!len) {
		dev_err(&apart->dev, "no buf length from shim dma bd.\n");
		return -EINVAL;
	}

	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%llx, 0x%x.\n",
			addr, len);
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

	ret = aie_part_set_shimdma_bd(apart, args->loc, args->bd_id, bd);
	if (ret)
		dev_err(&apart->dev, "failed to set to shim dma bd.\n");

	return ret;
}

/**
 * aie_part_set_bd() - Set AI engine SHIM DMA buffer descriptor
 * @apart: AI engine partition
 * @args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor.
 */
long aie_part_set_bd(struct aie_partition *apart, struct aie_dma_bd_args *args)
{
	struct aie_device *adev = apart->adev;
	const struct aie_dma_attr *shim_dma = adev->shim_dma;
	u32 *bd, *tmpbd, buf_len, laddr, haddr, regval;
	dma_addr_t addr;
	int ret;

	ret = aie_part_validate_bdloc(apart, args->loc, args->bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		return -EINVAL;
	}

	bd = memdup_user((void __user *)args->bd, shim_dma->num_bd_regs * sizeof(u32));
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	regval = bd[shim_dma->buflen.regoff / sizeof(u32)];
	buf_len = aie_get_reg_field(&shim_dma->buflen, regval);
	if (!buf_len) {
		dev_err(&apart->dev, "no buf length from shim dma bd.\n");
		kfree(bd);
		return -EINVAL;
	}

	/* Get device address from virtual address */
	addr = aie_part_get_dmabuf_da(apart, (void *)(uintptr_t)args->data_va,
				      buf_len);
	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%llx, 0x%x.\n",
			args->data_va, buf_len);
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

	ret = aie_part_set_shimdma_bd(apart, args->loc, args->bd_id, bd);
	if (ret)
		dev_err(&apart->dev, "failed to set to shim dma bd.\n");

	kfree(bd);
	return ret;
}

/**
 * aie_part_set_bd_from_user() - Set AI engine SHIM DMA buffer descriptor
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor.
 */
long aie_part_set_bd_from_user(struct aie_partition *apart, void __user *user_args)
{
	struct aie_dma_bd_args args;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = aie_part_set_bd(apart, &args);

	mutex_unlock(&apart->mlock);
	return ret;
}

/**
 * aie_part_set_dmabuf_bd() - Set AI engine SHIM DMA dmabuf buffer descriptor
 * @apart: AI engine partition
 * @args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor. The buffer descriptor contained in the @user_args has the
 * offset to the start of the buffer descriptor.
 */
long aie_part_set_dmabuf_bd(struct aie_partition *apart,
			    struct aie_dmabuf_bd_args *args)
{
	struct aie_device *adev = apart->adev;
	const struct aie_dma_attr *shim_dma = adev->shim_dma;
	u32 *bd, *tmpbd, len, laddr, haddr, regval;
	u64 off;
	dma_addr_t addr;
	int ret;

	lockdep_assert_held(&apart->mlock);
	ret = aie_part_validate_bdloc(apart, args->loc, args->bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		return -EINVAL;
	}

	bd = memdup_user((void __user *)args->bd, shim_dma->num_bd_regs * sizeof(u32));
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

	/* Get device address from offset */
	addr = aie_part_get_dmabuf_da_from_off(apart, args->buf_fd, off, len);
	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%llx, 0x%x.\n",
			off, len);
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

	ret = aie_part_set_shimdma_bd(apart, args->loc, args->bd_id, bd);
	if (ret)
		dev_err(&apart->dev, "failed to set to shim dma bd.\n");

	kfree(bd);
	return ret;
}

/**
 * aie_part_set_dmabuf_bd_from_user() - Set AI engine SHIM DMA dmabuf buffer descriptor
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function set the user specified buffer descriptor into the SHIM DMA
 * buffer descriptor. The buffer descriptor contained in the @user_args has the
 * offset to the start of the buffer descriptor.
 */
long aie_part_set_dmabuf_bd_from_user(struct aie_partition *apart, void __user *user_args)
{
	struct aie_dmabuf_bd_args args;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = aie_part_set_dmabuf_bd(apart, &args);

	mutex_unlock(&apart->mlock);
	return ret;
}

/**
 * aie_part_update_dmabuf_bd_from_user() - Updates the AI engine SHIM DMA
 *					   address
 * @apart: AI engine partition
 * @user_args: user AI engine dmabuf argument
 *
 * @return: 0 for success, negative value for failure
 *
 * This function updates the address in SHIM DMA BD. The offset passed from the
 * userspace is added with the buffer base address obtained from dmabuf.
 */
long aie_part_update_dmabuf_bd_from_user(struct aie_partition *apart,
					 void __user *user_args)
{
	const struct aie_dma_attr *shim_dma = apart->adev->shim_dma;
	struct aie_aperture *aperture = apart->aperture;
	struct aie_device *adev = apart->adev;
	u32 len, regval, *off, laddr, haddr;
	struct aie_dmabuf_bd_args args;
	void __iomem *va;
	dma_addr_t addr;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = aie_part_validate_bdloc(apart, args.loc, args.bd_id);
	if (ret) {
		dev_err(&apart->dev, "invalid SHIM DMA BD reg address.\n");
		ret = -EINVAL;
		goto exit;
	}

	off = memdup_user((void __user *)args.bd, sizeof(*off));
	if (IS_ERR(off)) {
		ret = PTR_ERR(off);
		goto exit;
	}

	va = aperture->base +
	     aie_cal_regoff(adev, args.loc, shim_dma->bd_regoff) +
	     shim_dma->bd_len * args.bd_id +
	     shim_dma->buflen.regoff;
	regval = ioread32(va);
	len = aie_get_reg_field(&shim_dma->buflen, regval);

	addr = aie_part_get_dmabuf_da_from_off(apart, args.buf_fd, *off, len);
	if (!addr) {
		dev_err(&apart->dev, "invalid buffer 0x%x, 0x%x.\n",
			*off, len);
		ret = -EINVAL;
		kfree(off);
		goto exit;
	}

	/* Set low 32bit address */
	laddr = lower_32_bits(addr);
	va = aperture->base +
	     aie_cal_regoff(adev, args.loc, shim_dma->bd_regoff) +
	     shim_dma->bd_len * args.bd_id + shim_dma->laddr.regoff;
	regval = ioread32(va);
	regval &= ~shim_dma->laddr.mask;
	laddr |= aie_get_field_val(&shim_dma->laddr, laddr);
	iowrite32(laddr, va);

	/* Set high 32bit address */
	haddr = upper_32_bits(addr);
	va = aperture->base +
	     aie_cal_regoff(adev, args.loc, shim_dma->bd_regoff) +
	     shim_dma->bd_len * args.bd_id + shim_dma->haddr.regoff;
	regval = ioread32(va);
	regval &= ~shim_dma->haddr.mask;
	haddr |= aie_get_field_val(&shim_dma->haddr, haddr);
	iowrite32(haddr, va);
	kfree(off);

exit:
	mutex_unlock(&apart->mlock);
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
