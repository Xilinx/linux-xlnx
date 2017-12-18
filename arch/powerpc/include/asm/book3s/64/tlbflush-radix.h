#ifndef _ASM_POWERPC_TLBFLUSH_RADIX_H
#define _ASM_POWERPC_TLBFLUSH_RADIX_H

struct vm_area_struct;
struct mm_struct;
struct mmu_gather;

static inline int mmu_get_ap(int psize)
{
	return mmu_psize_defs[psize].ap;
}

extern void radix__flush_hugetlb_tlb_range(struct vm_area_struct *vma,
					   unsigned long start, unsigned long end);
extern void radix__flush_tlb_range_psize(struct mm_struct *mm, unsigned long start,
					 unsigned long end, int psize);
extern void radix__flush_pmd_tlb_range(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end);
extern void radix__flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end);

extern void radix__local_flush_tlb_mm(struct mm_struct *mm);
extern void radix__local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix__local_flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr);
extern void radix__local_flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
					      int psize);
extern void radix__tlb_flush(struct mmu_gather *tlb);
#ifdef CONFIG_SMP
extern void radix__flush_tlb_mm(struct mm_struct *mm);
extern void radix__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix__flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr);
extern void radix__flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
					int psize);
#else
#define radix__flush_tlb_mm(mm)		radix__local_flush_tlb_mm(mm)
#define radix__flush_tlb_page(vma,addr)	radix__local_flush_tlb_page(vma,addr)
#define radix__flush_tlb_page_psize(mm,addr,p) radix__local_flush_tlb_page_psize(mm,addr,p)
#define radix__flush_tlb_pwc(tlb, addr)	radix__local_flush_tlb_pwc(tlb, addr)
#endif
extern void radix__flush_tlb_lpid_va(unsigned long lpid, unsigned long gpa,
				     unsigned long page_size);
extern void radix__flush_tlb_lpid(unsigned long lpid);
extern void radix__flush_tlb_all(void);
#endif
