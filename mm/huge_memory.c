/*
 *  Copyright (C) 2009  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/shrinker.h>
#include <linux/mm_inline.h>
#include <linux/swapops.h>
#include <linux/dax.h>
#include <linux/khugepaged.h>
#include <linux/freezer.h>
#include <linux/pfn_t.h>
#include <linux/mman.h>
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/migrate.h>
#include <linux/hashtable.h>
#include <linux/userfaultfd_k.h>
#include <linux/page_idle.h>
#include <linux/shmem_fs.h>

#include <asm/tlb.h>
#include <asm/pgalloc.h>
#include "internal.h"

/*
 * By default transparent hugepage support is disabled in order that avoid
 * to risk increase the memory footprint of applications without a guaranteed
 * benefit. When transparent hugepage support is enabled, is for all mappings,
 * and khugepaged scans all mappings.
 * Defrag is invoked by khugepaged hugepage allocations and by page faults
 * for all hugepage allocations.
 */
unsigned long transparent_hugepage_flags __read_mostly =
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS
	(1<<TRANSPARENT_HUGEPAGE_FLAG)|
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE_MADVISE
	(1<<TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG)|
#endif
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_DEFRAG_KHUGEPAGED_FLAG)|
	(1<<TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);

static struct shrinker deferred_split_shrinker;

static atomic_t huge_zero_refcount;
struct page *huge_zero_page __read_mostly;

static struct page *get_huge_zero_page(void)
{
	struct page *zero_page;
retry:
	if (likely(atomic_inc_not_zero(&huge_zero_refcount)))
		return READ_ONCE(huge_zero_page);

	zero_page = alloc_pages((GFP_TRANSHUGE | __GFP_ZERO) & ~__GFP_MOVABLE,
			HPAGE_PMD_ORDER);
	if (!zero_page) {
		count_vm_event(THP_ZERO_PAGE_ALLOC_FAILED);
		return NULL;
	}
	count_vm_event(THP_ZERO_PAGE_ALLOC);
	preempt_disable();
	if (cmpxchg(&huge_zero_page, NULL, zero_page)) {
		preempt_enable();
		__free_pages(zero_page, compound_order(zero_page));
		goto retry;
	}

	/* We take additional reference here. It will be put back by shrinker */
	atomic_set(&huge_zero_refcount, 2);
	preempt_enable();
	return READ_ONCE(huge_zero_page);
}

static void put_huge_zero_page(void)
{
	/*
	 * Counter should never go to zero here. Only shrinker can put
	 * last reference.
	 */
	BUG_ON(atomic_dec_and_test(&huge_zero_refcount));
}

struct page *mm_get_huge_zero_page(struct mm_struct *mm)
{
	if (test_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		return READ_ONCE(huge_zero_page);

	if (!get_huge_zero_page())
		return NULL;

	if (test_and_set_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		put_huge_zero_page();

	return READ_ONCE(huge_zero_page);
}

void mm_put_huge_zero_page(struct mm_struct *mm)
{
	if (test_bit(MMF_HUGE_ZERO_PAGE, &mm->flags))
		put_huge_zero_page();
}

static unsigned long shrink_huge_zero_page_count(struct shrinker *shrink,
					struct shrink_control *sc)
{
	/* we can free zero page only if last reference remains */
	return atomic_read(&huge_zero_refcount) == 1 ? HPAGE_PMD_NR : 0;
}

static unsigned long shrink_huge_zero_page_scan(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	if (atomic_cmpxchg(&huge_zero_refcount, 1, 0) == 1) {
		struct page *zero_page = xchg(&huge_zero_page, NULL);
		BUG_ON(zero_page == NULL);
		__free_pages(zero_page, compound_order(zero_page));
		return HPAGE_PMD_NR;
	}

	return 0;
}

static struct shrinker huge_zero_page_shrinker = {
	.count_objects = shrink_huge_zero_page_count,
	.scan_objects = shrink_huge_zero_page_scan,
	.seeks = DEFAULT_SEEKS,
};

#ifdef CONFIG_SYSFS

static ssize_t triple_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag enabled,
				 enum transparent_hugepage_flag deferred,
				 enum transparent_hugepage_flag req_madv)
{
	if (!memcmp("defer", buf,
		    min(sizeof("defer")-1, count))) {
		if (enabled == deferred)
			return -EINVAL;
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		set_bit(deferred, &transparent_hugepage_flags);
	} else if (!memcmp("always", buf,
		    min(sizeof("always")-1, count))) {
		clear_bit(deferred, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		set_bit(enabled, &transparent_hugepage_flags);
	} else if (!memcmp("madvise", buf,
			   min(sizeof("madvise")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(deferred, &transparent_hugepage_flags);
		set_bit(req_madv, &transparent_hugepage_flags);
	} else if (!memcmp("never", buf,
			   min(sizeof("never")-1, count))) {
		clear_bit(enabled, &transparent_hugepage_flags);
		clear_bit(req_madv, &transparent_hugepage_flags);
		clear_bit(deferred, &transparent_hugepage_flags);
	} else
		return -EINVAL;

	return count;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	if (test_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "[always] madvise never\n");
	else if (test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always [madvise] never\n");
	else
		return sprintf(buf, "always madvise [never]\n");
}

static ssize_t enabled_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	ssize_t ret;

	ret = triple_flag_store(kobj, attr, buf, count,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_FLAG,
				TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG);

	if (ret > 0) {
		int err = start_stop_khugepaged();
		if (err)
			ret = err;
	}

	return ret;
}
static struct kobj_attribute enabled_attr =
	__ATTR(enabled, 0644, enabled_show, enabled_store);

ssize_t single_hugepage_flag_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf,
				enum transparent_hugepage_flag flag)
{
	return sprintf(buf, "%d\n",
		       !!test_bit(flag, &transparent_hugepage_flags));
}

ssize_t single_hugepage_flag_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count,
				 enum transparent_hugepage_flag flag)
{
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0)
		return ret;
	if (value > 1)
		return -EINVAL;

	if (value)
		set_bit(flag, &transparent_hugepage_flags);
	else
		clear_bit(flag, &transparent_hugepage_flags);

	return count;
}

/*
 * Currently defrag only disables __GFP_NOWAIT for allocation. A blind
 * __GFP_REPEAT is too aggressive, it's never worth swapping tons of
 * memory just to allocate one more hugepage.
 */
static ssize_t defrag_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "[always] defer madvise never\n");
	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always [defer] madvise never\n");
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG, &transparent_hugepage_flags))
		return sprintf(buf, "always defer [madvise] never\n");
	else
		return sprintf(buf, "always defer madvise [never]\n");

}
static ssize_t defrag_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	return triple_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG,
				 TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG,
				 TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG);
}
static struct kobj_attribute defrag_attr =
	__ATTR(defrag, 0644, defrag_show, defrag_store);

static ssize_t use_zero_page_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static ssize_t use_zero_page_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_USE_ZERO_PAGE_FLAG);
}
static struct kobj_attribute use_zero_page_attr =
	__ATTR(use_zero_page, 0644, use_zero_page_show, use_zero_page_store);
#ifdef CONFIG_DEBUG_VM
static ssize_t debug_cow_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return single_hugepage_flag_show(kobj, attr, buf,
				TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static ssize_t debug_cow_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	return single_hugepage_flag_store(kobj, attr, buf, count,
				 TRANSPARENT_HUGEPAGE_DEBUG_COW_FLAG);
}
static struct kobj_attribute debug_cow_attr =
	__ATTR(debug_cow, 0644, debug_cow_show, debug_cow_store);
#endif /* CONFIG_DEBUG_VM */

static struct attribute *hugepage_attr[] = {
	&enabled_attr.attr,
	&defrag_attr.attr,
	&use_zero_page_attr.attr,
#if defined(CONFIG_SHMEM) && defined(CONFIG_TRANSPARENT_HUGE_PAGECACHE)
	&shmem_enabled_attr.attr,
#endif
#ifdef CONFIG_DEBUG_VM
	&debug_cow_attr.attr,
#endif
	NULL,
};

static struct attribute_group hugepage_attr_group = {
	.attrs = hugepage_attr,
};

static int __init hugepage_init_sysfs(struct kobject **hugepage_kobj)
{
	int err;

	*hugepage_kobj = kobject_create_and_add("transparent_hugepage", mm_kobj);
	if (unlikely(!*hugepage_kobj)) {
		pr_err("failed to create transparent hugepage kobject\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(*hugepage_kobj, &hugepage_attr_group);
	if (err) {
		pr_err("failed to register transparent hugepage group\n");
		goto delete_obj;
	}

	err = sysfs_create_group(*hugepage_kobj, &khugepaged_attr_group);
	if (err) {
		pr_err("failed to register transparent hugepage group\n");
		goto remove_hp_group;
	}

	return 0;

remove_hp_group:
	sysfs_remove_group(*hugepage_kobj, &hugepage_attr_group);
delete_obj:
	kobject_put(*hugepage_kobj);
	return err;
}

static void __init hugepage_exit_sysfs(struct kobject *hugepage_kobj)
{
	sysfs_remove_group(hugepage_kobj, &khugepaged_attr_group);
	sysfs_remove_group(hugepage_kobj, &hugepage_attr_group);
	kobject_put(hugepage_kobj);
}
#else
static inline int hugepage_init_sysfs(struct kobject **hugepage_kobj)
{
	return 0;
}

static inline void hugepage_exit_sysfs(struct kobject *hugepage_kobj)
{
}
#endif /* CONFIG_SYSFS */

static int __init hugepage_init(void)
{
	int err;
	struct kobject *hugepage_kobj;

	if (!has_transparent_hugepage()) {
		transparent_hugepage_flags = 0;
		return -EINVAL;
	}

	/*
	 * hugepages can't be allocated by the buddy allocator
	 */
	MAYBE_BUILD_BUG_ON(HPAGE_PMD_ORDER >= MAX_ORDER);
	/*
	 * we use page->mapping and page->index in second tail page
	 * as list_head: assuming THP order >= 2
	 */
	MAYBE_BUILD_BUG_ON(HPAGE_PMD_ORDER < 2);

	err = hugepage_init_sysfs(&hugepage_kobj);
	if (err)
		goto err_sysfs;

	err = khugepaged_init();
	if (err)
		goto err_slab;

	err = register_shrinker(&huge_zero_page_shrinker);
	if (err)
		goto err_hzp_shrinker;
	err = register_shrinker(&deferred_split_shrinker);
	if (err)
		goto err_split_shrinker;

	/*
	 * By default disable transparent hugepages on smaller systems,
	 * where the extra memory used could hurt more than TLB overhead
	 * is likely to save.  The admin can still enable it through /sys.
	 */
	if (totalram_pages < (512 << (20 - PAGE_SHIFT))) {
		transparent_hugepage_flags = 0;
		return 0;
	}

	err = start_stop_khugepaged();
	if (err)
		goto err_khugepaged;

	return 0;
err_khugepaged:
	unregister_shrinker(&deferred_split_shrinker);
err_split_shrinker:
	unregister_shrinker(&huge_zero_page_shrinker);
err_hzp_shrinker:
	khugepaged_destroy();
err_slab:
	hugepage_exit_sysfs(hugepage_kobj);
err_sysfs:
	return err;
}
subsys_initcall(hugepage_init);

static int __init setup_transparent_hugepage(char *str)
{
	int ret = 0;
	if (!str)
		goto out;
	if (!strcmp(str, "always")) {
		set_bit(TRANSPARENT_HUGEPAGE_FLAG,
			&transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "madvise")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			&transparent_hugepage_flags);
		ret = 1;
	} else if (!strcmp(str, "never")) {
		clear_bit(TRANSPARENT_HUGEPAGE_FLAG,
			  &transparent_hugepage_flags);
		clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG,
			  &transparent_hugepage_flags);
		ret = 1;
	}
out:
	if (!ret)
		pr_warn("transparent_hugepage= cannot parse, ignored\n");
	return ret;
}
__setup("transparent_hugepage=", setup_transparent_hugepage);

pmd_t maybe_pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (likely(vma->vm_flags & VM_WRITE))
		pmd = pmd_mkwrite(pmd);
	return pmd;
}

static inline struct list_head *page_deferred_list(struct page *page)
{
	/*
	 * ->lru in the tail pages is occupied by compound_head.
	 * Let's use ->mapping + ->index in the second tail page as list_head.
	 */
	return (struct list_head *)&page[2].mapping;
}

void prep_transhuge_page(struct page *page)
{
	/*
	 * we use page->mapping and page->indexlru in second tail page
	 * as list_head: assuming THP order >= 2
	 */

	INIT_LIST_HEAD(page_deferred_list(page));
	set_compound_page_dtor(page, TRANSHUGE_PAGE_DTOR);
}

unsigned long __thp_get_unmapped_area(struct file *filp, unsigned long len,
		loff_t off, unsigned long flags, unsigned long size)
{
	unsigned long addr;
	loff_t off_end = off + len;
	loff_t off_align = round_up(off, size);
	unsigned long len_pad;

	if (off_end <= off_align || (off_end - off_align) < size)
		return 0;

	len_pad = len + size;
	if (len_pad < len || (off + len_pad) < off)
		return 0;

	addr = current->mm->get_unmapped_area(filp, 0, len_pad,
					      off >> PAGE_SHIFT, flags);
	if (IS_ERR_VALUE(addr))
		return 0;

	addr += (off - addr) & (size - 1);
	return addr;
}

unsigned long thp_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	loff_t off = (loff_t)pgoff << PAGE_SHIFT;

	if (addr)
		goto out;
	if (!IS_DAX(filp->f_mapping->host) || !IS_ENABLED(CONFIG_FS_DAX_PMD))
		goto out;

	addr = __thp_get_unmapped_area(filp, len, off, flags, PMD_SIZE);
	if (addr)
		return addr;

 out:
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}
EXPORT_SYMBOL_GPL(thp_get_unmapped_area);

static int __do_huge_pmd_anonymous_page(struct fault_env *fe, struct page *page,
		gfp_t gfp)
{
	struct vm_area_struct *vma = fe->vma;
	struct mem_cgroup *memcg;
	pgtable_t pgtable;
	unsigned long haddr = fe->address & HPAGE_PMD_MASK;

	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (mem_cgroup_try_charge(page, vma->vm_mm, gfp, &memcg, true)) {
		put_page(page);
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}

	pgtable = pte_alloc_one(vma->vm_mm, haddr);
	if (unlikely(!pgtable)) {
		mem_cgroup_cancel_charge(page, memcg, true);
		put_page(page);
		return VM_FAULT_OOM;
	}

	clear_huge_page(page, haddr, HPAGE_PMD_NR);
	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * clear_huge_page writes become visible before the set_pmd_at()
	 * write.
	 */
	__SetPageUptodate(page);

	fe->ptl = pmd_lock(vma->vm_mm, fe->pmd);
	if (unlikely(!pmd_none(*fe->pmd))) {
		spin_unlock(fe->ptl);
		mem_cgroup_cancel_charge(page, memcg, true);
		put_page(page);
		pte_free(vma->vm_mm, pgtable);
	} else {
		pmd_t entry;

		/* Deliver the page fault to userland */
		if (userfaultfd_missing(vma)) {
			int ret;

			spin_unlock(fe->ptl);
			mem_cgroup_cancel_charge(page, memcg, true);
			put_page(page);
			pte_free(vma->vm_mm, pgtable);
			ret = handle_userfault(fe, VM_UFFD_MISSING);
			VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			return ret;
		}

		entry = mk_huge_pmd(page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		page_add_new_anon_rmap(page, vma, haddr, true);
		mem_cgroup_commit_charge(page, memcg, false, true);
		lru_cache_add_active_or_unevictable(page, vma);
		pgtable_trans_huge_deposit(vma->vm_mm, fe->pmd, pgtable);
		set_pmd_at(vma->vm_mm, haddr, fe->pmd, entry);
		add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		atomic_long_inc(&vma->vm_mm->nr_ptes);
		spin_unlock(fe->ptl);
		count_vm_event(THP_FAULT_ALLOC);
	}

	return 0;
}

/*
 * If THP defrag is set to always then directly reclaim/compact as necessary
 * If set to defer then do only background reclaim/compact and defer to khugepaged
 * If set to madvise and the VMA is flagged then directly reclaim/compact
 * When direct reclaim/compact is allowed, don't retry except for flagged VMA's
 */
static inline gfp_t alloc_hugepage_direct_gfpmask(struct vm_area_struct *vma)
{
	bool vma_madvised = !!(vma->vm_flags & VM_HUGEPAGE);

	if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_REQ_MADV_FLAG,
				&transparent_hugepage_flags) && vma_madvised)
		return GFP_TRANSHUGE;
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_KSWAPD_FLAG,
						&transparent_hugepage_flags))
		return GFP_TRANSHUGE_LIGHT | __GFP_KSWAPD_RECLAIM;
	else if (test_bit(TRANSPARENT_HUGEPAGE_DEFRAG_DIRECT_FLAG,
						&transparent_hugepage_flags))
		return GFP_TRANSHUGE | (vma_madvised ? 0 : __GFP_NORETRY);

	return GFP_TRANSHUGE_LIGHT;
}

/* Caller must hold page table lock. */
static bool set_huge_zero_page(pgtable_t pgtable, struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long haddr, pmd_t *pmd,
		struct page *zero_page)
{
	pmd_t entry;
	if (!pmd_none(*pmd))
		return false;
	entry = mk_pmd(zero_page, vma->vm_page_prot);
	entry = pmd_mkhuge(entry);
	if (pgtable)
		pgtable_trans_huge_deposit(mm, pmd, pgtable);
	set_pmd_at(mm, haddr, pmd, entry);
	atomic_long_inc(&mm->nr_ptes);
	return true;
}

int do_huge_pmd_anonymous_page(struct fault_env *fe)
{
	struct vm_area_struct *vma = fe->vma;
	gfp_t gfp;
	struct page *page;
	unsigned long haddr = fe->address & HPAGE_PMD_MASK;

	if (haddr < vma->vm_start || haddr + HPAGE_PMD_SIZE > vma->vm_end)
		return VM_FAULT_FALLBACK;
	if (unlikely(anon_vma_prepare(vma)))
		return VM_FAULT_OOM;
	if (unlikely(khugepaged_enter(vma, vma->vm_flags)))
		return VM_FAULT_OOM;
	if (!(fe->flags & FAULT_FLAG_WRITE) &&
			!mm_forbids_zeropage(vma->vm_mm) &&
			transparent_hugepage_use_zero_page()) {
		pgtable_t pgtable;
		struct page *zero_page;
		bool set;
		int ret;
		pgtable = pte_alloc_one(vma->vm_mm, haddr);
		if (unlikely(!pgtable))
			return VM_FAULT_OOM;
		zero_page = mm_get_huge_zero_page(vma->vm_mm);
		if (unlikely(!zero_page)) {
			pte_free(vma->vm_mm, pgtable);
			count_vm_event(THP_FAULT_FALLBACK);
			return VM_FAULT_FALLBACK;
		}
		fe->ptl = pmd_lock(vma->vm_mm, fe->pmd);
		ret = 0;
		set = false;
		if (pmd_none(*fe->pmd)) {
			if (userfaultfd_missing(vma)) {
				spin_unlock(fe->ptl);
				ret = handle_userfault(fe, VM_UFFD_MISSING);
				VM_BUG_ON(ret & VM_FAULT_FALLBACK);
			} else {
				set_huge_zero_page(pgtable, vma->vm_mm, vma,
						   haddr, fe->pmd, zero_page);
				spin_unlock(fe->ptl);
				set = true;
			}
		} else
			spin_unlock(fe->ptl);
		if (!set)
			pte_free(vma->vm_mm, pgtable);
		return ret;
	}
	gfp = alloc_hugepage_direct_gfpmask(vma);
	page = alloc_hugepage_vma(gfp, vma, haddr, HPAGE_PMD_ORDER);
	if (unlikely(!page)) {
		count_vm_event(THP_FAULT_FALLBACK);
		return VM_FAULT_FALLBACK;
	}
	prep_transhuge_page(page);
	return __do_huge_pmd_anonymous_page(fe, page, gfp);
}

static void insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, pfn_t pfn, pgprot_t prot, bool write)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t entry;
	spinlock_t *ptl;

	ptl = pmd_lock(mm, pmd);
	entry = pmd_mkhuge(pfn_t_pmd(pfn, prot));
	if (pfn_t_devmap(pfn))
		entry = pmd_mkdevmap(entry);
	if (write) {
		entry = pmd_mkyoung(pmd_mkdirty(entry));
		entry = maybe_pmd_mkwrite(entry, vma);
	}
	set_pmd_at(mm, addr, pmd, entry);
	update_mmu_cache_pmd(vma, addr, pmd);
	spin_unlock(ptl);
}

int vmf_insert_pfn_pmd(struct vm_area_struct *vma, unsigned long addr,
			pmd_t *pmd, pfn_t pfn, bool write)
{
	pgprot_t pgprot = vma->vm_page_prot;
	/*
	 * If we had pmd_special, we could avoid all these restrictions,
	 * but we need to be consistent with PTEs and architectures that
	 * can't support a 'special' bit.
	 */
	BUG_ON(!(vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)));
	BUG_ON((vma->vm_flags & (VM_PFNMAP|VM_MIXEDMAP)) ==
						(VM_PFNMAP|VM_MIXEDMAP));
	BUG_ON((vma->vm_flags & VM_PFNMAP) && is_cow_mapping(vma->vm_flags));
	BUG_ON(!pfn_t_devmap(pfn));

	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;
	if (track_pfn_insert(vma, &pgprot, pfn))
		return VM_FAULT_SIGBUS;
	insert_pfn_pmd(vma, addr, pmd, pfn, pgprot, write);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(vmf_insert_pfn_pmd);

static void touch_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd)
{
	pmd_t _pmd;

	/*
	 * We should set the dirty bit only for FOLL_WRITE but for now
	 * the dirty bit in the pmd is meaningless.  And if the dirty
	 * bit will become meaningful and we'll only set it with
	 * FOLL_WRITE, an atomic set_bit will be required on the pmd to
	 * set the young bit, instead of the current set_pmd_at.
	 */
	_pmd = pmd_mkyoung(pmd_mkdirty(*pmd));
	if (pmdp_set_access_flags(vma, addr & HPAGE_PMD_MASK,
				pmd, _pmd,  1))
		update_mmu_cache_pmd(vma, addr, pmd);
}

struct page *follow_devmap_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, int flags)
{
	unsigned long pfn = pmd_pfn(*pmd);
	struct mm_struct *mm = vma->vm_mm;
	struct dev_pagemap *pgmap;
	struct page *page;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		return NULL;

	if (pmd_present(*pmd) && pmd_devmap(*pmd))
		/* pass */;
	else
		return NULL;

	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd);

	/*
	 * device mapped pages can only be returned if the
	 * caller will manage the page reference count.
	 */
	if (!(flags & FOLL_GET))
		return ERR_PTR(-EEXIST);

	pfn += (addr & ~PMD_MASK) >> PAGE_SHIFT;
	pgmap = get_dev_pagemap(pfn, NULL);
	if (!pgmap)
		return ERR_PTR(-EFAULT);
	page = pfn_to_page(pfn);
	get_page(page);
	put_dev_pagemap(pgmap);

	return page;
}

int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *vma)
{
	spinlock_t *dst_ptl, *src_ptl;
	struct page *src_page;
	pmd_t pmd;
	pgtable_t pgtable = NULL;
	int ret = -ENOMEM;

	/* Skip if can be re-fill on fault */
	if (!vma_is_anonymous(vma))
		return 0;

	pgtable = pte_alloc_one(dst_mm, addr);
	if (unlikely(!pgtable))
		goto out;

	dst_ptl = pmd_lock(dst_mm, dst_pmd);
	src_ptl = pmd_lockptr(src_mm, src_pmd);
	spin_lock_nested(src_ptl, SINGLE_DEPTH_NESTING);

	ret = -EAGAIN;
	pmd = *src_pmd;
	if (unlikely(!pmd_trans_huge(pmd))) {
		pte_free(dst_mm, pgtable);
		goto out_unlock;
	}
	/*
	 * When page table lock is held, the huge zero pmd should not be
	 * under splitting since we don't split the page itself, only pmd to
	 * a page table.
	 */
	if (is_huge_zero_pmd(pmd)) {
		struct page *zero_page;
		/*
		 * get_huge_zero_page() will never allocate a new page here,
		 * since we already have a zero page to copy. It just takes a
		 * reference.
		 */
		zero_page = mm_get_huge_zero_page(dst_mm);
		set_huge_zero_page(pgtable, dst_mm, vma, addr, dst_pmd,
				zero_page);
		ret = 0;
		goto out_unlock;
	}

	src_page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!PageHead(src_page), src_page);
	get_page(src_page);
	page_dup_rmap(src_page, true);
	add_mm_counter(dst_mm, MM_ANONPAGES, HPAGE_PMD_NR);
	atomic_long_inc(&dst_mm->nr_ptes);
	pgtable_trans_huge_deposit(dst_mm, dst_pmd, pgtable);

	pmdp_set_wrprotect(src_mm, addr, src_pmd);
	pmd = pmd_mkold(pmd_wrprotect(pmd));
	set_pmd_at(dst_mm, addr, dst_pmd, pmd);

	ret = 0;
out_unlock:
	spin_unlock(src_ptl);
	spin_unlock(dst_ptl);
out:
	return ret;
}

void huge_pmd_set_accessed(struct fault_env *fe, pmd_t orig_pmd)
{
	pmd_t entry;
	unsigned long haddr;

	fe->ptl = pmd_lock(fe->vma->vm_mm, fe->pmd);
	if (unlikely(!pmd_same(*fe->pmd, orig_pmd)))
		goto unlock;

	entry = pmd_mkyoung(orig_pmd);
	haddr = fe->address & HPAGE_PMD_MASK;
	if (pmdp_set_access_flags(fe->vma, haddr, fe->pmd, entry,
				fe->flags & FAULT_FLAG_WRITE))
		update_mmu_cache_pmd(fe->vma, fe->address, fe->pmd);

unlock:
	spin_unlock(fe->ptl);
}

static int do_huge_pmd_wp_page_fallback(struct fault_env *fe, pmd_t orig_pmd,
		struct page *page)
{
	struct vm_area_struct *vma = fe->vma;
	unsigned long haddr = fe->address & HPAGE_PMD_MASK;
	struct mem_cgroup *memcg;
	pgtable_t pgtable;
	pmd_t _pmd;
	int ret = 0, i;
	struct page **pages;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	pages = kmalloc(sizeof(struct page *) * HPAGE_PMD_NR,
			GFP_KERNEL);
	if (unlikely(!pages)) {
		ret |= VM_FAULT_OOM;
		goto out;
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		pages[i] = alloc_page_vma_node(GFP_HIGHUSER_MOVABLE |
					       __GFP_OTHER_NODE, vma,
					       fe->address, page_to_nid(page));
		if (unlikely(!pages[i] ||
			     mem_cgroup_try_charge(pages[i], vma->vm_mm,
				     GFP_KERNEL, &memcg, false))) {
			if (pages[i])
				put_page(pages[i]);
			while (--i >= 0) {
				memcg = (void *)page_private(pages[i]);
				set_page_private(pages[i], 0);
				mem_cgroup_cancel_charge(pages[i], memcg,
						false);
				put_page(pages[i]);
			}
			kfree(pages);
			ret |= VM_FAULT_OOM;
			goto out;
		}
		set_page_private(pages[i], (unsigned long)memcg);
	}

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		copy_user_highpage(pages[i], page + i,
				   haddr + PAGE_SIZE * i, vma);
		__SetPageUptodate(pages[i]);
		cond_resched();
	}

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(vma->vm_mm, mmun_start, mmun_end);

	fe->ptl = pmd_lock(vma->vm_mm, fe->pmd);
	if (unlikely(!pmd_same(*fe->pmd, orig_pmd)))
		goto out_free_pages;
	VM_BUG_ON_PAGE(!PageHead(page), page);

	pmdp_huge_clear_flush_notify(vma, haddr, fe->pmd);
	/* leave pmd empty until pte is filled */

	pgtable = pgtable_trans_huge_withdraw(vma->vm_mm, fe->pmd);
	pmd_populate(vma->vm_mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t entry;
		entry = mk_pte(pages[i], vma->vm_page_prot);
		entry = maybe_mkwrite(pte_mkdirty(entry), vma);
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		page_add_new_anon_rmap(pages[i], fe->vma, haddr, false);
		mem_cgroup_commit_charge(pages[i], memcg, false, false);
		lru_cache_add_active_or_unevictable(pages[i], vma);
		fe->pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*fe->pte));
		set_pte_at(vma->vm_mm, haddr, fe->pte, entry);
		pte_unmap(fe->pte);
	}
	kfree(pages);

	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(vma->vm_mm, fe->pmd, pgtable);
	page_remove_rmap(page, true);
	spin_unlock(fe->ptl);

	mmu_notifier_invalidate_range_end(vma->vm_mm, mmun_start, mmun_end);

	ret |= VM_FAULT_WRITE;
	put_page(page);

out:
	return ret;

out_free_pages:
	spin_unlock(fe->ptl);
	mmu_notifier_invalidate_range_end(vma->vm_mm, mmun_start, mmun_end);
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		memcg = (void *)page_private(pages[i]);
		set_page_private(pages[i], 0);
		mem_cgroup_cancel_charge(pages[i], memcg, false);
		put_page(pages[i]);
	}
	kfree(pages);
	goto out;
}

int do_huge_pmd_wp_page(struct fault_env *fe, pmd_t orig_pmd)
{
	struct vm_area_struct *vma = fe->vma;
	struct page *page = NULL, *new_page;
	struct mem_cgroup *memcg;
	unsigned long haddr = fe->address & HPAGE_PMD_MASK;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */
	gfp_t huge_gfp;			/* for allocation and charge */
	int ret = 0;

	fe->ptl = pmd_lockptr(vma->vm_mm, fe->pmd);
	VM_BUG_ON_VMA(!vma->anon_vma, vma);
	if (is_huge_zero_pmd(orig_pmd))
		goto alloc;
	spin_lock(fe->ptl);
	if (unlikely(!pmd_same(*fe->pmd, orig_pmd)))
		goto out_unlock;

	page = pmd_page(orig_pmd);
	VM_BUG_ON_PAGE(!PageCompound(page) || !PageHead(page), page);
	/*
	 * We can only reuse the page if nobody else maps the huge page or it's
	 * part.
	 */
	if (page_trans_huge_mapcount(page, NULL) == 1) {
		pmd_t entry;
		entry = pmd_mkyoung(orig_pmd);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		if (pmdp_set_access_flags(vma, haddr, fe->pmd, entry,  1))
			update_mmu_cache_pmd(vma, fe->address, fe->pmd);
		ret |= VM_FAULT_WRITE;
		goto out_unlock;
	}
	get_page(page);
	spin_unlock(fe->ptl);
alloc:
	if (transparent_hugepage_enabled(vma) &&
	    !transparent_hugepage_debug_cow()) {
		huge_gfp = alloc_hugepage_direct_gfpmask(vma);
		new_page = alloc_hugepage_vma(huge_gfp, vma, haddr, HPAGE_PMD_ORDER);
	} else
		new_page = NULL;

	if (likely(new_page)) {
		prep_transhuge_page(new_page);
	} else {
		if (!page) {
			split_huge_pmd(vma, fe->pmd, fe->address);
			ret |= VM_FAULT_FALLBACK;
		} else {
			ret = do_huge_pmd_wp_page_fallback(fe, orig_pmd, page);
			if (ret & VM_FAULT_OOM) {
				split_huge_pmd(vma, fe->pmd, fe->address);
				ret |= VM_FAULT_FALLBACK;
			}
			put_page(page);
		}
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	if (unlikely(mem_cgroup_try_charge(new_page, vma->vm_mm,
					huge_gfp, &memcg, true))) {
		put_page(new_page);
		split_huge_pmd(vma, fe->pmd, fe->address);
		if (page)
			put_page(page);
		ret |= VM_FAULT_FALLBACK;
		count_vm_event(THP_FAULT_FALLBACK);
		goto out;
	}

	count_vm_event(THP_FAULT_ALLOC);

	if (!page)
		clear_huge_page(new_page, haddr, HPAGE_PMD_NR);
	else
		copy_user_huge_page(new_page, page, haddr, vma, HPAGE_PMD_NR);
	__SetPageUptodate(new_page);

	mmun_start = haddr;
	mmun_end   = haddr + HPAGE_PMD_SIZE;
	mmu_notifier_invalidate_range_start(vma->vm_mm, mmun_start, mmun_end);

	spin_lock(fe->ptl);
	if (page)
		put_page(page);
	if (unlikely(!pmd_same(*fe->pmd, orig_pmd))) {
		spin_unlock(fe->ptl);
		mem_cgroup_cancel_charge(new_page, memcg, true);
		put_page(new_page);
		goto out_mn;
	} else {
		pmd_t entry;
		entry = mk_huge_pmd(new_page, vma->vm_page_prot);
		entry = maybe_pmd_mkwrite(pmd_mkdirty(entry), vma);
		pmdp_huge_clear_flush_notify(vma, haddr, fe->pmd);
		page_add_new_anon_rmap(new_page, vma, haddr, true);
		mem_cgroup_commit_charge(new_page, memcg, false, true);
		lru_cache_add_active_or_unevictable(new_page, vma);
		set_pmd_at(vma->vm_mm, haddr, fe->pmd, entry);
		update_mmu_cache_pmd(vma, fe->address, fe->pmd);
		if (!page) {
			add_mm_counter(vma->vm_mm, MM_ANONPAGES, HPAGE_PMD_NR);
		} else {
			VM_BUG_ON_PAGE(!PageHead(page), page);
			page_remove_rmap(page, true);
			put_page(page);
		}
		ret |= VM_FAULT_WRITE;
	}
	spin_unlock(fe->ptl);
out_mn:
	mmu_notifier_invalidate_range_end(vma->vm_mm, mmun_start, mmun_end);
out:
	return ret;
out_unlock:
	spin_unlock(fe->ptl);
	return ret;
}

struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page = NULL;

	assert_spin_locked(pmd_lockptr(mm, pmd));

	if (flags & FOLL_WRITE && !pmd_write(*pmd))
		goto out;

	/* Avoid dumping huge zero page */
	if ((flags & FOLL_DUMP) && is_huge_zero_pmd(*pmd))
		return ERR_PTR(-EFAULT);

	/* Full NUMA hinting faults to serialise migration in fault paths */
	if ((flags & FOLL_NUMA) && pmd_protnone(*pmd))
		goto out;

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!PageHead(page) && !is_zone_device_page(page), page);
	if (flags & FOLL_TOUCH)
		touch_pmd(vma, addr, pmd);
	if ((flags & FOLL_MLOCK) && (vma->vm_flags & VM_LOCKED)) {
		/*
		 * We don't mlock() pte-mapped THPs. This way we can avoid
		 * leaking mlocked pages into non-VM_LOCKED VMAs.
		 *
		 * For anon THP:
		 *
		 * In most cases the pmd is the only mapping of the page as we
		 * break COW for the mlock() -- see gup_flags |= FOLL_WRITE for
		 * writable private mappings in populate_vma_page_range().
		 *
		 * The only scenario when we have the page shared here is if we
		 * mlocking read-only mapping shared over fork(). We skip
		 * mlocking such pages.
		 *
		 * For file THP:
		 *
		 * We can expect PageDoubleMap() to be stable under page lock:
		 * for file pages we set it in page_add_file_rmap(), which
		 * requires page to be locked.
		 */

		if (PageAnon(page) && compound_mapcount(page) != 1)
			goto skip_mlock;
		if (PageDoubleMap(page) || !page->mapping)
			goto skip_mlock;
		if (!trylock_page(page))
			goto skip_mlock;
		lru_add_drain();
		if (page->mapping && !PageDoubleMap(page))
			mlock_vma_page(page);
		unlock_page(page);
	}
skip_mlock:
	page += (addr & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	VM_BUG_ON_PAGE(!PageCompound(page) && !is_zone_device_page(page), page);
	if (flags & FOLL_GET)
		get_page(page);

out:
	return page;
}

/* NUMA hinting page fault entry point for trans huge pmds */
int do_huge_pmd_numa_page(struct fault_env *fe, pmd_t pmd)
{
	struct vm_area_struct *vma = fe->vma;
	struct anon_vma *anon_vma = NULL;
	struct page *page;
	unsigned long haddr = fe->address & HPAGE_PMD_MASK;
	int page_nid = -1, this_nid = numa_node_id();
	int target_nid, last_cpupid = -1;
	bool page_locked;
	bool migrated = false;
	bool was_writable;
	int flags = 0;

	fe->ptl = pmd_lock(vma->vm_mm, fe->pmd);
	if (unlikely(!pmd_same(pmd, *fe->pmd)))
		goto out_unlock;

	/*
	 * If there are potential migrations, wait for completion and retry
	 * without disrupting NUMA hinting information. Do not relock and
	 * check_same as the page may no longer be mapped.
	 */
	if (unlikely(pmd_trans_migrating(*fe->pmd))) {
		page = pmd_page(*fe->pmd);
		spin_unlock(fe->ptl);
		wait_on_page_locked(page);
		goto out;
	}

	page = pmd_page(pmd);
	BUG_ON(is_huge_zero_page(page));
	page_nid = page_to_nid(page);
	last_cpupid = page_cpupid_last(page);
	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == this_nid) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		flags |= TNF_FAULT_LOCAL;
	}

	/* See similar comment in do_numa_page for explanation */
	if (!pmd_write(pmd))
		flags |= TNF_NO_GROUP;

	/*
	 * Acquire the page lock to serialise THP migrations but avoid dropping
	 * page_table_lock if at all possible
	 */
	page_locked = trylock_page(page);
	target_nid = mpol_misplaced(page, vma, haddr);
	if (target_nid == -1) {
		/* If the page was locked, there are no parallel migrations */
		if (page_locked)
			goto clear_pmdnuma;
	}

	/* Migration could have started since the pmd_trans_migrating check */
	if (!page_locked) {
		spin_unlock(fe->ptl);
		wait_on_page_locked(page);
		page_nid = -1;
		goto out;
	}

	/*
	 * Page is misplaced. Page lock serialises migrations. Acquire anon_vma
	 * to serialises splits
	 */
	get_page(page);
	spin_unlock(fe->ptl);
	anon_vma = page_lock_anon_vma_read(page);

	/* Confirm the PMD did not change while page_table_lock was released */
	spin_lock(fe->ptl);
	if (unlikely(!pmd_same(pmd, *fe->pmd))) {
		unlock_page(page);
		put_page(page);
		page_nid = -1;
		goto out_unlock;
	}

	/* Bail if we fail to protect against THP splits for any reason */
	if (unlikely(!anon_vma)) {
		put_page(page);
		page_nid = -1;
		goto clear_pmdnuma;
	}

	/*
	 * Migrate the THP to the requested node, returns with page unlocked
	 * and access rights restored.
	 */
	spin_unlock(fe->ptl);
	migrated = migrate_misplaced_transhuge_page(vma->vm_mm, vma,
				fe->pmd, pmd, fe->address, page, target_nid);
	if (migrated) {
		flags |= TNF_MIGRATED;
		page_nid = target_nid;
	} else
		flags |= TNF_MIGRATE_FAIL;

	goto out;
clear_pmdnuma:
	BUG_ON(!PageLocked(page));
	was_writable = pmd_write(pmd);
	pmd = pmd_modify(pmd, vma->vm_page_prot);
	pmd = pmd_mkyoung(pmd);
	if (was_writable)
		pmd = pmd_mkwrite(pmd);
	set_pmd_at(vma->vm_mm, haddr, fe->pmd, pmd);
	update_mmu_cache_pmd(vma, fe->address, fe->pmd);
	unlock_page(page);
out_unlock:
	spin_unlock(fe->ptl);

out:
	if (anon_vma)
		page_unlock_anon_vma_read(anon_vma);

	if (page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, HPAGE_PMD_NR, fe->flags);

	return 0;
}

/*
 * Return true if we do MADV_FREE successfully on entire pmd page.
 * Otherwise, return false.
 */
bool madvise_free_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		pmd_t *pmd, unsigned long addr, unsigned long next)
{
	spinlock_t *ptl;
	pmd_t orig_pmd;
	struct page *page;
	struct mm_struct *mm = tlb->mm;
	bool ret = false;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		goto out_unlocked;

	orig_pmd = *pmd;
	if (is_huge_zero_pmd(orig_pmd))
		goto out;

	page = pmd_page(orig_pmd);
	/*
	 * If other processes are mapping this page, we couldn't discard
	 * the page unless they all do MADV_FREE so let's skip the page.
	 */
	if (page_mapcount(page) != 1)
		goto out;

	if (!trylock_page(page))
		goto out;

	/*
	 * If user want to discard part-pages of THP, split it so MADV_FREE
	 * will deactivate only them.
	 */
	if (next - addr != HPAGE_PMD_SIZE) {
		get_page(page);
		spin_unlock(ptl);
		split_huge_page(page);
		put_page(page);
		unlock_page(page);
		goto out_unlocked;
	}

	if (PageDirty(page))
		ClearPageDirty(page);
	unlock_page(page);

	if (PageActive(page))
		deactivate_page(page);

	if (pmd_young(orig_pmd) || pmd_dirty(orig_pmd)) {
		orig_pmd = pmdp_huge_get_and_clear_full(tlb->mm, addr, pmd,
			tlb->fullmm);
		orig_pmd = pmd_mkold(orig_pmd);
		orig_pmd = pmd_mkclean(orig_pmd);

		set_pmd_at(mm, addr, pmd, orig_pmd);
		tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	}
	ret = true;
out:
	spin_unlock(ptl);
out_unlocked:
	return ret;
}

int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	pmd_t orig_pmd;
	spinlock_t *ptl;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return 0;
	/*
	 * For architectures like ppc64 we look at deposited pgtable
	 * when calling pmdp_huge_get_and_clear. So do the
	 * pgtable_trans_huge_withdraw after finishing pmdp related
	 * operations.
	 */
	orig_pmd = pmdp_huge_get_and_clear_full(tlb->mm, addr, pmd,
			tlb->fullmm);
	tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
	if (vma_is_dax(vma)) {
		spin_unlock(ptl);
		if (is_huge_zero_pmd(orig_pmd))
			tlb_remove_page(tlb, pmd_page(orig_pmd));
	} else if (is_huge_zero_pmd(orig_pmd)) {
		pte_free(tlb->mm, pgtable_trans_huge_withdraw(tlb->mm, pmd));
		atomic_long_dec(&tlb->mm->nr_ptes);
		spin_unlock(ptl);
		tlb_remove_page(tlb, pmd_page(orig_pmd));
	} else {
		struct page *page = pmd_page(orig_pmd);
		page_remove_rmap(page, true);
		VM_BUG_ON_PAGE(page_mapcount(page) < 0, page);
		VM_BUG_ON_PAGE(!PageHead(page), page);
		if (PageAnon(page)) {
			pgtable_t pgtable;
			pgtable = pgtable_trans_huge_withdraw(tlb->mm, pmd);
			pte_free(tlb->mm, pgtable);
			atomic_long_dec(&tlb->mm->nr_ptes);
			add_mm_counter(tlb->mm, MM_ANONPAGES, -HPAGE_PMD_NR);
		} else {
			add_mm_counter(tlb->mm, MM_FILEPAGES, -HPAGE_PMD_NR);
		}
		spin_unlock(ptl);
		tlb_remove_page_size(tlb, page, HPAGE_PMD_SIZE);
	}
	return 1;
}

bool move_huge_pmd(struct vm_area_struct *vma, unsigned long old_addr,
		  unsigned long new_addr, unsigned long old_end,
		  pmd_t *old_pmd, pmd_t *new_pmd, bool *need_flush)
{
	spinlock_t *old_ptl, *new_ptl;
	pmd_t pmd;
	struct mm_struct *mm = vma->vm_mm;
	bool force_flush = false;

	if ((old_addr & ~HPAGE_PMD_MASK) ||
	    (new_addr & ~HPAGE_PMD_MASK) ||
	    old_end - old_addr < HPAGE_PMD_SIZE)
		return false;

	/*
	 * The destination pmd shouldn't be established, free_pgtables()
	 * should have release it.
	 */
	if (WARN_ON(!pmd_none(*new_pmd))) {
		VM_BUG_ON(pmd_trans_huge(*new_pmd));
		return false;
	}

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_sem prevents deadlock.
	 */
	old_ptl = __pmd_trans_huge_lock(old_pmd, vma);
	if (old_ptl) {
		new_ptl = pmd_lockptr(mm, new_pmd);
		if (new_ptl != old_ptl)
			spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);
		pmd = pmdp_huge_get_and_clear(mm, old_addr, old_pmd);
		if (pmd_present(pmd) && pmd_dirty(pmd))
			force_flush = true;
		VM_BUG_ON(!pmd_none(*new_pmd));

		if (pmd_move_must_withdraw(new_ptl, old_ptl) &&
				vma_is_anonymous(vma)) {
			pgtable_t pgtable;
			pgtable = pgtable_trans_huge_withdraw(mm, old_pmd);
			pgtable_trans_huge_deposit(mm, new_pmd, pgtable);
		}
		set_pmd_at(mm, new_addr, new_pmd, pmd_mksoft_dirty(pmd));
		if (new_ptl != old_ptl)
			spin_unlock(new_ptl);
		if (force_flush)
			flush_tlb_range(vma, old_addr, old_addr + PMD_SIZE);
		else
			*need_flush = true;
		spin_unlock(old_ptl);
		return true;
	}
	return false;
}

/*
 * Returns
 *  - 0 if PMD could not be locked
 *  - 1 if PMD was locked but protections unchange and TLB flush unnecessary
 *  - HPAGE_PMD_NR is protections changed and TLB flush necessary
 */
int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, pgprot_t newprot, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	spinlock_t *ptl;
	int ret = 0;

	ptl = __pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		pmd_t entry;
		bool preserve_write = prot_numa && pmd_write(*pmd);
		ret = 1;

		/*
		 * Avoid trapping faults against the zero page. The read-only
		 * data is likely to be read-cached on the local CPU and
		 * local/remote hits to the zero page are not interesting.
		 */
		if (prot_numa && is_huge_zero_pmd(*pmd)) {
			spin_unlock(ptl);
			return ret;
		}

		if (!prot_numa || !pmd_protnone(*pmd)) {
			entry = pmdp_huge_get_and_clear_notify(mm, addr, pmd);
			entry = pmd_modify(entry, newprot);
			if (preserve_write)
				entry = pmd_mkwrite(entry);
			ret = HPAGE_PMD_NR;
			set_pmd_at(mm, addr, pmd, entry);
			BUG_ON(vma_is_anonymous(vma) && !preserve_write &&
					pmd_write(entry));
		}
		spin_unlock(ptl);
	}

	return ret;
}

/*
 * Returns page table lock pointer if a given pmd maps a thp, NULL otherwise.
 *
 * Note that if it returns page table lock pointer, this routine returns without
 * unlocking page table lock. So callers must unlock it.
 */
spinlock_t *__pmd_trans_huge_lock(pmd_t *pmd, struct vm_area_struct *vma)
{
	spinlock_t *ptl;
	ptl = pmd_lock(vma->vm_mm, pmd);
	if (likely(pmd_trans_huge(*pmd) || pmd_devmap(*pmd)))
		return ptl;
	spin_unlock(ptl);
	return NULL;
}

static void __split_huge_zero_page_pmd(struct vm_area_struct *vma,
		unsigned long haddr, pmd_t *pmd)
{
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pgtable;
	pmd_t _pmd;
	int i;

	/* leave pmd empty until pte is filled */
	pmdp_huge_clear_flush_notify(vma, haddr, pmd);

	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0; i < HPAGE_PMD_NR; i++, haddr += PAGE_SIZE) {
		pte_t *pte, entry;
		entry = pfn_pte(my_zero_pfn(haddr), vma->vm_page_prot);
		entry = pte_mkspecial(entry);
		pte = pte_offset_map(&_pmd, haddr);
		VM_BUG_ON(!pte_none(*pte));
		set_pte_at(mm, haddr, pte, entry);
		pte_unmap(pte);
	}
	smp_wmb(); /* make pte visible before pmd */
	pmd_populate(mm, pmd, pgtable);
}

static void __split_huge_pmd_locked(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long haddr, bool freeze)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;
	pgtable_t pgtable;
	pmd_t _pmd;
	bool young, write, dirty, soft_dirty;
	unsigned long addr;
	int i;

	VM_BUG_ON(haddr & ~HPAGE_PMD_MASK);
	VM_BUG_ON_VMA(vma->vm_start > haddr, vma);
	VM_BUG_ON_VMA(vma->vm_end < haddr + HPAGE_PMD_SIZE, vma);
	VM_BUG_ON(!pmd_trans_huge(*pmd) && !pmd_devmap(*pmd));

	count_vm_event(THP_SPLIT_PMD);

	if (!vma_is_anonymous(vma)) {
		_pmd = pmdp_huge_clear_flush_notify(vma, haddr, pmd);
		if (vma_is_dax(vma))
			return;
		page = pmd_page(_pmd);
		if (!PageReferenced(page) && pmd_young(_pmd))
			SetPageReferenced(page);
		page_remove_rmap(page, true);
		put_page(page);
		add_mm_counter(mm, MM_FILEPAGES, -HPAGE_PMD_NR);
		return;
	} else if (is_huge_zero_pmd(*pmd)) {
		return __split_huge_zero_page_pmd(vma, haddr, pmd);
	}

	page = pmd_page(*pmd);
	VM_BUG_ON_PAGE(!page_count(page), page);
	page_ref_add(page, HPAGE_PMD_NR - 1);
	write = pmd_write(*pmd);
	young = pmd_young(*pmd);
	dirty = pmd_dirty(*pmd);
	soft_dirty = pmd_soft_dirty(*pmd);

	pmdp_huge_split_prepare(vma, haddr, pmd);
	pgtable = pgtable_trans_huge_withdraw(mm, pmd);
	pmd_populate(mm, &_pmd, pgtable);

	for (i = 0, addr = haddr; i < HPAGE_PMD_NR; i++, addr += PAGE_SIZE) {
		pte_t entry, *pte;
		/*
		 * Note that NUMA hinting access restrictions are not
		 * transferred to avoid any possibility of altering
		 * permissions across VMAs.
		 */
		if (freeze) {
			swp_entry_t swp_entry;
			swp_entry = make_migration_entry(page + i, write);
			entry = swp_entry_to_pte(swp_entry);
			if (soft_dirty)
				entry = pte_swp_mksoft_dirty(entry);
		} else {
			entry = mk_pte(page + i, READ_ONCE(vma->vm_page_prot));
			entry = maybe_mkwrite(entry, vma);
			if (!write)
				entry = pte_wrprotect(entry);
			if (!young)
				entry = pte_mkold(entry);
			if (soft_dirty)
				entry = pte_mksoft_dirty(entry);
		}
		if (dirty)
			SetPageDirty(page + i);
		pte = pte_offset_map(&_pmd, addr);
		BUG_ON(!pte_none(*pte));
		set_pte_at(mm, addr, pte, entry);
		atomic_inc(&page[i]._mapcount);
		pte_unmap(pte);
	}

	/*
	 * Set PG_double_map before dropping compound_mapcount to avoid
	 * false-negative page_mapped().
	 */
	if (compound_mapcount(page) > 1 && !TestSetPageDoubleMap(page)) {
		for (i = 0; i < HPAGE_PMD_NR; i++)
			atomic_inc(&page[i]._mapcount);
	}

	if (atomic_add_negative(-1, compound_mapcount_ptr(page))) {
		/* Last compound_mapcount is gone. */
		__dec_node_page_state(page, NR_ANON_THPS);
		if (TestClearPageDoubleMap(page)) {
			/* No need in mapcount reference anymore */
			for (i = 0; i < HPAGE_PMD_NR; i++)
				atomic_dec(&page[i]._mapcount);
		}
	}

	smp_wmb(); /* make pte visible before pmd */
	/*
	 * Up to this point the pmd is present and huge and userland has the
	 * whole access to the hugepage during the split (which happens in
	 * place). If we overwrite the pmd with the not-huge version pointing
	 * to the pte here (which of course we could if all CPUs were bug
	 * free), userland could trigger a small page size TLB miss on the
	 * small sized TLB while the hugepage TLB entry is still established in
	 * the huge TLB. Some CPU doesn't like that.
	 * See http://support.amd.com/us/Processor_TechDocs/41322.pdf, Erratum
	 * 383 on page 93. Intel should be safe but is also warns that it's
	 * only safe if the permission and cache attributes of the two entries
	 * loaded in the two TLB is identical (which should be the case here).
	 * But it is generally safer to never allow small and huge TLB entries
	 * for the same virtual address to be loaded simultaneously. So instead
	 * of doing "pmd_populate(); flush_pmd_tlb_range();" we first mark the
	 * current pmd notpresent (atomically because here the pmd_trans_huge
	 * and pmd_trans_splitting must remain set at all times on the pmd
	 * until the split is complete for this pmd), then we flush the SMP TLB
	 * and finally we write the non-huge version of the pmd entry with
	 * pmd_populate.
	 */
	pmdp_invalidate(vma, haddr, pmd);
	pmd_populate(mm, pmd, pgtable);

	if (freeze) {
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			page_remove_rmap(page + i, false);
			put_page(page + i);
		}
	}
}

void __split_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long address, bool freeze, struct page *page)
{
	spinlock_t *ptl;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long haddr = address & HPAGE_PMD_MASK;

	mmu_notifier_invalidate_range_start(mm, haddr, haddr + HPAGE_PMD_SIZE);
	ptl = pmd_lock(mm, pmd);

	/*
	 * If caller asks to setup a migration entries, we need a page to check
	 * pmd against. Otherwise we can end up replacing wrong page.
	 */
	VM_BUG_ON(freeze && !page);
	if (page && page != pmd_page(*pmd))
	        goto out;

	if (pmd_trans_huge(*pmd)) {
		page = pmd_page(*pmd);
		if (PageMlocked(page))
			clear_page_mlock(page);
	} else if (!pmd_devmap(*pmd))
		goto out;
	__split_huge_pmd_locked(vma, pmd, haddr, freeze);
out:
	spin_unlock(ptl);
	mmu_notifier_invalidate_range_end(mm, haddr, haddr + HPAGE_PMD_SIZE);
}

void split_huge_pmd_address(struct vm_area_struct *vma, unsigned long address,
		bool freeze, struct page *page)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(vma->vm_mm, address);
	if (!pgd_present(*pgd))
		return;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		return;

	pmd = pmd_offset(pud, address);

	__split_huge_pmd(vma, pmd, address, freeze, page);
}

void vma_adjust_trans_huge(struct vm_area_struct *vma,
			     unsigned long start,
			     unsigned long end,
			     long adjust_next)
{
	/*
	 * If the new start address isn't hpage aligned and it could
	 * previously contain an hugepage: check if we need to split
	 * an huge pmd.
	 */
	if (start & ~HPAGE_PMD_MASK &&
	    (start & HPAGE_PMD_MASK) >= vma->vm_start &&
	    (start & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= vma->vm_end)
		split_huge_pmd_address(vma, start, false, NULL);

	/*
	 * If the new end address isn't hpage aligned and it could
	 * previously contain an hugepage: check if we need to split
	 * an huge pmd.
	 */
	if (end & ~HPAGE_PMD_MASK &&
	    (end & HPAGE_PMD_MASK) >= vma->vm_start &&
	    (end & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= vma->vm_end)
		split_huge_pmd_address(vma, end, false, NULL);

	/*
	 * If we're also updating the vma->vm_next->vm_start, if the new
	 * vm_next->vm_start isn't page aligned and it could previously
	 * contain an hugepage: check if we need to split an huge pmd.
	 */
	if (adjust_next > 0) {
		struct vm_area_struct *next = vma->vm_next;
		unsigned long nstart = next->vm_start;
		nstart += adjust_next << PAGE_SHIFT;
		if (nstart & ~HPAGE_PMD_MASK &&
		    (nstart & HPAGE_PMD_MASK) >= next->vm_start &&
		    (nstart & HPAGE_PMD_MASK) + HPAGE_PMD_SIZE <= next->vm_end)
			split_huge_pmd_address(next, nstart, false, NULL);
	}
}

static void freeze_page(struct page *page)
{
	enum ttu_flags ttu_flags = TTU_IGNORE_MLOCK | TTU_IGNORE_ACCESS |
		TTU_RMAP_LOCKED;
	int i, ret;

	VM_BUG_ON_PAGE(!PageHead(page), page);

	if (PageAnon(page))
		ttu_flags |= TTU_MIGRATION;

	/* We only need TTU_SPLIT_HUGE_PMD once */
	ret = try_to_unmap(page, ttu_flags | TTU_SPLIT_HUGE_PMD);
	for (i = 1; !ret && i < HPAGE_PMD_NR; i++) {
		/* Cut short if the page is unmapped */
		if (page_count(page) == 1)
			return;

		ret = try_to_unmap(page + i, ttu_flags);
	}
	VM_BUG_ON_PAGE(ret, page + i - 1);
}

static void unfreeze_page(struct page *page)
{
	int i;

	for (i = 0; i < HPAGE_PMD_NR; i++)
		remove_migration_ptes(page + i, page + i, true);
}

static void __split_huge_page_tail(struct page *head, int tail,
		struct lruvec *lruvec, struct list_head *list)
{
	struct page *page_tail = head + tail;

	VM_BUG_ON_PAGE(atomic_read(&page_tail->_mapcount) != -1, page_tail);
	VM_BUG_ON_PAGE(page_ref_count(page_tail) != 0, page_tail);

	/*
	 * tail_page->_refcount is zero and not changing from under us. But
	 * get_page_unless_zero() may be running from under us on the
	 * tail_page. If we used atomic_set() below instead of atomic_inc() or
	 * atomic_add(), we would then run atomic_set() concurrently with
	 * get_page_unless_zero(), and atomic_set() is implemented in C not
	 * using locked ops. spin_unlock on x86 sometime uses locked ops
	 * because of PPro errata 66, 92, so unless somebody can guarantee
	 * atomic_set() here would be safe on all archs (and not only on x86),
	 * it's safer to use atomic_inc()/atomic_add().
	 */
	if (PageAnon(head)) {
		page_ref_inc(page_tail);
	} else {
		/* Additional pin to radix tree */
		page_ref_add(page_tail, 2);
	}

	page_tail->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	page_tail->flags |= (head->flags &
			((1L << PG_referenced) |
			 (1L << PG_swapbacked) |
			 (1L << PG_mlocked) |
			 (1L << PG_uptodate) |
			 (1L << PG_active) |
			 (1L << PG_locked) |
			 (1L << PG_unevictable) |
			 (1L << PG_dirty)));

	/*
	 * After clearing PageTail the gup refcount can be released.
	 * Page flags also must be visible before we make the page non-compound.
	 */
	smp_wmb();

	clear_compound_head(page_tail);

	if (page_is_young(head))
		set_page_young(page_tail);
	if (page_is_idle(head))
		set_page_idle(page_tail);

	/* ->mapping in first tail page is compound_mapcount */
	VM_BUG_ON_PAGE(tail > 2 && page_tail->mapping != TAIL_MAPPING,
			page_tail);
	page_tail->mapping = head->mapping;

	page_tail->index = head->index + tail;
	page_cpupid_xchg_last(page_tail, page_cpupid_last(head));
	lru_add_page_tail(head, page_tail, lruvec, list);
}

static void __split_huge_page(struct page *page, struct list_head *list,
		unsigned long flags)
{
	struct page *head = compound_head(page);
	struct zone *zone = page_zone(head);
	struct lruvec *lruvec;
	pgoff_t end = -1;
	int i;

	lruvec = mem_cgroup_page_lruvec(head, zone->zone_pgdat);

	/* complete memcg works before add pages to LRU */
	mem_cgroup_split_huge_fixup(head);

	if (!PageAnon(page))
		end = DIV_ROUND_UP(i_size_read(head->mapping->host), PAGE_SIZE);

	for (i = HPAGE_PMD_NR - 1; i >= 1; i--) {
		__split_huge_page_tail(head, i, lruvec, list);
		/* Some pages can be beyond i_size: drop them from page cache */
		if (head[i].index >= end) {
			__ClearPageDirty(head + i);
			__delete_from_page_cache(head + i, NULL);
			if (IS_ENABLED(CONFIG_SHMEM) && PageSwapBacked(head))
				shmem_uncharge(head->mapping->host, 1);
			put_page(head + i);
		}
	}

	ClearPageCompound(head);
	/* See comment in __split_huge_page_tail() */
	if (PageAnon(head)) {
		page_ref_inc(head);
	} else {
		/* Additional pin to radix tree */
		page_ref_add(head, 2);
		spin_unlock(&head->mapping->tree_lock);
	}

	spin_unlock_irqrestore(zone_lru_lock(page_zone(head)), flags);

	unfreeze_page(head);

	for (i = 0; i < HPAGE_PMD_NR; i++) {
		struct page *subpage = head + i;
		if (subpage == page)
			continue;
		unlock_page(subpage);

		/*
		 * Subpages may be freed if there wasn't any mapping
		 * like if add_to_swap() is running on a lru page that
		 * had its mapping zapped. And freeing these pages
		 * requires taking the lru_lock so we do the put_page
		 * of the tail pages after the split is complete.
		 */
		put_page(subpage);
	}
}

int total_mapcount(struct page *page)
{
	int i, compound, ret;

	VM_BUG_ON_PAGE(PageTail(page), page);

	if (likely(!PageCompound(page)))
		return atomic_read(&page->_mapcount) + 1;

	compound = compound_mapcount(page);
	if (PageHuge(page))
		return compound;
	ret = compound;
	for (i = 0; i < HPAGE_PMD_NR; i++)
		ret += atomic_read(&page[i]._mapcount) + 1;
	/* File pages has compound_mapcount included in _mapcount */
	if (!PageAnon(page))
		return ret - compound * HPAGE_PMD_NR;
	if (PageDoubleMap(page))
		ret -= HPAGE_PMD_NR;
	return ret;
}

/*
 * This calculates accurately how many mappings a transparent hugepage
 * has (unlike page_mapcount() which isn't fully accurate). This full
 * accuracy is primarily needed to know if copy-on-write faults can
 * reuse the page and change the mapping to read-write instead of
 * copying them. At the same time this returns the total_mapcount too.
 *
 * The function returns the highest mapcount any one of the subpages
 * has. If the return value is one, even if different processes are
 * mapping different subpages of the transparent hugepage, they can
 * all reuse it, because each process is reusing a different subpage.
 *
 * The total_mapcount is instead counting all virtual mappings of the
 * subpages. If the total_mapcount is equal to "one", it tells the
 * caller all mappings belong to the same "mm" and in turn the
 * anon_vma of the transparent hugepage can become the vma->anon_vma
 * local one as no other process may be mapping any of the subpages.
 *
 * It would be more accurate to replace page_mapcount() with
 * page_trans_huge_mapcount(), however we only use
 * page_trans_huge_mapcount() in the copy-on-write faults where we
 * need full accuracy to avoid breaking page pinning, because
 * page_trans_huge_mapcount() is slower than page_mapcount().
 */
int page_trans_huge_mapcount(struct page *page, int *total_mapcount)
{
	int i, ret, _total_mapcount, mapcount;

	/* hugetlbfs shouldn't call it */
	VM_BUG_ON_PAGE(PageHuge(page), page);

	if (likely(!PageTransCompound(page))) {
		mapcount = atomic_read(&page->_mapcount) + 1;
		if (total_mapcount)
			*total_mapcount = mapcount;
		return mapcount;
	}

	page = compound_head(page);

	_total_mapcount = ret = 0;
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		mapcount = atomic_read(&page[i]._mapcount) + 1;
		ret = max(ret, mapcount);
		_total_mapcount += mapcount;
	}
	if (PageDoubleMap(page)) {
		ret -= 1;
		_total_mapcount -= HPAGE_PMD_NR;
	}
	mapcount = compound_mapcount(page);
	ret += mapcount;
	_total_mapcount += mapcount;
	if (total_mapcount)
		*total_mapcount = _total_mapcount;
	return ret;
}

/*
 * This function splits huge page into normal pages. @page can point to any
 * subpage of huge page to split. Split doesn't change the position of @page.
 *
 * Only caller must hold pin on the @page, otherwise split fails with -EBUSY.
 * The huge page must be locked.
 *
 * If @list is null, tail pages will be added to LRU list, otherwise, to @list.
 *
 * Both head page and tail pages will inherit mapping, flags, and so on from
 * the hugepage.
 *
 * GUP pin and PG_locked transferred to @page. Rest subpages can be freed if
 * they are not mapped.
 *
 * Returns 0 if the hugepage is split successfully.
 * Returns -EBUSY if the page is pinned or if anon_vma disappeared from under
 * us.
 */
int split_huge_page_to_list(struct page *page, struct list_head *list)
{
	struct page *head = compound_head(page);
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(head));
	struct anon_vma *anon_vma = NULL;
	struct address_space *mapping = NULL;
	int count, mapcount, extra_pins, ret;
	bool mlocked;
	unsigned long flags;

	VM_BUG_ON_PAGE(is_huge_zero_page(page), page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);
	VM_BUG_ON_PAGE(!PageCompound(page), page);

	if (PageAnon(head)) {
		/*
		 * The caller does not necessarily hold an mmap_sem that would
		 * prevent the anon_vma disappearing so we first we take a
		 * reference to it and then lock the anon_vma for write. This
		 * is similar to page_lock_anon_vma_read except the write lock
		 * is taken to serialise against parallel split or collapse
		 * operations.
		 */
		anon_vma = page_get_anon_vma(head);
		if (!anon_vma) {
			ret = -EBUSY;
			goto out;
		}
		extra_pins = 0;
		mapping = NULL;
		anon_vma_lock_write(anon_vma);
	} else {
		mapping = head->mapping;

		/* Truncated ? */
		if (!mapping) {
			ret = -EBUSY;
			goto out;
		}

		/* Addidional pins from radix tree */
		extra_pins = HPAGE_PMD_NR;
		anon_vma = NULL;
		i_mmap_lock_read(mapping);
	}

	/*
	 * Racy check if we can split the page, before freeze_page() will
	 * split PMDs
	 */
	if (total_mapcount(head) != page_count(head) - extra_pins - 1) {
		ret = -EBUSY;
		goto out_unlock;
	}

	mlocked = PageMlocked(page);
	freeze_page(head);
	VM_BUG_ON_PAGE(compound_mapcount(head), head);

	/* Make sure the page is not on per-CPU pagevec as it takes pin */
	if (mlocked)
		lru_add_drain();

	/* prevent PageLRU to go away from under us, and freeze lru stats */
	spin_lock_irqsave(zone_lru_lock(page_zone(head)), flags);

	if (mapping) {
		void **pslot;

		spin_lock(&mapping->tree_lock);
		pslot = radix_tree_lookup_slot(&mapping->page_tree,
				page_index(head));
		/*
		 * Check if the head page is present in radix tree.
		 * We assume all tail are present too, if head is there.
		 */
		if (radix_tree_deref_slot_protected(pslot,
					&mapping->tree_lock) != head)
			goto fail;
	}

	/* Prevent deferred_split_scan() touching ->_refcount */
	spin_lock(&pgdata->split_queue_lock);
	count = page_count(head);
	mapcount = total_mapcount(head);
	if (!mapcount && page_ref_freeze(head, 1 + extra_pins)) {
		if (!list_empty(page_deferred_list(head))) {
			pgdata->split_queue_len--;
			list_del(page_deferred_list(head));
		}
		if (mapping)
			__dec_node_page_state(page, NR_SHMEM_THPS);
		spin_unlock(&pgdata->split_queue_lock);
		__split_huge_page(page, list, flags);
		ret = 0;
	} else {
		if (IS_ENABLED(CONFIG_DEBUG_VM) && mapcount) {
			pr_alert("total_mapcount: %u, page_count(): %u\n",
					mapcount, count);
			if (PageTail(page))
				dump_page(head, NULL);
			dump_page(page, "total_mapcount(head) > 0");
			BUG();
		}
		spin_unlock(&pgdata->split_queue_lock);
fail:		if (mapping)
			spin_unlock(&mapping->tree_lock);
		spin_unlock_irqrestore(zone_lru_lock(page_zone(head)), flags);
		unfreeze_page(head);
		ret = -EBUSY;
	}

out_unlock:
	if (anon_vma) {
		anon_vma_unlock_write(anon_vma);
		put_anon_vma(anon_vma);
	}
	if (mapping)
		i_mmap_unlock_read(mapping);
out:
	count_vm_event(!ret ? THP_SPLIT_PAGE : THP_SPLIT_PAGE_FAILED);
	return ret;
}

void free_transhuge_page(struct page *page)
{
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(page));
	unsigned long flags;

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	if (!list_empty(page_deferred_list(page))) {
		pgdata->split_queue_len--;
		list_del(page_deferred_list(page));
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
	free_compound_page(page);
}

void deferred_split_huge_page(struct page *page)
{
	struct pglist_data *pgdata = NODE_DATA(page_to_nid(page));
	unsigned long flags;

	VM_BUG_ON_PAGE(!PageTransHuge(page), page);

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	if (list_empty(page_deferred_list(page))) {
		count_vm_event(THP_DEFERRED_SPLIT_PAGE);
		list_add_tail(page_deferred_list(page), &pgdata->split_queue);
		pgdata->split_queue_len++;
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);
}

static unsigned long deferred_split_count(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	return ACCESS_ONCE(pgdata->split_queue_len);
}

static unsigned long deferred_split_scan(struct shrinker *shrink,
		struct shrink_control *sc)
{
	struct pglist_data *pgdata = NODE_DATA(sc->nid);
	unsigned long flags;
	LIST_HEAD(list), *pos, *next;
	struct page *page;
	int split = 0;

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	/* Take pin on all head pages to avoid freeing them under us */
	list_for_each_safe(pos, next, &pgdata->split_queue) {
		page = list_entry((void *)pos, struct page, mapping);
		page = compound_head(page);
		if (get_page_unless_zero(page)) {
			list_move(page_deferred_list(page), &list);
		} else {
			/* We lost race with put_compound_page() */
			list_del_init(page_deferred_list(page));
			pgdata->split_queue_len--;
		}
		if (!--sc->nr_to_scan)
			break;
	}
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);

	list_for_each_safe(pos, next, &list) {
		page = list_entry((void *)pos, struct page, mapping);
		lock_page(page);
		/* split_huge_page() removes page from list on success */
		if (!split_huge_page(page))
			split++;
		unlock_page(page);
		put_page(page);
	}

	spin_lock_irqsave(&pgdata->split_queue_lock, flags);
	list_splice_tail(&list, &pgdata->split_queue);
	spin_unlock_irqrestore(&pgdata->split_queue_lock, flags);

	/*
	 * Stop shrinker if we didn't split any page, but the queue is empty.
	 * This can happen if pages were freed under us.
	 */
	if (!split && list_empty(&pgdata->split_queue))
		return SHRINK_STOP;
	return split;
}

static struct shrinker deferred_split_shrinker = {
	.count_objects = deferred_split_count,
	.scan_objects = deferred_split_scan,
	.seeks = DEFAULT_SEEKS,
	.flags = SHRINKER_NUMA_AWARE,
};

#ifdef CONFIG_DEBUG_FS
static int split_huge_pages_set(void *data, u64 val)
{
	struct zone *zone;
	struct page *page;
	unsigned long pfn, max_zone_pfn;
	unsigned long total = 0, split = 0;

	if (val != 1)
		return -EINVAL;

	for_each_populated_zone(zone) {
		max_zone_pfn = zone_end_pfn(zone);
		for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++) {
			if (!pfn_valid(pfn))
				continue;

			page = pfn_to_page(pfn);
			if (!get_page_unless_zero(page))
				continue;

			if (zone != page_zone(page))
				goto next;

			if (!PageHead(page) || PageHuge(page) || !PageLRU(page))
				goto next;

			total++;
			lock_page(page);
			if (!split_huge_page(page))
				split++;
			unlock_page(page);
next:
			put_page(page);
		}
	}

	pr_info("%lu of %lu THP split\n", split, total);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(split_huge_pages_fops, NULL, split_huge_pages_set,
		"%llu\n");

static int __init split_huge_pages_debugfs(void)
{
	void *ret;

	ret = debugfs_create_file("split_huge_pages", 0200, NULL, NULL,
			&split_huge_pages_fops);
	if (!ret)
		pr_warn("Failed to create split_huge_pages in debugfs");
	return 0;
}
late_initcall(split_huge_pages_debugfs);
#endif
