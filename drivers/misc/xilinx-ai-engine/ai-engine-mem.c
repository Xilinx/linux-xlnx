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

#define aie_cal_reg_pa(aperture, rloc, regoff) ({ \
	struct aie_aperture *__aperture = (aperture); \
	__aperture->res.start + aie_cal_reg_goffset(__aperture->adev, rloc, \
						    regoff); \
	})

static struct sg_table *
aie_mem_map_dma_buf(struct dma_buf_attachment *attachment,
		    enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct aie_dma_mem *dma_mem;
	struct scatterlist *slist;
	struct aie_part_mem *pmem;
	struct sg_table *table;
	void *vaddr;
	int ret;

	pmem = (struct aie_part_mem *)dmabuf->priv;
	vaddr = (void *)pmem->mem.offset;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(table, 1, GFP_KERNEL);

	if (ret < 0)
		goto err;

	slist = table->sgl;

	sg_init_one(slist, vaddr, pmem->mem.size);

	/*
	 * Since memory is allocated using dma_alloc_coherent which stores the
	 * dma address for the virtual address returned dma_map_sgtable is not
	 * needed for converting virtual address to dma-able address.
	 */
	dma_mem = container_of(pmem, struct aie_dma_mem, pmem);

	slist->dma_address = dma_mem->dma_addr;

	return table;
err:
	kfree(table);
	return ERR_PTR(ret);
}

static void aie_mem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				  struct sg_table *table,
				  enum dma_data_direction direction)
{
	sg_free_table(table);
	kfree(table);
}

static int aie_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct aie_part_mem *pmem = dmabuf->priv;
	struct aie_mem *mem = &pmem->mem;
	struct aie_dma_mem *dma_mem;
	struct aie_partition *apart = pmem->apart;
	struct aie_aperture *aperture = apart->aperture;
	struct aie_location rloc;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE, moffset = 0;
	unsigned long remainder = vma->vm_end - addr;
	size_t msize = mem->size;
	u32 rstart_col = mem->range.start.col - aperture->range.start.col;
	int ret;

	if (remainder + offset > pmem->size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (pmem->mem.range.size.row == 0) {
		if (vma->vm_end - addr < pmem->mem.size)
			return -EINVAL;

		dma_mem = container_of(pmem, struct aie_dma_mem, pmem);
		ret = remap_pfn_range(vma, addr, dma_mem->dma_addr >> PAGE_SHIFT,
				      pmem->size, vma->vm_page_prot);
		if (ret < 0) {
			dev_err(&apart->dev,
				"failed to mmap dma memory, remap failed, 0x%p, 0x%lx.\n",
				(void *)dma_mem->dma_addr, pmem->size);
			return ret;
		}
	} else {
		for (rloc.col = rstart_col;
		     rloc.col < rstart_col + mem->range.size.col; rloc.col++) {
			for (rloc.row = mem->range.start.row;
			     rloc.row < mem->range.start.row + mem->range.size.row;
			     rloc.row++) {
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
				mempa = aie_cal_reg_pa(apart->aperture, rloc,
						       toffset + mem->offset);

				ret = remap_pfn_range(vma, addr, mempa >> PAGE_SHIFT,
						      len, vma->vm_page_prot);
				if (ret) {
					dev_err(&apart->dev,
						"failed to mmap (%u,%u)memory, remap failed, 0x%pa, 0x%lx.\n",
						(rloc.col + aperture->range.start.col),
						rloc.row, &mempa, len);
					return ret;
				}
				addr += len;
				offset += len;
				moffset += msize;
			}
		}
	}
	return 0;
}

static void aie_mem_dmabuf_release(struct dma_buf *dmabuf)
{
	struct aie_part_mem *pmem = dmabuf->priv;
	struct aie_dma_mem *dma_mem;

	pmem->dbuf = NULL;

	if (pmem->mem.range.size.row == 0) {
		dma_mem = container_of(pmem, struct aie_dma_mem, pmem);

		kfree(dma_mem);
		dma_mem = NULL;
	}
}

static const struct dma_buf_ops aie_mem_dma_buf_ops = {
	.map_dma_buf = aie_mem_map_dma_buf,
	.unmap_dma_buf = aie_mem_unmap_dma_buf,
	.mmap = aie_mem_mmap,
	.begin_cpu_access = aie_dma_begin_cpu_access,
	.end_cpu_access = aie_dma_end_cpu_access,
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
	dma_buf_get(ret);
	memcpy(mem, &pmem->mem, sizeof(*mem));
	mem->fd = ret;

	return 0;
}

/**
 * aie_dma_mem_alloc() - Allocates physically contiguous memory for dma
 *			 transactions.
 * @apart: AI engine partition
 * @size: Size of the memory to be allocated in bytes
 * @return: buffer file descriptor on success, otherwise returns error.
 *
 * This function allocated physically contiguous memory for dma transactions,
 * exports it as a dma-buf and creates a file descriptor for the buffer.
 */
int aie_dma_mem_alloc(struct aie_partition *apart, __kernel_size_t size)
{
	struct aie_dma_mem *dma_mem;
	struct aie_part_mem *pmem;
	dma_addr_t dma_addr;
	struct aie_mem mem;
	void *vaddr;
	int ret;

	vaddr = dma_alloc_coherent(&apart->dev, size, &dma_addr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	dma_mem = kzalloc(sizeof(*dma_mem), GFP_KERNEL);
	if (!dma_mem) {
		dma_free_coherent(&apart->dev, size, vaddr, dma_addr);
		return -ENOMEM;
	}

	pmem = &dma_mem->pmem;

	pmem->apart = apart;
	pmem->mem.offset = (__kernel_size_t)vaddr;
	pmem->mem.size = size;
	pmem->size = (size_t)size;

	ret = aie_mem_create_dmabuf(apart, pmem, &mem);
	if (ret < 0) {
		dma_free_coherent(&apart->dev, size, vaddr, dma_addr);
		kfree(dma_mem);
		return -EINVAL;
	}

	dma_mem->dma_addr = dma_addr;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dma_free_coherent(&apart->dev, size, vaddr, dma_addr);
		dma_buf_put(pmem->dbuf);
		return ret;
	}

	list_add_tail(&dma_mem->node, &apart->dma_mem);

	mutex_unlock(&apart->mlock);
	return mem.fd;
}

/**
 * aie_dma_mem_free() - De-allocates physically contiguous memory for dma
 *			 transactions.
 * @fd: DMA-BUF File Descriptor
 * @return: 0 on success.
 *
 * This function de-allocated physically contiguous memory for dma transactions,
 * and decreases the reference count of the dma-buf.
 */
int aie_dma_mem_free(int fd)
{
	struct aie_partition *apart;
	struct aie_dma_mem *dma_mem;
	struct aie_part_mem *pmem;
	struct dma_buf *dmabuf;
	int ret;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	pmem = (struct aie_part_mem *)dmabuf->priv;
	apart = pmem->apart;

	/*
	 * Following dma_buf_put reduces the reference count increased when
	 * converting fd to dmabuf using dma_buf_get.
	 */
	dma_buf_put(dmabuf);

	dma_mem = container_of(pmem, struct aie_dma_mem, pmem);
	dma_free_coherent(&apart->dev, pmem->mem.size,
			  (void *)pmem->mem.offset, dma_mem->dma_addr);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	list_del(&dma_mem->node);

	mutex_unlock(&apart->mlock);
	/*
	 * dma_buf_put reduces the reference count increased during allocation.
	 */
	dma_buf_put(dmabuf);

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

	num_mems = apart->adev->ops->get_mem_info(apart->adev, &apart->range,
						  NULL);
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

	num_mems = apart->adev->ops->get_mem_info(apart->adev, &apart->range,
			NULL);
	if (!num_mems)
		return false;

	for (i = 0; i < num_mems; i++) {
		if (apart->pmems[i].dbuf)
			return true;
	}
	return false;
}
