/*
 *  Microblaze support for cache consistent memory.
 *
 *  Copyright (C) 2007 Xilinx, Inc.
 *
 *  Based on arch/microblaze/mm/consistent.c
 *  Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 *  Based on arch/avr32/mm/dma-coherent.c
 *  Copyright (C) 2004-2006 Atmel Corporation
 *
 * Consistent memory allocators.  Used for DMA devices that want to
 * share memory with the processor core.
 *
 * If CONFIG_XILINX_UNCACHED_SHADOW, then this code assumes that the
 * HW platform optionally mirrors the memory up above the processor
 * cacheable region. So, memory accessed in this mirror region will
 * not be cached.  It's alloced from the same pool as normal memory,
 * but the handle we return is shifted up into the uncached region.
 * This will no doubt cause big problems if memory allocated here is
 * not also freed properly.
 * If this trick is not used, then the memory is not actually coherent.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>

void dma_cache_sync(struct device *dev, void *vaddr, size_t size, int direction)
{
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
	/* Convert start address back down to unshadowed memory region */
	unsigned long start = ((unsigned long)vaddr) & UNCACHED_SHADOW_MASK;
	/*
	 * No need to sync an uncached area
	 */
	if(start != (unsigned long)vaddr)
		return;
#endif

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		invalidate_dcache_range(vaddr, vaddr + size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		flush_dcache_range(vaddr, vaddr + size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		invalidate_dcache_range(vaddr, vaddr + size);
		flush_dcache_range(vaddr, vaddr + size);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(dma_cache_sync);

static struct page *__dma_alloc(struct device *dev, size_t size,
				dma_addr_t *handle, gfp_t gfp)
{
	struct page *page, *free, *end;
	int order;

	if (in_interrupt())
		BUG();

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	split_page(page, order);

	/*
	 * When accessing physical memory with valid cache data, we
	 * get a cache hit even if the virtual memory region is marked
	 * as uncached.
	 *
	 * Since the memory is newly allocated, there is no point in
	 * doing a writeback. If the previous owner cares, he should
	 * have flushed the cache before releasing the memory.
	 */
	invalidate_dcache_range(phys_to_virt(page_to_phys(page)), size);

	*handle = page_to_bus(page);
	free = page + (size >> PAGE_SHIFT);
	end = page + (1 << order);

	/*
	 * Free any unused pages
	 */
	while (free < end) {
		__free_page(free);
		free++;
	}

	return page;
}

static void __dma_free(struct device *dev, size_t size,
		       struct page *page, dma_addr_t handle)
{
	struct page *end = page + (PAGE_ALIGN(size) >> PAGE_SHIFT);

	while (page < end)
		__free_page(page++);
}

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *handle, gfp_t gfp)
{
	struct page *page;
	void *ret = NULL;

	page = __dma_alloc(dev, size, handle, gfp);
	if (page) {
		ret = (void *)page_to_phys(page);

		/* Here's the magic!  Note if the uncached shadow is
		   not implemented, it's up to the calling code to
		   also test that condition and make other
		   arranegments, such as manually flushing the cache
		   and so on.
		 */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
		ret = (void *)((unsigned) ret | UNCACHED_SHADOW_MASK);
#endif
	}

	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size,
		       void *cpu_addr, dma_addr_t handle)
{
	void *addr;
	struct page *page;

	/* Clear SHADOW_MASK bit in address, and free as per usual */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
	addr = (void *)((unsigned)cpu_addr & ~UNCACHED_SHADOW_MASK);
#endif

	pr_debug("dma_free_coherent addr %p (phys %08lx) size %u\n",
		 cpu_addr, (unsigned long)handle, (unsigned)size);
	BUG_ON(!virt_addr_valid(addr));
	page = virt_to_page(addr);
	__dma_free(dev, size, page, handle);
}
EXPORT_SYMBOL(dma_free_coherent);
