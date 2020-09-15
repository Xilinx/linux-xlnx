// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device memory implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define aie_cal_reg_goffset(adev, loc, regoff) ({ \
	struct aie_device *_adev = (adev); \
	struct aie_location *_loc = &(loc); \
	(_loc->col << _adev->col_shift) + \
	(_loc->row << _adev->row_shift) + (regoff); \
	})

#define aie_cal_reg_pa(adev, loc, regoff) ({ \
	struct aie_device *__adev = (adev); \
	__adev->res->start + aie_cal_reg_goffset(__adev, loc, regoff); \
	})

static struct sg_table *
aie_mem_map_dma_buf(struct dma_buf_attachment *attachment,
		    enum dma_data_direction direction)
{
	/*
	 * TODO: It is mandatory by DMA buf operation. It is used return
	 * scatterlist table of an attachment. We don't have the implementation
	 * for now. And thus it has empty implementation.
	 */
	(void)attachment;
	(void)direction;
	dev_warn(attachment->dev,
		 "AI engine memory map dma buf is not implemented.\n");
	return NULL;
}

static void aie_mem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				  struct sg_table *table,
				  enum dma_data_direction direction)
{
	/*
	 * TODO: It is mandatory by DMA buf operation. It is used deallocate
	 * scatterlist table of an attachment. We don't have the implementation
	 * for now. And thus it has empty implementation.
	 */
	(void)attachment;
	(void)table;
	(void)direction;
	dev_warn(attachment->dev,
		 "AI engine memory unmap dma buf is not implemented.\n");
}

static int aie_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct aie_part_mem *pmem = dmabuf->priv;
	struct aie_mem *mem = &pmem->mem;
	struct aie_partition *apart = pmem->apart;
	struct aie_location loc;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE, moffset = 0;
	unsigned long remainder = vma->vm_end - addr;
	size_t msize = mem->size;

	if (remainder + offset > pmem->size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	for (loc.col = mem->range.start.col;
	     loc.col < mem->range.start.col + mem->range.size.col; loc.col++) {
		for (loc.row = mem->range.start.row;
		     loc.row < mem->range.start.row + mem->range.size.row;
		     loc.row++) {
			unsigned long toffset, len;
			phys_addr_t mempa;
			int ret;

			remainder = vma->vm_end - addr;
			if (!remainder)
				return 0;

			if (moffset + msize < offset) {
				moffset += msize;
				continue;
			}
			/*
			 * calculate offset within the tile memory.
			 * offset is the offset to vma->start.
			 * moffset is the tile memory start offset to
			 * vma->start.
			 */
			toffset = offset - moffset;
			len = msize - toffset;
			if (len > remainder)
				len = remainder;
			mempa = aie_cal_reg_pa(apart->adev, loc,
					       toffset + mem->offset);

			ret = remap_pfn_range(vma, addr, mempa >> PAGE_SHIFT,
					      len, vma->vm_page_prot);
			if (ret) {
				dev_err(&apart->dev,
					"failed to mmap (%u,%u)memory, remap failed, 0x%pa, 0x%lx.\n",
					loc.col, loc.row, &mempa, len);
				return ret;
			}
			addr += len;
			offset += len;
			moffset += msize;
		}
	}
	return 0;
}

static void aie_mem_dmabuf_release(struct dma_buf *dmabuf)
{
	struct aie_part_mem *pmem = dmabuf->priv;

	pmem->dbuf = NULL;
}

static const struct dma_buf_ops aie_mem_dma_buf_ops = {
	.map_dma_buf = aie_mem_map_dma_buf,
	.unmap_dma_buf = aie_mem_unmap_dma_buf,
	.mmap = aie_mem_mmap,
	.release = aie_mem_dmabuf_release,
};

/**
 * aie_mem_create_dmabuf() - creates DMA buffer for AI engine partition
 *			     memories
 * @apart: AI engine partition
 * @pmem: pointer to the partition memory information
 * @mem: pointer to where it store the memory information and DMA buf file
 *	 descriptor for user.
 * @return: 0 for success, negative value for failure
 *
 * This function will create DMA buffer for the AI engine partition memory
 * and will store the DMA buffer file descriptor and memory information in
 * @mem.
 */
static int aie_mem_create_dmabuf(struct aie_partition *apart,
				 struct aie_part_mem *pmem,
				 struct aie_mem *mem)
{
	struct dma_buf *dmabuf;
	int ret;

	if (!PAGE_ALIGNED(pmem->mem.size)) {
		dev_warn(&apart->dev,
			 "no dmabuf for mem(0x%zx, 0x%zx), not aligned with page size.\n",
			 pmem->mem.offset, pmem->mem.size);
		return -EINVAL;
	}

	dmabuf = pmem->dbuf;
	if (!dmabuf) {
		DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

		exp_info.ops = &aie_mem_dma_buf_ops;
		exp_info.size = pmem->size;
		exp_info.flags = O_RDWR;
		exp_info.priv = pmem;

		dmabuf = dma_buf_export(&exp_info);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		pmem->dbuf = dmabuf;
	}

	ret = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (ret < 0) {
		dev_err(&apart->dev,
			"dmabuf creation failed, failed to get fd.\n");
		return ret;
	}
	memcpy(mem, &pmem->mem, sizeof(*mem));
	mem->fd = ret;

	return 0;
}

/**
 * aie_mem_get_info() - get AI engine memories information
 * @apart: AI engine partition
 * @arg: argument from user to enquire AI engine partition memory information
 * @return: 0 for success, and negative value for failure
 *
 * This function will get the memories information for the specified AI engine
 * partition. It will create DMA buf file descriptors for the memories and
 * return the DMA buf file descriptors to users.
 * It will create a DMA buffer per type of memories.
 * e.g. There will be a DMA buffer for all the tile program memories in the
 * partition, and another DMA buffer for all the tile data memories in the
 * partition.
 * User can first pass num_mems as 0 in the @arg to enquire for how many types
 * of memories in this AI engine partition. And then, user can allocate memory
 * to keep the information for different types of memories, and then use the
 * same enqury with non-zero num_mems and none NULL pointer to ask for the
 * details of the information of all the types of memories in the AI engine
 * partition.
 */
int aie_mem_get_info(struct aie_partition *apart, unsigned long arg)
{
	struct aie_mem_args margs;
	struct aie_mem *mems;
	unsigned int num_mems, i;
	int ret;

	if (copy_from_user(&margs, (void __user *)arg, sizeof(margs)))
		return -EFAULT;

	num_mems = apart->adev->ops->get_mem_info(&apart->range, NULL);
	if (num_mems <= 0)
		return -EINVAL;

	if (!margs.num_mems) {
		struct aie_mem_args __user *umargs_ptr = (void __user *)arg;

		/* This enquiry is to get the number of types of memories. */
		if (copy_to_user((void __user *)&umargs_ptr->num_mems,
				 &num_mems, sizeof(num_mems)))
			return -EFAULT;
		return 0;
	}

	if (num_mems != margs.num_mems) {
		dev_err(&apart->dev,
			"failed to get mem info, invalid num of mems %d,%d.\n",
			num_mems, margs.num_mems);
		return -EINVAL;
	}
	if (!margs.mems) {
		dev_err(&apart->dev,
			"failed to get mem info, mems pointer is NULL.\n");
		return -EINVAL;
	}

	mems = kcalloc(num_mems, sizeof(*mems), GFP_KERNEL);
	if (!mems)
		return -ENOMEM;

	/*
	 * Create DMA buffer for the memories.
	 * Each type of memory in the partition has its own DMA buf.
	 */
	for (i = 0; i < num_mems; i++) {
		ret = aie_mem_create_dmabuf(apart, &apart->pmems[i], &mems[i]);
		if (ret)
			break;
	}
	if (!ret) {
		if (copy_to_user((void __user *)margs.mems, mems,
				 num_mems * sizeof(mems[0])))
			ret = -EFAULT;
	}

	if (ret) {
		for (i = 0; i < num_mems; i++) {
			if (mems[i].fd)
				put_unused_fd(mems[i].fd);
		}
	}

	kfree(mems);
	return ret;
}

/**
 * aie_part_has_mem_mmapped() - check if memories in the partition are mapped
 * @apart: AI engine partition
 * @return: return true if there are memories mmaped, false otherwise.
 *
 * This function checks if there are memories in the partition mmapped in the
 * partition.
 */
bool aie_part_has_mem_mmapped(struct aie_partition *apart)
{
	unsigned int num_mems, i;

	num_mems = apart->adev->ops->get_mem_info(&apart->range, NULL);
	if (!num_mems)
		return false;

	for (i = 0; i < num_mems; i++) {
		if (apart->pmems[i].dbuf)
			return true;
	}
	return false;
}
