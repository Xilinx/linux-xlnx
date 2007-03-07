/*
 * include/asm-microblaze/cacheflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

/*
 * FIXME!!!
 */
#define flush_cache_all()			do {} while(0)
#define flush_cache_mm(mm)			do {} while(0)

#define flush_cache_range(mm, start, end)	do {} while(0)
#define flush_cache_page(vma, vmaddr, pfn)	do {} while(0)
#define flush_cache_vmap(start, end)		do {} while(0)
#define flush_cache_vunmap(start, end)		do {} while(0)

#define flush_dcache_range(start,len)		do {} while(0)
#define flush_dcache_page(page)			do {} while(0)
#define flush_dcache_mmap_lock(mapping)		do {} while(0)
#define flush_dcache_mmap_unlock(mapping)	do {} while(0)

#define flush_icache_range(start,len)		do {} while(0)
#define flush_icache_page(vma,pg)		do {} while(0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len)	\
	memcpy((dst), (src), (len))

#define copy_from_user_page(vma, page, vaddr, dst, src, len)	\
	memcpy((dst), (src), (len))

#endif /* _ASM_CACHEFLUSH_H */
