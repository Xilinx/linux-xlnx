/*
 * include/asm-microblaze/pgtable.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

#define pgd_present(pgd)	(1)		/* pages are always present on NO_MM */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)
#define	pmd_offset(a, b)	((void *) 0)

#define PAGE_NONE		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_SHARED		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_COPY		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_READONLY		__pgprot(0)	/* these mean nothing to NO_MM */
#define PAGE_KERNEL		__pgprot(0)	/* these mean nothing to NO_MM */

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ,off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#ifndef __ASSEMBLY__
static inline int pte_file(pte_t pte) { return 0; }
#endif

#define ZERO_PAGE(vaddr)	({ BUG(); NULL; })

#define swapper_pg_dir ((pgd_t *) NULL)

#define pgtable_cache_init()	do {} while(0)

#endif /* _ASM_PGTABLE_H */
