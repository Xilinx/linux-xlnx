#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_H_
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_H_

/*
 * Common bits between hash and Radix page table
 */
#define _PAGE_BIT_SWAP_TYPE	0

#define _PAGE_RO		0

#define _PAGE_EXEC		0x00001 /* execute permission */
#define _PAGE_WRITE		0x00002 /* write access allowed */
#define _PAGE_READ		0x00004	/* read access allowed */
#define _PAGE_RW		(_PAGE_READ | _PAGE_WRITE)
#define _PAGE_RWX		(_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)
#define _PAGE_PRIVILEGED	0x00008 /* kernel access only */
#define _PAGE_SAO		0x00010 /* Strong access order */
#define _PAGE_NON_IDEMPOTENT	0x00020 /* non idempotent memory */
#define _PAGE_TOLERANT		0x00030 /* tolerant memory, cache inhibited */
#define _PAGE_DIRTY		0x00080 /* C: page changed */
#define _PAGE_ACCESSED		0x00100 /* R: page referenced */
/*
 * Software bits
 */
#define _RPAGE_SW0		0x2000000000000000UL
#define _RPAGE_SW1		0x00800
#define _RPAGE_SW2		0x00400
#define _RPAGE_SW3		0x00200
#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SOFT_DIRTY	_RPAGE_SW3 /* software: software dirty tracking */
#else
#define _PAGE_SOFT_DIRTY	0x00000
#endif
#define _PAGE_SPECIAL		_RPAGE_SW2 /* software: special page */


#define _PAGE_PTE		(1ul << 62)	/* distinguishes PTEs from pointers */
#define _PAGE_PRESENT		(1ul << 63)	/* pte contains a translation */
/*
 * Drivers request for cache inhibited pte mapping using _PAGE_NO_CACHE
 * Instead of fixing all of them, add an alternate define which
 * maps CI pte mapping.
 */
#define _PAGE_NO_CACHE		_PAGE_TOLERANT
/*
 * We support 57 bit real address in pte. Clear everything above 57, and
 * every thing below PAGE_SHIFT;
 */
#define PTE_RPN_MASK	(((1UL << 57) - 1) & (PAGE_MASK))
/*
 * set of bits not changed in pmd_modify. Even though we have hash specific bits
 * in here, on radix we expect them to be zero.
 */
#define _HPAGE_CHG_MASK (PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | H_PAGE_THP_HUGE | _PAGE_PTE | \
			 _PAGE_SOFT_DIRTY)
/*
 * user access blocked by key
 */
#define _PAGE_KERNEL_RW		(_PAGE_PRIVILEGED | _PAGE_RW | _PAGE_DIRTY)
#define _PAGE_KERNEL_RO		 (_PAGE_PRIVILEGED | _PAGE_READ)
#define _PAGE_KERNEL_RWX	(_PAGE_PRIVILEGED | _PAGE_DIRTY |	\
				 _PAGE_RW | _PAGE_EXEC)
/*
 * No page size encoding in the linux PTE
 */
#define _PAGE_PSIZE		0
/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | _PAGE_SPECIAL | _PAGE_PTE |	\
			 _PAGE_SOFT_DIRTY)
/*
 * Mask of bits returned by pte_pgprot()
 */
#define PAGE_PROT_BITS  (_PAGE_SAO | _PAGE_NON_IDEMPOTENT | _PAGE_TOLERANT | \
			 H_PAGE_4K_PFN | _PAGE_PRIVILEGED | _PAGE_ACCESSED | \
			 _PAGE_READ | _PAGE_WRITE |  _PAGE_DIRTY | _PAGE_EXEC | \
			 _PAGE_SOFT_DIRTY)
/*
 * We define 2 sets of base prot bits, one for basic pages (ie,
 * cacheable kernel and user pages) and one for non cacheable
 * pages. We always set _PAGE_COHERENT when SMP is enabled or
 * the processor might need it for DMA coherency.
 */
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_PSIZE)
#define _PAGE_BASE	(_PAGE_BASE_NC)

/* Permission masks used to generate the __P and __S table,
 *
 * Note:__pgprot is defined in arch/powerpc/include/asm/page.h
 *
 * Write permissions imply read permissions for now (we could make write-only
 * pages on BookE but we don't bother for now). Execute permission control is
 * possible on platforms that define _PAGE_EXEC
 *
 * Note due to the way vm flags are laid out, the bits are XWR
 */
#define PAGE_NONE	__pgprot(_PAGE_BASE | _PAGE_PRIVILEGED)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_X
#define __P101	PAGE_READONLY_X
#define __P110	PAGE_COPY_X
#define __P111	PAGE_COPY_X

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_X
#define __S101	PAGE_READONLY_X
#define __S110	PAGE_SHARED_X
#define __S111	PAGE_SHARED_X

/* Permission masks used for kernel mappings */
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RW)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_TOLERANT)
#define PAGE_KERNEL_NCG	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_NON_IDEMPOTENT)
#define PAGE_KERNEL_X	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RWX)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX	__pgprot(_PAGE_BASE | _PAGE_KERNEL_ROX)

/*
 * Protection used for kernel text. We want the debuggers to be able to
 * set breakpoints anywhere, so don't write protect the kernel text
 * on platforms where such control is possible.
 */
#if defined(CONFIG_KGDB) || defined(CONFIG_XMON) || defined(CONFIG_BDI_SWITCH) || \
	defined(CONFIG_KPROBES) || defined(CONFIG_DYNAMIC_FTRACE)
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_X
#else
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_ROX
#endif

/* Make modules code happy. We don't set RO yet */
#define PAGE_KERNEL_EXEC	PAGE_KERNEL_X
#define PAGE_AGP		(PAGE_KERNEL_NC)

#ifndef __ASSEMBLY__
/*
 * page table defines
 */
extern unsigned long __pte_index_size;
extern unsigned long __pmd_index_size;
extern unsigned long __pud_index_size;
extern unsigned long __pgd_index_size;
extern unsigned long __pmd_cache_index;
#define PTE_INDEX_SIZE  __pte_index_size
#define PMD_INDEX_SIZE  __pmd_index_size
#define PUD_INDEX_SIZE  __pud_index_size
#define PGD_INDEX_SIZE  __pgd_index_size
#define PMD_CACHE_INDEX __pmd_cache_index
/*
 * Because of use of pte fragments and THP, size of page table
 * are not always derived out of index size above.
 */
extern unsigned long __pte_table_size;
extern unsigned long __pmd_table_size;
extern unsigned long __pud_table_size;
extern unsigned long __pgd_table_size;
#define PTE_TABLE_SIZE	__pte_table_size
#define PMD_TABLE_SIZE	__pmd_table_size
#define PUD_TABLE_SIZE	__pud_table_size
#define PGD_TABLE_SIZE	__pgd_table_size

extern unsigned long __pmd_val_bits;
extern unsigned long __pud_val_bits;
extern unsigned long __pgd_val_bits;
#define PMD_VAL_BITS	__pmd_val_bits
#define PUD_VAL_BITS	__pud_val_bits
#define PGD_VAL_BITS	__pgd_val_bits

extern unsigned long __pte_frag_nr;
#define PTE_FRAG_NR __pte_frag_nr
extern unsigned long __pte_frag_size_shift;
#define PTE_FRAG_SIZE_SHIFT __pte_frag_size_shift
#define PTE_FRAG_SIZE (1UL << PTE_FRAG_SIZE_SHIFT)
/*
 * Pgtable size used by swapper, init in asm code
 */
#define MAX_PGD_TABLE_SIZE (sizeof(pgd_t) << RADIX_PGD_INDEX_SIZE)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PUD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PUD to get to the PMD page */
#define PUD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PGD to get to the PUD page */
#define PGD_MASKED_BITS		0xc0000000000000ffUL

extern unsigned long __vmalloc_start;
extern unsigned long __vmalloc_end;
#define VMALLOC_START	__vmalloc_start
#define VMALLOC_END	__vmalloc_end

extern unsigned long __kernel_virt_start;
extern unsigned long __kernel_virt_size;
#define KERN_VIRT_START __kernel_virt_start
#define KERN_VIRT_SIZE  __kernel_virt_size
extern struct page *vmemmap;
extern unsigned long ioremap_bot;
extern unsigned long pci_io_base;
#endif /* __ASSEMBLY__ */

#include <asm/book3s/64/hash.h>
#include <asm/book3s/64/radix.h>

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/book3s/64/pgtable-64k.h>
#else
#include <asm/book3s/64/pgtable-4k.h>
#endif

#include <asm/barrier.h>
/*
 * The second half of the kernel virtual space is used for IO mappings,
 * it's itself carved into the PIO region (ISA and PHB IO space) and
 * the ioremap space
 *
 *  ISA_IO_BASE = KERN_IO_START, 64K reserved area
 *  PHB_IO_BASE = ISA_IO_BASE + 64K to ISA_IO_BASE + 2G, PHB IO spaces
 * IOREMAP_BASE = ISA_IO_BASE + 2G to VMALLOC_START + PGTABLE_RANGE
 */
#define KERN_IO_START	(KERN_VIRT_START + (KERN_VIRT_SIZE >> 1))
#define FULL_IO_SIZE	0x80000000ul
#define  ISA_IO_BASE	(KERN_IO_START)
#define  ISA_IO_END	(KERN_IO_START + 0x10000ul)
#define  PHB_IO_BASE	(ISA_IO_END)
#define  PHB_IO_END	(KERN_IO_START + FULL_IO_SIZE)
#define IOREMAP_BASE	(PHB_IO_END)
#define IOREMAP_END	(KERN_VIRT_START + KERN_VIRT_SIZE)

/* Advertise special mapping type for AGP */
#define HAVE_PAGE_AGP

/* Advertise support for _PAGE_SPECIAL */
#define __HAVE_ARCH_PTE_SPECIAL

#ifndef __ASSEMBLY__

/*
 * This is the default implementation of various PTE accessors, it's
 * used in all cases except Book3S with 64K pages where we have a
 * concept of sub-pages
 */
#ifndef __real_pte

#define __real_pte(e,p)		((real_pte_t){(e)})
#define __rpte_to_pte(r)	((r).pte)
#define __rpte_to_hidx(r,index)	(pte_val(__rpte_to_pte(r)) >> H_PAGE_F_GIX_SHIFT)

#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)       \
	do {							         \
		index = 0;					         \
		shift = mmu_psize_defs[psize].shift;		         \

#define pte_iterate_hashed_end() } while(0)

/*
 * We expect this to be called only for user addresses or kernel virtual
 * addresses other than the linear mapping.
 */
#define pte_pagesize_index(mm, addr, pte)	MMU_PAGE_4K

#endif /* __real_pte */

static inline unsigned long pte_update(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       unsigned long set, int huge)
{
	if (radix_enabled())
		return radix__pte_update(mm, addr, ptep, clr, set, huge);
	return hash__pte_update(mm, addr, ptep, clr, set, huge);
}
/*
 * For hash even if we have _PAGE_ACCESSED = 0, we do a pte_update.
 * We currently remove entries from the hashtable regardless of whether
 * the entry was young or dirty.
 *
 * We should be more intelligent about this but for the moment we override
 * these functions and force a tlb flush unconditionally
 * For radix: H_PAGE_HASHPTE should be zero. Hence we can use the same
 * function for both hash and radix.
 */
static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	if ((pte_raw(*ptep) & cpu_to_be64(_PAGE_ACCESSED | H_PAGE_HASHPTE)) == 0)
		return 0;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0, 0);
	return (old & _PAGE_ACCESSED) != 0;
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __addr, __ptep)	\
({								\
	int __r;						\
	__r = __ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep); \
	__r;							\
})

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	if ((pte_raw(*ptep) & cpu_to_be64(_PAGE_WRITE)) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_WRITE, 0, 0);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	if ((pte_raw(*ptep) & cpu_to_be64(_PAGE_WRITE)) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_WRITE, 0, 1);
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 0, 0);
	return __pte(old);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t * ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 0);
}

static inline int pte_write(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_WRITE));
}

static inline int pte_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_DIRTY));
}

static inline int pte_young(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_ACCESSED));
}

static inline int pte_special(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SPECIAL));
}

static inline pgprot_t pte_pgprot(pte_t pte)	{ return __pgprot(pte_val(pte) & PAGE_PROT_BITS); }

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline bool pte_soft_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SOFT_DIRTY));
}

static inline pte_t pte_mksoft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_clear_soft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SOFT_DIRTY);
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
/*
 * These work without NUMA balancing but the kernel does not care. See the
 * comment in include/asm-generic/pgtable.h . On powerpc, this will only
 * work for user pages and always return true for kernel pages.
 */
static inline int pte_protnone(pte_t pte)
{
	return (pte_raw(pte) & cpu_to_be64(_PAGE_PRESENT | _PAGE_PRIVILEGED)) ==
		cpu_to_be64(_PAGE_PRESENT | _PAGE_PRIVILEGED);
}
#endif /* CONFIG_NUMA_BALANCING */

static inline int pte_present(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_PRESENT));
}
/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * Even if PTEs can be unsigned long long, a PFN is always an unsigned
 * long for now.
 */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot)
{
	return __pte((((pte_basic_t)(pfn) << PAGE_SHIFT) & PTE_RPN_MASK) |
		     pgprot_val(pgprot));
}

static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) & PTE_RPN_MASK) >> PAGE_SHIFT;
}

/* Generic modifiers for PTE bits */
static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_WRITE);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_DIRTY);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	/*
	 * write implies read, hence set both
	 */
	return __pte(pte_val(pte) | _PAGE_RW);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return pte;
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	/* FIXME!! check whether this need to be a conditional */
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

static inline bool pte_user(pte_t pte)
{
	return !(pte_raw(pte) & cpu_to_be64(_PAGE_PRIVILEGED));
}

/* Encode and de-code a swap entry */
#define MAX_SWAPFILES_CHECK() do { \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS); \
	/*							\
	 * Don't have overlapping bits with _PAGE_HPTEFLAGS	\
	 * We filter HPTEFLAGS on set_pte.			\
	 */							\
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & (0x1f << _PAGE_BIT_SWAP_TYPE)); \
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & _PAGE_SWP_SOFT_DIRTY);	\
	} while (0)
/*
 * on pte we don't need handle RADIX_TREE_EXCEPTIONAL_SHIFT;
 */
#define SWP_TYPE_BITS 5
#define __swp_type(x)		(((x).val >> _PAGE_BIT_SWAP_TYPE) \
				& ((1UL << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)		(((x).val & PTE_RPN_MASK) >> PAGE_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
				((type) << _PAGE_BIT_SWAP_TYPE) \
				| (((offset) << PAGE_SHIFT) & PTE_RPN_MASK)})
/*
 * swp_entry_t must be independent of pte bits. We build a swp_entry_t from
 * swap type and offset we get from swap and convert that to pte to find a
 * matching pte in linux page table.
 * Clear bits not found in swap entries here.
 */
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val((pte)) & ~_PAGE_PTE })
#define __swp_entry_to_pte(x)	__pte((x).val | _PAGE_PTE)

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SWP_SOFT_DIRTY   (1UL << (SWP_TYPE_BITS + _PAGE_BIT_SWAP_TYPE))
#else
#define _PAGE_SWP_SOFT_DIRTY	0UL
#endif /* CONFIG_MEM_SOFT_DIRTY */

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline pte_t pte_swp_mksoft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SWP_SOFT_DIRTY);
}

static inline bool pte_swp_soft_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SWP_SOFT_DIRTY));
}

static inline pte_t pte_swp_clear_soft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SWP_SOFT_DIRTY);
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

static inline bool check_pte_access(unsigned long access, unsigned long ptev)
{
	/*
	 * This check for _PAGE_RWX and _PAGE_PRESENT bits
	 */
	if (access & ~ptev)
		return false;
	/*
	 * This check for access to privilege space
	 */
	if ((access & _PAGE_PRIVILEGED) != (ptev & _PAGE_PRIVILEGED))
		return false;

	return true;
}
/*
 * Generic functions with hash/radix callbacks
 */

static inline void __ptep_set_access_flags(struct mm_struct *mm,
					   pte_t *ptep, pte_t entry)
{
	if (radix_enabled())
		return radix__ptep_set_access_flags(mm, ptep, entry);
	return hash__ptep_set_access_flags(ptep, entry);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	if (radix_enabled())
		return radix__pte_same(pte_a, pte_b);
	return hash__pte_same(pte_a, pte_b);
}

static inline int pte_none(pte_t pte)
{
	if (radix_enabled())
		return radix__pte_none(pte);
	return hash__pte_none(pte);
}

static inline void __set_pte_at(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, int percpu)
{
	if (radix_enabled())
		return radix__set_pte_at(mm, addr, ptep, pte, percpu);
	return hash__set_pte_at(mm, addr, ptep, pte, percpu);
}

#define _PAGE_CACHE_CTL	(_PAGE_NON_IDEMPOTENT | _PAGE_TOLERANT)

#define pgprot_noncached pgprot_noncached
static inline pgprot_t pgprot_noncached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_NON_IDEMPOTENT);
}

#define pgprot_noncached_wc pgprot_noncached_wc
static inline pgprot_t pgprot_noncached_wc(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_TOLERANT);
}

#define pgprot_cached pgprot_cached
static inline pgprot_t pgprot_cached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL));
}

#define pgprot_writecombine pgprot_writecombine
static inline pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached_wc(prot);
}
/*
 * check a pte mapping have cache inhibited property
 */
static inline bool pte_ci(pte_t pte)
{
	unsigned long pte_v = pte_val(pte);

	if (((pte_v & _PAGE_CACHE_CTL) == _PAGE_TOLERANT) ||
	    ((pte_v & _PAGE_CACHE_CTL) == _PAGE_NON_IDEMPOTENT))
		return true;
	return false;
}

static inline void pmd_set(pmd_t *pmdp, unsigned long val)
{
	*pmdp = __pmd(val);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	*pmdp = __pmd(0);
}

static inline int pmd_none(pmd_t pmd)
{
	return !pmd_raw(pmd);
}

static inline int pmd_present(pmd_t pmd)
{

	return !pmd_none(pmd);
}

static inline int pmd_bad(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_bad(pmd);
	return hash__pmd_bad(pmd);
}

static inline void pud_set(pud_t *pudp, unsigned long val)
{
	*pudp = __pud(val);
}

static inline void pud_clear(pud_t *pudp)
{
	*pudp = __pud(0);
}

static inline int pud_none(pud_t pud)
{
	return !pud_raw(pud);
}

static inline int pud_present(pud_t pud)
{
	return !pud_none(pud);
}

extern struct page *pud_page(pud_t pud);
extern struct page *pmd_page(pmd_t pmd);
static inline pte_t pud_pte(pud_t pud)
{
	return __pte_raw(pud_raw(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud_raw(pte_raw(pte));
}
#define pud_write(pud)		pte_write(pud_pte(pud))

static inline int pud_bad(pud_t pud)
{
	if (radix_enabled())
		return radix__pud_bad(pud);
	return hash__pud_bad(pud);
}


#define pgd_write(pgd)		pte_write(pgd_pte(pgd))
static inline void pgd_set(pgd_t *pgdp, unsigned long val)
{
	*pgdp = __pgd(val);
}

static inline void pgd_clear(pgd_t *pgdp)
{
	*pgdp = __pgd(0);
}

static inline int pgd_none(pgd_t pgd)
{
	return !pgd_raw(pgd);
}

static inline int pgd_present(pgd_t pgd)
{
	return !pgd_none(pgd);
}

static inline pte_t pgd_pte(pgd_t pgd)
{
	return __pte_raw(pgd_raw(pgd));
}

static inline pgd_t pte_pgd(pte_t pte)
{
	return __pgd_raw(pte_raw(pte));
}

static inline int pgd_bad(pgd_t pgd)
{
	if (radix_enabled())
		return radix__pgd_bad(pgd);
	return hash__pgd_bad(pgd);
}

extern struct page *pgd_page(pgd_t pgd);

/* Pointers in the page table tree are physical addresses */
#define __pgtable_ptr_val(ptr)	__pa(ptr)

#define pmd_page_vaddr(pmd)	__va(pmd_val(pmd) & ~PMD_MASKED_BITS)
#define pud_page_vaddr(pud)	__va(pud_val(pud) & ~PUD_MASKED_BITS)
#define pgd_page_vaddr(pgd)	__va(pgd_val(pgd) & ~PGD_MASKED_BITS)

#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & (PTRS_PER_PGD - 1))
#define pud_index(address) (((address) >> (PUD_SHIFT)) & (PTRS_PER_PUD - 1))
#define pmd_index(address) (((address) >> (PMD_SHIFT)) & (PTRS_PER_PMD - 1))
#define pte_index(address) (((address) >> (PAGE_SHIFT)) & (PTRS_PER_PTE - 1))

/*
 * Find an entry in a page-table-directory.  We combine the address region
 * (the high order N bits) and the pgd portion of the address.
 */

#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

#define pud_offset(pgdp, addr)	\
	(((pud_t *) pgd_page_vaddr(*(pgdp))) + pud_index(addr))
#define pmd_offset(pudp,addr) \
	(((pmd_t *) pud_page_vaddr(*(pudp))) + pmd_index(addr))
#define pte_offset_kernel(dir,addr) \
	(((pte_t *) pmd_page_vaddr(*(dir))) + pte_index(addr))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while(0)

/* to find an entry in a kernel page-table-directory */
/* This now only contains the vmalloc pages */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pud_ERROR(e) \
	pr_err("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

void pgtable_cache_add(unsigned shift, void (*ctor)(void *));
void pgtable_cache_init(void);

static inline int map_kernel_page(unsigned long ea, unsigned long pa,
				  unsigned long flags)
{
	if (radix_enabled()) {
#if defined(CONFIG_PPC_RADIX_MMU) && defined(DEBUG_VM)
		unsigned long page_size = 1 << mmu_psize_defs[mmu_io_psize].shift;
		WARN((page_size != PAGE_SIZE), "I/O page size != PAGE_SIZE");
#endif
		return radix__map_kernel_page(ea, pa, __pgprot(flags), PAGE_SIZE);
	}
	return hash__map_kernel_page(ea, pa, flags);
}

static inline int __meminit vmemmap_create_mapping(unsigned long start,
						   unsigned long page_size,
						   unsigned long phys)
{
	if (radix_enabled())
		return radix__vmemmap_create_mapping(start, page_size, phys);
	return hash__vmemmap_create_mapping(start, page_size, phys);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline void vmemmap_remove_mapping(unsigned long start,
					  unsigned long page_size)
{
	if (radix_enabled())
		return radix__vmemmap_remove_mapping(start, page_size);
	return hash__vmemmap_remove_mapping(start, page_size);
}
#endif
struct page *realmode_pfn_to_page(unsigned long pfn);

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte_raw(pmd_raw(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd_raw(pte_raw(pte));
}

static inline pte_t *pmdp_ptep(pmd_t *pmd)
{
	return (pte_t *)pmd;
}
#define pmd_pfn(pmd)		pte_pfn(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
#define pmd_soft_dirty(pmd)    pte_soft_dirty(pmd_pte(pmd))
#define pmd_mksoft_dirty(pmd)  pte_pmd(pte_mksoft_dirty(pmd_pte(pmd)))
#define pmd_clear_soft_dirty(pmd) pte_pmd(pte_clear_soft_dirty(pmd_pte(pmd)))
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif /* CONFIG_NUMA_BALANCING */

#define __HAVE_ARCH_PMD_WRITE
#define pmd_write(pmd)		pte_write(pmd_pte(pmd))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern pmd_t pfn_pmd(unsigned long pfn, pgprot_t pgprot);
extern pmd_t mk_pmd(struct page *page, pgprot_t pgprot);
extern pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot);
extern void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t pmd);
extern void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
				 pmd_t *pmd);
extern int hash__has_transparent_hugepage(void);
static inline int has_transparent_hugepage(void)
{
	if (radix_enabled())
		return radix__has_transparent_hugepage();
	return hash__has_transparent_hugepage();
}
#define has_transparent_hugepage has_transparent_hugepage

static inline unsigned long
pmd_hugepage_update(struct mm_struct *mm, unsigned long addr, pmd_t *pmdp,
		    unsigned long clr, unsigned long set)
{
	if (radix_enabled())
		return radix__pmd_hugepage_update(mm, addr, pmdp, clr, set);
	return hash__pmd_hugepage_update(mm, addr, pmdp, clr, set);
}

static inline int pmd_large(pmd_t pmd)
{
	return !!(pmd_raw(pmd) & cpu_to_be64(_PAGE_PTE));
}

static inline pmd_t pmd_mknotpresent(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) & ~_PAGE_PRESENT);
}
/*
 * For radix we should always find H_PAGE_HASHPTE zero. Hence
 * the below will work for radix too
 */
static inline int __pmdp_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pmd_t *pmdp)
{
	unsigned long old;

	if ((pmd_raw(*pmdp) & cpu_to_be64(_PAGE_ACCESSED | H_PAGE_HASHPTE)) == 0)
		return 0;
	old = pmd_hugepage_update(mm, addr, pmdp, _PAGE_ACCESSED, 0);
	return ((old & _PAGE_ACCESSED) != 0);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pmd_t *pmdp)
{

	if ((pmd_raw(*pmdp) & cpu_to_be64(_PAGE_WRITE)) == 0)
		return;

	pmd_hugepage_update(mm, addr, pmdp, _PAGE_WRITE, 0);
}

static inline int pmd_trans_huge(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_trans_huge(pmd);
	return hash__pmd_trans_huge(pmd);
}

#define __HAVE_ARCH_PMD_SAME
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	if (radix_enabled())
		return radix__pmd_same(pmd_a, pmd_b);
	return hash__pmd_same(pmd_a, pmd_b);
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_mkhuge(pmd);
	return hash__pmd_mkhuge(pmd);
}

#define __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
extern int pmdp_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp,
				 pmd_t entry, int dirty);

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
extern int pmdp_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long address, pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pmdp_huge_get_and_clear(mm, addr, pmdp);
	return hash__pmdp_huge_get_and_clear(mm, addr, pmdp);
}

static inline pmd_t pmdp_collapse_flush(struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pmdp_collapse_flush(vma, address, pmdp);
	return hash__pmdp_collapse_flush(vma, address, pmdp);
}
#define pmdp_collapse_flush pmdp_collapse_flush

#define __HAVE_ARCH_PGTABLE_DEPOSIT
static inline void pgtable_trans_huge_deposit(struct mm_struct *mm,
					      pmd_t *pmdp, pgtable_t pgtable)
{
	if (radix_enabled())
		return radix__pgtable_trans_huge_deposit(mm, pmdp, pgtable);
	return hash__pgtable_trans_huge_deposit(mm, pmdp, pgtable);
}

#define __HAVE_ARCH_PGTABLE_WITHDRAW
static inline pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm,
						    pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pgtable_trans_huge_withdraw(mm, pmdp);
	return hash__pgtable_trans_huge_withdraw(mm, pmdp);
}

#define __HAVE_ARCH_PMDP_INVALIDATE
extern void pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_HUGE_SPLIT_PREPARE
static inline void pmdp_huge_split_prepare(struct vm_area_struct *vma,
					   unsigned long address, pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pmdp_huge_split_prepare(vma, address, pmdp);
	return hash__pmdp_huge_split_prepare(vma, address, pmdp);
}

#define pmd_move_must_withdraw pmd_move_must_withdraw
struct spinlock;
static inline int pmd_move_must_withdraw(struct spinlock *new_pmd_ptl,
					 struct spinlock *old_pmd_ptl)
{
	if (radix_enabled())
		return false;
	/*
	 * Archs like ppc64 use pgtable to store per pmd
	 * specific information. So when we switch the pmd,
	 * we should also withdraw and deposit the pgtable
	 */
	return true;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_PGTABLE_H_ */
