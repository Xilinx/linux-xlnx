/*
 *  mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 *  (C) Copyright 2002 Christoph Hellwig
 *
 *  Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 *  (C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/security.h>
#include <linux/mempolicy.h>
#include <linux/personality.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/perf_event.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifndef pgprot_modify
static inline pgprot_t pgprot_modify(pgprot_t oldprot, pgprot_t newprot)
{
	return newprot;
}
#endif

static unsigned long change_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, unsigned long end, pgprot_t newprot,
		int dirty_accountable, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte, oldpte;
	spinlock_t *ptl;
	unsigned long pages = 0;

	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	arch_enter_lazy_mmu_mode();
	do {
		oldpte = *pte;
		if (pte_present(oldpte)) {
			pte_t ptent;
			bool updated = false;

			if (!prot_numa) {
				ptent = ptep_modify_prot_start(mm, addr, pte);
				if (pte_numa(ptent))
					ptent = pte_mknonnuma(ptent);
				ptent = pte_modify(ptent, newprot);
				updated = true;
			} else {
				struct page *page;

				ptent = *pte;
				page = vm_normal_page(vma, addr, oldpte);
				if (page) {
					if (!pte_numa(oldpte)) {
						ptent = pte_mknuma(ptent);
						set_pte_at(mm, addr, pte, ptent);
						updated = true;
					}
				}
			}

			/*
			 * Avoid taking write faults for pages we know to be
			 * dirty.
			 */
			if (dirty_accountable && pte_dirty(ptent)) {
				ptent = pte_mkwrite(ptent);
				updated = true;
			}

			if (updated)
				pages++;

			/* Only !prot_numa always clears the pte */
			if (!prot_numa)
				ptep_modify_prot_commit(mm, addr, pte, ptent);
		} else if (IS_ENABLED(CONFIG_MIGRATION) && !pte_file(oldpte)) {
			swp_entry_t entry = pte_to_swp_entry(oldpte);

			if (is_write_migration_entry(entry)) {
				pte_t newpte;
				/*
				 * A protection check is difficult so
				 * just be safe and disable write
				 */
				make_migration_entry_read(&entry);
				newpte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(oldpte))
					newpte = pte_swp_mksoft_dirty(newpte);
				set_pte_at(mm, addr, pte, newpte);

				pages++;
			}
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);

	return pages;
}

static inline unsigned long change_pmd_range(struct vm_area_struct *vma,
		pud_t *pud, unsigned long addr, unsigned long end,
		pgprot_t newprot, int dirty_accountable, int prot_numa)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long pages = 0;
	unsigned long nr_huge_updates = 0;

	pmd = pmd_offset(pud, addr);
	do {
		unsigned long this_pages;

		next = pmd_addr_end(addr, end);
		if (pmd_trans_huge(*pmd)) {
			if (next - addr != HPAGE_PMD_SIZE)
				split_huge_page_pmd(vma, addr, pmd);
			else {
				int nr_ptes = change_huge_pmd(vma, pmd, addr,
						newprot, prot_numa);

				if (nr_ptes) {
					if (nr_ptes == HPAGE_PMD_NR) {
						pages += HPAGE_PMD_NR;
						nr_huge_updates++;
					}
					continue;
				}
			}
			/* fall through */
		}
		if (pmd_none_or_clear_bad(pmd))
			continue;
		this_pages = change_pte_range(vma, pmd, addr, next, newprot,
				 dirty_accountable, prot_numa);
		pages += this_pages;
	} while (pmd++, addr = next, addr != end);

	if (nr_huge_updates)
		count_vm_numa_events(NUMA_HUGE_PTE_UPDATES, nr_huge_updates);
	return pages;
}

static inline unsigned long change_pud_range(struct vm_area_struct *vma,
		pgd_t *pgd, unsigned long addr, unsigned long end,
		pgprot_t newprot, int dirty_accountable, int prot_numa)
{
	pud_t *pud;
	unsigned long next;
	unsigned long pages = 0;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		pages += change_pmd_range(vma, pud, addr, next, newprot,
				 dirty_accountable, prot_numa);
	} while (pud++, addr = next, addr != end);

	return pages;
}

static unsigned long change_protection_range(struct vm_area_struct *vma,
		unsigned long addr, unsigned long end, pgprot_t newprot,
		int dirty_accountable, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;
	unsigned long start = addr;
	unsigned long pages = 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	set_tlb_flush_pending(mm);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		pages += change_pud_range(vma, pgd, addr, next, newprot,
				 dirty_accountable, prot_numa);
	} while (pgd++, addr = next, addr != end);

	/* Only flush the TLB if we actually modified any entries: */
	if (pages)
		flush_tlb_range(vma, start, end);
	clear_tlb_flush_pending(mm);

	return pages;
}

unsigned long change_protection(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end, pgprot_t newprot,
		       int dirty_accountable, int prot_numa)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long pages;

	mmu_notifier_invalidate_range_start(mm, start, end);
	if (is_vm_hugetlb_page(vma))
		pages = hugetlb_change_protection(vma, start, end, newprot);
	else
		pages = change_protection_range(vma, start, end, newprot, dirty_accountable, prot_numa);
	mmu_notifier_invalidate_range_end(mm, start, end);

	return pages;
}

int
mprotect_fixup(struct vm_area_struct *vma, struct vm_area_struct **pprev,
	unsigned long start, unsigned long end, unsigned long newflags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long oldflags = vma->vm_flags;
	long nrpages = (end - start) >> PAGE_SHIFT;
	unsigned long charged = 0;
	pgoff_t pgoff;
	int error;
	int dirty_accountable = 0;

	if (newflags == oldflags) {
		*pprev = vma;
		return 0;
	}

	/*
	 * If we make a private mapping writable we increase our commit;
	 * but (without finer accounting) cannot reduce our commit if we
	 * make it unwritable again. hugetlb mapping were accounted for
	 * even if read-only so there is no need to account for them here
	 */
	if (newflags & VM_WRITE) {
		if (!(oldflags & (VM_ACCOUNT|VM_WRITE|VM_HUGETLB|
						VM_SHARED|VM_NORESERVE))) {
			charged = nrpages;
			if (security_vm_enough_memory_mm(mm, charged))
				return -ENOMEM;
			newflags |= VM_ACCOUNT;
		}
	}

	/*
	 * First try to merge with previous and/or next vma.
	 */
	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*pprev = vma_merge(mm, *pprev, start, end, newflags,
			vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma));
	if (*pprev) {
		vma = *pprev;
		goto success;
	}

	*pprev = vma;

	if (start != vma->vm_start) {
		error = split_vma(mm, vma, start, 1);
		if (error)
			goto fail;
	}

	if (end != vma->vm_end) {
		error = split_vma(mm, vma, end, 0);
		if (error)
			goto fail;
	}

success:
	/*
	 * vm_flags and vm_page_prot are protected by the mmap_sem
	 * held in write mode.
	 */
	vma->vm_flags = newflags;
	vma->vm_page_prot = pgprot_modify(vma->vm_page_prot,
					  vm_get_page_prot(newflags));

	if (vma_wants_writenotify(vma)) {
		vma->vm_page_prot = vm_get_page_prot(newflags & ~VM_SHARED);
		dirty_accountable = 1;
	}

	change_protection(vma, start, end, vma->vm_page_prot,
			  dirty_accountable, 0);

	vm_stat_account(mm, oldflags, vma->vm_file, -nrpages);
	vm_stat_account(mm, newflags, vma->vm_file, nrpages);
	perf_event_mmap(vma);
	return 0;

fail:
	vm_unacct_memory(charged);
	return error;
}

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
		unsigned long, prot)
{
	unsigned long vm_flags, nstart, end, tmp, reqprot;
	struct vm_area_struct *vma, *prev;
	int error = -EINVAL;
	const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
	prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
	if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
		return -EINVAL;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	if (!len)
		return 0;
	len = PAGE_ALIGN(len);
	end = start + len;
	if (end <= start)
		return -ENOMEM;
	if (!arch_validate_prot(prot))
		return -EINVAL;

	reqprot = prot;
	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC:
	 */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	vm_flags = calc_vm_prot_bits(prot);

	down_write(&current->mm->mmap_sem);

	vma = find_vma(current->mm, start);
	error = -ENOMEM;
	if (!vma)
		goto out;
	prev = vma->vm_prev;
	if (unlikely(grows & PROT_GROWSDOWN)) {
		if (vma->vm_start >= end)
			goto out;
		start = vma->vm_start;
		error = -EINVAL;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
	} else {
		if (vma->vm_start > start)
			goto out;
		if (unlikely(grows & PROT_GROWSUP)) {
			end = vma->vm_end;
			error = -EINVAL;
			if (!(vma->vm_flags & VM_GROWSUP))
				goto out;
		}
	}
	if (start > vma->vm_start)
		prev = vma;

	for (nstart = start ; ; ) {
		unsigned long newflags;

		/* Here we know that vma->vm_start <= nstart < vma->vm_end. */

		newflags = vm_flags;
		newflags |= (vma->vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC));

		/* newflags >> 4 shift VM_MAY% in place of VM_% */
		if ((newflags & ~(newflags >> 4)) & (VM_READ | VM_WRITE | VM_EXEC)) {
			error = -EACCES;
			goto out;
		}

		error = security_file_mprotect(vma, reqprot, prot);
		if (error)
			goto out;

		tmp = vma->vm_end;
		if (tmp > end)
			tmp = end;
		error = mprotect_fixup(vma, &prev, nstart, tmp, newflags);
		if (error)
			goto out;
		nstart = tmp;

		if (nstart < prev->vm_end)
			nstart = prev->vm_end;
		if (nstart >= end)
			goto out;

		vma = prev->vm_next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			goto out;
		}
	}
out:
	up_write(&current->mm->mmap_sem);
	return error;
}
