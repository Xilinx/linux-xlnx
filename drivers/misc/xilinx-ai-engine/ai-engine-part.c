// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine partition driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_context.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

/**
 * aie_cal_loc() - calculate tile location from register offset to the AI
 *		   engine device
 * @adev: AI engine device
 * @loc: memory pointer to restore returning location information
 * @regoff: tile internal register offset
 *
 * This function returns the tile location.
 */
static void aie_cal_loc(struct aie_device *adev,
			struct aie_location *loc, u64 regoff)
{
	loc->col = (u32)aie_tile_reg_field_get(aie_col_mask(adev),
					       adev->col_shift, regoff);
	loc->row = (u32)aie_tile_reg_field_get(aie_row_mask(adev),
					       adev->row_shift, regoff);
}

/**
 * aie_part_reg_validation() - validate AI engine partition register access
 * @apart: AI engine partition
 * @offset: AI engine register offset
 * @len: len of data to write/read
 * @is_write: is the access to write to register
 * @return: 0 for success, or negative value for failure.
 *
 * This function validate if the register to access is within the AI engine
 * partition. If it is write access, if the register is writable by user.
 */
static int aie_part_reg_validation(struct aie_partition *apart, size_t offset,
				   size_t len, u8 is_write)
{
	struct aie_device *adev;
	u32 regend32, ttype;
	u64 regoff, regend64;
	struct aie_location loc;
	unsigned int i;

	adev = apart->adev;
	if (offset % sizeof(u32)) {
		dev_err(&apart->dev,
			"Invalid reg off(0x%zx), not 32bit aligned.\n",
			offset);
		return -EINVAL;
	}

	if (len % sizeof(u32)) {
		dev_err(&apart->dev, "Invalid reg operation len %zu.\n", len);
		return -EINVAL;
	}

	regoff = aie_cal_tile_reg(adev, offset);
	regend64 = regoff + len - 1;
	if (regend64 >= BIT_ULL(adev->row_shift)) {
		dev_err(&apart->dev,
			"Invalid reg operation len %zu.\n", len);
		return -EINVAL;
	}

	aie_cal_loc(adev, &loc, offset);
	if (aie_validate_location(apart, loc)) {
		dev_err(&apart->dev,
			"Invalid (%d,%d) out of part(%d,%d),(%d,%d)\n",
			loc.col, loc.row,
			apart->range.start.col, apart->range.start.row,
			apart->range.size.col, apart->range.size.row);
		return -EINVAL;
	}

	/*
	 * We check if a tile is gated before trying to access the tile.
	 * As we mmap() the registers as read only to enable faster status
	 * enquiry, and mmap() memories as write/read to faster memory access,
	 * user can still access the clock gated tiles from userspace by
	 * accessing the mmapped space.
	 * Accessing the gated tiles can cause decode error. With PDI flow,
	 * the PDI sets up the SHIM NOC AXI MM to only generate AI engine error
	 * even instead of generating the NSU error. but for non PDI flow, as
	 * the AXI MM register are protected register, until we have EEMI API
	 * to update the AXI MM register, access the gated tiles can cause NSU
	 * errors.
	 * TODO: To solve this, we need to either request EEMI to configure
	 * AXI MM or split the mmapped space into tiles based lists.
	 */
	if (!aie_part_check_clk_enable_loc(apart, &loc)) {
		dev_err(&apart->dev,
			"Tile(%u,%d) is gated.\n", loc.col, loc.row);
		return -EINVAL;
	}

	if (!is_write)
		return 0;

	regend32 = lower_32_bits(regend64);
	ttype = adev->ops->get_tile_type(&loc);
	for (i = 0; i < adev->num_kernel_regs; i++) {
		const struct aie_tile_regs *regs;
		u32 rttype, writable;

		regs = &adev->kernel_regs[i];
		rttype = (regs->attribute & AIE_REGS_ATTR_TILE_TYPE_MASK) >>
			 AIE_REGS_ATTR_TILE_TYPE_SHIFT;
		writable = (regs->attribute & AIE_REGS_ATTR_PERM_MASK) >>
			   AIE_REGS_ATTR_PERM_SHIFT;
		if (!(BIT(ttype) & rttype))
			continue;
		if ((regoff >= regs->soff && regoff <= regs->eoff) ||
		    (regend32 >= regs->soff && regend32 <= regs->eoff)) {
			if (!writable) {
				dev_err(&apart->dev,
					"reg 0x%zx,0x%zx not writable.\n",
					offset, len);
				return -EINVAL;
			}
		}
	}

	return 0;
}

/**
 * aie_part_write_register() - AI engine partition write register
 * @apart: AI engine partition
 * @offset: AI engine register offset
 * @len: len of data to write
 * @data: data to write
 * @mask: mask, if it is non 0, it is mask write.
 * @return: number of bytes write for success, or negative value for failure.
 *
 * This function writes data to the specified registers.
 * If the mask is non 0, it is mask write.
 */
static int aie_part_write_register(struct aie_partition *apart, size_t offset,
				   size_t len, void *data, u32 mask)
{
	u32 i;
	int ret;
	void __iomem *va;

	if (mask && len > sizeof(u32)) {
		/* For mask write, only allow 32bit. */
		dev_err(&apart->dev,
			"failed mask write, len is more that 32bit.\n");
		return -EINVAL;
	}

	/* offset is expected to be relative to the start of the partition */
	offset += aie_cal_regoff(apart->adev, apart->range.start, 0);
	ret = aie_part_reg_validation(apart, offset, len, 1);
	if (ret < 0) {
		dev_err(&apart->dev, "failed to write to 0x%zx,0x%zx.\n",
			offset, len);
		return ret;
	}

	va = apart->adev->base + offset;
	if (!mask) {
		/*
		 * TODO: use the burst mode to improve performance when len
		 * is more than 4. Additional checks have to be made to ensure
		 * the destination address is 128 bit aligned when burst mode
		 * is used.
		 */
		for (i = 0; i < len; i = i + 4)
			iowrite32(*((u32 *)(data + i)), va + i);
	} else {
		u32 val = ioread32(va);

		val &= ~mask;
		val |= *((u32 *)data) & mask;
		iowrite32(val, va);
	}

	return (int)len;
}

/**
 * aie_part_read_register() - AI engine partition read register
 * @apart: AI engine partition
 * @offset: AI engine register offset
 * @len: len of data to read
 * @data: pointer to the memory to store the read data
 * @return: number of bytes read for success, or negative value for failure.
 *
 * This function reads data from the specified registers.
 */
static int aie_part_read_register(struct aie_partition *apart, size_t offset,
				  size_t len, void *data)
{
	void __iomem *va;
	int ret;

	/* offset is expected to be relative to the start of the partition */
	offset += aie_cal_regoff(apart->adev, apart->range.start, 0);
	ret = aie_part_reg_validation(apart, offset, len, 0);
	if (ret) {
		dev_err(&apart->dev, "Invalid read request 0x%zx,0x%zx.\n",
			offset, len);
		return -EINVAL;
	}

	va = apart->adev->base + offset;
	if (len == 4)
		*((u32 *)data) = ioread32(va);
	else
		memcpy_fromio(data, va, len);

	return (int)len;
}

/**
 * aie_part_block_set() - AI Engine partition block set registers
 * @apart: AI engine partition
 * @args: regestier access arguments
 * @return: 0 for success, and negative value for failure
 */
static int aie_part_block_set(struct aie_partition *apart,
			      struct aie_reg_args *args)
{
	u32 i;
	int ret;

	for (i = 0; i < args->len; i++) {
		size_t offset = (size_t)args->offset;

		ret = aie_part_write_register(apart, offset + i * 4,
					      sizeof(args->val), &args->val,
					      args->mask);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * aie_part_pin_user_region() - pin user pages for access
 * @apart: AI engine partition
 * @region: user space region to pin. Includes virtual address and size of the
 *	    user space buffer.
 * @return: 0 for success, and negative value for failure.
 *
 * This function pins all the pages of a user space buffer.
 */
static int aie_part_pin_user_region(struct aie_partition *apart,
				    struct aie_part_pinned_region *region)
{
	int ret, npages;
	unsigned long first, last;
	struct page **pages;

	first = (region->user_addr & PAGE_MASK) >> PAGE_SHIFT;
	last = ((region->user_addr + region->len - 1) & PAGE_MASK) >>
		PAGE_SHIFT;
	npages = last - first + 1;

	pages = kcalloc(npages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = pin_user_pages_fast(region->user_addr, npages, 0, pages);
	if (ret < 0) {
		kfree(pages);
		dev_err(&apart->dev, "Unable to pin user pages\n");
		return ret;
	} else if (ret != npages) {
		unpin_user_pages(pages, ret);
		kfree(pages);
		dev_err(&apart->dev, "Unable to pin all user pages\n");
		return -EFAULT;
	}

	region->pages = pages;
	region->npages = npages;

	return 0;
}

/**
 * aie_part_unpin_user_region() - unpin user pages
 * @region: user space region to unpin.
 *
 * This function unpins all the pages of a user space buffer. User region passed
 * to this api must be pinned using aie_part_pin_user_region()
 */
static void aie_part_unpin_user_region(struct aie_part_pinned_region *region)
{
	unpin_user_pages(region->pages, region->npages);
	kfree(region->pages);
}

/**
 * aie_part_access_regs() - AI engine partition registers access
 * @apart: AI engine partition
 * @num_reqs: number of access requests
 * @reqs: array of registers access
 * @return: 0 for success, and negative value for failure.
 *
 * This function executes AI engine partition register access requests.
 */
static int aie_part_access_regs(struct aie_partition *apart, u32 num_reqs,
				struct aie_reg_args *reqs)
{
	u32 i;

	for (i = 0; i < num_reqs; i++) {
		struct aie_reg_args *args = &reqs[i];
		int ret;

		switch (args->op) {
		case AIE_REG_WRITE:
		{
			ret = aie_part_write_register(apart,
						      (size_t)args->offset,
						      sizeof(args->val),
						      &args->val, args->mask);
			break;
		}
		case AIE_REG_BLOCKWRITE:
		{
			struct aie_part_pinned_region region;

			region.user_addr = args->dataptr;
			region.len = args->len * sizeof(u32);
			ret = aie_part_pin_user_region(apart, &region);
			if (ret)
				break;

			ret = aie_part_write_register(apart,
						      (size_t)args->offset,
						      sizeof(u32) * args->len,
						      (void *)args->dataptr,
						      args->mask);
			aie_part_unpin_user_region(&region);
			break;
		}
		case AIE_REG_BLOCKSET:
		{
			ret = aie_part_block_set(apart, args);
			break;
		}
		default:
			dev_err(&apart->dev,
				"Invalid register command type: %u.\n",
				args->op);
			return -EINVAL;
		}

		if (ret < 0) {
			dev_err(&apart->dev, "reg op %u failed: 0x%llx.\n",
				args->op, args->offset);
			return ret;
		}
	}

	return 0;
}

/**
 * aie_part_execute_transaction_from_user() - AI engine configure registers
 * @apart: AI engine partition
 * @user_args: arguments passed by user.
 * @return: 0 for success, and negative value for failure.
 *
 * This function executes AI engine register access requests that are part of a
 * buffer that is populated and passed by user.
 */
static int aie_part_execute_transaction_from_user(struct aie_partition *apart,
						  void __user *user_args)
{
	long ret;
	struct aie_txn_inst txn_inst;
	struct aie_part_pinned_region region;

	if (copy_from_user(&txn_inst, user_args, sizeof(txn_inst)))
		return -EFAULT;

	region.user_addr = txn_inst.cmdsptr;
	region.len = txn_inst.num_cmds * sizeof(struct aie_reg_args);
	ret = aie_part_pin_user_region(apart, &region);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		aie_part_unpin_user_region(&region);
		return ret;
	}

	ret = aie_part_access_regs(apart, txn_inst.num_cmds,
				   (struct aie_reg_args *)region.user_addr);

	mutex_unlock(&apart->mlock);

	aie_part_unpin_user_region(&region);
	return ret;
}

/**
 * aie_part_create_event_bitmap() - create event bitmap for all modules in a
 *				    given partition.
 * @apart: AI engine partition
 * @return: 0 for success, and negative value for failure.
 */
static int aie_part_create_event_bitmap(struct aie_partition *apart)
{
	struct aie_range range = apart->range;
	u32 bitmap_sz;
	u32 num_aie_module = range.size.col * (range.size.row - 1);
	int ret;

	bitmap_sz = num_aie_module * apart->adev->core_events->num_events;
	ret = aie_resource_initialize(&apart->core_event_status, bitmap_sz);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize event status resource.\n");
		return -ENOMEM;
	}

	bitmap_sz = num_aie_module * apart->adev->mem_events->num_events;
	ret = aie_resource_initialize(&apart->mem_event_status, bitmap_sz);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize event status resource.\n");
		return -ENOMEM;
	}

	bitmap_sz = range.size.col * apart->adev->pl_events->num_events;
	ret = aie_resource_initialize(&apart->pl_event_status, bitmap_sz);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize event status resource.\n");
		return -ENOMEM;
	}
	return 0;
}

/**
 * aie_part_create_l2_bitmap() - create bitmaps to record mask and status
 *				 values for level 2 interrupt controllers.
 * @apart: AI engine partition
 * @return: 0 for success, and negative value for failure.
 */
static int aie_part_create_l2_bitmap(struct aie_partition *apart)
{
	struct aie_location loc;
	u8 num_l2_ctrls = 0;
	int ret;

	loc.row = 0;
	for (loc.col = apart->range.start.col;
	     loc.col < apart->range.start.col + apart->range.size.col;
	     loc.col++) {
		u32 ttype = apart->adev->ops->get_tile_type(&loc);

		if (ttype == AIE_TILE_TYPE_SHIMNOC)
			num_l2_ctrls++;
	}

	ret = aie_resource_initialize(&apart->l2_mask, num_l2_ctrls *
				      AIE_INTR_L2_CTRL_MASK_WIDTH);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize l2 mask resource.\n");
		return ret;
	}

	return 0;
}

/**
 * aie_part_release_event_bitmap() - Deallocates event bitmap for all modules
 *				     in a given partition.
 * @apart: AI engine partition
 * @return: 0 for success, and negative value for failure.
 */
static void aie_part_release_event_bitmap(struct aie_partition *apart)
{
	aie_resource_uninitialize(&apart->core_event_status);
	aie_resource_uninitialize(&apart->mem_event_status);
	aie_resource_uninitialize(&apart->pl_event_status);
}

static int aie_part_release(struct inode *inode, struct file *filp)
{
	struct aie_partition *apart = filp->private_data;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	aie_part_release_dmabufs(apart);
	aie_part_clean(apart);

	apart->error_cb.cb = NULL;
	apart->error_cb.priv = NULL;
	apart->status = 0;
	apart->error_to_report = 0;

	aie_part_clear_cached_events(apart);
	aie_resource_clear_all(&apart->l2_mask);

	aie_part_rscmgr_reset(apart);

	mutex_unlock(&apart->mlock);

	return 0;
}

static ssize_t aie_part_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *filp = iocb->ki_filp;
	struct aie_partition *apart = filp->private_data;
	size_t len = iov_iter_count(from);
	loff_t offset = iocb->ki_pos;
	void *buf;
	int ret;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (!copy_from_iter_full(buf, len, from)) {
		kfree(buf);
		return -EFAULT;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(buf);
		return ret;
	}

	ret = aie_part_write_register(apart, (size_t)offset, len, buf, 0);
	mutex_unlock(&apart->mlock);
	kfree(buf);

	return ret;
}

static ssize_t aie_part_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *filp = iocb->ki_filp;
	struct aie_partition *apart = filp->private_data;
	size_t len = iov_iter_count(to);
	loff_t offset = iocb->ki_pos;
	void *buf;
	int ret;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(buf);
		return ret;
	}

	ret = aie_part_read_register(apart, (size_t)offset, len, buf);
	mutex_unlock(&apart->mlock);
	if (ret > 0) {
		if (copy_to_iter(buf, ret, to) != len) {
			dev_err(&apart->dev, "Failed to copy to read iter.\n");
			ret = -EFAULT;
		}
	}
	kfree(buf);

	return ret;
}

static const struct vm_operations_struct aie_part_physical_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int aie_part_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct aie_partition *apart = fp->private_data;
	struct aie_device *adev = apart->adev;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	phys_addr_t addr;
	size_t size;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	/* Only allow userspace directly read registers */
	if (vma->vm_flags & VM_WRITE) {
		dev_err(&apart->dev, "%s: do not support writable mmap.\n",
			__func__);
		return -EINVAL;
	}
	vma->vm_private_data = apart;
	vma->vm_ops = &aie_part_physical_vm_ops;
	size = apart->range.size.col << adev->col_shift;
	if ((vma->vm_end - vma->vm_start) > (size - offset)) {
		dev_err(&apart->dev,
			"%s: size exceed.\n", __func__);
		return -EINVAL;
	}
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/* Calculate the partition address */
	addr = adev->res->start;
	addr += (phys_addr_t)apart->range.start.col << adev->col_shift;
	addr += (phys_addr_t)apart->range.start.row << adev->row_shift;
	addr += offset;
	return remap_pfn_range(vma,
			       vma->vm_start,
			       addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static long aie_part_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct aie_partition *apart = fp->private_data;
	void __user *argp = (void __user *)arg;
	long ret;

	switch (cmd) {
	case AIE_REG_IOCTL:
	{
		struct aie_reg_args raccess;

		if (copy_from_user(&raccess, argp, sizeof(raccess)))
			return -EFAULT;

		ret = mutex_lock_interruptible(&apart->mlock);
		if (ret)
			return ret;

		ret = aie_part_access_regs(apart, 1, &raccess);
		mutex_unlock(&apart->mlock);
		break;
	}
	case AIE_GET_MEM_IOCTL:
		return aie_mem_get_info(apart, arg);
	case AIE_ATTACH_DMABUF_IOCTL:
		return aie_part_attach_dmabuf_req(apart, argp);
	case AIE_DETACH_DMABUF_IOCTL:
		return aie_part_detach_dmabuf_req(apart, argp);
	case AIE_SET_SHIMDMA_BD_IOCTL:
		return aie_part_set_bd(apart, argp);
	case AIE_SET_SHIMDMA_DMABUF_BD_IOCTL:
		return aie_part_set_dmabuf_bd(apart, argp);
	case AIE_REQUEST_TILES_IOCTL:
		return aie_part_request_tiles_from_user(apart, argp);
	case AIE_RELEASE_TILES_IOCTL:
		return aie_part_release_tiles_from_user(apart, argp);
	case AIE_TRANSACTION_IOCTL:
		return aie_part_execute_transaction_from_user(apart, argp);
	case AIE_SET_FREQUENCY_IOCTL:
	{
		u64 freq;

		if (copy_from_user(&freq, argp, sizeof(freq)))
			return -EFAULT;

		ret = mutex_lock_interruptible(&apart->mlock);
		if (ret)
			return ret;
		ret = aie_part_set_freq(apart, freq);
		mutex_unlock(&apart->mlock);
		return ret;
	}
	case AIE_GET_FREQUENCY_IOCTL:
	{
		u64 freq;

		ret = mutex_lock_interruptible(&apart->mlock);
		if (ret)
			return ret;
		ret = aie_part_get_running_freq(apart, &freq);
		mutex_unlock(&apart->mlock);

		if (!ret) {
			if (copy_to_user(argp, &freq, sizeof(freq)))
				return -EFAULT;
		}
		return ret;
	}
	case AIE_RSC_REQ_IOCTL:
		return aie_part_rscmgr_rsc_req(apart, argp);
	case AIE_RSC_REQ_SPECIFIC_IOCTL:
		return aie_part_rscmgr_rsc_req_specific(apart, argp);
	case AIE_RSC_RELEASE_IOCTL:
		return aie_part_rscmgr_rsc_release(apart, argp);
	case AIE_RSC_FREE_IOCTL:
		return aie_part_rscmgr_rsc_free(apart, argp);
	case AIE_RSC_CHECK_AVAIL_IOCTL:
		return aie_part_rscmgr_rsc_check_avail(apart, argp);
	case AIE_RSC_GET_COMMON_BROADCAST_IOCTL:
		return aie_part_rscmgr_get_broadcast(apart, argp);
	case AIE_RSC_GET_STAT_IOCTL:
		return aie_part_rscmgr_get_statistics(apart, argp);
	default:
		dev_err(&apart->dev, "Invalid ioctl command %u.\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct file_operations aie_part_fops = {
	.owner		= THIS_MODULE,
	.release	= aie_part_release,
	.read_iter	= aie_part_read_iter,
	.write_iter	= aie_part_write_iter,
	.mmap		= aie_part_mmap,
	.unlocked_ioctl	= aie_part_ioctl,
};

/**
 * aie_tile_release_device() - release an AI engine tile instance
 * @dev: AI engine tile device
 *
 * It will be called by device driver core when no one holds a valid
 * pointer to @dev anymore.
 */
static void aie_tile_release_device(struct device *dev)
{
	(void)dev;
}

/**
 * aie_part_release_device() - release an AI engine partition instance
 * @dev: AI engine partition device
 *
 * It will be called by device driver core when no one holds a valid
 * pointer to @dev anymore.
 */
static void aie_part_release_device(struct device *dev)
{
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_device *adev = apart->adev;
	int ret;

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret) {
		dev_warn(&apart->dev,
			 "getting adev->mlock is interrupted by signal\n");
	}

	aie_resource_put_region(&adev->cols_res, apart->range.start.col,
				apart->range.size.col);
	aie_part_release_event_bitmap(apart);
	aie_resource_uninitialize(&apart->l2_mask);
	list_del(&apart->node);
	mutex_unlock(&adev->mlock);
	aie_resource_uninitialize(&apart->cores_clk_state);
	aie_part_rscmgr_finish(apart);
}

/**
 * aie_part_create_mems_info() - creates array to store the AI engine partition
 *				 different memories types information
 * @apart: AI engine partition
 * @return: 0 for success, negative value for failure
 *
 * This function will create array to store the information of different
 * memories types in the partition. This array is stored in @apart->pmems.
 */
static int aie_part_create_mems_info(struct aie_partition *apart)
{
	unsigned int i, num_mems;

	num_mems = apart->adev->ops->get_mem_info(&apart->range, NULL);
	if (!num_mems)
		return 0;

	apart->pmems = devm_kcalloc(&apart->dev, num_mems,
				    sizeof(struct aie_part_mem),
				    GFP_KERNEL);
	if (!apart->pmems)
		return -ENOMEM;

	apart->adev->ops->get_mem_info(&apart->range, apart->pmems);
	for (i = 0; i < num_mems; i++) {
		struct aie_mem *mem = &apart->pmems[i].mem;

		apart->pmems[i].apart = apart;
		apart->pmems[i].size = mem->size *
				       mem->range.size.col *
				       mem->range.size.row;
	}
	return 0;
}

/**
 * aie_create_tiles() - create AI engine tile devices
 * @apart: AI engine partition
 * @return: 0 for success, error code on failure
 *
 * This function creates AI engine child tile devices for a given partition.
 */
static int aie_create_tiles(struct aie_partition *apart)
{
	struct aie_tile *atile;
	u32 row, col, numtiles;
	int ret = 0;

	numtiles = apart->range.size.col * apart->range.size.row;
	atile = devm_kzalloc(&apart->dev, numtiles * sizeof(struct aie_tile),
			     GFP_KERNEL);
	if (!atile)
		return -ENOMEM;

	apart->atiles = atile;
	for (col = 0; col < apart->range.size.col; col++) {
		for (row = 0; row < apart->range.size.row; row++) {
			struct device *tdev = &atile->dev;
			char tdevname[10];

			atile->apart = apart;
			atile->loc.col = apart->range.start.col + col;
			atile->loc.row = apart->range.start.row + row;
			device_initialize(tdev);
			tdev->parent = &apart->dev;
			dev_set_drvdata(tdev, atile);
			snprintf(tdevname, sizeof(tdevname) - 1, "%d_%d",
				 apart->range.start.col + col,
				 apart->range.start.row + row);
			dev_set_name(tdev, tdevname);
			tdev->release = aie_tile_release_device;
			ret = device_add(tdev);
			if (ret) {
				dev_err(tdev, "tile device_add failed: %d\n",
					ret);
				put_device(tdev);
				return ret;
			}
			ret = aie_tile_sysfs_create_entries(atile);
			if (ret) {
				dev_err(tdev, "failed to create tile sysfs: %d\n",
					ret);
				device_del(tdev);
				put_device(tdev);
				return ret;
			}
			atile++;
		}
	}
	return ret;
}

/**
 * aie_create_partition() - create AI engine partition instance
 * @adev: AI engine device
 * @range: AI engine partition range to check. A range describes a group
 *	   of AI engine tiles.
 * @return: created AI engine partition pointer for success, and PTR_ERR
 *	    for failure.
 *
 * This function creates an AI engine partition instance.
 * It creates AI engine partition, the AI engine partition device and
 * the AI engine partition character device.
 */
static struct aie_partition *aie_create_partition(struct aie_device *adev,
						  struct aie_range *range)
{
	struct aie_partition *apart;
	struct device *dev;
	char devname[32];
	int ret;

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret)
		return ERR_PTR(ret);

	ret = aie_resource_check_region(&adev->cols_res, range->start.col,
					range->size.col);
	if (ret != range->start.col) {
		dev_err(&adev->dev, "invalid partition (%u,%u)(%u,%u).\n",
			range->start.col, range->start.row,
			range->size.col, range->size.row);
		mutex_unlock(&adev->mlock);
		return ERR_PTR(-EINVAL);
	}
	ret = aie_resource_get_region(&adev->cols_res, range->start.col,
				      range->size.col);
	if (ret != range->start.col) {
		dev_err(&adev->dev, "failed to get partition (%u,%u)(%u,%u).\n",
			range->start.col, range->start.row,
			range->size.col, range->size.row);
		mutex_unlock(&adev->mlock);
		return ERR_PTR(-EFAULT);
	}
	mutex_unlock(&adev->mlock);

	apart = devm_kzalloc(&adev->dev, sizeof(*apart), GFP_KERNEL);
	if (!apart)
		return ERR_PTR(-ENOMEM);

	apart->adev = adev;
	INIT_LIST_HEAD(&apart->dbufs);
	memcpy(&apart->range, range, sizeof(*range));
	mutex_init(&apart->mlock);

	/* Create AI engine partition device */
	dev = &apart->dev;
	device_initialize(dev);
	dev->parent = &adev->dev;
	dev->class = aie_class;
	dev_set_drvdata(dev, apart);
	snprintf(devname, sizeof(devname) - 1, "aiepart_%d_%d",
		 apart->range.start.col, apart->range.size.col);
	dev_set_name(dev, devname);
	/* We can now rely on the release function for cleanup */
	dev->release = aie_part_release_device;
	ret = device_add(dev);
	if (ret) {
		dev_err(dev, "device_add failed: %d\n", ret);
		put_device(dev);
		return ERR_PTR(ret);
	}

	/* Set up the DMA mask */
	dev->coherent_dma_mask = DMA_BIT_MASK(48);
	dev->dma_mask = &dev->coherent_dma_mask;

	/* Create AI Engine tile devices */
	ret = aie_create_tiles(apart);
	if (ret) {
		dev_err(dev, "Failed to create tile devices.\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	/*
	 * Create array to keep the information of the different types of tile
	 * memories information of the AI engine partition.
	 */
	ret = aie_part_create_mems_info(apart);
	if (ret) {
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = adev->ops->init_part_clk_state(apart);
	if (ret) {
		put_device(dev);
		return ERR_PTR(ret);
	}

	/*
	 * Create bitmap to record event status for each module in a
	 * partition
	 */
	ret = aie_part_create_event_bitmap(apart);
	if (ret < 0) {
		dev_err(&apart->dev, "Failed to allocate event bitmap.\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = aie_part_create_l2_bitmap(apart);
	if (ret < 0) {
		dev_err(&apart->dev, "Failed to allocate l2 bitmap.\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = aie_part_rscmgr_init(apart);
	if (ret < 0) {
		dev_err(&apart->dev,
			"Failed to initialize resources bitmaps.\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = aie_part_sysfs_create_entries(apart);
	if (ret) {
		dev_err(&apart->dev, "Failed to create partition sysfs.\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret) {
		put_device(dev);
		return ERR_PTR(ret);
	}
	list_add_tail(&apart->node, &adev->partitions);
	mutex_unlock(&adev->mlock);
	dev_dbg(dev, "created AIE partition device.\n");

	return apart;
}

struct aie_partition *
of_aie_part_probe(struct aie_device *adev, struct device_node *nc)
{
	struct aie_partition *apart;
	struct aie_range range;
	u32 partition_id, regs[4];
	int ret;

	/* Select device driver */
	ret = of_property_read_u32_array(nc, "reg", regs, ARRAY_SIZE(regs));
	if (ret < 0) {
		dev_err(&adev->dev,
			"probe %pOF failed, no tiles range information.\n",
			nc);
		return ERR_PTR(ret);
	}
	range.start.col = regs[0];
	range.start.row = regs[1];
	range.size.col = regs[2];
	range.size.row = regs[3];

	ret = of_property_read_u32_index(nc, "xlnx,partition-id", 0,
					 &partition_id);
	if (ret < 0) {
		dev_err(&adev->dev,
			"probe %pOF failed, no partition id.\n", nc);
		return ERR_PTR(ret);
	}

	ret = mutex_lock_interruptible(&adev->mlock);
	if (ret)
		return ERR_PTR(ret);

	apart = aie_get_partition_from_id(adev, partition_id);
	mutex_unlock(&adev->mlock);
	if (apart) {
		dev_err(&adev->dev,
			"probe failed: partition %u exists.\n",
			partition_id);
		return ERR_PTR(-EINVAL);
	}

	apart = aie_create_partition(adev, &range);
	if (IS_ERR(apart)) {
		dev_err(&adev->dev,
			"%s: failed to create part(%u,%u),(%u,%u).\n",
			__func__, range.start.col, range.start.row,
			range.size.col, range.size.row);
		return apart;
	}

	of_node_get(nc);
	apart->dev.of_node = nc;
	apart->dev.driver = adev->dev.parent->driver;
	apart->partition_id = partition_id;
	apart->error_cb.cb = NULL;
	apart->error_cb.priv = NULL;

	ret = of_dma_configure(&apart->dev, nc, true);
	if (ret)
		dev_warn(&apart->dev, "Failed to configure DMA.\n");

	/* Create FPGA bridge for AI engine partition */
	ret = aie_fpga_create_bridge(apart);
	if (ret < 0)
		dev_warn(&apart->dev, "failed to create fpga region.\n");

	dev_info(&adev->dev,
		 "AI engine part(%u,%u),(%u,%u), id %u is probed successfully.\n",
		 range.start.col, range.start.row,
		 range.size.col, range.size.row, apart->partition_id);

	return apart;
}

/**
 * aie_tile_remove() - remove AI engine tile device.
 * @atile: AI engine tile.
 *
 * This function will remove AI engine tile device.
 */
static void aie_tile_remove(struct aie_tile *atile)
{
	aie_tile_sysfs_remove_entries(atile);
	device_del(&atile->dev);
	put_device(&atile->dev);
}

/**
 * aie_part_remove() - destroy AI engine partition
 * @apart: AI engine partition
 *
 * This function will remove AI engine partition.
 */
void aie_part_remove(struct aie_partition *apart)
{
	struct aie_tile *atile = apart->atiles;
	u32 index;

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++)
		aie_tile_remove(atile);

	aie_fpga_free_bridge(apart);
	aie_part_sysfs_remove_entries(apart);
	of_node_clear_flag(apart->dev.of_node, OF_POPULATED);
	device_del(&apart->dev);
	put_device(&apart->dev);
}

/**
 * aie_part_has_regs_mmapped() - check if registers in the partition are mapped.
 * @apart: AI engine partition
 * @return: return true if there are registers mmaped, false otherwise.
 *
 * This function checks if there are registerss in the partition mmapped in the
 * partition.
 */
bool aie_part_has_regs_mmapped(struct aie_partition *apart)
{
	struct address_space *mapping;

	mapping = apart->filep->f_inode->i_mapping;
	return mapping_mapped(mapping);
}

/**
 * aie_part_get_tile_rows - helper function to get the number of rows of a
 *			    tile type.
 *
 * @apart: AI engine partition
 * @ttype: tile type
 * @return: number of rows of a tile type
 */
int aie_part_get_tile_rows(struct aie_partition *apart,
			   enum aie_tile_type ttype)
{
	struct aie_tile_attr *tattr = &apart->adev->ttype_attr[ttype];

	/*
	 * TODO: number of rows information of the AI engine device
	 * should get from device tree.
	 */
	if (tattr->num_rows != 0xFF)
		return tattr->num_rows;
	else
		return (apart->range.size.row - tattr->start_row);
}
