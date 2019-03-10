// SPDX-License-Identifier: GPL-2.0
/*
 * Memory registrations helpers for RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#include "xcommon.h"
#include "xhw_config.h"

struct list_head mr_free;
struct list_head mr_alloc;

atomic_t pd_index = ATOMIC_INIT(0);
int free_mem_ceil;
int free_mem_remain;
void __iomem *mtt_va;

DECLARE_BITMAP(ernic_memtable, XRNIC_HW_MAX_QP_SUPPORT);
/**
 * alloc_pool_remove() - remove an entry from alloc pool
 * @chunk: memory region to be removed from alloc pool.
 * @return: 0 on success.
 *
 * TODO: Need to modify the return value as void and remove return statement.
 */
int alloc_pool_remove(struct mr *chunk)
{
	struct mr *next, *tmp;

	list_for_each_entry_safe(next, tmp, &mr_alloc, list) {
		if (next->paddr == chunk->paddr) {
			__list_del_entry(&next->list);
			free_mem_remain += chunk->len;
		}
	}
	return 0;
}

/**
 * free_pool_insert() -	inserts specified memory region in the free pool
 * @chunk:		memory region to be inserted in free pool.
 * @return: 0 on success. else, returns -ENOMEM.
 *
 * Adds the specified memory to the free pool and if possible,
 * merges it with adjacent regions in free pool.
 */
int free_pool_insert(struct mr *chunk)
{
	struct mr *next, *dup, *tmp;
	struct mr *prev = NULL;

	dup = kzalloc(sizeof(*dup), GFP_ATOMIC);
	memcpy(dup, chunk, sizeof(*dup));

	/* If list is empty, then, add the new region to the free pool */
	if (list_empty(&mr_free)) {
		list_add_tail(&dup->list, &mr_free);
		goto done;
	}

	/* If the new region size exceeds the free memory limit,
	 * return error.
	 */
	if (free_mem_ceil < (free_mem_remain + dup->len))
		return -ENOMEM;

	/* For a non-empty list, add the region at a suitable place
	 * in the free pool.
	 */
	list_for_each_entry_safe(next, tmp, &mr_free, list) {
		if (dup->paddr < next->paddr) {
			prev = list_prev_entry(next, list);
			list_add(&dup->list, &prev->list);
			goto merge_free_pool;
		}
	}
	/*
	 * If no suitable position to insert within free pool, then,
	 * append at the tail.
	 */
	list_add_tail(&dup->list, &mr_free);

	/* If possible, merge the region with previous and next regions. */
merge_free_pool:
	if (next && (dup->paddr + dup->len == next->paddr)) {
		dup->len += next->len;
		__list_del_entry(&next->list);
	}

	if (prev && (prev->paddr + prev->len == dup->paddr)) {
		prev->len += dup->len;
		__list_del_entry(&dup->list);
	}
	/* Except Phys and Virt address, clear all the contents of the region,
	 * If this region is in alloc pool, remove it from alloc pool.
	 */
done:
	dup->lkey = 0;
	dup->rkey = 0;
	dup->vaddr = 0;
	dup->access = MR_ACCESS_RESVD;
	alloc_pool_remove(chunk);
	return 0;
}
EXPORT_SYMBOL(free_pool_insert);

/**
 * alloc_pd() - Allocates a Protection Domain
 * @return: returns pointer to ernic_pd struct.
 *
 */
struct ernic_pd *alloc_pd(void)
{
	struct ernic_pd *new_pd;
	/* TODO: Need to check for return value and return ENOMEM */
	new_pd = kzalloc(sizeof(*new_pd), GFP_ATOMIC);
	atomic_inc(&pd_index);
	atomic_set(&new_pd->id, atomic_read(&pd_index));
	return new_pd;
}
EXPORT_SYMBOL(alloc_pd);

/**
 * dealloc_pd() - Allocates a Protection Domain
 * @pd: protection domain to be deallocated.
 *
 */
void dealloc_pd(struct ernic_pd *pd)
{
	atomic_dec(&pd_index);
	kfree(pd);
}
EXPORT_SYMBOL(dealloc_pd);

/**
 * dereg_mr() - deregisters the memory region from the Channel adapter.
 * @mr: memory region to be de-registered.
 *
 * dereg_mr() de-registers a memory region with CA and clears the memory region
 * registered with CA.
 */
void dereg_mr(struct mr *mr)
{
	int mtt_idx = (mr->rkey & 0xFF);

	//memset(mtt_va + mtt_offset, 0, sizeof(struct ernic_mtt));
	clear_bit(mtt_idx, ernic_memtable);
}
EXPORT_SYMBOL(dereg_mr);

/**
 * alloc_mem() - Allocates a Memory Region
 * @pd:     Protection domain mapped to the memory region
 * @len:    Length of the memory region required
 * @return: on success, returns the physical address.
 *		else, returns -ENOMEM.
 */
phys_addr_t alloc_mem(struct ernic_pd *pd, int len)
{
	struct mr *next, *new_alloc, *new_free, *tmp;
	int _len;

	_len = round_up(len, 256);
	new_alloc = kzalloc(sizeof(*new_alloc), GFP_KERNEL);
	new_free = kzalloc(sizeof(*new_free),  GFP_KERNEL);

	/* requested more memory than the free pool capacity? */
	if (free_mem_remain < _len)
		goto err;

	list_for_each_entry_safe(next, tmp, &mr_free, list) {
		if (next->len == _len) {
			new_alloc->paddr = next->paddr;
			__list_del_entry(&next->list);
			goto reg_mr;
		}
		if (next->len > _len) {
			__list_del_entry(&next->list);
			new_alloc->paddr = next->paddr;
			new_free->paddr = next->paddr + _len;
			new_free->len = next->len - _len;
			free_pool_insert(new_free);
			goto reg_mr;
		}
	}

err:
	/* No free memory of requested size */
	kfree(new_alloc);
	kfree(new_free);

	return -ENOMEM;
reg_mr:
	free_mem_remain = free_mem_remain - _len;
	new_alloc->pd = pd;
	new_alloc->len = _len;
	new_alloc->vaddr = (u64)(uintptr_t)ioremap(new_alloc->paddr, _len);
	list_add_tail(&new_alloc->list, &mr_alloc);
	return new_alloc->paddr;
}
EXPORT_SYMBOL(alloc_mem);

u64 get_virt_addr(phys_addr_t phys_addr)
{
	struct mr *next;

	list_for_each_entry(next, &mr_alloc, list) {
		if (next->paddr == phys_addr)
			return next->vaddr;
	}
	return 0;
}
EXPORT_SYMBOL(get_virt_addr);

/**
 * free_mem() - inserts a memory region in free pool and
 *		removes from alloc pool
 * @paddr: physical address to be freed.
 *
 */
void free_mem(phys_addr_t paddr)
{
	struct mr *next;

	list_for_each_entry(next, &mr_alloc, list) {
		if (next->paddr == paddr)
			goto found;
	}
	return;
found:
	iounmap((void __iomem *)(unsigned long)next->vaddr);
	free_pool_insert(next);
}
EXPORT_SYMBOL(free_mem);

/**
 * register_mem_to_ca() - Registers a memory region with the Channel Adapter
 * @mr:  memory region to register.
 * @return: a pointer to struct mr
 *
 * register_mem_to_ca() validates the memory region provided and registers
 * the memory region with the CA and updates the mkey in the registered region.
 *
 */
static struct mr *register_mem_to_ca(struct mr *mr)
{
	int bit, mtt_idx, offset;
	struct ernic_mtt mtt;

	bit = find_first_zero_bit(ernic_memtable, XRNIC_HW_MAX_QP_SUPPORT);
	set_bit(bit, ernic_memtable);
	mtt_idx = bit;
	mtt.pa = mr->paddr;
	mtt.iova = mr->vaddr;
	mtt.pd = atomic_read(&mr->pd->id);
	mr->rkey = (mtt_idx << 8) | bit;
	mtt.rkey = mr->rkey;
	mtt.access = mr->access;
	mtt.len = mr->len;
	offset = (int)(mtt_va + (mtt_idx * 0x100));

	iowrite32(mtt.pd, (void __iomem *)(offset + ERNIC_PD_OFFSET));
	iowrite32((mtt.iova & 0xFFFFFFFF),
		  (void __iomem *)(offset + ERNIC_IOVA_OFFSET));
	iowrite32(((mtt.iova >> 32) & 0xFFFFFFFF),
		  (void __iomem *)(offset + ERNIC_IOVA_OFFSET + 4));
	iowrite32((mtt.pa & 0xFFFFFFFF),
		  (void __iomem *)(offset + ERNIC_PA_OFFSET));
	iowrite32(((mtt.pa >> 32) & 0xFFFFFFFF),
		  (void __iomem *)(offset + ERNIC_PA_OFFSET + 4));
	iowrite32((mtt.rkey & 0xFFFF),
		  (void __iomem *)(offset + ERNIC_RKEY_OFFSET));
	iowrite32(mtt.len, (void __iomem *)(offset + ERNIC_LEN_OFFSET));
	iowrite32(mtt.access, (void __iomem *)(offset + ERNIC_ACCESS_OFFSET));
	return mr;
}

/**
 * reg_phys_mr() - Registers a physical address with the Channel Adapter
 * @pd: Protection domian associtated with the physical address.
 * @phys_addr: The physical address to be registered.
 * @len: length of the buffer to be registered.
 * @access: access permissions for the registered buffer.
 * @va_reg_base: Virtual address. Currently, ERNIC doesn't support either
 *               Base Memory Extensions or Zero Based VA. So, this arg is
 *               ignired for now. This is just to satisfy the Verbs Signature.
 * @return:	on success, returns a pointer to struct mr.
 *		else, returns a pointer to error.
 *
 * register_mem_to_ca() validates the memory region provided and registers
 * the memory region with the CA and updates the mkey in the registered region.
 */
struct mr *reg_phys_mr(struct ernic_pd *pd, phys_addr_t phys_addr,
		       int len, int access, void *va_reg_base)
{
	struct mr *phys_mr;
	struct mr *next;

	list_for_each_entry(next, &mr_alloc, list) {
		if (next->paddr == phys_addr)
			goto found;
	}
	/* Physical Address of the requested region is invalid */
	return ERR_PTR(-EINVAL);
found:
	phys_mr = kzalloc(sizeof(*phys_mr), GFP_KERNEL);
	phys_mr->paddr = phys_addr;
	phys_mr->vaddr = next->vaddr;
	phys_mr->len = len;
	phys_mr->access = access;
	phys_mr->pd = pd;

	return register_mem_to_ca(phys_mr);
}
EXPORT_SYMBOL(reg_phys_mr);

struct mr *query_mr(struct ernic_pd *pd)
{
	struct mr *next, *tmp;

	list_for_each_entry_safe(next, tmp, &mr_alloc, list) {
		if (atomic_read(&next->pd->id) == atomic_read(&pd->id)) {
			pr_info("Found MR\n");
			goto ret;
		}
	}
	return ERR_PTR(-EINVAL);
ret:
	return next;
}
EXPORT_SYMBOL(query_mr);

/**
 * dump_list() - prints all the regions for the specified list.
 * @head: HEAD pointer for the list to be printed.
 *
 * dump_list() iterates over the specified list HEAD and
 * prints all the physical address and length at each node in the list.
 */
static void dump_list(struct list_head *head)
{
	struct mr *next;

	list_for_each_entry(next, head, list) {
		pr_info("MR [%d:%s] Phys_addr = %#x, vaddr = %llx, len = %d\n",
			__LINE__, __func__,
			next->paddr, next->vaddr, next->len);
	}
}

/**
 * dump_free_list() - prints all the regions in the free pool.
 *
 * dump_free_list() is a wrapper function for dump_list()
 * to print free pool data
 *
 */
void dump_free_list(void)
{
	dump_list(&mr_free);
}
EXPORT_SYMBOL(dump_free_list);

/**
 * dump_alloc_list() - prints all the regions in the alloc pool.
 *
 * dump_alloc_list() is a wrapper function for dump_list()
 * to print alloc pool data
 */
void dump_alloc_list(void)
{
	dump_list(&mr_alloc);
}
EXPORT_SYMBOL(dump_alloc_list);

/**
 * init_mr() - Initialization function for memory region.
 * @addr: Physical Address of the starting memory region.
 * @length: Length of the region to initialize.
 * @return: 0 on success.
 *		else, -EINVAL.
 *
 * init_mr() initializes a region of free memory
 *
 * Note: This should be called only once by the RNIC driver.
 */
int init_mr(phys_addr_t addr, int length)
{
	struct mr *reg = kmalloc(sizeof(struct mr *), GFP_KERNEL);

	/* Multiple init_mr() calls? */
	if (free_mem_ceil > 0)
		return -EINVAL;

	INIT_LIST_HEAD(&mr_free);
	INIT_LIST_HEAD(&mr_alloc);
	reg->paddr = addr;
	reg->len = length;
	free_pool_insert(reg);
	free_mem_remain = reg->len;
	free_mem_ceil = free_mem_remain;
/* TODO: 0x2000 is the current Protection domain length for 255
 * Protection Domains.
 * Need to retrieve number of Protections doamins and length of each
 * protection domains from DTS and calculate the overall remap size for
 * all protection domains, instead of using a hard-coded value.
 * currently, length of each protection domain is not exported in DTS.
 */
	mtt_va = ioremap(MTT_BASE, 0x2000);
	return 0;
}
