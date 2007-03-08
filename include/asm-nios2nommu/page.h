/*
 * Copyright (C) 2004, Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _NIOS2_PAGE_H
#define _NIOS2_PAGE_H

/* copied from m68knommu arch */

/* PAGE_SHIFT determines the page size */

#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#include <asm/setup.h>

#if PAGE_SHIFT < 13
#define THREAD_SIZE (8192)
#else
#define THREAD_SIZE PAGE_SIZE
#endif
 
#ifndef __ASSEMBLY__
 
#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

extern unsigned long memory_start;
extern unsigned long memory_end;

#endif /* !__ASSEMBLY__ */
#include <asm/nios.h>
#define PAGE_OFFSET		((int)(nasys_program_mem))

#ifndef __ASSEMBLY__

#define __pa(vaddr)		virt_to_phys((void *)(vaddr))
#define __va(paddr)		phys_to_virt((unsigned long)(paddr))

#define MAP_NR(addr)		(((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT)

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_to_page(addr)	(mem_map + (((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT))
#define page_to_virt(page)	((((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET)
#define VALID_PAGE(page)	(((page) - mem_map) < max_mapnr)

#define pfn_to_page(pfn)	virt_to_page(pfn_to_virt(pfn))
#define page_to_pfn(page)	virt_to_pfn(page_to_virt(page))
#define pfn_valid(pfn)	        ((pfn) < max_mapnr)

#define	virt_addr_valid(kaddr)	(((void *)(kaddr) >= (void *)PAGE_OFFSET) && \
				((void *)(kaddr) < (void *)memory_end))

#ifdef CONFIG_NO_KERNEL_MSG
#define	BUG_PRINT()
#else
#define	BUG_PRINT() printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__)
#endif

#ifdef na_cpu_oci_core
#define BUG_PANIC()	asm volatile ("break") /* drop to debugger */
#else
// #define BUG_PANIC()	while(1)
#define BUG_PANIC()	panic("BUG!")
#endif

#endif /* __ASSEMBLY__ */

#include <asm-generic/page.h>

#endif /* __KERNEL__ */

#endif /* _NIOS2_PAGE_H */
