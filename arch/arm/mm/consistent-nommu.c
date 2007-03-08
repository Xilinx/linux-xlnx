/*
 *  arch/arm/mm/consistent-nommu.c
 *  Based on arch/arm/mm/consistent.c
 *
 *  Copyright (C) 2000-2004 Russell King
 *  Modified by Catalin Marinas for noMMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA uncached mapping support.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>
#include <asm/io.h>

/* set_page_count */
#include "../../../mm/internal.h"

static void *
__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp,
	    pgprot_t prot)
{
	struct page *page;
	void *page_addr;
	unsigned long order;
	u64 mask = ISA_DMA_THRESHOLD, limit;

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			goto no_page;
		}

		if ((~mask) & ISA_DMA_THRESHOLD) {
			dev_warn(dev, "coherent DMA mask %#llx is smaller "
				 "than system GFP_DMA mask %#llx\n",
				 mask, (unsigned long long)ISA_DMA_THRESHOLD);
			goto no_page;
		}
	}

	/*
	 * Sanity check the allocation size.
	 */
	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if (limit && size >= limit) {
		printk(KERN_WARNING "coherent allocation too big "
		       "(requested %#x mask %#llx)\n", size, mask);
		goto no_page;
	}

	order = get_order(size);

	if (mask != 0xffffffff)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		unsigned long kaddr = (unsigned long)page_address(page);
		memset(page_address(page), 0, size);
		dmac_flush_range(kaddr, kaddr + size);
	}

	/*
	 * Set the "dma handle"
	 */
	*handle = page_to_dma(dev, page);
	page_addr = page_address(page);

	do {
		set_page_count(page, 1);
		/*
		 * x86 does not mark the pages reserved...
		 */
		SetPageReserved(page);
		page++;
	} while (size -= PAGE_SIZE);

	return page_addr;

 no_page:
	*handle = ~0;
	return NULL;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_noncached(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Allocate a writecombining region, in much the same way as
 * dma_alloc_coherent above.
 */
void *
dma_alloc_writecombine(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_writecombine(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_writecombine);

static int dma_mmap(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	unsigned long user_size;
	int ret = -ENXIO;

	user_size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	vma->vm_flags |= VM_RESERVED;

	/* Equivalent to: vma->vm_start = vma->vm_pgoff << PAGE_SHIFT; */
	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			      user_size << PAGE_SHIFT, vma->vm_page_prot);

	return ret;
}

int dma_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
		      void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return dma_mmap(dev, vma, cpu_addr, dma_addr, size);
}
EXPORT_SYMBOL(dma_mmap_coherent);

int dma_mmap_writecombine(struct device *dev, struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return dma_mmap(dev, vma, cpu_addr, dma_addr, size);
}
EXPORT_SYMBOL(dma_mmap_writecombine);

/*
 * free a page as defined by the above mapping.
 */
void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t handle)
{
	struct page *page;

	size = PAGE_ALIGN(size);

	page = virt_to_page(cpu_addr);
	do {
	
		/*
		 * x86 does not mark the pages reserved...
		 */
		ClearPageReserved(page);

		__free_page(page);
		page++;
	} while (size -= PAGE_SIZE);
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * Make an area consistent for devices.
 */
void consistent_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		dmac_inv_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		dmac_clean_range(start, end);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		dmac_flush_range(start, end);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(consistent_sync);
