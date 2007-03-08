#ifndef _NIOS2NOMMU_CACHEFLUSH_H
#define _NIOS2NOMMU_CACHEFLUSH_H

/*
 * Ported from m68knommu.
 *
 * (C) Copyright 2003, Microtronix Datacom Ltd.
 * (C) Copyright 2000-2002, Greg Ungerer <gerg@snapgear.com>
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
#include <linux/mm.h>

extern void cache_push (unsigned long vaddr, int len);
extern void dcache_push (unsigned long vaddr, int len);
extern void icache_push (unsigned long vaddr, int len);
extern void cache_push_all (void);
extern void cache_clear (unsigned long paddr, int len);

#define flush_cache_all()			__flush_cache_all()
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	cache_push(start, end - start)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_dcache_range(start,end)		dcache_push(start, end - start)
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_icache_range(start,end)		cache_push(start, end - start)
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)


extern inline void __flush_cache_all(void)
{
	cache_push_all();
}

#endif /* _NIOS2NOMMU_CACHEFLUSH_H */
