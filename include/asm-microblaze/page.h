/*
 * include/asm-microblaze/page.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_PAGE_H
#define _ASM_PAGE_H

#include <linux/config.h>
#include <linux/pfn.h>

/* PAGE_SHIFT determines the page size */

#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#include <asm/setup.h>
#include <asm/xparameters.h>

#ifndef __ASSEMBLY__

#define get_user_page(vaddr)			__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)		free_page(addr)

#define clear_page(pgaddr)			memset((pgaddr), 0, PAGE_SIZE)
#define copy_page(to,from)			memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(pgaddr, vaddr, page)	memset((pgaddr), 0, PAGE_SIZE)
#define copy_user_page(vto, vfrom, vaddr, topg)	memcpy((vto), (vfrom), PAGE_SIZE)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long	pte;	} pte_t;
typedef struct { unsigned long	ste[64];} pmd_t;
typedef struct { pmd_t		pue[1]; } pud_t;
typedef struct { pud_t		pge[1];	} pgd_t;
typedef struct { unsigned long	pgprot;	} pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).ste[0])
#define pud_val(x)	((x).pue[0])
#define pgd_val(x)	((x).pge[0])
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#if defined(CONFIG_SP3E)
#define DDR_SDRAM_BASEADDR (XPAR_DDR_SDRAM_32MX16_MEM0_BASEADDR)
#define DDR_SDRAM_HIGHADDR (XPAR_DDR_SDRAM_32MX16_MEM0_HIGHADDR)
#else
#define DDR_SDRAM_BASEADDR (XPAR_DDR_SDRAM_64MX32_MEM0_BASEADDR)
#define DDR_SDRAM_HIGHADDR (XPAR_DDR_SDRAM_64MX32_MEM0_HIGHADDR)
#endif

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr) + PAGE_SIZE - 1) & PAGE_MASK)

#define PAGE_OFFSET		(DDR_SDRAM_BASEADDR)

/**
 * Conversions for virtual address, physical address, pfn, and struct
 * page are defined in the following files.
 *
 *   virt -+
 *	   | asm-microblaze/page.h
 *   phys -+
 *         | linux/pfn.h
 *    pfn -+
 *         | asm-generic/memory_model.h
 *   page -+
 *
 */

#define __pa(vaddr)		((unsigned long) (vaddr))
#define __va(paddr)		((void *) (paddr))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#define virt_to_pfn(vaddr)	(phys_to_pfn((__pa(vaddr))))
#define pfn_to_virt(pfn)	__va(pfn_to_phys((pfn)))

#define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))

#define page_to_phys(page)	(pfn_to_phys(page_to_pfn(page)))
#define phys_to_page(paddr)	(pfn_to_page(phys_to_pfn(paddr)))

#define pfn_valid(pfn)		((pfn) >= min_low_pfn && (pfn) <= max_mapnr)
#define	virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))

#define ARCH_PFN_OFFSET		(DDR_SDRAM_BASEADDR >> PAGE_SHIFT)

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* _ASM_PAGE_H */
