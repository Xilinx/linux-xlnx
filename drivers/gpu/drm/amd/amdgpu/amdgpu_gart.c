/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

/*
 * GART
 * The GART (Graphics Aperture Remapping Table) is an aperture
 * in the GPU's address space.  System pages can be mapped into
 * the aperture and look like contiguous pages from the GPU's
 * perspective.  A page table maps the pages in the aperture
 * to the actual backing pages in system memory.
 *
 * Radeon GPUs support both an internal GART, as described above,
 * and AGP.  AGP works similarly, but the GART table is configured
 * and maintained by the northbridge rather than the driver.
 * Radeon hw has a separate AGP aperture that is programmed to
 * point to the AGP aperture provided by the northbridge and the
 * requests are passed through to the northbridge aperture.
 * Both AGP and internal GART can be used at the same time, however
 * that is not currently supported by the driver.
 *
 * This file handles the common internal GART management.
 */

/*
 * Common GART table functions.
 */
/**
 * amdgpu_gart_table_ram_alloc - allocate system ram for gart page table
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate system memory for GART page table
 * (r1xx-r3xx, non-pcie r4xx, rs400).  These asics require the
 * gart table to be in system memory.
 * Returns 0 for success, -ENOMEM for failure.
 */
int amdgpu_gart_table_ram_alloc(struct amdgpu_device *adev)
{
	void *ptr;

	ptr = pci_alloc_consistent(adev->pdev, adev->gart.table_size,
				   &adev->gart.table_addr);
	if (ptr == NULL) {
		return -ENOMEM;
	}
#ifdef CONFIG_X86
	if (0) {
		set_memory_uc((unsigned long)ptr,
			      adev->gart.table_size >> PAGE_SHIFT);
	}
#endif
	adev->gart.ptr = ptr;
	memset((void *)adev->gart.ptr, 0, adev->gart.table_size);
	return 0;
}

/**
 * amdgpu_gart_table_ram_free - free system ram for gart page table
 *
 * @adev: amdgpu_device pointer
 *
 * Free system memory for GART page table
 * (r1xx-r3xx, non-pcie r4xx, rs400).  These asics require the
 * gart table to be in system memory.
 */
void amdgpu_gart_table_ram_free(struct amdgpu_device *adev)
{
	if (adev->gart.ptr == NULL) {
		return;
	}
#ifdef CONFIG_X86
	if (0) {
		set_memory_wb((unsigned long)adev->gart.ptr,
			      adev->gart.table_size >> PAGE_SHIFT);
	}
#endif
	pci_free_consistent(adev->pdev, adev->gart.table_size,
			    (void *)adev->gart.ptr,
			    adev->gart.table_addr);
	adev->gart.ptr = NULL;
	adev->gart.table_addr = 0;
}

/**
 * amdgpu_gart_table_vram_alloc - allocate vram for gart page table
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate video memory for GART page table
 * (pcie r4xx, r5xx+).  These asics require the
 * gart table to be in video memory.
 * Returns 0 for success, error for failure.
 */
int amdgpu_gart_table_vram_alloc(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.robj == NULL) {
		r = amdgpu_bo_create(adev, adev->gart.table_size,
				     PAGE_SIZE, true, AMDGPU_GEM_DOMAIN_VRAM,
				     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
				     NULL, NULL, &adev->gart.robj);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * amdgpu_gart_table_vram_pin - pin gart page table in vram
 *
 * @adev: amdgpu_device pointer
 *
 * Pin the GART page table in vram so it will not be moved
 * by the memory manager (pcie r4xx, r5xx+).  These asics require the
 * gart table to be in video memory.
 * Returns 0 for success, error for failure.
 */
int amdgpu_gart_table_vram_pin(struct amdgpu_device *adev)
{
	uint64_t gpu_addr;
	int r;

	r = amdgpu_bo_reserve(adev->gart.robj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_pin(adev->gart.robj,
				AMDGPU_GEM_DOMAIN_VRAM, &gpu_addr);
	if (r) {
		amdgpu_bo_unreserve(adev->gart.robj);
		return r;
	}
	r = amdgpu_bo_kmap(adev->gart.robj, &adev->gart.ptr);
	if (r)
		amdgpu_bo_unpin(adev->gart.robj);
	amdgpu_bo_unreserve(adev->gart.robj);
	adev->gart.table_addr = gpu_addr;
	return r;
}

/**
 * amdgpu_gart_table_vram_unpin - unpin gart page table in vram
 *
 * @adev: amdgpu_device pointer
 *
 * Unpin the GART page table in vram (pcie r4xx, r5xx+).
 * These asics require the gart table to be in video memory.
 */
void amdgpu_gart_table_vram_unpin(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.robj == NULL) {
		return;
	}
	r = amdgpu_bo_reserve(adev->gart.robj, false);
	if (likely(r == 0)) {
		amdgpu_bo_kunmap(adev->gart.robj);
		amdgpu_bo_unpin(adev->gart.robj);
		amdgpu_bo_unreserve(adev->gart.robj);
		adev->gart.ptr = NULL;
	}
}

/**
 * amdgpu_gart_table_vram_free - free gart page table vram
 *
 * @adev: amdgpu_device pointer
 *
 * Free the video memory used for the GART page table
 * (pcie r4xx, r5xx+).  These asics require the gart table to
 * be in video memory.
 */
void amdgpu_gart_table_vram_free(struct amdgpu_device *adev)
{
	if (adev->gart.robj == NULL) {
		return;
	}
	amdgpu_bo_unref(&adev->gart.robj);
}

/*
 * Common gart functions.
 */
/**
 * amdgpu_gart_unbind - unbind pages from the gart page table
 *
 * @adev: amdgpu_device pointer
 * @offset: offset into the GPU's gart aperture
 * @pages: number of pages to unbind
 *
 * Unbinds the requested pages from the gart page table and
 * replaces them with the dummy page (all asics).
 */
void amdgpu_gart_unbind(struct amdgpu_device *adev, uint64_t offset,
			int pages)
{
	unsigned t;
	unsigned p;
	int i, j;
	u64 page_base;
	uint32_t flags = AMDGPU_PTE_SYSTEM;

	if (!adev->gart.ready) {
		WARN(1, "trying to unbind memory from uninitialized GART !\n");
		return;
	}

	t = offset / AMDGPU_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / AMDGPU_GPU_PAGE_SIZE);
	for (i = 0; i < pages; i++, p++) {
#ifdef CONFIG_DRM_AMDGPU_GART_DEBUGFS
		adev->gart.pages[p] = NULL;
#endif
		page_base = adev->dummy_page.addr;
		if (!adev->gart.ptr)
			continue;

		for (j = 0; j < (PAGE_SIZE / AMDGPU_GPU_PAGE_SIZE); j++, t++) {
			amdgpu_gart_set_pte_pde(adev, adev->gart.ptr,
						t, page_base, flags);
			page_base += AMDGPU_GPU_PAGE_SIZE;
		}
	}
	mb();
	amdgpu_gart_flush_gpu_tlb(adev, 0);
}

/**
 * amdgpu_gart_bind - bind pages into the gart page table
 *
 * @adev: amdgpu_device pointer
 * @offset: offset into the GPU's gart aperture
 * @pages: number of pages to bind
 * @pagelist: pages to bind
 * @dma_addr: DMA addresses of pages
 *
 * Binds the requested pages to the gart page table
 * (all asics).
 * Returns 0 for success, -EINVAL for failure.
 */
int amdgpu_gart_bind(struct amdgpu_device *adev, uint64_t offset,
		     int pages, struct page **pagelist, dma_addr_t *dma_addr,
		     uint32_t flags)
{
	unsigned t;
	unsigned p;
	uint64_t page_base;
	int i, j;

	if (!adev->gart.ready) {
		WARN(1, "trying to bind memory to uninitialized GART !\n");
		return -EINVAL;
	}

	t = offset / AMDGPU_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / AMDGPU_GPU_PAGE_SIZE);

	for (i = 0; i < pages; i++, p++) {
#ifdef CONFIG_DRM_AMDGPU_GART_DEBUGFS
		adev->gart.pages[p] = pagelist[i];
#endif
		if (adev->gart.ptr) {
			page_base = dma_addr[i];
			for (j = 0; j < (PAGE_SIZE / AMDGPU_GPU_PAGE_SIZE); j++, t++) {
				amdgpu_gart_set_pte_pde(adev, adev->gart.ptr, t, page_base, flags);
				page_base += AMDGPU_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	amdgpu_gart_flush_gpu_tlb(adev, 0);
	return 0;
}

/**
 * amdgpu_gart_init - init the driver info for managing the gart
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate the dummy page and init the gart driver info (all asics).
 * Returns 0 for success, error for failure.
 */
int amdgpu_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->dummy_page.page)
		return 0;

	/* We need PAGE_SIZE >= AMDGPU_GPU_PAGE_SIZE */
	if (PAGE_SIZE < AMDGPU_GPU_PAGE_SIZE) {
		DRM_ERROR("Page size is smaller than GPU page size!\n");
		return -EINVAL;
	}
	r = amdgpu_dummy_page_init(adev);
	if (r)
		return r;
	/* Compute table size */
	adev->gart.num_cpu_pages = adev->mc.gtt_size / PAGE_SIZE;
	adev->gart.num_gpu_pages = adev->mc.gtt_size / AMDGPU_GPU_PAGE_SIZE;
	DRM_INFO("GART: num cpu pages %u, num gpu pages %u\n",
		 adev->gart.num_cpu_pages, adev->gart.num_gpu_pages);

#ifdef CONFIG_DRM_AMDGPU_GART_DEBUGFS
	/* Allocate pages table */
	adev->gart.pages = vzalloc(sizeof(void *) * adev->gart.num_cpu_pages);
	if (adev->gart.pages == NULL) {
		amdgpu_gart_fini(adev);
		return -ENOMEM;
	}
#endif

	return 0;
}

/**
 * amdgpu_gart_fini - tear down the driver info for managing the gart
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the gart driver info and free the dummy page (all asics).
 */
void amdgpu_gart_fini(struct amdgpu_device *adev)
{
	if (adev->gart.ready) {
		/* unbind pages */
		amdgpu_gart_unbind(adev, 0, adev->gart.num_cpu_pages);
	}
	adev->gart.ready = false;
#ifdef CONFIG_DRM_AMDGPU_GART_DEBUGFS
	vfree(adev->gart.pages);
	adev->gart.pages = NULL;
#endif
	amdgpu_dummy_page_fini(adev);
}
