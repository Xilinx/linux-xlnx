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
#include <linux/platform_device.h>

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_linux.h"
#include "mali_scheduler.h"

#include "mali_memory.h"
#include "mali_memory_os_alloc.h"
#if defined(CONFIG_DMA_SHARED_BUFFER)
#include "mali_memory_dma_buf.h"
#endif
#if defined(CONFIG_MALI400_UMP)
#include "mali_memory_ump.h"
#endif
#include "mali_memory_external.h"
#include "mali_memory_manager.h"
#include "mali_memory_virtual.h"
#include "mali_memory_block_alloc.h"

/**
*function @_mali_free_allocation_mem - free a memory allocation
* support backend type:
* MALI_MEM_OS
* maybe COW later?
*/
static void _mali_free_allocation_mem(struct kref *kref)
{
	mali_mem_backend *mem_bkend = NULL;

	mali_mem_allocation *mali_alloc = container_of(kref, struct mali_mem_allocation, ref);

	struct mali_session_data *session = mali_alloc->session;
	MALI_DEBUG_PRINT(4, (" _mali_free_allocation_mem, psize =0x%x! \n", mali_alloc->psize));
	if (0 == mali_alloc->psize)
		goto out;

	/* Get backend memory & Map on CPU */
	mutex_lock(&mali_idr_mutex);
	mem_bkend = idr_find(&mali_backend_idr, mali_alloc->backend_handle);
	mutex_unlock(&mali_idr_mutex);

	MALI_DEBUG_ASSERT(NULL != mem_bkend);

	switch (mem_bkend->type) {
	case MALI_MEM_OS:
		mali_mem_os_release(mem_bkend);
		break;
	case MALI_MEM_UMP:
#if defined(CONFIG_MALI400_UMP)
		mali_mem_ump_release(mem_bkend);
#else
		MALI_DEBUG_PRINT(2, ("DMA not supported\n"));
#endif
		break;
	case MALI_MEM_DMA_BUF:
#if defined(CONFIG_DMA_SHARED_BUFFER)
		mali_mem_dma_buf_release(mem_bkend);
#else
		MALI_DEBUG_PRINT(2, ("DMA not supported\n"));
#endif
		break;
	case MALI_MEM_EXTERNAL:
		mali_mem_external_release(mem_bkend);
		break;
	case MALI_MEM_BLOCK:
		mali_mem_block_release(mem_bkend);
		break;
	default:
		MALI_DEBUG_PRINT(1, ("mem type %d is not in the mali_mem_type enum.\n", mem_bkend->type));
		break;
	}

	/* remove backend memory idex */
	mutex_lock(&mali_idr_mutex);
	idr_remove(&mali_backend_idr, mali_alloc->backend_handle);
	mutex_unlock(&mali_idr_mutex);
	kfree(mem_bkend);
out:
	/* remove memory allocation  */
	mali_vma_offset_remove(&session->allocation_mgr, &mali_alloc->mali_vma_node);
	mali_mem_allocation_struct_destory(mali_alloc);

}



/**
*  ref_count for allocation
*/
void mali_allocation_unref(struct mali_mem_allocation **alloc)
{
	mali_mem_allocation *mali_alloc = *alloc;
	*alloc = NULL;
	kref_put(&mali_alloc->ref, _mali_free_allocation_mem);
}

void mali_allocation_ref(struct mali_mem_allocation *alloc)
{
	kref_get(&alloc->ref);
}

void mali_free_session_allocations(struct mali_session_data *session)
{

	struct mali_mem_allocation *entry, *next;

	MALI_DEBUG_PRINT(4, (" mali_free_session_allocations! \n"));

	list_for_each_entry_safe(entry, next, &session->allocation_mgr.head, list) {
		mali_allocation_unref(&entry);
	}
}

