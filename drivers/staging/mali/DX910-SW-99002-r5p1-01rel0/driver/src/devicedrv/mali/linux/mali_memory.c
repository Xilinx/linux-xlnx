/*
 * Copyright (C) 2013-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/idr.h>

#include "mali_osk.h"
#include "mali_executor.h"

#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#include "mali_memory_block_alloc.h"
#include "mali_memory_util.h"
#include "mali_memory_virtual.h"
#include "mali_memory_manager.h"

extern unsigned int mali_dedicated_mem_size;
extern unsigned int mali_shared_mem_size;

/* session->memory_lock must be held when calling this function */
static void mali_mem_vma_open(struct vm_area_struct *vma)
{
	mali_mem_allocation *alloc = (mali_mem_allocation *)vma->vm_private_data;
	MALI_DEBUG_PRINT(4, ("Open called on vma %p\n", vma));

	/* If need to share the allocation, add ref_count here */
	mali_allocation_ref(alloc);
	return;
}
static void mali_mem_vma_close(struct vm_area_struct *vma)
{
	/* If need to share the allocation, unref ref_count here */
	mali_mem_allocation *alloc = (mali_mem_allocation *)vma->vm_private_data;

	mali_allocation_unref(&alloc);
	vma->vm_private_data = NULL;
}

static int mali_mem_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	/* Not support yet */
	MALI_DEBUG_ASSERT(0);
	return 0;
}

static struct vm_operations_struct mali_kernel_vm_ops = {
	.open = mali_mem_vma_open,
	.close = mali_mem_vma_close,
	.fault = mali_mem_vma_fault,
};


/** @ map mali allocation to CPU address
*
* Supported backend types:
* --MALI_MEM_OS
* -- need to add COW?
 *Not supported backend types:
* -_MALI_MEMORY_BIND_BACKEND_UMP
* -_MALI_MEMORY_BIND_BACKEND_DMA_BUF
* -_MALI_MEMORY_BIND_BACKEND_EXTERNAL_MEMORY
*
*/
int mali_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct mali_session_data *session;
	mali_mem_allocation *mali_alloc = NULL;
	u32 mali_addr = vma->vm_pgoff << PAGE_SHIFT;
	struct mali_vma_node *mali_vma_node = NULL;
	mali_mem_backend *mem_bkend = NULL;
	int ret;

	session = (struct mali_session_data *)filp->private_data;
	if (NULL == session) {
		MALI_PRINT_ERROR(("mmap called without any session data available\n"));
		return -EFAULT;
	}

	MALI_DEBUG_PRINT(4, ("MMap() handler: start=0x%08X, phys=0x%08X, size=0x%08X vma->flags 0x%08x\n",
			     (unsigned int)vma->vm_start, (unsigned int)(vma->vm_pgoff << PAGE_SHIFT),
			     (unsigned int)(vma->vm_end - vma->vm_start), vma->vm_flags));

	/* Set some bits which indicate that, the memory is IO memory, meaning
	 * that no paging is to be performed and the memory should not be
	 * included in crash dumps. And that the memory is reserved, meaning
	 * that it's present and can never be paged out (see also previous
	 * entry)
	 */
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_DONTCOPY;
	vma->vm_flags |= VM_PFNMAP;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
	vma->vm_flags |= VM_RESERVED;
#else
	vma->vm_flags |= VM_DONTDUMP;
	vma->vm_flags |= VM_DONTEXPAND;
#endif

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mali_kernel_vm_ops;
	/* Operations used on any memory system */

	/* find mali allocation structure by vaddress*/
	mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, mali_addr, 0);
	if (likely(mali_vma_node)) {
		mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
		MALI_DEBUG_ASSERT(mali_addr == mali_vma_node->vm_node.start);
		if (unlikely(mali_addr != mali_vma_node->vm_node.start)) {
			/* only allow to use start address for mmap */
			return -EFAULT;
		}
	} else {
		MALI_DEBUG_ASSERT(NULL == mali_vma_node);
		return -EFAULT;
	}

	mali_alloc->cpu_mapping.addr = (void __user *)vma->vm_start;

	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	if (!(mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle))) {
		MALI_DEBUG_PRINT(1, ("Can't find memory backend in mmap!\n"));
		mutex_unlock(&mali_idr_mutex);
		return -EFAULT;
	}
	mutex_unlock(&mali_idr_mutex);

	if (mem_bkend->type == MALI_MEM_OS) {
		ret = mali_mem_os_cpu_map(&mem_bkend->os_mem, vma);
	} else if (mem_bkend->type == MALI_MEM_BLOCK) {
		ret = mali_mem_block_cpu_map(mem_bkend, vma);
	} else {
		/* Not support yet*/
		MALI_DEBUG_ASSERT(0);
		ret = -EFAULT;
	}

	if (ret != 0)
		return -EFAULT;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == mali_alloc->magic);

	vma->vm_private_data = (void *)mali_alloc;
	mali_allocation_ref(mali_alloc);

	return 0;
}

_mali_osk_errcode_t mali_mem_mali_map_prepare(mali_mem_allocation *descriptor)
{
	u32 size = descriptor->psize;
	struct mali_session_data *session = descriptor->session;

	MALI_DEBUG_ASSERT(MALI_MEM_ALLOCATION_VALID_MAGIC == descriptor->magic);

	/* Map dma-buf into this session's page tables */

	if (descriptor->flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	return mali_mmu_pagedir_map(session->page_directory, descriptor->mali_vma_node.vm_node.start, size);
}


void mali_mem_mali_map_free(struct mali_session_data *session, u32 size, mali_address_t vaddr, u32 flags)
{
	if (flags & MALI_MEM_FLAG_MALI_GUARD_PAGE) {
		size += MALI_MMU_PAGE_SIZE;
	}

	/* Umap and flush L2 */
	mali_mmu_pagedir_unmap(session->page_directory, vaddr, size);
	mali_executor_zap_all_active(session);
}

u32 _mali_ukk_report_memory_usage(void)
{
	u32 sum = 0;

	if (MALI_TRUE == mali_memory_have_dedicated_memory()) {
		sum += mali_mem_block_allocator_stat();
	}

	sum += mali_mem_os_stat();

	return sum;
}

u32 _mali_ukk_report_total_memory_size(void)
{
	return mali_dedicated_mem_size + mali_shared_mem_size;
}


/**
 * Per-session memory descriptor mapping table sizes
 */
#define MALI_MEM_DESCRIPTORS_INIT 64
#define MALI_MEM_DESCRIPTORS_MAX 65536

_mali_osk_errcode_t mali_memory_session_begin(struct mali_session_data *session_data)
{
	MALI_DEBUG_PRINT(5, ("Memory session begin\n"));

	session_data->memory_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
				    _MALI_OSK_LOCK_ORDER_MEM_SESSION);

	if (NULL == session_data->memory_lock) {
		_mali_osk_free(session_data);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	mali_memory_manager_init(&session_data->allocation_mgr);

	MALI_DEBUG_PRINT(5, ("MMU session begin: success\n"));
	MALI_SUCCESS;
}

void mali_memory_session_end(struct mali_session_data *session)
{
	MALI_DEBUG_PRINT(3, ("MMU session end\n"));

	if (NULL == session) {
		MALI_DEBUG_PRINT(1, ("No session data found during session end\n"));
		return;
	}
	/* free allocation */
	mali_free_session_allocations(session);
	/* do some check in unint*/
	mali_memory_manager_uninit(&session->allocation_mgr);

	/* Free the lock */
	_mali_osk_mutex_term(session->memory_lock);

	return;
}

_mali_osk_errcode_t mali_memory_initialize(void)
{
	idr_init(&mali_backend_idr);
	mutex_init(&mali_idr_mutex);
	return mali_mem_os_init();
}

void mali_memory_terminate(void)
{
	mali_mem_os_term();
	if (mali_memory_have_dedicated_memory()) {
		mali_mem_block_allocator_destroy();
	}
}


